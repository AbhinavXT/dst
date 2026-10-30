#include "layoutdrawing.h"

#include "tinlayout.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cstdlib>

namespace LayoutDrawing {

namespace {

QString tr(const char *s) { return QCoreApplication::translate("LayoutDrawing", s); }

QString km(qint64 m) { return QString::number(m / 1000.0, 'f', 3); }

QString tinText(int tin) { return tin <= 0 ? tr("none") : QStringLiteral("N-%1").arg(tin); }

// The schema's rfidType / rfidPlace labels.
QString typeText(int type)
{
    switch (type) {
    case 9:  return tr("9 (Normal)");
    case 10: return tr("10 (LC)");
    case 11: return tr("11 (Adjacent-Line)");
    case 12: return tr("12 (Adjustment/Junction)");
    default: return QString::number(type);
    }
}

QString placementText(int p)
{
    static const char *const kPlace[] = {
        QT_TRANSLATE_NOOP("LayoutDrawing", "In line section"), QT_TRANSLATE_NOOP("LayoutDrawing", "Signal foot (Nominal)"),
        QT_TRANSLATE_NOOP("LayoutDrawing", "Signal foot (Reverse)"), QT_TRANSLATE_NOOP("LayoutDrawing", "Turnout"),
        QT_TRANSLATE_NOOP("LayoutDrawing", "Exit (Nominal)"), QT_TRANSLATE_NOOP("LayoutDrawing", "Exit (Reverse)"),
        QT_TRANSLATE_NOOP("LayoutDrawing", "Signal foot (both)"), QT_TRANSLATE_NOOP("LayoutDrawing", "Exit (both)"),
        QT_TRANSLATE_NOOP("LayoutDrawing", "Dead Stop (Nominal)"), QT_TRANSLATE_NOOP("LayoutDrawing", "Dead Stop (Reverse)") };
    return p >= 0 && p < 10 ? QStringLiteral("%1 (%2)").arg(p).arg(tr(kPlace[p])) : tr("%1 (reserved)").arg(p);
}

// The way a placement faces: +1 nominal, -1 reverse, 0 both or none.
int placementDir(int p)
{
    switch (p) {
    case 1: case 4: case 8: return 1;
    case 2: case 5: case 9: return -1;
    default: return 0;
    }
}

QString drawn(const Tag &t)
{
    return QString(t.kind) + (t.dir > 0 ? QStringLiteral("▷") : t.dir < 0 ? QStringLiteral("◁") : QString());
}

}  // namespace

bool fromJson(const QByteArray &json, Drawing *out, QString *err)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    const QJsonObject o = doc.object();
    if (pe.error != QJsonParseError::NoError || o.value(QStringLiteral("format")).toString() != QStringLiteral("dlconsole-layout-drawing")) {
        if (err) *err = pe.error != QJsonParseError::NoError ? pe.errorString() : tr("not a tag-layout drawing (format \"dlconsole-layout-drawing\")");
        return false;
    }
    Drawing d;
    d.title = o.value(QStringLiteral("title")).toString();
    d.source = o.value(QStringLiteral("source")).toString();
    const QJsonObject st = o.value(QStringLiteral("station")).toObject();
    d.stationId = st.value(QStringLiteral("id")).toVariant().toLongLong();
    d.stationLoc = st.value(QStringLiteral("location")).toVariant().toLongLong();
    for (const QJsonValue &v : o.value(QStringLiteral("tags")).toArray()) {
        const QJsonObject r = v.toObject();
        Tag t;
        t.number = r.value(QStringLiteral("tag")).toInt();
        t.loc = r.value(QStringLiteral("loc")).toVariant().toLongLong();
        const QString kind = r.value(QStringLiteral("kind")).toString();
        if (!kind.isEmpty()) t.kind = kind.at(0);
        const QString dir = r.value(QStringLiteral("dir")).toString();
        t.dir = dir == QStringLiteral(">") ? 1 : dir == QStringLiteral("<") ? -1 : 0;
        t.line = r.value(QStringLiteral("line")).toString();
        const QJsonArray tin = r.value(QStringLiteral("tin")).toArray();
        if (tin.size() == 2) { t.tinRev = tin.at(0).toInt(); t.tinNom = tin.at(1).toInt(); }
        t.dashed = r.value(QStringLiteral("dashed")).toBool();
        t.border = r.value(QStringLiteral("border")).toBool();
        if (t.number <= 0 || t.kind.isNull()) {
            if (err) *err = tr("a tag without a number or a kind");
            return false;
        }
        d.tags << t;
    }
    for (const QJsonValue &v : o.value(QStringLiteral("signals")).toArray()) {
        const QJsonObject r = v.toObject();
        d.signalList << Signal{ r.value(QStringLiteral("name")).toString(), r.value(QStringLiteral("foot")).toInt() };
    }
    *out = d;
    return true;
}

