#ifndef SCHEMAENCODER_H
#define SCHEMAENCODER_H
// =====================================================================
//  schemaencoder.{h,cpp} -- data-driven packet ENCODER.
//
//  The inverse of schemadecoder: it reads the SAME kavach.xml and packs
//  field values back into a wire frame, msb-first in spec order, so a
//  packet the decoder can read can also be built. This is the engine
//  behind the Packet Maker.
//
//  Scope: the grammar SLRP (and the other radio packets) need —
//    <field bits signed when enum format>, <pad>, and the
//    <subpackets>/<case struct> + <struct> dispatch with per-field
//    `when` gating. Groups / repeat / flags (used by ARP, DOP, LSRP
//    health) are not emitted yet; packet() reports them so the caller
//    can refuse rather than silently mis-build. Extending is additive.
//
//  It does NOT append the CRC or the CBC-MAC and does NOT know the
//  transport envelope — PacketBuilder owns assembly, because the CRC
//  region, the MAC coverage and the tail layout are per-regime and
//  belong with the code that also self-verifies the finished frame.
// =====================================================================
#include <QString>
#include <QStringList>
#include <QVector>
#include <QHash>
#include <QByteArray>
#include <QDomDocument>
#include <QDomElement>
#include <QPair>

namespace Schema {

// One buildable field (or pad) as the UI needs to render it.
struct FieldInfo {
    QString name;
    int     bits    = 0;
    bool    isSigned = false;
    bool    isPad   = false;
    QString enumName;      // non-empty => offer a choice list
    QString when;          // present only when this condition holds
};

struct CaseInfo { int type = 0; QString structName; };

// A <repeat> block inside a struct. `countField` is the <field> whose value is
// the number of rows; the builder sets it automatically. Each row carries the
// per-row `fields` (their own `when` conditions apply within the row).
struct RepeatInfo {
    QString name;
    QString countField;
    int     cmin = 0, cmax = 63;
    QVector<FieldInfo> fields;
};

// A struct's editable shape for the UI: scalar fields the operator sets, plus
// any repeats. Count fields are driven automatically and listed separately so
// the form can hide them.
struct StructLayout {
    bool ok = false;
    QVector<FieldInfo>  scalarFields;   // non-repeat, non-count fields (in order)
    QVector<RepeatInfo> repeats;
    QStringList         countFields;    // auto-driven; hidden from the form
};

// A packet's shape, for both encoding and driving the builder UI.
struct PacketInfo {
    bool    ok = false;
    QString name;
    QString wire;              // "msb-first" / "lsb-first"
    QString crc;               // e.g. "anxc_jamcrc" ("" => none)
    int     bodyOffsetBits = 0;
    QVector<FieldInfo> header; // flat fields (and pads) before any subpackets

    bool    hasSub = false;
    int     subTypeBits = 0, subLenBits = 0, reserveTail = 8;
    QVector<CaseInfo> cases;

    // Grammar this encoder does not emit yet, if the packet uses it. When
    // non-empty the caller should not attempt to build this packet.
    QStringList unsupported;
};

// One sub-packet instance the operator has added.
struct SubEntry {
    int type = 0;
    QHash<QString, qint64> values;   // scalar field name -> value
    // repeat name -> its rows (each row: field name -> value)
    QHash<QString, QVector<QHash<QString, qint64>>> repeats;
};

// What parseBody() reads back out of a raw buffer: exactly the inputs
// encodeBody() takes, so a parsed frame can be re-encoded byte-for-byte and
// then edited field by field. `tailBytes` is whatever the packet reserves
// after the last sub-packet (MAC + CRC for SLRP); it is NOT part of the
// rebuild, because PacketBuilder recomputes both.
struct ParsedPacket {
    bool    ok = false;
    QString error;
    QString captype;
    QHash<QString, qint64> header;
    QVector<SubEntry> subs;
    int     bodyBytes = 0;       // bytes consumed by header + sub-packets
    int     tailBytes = 0;       // reserved tail present in the buffer
    QStringList notes;           // non-fatal observations for the UI
};

class Encoder {
public:
    Encoder() = default;

    bool load(const QString &xmlPath, QString *err = nullptr);
    bool isLoaded() const { return m_loaded; }

    QStringList packetNames() const;              // captype tokens with a packet
    PacketInfo  packet(const QString &captype) const;
    QVector<FieldInfo> structFields(const QString &structName) const;
    StructLayout       structLayout(const QString &structName) const;

    // Enum helpers for the UI (exact-value labels only; range/formula enums
    // fall back to the raw number, which is still a valid entry).
    QVector<QPair<qint64, QString>> enumChoices(const QString &enumName) const;

    // Pack header + sub-packets into an msb-first body. Does NOT append MAC or
    // CRC. `header` supplies the flat field values (PKT_LENGTH included —
    // PacketBuilder sets it); unspecified fields default to 0. `when`-gated
    // fields are emitted iff their condition holds against the values given.
    // Returns empty and sets *err on any problem (unknown packet, unsupported
    // grammar, a sub-packet that overflows its length field, ...).
    QByteArray encodeBody(const QString &captype,
                          const QHash<QString, qint64> &header,
                          const QVector<SubEntry> &subs,
                          QString *err = nullptr) const;

    // The inverse of encodeBody(): read a raw wire buffer back into the header
    // values and sub-packet entries that would rebuild it. Walks the SAME DOM
    // in the same order with the same `when` gating and the same count-driven
    // repeats, so parse -> encode is byte-identical for any frame the encoder
    // can emit. `frame` is the packet body as captured (no UDP envelope, no
    // message header); the reserved tail (MAC + CRC) is measured, not decoded.
    ParsedPacket parseBody(const QString &captype, const QByteArray &frame) const;

    // Best-effort captype for a buffer whose type is not known: try every
    // packet and keep the one that parses cleanly with PKT_LENGTH agreeing
    // with the buffer size. Returns an empty string when nothing fits.
    QString detectCaptype(const QByteArray &frame) const;

private:
    QDomElement packetElem(const QString &captype) const;
    static QVector<FieldInfo> fieldsOf(const QDomElement &parent,
                                       QStringList *unsupported);
    static void collectFields(const QDomElement &parent, const QString &whenCtx,
                              QVector<FieldInfo> &out, QStringList *unsupported);

    QDomDocument m_doc;
    QHash<QString, QDomElement> m_structs;
    QHash<QString, QDomElement> m_enums;
    QVector<QDomElement>        m_packets;
    bool m_loaded = false;
};

}  // namespace Schema

#endif  // SCHEMAENCODER_H
