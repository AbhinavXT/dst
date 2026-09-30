#ifndef SESSIONFILE_H
#define SESSIONFILE_H

// =============================================================================
//  Session file format (.dlr — "DL Console raw")
//  -----------------------------------------------------------------------------
//  A byte-exact archive of the datagrams that arrived on one (source_id,
//  kvchId) pair, written alongside the human-readable .log by the same
//  worker.
//
//  WHY THIS EXISTS
//    The rolling .log is a rendering. It holds the decoded text, the
//    severity the colour rules assigned at the time, and nothing else — the
//    original bytes are dropped. LogEntry::rawBytes only ever reached disk
//    through a manual JSON export, and the in-memory ring discards it after
//    the per-tab cap.
//
//    That makes the log's own stated purpose unachievable. Root-cause work
//    happens hours or days after the symptom, and by then you frequently
//    want to do something the text can't support: re-run a corrected
//    decoder, apply a schema field that didn't exist yet, check a CRC the
//    parser was computing over the wrong range, or feed the frames to the
//    ReplayWindow (which today can only replay .cap files that someone
//    remembered to record from the Loco Console).
//
//    Keeping the bytes costs roughly what the text costs — for typical
//    diagnostic traffic the wire form and the rendered line are within a
//    factor of two — and it is the difference between an archive you can
//    re-interrogate and one you can only re-read.
//
//  DESIGN CONSTRAINTS
//    1. Append-only, streamable. The writer never seeks; the reader never
//       needs the whole file in memory.
//    2. Torn-write tolerant. Power loss or SIGKILL mid-record must cost you
//       that one record, not the file. Every record carries a CRC-32 and the
//       reader stops cleanly at the first incomplete or corrupt one.
//    3. Self-describing. A .dlr found on its own, six months later, names
//       its own source, its creation time, and its version.
//    4. No dependency on Structures.h layout. The wire bytes are stored as
//       an opaque blob with an explicit length, so a future change to the
//       header struct does not invalidate old archives — the version field
//       tells a reader how to interpret them.
//
//  LAYOUT
//
//    File header — 32 bytes, once, at offset 0:
//
//      off  size  field
//      ---  ----  ---------------------------------------------------------
//        0     8  magic       "DLCONRAW"  (no NUL terminator)
//        8     2  version     uint16 LE — currently 1
//       10     2  headerLen   uint16 LE — 32; lets a v2 header grow without
//                             breaking v1 readers, which skip headerLen bytes
//       12     8  createdMs   int64  LE — UTC ms when the file was opened
//       20     1  sourceId    uint8     — from the tab key
//       21     1  reserved    zero
//       22     2  kvchId      uint16 LE — from the tab key
//       24     4  flags       uint32 LE — reserved, zero
//       28     4  reserved    zero
//
//    Record — repeated to end of file:
//
//      off  size  field
//      ---  ----  ---------------------------------------------------------
//        0     4  bodyLen     uint32 LE — bytes following THIS field,
//                             including the trailing CRC. Always
//                             10 + wireLen + 4.
//        4     8  arrivalMs   int64  LE — UTC ms, captured on the receiver
//                             thread when the datagram was read
//       12     2  wireLen     uint16 LE — length of the wire blob
//       14     N  wire        the complete datagram, byte for byte:
//                             STRUCT_MESSAGE_HEADER followed by payload
//     14+N     4  crc32       uint32 LE over bytes [4 .. 14+N), i.e. from
//                             arrivalMs through the end of `wire`
//
//    Multi-byte fields are little-endian throughout, matching the Kavach
//    wire convention the receiver already assumes.
//
//  WHAT IS DELIBERATELY NOT STORED
//    Severity, direction, colours, and the friendly name. All four are
//    derived — from colour_rules.json and friendly_names.csv, both of which
//    change over time. Storing them would freeze one moment's interpretation
//    into the archive and quietly defeat the point of keeping the bytes. A
//    loaded session is re-classified with the rules in force at load time.
// =============================================================================

#include <QByteArray>
#include <QtGlobal>

namespace SessionFile {

// ---- constants ------------------------------------------------------------

static const char   kMagic[8]      = { 'D','L','C','O','N','R','A','W' };
static constexpr int    kMagicLen  = 8;
static constexpr quint16 kVersion  = 1;
static constexpr int    kHeaderLen = 32;

// Fixed part of a record: bodyLen(4) + arrivalMs(8) + wireLen(2) + crc(4).
static constexpr int kRecordOverhead = 18;

// Sanity ceiling on a single record's wire blob. MAX_MSG_SIZE is 8000; this
// is the same bound expressed without dragging Constants.h in, and it stops
// a corrupt bodyLen from provoking a huge allocation in the reader.
static constexpr int kMaxWireBytes = 8192;

static constexpr const char *kExtension = ".dlr";

// ---- CRC-32 (IEEE 802.3, reflected, init/xorout 0xFFFFFFFF) --------------
//
// Self-contained on purpose. capturedecoder.cpp has CRC helpers, but they
// are protocol-specific and file-static, and the archive format should not
// acquire a dependency on the decoder it exists to outlive.

inline quint32 crc32(const char *data, int len, quint32 seed = 0xFFFFFFFFu)
{
    static quint32 table[256];
    static bool built = false;
    if (!built) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        built = true;   // single-threaded init: writer thread builds it first
    }
    quint32 c = seed;
    for (int i = 0; i < len; ++i) {
        c = table[(c ^ static_cast<quint8>(data[i])) & 0xFF] ^ (c >> 8);
    }
    return c;
}