bool load(const QString &path, Drawing *out, QString *err)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = f.errorString();
        return false;
    }
    return fromJson(f.readAll(), out, err);
}

QStringList builtIn()
{
    QStringList out;
    QDirIterator it(QStringLiteral(":/layout_drawings"), { QStringLiteral("*.json") }, QDir::Files);
    while (it.hasNext()) out << it.next();
    std::sort(out.begin(), out.end());
    return out;
}

QVector<Finding> check(const Drawing &d, const StationLayout::Layout &l)
{
    QVector<Finding> out;
    if (d.tags.isEmpty()) return out;

    // The layout's main tags by number: their keys and what their bits say.
    struct Copy { QString key; StationLayout::TagInfo in; };
    const QStringList keys = StationLayout::tagKeys(l);
    QHash<int, QVector<Copy>> byNumber;
    QVector<Copy> all;
    for (int i = 0; i < l.tags.size(); ++i) {
        const StationLayout::TagInfo in = StationLayout::info(l.tags.at(i));
        if (!in.ok || in.duplicate) continue;
        byNumber[in.unique] << Copy{ keys.at(i), in };
        all << Copy{ keys.at(i), in };
    }
    qint64 lo = d.tags.first().loc, hi = lo;
    QSet<int> numbers;
    for (const Tag &t : d.tags) { lo = std::min(lo, t.loc); hi = std::max(hi, t.loc); numbers.insert(t.number); }

    if (d.stationId > 0 && !l.stations.isEmpty()) {
        const auto it = std::find_if(l.stations.cbegin(), l.stations.cend(), [&](const StationLayout::Station &s) { return s.id == d.stationId; });
        if (it == l.stations.cend())
            out << Finding{ Finding::Station, QString(), tr("station: %1 on the drawing; the layout's station sheet has %2")
                                                              .arg(d.stationId).arg(l.stations.first().id) };
        else if (d.stationLoc > 0 && it->location != d.stationLoc)
            out << Finding{ Finding::Station, QString(), tr("station %1: centre line at %2 km on the drawing, %3 km in the station sheet")
                                                              .arg(d.stationId).arg(km(d.stationLoc), km(it->location)) };
    }

    QSet<QString> used;
    QStringList lines;                        // the drawing's lines, in order
    QHash<QString, QStringList> missing;      // line -> tag numbers not in the layout
    for (const Tag &t : d.tags) {
        if (!lines.contains(t.line)) lines << t.line;
        const QVector<Copy> copies = byNumber.value(t.number);
        if (copies.isEmpty()) { missing[t.line] << QString::number(t.number); continue; }
        int best = 0;
        for (int i = 1; i < copies.size(); ++i)
            if (std::llabs(copies.at(i).in.absLoc - t.loc) < std::llabs(copies.at(best).in.absLoc - t.loc)) best = i;
        const Copy &c = copies.at(best);
        used.insert(c.key);
        if (c.in.absLoc != t.loc)
            out << Finding{ Finding::Moved, c.key, tr("tag %1: %2 km on the drawing, %3 km in the layout (%4%5 m)")
                                                       .arg(t.number).arg(km(t.loc), km(c.in.absLoc))
                                                       .arg(c.in.absLoc > t.loc ? QStringLiteral("+") : QString()).arg(c.in.absLoc - t.loc) };
        const QChar letter = TinLayout::notation(c.in.type, c.in.placement);
        const int pd = placementDir(c.in.placement);
        if (letter != t.kind || (t.dir != 0 && pd != 0 && pd != t.dir)) {
            QString was = tr("type %1").arg(typeText(c.in.type));
            if (c.in.type != 11) was += tr(", placement %1").arg(placementText(c.in.placement));
            out << Finding{ Finding::KindDiffers, c.key, tr("tag %1: the drawing shows %2 (%3%4); the tag says %5")
                                                             .arg(t.number).arg(drawn(t), TinLayout::notationName(t.kind))
                                                             .arg(t.dir > 0 ? tr(", nominal") : t.dir < 0 ? tr(", reverse") : QString(), was) };
        }
        if (t.tinNom >= 0 && (c.in.tinNom != t.tinNom || c.in.tinRev != t.tinRev))
            out << Finding{ Finding::TinDiffers, c.key, tr("tag %1: %2 | %3 either side on the drawing (reverse | nominal); the tag says tin_rev %4, tin_nom %5")
                                                            .arg(t.number).arg(tinText(t.tinRev), tinText(t.tinNom)).arg(c.in.tinRev).arg(c.in.tinNom) };
        for (int i = 0; i < copies.size(); ++i) {
            const Copy &o = copies.at(i);
            if (i == best || used.contains(o.key) || o.in.absLoc < lo || o.in.absLoc > hi) continue;
            used.insert(o.key);
            out << Finding{ Finding::Repeated, o.key, tr("tag %1 again at %2 km (%3), within the drawing's %4 to %5 km")
                                                          .arg(t.number).arg(km(o.in.absLoc), o.key, km(lo), km(hi)) };
        }
    }
    for (const Copy &c : all)
        if (!used.contains(c.key) && !numbers.contains(c.in.unique) && c.in.absLoc >= lo && c.in.absLoc <= hi)
            out << Finding{ Finding::NotOnDrawing, c.key, tr("tag %1 at %2 km: in the layout, not on the drawing").arg(c.key, km(c.in.absLoc)) };

    // Signals: the drawing's name at its foot tag against the signals sheet.
    QHash<QString, QSet<int>> footsOf;
    QHash<int, QStringList> drawnAt;
    for (const StationLayout::Signal &s : l.signalList) footsOf[s.name].insert(s.footTag.toInt());
    for (const Signal &s : d.signalList) drawnAt[s.foot] << s.name;
    for (const Signal &s : d.signalList) {
        const QSet<int> feet = footsOf.value(s.name);
        if (feet.isEmpty()) {
            if (byNumber.contains(s.foot))
                out << Finding{ Finding::SignalDiffers, QString(), tr("signal %1 at tag %2 on the drawing: the signals sheet has no %1").arg(s.name).arg(s.foot) };
        } else if (!feet.contains(s.foot)) {
            QList<int> f = feet.values();
            std::sort(f.begin(), f.end());
            QStringList fs;
            for (int x : f) fs << QString::number(x);
            out << Finding{ Finding::SignalDiffers, QString(), tr("signal %1: foot tag %2 on the drawing, %3 in the signals sheet")
                                                                   .arg(s.name).arg(s.foot).arg(fs.join(QStringLiteral(", "))) };
        }
    }
    QSet<QString> said;
    for (const StationLayout::Signal &s : l.signalList) {
        const int foot = s.footTag.toInt();
        const QStringList there = drawnAt.value(foot);
        if (there.isEmpty() || there.contains(s.name) || said.contains(s.name)) continue;
        said.insert(s.name);
        out << Finding{ Finding::SignalDiffers, QString(), tr("signals sheet: %1 at tag %2; the drawing has %3 there")
                                                               .arg(s.name).arg(foot).arg(there.join(QStringLiteral(", "))) };
    }

    for (const QString &line : lines) {
        const QStringList m = missing.value(line);
        if (m.isEmpty()) continue;
        int onLine = 0;
        for (const Tag &t : d.tags) onLine += t.line == line;
        out << Finding{ Finding::NotInLayout, QString(),
                        (m.size() == onLine ? tr("%1: none of its %2 tag(s) on the drawing is in the layout: %3")
                                            : tr("%1: %2 tag(s) on the drawing not in the layout: %3"))
                            .arg(line).arg(m.size() == onLine ? onLine : m.size()).arg(m.join(QStringLiteral(", "))) };
    }
    return out;
}

}  // namespace LayoutDrawing
