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
//    kavach    session 422: the Stationary KAVACH data (stationkavach.h), as
//              JSON: one line of it per row in column A ("kavach" in the
//              own file). The Python tool does not read it.
//  Any other sheet (relaymap, adjustment, …) is carried through untouched:
//  DLConsole has no use for it (relaymap is NMS field-input data), but the
//  Python tool reads it, so an exported file keeps it.
//
//  OWN FILE: JSON ("format": "dlconsole-station-layout", version 1).
//  Session 431: or the station XML, station_<id>.xml ("kavsite-xml 1", the
//  kav_sim Config Maker's file): one nested file with the station, its tags
//  (decoded fields + hex), lines, points, signals, track circuits, relay
//  map, labels, routes and a TIN index. Saved and opened like the JSON.
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
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace StationLayout {

struct Tag      { QString name, pageX, pageY; };
struct Signal   { QString footTag, name; qint64 sigId = 0, stationId = 0;
                  QString route;      // session 431: the route a route file's signal belongs to; "" the station's
                  // Session 432: how it is drawn. aspects: lamps of its main head (2, 3, 4;
                  // 0 none: a shunt signal); -1 not said (lookOf). Columns aspects,
                  // calling_on, shunt; the tool's files have none of them.
                  int aspects = -1; bool callingOn = false, shunt = false;
                  // Session 435: its junction route indicator's position, 1-6 (1-3
                  // left, 4-6 right, as sigAspect numbers them); 0 none. Column indicator.
                  int indicator = 0; };
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
    QJsonObject kavach;                     // session 422: StationKavach::fromJson reads it
    bool isEmpty() const { return tags.isEmpty() && signalList.isEmpty() && points.isEmpty() && lines.isEmpty()
                                  && stations.isEmpty() && texts.isEmpty() && otherSheets.isEmpty() && kavach.isEmpty(); }
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
// Session 237: what Export .xlsx writes — toSheets(), plus an empty
// "relaymap" sheet (the tool's header row only) when the layout carries none.
// The Python tool reads relaymap unconditionally and stops without it, so a
// station made here from scratch would not open there.
QVector<XlsxBook::Sheet> toToolSheets(const Layout &l);

// Session 431: the station XML (<kavsite format="kavsite-xml 1">). Tags are
// written as their decoded fields (type, location, tin_nom, tin_rev,
// placement; location2, dir1, dir2 on an adjustment tag) and the 32 hex
// digits that hold them (page_y then page_x); read back from the hex, a
// field that disagrees with it named in *notes. Lines, points and routes
// carry their tags as <tag id>, a point its connections as <conn x line
// tin>. The other sheets: adjustment, circuits, relaymap and routes by name,
// any other as <sheet name>; the KAVACH data as JSON in <kavach>. <tins>
// (which tags carry each TIN) is derived, and not read back. `name` is the
// root's name (the file's base name). *notes: what the XML could not hold.
QByteArray toXml(const Layout &l, const QString &name, QStringList *notes = nullptr);
bool fromXml(const QByteArray &xml, Layout *out, QString *err = nullptr, QStringList *notes = nullptr);

// By extension: .xlsx imports / exports the tool's file, .xml the station
// XML, anything else is JSON.
bool load(const QString &path, Layout *out, QString *err = nullptr, QStringList *notes = nullptr);
bool save(const QString &path, const Layout &l, QString *err = nullptr);

// Session 432: the heads a signal is drawn with. Not said (aspects -1): a
// name starting "SH" is a shunt signal (a disc, no main head), any other a
// 3-aspect main signal (Red, Yellow, Green).
struct SignalLook { int aspects = 3; bool callingOn = false, shunt = false; int indicator = 0; };
SignalLook lookOf(const Signal &s);

// Session 436: the track circuits of the "circuits" sheet (the station XML's
// <circuits>): name, line, from startx to endx (m); without its ends, at
// its location (from == to). A row without a line or any place is left out
// (named in *notes).
struct Circuit { int cid = 0; QString name, line; qint64 from = 0, to = 0; };
QVector<Circuit> circuits(const Layout &l, QStringList *notes = nullptr);

// --- tags --------------------------------------------------------------------
// 'page_x="…" page_y="…"' <-> the two halves.
QString pagesCell(const QString &pageX, const QString &pageY);
bool parsePagesCell(const QString &cell, QString *pageX, QString *pageY);

struct TagInfo {
    bool    ok = false;           // page_x / page_y decode to a tag
    int     type = 0, unique = 0;
    bool    duplicate = false;
    qint64  absLoc = 0;           // abs_loc (abs_loc_1 on type 12)
    int     tinNom = 0, tinRev = 0, placement = 0;
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
// Session 426: tag number -> the abs_loc of every main tag of that number
// (more than one when the layout lists the number more than once).
QHash<int, QVector<qint64>> tagLocationsByNumber(const Layout &l);
// Tag name -> its line (from the lines sheet; first line wins).
QHash<QString, QString> lineOfTag(const Layout &l);
// Session 429: a key for each tag of the layout, in order: its name; the
// second, third, ... tag of a name the layout lists more than once (a
// section drawn block after block) "name#2", "name#3". The drawing places
// each by its key, so every one is drawn where it is.
QStringList tagKeys(const Layout &l);
QString nameOfKey(const QString &key);             // "906#2" -> "906"; a name -> itself
int indexOfKey(const Layout &l, const QString &key);   // into l.tags; -1 when absent

// --- comparing two layouts (session 226) -----------------------------------------
// What changed from `from` (another file) to `to` (the layout open): tags by
// name, then signals, lines, points, the station and texts, one line each.
struct TagChange {
    enum Kind { Added, Removed, Moved, Changed };
    QString name;
    Kind    kind = Changed;
    qint64  fromLoc = 0, toLoc = 0;    // Moved (and Changed when the location changed too)
    QStringList fields;                // Changed: "tin_nom 7 -> 8", ...; "CRC-30" when only the CRC differs
};
struct Diff {
    QVector<TagChange> tags;
    QStringList other;
    bool isEmpty() const { return tags.isEmpty() && other.isEmpty(); }
};
Diff diff(const Layout &from, const Layout &to);

}  // namespace StationLayout

#endif // STATIONLAYOUT_H
