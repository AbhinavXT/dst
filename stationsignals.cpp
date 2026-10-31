#include "stationsignals.h"

#include "schema/schemadecoder.h"

#include <QCoreApplication>
#include <QHash>

namespace StationSignals {

namespace {
QString tr(const char *s) { return QCoreApplication::translate("StationSignals", s); }
QString undefinedName() { return QStringLiteral("undefined"); }
}

SigInfo unpack(qint64 v)
{
    SigInfo s;
    s.line = int(v & 0x1F);
    s.dir = int((v >> 5) & 0xF);
    s.kind = int((v >> 9) & 0x3F);
    s.running = (v >> 15) & 1;
    s.stop = (v >> 16) & 1;
    return s;
}

qint64 pack(const SigInfo &s)
{
    return qint64(s.line & 0x1F) | qint64(s.dir & 0xF) << 5 | qint64(s.kind & 0x3F) << 9
         | qint64(s.running ? 1 : 0) << 15 | qint64(s.stop ? 1 : 0) << 16;
}

QString describe(qint64 v)
{
    if (!v) return tr("not given");
    const SigInfo s = unpack(v);
    QStringList parts;
    parts << Schema::sigDirName(s.dir) << Schema::sigKindName(s.kind);
    if (s.line) parts << tr("line %1").arg(s.line);
    if (s.running) parts << tr("running override");
    if (s.stop) parts << tr("stop signal");
    return parts.join(QStringLiteral(" · "));
}

QVector<QPair<int, QString>> kinds()
{
    QVector<QPair<int, QString>> out;
    for (int k = 0; k < 64; ++k)
        if (Schema::sigKindName(k) != undefinedName()) out.append({ k, Schema::sigKindName(k) });
    return out;
}

QVector<QPair<int, QString>> directions()
{
    QVector<QPair<int, QString>> out;
    for (int d = 0; d < 16; ++d)
        if (Schema::sigDirName(d) != undefinedName()) out.append({ d, Schema::sigDirName(d) });
    return out;
}

QStringList reconcile(const StationLayout::Layout &l, const StationKavach::Station &s)
{
    QStringList out;
    if (s.signalList.isEmpty() && s.routes.isEmpty()) return out;
    QHash<QString, int> layoutFoot;    // station signal -> its foot tag number
    for (const StationLayout::Signal &g : l.signalList) {
        if (!g.route.isEmpty() || g.name.trimmed().isEmpty()) continue;
        QString foot = g.footTag.trimmed();
        if (foot.endsWith(QLatin1Char('D'))) foot.chop(1);
        layoutFoot.insert(g.name.trimmed(), foot.toInt());
    }
    if (layoutFoot.isEmpty()) return out;   // a file of KAVACH data alone (no station signals drawn): nothing to compare
    for (const StationKavach::Signal &k : s.signalList) {
        if (!layoutFoot.contains(k.name)) {
            out << tr("signal %1 is in the KAVACH data, not in the layout's signals").arg(k.name);
            continue;
        }
        if (layoutFoot.value(k.name) != k.footTag)
            out << tr("signal %1: the KAVACH data puts its foot at tag %2, the layout at tag %3")
                       .arg(k.name).arg(k.footTag).arg(layoutFoot.value(k.name));
    }
    QStringList used;   // only the layout's signals a route names: a signal on no route needs no KAVACH row
    for (const StationKavach::Route &r : s.routes) used << r.entry << r.exit;
    for (auto it = layoutFoot.cbegin(); it != layoutFoot.cend(); ++it)
        if (s.signalIndex(it.key()) < 0 && used.contains(it.key()))
            out << tr("signal %1 is in the layout, and a route names it, but the KAVACH data has no row for it").arg(it.key());
    out.sort();
    return out;
}

QVector<int> previewAspects() { return { 1, 15, 2, 10, 11 }; }

QVector<MaRow> maTable(const StationKavach::Station &s, int route)
{
    QVector<MaRow> out;
    if (route < 0 || route >= s.routes.size()) return out;
    const StationKavach::Route &r = s.routes.at(route);
    for (int aspect : previewAspects()) {
        StationKavach::State st = StationKavach::State::initial(s);
        for (const StationKavach::Signal &g : s.signalList) st.aspect.insert(g.name, 11);   // beyond: Green
        st.aspect.insert(r.exit, aspect);
        for (const QString &p : r.pointsReverse) st.reverse.insert(p);
        const StationKavach::Sections sec = StationKavach::sectionsFrom(s, st, route);
        MaRow row;
        row.aspect = aspect;
        row.wanted = sec.wanted;
        row.got = sec.routes.size();
        row.maEnd = sec.maEnd;
        for (int i : sec.routes) row.routes << s.routes.value(i).name;
        row.note = !sec.broken.isEmpty() ? sec.broken : sec.endsShort;
        out << row;
    }
    return out;
}

}  // namespace StationSignals
