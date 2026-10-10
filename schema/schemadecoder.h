// =====================================================================
//  schemadecoder.{h,cpp} -- data-driven packet decoder.
//
//  Decodes a frame purely from an XML schema (kavach.xml), so adding a
//  field or a whole struct is an edit to the XML, not to C++.  Output is
//  the same QVector<FieldRow> the rest of DLConsole uses.
//
//  This C++ engine MIRRORS the validated reference engine (schema/engine.py).
//  engine.py is the gate: validate every schema edit there (validate_slrp.py)
//  before trusting this. SLRP is at full parity with the kschema tables:
//    - msb/lsb cursors, signed fields, units
//    - enums: exact + range(formula v*K/v+K/v-K) + %v/%f labels + default
//    - id/name capture (every field addressable by name in when/count)
//    - when: "id OP literal" and "id in LO..HI"
//    - repeat with per-field templates (tmpl) and hidden fields (hide)
//    - abs-location roles (seglen/start/len/taghop) from a ref RFID abs_loc
//    - byte-aligned sub-packet dispatch + MAC_CODE / unparsed tail
//    - format="name" hook for composite fields (sigInfo)
//
//  Build note: uses Qt's DOM parser -> add  QT += xml  to DLConsole.pro.
// =====================================================================
#ifndef SCHEMADECODER_H
#define SCHEMADECODER_H

#include <QString>
#include <QVector>
#include <QHash>
#include <QByteArray>
#include <QDomDocument>
#include <QDomElement>
#include <functional>

#include "capturedecoder.h"   // FieldRow { QString field; QString value; }

namespace Schema {

// The captype tokens a <packet match="..."> answers to.
//
// A packet may name more than one, comma-separated —
//
//     match="captype==arp,arprecv"
//
// — because two capture tokens can be the same packet arriving by different
// routes. ARP is the case that forced it: what a loco sends and what it
// receives from another loco are the same 29 bytes behind headers of
// different lengths, and giving each token its own <packet> would mean two
// copies of twenty-one field definitions that have to stay identical
// forever. They would not. Anything that is genuinely a different packet
// still gets its own element.
QStringList captypeTokens(const QString &match);

class Decoder
{
public:
    Decoder() = default;

    bool load(const QString &xmlPath, QString *err = nullptr);
    bool isLoaded() const { return m_loaded; }

    // Counts of what the last successful load() produced. Used by the
    // reload report: "loaded" is not the same as "loaded what you meant",
    // and a schema that parses but yields zero packets is a silent
    // catastrophe otherwise.
    int packetCount() const { return m_packets.size(); }
    int structCount() const { return m_structs.size(); }
    int enumCount()   const { return m_enums.size(); }
    // A coded value as the decoder shows it: "1 (Duplicate Tag)", "N/A", or
    // the bare number when the enum does not name it (session 195: public,
    // for the tag builder's choice lists).
    QString enumLabel(const QString &enumName, qint64 v) const;
    QStringList packetNames() const;

    // Every field name the schema can produce, across all packets and structs,
    // sorted and de-duplicated.
    //
    // For offering a choice rather than for decoding. What a session has
    // actually carried is a much shorter list than what the equipment can
    // send, and a chooser built only from the traffic seen so far cannot offer
    // the field an operator is waiting to see appear — which is the field they
    // most want to pin.
    QStringList allFieldNames() const;

    // The same names, split by the packet that produces them.
    //
    // allFieldNames() answers "what can the equipment send", which is the
    // right question when the operator already knows the name. It is the
    // wrong shape for BROWSING: five hundred names in one alphabetical run
    // is a list nobody reads to the end, and thirty-five of those names
    // occur in more than one packet, so the flat list cannot even say which
    // of the five FRAME_NUMs is meant.
    //
    // One entry per (packet, captype token) pair rather than per packet.
    // ARP answers to two tokens — what a loco sends and what it receives —
    // and a pin narrows on the TOKEN, because that is what a capture line
    // carries and what PinBoard::observe compares against. A chooser
    // offering the packet NAME would produce pins that silently never match:
    // "LOCO_SOS" is a packet, "lsos" is what arrives.
    struct PacketFields {
        QString     packet;    // <packet name=…>, e.g. "LOCO_SOS"
        QString     captype;   // one token from match=, e.g. "lsos"
        QStringList fields;    // sorted, de-duplicated
    };
    QVector<PacketFields> fieldsByCaptype() const;

    // The schema's own condition language — "A==1", "A in 3..7",
    // "A not in 0..0", "A & 7 == 3" — evaluated against decoded values.
    //
    // Public so the reject rules can use the SAME expressions the schema
    // uses on <field when=>. One condition language in the program, one
    // implementation: a second dialect would be one more thing to learn and
    // one more thing to drift from this one.
    static bool conditionHolds(const QString &when,
                               const QHash<QString, qint64> &values);

    // Well-formedness of one condition against the grammar above. Returns
    // "" when it parses, otherwise why it does not; *name receives the field
    // it tests. load() runs this over every when=/test= and refuses the
    // schema on the first failure. The reject rules use it the same way.
    static QString checkCondition(const QString &when, QString *name = nullptr);

    // Register a value-meaning hook for <eventstream meaning="name">.
    // Signature: (eid, value) -> display string ("" = show the number only).
    void registerMeaning(const QString &name,
                         std::function<QString(int, qint64)> fn) { m_meanings.insert(name, fn); }

