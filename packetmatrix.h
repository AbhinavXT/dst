#ifndef PACKETMATRIX_H
#define PACKETMATRIX_H

// =============================================================================
//  Packet Matrix (session 232) — one Packet Maker preset per SIF operation.
//  -----------------------------------------------------------------------------
//  SIF sections 31-32 test a receiver against one field at a time: "31.6.1
//  Stationary KAVACH should send a packet with PKT_TYPE as 1000 (Reserved)",
//  31.6.2 "... as 1010", and so on, about 570 rows. Each row is one preset:
//  a base preset with that one field changed. Made by hand, that is one
//  Packet Maker edit and Save per row.
//
//  Here: the value each row names is read off the SIF's text, the way it is
//  printed ("1000"), and how it was read is said out loud — the SIF prints
//  binary ("1000" in a 4-bit PKT_TYPE is 8) but sometimes decimal ("100 m"),
//  and a value the reading is not sure of is flagged, never silently picked.
//  Every preset written is first built and self-verified by PacketBuilder,
//  exactly as Build does; a row that does not build is refused by name, not
//  written. A sequence file runs them in clause order.
//
//  Fields the builder sets itself (PKT_LENGTH, CRCs, counts) are not offered:
//  a value written there would be overwritten on the way to the wire.
// =============================================================================

#include "packetpreset.h"

#include <QString>
#include <QVector>

class PacketBuilder;

namespace PacketMatrix {

// A field the matrix can set: in the header (subType -1) or in every
// sub-packet of one type.
struct FieldRef {
    QString name;
    int     bits = 0;
    bool    isSigned = false;
    int     subType = -1;
    QString structName;         // "MovementAuthority" for a sub-packet field
    QString label() const;      // "PKT_TYPE" / "MovementAuthority.CUR_SIG_ASPECT"
};
QVector<FieldRef> fieldsOf(const Schema::Encoder &enc, const QString &captype);

// "FIELD as VALUE" in a clause's text: the first such pair. False if none.
bool findValue(const QString &text, QString *field, QString *printed);

// Who the SIF says sends the row's packet: "Station" ("Stationary KAVACH
// should send ..."), "Onboard", or "" when the text does not start so.
QString senderOf(const QString &text);
// Who sends a packet type: SLRP / AAP / AEP the station, LSRP / ARP the
// onboard; "" for the rest.
QString senderOfPacket(const QString &captype);

// How a printed value was read.
struct Reading {
    bool    ok = false;         // a number came out
    qint64  value = 0;
    bool    sure = true;        // false: shown to the operator as "check"
    QString how;                // "binary 1000 = 8", "decimal 100", "0x1001 = 4097"
    QString why;                // when not sure: what makes it doubtful
};
// `bits`: the field's width. `siblings`: the other rows' printed values for
// the same field — a case that prints 0, 1, 10, 11, 100 is counting in binary.
Reading read(const QString &printed, int bits, const QStringList &siblings);

// "8", "0b1000", "0x8": what the operator may type in the value column.
bool parseTyped(const QString &text, qint64 *out);

// Does `value` fit the field?
bool fits(const FieldRef &f, qint64 value);

struct Row {
    QString clause;             // "31.6.1"
    qint64  value = 0;
};
struct Outcome {
    QString clause;
    bool    ok = false;
    QString file;               // the preset written
    QString why;                // when not
};

// The base with `f` set to `value` (header, or every sub-packet of its type,
// with any variation rule on that field switched off). False, with the
// reason, when the base has no sub-packet of that type.
bool apply(const PacketPreset &base, const FieldRef &f, qint64 value, PacketPreset *out, QString *why);

// One preset per row in `folder` ("31.6.1.packet.json"), each built and
// verified first, and "<caseId>.seq.json" over the ones written, in row
// order, `delayMs` after each. Existing files are replaced only when
// `overwrite`. The sequence path is in *seqPath ("" when nothing was written).
QVector<Outcome> generate(const PacketPreset &base, const FieldRef &f, const QVector<Row> &rows,
                          const QString &folder, const QString &caseId, int delayMs, bool overwrite,
                          PacketBuilder &builder, QString *seqPath);

} // namespace PacketMatrix

#endif // PACKETMATRIX_H
