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
// The same tag with the CRC-30 its contents give (session 199). Only the CRC
// bits change (page_y bits 34-63, where tags_sim's corrector writes it too):
// every data and reserved bit is kept, which rebuilding from the fields
// would not promise.
QByteArray fixCrc(const QByteArray &tag);
// tags_sim's name: the unique id, "D" after it for a duplicate tag.
QString nameOf(const QByteArray &tag);
QString typeName(int type);       // "Normal", "LC gate", ...

// --- routes ------------------------------------------------------------------
enum Direction { DirUnset = 0, DirNominal = 1, DirReverse = 2 };

// Session 203: a <rfid_data> row's own attributes, as the RFID simulator
// reads them (it moves by abs_loc / next_rfid_abs_loc and picks the packet by
// tag_type, whatever the tag says). Kept when a route.xml / Configuration1.xml
// is read; an edited tag keeps the stale copy, so compare pageX / pageY with
// the tag before trusting it.
struct FileRow {
    bool    present = false;
    QString rfidId, tagName, pageX, pageY;
    int     tagType = 0;
    double  absLoc = 0, nextAbsLoc = 0;
};

struct Tag {
    QString    name;              // as the file had it ("904", "904D")
    QByteArray bytes;             // 16 bytes
    FileRow    file;              // set for rows read from <rfid_data>
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
// The tags_sim <rfid_data> rows of a route run in `dir`, as the simulator
// reads them: rfid_id, tag_name main/duplicate, tag_type, abs_loc,
// next_rfid_abs_loc (the next row's; the last row its own), track_id (the
// reverse-direction TIN, as tags_sim writes it), page_x, page_y.
//
// Session 197, ADJUSTMENT TAGS: after an adjustment tag (type 12) the tags'
// locations are in the numbering the tag corrects to. tags_sim writes them
// back in the numbering before it, so abs_loc / next_rfid_abs_loc stay one
// continuous line for the simulator (its set_nom/rev_adjusted_tags):
//   nominal: dir_corr_1  1 N->N: from location-1 to location-2
//                        4 R->R: from location-2 to location-1
//                        2 N->R: from location-1 to location-2, mirrored
//   reverse: dir_corr_2  4 R->R: from location-2 to location-1
//                        1 N->N: from location-1 to location-2
//                        3 R->N: from location-1 to location-2, mirrored
//   a later main tag at L:  (from - to) + L, mirrored: +/- 2 |to - L|
//   (+ nominal, - reverse); the adjustment tag itself at `from`.
// Duplicate tags keep their own location, and a row's next location is
// adjusted only when its own is, both as tags_sim does. Reproduces every
// row of tags_sim's six route.xml files that hold adjustment tags. One
// difference, on purpose: a dir_corr with no rule here (0, 5-7, or N->R
// read in reverse) leaves the tag at its own location (tags_sim wrote -1)
// and `adjustNote` says so; the tags after it are not corrected, as in
// tags_sim.
struct RouteRow {
    QString rfidId, tagName;
    int     tagType = 0;
    qint64  absLoc = 0, nextAbsLoc = 0;
    qint64  ownLoc = 0;           // the tag's own abs_loc (abs_loc_1 on type 12)
    int     trackId = 0;
    QString pageX, pageY;
    QString adjustNote;           // set on an adjustment tag: what it did
};
QVector<RouteRow> routeRows(const QVector<Tag> &tags, int dir);

bool writeFile(const QString &path, const QByteArray &data, QString *err = nullptr);

}  // namespace RfidTag

#endif  // RFIDTAG_H
