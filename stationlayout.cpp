#include "stationlayout.h"

#include <QSet>

#include "rfidtag.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>
#include <functional>

namespace StationLayout {
namespace {

const char *kFormat = "dlconsole-station-layout";
const char *kKavachSheet = "kavach";
const char *kSheetNotes = "sheet_notes";   // session 454

// The tool's sheets, and their columns in its order. A column listed in
// `numbers` is written as a number on export.
struct SheetSpec { const char *name; QStringList columns; QStringList numbers; };
const QVector<SheetSpec> &specs()
{
    static const QVector<SheetSpec> s{
        { "tags",     { "Tag", "Page X Page Y" }, {} },
        // Session 431: "route" for a route file's signals (the station XML has them); empty in the tool's own.
        // Session 432: aspects, calling_on, shunt: how it is drawn (empty: lookOf decides).
        // Session 435: indicator, its junction route indicator's position (1-6).
        { "signals",  { "sig_foot_tag", "signal", "sig_id", "StationId", "route", "aspects", "calling_on", "shunt", "indicator" },
                      { "sig_id", "StationId", "aspects", "calling_on", "shunt", "indicator" } },
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

// Session 432: a signal's look from its cells; a value it cannot use named, not guessed.
void readLook(Signal *s, const QString &aspects, const QString &callingOn, const QString &shunt,
              const std::function<void(const QString &)> &note, const QString &indicator = QString())
{
    if (!indicator.isEmpty()) {   // session 435
        bool ok = false;
        const int n = indicator.toInt(&ok);
        if (ok && n >= 0 && n <= 6) s->indicator = n;
        else note(QStringLiteral("signal %1: indicator \"%2\" is not a position 1-6: none drawn").arg(s->name, indicator));
    }
    if (!aspects.isEmpty()) {
        bool ok = false;
        const int n = aspects.toInt(&ok);
        if (ok && (n == 0 || (n >= 2 && n <= 4))) s->aspects = n;
        else note(QStringLiteral("signal %1: aspects \"%2\" is not 0, 2, 3 or 4: drawn as not said").arg(s->name, aspects));
    }
    auto yes = [](const QString &v) {
        return v == QLatin1String("1") || v.compare(QLatin1String("yes"), Qt::CaseInsensitive) == 0
               || v.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
    };
    s->callingOn = yes(callingOn);
    s->shunt = yes(shunt);
}

}  // namespace

QVector<Circuit> circuits(const Layout &l, QStringList *notes)
{
    QVector<Circuit> out;
    for (const XlsxBook::Sheet &sh : l.otherSheets) {
        if (sh.name.compare(QLatin1String("circuits"), Qt::CaseInsensitive) != 0) continue;
        for (const auto &r : records(sh)) {
            Circuit c;
            c.name = text(r.value(QStringLiteral("name")));
            c.line = text(r.value(QStringLiteral("line")));
            c.cid = int(text(r.value(QStringLiteral("cid"))).toLongLong());
            bool a = false, b = false;
            c.from = text(r.value(QStringLiteral("startx"))).toLongLong(&a);
            c.to = text(r.value(QStringLiteral("endx"))).toLongLong(&b);
            if (!a || !b) {   // station_527.xml's L_DM, 50AT, L_UM: a location only
                bool ok = false;
                c.from = c.to = text(r.value(QStringLiteral("location"))).toLongLong(&ok);
                a = b = ok;
            }
            if (c.name.isEmpty() || c.line.isEmpty() || !a || !b) {
                if (notes) *notes << QStringLiteral("circuit %1: no line, or no startx / endx or location: not drawn").arg(c.name.isEmpty() ? QString::number(c.cid) : c.name);
                continue;
            }
            if (c.from > c.to) std::swap(c.from, c.to);
            out << c;
        }
    }
    return out;
}

SignalLook lookOf(const Signal &s)
{
    SignalLook k;
    k.callingOn = s.callingOn;
    k.shunt = s.shunt;
    k.indicator = s.indicator;
    if (s.aspects >= 0) k.aspects = s.aspects;
    else if (s.name.startsWith(QLatin1String("SH"), Qt::CaseInsensitive)) k.aspects = 0;
    if (k.aspects == 0) k.shunt = true;   // nothing but the disc
    return k;
}

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
    i.placement = s.placement;
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

QHash<int, QVector<qint64>> tagLocationsByNumber(const Layout &l)
{
    QHash<int, QVector<qint64>> out;
    for (const Tag &t : l.tags) {
        bool ok = false;
        const int id = t.name.toInt(&ok);   // main tags only: a duplicate ("…D") lies apart
        const TagInfo in = info(t);
        if (ok && in.ok) out[id] << in.absLoc;
    }
    return out;
}

QStringList tagKeys(const Layout &l)
{
    QStringList out;
    QHash<QString, int> seen;
    for (const Tag &t : l.tags) {
        const int n = ++seen[t.name];
        out << (n == 1 ? t.name : t.name + QLatin1Char('#') + QString::number(n));
    }
    return out;
}

QString nameOfKey(const QString &key)
{
    const int at = key.lastIndexOf(QLatin1Char('#'));
    bool ok = false;
    if (at > 0) key.mid(at + 1).toInt(&ok);
    return ok ? key.left(at) : key;
}

int indexOfKey(const Layout &l, const QString &key)
{
    const QString name = nameOfKey(key);
    const int want = name == key ? 1 : key.mid(name.size() + 1).toInt();
    for (int i = 0, n = 0; i < l.tags.size(); ++i)
        if (l.tags.at(i).name == name && ++n == want) return i;
    return -1;
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
    // Session 233: drawn on a lane of its own, but the lines sheet should list it.
    QSet<QString> lines;
    for (const Line &ln : l.lines) lines.insert(ln.line);
    for (const Point &p : l.points)
        for (const QString &id : { p.line1, p.line2 })
            if (!id.isEmpty() && !lines.contains(id)) {
                out << QStringLiteral("point %1: line %2 is not in the lines sheet").arg(p.name, id);
                break;
            }
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
            s.route = text(r.value(QStringLiteral("route")));
            readLook(&s, text(r.value(QStringLiteral("aspects"))), text(r.value(QStringLiteral("calling_on"))),
                     text(r.value(QStringLiteral("shunt"))), note, text(r.value(QStringLiteral("indicator"))));
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
        if (sh.name == QLatin1String(kKavachSheet)) {
            QStringList json;
            for (const XlsxBook::Row &r : sh.rows) json << (r.isEmpty() ? QString() : r.first().toString());
            const QJsonDocument doc = QJsonDocument::fromJson(json.join(QLatin1Char('\n')).toUtf8());
            if (doc.isObject()) l.kavach = doc.object();
            else if (notes) *notes << QStringLiteral("the kavach sheet is not JSON: not read");
            continue;
        }
        if (sh.name == QLatin1String(kSheetNotes)) {
            for (const XlsxBook::Row &r : sh.rows)
                if (!r.isEmpty() && !r.first().toString().isEmpty()) l.sheetNotes << r.first().toString();
            continue;
        }
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
        rows << QStringList{ s.footTag, s.name, QString::number(s.sigId), QString::number(s.stationId), s.route,
                             s.aspects >= 0 ? QString::number(s.aspects) : QString(),
                             s.callingOn ? QStringLiteral("1") : QString(), s.shunt ? QStringLiteral("1") : QString(),
                             s.indicator > 0 ? QString::number(s.indicator) : QString() };
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
    if (!l.kavach.isEmpty()) {
        XlsxBook::Sheet k;
        k.name = QLatin1String(kKavachSheet);
        for (const QByteArray &line : QJsonDocument(l.kavach).toJson(QJsonDocument::Indented).split('\n'))
            if (!line.isEmpty()) k.rows << XlsxBook::Row{ QString::fromUtf8(line) };
        out << k;
    }
    if (!l.sheetNotes.isEmpty()) {
        XlsxBook::Sheet k;
        k.name = QLatin1String(kSheetNotes);
        for (const QString &n : l.sheetNotes) k.rows << XlsxBook::Row{ n };
        out << k;
    }
    return out;
}

// ---- JSON -------------------------------------------------------------------------

QByteArray toJson(const Layout &l)
{
    QJsonObject root;
    root.insert(QStringLiteral("format"), QLatin1String(kFormat));
    // Version 2 only with the KAVACH data: a version 1 file stays readable as it was.
    root.insert(QStringLiteral("version"), l.kavach.isEmpty() ? 1 : 2);
    QJsonArray a;
    for (const Tag &t : l.tags)
        a.append(QJsonObject{ { "name", t.name }, { "page_x", t.pageX }, { "page_y", t.pageY } });
    root.insert(QStringLiteral("tags"), a);
    a = QJsonArray();
    for (const Signal &s : l.signalList) {
        QJsonObject o{ { "foot_tag", s.footTag }, { "signal", s.name },
                       { "sig_id", double(s.sigId) }, { "station_id", double(s.stationId) } };
        if (!s.route.isEmpty()) o.insert(QStringLiteral("route"), s.route);   // session 431
        if (s.aspects >= 0) o.insert(QStringLiteral("aspects"), s.aspects);    // session 432
        if (s.callingOn) o.insert(QStringLiteral("calling_on"), true);
        if (s.shunt) o.insert(QStringLiteral("shunt"), true);
        if (s.indicator > 0) o.insert(QStringLiteral("indicator"), s.indicator);   // session 435
        a.append(o);
    }
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
    if (!l.kavach.isEmpty()) root.insert(QStringLiteral("kavach"), l.kavach);
    if (!l.sheetNotes.isEmpty()) root.insert(QStringLiteral("sheet_notes"), QJsonArray::fromStringList(l.sheetNotes));
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
    if (root.value(QStringLiteral("version")).toInt() > 2 && notes)
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
        Signal sg{ s(o, "foot_tag"), s(o, "signal"), n(o, "sig_id"), n(o, "station_id"), s(o, "route") };
        sg.aspects = o.contains(QStringLiteral("aspects")) ? o.value(QStringLiteral("aspects")).toInt() : -1;
        sg.callingOn = o.value(QStringLiteral("calling_on")).toBool();
        sg.shunt = o.value(QStringLiteral("shunt")).toBool();
        sg.indicator = qBound(0, o.value(QStringLiteral("indicator")).toInt(), 6);
        l.signalList << sg;
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
    l.kavach = root.value(QStringLiteral("kavach")).toObject();
    for (const QJsonValue &v : root.value(QStringLiteral("sheet_notes")).toArray())
        if (!v.toString().isEmpty()) l.sheetNotes << v.toString();
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

// ---- session 431: the station XML ------------------------------------------------------

namespace {

const char *kXmlFormat = "kavsite-xml 1";

// The other sheets the XML names: sheet -> <group><row>, columns in the order
// written (a column the sheet adds goes after them), the ones read back as
// numbers, and the list columns written as child elements.
struct XmlSheet { const char *sheet, *group, *row; QStringList columns, numbers; };
const QVector<XmlSheet> &xmlSheets()
{
    static const QVector<XmlSheet> s{
        { "adjustment", "adjustments", "adjustment", { "line", "adjustment_tag", "adjusted_tags" }, { "adjustment_tag" } },
        { "circuits", "circuits", "circuit", { "cid", "name", "line", "location", "startx", "endx", "station" },
          { "cid", "location", "startx", "endx", "station" } },
        { "relaymap", "relays", "relay", { "EqipType", "EqipName", "EqipId", "EqipAttr", "BitPos", "StationId" },
          { "EqipType", "EqipId", "EqipAttr", "BitPos", "StationId" } },
        { "routes", "routes", "route", { "route", "direction", "tags" }, {} },
    };
    return s;
}
// A comma list column -> its child element (<tag id="…"/>).
QString listChild(const QString &column)
{
    if (column == QLatin1String("tags")) return QStringLiteral("tag");
    if (column == QLatin1String("adjusted_tags")) return QStringLiteral("adjusted");
    return QString();
}
const QStringList &connColumns()
{
    static const QStringList c{ QStringLiteral("conn_x_list"), QStringLiteral("conn_line_list"), QStringLiteral("conn_tin_list") };
    return c;
}
const QStringList &connAttrs()
{
    static const QStringList c{ QStringLiteral("x"), QStringLiteral("line"), QStringLiteral("tin") };
    return c;
}
QStringList commaList(const QString &s)
{
    QStringList out;
    for (const QString &x : s.split(QLatin1Char(','), Qt::SkipEmptyParts))
        if (!x.trimmed().isEmpty()) out << x.trimmed();
    return out;
}
bool xmlName(const QString &n)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z_][A-Za-z0-9_.-]*$"));
    return re.match(n).hasMatch();
}
// The tag type as the Config Maker names it (9..12), else its number.
QString typeWord(int type)
{
    static const char *names[] = { "normal", "lc", "adjacent", "adjustment" };
    return type >= 9 && type <= 12 ? QLatin1String(names[type - 9]) : QString::number(type);
}

// The attributes a tag is written with, in order: the fields its bits hold.
QVector<QPair<QString, QString>> tagAttributes(const Tag &t)
{
    QVector<QPair<QString, QString>> a{ { QStringLiteral("tag"), t.name } };
    const QHash<QString, qint64> v = RfidTag::values(RfidTag::fromPages(t.pageX, t.pageY));
    if (!v.isEmpty()) {
        const int type = int(v.value(QStringLiteral("type")));
        auto put = [&a, &v](const char *attr, const char *field) {
            if (v.contains(QLatin1String(field))) a.append({ QLatin1String(attr), QString::number(v.value(QLatin1String(field))) });
        };
        a.append({ QStringLiteral("type"), typeWord(type) });
        put("location", type == 12 ? "abs_loc_1" : "abs_loc");
        put("tin_nom", "tin_nom");
        put("tin_rev", "tin_rev");
        put("placement", "placement");
        put("location2", "abs_loc_2");
        put("dir1", "dir_corr_1");
        put("dir2", "dir_corr_2");
    }
    // 32 hex digits when both halves are 16; else the halves as they are (a
    // malformed tag, which Checks names), so the file does not shift its bits.
    if (t.pageX.size() == 16 && t.pageY.size() == 16) a.append({ QStringLiteral("hex"), t.pageY + t.pageX });
    else a << QPair<QString, QString>{ QStringLiteral("page_x"), t.pageX } << QPair<QString, QString>{ QStringLiteral("page_y"), t.pageY };
    return a;
}

}  // namespace

QByteArray toXml(const Layout &l, const QString &name, QStringList *notes)
{
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.setAutoFormatting(true);
    w.setAutoFormattingIndent(2);
    w.writeStartDocument();
    w.writeStartElement(QStringLiteral("kavsite"));
    w.writeAttribute(QStringLiteral("format"), QLatin1String(kXmlFormat));
    w.writeAttribute(QStringLiteral("name"), name);
    if (l.stations.size() == 1) w.writeAttribute(QStringLiteral("station"), QString::number(l.stations.first().id));
    auto ids = [&w](const QString &child, const QStringList &list) {
        for (const QString &id : list) {
            w.writeEmptyElement(child);
            w.writeAttribute(QStringLiteral("id"), id);
        }
    };

    if (!l.stations.isEmpty()) {
        w.writeStartElement(QStringLiteral("stations"));
        for (const Station &s : l.stations) {
            w.writeEmptyElement(QStringLiteral("station"));
            w.writeAttribute(QStringLiteral("stationid"), QString::number(s.id));
            w.writeAttribute(QStringLiteral("location"), QString::number(s.location));
        }
        w.writeEndElement();
    }
    if (!l.tags.isEmpty()) {
        w.writeStartElement(QStringLiteral("tags"));
        for (const Tag &t : l.tags) {
            w.writeEmptyElement(QStringLiteral("tag"));
            for (const auto &a : tagAttributes(t)) w.writeAttribute(a.first, a.second);
        }
        w.writeEndElement();
    }
    if (!l.lines.isEmpty()) {
        w.writeStartElement(QStringLiteral("lines"));
        for (const Line &ln : l.lines) {
            w.writeStartElement(QStringLiteral("line"));
            if (!ln.name.isEmpty()) w.writeAttribute(QStringLiteral("name"), ln.name);
            if (!ln.line.isEmpty()) w.writeAttribute(QStringLiteral("line"), ln.line);
            ids(QStringLiteral("tag"), ln.tags);
            w.writeEndElement();
        }
        w.writeEndElement();
    }
    if (!l.points.isEmpty()) {
        w.writeStartElement(QStringLiteral("points"));
        for (const Point &p : l.points) {
            w.writeStartElement(QStringLiteral("point"));
            w.writeAttribute(QStringLiteral("point"), p.name);
            w.writeAttribute(QStringLiteral("location1"), QString::number(p.loc1));
            if (!p.line1.isEmpty()) w.writeAttribute(QStringLiteral("line1"), p.line1);
            w.writeAttribute(QStringLiteral("location2"), QString::number(p.loc2));
            if (!p.line2.isEmpty()) w.writeAttribute(QStringLiteral("line2"), p.line2);
            if (p.stationId) w.writeAttribute(QStringLiteral("StationId"), QString::number(p.stationId));
            ids(QStringLiteral("tag"), commaList(p.tags));
            // One <conn> per connection; a list shorter than the others leaves its attribute empty.
            const QStringList xs = commaList(p.connX), ls = commaList(p.connLine), ts = commaList(p.connTin);
            const int n = int(qMax(xs.size(), qMax(ls.size(), ts.size())));
            if (n && (xs.size() != n || ls.size() != n || ts.size() != n) && notes)
                *notes << QStringLiteral("point %1: conn_x_list, conn_line_list and conn_tin_list differ in length").arg(p.name);
            for (int i = 0; i < n; ++i) {
                w.writeEmptyElement(QStringLiteral("conn"));
                w.writeAttribute(QStringLiteral("x"), xs.value(i));
                w.writeAttribute(QStringLiteral("line"), ls.value(i));
                w.writeAttribute(QStringLiteral("tin"), ts.value(i));
            }
            w.writeEndElement();
        }
        w.writeEndElement();
    }
    if (!l.signalList.isEmpty()) {
        w.writeStartElement(QStringLiteral("signals"));
        for (const Signal &s : l.signalList) {
            w.writeEmptyElement(QStringLiteral("signal"));
            if (!s.route.isEmpty()) w.writeAttribute(QStringLiteral("route"), s.route);
            w.writeAttribute(QStringLiteral("sig_foot_tag"), s.footTag);
            w.writeAttribute(QStringLiteral("signal"), s.name);
            w.writeAttribute(QStringLiteral("sig_id"), QString::number(s.sigId));
            if (s.stationId) w.writeAttribute(QStringLiteral("StationId"), QString::number(s.stationId));
            if (s.aspects >= 0) w.writeAttribute(QStringLiteral("aspects"), QString::number(s.aspects));   // session 432
            if (s.callingOn) w.writeAttribute(QStringLiteral("calling_on"), QStringLiteral("1"));
            if (s.shunt) w.writeAttribute(QStringLiteral("shunt"), QStringLiteral("1"));
            if (s.indicator > 0) w.writeAttribute(QStringLiteral("indicator"), QString::number(s.indicator));   // session 435
        }
        w.writeEndElement();
    }

    // The other sheets: the named ones in the Config Maker's order (texts between relays and routes).
    auto otherSheet = [&l](const QString &name) -> const XlsxBook::Sheet * {
        for (const XlsxBook::Sheet &sh : l.otherSheets)
            if (sh.name.compare(name, Qt::CaseInsensitive) == 0) return &sh;
        return nullptr;
    };
    auto writeSheet = [&](const XmlSheet &x) {
        const XlsxBook::Sheet *sh = otherSheet(QLatin1String(x.sheet));
        if (!sh || sh->rows.size() < 2) return;
        QStringList header;
        for (const QVariant &h : sh->rows.first()) header << text(h);
        w.writeStartElement(QLatin1String(x.group));
        for (int r = 1; r < sh->rows.size(); ++r) {
            const XlsxBook::Row &row = sh->rows.at(r);
            w.writeStartElement(QLatin1String(x.row));
            // A cell under no header (the tool's files have notes there: relaymap's "Red", "NORMAL")
            // is written by its place, c7 for the seventh column.
            for (int c = 0; c < row.size(); ++c) {
                const QVariant &cell = row.at(c);   // text as it is (the tool's files have "S50     ")
                const QString v = cell.userType() == QMetaType::QString ? cell.toString() : text(cell);
                const QString h = header.value(c);
                if (v.isEmpty() || !listChild(h).isEmpty()) continue;
                if (h.isEmpty()) w.writeAttribute(QStringLiteral("c%1").arg(c + 1), v);
                else if (xmlName(h)) w.writeAttribute(h, v);
                else if (notes) *notes << QStringLiteral("%1: column \"%2\" is not an XML name, not written").arg(sh->name, h);
            }
            for (int c = 0; c < header.size(); ++c)
                if (!listChild(header.at(c)).isEmpty()) ids(listChild(header.at(c)), commaList(text(row.value(c))));
            w.writeEndElement();
        }
        w.writeEndElement();
    };
    const QVector<XmlSheet> &xs = xmlSheets();
    writeSheet(xs.at(0));   // adjustments
    writeSheet(xs.at(1));   // circuits
    writeSheet(xs.at(2));   // relays
    if (!l.texts.isEmpty()) {
        w.writeStartElement(QStringLiteral("texts"));
        for (const Text &t : l.texts) {
            w.writeEmptyElement(QStringLiteral("text"));
            w.writeAttribute(QStringLiteral("text"), t.text);
            w.writeAttribute(QStringLiteral("location"), QString::number(t.location));
            w.writeAttribute(QStringLiteral("posy"), QString::number(t.posY));
        }
        w.writeEndElement();
    }
    writeSheet(xs.at(3));   // routes
    // Any other sheet, cell by cell under its header.
    for (const XlsxBook::Sheet &sh : l.otherSheets) {
        bool named = false;
        for (const XmlSheet &x : xs) named = named || sh.name.compare(QLatin1String(x.sheet), Qt::CaseInsensitive) == 0;
        if (named) continue;
        w.writeStartElement(QStringLiteral("sheet"));
        w.writeAttribute(QStringLiteral("name"), sh.name);
        for (const XlsxBook::Row &row : sh.rows) {
            w.writeStartElement(QStringLiteral("row"));
            for (const QVariant &v : row) {
                w.writeStartElement(QStringLiteral("cell"));
                if (v.isValid() && v.userType() != QMetaType::QString) w.writeAttribute(QStringLiteral("number"), QStringLiteral("1"));
                w.writeCharacters(v.isValid() ? (v.userType() == QMetaType::QString ? v.toString() : text(v)) : QString());
                w.writeEndElement();
            }
            w.writeEndElement();
        }
        w.writeEndElement();
    }
    if (!l.kavach.isEmpty()) {
        w.writeStartElement(QStringLiteral("kavach"));
        w.writeAttribute(QStringLiteral("format"), QStringLiteral("json"));
        w.writeCharacters(QString::fromUtf8(QJsonDocument(l.kavach).toJson(QJsonDocument::Compact)));
        w.writeEndElement();
    }
    if (!l.sheetNotes.isEmpty()) {   // session 454
        w.writeStartElement(QStringLiteral("sheet_notes"));
        for (const QString &n : l.sheetNotes) w.writeTextElement(QStringLiteral("note"), n);
        w.writeEndElement();
    }

    // Which tags carry each TIN, in each direction: for people and tools reading the file.
    QMap<int, QVector<QPair<QString, QString>>> tins;
    for (const Tag &t : l.tags) {
        const TagInfo i = info(t);
        if (!i.ok) continue;
        if (i.tinNom) tins[i.tinNom].append({ t.name, QStringLiteral("N") });
        if (i.tinRev) tins[i.tinRev].append({ t.name, QStringLiteral("R") });
    }
    if (!tins.isEmpty()) {
        w.writeStartElement(QStringLiteral("tins"));
        for (auto it = tins.cbegin(); it != tins.cend(); ++it) {
            w.writeStartElement(QStringLiteral("tin"));
            w.writeAttribute(QStringLiteral("id"), QString::number(it.key()));
            for (const auto &tag : it.value()) {
                w.writeEmptyElement(QStringLiteral("tag"));
                w.writeAttribute(QStringLiteral("id"), tag.first);
                w.writeAttribute(QStringLiteral("direction"), tag.second);
            }
            w.writeEndElement();
        }
        w.writeEndElement();
    }
    w.writeEndElement();
    w.writeEndDocument();
    return out;
}

bool fromXml(const QByteArray &xml, Layout *out, QString *err, QStringList *notes)
{
    auto note = [notes](const QString &n) { if (notes) *notes << n; };
    QXmlStreamReader r(xml);
    if (!r.readNextStartElement() || r.name() != QLatin1String("kavsite")) {
        if (err) *err = r.hasError() ? QStringLiteral("not XML: %1").arg(r.errorString())
                                     : QStringLiteral("not a station XML file (no <kavsite> root)");
        return false;
    }
    const QString format = r.attributes().value(QStringLiteral("format")).toString();
    if (!format.isEmpty() && format != QLatin1String(kXmlFormat))
        note(QStringLiteral("format \"%1\", read as \"%2\"").arg(format, QLatin1String(kXmlFormat)));
    Layout l;
    auto attr = [&r](const char *name) { return r.attributes().value(QLatin1String(name)).toString().trimmed(); };
    auto number = [&](const char *name, const QString &where) -> qint64 {
        const QString v = attr(name);
        if (v.isEmpty()) return 0;
        bool ok = false;
        const qint64 n = v.toLongLong(&ok);
        if (!ok) note(QStringLiteral("%1: %2 \"%3\" is not a number, read as 0").arg(where, QLatin1String(name), v));
        return ok ? n : 0;
    };
    // The child elements of the current row: <tag id>, <adjusted id>, <conn x line tin>.
    auto children = [&r](QHash<QString, QStringList> *lists, QVector<QStringList> *conns) {
        while (r.readNextStartElement()) {
            const QString child = r.name().toString();
            if (child == QLatin1String("conn")) {
                QStringList c;
                for (const QString &a : connAttrs()) c << r.attributes().value(a).toString().trimmed();
                if (conns) *conns << c;
            } else if (lists) {
                (*lists)[child] << r.attributes().value(QStringLiteral("id")).toString().trimmed();
            }
            r.skipCurrentElement();
        }
    };

    while (r.readNextStartElement()) {
        const QString group = r.name().toString();
        const XmlSheet *named = nullptr;
        for (const XmlSheet &x : xmlSheets()) if (group == QLatin1String(x.group)) named = &x;
        if (group == QLatin1String("stations")) {
            while (r.readNextStartElement()) {
                Station s;
                s.id = number("stationid", QStringLiteral("station"));
                s.location = number("location", QStringLiteral("station %1").arg(s.id));
                l.stations << s;
                r.skipCurrentElement();
            }
        } else if (group == QLatin1String("tags")) {
            while (r.readNextStartElement()) {
                Tag t;
                t.name = attr("tag");
                const QString hex = attr("hex").toLower();
                if (!hex.isEmpty()) {
                    t.pageY = hex.left(16);
                    t.pageX = hex.mid(16);
                } else {   // a Config Maker file of page_x / page_y
                    t.pageX = attr("page_x").toLower();
                    t.pageY = attr("page_y").toLower();
                }
                if (t.pageX.isEmpty() && t.pageY.isEmpty()) {
                    note(QStringLiteral("tag %1: no hex (or page_x / page_y), skipped: the tag is its bits").arg(t.name));
                    r.skipCurrentElement();
                    continue;
                }
                // The fields are what the bits say; one written otherwise is named, the bits kept.
                QHash<QString, QString> given;
                for (const QXmlStreamAttribute &a : r.attributes()) given.insert(a.name().toString(), a.value().toString().trimmed());
                for (const auto &a : tagAttributes(t))
                    if (a.first != QLatin1String("tag") && a.first != QLatin1String("hex") && !a.first.startsWith(QLatin1String("page_"))
                        && given.contains(a.first)
                        && given.value(a.first) != a.second)
                        note(QStringLiteral("tag %1: %2 says %3, its bits %4 (the bits are kept)").arg(t.name, a.first, given.value(a.first), a.second));
                l.tags << t;
                r.skipCurrentElement();
            }
        } else if (group == QLatin1String("lines")) {
            while (r.readNextStartElement()) {
                Line ln;
                ln.name = attr("name");
                ln.line = attr("line");
                QHash<QString, QStringList> lists;
                children(&lists, nullptr);
                ln.tags = lists.value(QStringLiteral("tag"));
                l.lines << ln;
            }
        } else if (group == QLatin1String("points")) {
            while (r.readNextStartElement()) {
                Point p;
                p.name = attr("point");
                const QString where = QStringLiteral("point %1").arg(p.name);
                p.loc1 = number("location1", where);
                p.line1 = attr("line1");
                p.loc2 = number("location2", where);
                p.line2 = attr("line2");
                p.stationId = number("StationId", where);
                QHash<QString, QStringList> lists;
                QVector<QStringList> conns;
                children(&lists, &conns);
                p.tags = lists.value(QStringLiteral("tag")).join(QLatin1Char(','));
                QStringList cx, cl, ct;
                for (const QStringList &c : conns) { cx << c.at(0); cl << c.at(1); ct << c.at(2); }
                p.connX = cx.join(QLatin1Char(','));
                p.connLine = cl.join(QLatin1Char(','));
                p.connTin = ct.join(QLatin1Char(','));
                l.points << p;
            }
        } else if (group == QLatin1String("signals")) {
            while (r.readNextStartElement()) {
                Signal s;
                s.footTag = attr("sig_foot_tag");
                s.name = attr("signal");
                s.sigId = number("sig_id", QStringLiteral("signal %1").arg(s.name));
                s.stationId = number("StationId", QStringLiteral("signal %1").arg(s.name));
                s.route = attr("route");
                readLook(&s, attr("aspects"), attr("calling_on"), attr("shunt"), note, attr("indicator"));
                l.signalList << s;
                r.skipCurrentElement();
            }
        } else if (group == QLatin1String("texts")) {
            while (r.readNextStartElement()) {
                Text t;
                t.text = attr("text");
                t.location = number("location", QStringLiteral("text \"%1\"").arg(t.text));
                t.posY = number("posy", QStringLiteral("text \"%1\"").arg(t.text));
                l.texts << t;
                r.skipCurrentElement();
            }
        } else if (named) {
            // Back into the sheet the Python tool has: its columns the file uses, in its order,
            // then any the file adds; a cell under no header (c7="…") at its place.
            static const QRegularExpression placed(QStringLiteral("^c([1-9][0-9]*)$"));
            XlsxBook::Sheet sh;
            sh.name = QLatin1String(named->sheet);
            QStringList extra;
            QSet<QString> used;
            QVector<QHash<QString, QString>> rows;
            QVector<QHash<int, QString>> atPlace;
            while (r.readNextStartElement()) {
                QHash<QString, QString> row;
                QHash<int, QString> byPlace;
                for (const QXmlStreamAttribute &a : r.attributes()) {
                    const QString c = a.name().toString();
                    const QRegularExpressionMatch m = placed.match(c);
                    if (m.hasMatch() && !named->columns.contains(c)) { byPlace.insert(m.captured(1).toInt() - 1, a.value().toString()); continue; }
                    if (!named->columns.contains(c) && !extra.contains(c)) extra << c;
                    used.insert(c);
                    row.insert(c, a.value().toString());
                }
                QHash<QString, QStringList> lists;
                children(&lists, nullptr);
                for (const QString &c : named->columns)
                    if (!listChild(c).isEmpty() && lists.contains(listChild(c))) {
                        row.insert(c, lists.value(listChild(c)).join(QLatin1Char(',')));
                        used.insert(c);
                    }
                rows << row;
                atPlace << byPlace;
            }
            QStringList header;
            for (const QString &c : named->columns) if (used.contains(c)) header << c;
            header << extra;
            XlsxBook::Row head;
            for (const QString &c : header) head << c;
            sh.rows << head;
            for (int i = 0; i < rows.size(); ++i) {
                XlsxBook::Row cells;
                for (const QString &c : header) cells << cellFor(rows.at(i).value(c), named->numbers.contains(c));
                for (auto it = atPlace.at(i).cbegin(); it != atPlace.at(i).cend(); ++it) {
                    while (cells.size() <= it.key()) cells << QVariant();
                    cells[it.key()] = cellFor(it.value(), true);
                }
                while (!cells.isEmpty() && !cells.last().isValid()) cells.removeLast();
                sh.rows << cells;
            }
            l.otherSheets << sh;
        } else if (group == QLatin1String("sheet")) {
            XlsxBook::Sheet sh;
            sh.name = attr("name");
            while (r.readNextStartElement()) {   // <row>
                XlsxBook::Row row;
                while (r.readNextStartElement()) {   // <cell>
                    const bool isNumber = r.attributes().value(QStringLiteral("number")) == QLatin1String("1");
                    const QString v = r.readElementText();
                    if (v.isEmpty()) row << QVariant();
                    else if (isNumber) {
                        bool ok = false;
                        const qlonglong n = v.toLongLong(&ok);
                        row << (ok ? QVariant(n) : QVariant(v.toDouble()));
                    } else row << v;
                }
                sh.rows << row;
            }
            l.otherSheets << sh;
        } else if (group == QLatin1String("kavach")) {
            const QJsonDocument doc = QJsonDocument::fromJson(r.readElementText().toUtf8());
            if (doc.isObject()) l.kavach = doc.object();
            else note(QStringLiteral("<kavach> is not JSON: not read"));
        } else if (group == QLatin1String("sheet_notes")) {   // session 454
            while (r.readNextStartElement()) {
                if (r.name() == QLatin1String("note")) {
                    const QString n = r.readElementText();
                    if (!n.isEmpty()) l.sheetNotes << n;
                } else {
                    r.skipCurrentElement();
                }
            }
        } else {
            if (group != QLatin1String("tins")) note(QStringLiteral("<%1> is not read").arg(group));   // <tins> is derived
            r.skipCurrentElement();
        }
    }
    if (r.hasError()) {
        if (err) *err = QStringLiteral("XML error at line %1: %2").arg(r.lineNumber()).arg(r.errorString());
        return false;
    }
    *out = l;
    return true;
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
    if (QFileInfo(path).suffix().compare(QLatin1String("xml"), Qt::CaseInsensitive) == 0)   // session 431
        return fromXml(f.readAll(), out, err, notes);
    return fromJson(f.readAll(), out, err, notes);
}

QVector<XlsxBook::Sheet> toToolSheets(const Layout &l)
{
    QVector<XlsxBook::Sheet> out = toSheets(l);
    for (const XlsxBook::Sheet &sh : l.otherSheets)
        if (sh.name.compare(QLatin1String("relaymap"), Qt::CaseInsensitive) == 0) return out;
    XlsxBook::Sheet relay;
    relay.name = QStringLiteral("relaymap");
    XlsxBook::Row header;
    for (const char *c : { "EqipType", "EqipName", "EqipId", "EqipAttr", "BitPos", "StationId" })
        header << QString::fromLatin1(c);
    relay.rows << header;
    out << relay;
    return out;
}

bool save(const QString &path, const Layout &l, QString *err)
{
    if (QFileInfo(path).suffix().compare(QLatin1String("xlsx"), Qt::CaseInsensitive) == 0)
        return XlsxBook::writeFile(path, toToolSheets(l), err);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (err) *err = QStringLiteral("cannot write %1: %2").arg(path, f.errorString());
        return false;
    }
    const bool xml = QFileInfo(path).suffix().compare(QLatin1String("xml"), Qt::CaseInsensitive) == 0;
    f.write(xml ? toXml(l, QFileInfo(path).completeBaseName()) : toJson(l));   // session 431
    if (!f.commit()) {
        if (err) *err = QStringLiteral("cannot write %1: %2").arg(path, f.errorString());
        return false;
    }
    return true;
}

// ---- session 226: comparing two layouts ------------------------------------------------

Diff diff(const Layout &from, const Layout &to)
{
    Diff d;
    QHash<QString, const Tag *> a, b;
    for (const Tag &t : from.tags) a.insert(t.name, &t);
    for (const Tag &t : to.tags) b.insert(t.name, &t);
    auto locOf = [](const Tag &t) { return info(t).absLoc; };
    for (const Tag &t : to.tags) {
        if (!a.contains(t.name)) { d.tags << TagChange{ t.name, TagChange::Added, 0, locOf(t), {} }; continue; }
        const Tag &o = *a.value(t.name);
        if (o.pageX.compare(t.pageX, Qt::CaseInsensitive) == 0 && o.pageY.compare(t.pageY, Qt::CaseInsensitive) == 0) continue;
        const QHash<QString, qint64> va = RfidTag::values(RfidTag::fromPages(o.pageX, o.pageY));
        const QHash<QString, qint64> vb = RfidTag::values(RfidTag::fromPages(t.pageX, t.pageY));
        TagChange c{ t.name, TagChange::Changed, locOf(o), locOf(t), {} };
        if (va.isEmpty() || vb.isEmpty()) {
            c.fields << QStringLiteral("page_x / page_y (%1 does not decode)").arg(va.isEmpty() ? QStringLiteral("the other file's") : QStringLiteral("this one"));
        } else {
            QStringList keys = va.keys() + vb.keys();
            keys.removeDuplicates();
            std::sort(keys.begin(), keys.end());
            for (const QString &k : keys)
                if (va.value(k, -1) != vb.value(k, -1))
                    c.fields << QStringLiteral("%1 %2 → %3").arg(k, va.contains(k) ? QString::number(va.value(k)) : QStringLiteral("—"),
                                                                     vb.contains(k) ? QString::number(vb.value(k)) : QStringLiteral("—"));
            if (c.fields.isEmpty()) c.fields << QStringLiteral("CRC-30");
            bool onlyLoc = true;
            for (const QString &f : c.fields) onlyLoc = onlyLoc && f.startsWith(QLatin1String("abs_loc"));
            if (onlyLoc) c.kind = TagChange::Moved;
        }
        d.tags << c;
    }
    for (const Tag &t : from.tags)
        if (!b.contains(t.name)) d.tags << TagChange{ t.name, TagChange::Removed, locOf(t), 0, {} };

    // The other sheets, by their key.
    auto keyed = [&](const QString &what, const QStringList &x, const QStringList &y) {
        for (const QString &k : y) if (!x.contains(k)) d.other << QStringLiteral("%1 added: %2").arg(what, k);
        for (const QString &k : x) if (!y.contains(k)) d.other << QStringLiteral("%1 removed: %2").arg(what, k);
    };
    auto signalKeys = [](const Layout &l) {
        QStringList out;
        for (const Signal &s : l.signalList) out << QStringLiteral("%1 at tag %2 (sig_id %3)").arg(s.name, s.footTag).arg(s.sigId);
        return out;
    };
    keyed(QStringLiteral("signal"), signalKeys(from), signalKeys(to));
    QHash<QString, QStringList> la, lb;
    for (const Line &l : from.lines) la.insert(l.line, l.tags);
    for (const Line &l : to.lines) lb.insert(l.line, l.tags);
    keyed(QStringLiteral("line"), la.keys(), lb.keys());
    for (const Line &l : to.lines) {
        if (!la.contains(l.line) || la.value(l.line) == l.tags) continue;
        const QStringList &old = la.value(l.line);
        QStringList plus, minus;
        for (const QString &t : l.tags) if (!old.contains(t)) plus << t;
        for (const QString &t : old) if (!l.tags.contains(t)) minus << t;
        d.other << QStringLiteral("line %1: %2 tags → %3%4%5").arg(l.line).arg(old.size()).arg(l.tags.size())
                       .arg(plus.isEmpty() ? QString() : QStringLiteral("; added ") + plus.join(QStringLiteral(", ")))
                       .arg(minus.isEmpty() && plus.isEmpty() ? QStringLiteral("; the order changed")
                            : minus.isEmpty() ? QString() : QStringLiteral("; removed ") + minus.join(QStringLiteral(", ")));
    }
    auto pointText = [](const Point &p) {
        return QStringLiteral("%1 %2 m on %3 to %4 m on %5, tags %6").arg(p.name).arg(p.loc1).arg(p.line1).arg(p.loc2).arg(p.line2, p.tags);
    };
    QHash<QString, QString> pa, pb;
    for (const Point &p : from.points) pa.insert(p.name, pointText(p));
    for (const Point &p : to.points) pb.insert(p.name, pointText(p));
    keyed(QStringLiteral("point"), pa.keys(), pb.keys());
    for (auto it = pb.cbegin(); it != pb.cend(); ++it)
        if (pa.contains(it.key()) && pa.value(it.key()) != it.value())
            d.other << QStringLiteral("point changed: %1 (was %2)").arg(it.value(), pa.value(it.key()));
    auto stationKeys = [](const Layout &l) {
        QStringList out;
        for (const Station &s : l.stations) out << QStringLiteral("%1 at %2 m").arg(s.id).arg(s.location);
        return out;
    };
    keyed(QStringLiteral("station"), stationKeys(from), stationKeys(to));
    auto textKeys = [](const Layout &l) {
        QStringList out;
        for (const Text &t : l.texts) out << QStringLiteral("“%1” at %2 m").arg(t.text).arg(t.location);
        return out;
    };
    keyed(QStringLiteral("text"), textKeys(from), textKeys(to));
    if (from.kavach != to.kavach)
        d.other << (from.kavach.isEmpty() ? QStringLiteral("KAVACH data added")
                    : to.kavach.isEmpty() ? QStringLiteral("KAVACH data removed")
                                          : QStringLiteral("KAVACH data changed (signals, routes or profiles)"));
    if (from.sheetNotes != to.sheetNotes)
        d.other << QStringLiteral("sheet notes: %1 -> %2").arg(from.sheetNotes.size()).arg(to.sheetNotes.size());
    std::sort(d.other.begin(), d.other.end());
    return d;
}

}  // namespace StationLayout
