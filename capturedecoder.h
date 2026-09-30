#ifndef CAPTUREDECODER_H
#define CAPTUREDECODER_H

// =============================================================================
//  CaptureDecoder
//  -----------------------------------------------------------------------------
//  Parses the loco capture stream emitted by the firmware (ui_capture.c):
//
//      @<type>_<loco>_<ctrl> <YYYY-MM-DDTHH:MM:SS> <seq> <hex bytes...>
//
//  Each such line arrives as LogEntry::text. This unit turns the line into a
//  typed CaptureLine, verifies the frame CRC where one exists (JAMCRC, the
//  firmware's crcFast), and decodes the payloads we have verified field specs
//  for. describe() returns a full Field|Value breakdown per type:
//    aap/slrp/arp/lsrp : Annexure-C radio frames, decoded field-by-field
//                        (incl. all slrp sub-packets: MA, SSP, gradient, LC,
//                        turnout, tag-linking, track-condition, TSR), golden-
//                        vector-verified MSB-first per C.3.2.13.
//    nmshlth/nmsfault  : Annexure-G NMS payloads (event + fault tables).
//    rfid              : Annexure-D tag (all types) + CRC-30.
//    dmi               : regular DMI frame (LSB-first packed struct).
//    ccsys/dlsys       : controller / logger self-status.
//  It carries no Qt-widget dependency so it can be unit-tested independently
//  of the live console view.
//
//  Verified frame recipes (see field listing / nms_data_manager.c):
//    aap, slrp      raw Anx-C radio   : JAMCRC over [0 : len-4], stored BE
//    arp, lsrp      access-req frame  : JAMCRC over [10: len-4], stored LE
//    nmsflt,nmshlth NMS (Anx-G)       : JAMCRC over [10: len-4], stored BE
//    nmsrssi        NMS (Anx-G)       : CRC FAILS — firmware builder bug
//    linfo          LOCO_INFO body    : JAMCRC(init 0) over [0 : len-4], stored LE
//    uba            Target_Internal   : no CRC, no header — a raw packed
//                                       struct of little-endian doubles
//    dlt,dmi,biu,brk                  : no verifiable frame CRC
// =============================================================================

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QString>
#include <QVector>

enum class CapType {
    Unknown = 0,
    AAP, ARP, SLRP, LSRP,
    ArpRecv,                  // @arprecv an ARP received FROM another loco:
                              // the same packet behind an 8-byte header, the
                              // 2-byte station id being absent on this path
    NmsFault, NmsHlth, NmsRssi,
    Dlt, Dmi, Biu, Brk,
    Rfid,
    Dip1, Dip2, Dop1, Dop2,
    CcSys, DlSys,
    Aep,                      // C.4.5 Additional Emergency Packet (no header, MSB-first)
    Linfo,                    // @linfo LOCO_INFO config struct (flat LE, no header/CRC)
    AuthKeys,                 // @authkeys authentication key sets from the LCU
    Random,                   // @random_num RANDOM_NUMBER pair (loco + stn)
    UBA,                      // @uba Target_Internal: a target + its braking
                              // curve (flat LE doubles, no header/CRC)
    Speed,                    // @speed STRUCT_SENSOR_SPEED_DATA, 14 B LE
    AnalogTop, AnalogBottom   // @analog_top / @analog_bottom
                              // STRUCT_ANALOG_SENSOR_DATA, 6 floats, 24 B LE, no CRC
};

enum class CapDir { Unknown = 0, In, Out };   // provisional per-type direction

struct NmsEventDef { int id; int size; bool isSigned; const char *name; };

struct CaptureLine {
    bool       valid       = false;   // true only for a well-formed @-line
    CapType    type        = CapType::Unknown;
    QString    typeToken;
    int        locoId      = -1;
    int        ctrlId      = -1;
    QDateTime  rtc;                    // loco RTC, 1 s resolution
    quint32    seq         = 0;
    QByteArray bytes;                  // full frame (message header included)
    bool       crcChecked  = false;   // a CRC recipe applies to this type
    bool       crcOk       = false;   // result of that check

    QString key() const { return QStringLiteral("%1_%2").arg(locoId).arg(ctrlId); }
};

struct NmsHealthEvent { int id; qint64 value; QString name; };
struct NmsFaultEntry  { int moduleId; quint8 codeType; int faultId; };

// A single active fault from an NMS fault packet, fully named for display.
struct ActiveFaultInfo {
    int     subsystem = 0;     // reporting subsystem (module id b[27])
    int     moduleId  = 0;     // module the fault is on
    int     codeType  = 0;
    int     faultId   = 0;
    QString subsystemName;
    QString moduleName;
    QString faultName;
};

// Decoded Anx-D RFID tag (reader_id + 16-byte tag). All four tag types share
// the X0..X54 header (type, version, unique, absLoc, tinNom, tinRev) and the
// trailing CRC-30 (firmware crc30 over the first 13 LE bytes with the CRC field
// zeroed). The remaining bits are type-specific; only the fields belonging to
// `type` are populated. Layouts golden-vector-verified against the GGD-521 data
// sheets (Anx-D sheets 1-6) for all four types incl. duplicate tags.
struct RfidInfo {
    bool    valid    = false;
    int     readerId = -1;