    // Register a CRC algorithm for <crc algo="name">. Signature: (data, from, len) -> crc.
    void registerCrc(const QString &name,
                     std::function<quint32(const QByteArray &, int, int)> fn) { m_crcs.insert(name, fn); }

    bool handles(const QByteArray &frame, const QString &captype = {}) const;

    // Decode a frame into Field|Value rows. Empty if no packet matches.
    // captype is the capture @token (e.g. "slrp", "aap"); when given, packets
    // are selected by match="captype==X". tagLoc maps a reference RFID id ->
    // its absolute track location (metres) for abs-location suffixes.
    // bodyOffsetOverrideBits >= 0 overrides the packet's body_offset attribute
    // — used to self-verify a built ARP/LSRP frame, whose send format has no
    // in-frame envelope (offset 0), unlike the captured format (offset 80).
    // `rawValues`, when given, receives every field's DECODED NUMERIC value
    // keyed by field name — the same map the decoder already builds to
    // resolve counts and conditions, which until now it threw away.
    //
    // Rules are evaluated on these, not on the rendered rows. A row's value
    // is a display string: "2 (Reverse)", "12.5 m", an enum label. Comparing
    // a reject rule against that would mean parsing the presentation back
    // into a number, and would break the day a label is reworded.
    QVector<FieldRow> decode(const QByteArray &frame,
                             const QHash<int, qint64> &tagLoc = {},
                             const QString &captype = {},
                             int bodyOffsetOverrideBits = -1,
                             QHash<QString, qint64> *rawValues = nullptr) const;

private:
    // Bit cursor over a frame.
    //
    //   b      – frame bytes
    //   pos    – current bit position
    //   limit  – logical end-of-body in bits, derived from SCHEMA attributes
    //            (trailer_bytes, subpacket lengths). Untrusted.
    //   nbytes – the frame's REAL size. `limit` is schema-derived and can be
    //            wrong or hostile; `nbytes` is ground truth and is what
    //            actually keeps take() inside the allocation. Both are
    //            checked, because they mean different things: exceeding
    //            `limit` is a normal end-of-body condition (zero-fill),
    //            exceeding `nbytes` is a bug or a bad schema.
    struct Cursor {
        const uchar *b; int pos; int limit; bool msb; int nbytes;
        quint32 take(int n);
    };

    // running absolute-location state, composed from the header
    struct Geo { bool known=false; qint64 refAbs=0; qint64 blockAbs=0; int travel=1; };
    // running accumulators that persist across the entries of one <repeat>
    struct Grp { qint64 seg=0; qint64 tag=0; };

    struct Ctx { QHash<QString, qint64> vals; };  // field name (and id) -> value

    // One typed field read, shared by the flat / entry / expand walkers so
    // that all three agree on what type="float|double|char" means. Before
    // this existed only walkFlat understood a typed field, so a float inside
    // a <repeat> was read as a 0-bit integer — a silent divergence from
    // engine.py, which routes all three through _read().
    //
    // Reals are NOT written into Ctx: Ctx is integer-valued, and rounding a
    // double into it would make when=/count= mean something different here
    // than in the reference engine. Do not condition on a real field.
    enum class Kind { Int, Real, Text };
    static Kind readTyped(const QDomElement &fld, Cursor &c,
                          qint64 *iv, double *dv, QString *sv);

    // Wire width of a field in bits, typed fields included. Used by the
    // expand-repeat break guard, which must know an entry's size before
    // reading it.
    static int fieldBits(const QDomElement &fld);

    const QDomElement *packetFor(const QByteArray &frame, const QString &captype) const;

    void walkFlat(QDomElement node, Cursor &c, Ctx &ctx,
                  QVector<FieldRow> &rows, const Geo &geo) const;
    QString walkEntry(QDomElement rep, Cursor &c, Ctx &ctx,
                      Grp &grp, const Geo &geo, const QString &sep) const;
    // `depth` guards against a schema whose structs reference each other in
    // a cycle, which would otherwise recurse until the stack is gone.
    void walkStruct(const QString &name, Cursor &c, Ctx ctx,
                    QVector<FieldRow> &rows, const Geo &geo,
                    int depth = 0) const;
    static constexpr int kMaxStructDepth = 32;

    QString bareVal(const QDomElement &fld, qint64 v) const;  // entry token value
    QString fullVal(const QDomElement &fld, qint64 v) const;  // flat row value
    static bool condOk(const QString &when, const Ctx &ctx);
    static bool validateConditions(const QDomElement &root, QString *err);
    static QString fmtMap(const QDomElement &m, qint64 v, qint64 f);
    static qint64  formula(const QString &expr, qint64 v);
    static QString composite(const QString &name, qint64 v);  // sigInfo, ...
    static QString rowTmpl(const QString &tmpl, const Ctx &ctx); // computed <row>

    QDomDocument m_doc;
    QHash<QString, QDomElement> m_structs;
    QHash<QString, QDomElement> m_enums;
    QVector<QDomElement>        m_packets;

    struct EventDef { int bytes = 1; bool isSigned = false; QString name; };
    QHash<QString, QHash<int, EventDef>>           m_eventtables;  // table -> id -> def
    QHash<QString, QStringList>                    m_flagtables;   // table -> [flag name]
    QHash<QString, std::function<QString(int, qint64)>> m_meanings; // hook name -> fn
    QHash<QString, std::function<quint32(const QByteArray &, int, int)>> m_crcs; // algo -> fn

    bool m_loaded = false;
};

} // namespace Schema

#endif // SCHEMADECODER_H
