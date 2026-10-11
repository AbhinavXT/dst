#include "stationlayout.h"

#include "rfidtag.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>

namespace StationLayout {
namespace {

const char *kFormat = "dlconsole-station-layout";

// The tool's sheets, and their columns in its order. A column listed in
// `numbers` is written as a number on export.
struct SheetSpec { const char *name; QStringList columns; QStringList numbers; };
const QVector<SheetSpec> &specs()
{
    static const QVector<SheetSpec> s{
        { "tags",     { "Tag", "Page X Page Y" }, {} },
        { "signals",  { "sig_foot_tag", "signal", "sig_id", "StationId" }, { "sig_id", "StationId" } },
        { "points",   { "point", "location1", "line1", "location2", "line2", "tags", "StationId",
                        "conn_x_list", "conn_line_list", "conn_tin_list" }, { "location1", "location2", "StationId" } },
        { "lines",    { "name", "line", "tags" }, {} },
        { "station",  { "stationid", "location" }, { "stationid", "location" } },
        { "texts",    { "text", "location", "posy" }, { "location", "posy" } },
    };
    return s;
}

// A cell as text the way pandas' astype(str) would give it for a whole
// number: "62", not "62.0".
QString text(const QVariant &v)
{
    if (!v.isValid()) return QString();
    if (v.userType() == QMetaType::Double) {
        const double d = v.toDouble();
        if (d == double(qint64(d))) return QString::number(qint64(d));
    }
    return v.toString().trimmed();
}

qint64 whole(const QVariant &v, bool *ok)
{
    const QString t = text(v);
    if (t.isEmpty()) { *ok = true; return 0; }
    bool good = false;
    const double d = t.toDouble(&good);
    *ok = good;
    return good ? qint64(d) : 0;
}

// Rows of one sheet as column-name -> cell, by its header row.
QVector<QHash<QString, QVariant>> records(const XlsxBook::Sheet &sh)
{
    QVector<QHash<QString, QVariant>> out;
    if (sh.rows.isEmpty()) return out;
    QStringList header;
    for (const QVariant &h : sh.rows.first()) header << text(h);
    for (int r = 1; r < sh.rows.size(); ++r) {
        const XlsxBook::Row &row = sh.rows.at(r);
        QHash<QString, QVariant> rec;
        bool any = false;
        for (int c = 0; c < row.size() && c < header.size(); ++c) {
            if (header.at(c).isEmpty() || !row.at(c).isValid()) continue;
            rec.insert(header.at(c), row.at(c));
            any = true;
        }
        if (any) out << rec;
    }
    return out;
}

QVariant cellFor(const QString &value, bool asNumber)
{
    if (value.isEmpty()) return QVariant();
    if (asNumber) {
        bool ok = false;
        const qlonglong n = value.toLongLong(&ok);
        if (ok) return n;
    }
    return value;
}

QJsonValue sheetJson(const XlsxBook::Sheet &sh)
{
    QJsonArray rows;
    for (const XlsxBook::Row &r : sh.rows) {
        QJsonArray cells;
        for (const QVariant &v : r) {
            if (!v.isValid()) cells.append(QJsonValue());
            else if (v.userType() == QMetaType::QString) cells.append(v.toString());
            else cells.append(v.toDouble());
        }
        rows.append(cells);
    }
    return QJsonObject{ { QStringLiteral("name"), sh.name }, { QStringLiteral("rows"), rows } };
}

XlsxBook::Sheet sheetFromJson(const QJsonObject &o)
{
    XlsxBook::Sheet sh;
    sh.name = o.value(QStringLiteral("name")).toString();
    for (const QJsonValue &r : o.value(QStringLiteral("rows")).toArray()) {
        XlsxBook::Row row;
        for (const QJsonValue &c : r.toArray()) {
            if (c.isString()) row << c.toString();
            else if (c.isDouble()) {
                const double d = c.toDouble();
                row << (d == double(qint64(d)) ? QVariant(qlonglong(d)) : QVariant(d));
            } else row << QVariant();
        }
        sh.rows << row;
    }
    return sh;
}

}  // namespace

// ---- tags -------------------------------------------------------------------------

QString pagesCell(const QString &pageX, const QString &pageY)
{
    return QStringLiteral("page_x=\"%1\" page_y=\"%2\"").arg(pageX, pageY);
}

bool parsePagesCell(const QString &cell, QString *pageX, QString *pageY)
{
    // Any length: a malformed half is kept (and reported by checks()) rather
    // than dropped, so an export does not lose the row.
    static const QRegularExpression re(QStringLiteral("page_x\\s*=\\s*\"([0-9A-Fa-f]*)\"\\s*page_y\\s*=\\s*\"([0-9A-Fa-f]*)\""));
    const QRegularExpressionMatch m = re.match(cell);
    if (!m.hasMatch()) return false;
    *pageX = m.captured(1).toLower();
    *pageY = m.captured(2).toLower();
    return true;
}

TagInfo info(const Tag &t)
{
    TagInfo i;
    const QByteArray bytes = RfidTag::fromPages(t.pageX, t.pageY);
    if (bytes.size() != RfidTag::TagBytes) return i;
    const RfidTag::Summary s = RfidTag::summary(bytes);
    i.ok = s.ok;
    i.type = s.type;
    i.unique = s.unique;
    i.duplicate = s.duplicate;
    i.absLoc = s.absLoc;
    i.tinNom = s.tinNom;
    i.tinRev = s.tinRev;
    i.crcOk = s.crcOk;
    return i;
}

bool moveTag(Tag *t, qint64 absLoc, QString *err)
{
    const QByteArray bytes = RfidTag::fromPages(t->pageX, t->pageY, err);
    if (bytes.size() != RfidTag::TagBytes) return false;
    QHash<QString, qint64> v = RfidTag::values(bytes, err);
    if (v.isEmpty()) return false;
    const QString key = v.contains(QStringLiteral("abs_loc_1")) ? QStringLiteral("abs_loc_1") : QStringLiteral("abs_loc");
    if (!v.contains(key)) {
        if (err) *err = QStringLiteral("this tag type carries no location");
        return false;
    }
    v.insert(key, absLoc);
    const QByteArray moved = RfidTag::build(v, err);
    if (moved.size() != RfidTag::TagBytes) return false;
    t->pageX = RfidTag::pageX(moved);
    t->pageY = RfidTag::pageY(moved);
    return true;
}

QHash<QString, qint64> tagLocations(const Layout &l)
{
    QHash<QString, qint64> out;
    for (const Tag &t : l.tags) {
        const TagInfo i = info(t);
        if (i.ok) out.insert(t.name, i.absLoc);
    }
    return out;
}

QHash<QString, QString> lineOfTag(const Layout &l)
{
    QHash<QString, QString> out;
    for (const Line &ln : l.lines)
        for (const QString &t : ln.tags)
            if (!out.contains(t)) out.insert(t, ln.line);
    return out;
}

QStringList checks(const Layout &l)
{
    QStringList out;
    QHash<QString, int> seen;
    for (const Tag &t : l.tags) {
        if (++seen[t.name] == 2) out << QStringLiteral("tag %1 is listed more than once").arg(t.name);
        QString err;
        const QByteArray bytes = RfidTag::fromPages(t.pageX, t.pageY, &err);
        if (bytes.size() != RfidTag::TagBytes) {
            out << QStringLiteral("tag %1: page_x / page_y do not make a tag (%2)").arg(t.name, err);
            continue;
        }
        const RfidTag::Summary s = RfidTag::summary(bytes);
        const QString bitsName = RfidTag::nameOf(bytes);
        if (bitsName != t.name) out << QStringLiteral("tag %1: its bits say %2").arg(t.name, bitsName);
        if (!s.crcOk) out << QStringLiteral("tag %1: CRC-30 does not match its contents").arg(t.name);
    }
    for (const Signal &sg : l.signalList)
        if (!seen.contains(sg.footTag)) out << QStringLiteral("signal %1: foot tag %2 is not in the tags").arg(sg.name, sg.footTag);
    for (const Line &ln : l.lines)
        for (const QString &t : ln.tags)
            if (!seen.contains(t)) out << QStringLiteral("line %1: tag %2 is not in the tags").arg(ln.line, t);
    return out;
}

// ---- the tool's sheets ---------------------------------------------------------

bool fromSheets(const QVector<XlsxBook::Sheet> &sheets, Layout *out, QString *err, QStringList *notes)
{
    Layout l;
    QHash<QString, const XlsxBook::Sheet *> byName;
    for (const XlsxBook::Sheet &sh : sheets) byName.insert(sh.name, &sh);
    bool any = false;
    for (const SheetSpec &s : specs()) any = any || byName.contains(QLatin1String(s.name));
    if (!any) {
        if (err) *err = QStringLiteral("no station sheets in it (tags, signals, points, lines, station, texts)");
        return false;
    }
    auto note = [notes](const QString &n) { if (notes) *notes << n; };
    auto num = [&note](const QHash<QString, QVariant> &r, const char *col, const QString &where) {
        bool ok = true;
        const qint64 v = whole(r.value(QLatin1String(col)), &ok);
        if (!ok) note(QStringLiteral("%1: %2 \"%3\" is not a number, read as 0").arg(where, QLatin1String(col), text(r.value(QLatin1String(col)))));
        return v;
    };
    for (const char *needed : { "tags", "signals" })
        if (!byName.contains(QLatin1String(needed))) note(QStringLiteral("no \"%1\" sheet: the Python tool needs one").arg(QLatin1String(needed)));

    if (const XlsxBook::Sheet *sh = byName.value(QStringLiteral("tags"))) {
        int n = 1;
        for (const auto &r : records(*sh)) {
            ++n;
            Tag t;
            t.name = text(r.value(QStringLiteral("Tag")));
            if (!parsePagesCell(text(r.value(QStringLiteral("Page X Page Y"))), &t.pageX, &t.pageY)) {
                note(QStringLiteral("tags row %1 (%2): no page_x=\"…\" page_y=\"…\", skipped").arg(n).arg(t.name));
                continue;
            }
            l.tags << t;
        }
    }
    if (const XlsxBook::Sheet *sh = byName.value(QStringLiteral("signals"))) {
        for (const auto &r : records(*sh)) {
            Signal s;
            s.footTag = text(r.value(QStringLiteral("sig_foot_tag")));
            s.name = text(r.value(QStringLiteral("signal")));
            s.sigId = num(r, "sig_id", QStringLiteral("signal %1").arg(s.name));
            s.stationId = num(r, "StationId", QStringLiteral("signal %1").arg(s.name));
            l.signalList << s;
        }
    }
    if (const XlsxBook::Sheet *sh = byName.value(QStringLiteral("points"))) {
        for (const auto &r : records(*sh)) {
            Point p;
            p.name = text(r.value(QStringLiteral("point")));
            const QString where = QStringLiteral("point %1").arg(p.name);
            p.loc1 = num(r, "location1", where);
            p.line1 = text(r.value(QStringLiteral("line1")));
            p.loc2 = num(r, "location2", where);
            p.line2 = text(r.value(QStringLiteral("line2")));
            p.tags = text(r.value(QStringLiteral("tags")));
            p.stationId = num(r, "StationId", where);
            p.connX = text(r.value(QStringLiteral("conn_x_list")));
            p.connLine = text(r.value(QStringLiteral("conn_line_list")));
            p.connTin = text(r.value(QStringLiteral("conn_tin_list")));
            l.points << p;
        }
    }
    if (const XlsxBook::Sheet *sh = byName.value(QStringLiteral("lines"))) {
        for (const auto &r : records(*sh)) {
            Line ln;
            ln.name = text(r.value(QStringLiteral("name")));
            ln.line = text(r.value(QStringLiteral("line")));
            for (const QString &t : text(r.value(QStringLiteral("tags"))).split(QLatin1Char(',')))
                if (!t.trimmed().isEmpty()) ln.tags << t.trimmed();
            l.lines << ln;
        }
    }
    if (const XlsxBook::Sheet *sh = byName.value(QStringLiteral("station"))) {
        for (const auto &r : records(*sh)) {
            Station s;
            s.id = num(r, "stationid", QStringLiteral("station"));
            s.location = num(r, "location", QStringLiteral("station %1").arg(s.id));
            l.stations << s;
        }
    }
    if (const XlsxBook::Sheet *sh = byName.value(QStringLiteral("texts"))) {
        for (const auto &r : records(*sh)) {
            Text t;
            t.text = text(r.value(QStringLiteral("text")));
            t.location = num(r, "location", QStringLiteral("text \"%1\"").arg(t.text));
            t.posY = num(r, "posy", QStringLiteral("text \"%1\"").arg(t.text));
            l.texts << t;
        }
    }
    for (const XlsxBook::Sheet &sh : sheets) {
        bool known = false;
        for (const SheetSpec &s : specs()) known = known || sh.name == QLatin1String(s.name);
        if (!known) l.otherSheets << sh;
    }
    *out = l;
    return true;
}

QVector<XlsxBook::Sheet> toSheets(const Layout &l)
{
    QVector<XlsxBook::Sheet> out;
    auto sheet = [&out](const SheetSpec &s, const QVector<QStringList> &rows) {
        XlsxBook::Sheet sh;
        sh.name = QLatin1String(s.name);
        XlsxBook::Row header;
        for (const QString &c : s.columns) header << c;
        sh.rows << header;
        for (const QStringList &r : rows) {
            XlsxBook::Row row;
            for (int c = 0; c < r.size(); ++c) row << cellFor(r.at(c), s.numbers.contains(s.columns.at(c)));
            while (!row.isEmpty() && !row.last().isValid()) row.removeLast();
            sh.rows << row;
        }
        out << sh;
    };
    const QVector<SheetSpec> &sp = specs();
    QVector<QStringList> rows;
    for (const Tag &t : l.tags) rows << QStringList{ t.name, pagesCell(t.pageX, t.pageY) };
    sheet(sp.at(0), rows);
    rows.clear();
    for (const Signal &s : l.signalList)
        rows << QStringList{ s.footTag, s.name, QString::number(s.sigId), QString::number(s.stationId) };
    sheet(sp.at(1), rows);
    rows.clear();
    for (const Point &p : l.points)
        rows << QStringList{ p.name, QString::number(p.loc1), p.line1, QString::number(p.loc2), p.line2, p.tags,
                             QString::number(p.stationId), p.connX, p.connLine, p.connTin };
    sheet(sp.at(2), rows);
    rows.clear();
    for (const Line &ln : l.lines) rows << QStringList{ ln.name, ln.line, ln.tags.join(QLatin1Char(',')) };
    sheet(sp.at(3), rows);
    rows.clear();
    for (const Station &s : l.stations) rows << QStringList{ QString::number(s.id), QString::number(s.location) };
    sheet(sp.at(4), rows);
    rows.clear();
    for (const Text &t : l.texts) rows << QStringList{ t.text, QString::number(t.location), QString::number(t.posY) };
    sheet(sp.at(5), rows);
    out << l.otherSheets;
    return out;
}

// ---- JSON -------------------------------------------------------------------------

QByteArray toJson(const Layout &l)
{
    QJsonObject root;
    root.insert(QStringLiteral("format"), QLatin1String(kFormat));
    root.insert(QStringLiteral("version"), 1);
    QJsonArray a;
    for (const Tag &t : l.tags)
        a.append(QJsonObject{ { "name", t.name }, { "page_x", t.pageX }, { "page_y", t.pageY } });
    root.insert(QStringLiteral("tags"), a);
    a = QJsonArray();
    for (const Signal &s : l.signalList)
        a.append(QJsonObject{ { "foot_tag", s.footTag }, { "signal", s.name },
                              { "sig_id", double(s.sigId) }, { "station_id", double(s.stationId) } });
    root.insert(QStringLiteral("signals"), a);
    a = QJsonArray();
    for (const Point &p : l.points)
        a.append(QJsonObject{ { "point", p.name }, { "location1", double(p.loc1) }, { "line1", p.line1 },
                              { "location2", double(p.loc2) }, { "line2", p.line2 }, { "tags", p.tags },
                              { "station_id", double(p.stationId) }, { "conn_x_list", p.connX },
                              { "conn_line_list", p.connLine }, { "conn_tin_list", p.connTin } });
    root.insert(QStringLiteral("points"), a);
    a = QJsonArray();
    for (const Line &ln : l.lines)
        a.append(QJsonObject{ { "name", ln.name }, { "line", ln.line }, { "tags", QJsonArray::fromStringList(ln.tags) } });
    root.insert(QStringLiteral("lines"), a);
    a = QJsonArray();
    for (const Station &s : l.stations)
        a.append(QJsonObject{ { "station_id", double(s.id) }, { "location", double(s.location) } });
    root.insert(QStringLiteral("stations"), a);
    a = QJsonArray();
    for (const Text &t : l.texts)
        a.append(QJsonObject{ { "text", t.text }, { "location", double(t.location) }, { "posy", double(t.posY) } });
    root.insert(QStringLiteral("texts"), a);
    a = QJsonArray();
    for (const XlsxBook::Sheet &sh : l.otherSheets) a.append(sheetJson(sh));
    root.insert(QStringLiteral("other_sheets"), a);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool fromJson(const QByteArray &json, Layout *out, QString *err, QStringList *notes)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (!doc.isObject()) {
        if (err) *err = QStringLiteral("not JSON: %1").arg(pe.errorString());
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String(kFormat)) {
        if (err) *err = QStringLiteral("not a DLConsole station layout (format \"%1\")").arg(QLatin1String(kFormat));
        return false;
    }
    if (root.value(QStringLiteral("version")).toInt() > 1 && notes)
        *notes << QStringLiteral("written by a newer DLConsole (version %1): fields it added are not read")
                      .arg(root.value(QStringLiteral("version")).toInt());
    Layout l;
    auto arr = [&root](const char *k) { return root.value(QLatin1String(k)).toArray(); };
    auto s = [](const QJsonObject &o, const char *k) { return o.value(QLatin1String(k)).toString(); };
    auto n = [](const QJsonObject &o, const char *k) { return qint64(o.value(QLatin1String(k)).toDouble()); };
    for (const QJsonValue &v : arr("tags")) {
        const QJsonObject o = v.toObject();
        l.tags << Tag{ s(o, "name"), s(o, "page_x").toLower(), s(o, "page_y").toLower() };
    }
    for (const QJsonValue &v : arr("signals")) {
        const QJsonObject o = v.toObject();
        l.signalList << Signal{ s(o, "foot_tag"), s(o, "signal"), n(o, "sig_id"), n(o, "station_id") };
    }
    for (const QJsonValue &v : arr("points")) {
        const QJsonObject o = v.toObject();
        l.points << Point{ s(o, "point"), n(o, "location1"), s(o, "line1"), n(o, "location2"), s(o, "line2"),
                           s(o, "tags"), n(o, "station_id"), s(o, "conn_x_list"), s(o, "conn_line_list"),
                           s(o, "conn_tin_list") };
    }
    for (const QJsonValue &v : arr("lines")) {
        const QJsonObject o = v.toObject();
        Line ln{ s(o, "name"), s(o, "line"), {} };
        for (const QJsonValue &t : o.value(QStringLiteral("tags")).toArray()) ln.tags << t.toString();
        l.lines << ln;
    }
    for (const QJsonValue &v : arr("stations")) {
        const QJsonObject o = v.toObject();
        l.stations << Station{ n(o, "station_id"), n(o, "location") };
    }
    for (const QJsonValue &v : arr("texts")) {
        const QJsonObject o = v.toObject();
        l.texts << Text{ s(o, "text"), n(o, "location"), n(o, "posy") };
    }
    for (const QJsonValue &v : arr("other_sheets")) l.otherSheets << sheetFromJson(v.toObject());
    *out = l;
    return true;
}

QString defaultFile() { return QStringLiteral(":/station_layouts/station.xlsx"); }

QStringList library()
{
    QStringList out;
    QDirIterator it(QStringLiteral(":/station_layouts"), { QStringLiteral("*.xlsx") }, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) out << it.next();
    std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) {
        const QString ga = libraryGroup(a), gb = libraryGroup(b);
        if (ga != gb) return ga < gb;   // the top level ("") first
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return out;
}

QString libraryGroup(const QString &path)
{
    const QString rel = path.mid(QStringLiteral(":/station_layouts/").size());
    return rel.contains(QLatin1Char('/')) ? rel.section(QLatin1Char('/'), 0, 0) : QString();
}

// ---- by extension ------------------------------------------------------------------

bool load(const QString &path, Layout *out, QString *err, QStringList *notes)
{
    if (QFileInfo(path).suffix().compare(QLatin1String("xlsx"), Qt::CaseInsensitive) == 0) {
        const QVector<XlsxBook::Sheet> sheets = XlsxBook::readFile(path, err);
        return !sheets.isEmpty() && fromSheets(sheets, out, err, notes);
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = QStringLiteral("cannot open %1: %2").arg(path, f.errorString());
        return false;
    }
    return fromJson(f.readAll(), out, err, notes);
}

bool save(const QString &path, const Layout &l, QString *err)
{
    if (QFileInfo(path).suffix().compare(QLatin1String("xlsx"), Qt::CaseInsensitive) == 0)
        return XlsxBook::writeFile(path, toSheets(l), err);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (err) *err = QStringLiteral("cannot write %1: %2").arg(path, f.errorString());
        return false;
    }
    f.write(toJson(l));
    if (!f.commit()) {
        if (err) *err = QStringLiteral("cannot write %1: %2").arg(path, f.errorString());
        return false;
    }
    return true;
}

}  // namespace StationLayout