    // --- common header (X0..X54), all types ---
    int     type = 0, version = 0, unique = 0;
    qint64  absLoc = 0;                 // X38-X16  (Absolute Location-1 for type 12)
    int     tinNom = 0, tinRev = 0;     // X46-39 / X54-47  (TIN-1 / TIN-2 for type 12)

    quint32 crcStored = 0, crcCalc = 0;
    bool    crcOk = false;

    // --- section type, 2-bit (types 9, 10, 12) ---
    // For type 12 these carry Section type-1 / Section type-2.
    int     sectNom = 0, sectRev = 0;

    // --- Normal tag (type 9) ---
    int     stnNom = 0, stnRev = 0;

    // --- placement, 4-bit (types 9 and 10; same enum, different bit offset) ---
    int     place = 0;

    // --- tag-duplication / comm flags ---
    // dup     : Y31  (Tag Duplication / "Tag Type" on type 12) — all types
    // commNom : Y32  (types 9, 10, 12;  Adjacent-line tag has no comm flags)
    // commRev : Y33  (types 9, 10, 12)
    int     dup = 0, commNom = 0, commRev = 0;

    // --- LC gate tag (type 10) ---
    int     lcApproach = 0, applDir = 0, gateId = 0, gateAlpha = 0,
            gateType = 0, distGate = 0, autoWhistle = 0, whistleType = 0;

    // --- Adjacent-line tag (type 11) ---
    int     adj[5] = { 0, 0, 0, 0, 0 };   // Adjacent Line-1..5 TIN

    // --- Adjustment/Junction tag (type 12) ---
    qint64  absLoc2 = 0;                   // Absolute Location-2
    int     dirCorr1 = 0, dirCorr2 = 0, locCorrType = 0;
};

// One row in a per-packet field table (Field | Value).
namespace Schema { class Decoder; }

// The one shared, lazily-loaded kavach.xml decoder, with all the meaning
// and CRC hooks already registered. Exposed so the field inspector uses
// the SAME instance the rest of the app decodes with — a second copy would
// be a second schema-version truth, and the two could disagree.
const Schema::Decoder &kavachSchema();

// Reload the shared schema from `path`, or from the built-in resource when
// `path` is empty. Returns false and fills `err` on failure — in which case
// the decoder is left EMPTY (load clears before parsing), not holding the
// previous schema.
//
// GUI thread only; see the note at the implementation.
bool reloadKavachSchema(const QString &path, QString *err = nullptr);

struct FieldRow {
    QString field;
    QString value;

    // Which bits of the frame produced this row, so the UI can highlight
    // the bytes a field came from. -1 means "not attributable" — a header,
    // a separator, a schema error, or a row produced by a decoder that
    // does not track provenance.
    //
    // Bits rather than bytes because the wire format is bit-packed: a
    // 3-bit flag and its 5-bit neighbour share a byte, and rounding to
    // byte granularity would highlight the wrong span for both.
    int bitOffset = -1;
    int bitLength = 0;

    bool hasSpan() const { return bitOffset >= 0 && bitLength > 0; }

    // Byte range covering the bit span, for hex highlighting.
    int byteStart() const { return bitOffset >= 0 ? bitOffset / 8 : -1; }
    int byteEnd()   const {
        return bitOffset >= 0 ? (bitOffset + bitLength - 1) / 8 : -1;
    }
};

// Structured SLRP look-ahead profile (the same data describe() renders as
// rows, but as numbers for drawing). All distances are in metres, AS DECODED
// from the sub-packets: i.e. relative to the SLRP profile reference
// (LAST_REF_RFID + DIST_PKT_START), NOT absolute track locations. Speeds are
// km/h, or -1 for "unknown / reserved".
struct SlrpProfile {
    bool valid = false;
    int  refRfid      = -1;   // LAST_REF_RFID
    int  distPktStart = 0;    // signed metres (profile start vs last ref)
    int  pktDir       = 0;    // PKT_DIR: 0 unident, 1 nominal, 2 reverse

    // REF_PROF_ID: which profile the lanes below belong to.
    //
    // The station sends the profile ONCE and then keeps sending movement
    // authorities against it — 3322 of the 3340 SLRP frames in replay/ carry
    // nothing but the MA sub-packet. The loco holds the profile it was given
    // and only discards it when this id changes to a different NON-ZERO value;
    // zero means "route ahead not known", and the firmware keeps the current
    // profile deliberately in that case.
    //
    // So a single frame is not a profile. See CaptureDecoder::carryProfile().
    int  refProfId    = -1;

    // Does this frame actually carry profile lanes, or is it MA-only?
    bool carriesLanes() const
    {
        return !ssp.isEmpty() || !grad.isEmpty() || !tsr.isEmpty()
            || !tags.isEmpty() || !cond.isEmpty();
    }

