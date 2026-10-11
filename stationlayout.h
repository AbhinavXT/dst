#ifndef STATIONLAYOUT_H
#define STATIONLAYOUT_H

// =============================================================================
//  Station layout (session 208) — Tools ▸ Station Layout…
//  -----------------------------------------------------------------------------
//  The station file of the old Python tool (DEBUGGING_TOOL_KAVACH,
//  loco_performance/config/station/*.xlsx), as DLConsole's own file and
//  editable:
//    tags      Tag, Page X Page Y  ('page_x="…" page_y="…"' in one cell)
//    signals   sig_foot_tag, signal, sig_id, StationId
//    points    point, location1, line1, location2, line2, tags, StationId,
//              conn_x_list, conn_line_list, conn_tin_list
//    lines     name, line, tags (comma-separated tag names, in order)
//    station   stationid, location
//    texts     text, location, posy
//  Any other sheet (relaymap, adjustment, …) is carried through untouched:
//  DLConsole has no use for it (relaymap is NMS field-input data), but the
//  Python tool reads it, so an exported file keeps it.
//
//  OWN FILE: JSON ("format": "dlconsole-station-layout", version 1).
//  IMPORT / EXPORT: the tool's .xlsx (XlsxBook). The tool reads every column
//  through pandas astype(str/int), but a whole-number column read as float
//  (one empty cell is enough) turns 981 into "981.0" — so export writes the
//  columns that were numbers as numbers and the rest as text, as the tool's
//  own files have them.
//
//  A TAG's location is inside its bits (abs_loc, abs_loc_1 on type 12), so
//  moving a tag re-encodes it through RfidTag with a fresh CRC-30 (decided
//  2026-10-11); its name follows its unique id ("D" for a duplicate).
// =============================================================================

#include "xlsxbook.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace StationLayout {

struct Tag      { QString name, pageX, pageY; };
struct Signal   { QString footTag, name; qint64 sigId = 0, stationId = 0; };
struct Point    { QString name; qint64 loc1 = 0; QString line1; qint64 loc2 = 0; QString line2, tags;
                  qint64 stationId = 0; QString connX, connLine, connTin; };
struct Line     { QString name, line; QStringList tags; };
struct Station  { qint64 id = 0, location = 0; };
struct Text     { QString text; qint64 location = 0, posY = 0; };

struct Layout {
    QVector<Tag>     tags;
    QVector<Signal>  signalList;     // (not "signals": a Qt keyword)
    QVector<Point>   points;
    QVector<Line>    lines;
    QVector<Station> stations;
    QVector<Text>    texts;
    QVector<XlsxBook::Sheet> otherSheets;   // relaymap, adjustment, ...: carried, not used
    bool isEmpty() const { return tags.isEmpty() && signalList.isEmpty() && points.isEmpty() && lines.isEmpty()
                                  && stations.isEmpty() && texts.isEmpty() && otherSheets.isEmpty(); }
};

// --- files -------------------------------------------------------------------
// Session 210: the built-in default layout, the station file the Python
// tool opens at start (its config/station/station.xlsx), unchanged.
QString defaultFile();
// Session 211: every built-in station file (scripts/station_layout_library.py:
// the Python tool's config/station/*.xlsx), as resource paths, sorted; the
// folder under :/station_layouts/ is the group ("" for the top level).
QStringList library();
QString libraryGroup(const QString &path);

QByteArray toJson(const Layout &l);
// *err set and false on a file that is not a layout; a row it cannot read is
// skipped and named in *notes.
bool fromJson(const QByteArray &json, Layout *out, QString *err = nullptr, QStringList *notes = nullptr);

// The tool's sheets. A missing sheet is an empty table (the tool needs
// tags and signals; *notes says when either is missing).
bool fromSheets(const QVector<XlsxBook::Sheet> &sheets, Layout *out, QString *err = nullptr, QStringList *notes = nullptr);
QVector<XlsxBook::Sheet> toSheets(const Layout &l);

// By extension: .xlsx imports / exports the tool's file, anything else is JSON.
bool load(const QString &path, Layout *out, QString *err = nullptr, QStringList *notes = nullptr);
bool save(const QString &path, const Layout &l, QString *err = nullptr);

// --- tags --------------------------------------------------------------------
// 'page_x="…" page_y="…"' <-> the two halves.
QString pagesCell(const QString &pageX, const QString &pageY);
bool parsePagesCell(const QString &cell, QString *pageX, QString *pageY);

struct TagInfo {
    bool    ok = false;           // page_x / page_y decode to a tag
    int     type = 0, unique = 0;
    bool    duplicate = false;
    qint64  absLoc = 0;           // abs_loc (abs_loc_1 on type 12)
    int     tinNom = 0, tinRev = 0;
    bool    crcOk = false;
};
TagInfo info(const Tag &t);

// The tag at a new location: re-encoded, CRC-30 recomputed, every other
// field kept. False and *err on refusal (no decodable tag, a location too
// wide for the field).
bool moveTag(Tag *t, qint64 absLoc, QString *err = nullptr);

// What a reader of the file would trip on, one line each: a tag whose
// page_x / page_y do not make a tag, whose bits name another tag, whose
// CRC-30 does not match, listed twice; a signal or line naming a tag that
// is not in the tags. Reported, never corrected.
QStringList checks(const Layout &l);

// --- drawing helpers -----------------------------------------------------------
// Tag name -> abs_loc, for every tag that decodes.
QHash<QString, qint64> tagLocations(const Layout &l);
// Tag name -> its line (from the lines sheet; first line wins).
QHash<QString, QString> lineOfTag(const Layout &l);

}  // namespace StationLayout

#endif // STATIONLAYOUT_H
