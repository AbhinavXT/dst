#ifndef RFIDTAG_H
#define RFIDTAG_H

// =============================================================================
//  RFID tags and tag routes (session 195) — the engine behind Tools ▸ RFID Tag
//  Builder, brought over from the tags_sim tool (docs/RFID_TAG_BUILDER.md).
//  -----------------------------------------------------------------------------
//  A TAG is the 16 bytes an Annexure-D balise holds. tags_sim and the RFID
//  simulator write it as two 16-hex-digit halves:
//      page_x = tag bytes 0-7  as one little-endian 64-bit number
//      page_y = tag bytes 8-15 as one little-endian 64-bit number
//  printed most significant digit first. Fields are read and written through
//  the schema's RFID packet (kavach.xml, captype rfid: a reader-id byte + the
//  16 tag bytes), so the tag editor, the decoder and the golden validator
//  agree on every bit; the CRC-30 is the packet's <crc> element.
//
//  A ROUTE is an ordered list of tags as the loco meets them, with the
//  signals at their foot tags and a direction. Read from:
//    - DLConsole's own file (<tag_route>, *.tagroute.xml; scripts/
//      tags_sim_import.py converts tags_sim's .xlsx routes to it),
//    - tags_sim's route.xml and the simulator's Configuration1.xml: every
//      <route_data> block is one route (their rows carry no signals).
//  Written to: the own file, and tags_sim's route.xml (the route and its REV
//  twin), which is the block Configuration1.xml is made of.
// =============================================================================

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace RfidTag {

constexpr int TagBytes = 16;

// --- page_x / page_y ---------------------------------------------------------
// Empty and *err set unless both are 1-16 hex digits.
QByteArray fromPages(const QString &pageX, const QString &pageY, QString *err = nullptr);
QString    pageX(const QByteArray &tag);    // 16 lower-case hex digits
QString    pageY(const QByteArray &tag);

// --- fields ------------------------------------------------------------------
// The schema's field values for a tag (reader_id excluded): type, version,
// unique, abs_loc (abs_loc_1 on type 12), tin_nom, ... Empty on failure.
QHash<QString, qint64> values(const QByteArray &tag, QString *err = nullptr);
// The tag those values make, CRC-30 computed. Fields not given are 0; fields
// the type does not carry are ignored. Empty and *err set on refusal (a
// value too wide for its field, no schema).
QByteArray build(const QHash<QString, qint64> &values, QString *err = nullptr);
// The editable fields of a tag type, in wire order: name, width, enum name.
struct Field { QString name; int bits = 0; QString enumName; };
QVector<Field> fieldsOf(int type);
// A coded value as the decoder shows it ("1 (Duplicate Tag)").
QString enumLabel(const QString &enumName, qint64 v);

// The decoder's display rows for a tag (Field | Value, CRC row included).
QVector<QPair<QString, QString>> describe(const QByteArray &tag);

struct Summary {
    bool    ok = false;           // 16 bytes
    int     type = 0, unique = 0;
    bool    duplicate = false;
    qint64  absLoc = 0;           // abs_loc (abs_loc_1 on type 12)
    int     tinNom = 0, tinRev = 0, placement = 0;
    bool    crcOk = false;
    quint32 crcStored = 0, crcCalc = 0;
};
Summary summary(const QByteArray &tag);
// tags_sim's name: the unique id, "D" after it for a duplicate tag.
QString nameOf(const QByteArray &tag);
QString typeName(int type);       // "Normal", "LC gate", ...

// --- routes ------------------------------------------------------------------
enum Direction { DirUnset = 0, DirNominal = 1, DirReverse = 2 };

struct Tag {
    QString    name;              // as the file had it ("904", "904D")
    QByteArray bytes;             // 16 bytes
};
struct Signal {                   // tags_sim's signals sheet
    QString footTag, name, sigId;
};
struct Route {
    QString name;
    int     dir = DirUnset;
    QVector<Tag> tags;
    QVector<Signal> signalList;   // (not "signals": a Qt keyword)
};

// Every route in a file: a <tag_route>, or each <route_data> of a route.xml /
// Configuration1.xml. Empty and *err set if none can be read; a row whose
// page_x/page_y does not parse is skipped and named in *notes.
QVector<Route> readFile(const QString &path, QString *err = nullptr, QStringList *notes = nullptr);
QVector<Route> readXml(const QByteArray &xml, QString *err = nullptr, QStringList *notes = nullptr);

QByteArray toTagRouteXml(const Route &r);
// tags_sim's route.xml: <route> with the route and its REV twin. Refused
// (empty, *err) while the direction is unset: the REV twin and the names
// depend on it.
QByteArray toRouteXml(const Route &r, QString *err = nullptr);
// The tags_sim <rfid_data> rows of one direction, as the simulator reads
// them: rfid_id, tag_name main/duplicate, tag_type, abs_loc,
// next_rfid_abs_loc (the next row's; the last row its own), track_id (the
// reverse-direction TIN, as tags_sim writes it), page_x, page_y.
struct RouteRow {
    QString rfidId, tagName;
    int     tagType = 0;
    qint64  absLoc = 0, nextAbsLoc = 0;
    int     trackId = 0;
    QString pageX, pageY;
};
QVector<RouteRow> routeRows(const QVector<Tag> &tags);

bool writeFile(const QString &path, const QByteArray &data, QString *err = nullptr);

}  // namespace RfidTag

#endif  // RFIDTAG_H