    // Movement authority
    bool haveMA   = false;
    int  maWrtSig = 0;        // MA_W_R_T_SIG (m, distance to authority end vs signal)
    int  authType = -1;       // 0..3
    int  authSpeed = -1;      // km/h when authType==1
    bool reqShorten = false;
    int  newMA    = 0;        // m, when reqShorten

    struct SpeedStep { int d; int a; int b; int c; bool classified; };  // km/h, -1 unknown
    QVector<SpeedStep> ssp;

    struct GradPt   { int d; bool uphill; int value; };
    QVector<GradPt> grad;

    struct TsrZone  { int id; int d; int len; int a; int b; int c; bool classified; };
    QVector<TsrZone> tsr;
    int tsrStatus = -1;

    struct TagLink  { int d; int tag; int flag; };
    QVector<TagLink> tags;

    struct TrackCond { int type; int sd; int len; };   // sd=start from ref, len=zone length (m)
    QVector<TrackCond> cond;
};

// Quick single-field indices for replay seek/jump (-1 = not present in type).
struct CaptureIndex {
    int frameNum  = -1;   // FRAME_NUM = seconds since midnight (aap/arp/slrp/lsrp)
    int locoMode  = -1;   // LOCO_MODE (arp/lsrp)
    int emergency = -1;   // EMERGENCY_STATUS (arp/lsrp)
    int rfidUid   = -1;   // RFID unique id
};

class SessionKeyStore;   // session 96: handed to describe() for the live MAC row

namespace CaptureDecoder {

// Returns valid=false if `text` is not an @-capture line.
CaptureLine parseLine(const QString &text);

// Field-by-field breakdown of a parsed frame for the per-type tab. Decodes
// semantically where we have a verified spec (NMS); for the bit-packed radio
// frames it lists the envelope, message header (arp/lsrp), CRC and the body
// as a raw block until the Anx-C bit-spec lands.
// `tagLoc` (optional) maps RFID unique -> abs_loc, used to annotate SLRP
// look-ahead elements with absolute locations. Pass nullptr to skip.
// `keySnapshotId` (optional) picks WHICH captured session-key set the live MAC
// row is checked against. 0 (the default) means "whatever is in force for this
// frame's loco", which is what the log view wants. The Decode Workbench passes
// a specific snapshot id when the operator has chosen one, so a frame can be
// tested against the material that was in force when it was sent rather than
// only against the newest.
// `rawValues`, when given, receives each decoded field's NUMERIC value, for
// callers that compare rather than display — the reject rules. The rendered
// rows carry display strings ("2 (Reverse)", "12.5 m"), and comparing a rule
// against those would mean parsing presentation back into a number.
// `keys` (session 96): the session-key store the SLRP "MAC (live)" row is
// checked against -- MainWindow's, fed by live traffic. nullptr = no MAC row
// (a replay, a recording, a panel that shows no MAC).
QVector<FieldRow> describe(const CaptureLine &c,
                           const QHash<int, qint64> *tagLoc = nullptr,
                           int keySnapshotId = 0,
                           QHash<QString, qint64> *rawValues = nullptr,
                           const SessionKeyStore *keys = nullptr);

// Structured SLRP look-ahead profile for the track view (empty .valid=false
// for non-SLRP frames). Reuses the same verified bit layout as describe().
SlrpProfile       profileOf(const CaptureLine &c);

// Merge a held profile with a newer frame, the way a loco does.
//
// `latest` is the frame being shown: it supplies the movement authority, the
// reference tag and the direction, all of which are in every SLRP frame.
// `held` is the profile last actually issued. Its lanes are carried into the
// result unless the new frame issues lanes of its own, or unless REF_PROF_ID
// has moved to a different non-zero value — which is the firmware's signal
// that the old profile has been discarded.
//
// Without this the look-ahead lanes are empty 99% of the time, because that
// is how often an SLRP frame carries nothing but a movement authority.
SlrpProfile carryProfile(const SlrpProfile &held, const SlrpProfile &latest);

// Track-condition type (4-bit) -> short name (mirrors kavach.xml tcType enum).
QString           tcTypeName(int t);

// Fully-named active faults from an NMS fault packet (empty for other types).
QVector<ActiveFaultInfo> faultsOf(const CaptureLine &c);

// JAMCRC: poly 0x04C11DB7, init 0, refin/refout true, xorout 0.
quint32 jamcrc(const QByteArray &d, int from, int len);

// Payload decoders (empty if the frame is too short or the wrong type).
QVector<NmsHealthEvent> decodeHealth(const QByteArray &b);
QVector<NmsFaultEntry>  decodeFault (const QByteArray &b);
RfidInfo                decodeRfid  (const QByteArray &frame);
CaptureIndex            indexOf     (const CaptureLine &c);

CapType     typeFromToken(const QString &token);
const char *typeLabel(CapType t);
CapDir      directionFor(CapType t);          // provisional TX/RX hint

} // namespace CaptureDecoder

#endif // CAPTUREDECODER_H