inline quint32 crc32(const QByteArray &b, quint32 seed = 0xFFFFFFFFu)
{
    return crc32(b.constData(), b.size(), seed);
}

inline quint32 crc32Final(quint32 running) { return running ^ 0xFFFFFFFFu; }

// ---- little-endian scalar helpers ----------------------------------------
//
// Explicit byte assembly rather than memcpy of a struct: the format is
// defined in bytes, and this stays correct on a big-endian host without
// anyone having to remember to add a swap.

inline void putU16(char *p, quint16 v)
{
    p[0] = char(v & 0xFF);
    p[1] = char((v >> 8) & 0xFF);
}

inline void putU32(char *p, quint32 v)
{
    p[0] = char( v        & 0xFF);
    p[1] = char((v >>  8) & 0xFF);
    p[2] = char((v >> 16) & 0xFF);
    p[3] = char((v >> 24) & 0xFF);
}

inline void putI64(char *p, qint64 v)
{
    const quint64 u = static_cast<quint64>(v);
    for (int i = 0; i < 8; ++i) p[i] = char((u >> (8 * i)) & 0xFF);
}

inline quint16 getU16(const char *p)
{
    return quint16(quint8(p[0])) | (quint16(quint8(p[1])) << 8);
}

inline quint32 getU32(const char *p)
{
    return  quint32(quint8(p[0]))
         | (quint32(quint8(p[1])) <<  8)
         | (quint32(quint8(p[2])) << 16)
         | (quint32(quint8(p[3])) << 24);
}

inline qint64 getI64(const char *p)
{
    quint64 u = 0;
    for (int i = 0; i < 8; ++i) u |= quint64(quint8(p[i])) << (8 * i);
    return static_cast<qint64>(u);
}

// ---- file header ----------------------------------------------------------

struct FileHeader {
    quint16 version   = kVersion;
    quint16 headerLen = kHeaderLen;
    qint64  createdMs = 0;
    quint8  sourceId  = 0;
    quint16 kvchId    = 0;
    quint32 flags     = 0;
};

// Serialise a file header to exactly kHeaderLen bytes.
inline QByteArray encodeHeader(const FileHeader &h)
{
    QByteArray out(kHeaderLen, '\0');
    char *p = out.data();
    memcpy(p, kMagic, kMagicLen);
    putU16(p +  8, h.version);
    putU16(p + 10, h.headerLen);
    putI64(p + 12, h.createdMs);
    p[20] = char(h.sourceId);
    p[21] = 0;
    putU16(p + 22, h.kvchId);
    putU32(p + 24, h.flags);
    putU32(p + 28, 0);
    return out;
}

// Parse a file header. Returns false if the magic or version is wrong.
inline bool decodeHeader(const QByteArray &bytes, FileHeader *out)
{
    if (bytes.size() < kHeaderLen) return false;
    if (memcmp(bytes.constData(), kMagic, kMagicLen) != 0) return false;
    const char *p = bytes.constData();
    FileHeader h;
    h.version   = getU16(p +  8);
    h.headerLen = getU16(p + 10);
    h.createdMs = getI64(p + 12);
    h.sourceId  = quint8(p[20]);
    h.kvchId    = getU16(p + 22);
    h.flags     = getU32(p + 24);
    if (h.version == 0 || h.version > kVersion) return false;
    if (h.headerLen < kHeaderLen)               return false;
    if (out) *out = h;
    return true;
}

// ---- record ---------------------------------------------------------------

// Build the complete on-disk bytes for one record.
inline QByteArray encodeRecord(qint64 arrivalMs, const QByteArray &wire)
{
    const int wireLen = wire.size();
    const int bodyLen = 8 + 2 + wireLen + 4;      // arrivalMs + wireLen + wire + crc

    QByteArray out(4 + bodyLen, '\0');
    char *p = out.data();
    putU32(p, quint32(bodyLen));
    putI64(p + 4, arrivalMs);
    putU16(p + 12, quint16(wireLen));
    if (wireLen > 0) memcpy(p + 14, wire.constData(), wireLen);

    // CRC covers arrivalMs through the end of wire — everything the reader
    // will hand back, and nothing that is merely framing.
    const quint32 c = crc32Final(crc32(p + 4, 10 + wireLen));
    putU32(p + 14 + wireLen, c);
    return out;
}

}  // namespace SessionFile

#endif // SESSIONFILE_H
