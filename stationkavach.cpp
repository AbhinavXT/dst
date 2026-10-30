#include "stationkavach.h"

#include <QJsonArray>
#include <QObject>

#include <algorithm>
#include <cstdlib>
#include <functional>

namespace StationKavach {

namespace {

const char *const kStruct[8] = { "MovementAuthority", "StaticSpeedProfile", "GradientProfile", "LCgateProfile",
                                 "TurnoutSpeed", "TagLinking", "TrackCondition", "TSR" };

bool redLike(int aspect) { return aspect == 0 || aspect == 1 || aspect == 24; }
bool isYellow(int aspect) { return (aspect >= 2 && aspect <= 8) || aspect >= 32; }

// --- JSON helpers ---------------------------------------------------------------
QStringList strings(const QJsonValue &v)
{
    QStringList out;
    for (const QJsonValue &x : v.toArray()) out << x.toString();
    return out;
}
QJsonArray stringArray(const QStringList &l) { return QJsonArray::fromStringList(l); }
qint64 num(const QJsonObject &o, const char *k, qint64 def = 0)
{
    const QJsonValue v = o.value(QLatin1String(k));
    return v.isDouble() ? qint64(v.toDouble()) : def;
}
QVector<Row> rows(const QJsonValue &v)
{
    QVector<Row> out;
    for (const QJsonValue &x : v.toArray()) {
        const QJsonObject o = x.toObject();
        Row r;
        for (auto it = o.begin(); it != o.end(); ++it)
            if (it.value().isDouble()) r.insert(it.key(), qint64(it.value().toDouble()));
            else if (it.value().isBool()) r.insert(it.key(), it.value().toBool() ? 1 : 0);
        out << r;
    }
    return out;
}
QJsonArray rowArray(const QVector<Row> &rs)
{
    QJsonArray a;
    for (const Row &r : rs) {
        QJsonObject o;
        for (auto it = r.begin(); it != r.end(); ++it) o.insert(it.key(), double(it.value()));
        a.append(o);
    }
    return a;
}

bool pointsSet(const Route &r, const State &st)
{
    for (const QString &p : r.pointsNormal) if (st.reverse.contains(p)) return false;
    for (const QString &p : r.pointsReverse) if (!st.reverse.contains(p)) return false;
    return true;
}

bool holds(const Route &r, int tag)
{
    for (const RouteTag &t : r.tags) if (t.tag == tag) return true;
    return false;
}

// Of `cands` (route indexes), the first whose points are set; else the first.
int bySetPoints(const Station &s, const State &st, const QVector<int> &cands)
{
    for (int i : cands) if (pointsSet(s.routes[i], st)) return i;
    return cands.isEmpty() ? -1 : cands.first();
}

// What the live state says against route `r`, one line each.
void routeChecks(const Route &r, const State &st, QStringList *out)
{
    for (const QString &p : r.pointsNormal)
        if (st.reverse.contains(p)) *out << QObject::tr("%1: point %2 lies Reverse, the route needs it Normal").arg(r.name, p);
    for (const QString &p : r.pointsReverse)
        if (!st.reverse.contains(p)) *out << QObject::tr("%1: point %2 lies Normal, the route needs it Reverse").arg(r.name, p);
    for (const QString &t : r.trackCircuits)
        if (st.occupied.contains(t)) *out << QObject::tr("%1: track circuit %2 is occupied").arg(r.name, t);
    for (const QString &t : r.tins)
        if (st.occupied.contains(t)) *out << QObject::tr("%1: TIN %2 is occupied").arg(r.name, t);
}

// Whether every value of `se` fits its field; *why names the first that does not.
bool fits(const Schema::Encoder &enc, const Schema::SubEntry &se, QString *why)
{
    const Schema::StructLayout sl = enc.structLayout(QLatin1String(kStruct[se.type]));
    auto ok = [why, &se](const Schema::FieldInfo &f, qint64 v, const QString &where) {
        if (f.bits <= 0 || f.bits >= 63) return true;
        const qint64 lo = f.isSigned ? -(qint64(1) << (f.bits - 1)) : 0;
        const qint64 hi = f.isSigned ? (qint64(1) << (f.bits - 1)) - 1 : (qint64(1) << f.bits) - 1;
        if (v >= lo && v <= hi) return true;
        *why = QObject::tr("%1 %2%3 = %4 does not fit its %5 bits")
                   .arg(QLatin1String(kStruct[se.type]), where, f.name).arg(v).arg(f.bits);
        return false;
    };
    for (const Schema::FieldInfo &f : sl.scalarFields)
        if (se.values.contains(f.name) && !ok(f, se.values.value(f.name), QString())) return false;
    for (const Schema::RepeatInfo &ri : sl.repeats) {
        const QVector<Row> rs = se.repeats.value(ri.name);
        if (rs.size() < ri.cmin || rs.size() > ri.cmax) {
            *why = QObject::tr("%1 would carry %2 %3 rows; it holds %4 to %5")
                       .arg(QLatin1String(kStruct[se.type])).arg(rs.size()).arg(ri.name).arg(ri.cmin).arg(ri.cmax);
            return false;
        }
        for (int i = 0; i < rs.size(); ++i)
            for (const Schema::FieldInfo &f : ri.fields)
                if (rs[i].contains(f.name) && !ok(f, rs[i].value(f.name), QStringLiteral("%1[%2].").arg(ri.name).arg(i + 1)))
                    return false;
    }
    return true;
}

QByteArray keyOf(int route, const QVector<Schema::SubEntry> &subs)
{
    QByteArray k = QByteArray::number(route);
    auto add = [&k](const Row &r) {
        QStringList names = r.keys();
        std::sort(names.begin(), names.end());
        for (const QString &n : names) k += ' ' + n.toLatin1() + '=' + QByteArray::number(r.value(n));
        k += ';';
    };
    for (const Schema::SubEntry &se : subs) {
        k += '|' + QByteArray::number(se.type);
        add(se.values);
        QStringList reps = se.repeats.keys();
        std::sort(reps.begin(), reps.end());
        for (const QString &rn : reps) {
            k += rn.toLatin1() + ':';
            for (const Row &r : se.repeats.value(rn)) add(r);
        }
    }
    return k;
}

// Rows that each start where the one before ends (role="seglen"), section by
// section, cut at `end`. *why set when a section's rows do not cover it.
bool segments(const QVector<const Route *> &secs, QVector<Row> Route::*list, const char *what, qint64 end,
              QVector<Row> *out, QString *why)
{
    bool any = false;
    for (const Route *r : secs) any = any || !(r->*list).isEmpty();
    if (!any) return true;
    qint64 pos = 0;
    for (const Route *r : secs) {
        qint64 sum = 0;
        for (const Row &row : r->*list) sum += row.value(QStringLiteral("d"));
        if (sum != r->length()) {
            *why = QObject::tr("route %1: its %2 covers %3 m of its %4 m").arg(r->name, QLatin1String(what)).arg(sum).arg(r->length());
            return false;
        }
        for (Row row : r->*list) {
            if (pos >= end) return true;
            const qint64 d = std::min<qint64>(row.value(QStringLiteral("d")), end - pos);
            row.insert(QStringLiteral("d"), d);
            *out << row;
            pos += d;
        }
    }
    return true;
}

// Rows placed by a start distance (`start`), moved by each section's offset,
// kept when they start before `end`; `len` (if given) cut at `end`.
QVector<Row> placed(const QVector<const Route *> &secs, const QVector<qint64> &off, QVector<Row> Route::*list,
                    const char *start, const char *len, qint64 end, const std::function<bool(const Route &, const Row &)> &keep = {})
{
    QVector<Row> out;
    for (int i = 0; i < secs.size(); ++i)
        for (Row row : secs[i]->*list) {
            if (keep && !keep(*secs[i], row)) continue;
            const qint64 at = row.value(QLatin1String(start)) + off[i];
            if (at >= end) continue;
            row.insert(QLatin1String(start), at);
            if (len) row.insert(QLatin1String(len), std::min<qint64>(row.value(QLatin1String(len)), end - at));
            out << row;
        }
    return out;
}

// A speed row as the schema reads it: class 1 when it names a, b or c.
Row speedRow(Row r)
{
    if (!r.contains(QStringLiteral("class")))
        r.insert(QStringLiteral("class"), (r.contains(QStringLiteral("a")) || r.contains(QStringLiteral("b"))
                                           || r.contains(QStringLiteral("c"))) ? 1 : 0);
    return r;
}

}  // namespace

// ---- the data -------------------------------------------------------------------

int Route::length() const
{
    int n = 0;
    for (int i = 1; i < tags.size(); ++i) n += tags[i].dist;
    return n;
}

int Station::signalIndex(const QString &name) const
{
    for (int i = 0; i < signalList.size(); ++i) if (signalList[i].name == name) return i;
    return -1;
}

static QStringList unique(const QVector<Route> &rs, std::function<QStringList(const Route &)> f)
{
    QStringList out;
    for (const Route &r : rs) for (const QString &n : f(r)) if (!out.contains(n)) out << n;
    return out;
}
QStringList Station::pointNames() const
{
    return unique(routes, [](const Route &r) { return r.pointsNormal + r.pointsReverse; });
}
QStringList Station::trackCircuitNames() const
{
    return unique(routes, [](const Route &r) { return r.trackCircuits; });
}
QStringList Station::tinNames() const
{
    return unique(routes, [](const Route &r) { return r.tins; });
}

int defaultSections(int aspect)
{
    if (isYellow(aspect)) return 2;
    if (aspect == 10 || aspect == 12 || aspect == 13) return 3;
    if (aspect == 11 || aspect == 14) return 4;
    return 1;
}

int sectionsFor(const Station &s, int aspect)
{
    return s.sections.contains(aspect) ? s.sections.value(aspect) : defaultSections(aspect);
}

QString aspectName(int a)
{
    if (a == 0) return QObject::tr("Unidentified");
    if (a == 1) return QObject::tr("Red");
    if (isYellow(a)) return a == 2 ? QObject::tr("Yellow") : QObject::tr("Yellow (route %1)").arg(a);
    if (a == 10 || a == 12 || a == 13) return QObject::tr("Double Yellow");
    if (a == 11) return QObject::tr("Green");
    if (a == 14) return QObject::tr("AG marker");
    if (a == 15) return QObject::tr("Calling-on");
    if (a == 24) return QObject::tr("Stop board");
    return QString::number(a);
}

Sections sectionsFrom(const Station &s, const State &st, int route)
{
    Sections out;
    if (route < 0 || route >= s.routes.size()) return out;
    const Route &r0 = s.routes[route];
    out.aspect = st.aspectOf(s, r0.exit);
    out.wanted = std::max(1, sectionsFor(s, out.aspect));
    out.routes << route;
    out.offsets << 0;
    while (out.routes.size() < out.wanted) {
        const Route &last = s.routes[out.routes.last()];
        QVector<int> next;
        for (int i = 0; i < s.routes.size(); ++i)
            if (s.routes[i].entry == last.exit && s.routes[i].pktDir == r0.pktDir) next << i;
        const int ni = bySetPoints(s, st, next);
        if (ni < 0) {
            out.endsShort = QObject::tr("%1 %2 wants %3 sections; no route on from %4: MA ends there")
                                .arg(r0.exit, aspectName(out.aspect)).arg(out.wanted).arg(last.exit);
            break;
        }
        const Route &n = s.routes[ni];
        if (n.tags.isEmpty() || last.tags.isEmpty() || n.tags.first().tag != last.tags.last().tag) {
            out.broken = QObject::tr("route %1 starts on tag %2, not on tag %3 where %4 ends")
                             .arg(n.name).arg(n.tags.isEmpty() ? 0 : n.tags.first().tag)
                             .arg(last.tags.isEmpty() ? 0 : last.tags.last().tag).arg(last.name);
            break;
        }
        out.offsets << out.offsets.last() + last.length();
        out.routes << ni;
    }
    out.maEnd = out.offsets.last() + s.routes[out.routes.last()].length();
    return out;
}

bool fromJson(const QJsonObject &o, Station *out, QString *err, QStringList *notes)
{
    if (!o.contains(QStringLiteral("routes"))) {
        if (err) *err = QObject::tr("no routes in the station's KAVACH data");
        return false;
    }
    Station s;
    s.stationId = int(num(o, "station_id"));
    s.stnVersion = int(num(o, "stn_version"));
    const QJsonObject sec = o.value(QStringLiteral("sections")).toObject();
    for (auto it = sec.begin(); it != sec.end(); ++it) s.sections.insert(it.key().toInt(), it.value().toInt());
    for (const QJsonValue &v : o.value(QStringLiteral("signals")).toArray()) {
        const QJsonObject x = v.toObject();
        Signal g;
        g.name = x.value(QStringLiteral("name")).toString();
        g.footTag = int(num(x, "foot_tag"));
        g.sigInfo = num(x, "sig_info");
        g.aspect = int(num(x, "aspect", 1));
        if (g.name.isEmpty()) { if (notes) *notes << QObject::tr("a signal without a name: skipped"); continue; }
        s.signalList << g;
    }
    for (const QJsonValue &v : o.value(QStringLiteral("routes")).toArray()) {
        const QJsonObject x = v.toObject();
        Route r;
        r.name = x.value(QStringLiteral("name")).toString();
        r.entry = x.value(QStringLiteral("entry")).toString();
        r.exit = x.value(QStringLiteral("exit")).toString();
        r.pktDir = int(num(x, "pkt_dir", 1));
        r.sectionType = int(num(x, "section_type"));
        r.authority = int(num(x, "authority", 2));
        r.osSpeed = int(num(x, "os_speed"));
        r.pointsNormal = strings(x.value(QStringLiteral("points_normal")));
        r.pointsReverse = strings(x.value(QStringLiteral("points_reverse")));
        r.trackCircuits = strings(x.value(QStringLiteral("track_circuits")));
        r.tins = strings(x.value(QStringLiteral("tins")));
        for (const Row &t : rows(x.value(QStringLiteral("tags"))))
            r.tags << RouteTag{ int(t.value(QStringLiteral("dist"))), int(t.value(QStringLiteral("tag"))),
                                int(t.value(QStringLiteral("dup_dir"))) };
        r.distDupTag = int(num(x, "dist_dup_tag"));
        for (const QJsonValue &t : x.value(QStringLiteral("adj_tins")).toArray()) r.adjTins << t.toInt();
        r.tin = int(num(x, "tin"));
        const QJsonObject lr = x.value(QStringLiteral("loc_reset")).toObject();
        if (!lr.isEmpty()) {
            r.locReset = true;
            r.locResetStart = int(num(lr, "start"));
            r.adjLocoDir = int(num(lr, "adj_dir"));
            r.absLocCorrection = num(lr, "correction");
        }
        r.ssp = rows(x.value(QStringLiteral("ssp")));
        r.gradient = rows(x.value(QStringLiteral("gradient")));
        r.lc = rows(x.value(QStringLiteral("lc")));
        r.turnouts = rows(x.value(QStringLiteral("turnouts")));
        r.trackConditions = rows(x.value(QStringLiteral("track_conditions")));
        r.tsr = rows(x.value(QStringLiteral("tsr")));
        if (r.name.isEmpty() || r.tags.isEmpty()) {
            if (notes) *notes << QObject::tr("a route without a name or tags: skipped");
            continue;
        }
        s.routes << r;
    }
    *out = s;
    return true;
}

QJsonObject toJson(const Station &s)
{
    QJsonObject o;
    o.insert(QStringLiteral("station_id"), s.stationId);
    o.insert(QStringLiteral("stn_version"), s.stnVersion);
    if (!s.sections.isEmpty()) {
        QJsonObject sec;
        for (auto it = s.sections.begin(); it != s.sections.end(); ++it) sec.insert(QString::number(it.key()), it.value());
        o.insert(QStringLiteral("sections"), sec);
    }
    QJsonArray sigs;
    for (const Signal &g : s.signalList)
        sigs.append(QJsonObject{ { "name", g.name }, { "foot_tag", g.footTag }, { "sig_info", double(g.sigInfo) },
                                 { "aspect", g.aspect } });
    o.insert(QStringLiteral("signals"), sigs);
    QJsonArray routes;
    for (const Route &r : s.routes) {
        QJsonObject x{ { "name", r.name }, { "entry", r.entry }, { "exit", r.exit }, { "pkt_dir", r.pktDir },
                       { "section_type", r.sectionType }, { "authority", r.authority }, { "os_speed", r.osSpeed },
                       { "points_normal", stringArray(r.pointsNormal) }, { "points_reverse", stringArray(r.pointsReverse) },
                       { "track_circuits", stringArray(r.trackCircuits) }, { "tins", stringArray(r.tins) },
                       { "dist_dup_tag", r.distDupTag }, { "tin", r.tin } };
        QJsonArray tags;
        for (const RouteTag &t : r.tags) {
            QJsonObject to{ { "dist", t.dist }, { "tag", t.tag } };
            if (t.dupDir) to.insert(QStringLiteral("dup_dir"), t.dupDir);
            tags.append(to);
        }
        x.insert(QStringLiteral("tags"), tags);
        QJsonArray adj;
        for (int t : r.adjTins) adj.append(t);
        x.insert(QStringLiteral("adj_tins"), adj);
        if (r.locReset)
            x.insert(QStringLiteral("loc_reset"), QJsonObject{ { "start", r.locResetStart }, { "adj_dir", r.adjLocoDir },
                                                               { "correction", double(r.absLocCorrection) } });
        x.insert(QStringLiteral("ssp"), rowArray(r.ssp));
        x.insert(QStringLiteral("gradient"), rowArray(r.gradient));
        x.insert(QStringLiteral("lc"), rowArray(r.lc));
        x.insert(QStringLiteral("turnouts"), rowArray(r.turnouts));
        x.insert(QStringLiteral("track_conditions"), rowArray(r.trackConditions));
        x.insert(QStringLiteral("tsr"), rowArray(r.tsr));
        routes.append(x);
    }
    o.insert(QStringLiteral("routes"), routes);
    return o;
}

QStringList checks(const Station &s)
{
    QStringList out;
    for (const Route &r : s.routes) {
        const int e = s.signalIndex(r.entry), x = s.signalIndex(r.exit);
        if (e < 0) out << QObject::tr("route %1: entry signal %2 is not in the signals").arg(r.name, r.entry);
        else if (s.signalList[e].footTag != r.tags.first().tag)
            out << QObject::tr("route %1 starts on tag %2, not on %3's foot tag %4")
                       .arg(r.name).arg(r.tags.first().tag).arg(r.entry).arg(s.signalList[e].footTag);
        if (x < 0) out << QObject::tr("route %1: exit signal %2 is not in the signals").arg(r.name, r.exit);
        else if (s.signalList[x].footTag != r.tags.last().tag)
            out << QObject::tr("route %1 ends on tag %2, not on %3's foot tag %4")
                       .arg(r.name).arg(r.tags.last().tag).arg(r.exit).arg(s.signalList[x].footTag);
        if (r.tags.first().dist != 0) out << QObject::tr("route %1: its first tag is not at 0 m").arg(r.name);
        const auto sum = [](const QVector<Row> &rs) {
            qint64 n = 0;
            for (const Row &x : rs) n += x.value(QStringLiteral("d"));
            return n;
        };
        if (!r.ssp.isEmpty() && sum(r.ssp) != r.length())
            out << QObject::tr("route %1: its SSP covers %2 m of its %3 m").arg(r.name).arg(sum(r.ssp)).arg(r.length());
        if (!r.gradient.isEmpty() && sum(r.gradient) != r.length())
            out << QObject::tr("route %1: its gradient covers %2 m of its %3 m").arg(r.name).arg(sum(r.gradient)).arg(r.length());
    }
    return out;
}

QStringList tagChecks(const Station &s, const QSet<int> &tagIds)
{
    QStringList out;
    if (tagIds.isEmpty()) return out;
    for (const Signal &g : s.signalList)
        if (!tagIds.contains(g.footTag))
            out << QObject::tr("signal %1: foot tag %2 is not in the layout's tags").arg(g.name).arg(g.footTag);
    for (const Route &r : s.routes) {
        QStringList missing;
        for (const RouteTag &t : r.tags)
            if (!tagIds.contains(t.tag) && !missing.contains(QString::number(t.tag))) missing << QString::number(t.tag);
        if (!missing.isEmpty())
            out << QObject::tr("route %1: tag(s) %2 not in the layout's tags").arg(r.name, missing.join(QStringLiteral(", ")));
    }
    return out;
}

QStringList locationChecks(const Station &s, const QHash<int, QVector<qint64>> &tagLocs)
{
    QStringList out;
    for (const Route &r : s.routes) {
        const int sign = r.pktDir == 2 ? -1 : 1;
        for (int i = 1; i < r.tags.size(); ++i) {
            const int a = r.tags[i - 1].tag, b = r.tags[i].tag;
            const QVector<qint64> la = tagLocs.value(a), lb = tagLocs.value(b);
            if (la.isEmpty() || lb.isEmpty()) continue;
            bool agrees = false;
            for (qint64 x : la) for (qint64 y : lb) agrees = agrees || (y - x) * sign == r.tags[i].dist;
            if (agrees) continue;
            if (la.size() == 1 && lb.size() == 1) {
                const qint64 along = (lb.first() - la.first()) * sign;
                out << QObject::tr("route %1: tag %2 is %3 m on from tag %4; the layout's tags lie %5 m %6")
                           .arg(r.name).arg(b).arg(r.tags[i].dist).arg(a).arg(std::llabs(along))
                           .arg(along >= 0 ? QObject::tr("on") : QObject::tr("back"));
            } else {
                out << QObject::tr("route %1: tag %2 is %3 m on from tag %4; none of the layout's %5 tag(s) %2 lies so "
                                   "from its %6 tag(s) %4")
                           .arg(r.name).arg(b).arg(r.tags[i].dist).arg(a).arg(lb.size()).arg(la.size());
            }
        }
    }
    return out;
}

State State::initial(const Station &s)
{
    State st;
    for (const Signal &g : s.signalList) st.aspect.insert(g.name, g.aspect);
    return st;
}

int State::aspectOf(const Station &s, const QString &signal) const
{
    if (aspect.contains(signal)) return aspect.value(signal);
    const int i = s.signalIndex(signal);
    return i < 0 ? 0 : s.signalList[i].aspect;
}

// ---- the answer -------------------------------------------------------------------

Answer answer(const Station &s, const State &st, Loco *loco, const QHash<QString, qint64> &lsrp,
              const Schema::Encoder &enc, const QList<Loco> &others)
{
    Answer a;
    loco->id = int(lsrp.value(QStringLiteral("SOURCE_LOCO_ID")));
    loco->absLoc = lsrp.value(QStringLiteral("ABS_LOCO_LOC"), -1);
    loco->trainLength = int(lsrp.value(QStringLiteral("TRAIN_LENGTH")));
    loco->dir = int(lsrp.value(QStringLiteral("MOVEMENT_DIR")));
    const int tag = int(lsrp.value(QStringLiteral("LAST_RFID_TAG")));
    const int dir = int(lsrp.value(QStringLiteral("MOVEMENT_DIR")));
    const auto dirOk = [dir](const Route &r) { return dir != 1 && dir != 2 ? true : r.pktDir == dir; };

    // The route: entered (its entry foot tag read), kept, or any holding the tag.
    QVector<int> entering, holding;
    for (int i = 0; i < s.routes.size(); ++i) {
        if (!dirOk(s.routes[i])) continue;
        if (s.routes[i].tags.first().tag == tag) entering << i;
        else if (holds(s.routes[i], tag)) holding << i;
    }
    int ri = bySetPoints(s, st, entering);
    if (ri < 0 && loco->route >= 0 && loco->route < s.routes.size() && holding.contains(loco->route)) ri = loco->route;
    if (ri < 0) ri = bySetPoints(s, st, holding);
    if (ri < 0 && loco->route >= 0 && loco->route < s.routes.size()) ri = loco->route;   // a tag not on any route: stay
    if (ri < 0) {
        a.why = QObject::tr("tag %1 is on no route of station %2").arg(tag).arg(s.stationId);
        return a;
    }
    const Route &r0 = s.routes[ri];

    // The ref: the tag read, when the route holds it; else the one in use, else the entry foot tag.
    int ref = holds(r0, tag) ? tag : (ri == loco->route && holds(r0, loco->refTag) ? loco->refTag : r0.tags.first().tag);
    qint64 refDist = 0;
    for (int i = 0, d = 0; i < r0.tags.size(); ++i) {
        d += i ? r0.tags[i].dist : 0;
        if (r0.tags[i].tag == ref) { refDist = d; break; }
    }

    // Session 426: past the foot of the Red signal ahead (SPAD): SoS, no authority, the MA alone.
    if (holds(r0, tag) && redLike(st.aspectOf(s, r0.exit)) && s.tagLocs.contains(ref)
        && lsrp.contains(QStringLiteral("ABS_LOCO_LOC"))) {
        const qint64 abs = lsrp.value(QStringLiteral("ABS_LOCO_LOC"));
        const QVector<qint64> at = s.tagLocs.value(ref);
        qint64 refLoc = at.first();    // of a number listed more than once, the place nearest the loco
        for (qint64 x : at) if (std::llabs(abs - x) < std::llabs(abs - refLoc)) refLoc = x;
        const qint64 past = refDist + (abs - refLoc) * (r0.pktDir == 2 ? -1 : 1) - r0.length();
        if (past > 0) {
            const int aspect = st.aspectOf(s, r0.exit);
            const int sigIdx = s.signalIndex(r0.exit);
            a.header = { { "PKT_TYPE", 9 }, { "FRAME_NUM", lsrp.value(QStringLiteral("FRAME_NUM")) },
                         { "SOURCE_STN_ID", s.stationId }, { "STN_VERSION", s.stnVersion },
                         { "DEST_LOCO_ID", lsrp.value(QStringLiteral("SOURCE_LOCO_ID")) }, { "REF_PROF_ID", loco->profId },
                         { "LAST_REF_RFID", ref }, { "DIST_PKT_START", 0 }, { "PKT_DIR", r0.pktDir } };
            Schema::SubEntry ma;
            ma.type = 0;
            ma.values = { { "FRAME_OFFSET", 0 }, { "DEST_LOCO_SOS", 4 }, { "TRAIN_SECTION_TYPE", r0.sectionType },
                          { "CUR_SIG_INFO", sigIdx < 0 ? 0 : s.signalList[sigIdx].sigInfo }, { "CUR_SIG_ASPECT", aspect },
                          { "NEXT_SIG_ASPECT", 0 }, { "APPR_SIG_DIST", r0.length() - refDist }, { "AUTHORITY_TYPE", 0 },
                          { "MA_W_R_T_SIG", 0 }, { "REQ_SHORTEN_MA", 0 }, { "TRN_LEN_INFO_STS", 0 }, { "NEXT_STN_COMM", 0 } };
            QString bad;
            if (!fits(enc, ma, &bad)) { a.why = bad; return a; }
            a.subs << ma;
            loco->route = ri;
            loco->refTag = ref;
            a.note = QObject::tr("SPAD: loco %1 m past %2 at %3: SoS SPAD, no authority, MA 0; ref tag %4")
                         .arg(past).arg(r0.exit, aspectName(aspect)).arg(ref);
            a.checks << QObject::tr("loco past %1 at %2 (SPAD)").arg(r0.exit, aspectName(aspect));
            a.ok = true;
            return a;
        }
    }

    // The sections: this route, then on from each exit signal.
    const Sections sx = sectionsFrom(s, st, ri);
    if (!sx.broken.isEmpty()) { a.why = sx.broken; return a; }
    if (!sx.endsShort.isEmpty()) a.checks << sx.endsShort;
    const int curAspect = sx.aspect;
    QVector<const Route *> secs;
    for (int i : sx.routes) secs << &s.routes[i];
    const QVector<qint64> off = sx.offsets;
    const qint64 maEnd = sx.maEnd;

    // Session 427: a loco ahead on these routes, the same way: the MA ends kRearMargin short of its rear.
    qint64 maSent = maEnd;
    QString cutNote;
    if (!others.isEmpty() && s.tagLocs.contains(ref) && lsrp.contains(QStringLiteral("ABS_LOCO_LOC"))) {
        const int sign = r0.pktDir == 2 ? -1 : 1;
        const qint64 abs = lsrp.value(QStringLiteral("ABS_LOCO_LOC"));
        const QVector<qint64> at = s.tagLocs.value(ref);
        qint64 refLoc = at.first();
        for (qint64 x : at) if (std::llabs(abs - x) < std::llabs(abs - refLoc)) refLoc = x;
        const qint64 startAbs = refLoc - sign * refDist;
        const qint64 self = (abs - startAbs) * sign;
        for (const Loco &o : others) {
            if (o.id == loco->id || o.absLoc < 0 || o.dir != dir || !sx.routes.contains(o.route)) continue;
            const qint64 front = (o.absLoc - startAbs) * sign;
            if (front <= self) continue;
            const qint64 cut = std::max<qint64>(0, front - o.trainLength - kRearMargin);
            if (cut < maSent) {
                maSent = cut;
                cutNote = QObject::tr("MA cut to %1 m: loco %2 ahead, its rear at %3 m, less %4 m")
                              .arg(cut).arg(o.id).arg(front - o.trainLength).arg(kRearMargin);
            }
        }
    }
    const bool rearEnd = lsrp.value(QStringLiteral("EMERGENCY_STATUS")) == 5;
    QStringList notes;
    if (entering.size() > 1 || (entering.isEmpty() && holding.size() > 1))
        if (!pointsSet(r0, st)) notes << QObject::tr("no route from tag %1 has its points set; %2 taken").arg(tag).arg(r0.name);

    // What the live state does not agree with.
    for (const Route *r : secs) routeChecks(*r, st, &a.checks);
    const int nextAspect = (redLike(curAspect) || secs.size() < 2) ? 0 : st.aspectOf(s, secs[1]->exit);
    if (secs.size() >= 2) {
        const int after = st.aspectOf(s, secs[1]->exit);
        if ((curAspect == 11 || curAspect == 10 || curAspect == 12 || curAspect == 13) && redLike(after))
            a.checks << QObject::tr("%1 shows %2 with %3 %4 ahead: the interlocking would not clear it so")
                            .arg(r0.exit, aspectName(curAspect), secs[1]->exit, aspectName(after));
    }

    // Header and MA.
    const int sigIdx = s.signalIndex(r0.exit);
    int auth = st.authority.contains(r0.name) ? st.authority.value(r0.name) : r0.authority;
    if (curAspect == 15) auth = 1;   // calling-on: on sight
    const qint64 appr = r0.length() - refDist;
    a.header = { { "PKT_TYPE", 9 }, { "FRAME_NUM", lsrp.value(QStringLiteral("FRAME_NUM")) },
                 { "SOURCE_STN_ID", s.stationId }, { "STN_VERSION", s.stnVersion },
                 { "DEST_LOCO_ID", lsrp.value(QStringLiteral("SOURCE_LOCO_ID")) }, { "REF_PROF_ID", 0 },
                 { "LAST_REF_RFID", ref }, { "DIST_PKT_START", -refDist }, { "PKT_DIR", r0.pktDir } };
    Schema::SubEntry ma;
    ma.type = 0;
    ma.values = { { "FRAME_OFFSET", 0 }, { "DEST_LOCO_SOS", rearEnd ? 5 : 0 }, { "TRAIN_SECTION_TYPE", r0.sectionType },
                  { "CUR_SIG_INFO", sigIdx < 0 ? 0 : s.signalList[sigIdx].sigInfo }, { "CUR_SIG_ASPECT", curAspect },
                  { "NEXT_SIG_ASPECT", nextAspect }, { "APPR_SIG_DIST", appr }, { "AUTHORITY_TYPE", auth },
                  { "MA_W_R_T_SIG", maSent }, { "REQ_SHORTEN_MA", 0 }, { "TRN_LEN_INFO_STS", 0 }, { "NEXT_STN_COMM", 0 } };
    if (auth == 1) ma.values.insert(QStringLiteral("AUTHORIZED_SPEED"), r0.osSpeed);

    // The profile, cut at the MA end.
    QVector<Schema::SubEntry> prof;
    auto sub = [&prof](int type, const QString &rep, const QVector<Row> &rs, Row values = {}) {
        Schema::SubEntry se;
        se.type = type;
        se.values = values;
        se.repeats.insert(rep, rs);
        prof << se;
    };
    QVector<Row> ssp, grad;
    if (!segments(secs, &Route::ssp, "SSP", maEnd, &ssp, &a.why)) return a;
    if (!segments(secs, &Route::gradient, "gradient", maEnd, &grad, &a.why)) return a;
    for (Row &x : ssp) x = speedRow(x);
    if (!ssp.isEmpty()) sub(1, QStringLiteral("speed"), ssp);
    if (!grad.isEmpty()) sub(2, QStringLiteral("grad"), grad);
    const QVector<Row> lc = placed(secs, off, &Route::lc, "d", nullptr, maEnd);
    if (!lc.isEmpty()) sub(3, QStringLiteral("lc"), lc);
    const QVector<Row> to = placed(secs, off, &Route::turnouts, "start", nullptr, maEnd);
    if (!to.isEmpty()) sub(4, QStringLiteral("to"), to);
    {
        QVector<Row> tags;
        qint64 at = 0;
        Row lr;
        for (int i = 0; i < secs.size(); ++i) {
            const QVector<RouteTag> &ts = secs[i]->tags;
            for (int k = (i ? 1 : 0); k < ts.size(); ++k) {
                at += k ? ts[k].dist : 0;
                if (at > maEnd) break;
                tags << Row{ { "dist", k ? ts[k].dist : 0 }, { "tag", ts[k].tag }, { "dup_dir", ts[k].dupDir } };
            }
            if (secs[i]->locReset && lr.isEmpty())
                lr = { { "LOC_RESET", 1 }, { "START_DIST_TO_LOC_RESET", secs[i]->locResetStart + off[i] },
                       { "ADJ_LOCO_DIR", secs[i]->adjLocoDir }, { "ABS_LOC_CORRECTION", secs[i]->absLocCorrection } };
        }
        Row v{ { "DIST_DUP_TAG", r0.distDupTag }, { "LOC_RESET", 0 }, { "TIN", r0.tin } };
        for (auto it = lr.begin(); it != lr.end(); ++it) v.insert(it.key(), it.value());
        QVector<Row> adj;
        for (int t : r0.adjTins) adj << Row{ { "tin", t } };
        Schema::SubEntry se;
        se.type = 5;
        se.values = v;
        se.repeats.insert(QStringLiteral("tag"), tags);
        se.repeats.insert(QStringLiteral("line_tin"), adj);
        prof << se;
    }
    const QVector<Row> tc = placed(secs, off, &Route::trackConditions, "sd", "ln", maEnd);
    if (!tc.isEmpty()) sub(6, QStringLiteral("tc"), tc);
    bool anyTsr = false;
    for (const Route *r : secs) anyTsr = anyTsr || !r->tsr.isEmpty();
    if (anyTsr) {
        QVector<Row> tsr = placed(secs, off, &Route::tsr, "dist", "len", maEnd, [&st](const Route &r, const Row &x) {
            return !st.tsrOff.contains(QStringLiteral("%1/%2").arg(r.name).arg(x.value(QStringLiteral("id"))));
        });
        for (Row &x : tsr) { x = speedRow(x); x.remove(QStringLiteral("active")); }
        sub(7, QStringLiteral("tsr"), tsr, Row{ { "TSR_STATUS", tsr.isEmpty() ? 0 : 2 } });
    }

    QString bad;
    if (!fits(enc, ma, &bad)) { a.why = bad; return a; }
    for (const Schema::SubEntry &se : prof) if (!fits(enc, se, &bad)) { a.why = bad; return a; }
    if (-refDist < -16384) {   // DIST_PKT_START: 15 bits, signed
        a.why = QObject::tr("DIST_PKT_START from tag %1 would be %2 m, outside 15 bits").arg(ref).arg(-refDist);
        return a;
    }

    // A new REF_PROF_ID when the profile changes; in full until the loco acknowledges it.
    const QByteArray key = keyOf(ri, prof);
    Loco l = *loco;
    if (key != l.profKey || l.profId == 0) {
        l.profId = l.profId % 15 + 1;
        l.profKey = key;
    }
    l.route = ri;
    l.refTag = ref;
    a.header.insert(QStringLiteral("REF_PROF_ID"), l.profId);
    a.fullProfile = lsrp.value(QStringLiteral("LAST_REF_PROFILE_NUM")) != l.profId;
    a.subs << ma;
    if (a.fullProfile) a.subs << prof;
    *loco = l;

    const int sent = secs.size();
    a.note = QObject::tr("route %1, %2 %3 → %4 section%5, MA %6 m; ref tag %7, APPR_SIG_DIST %8 m; profile %9 %10")
                 .arg(r0.name, r0.exit, aspectName(curAspect)).arg(sent).arg(sent == 1 ? QString() : QStringLiteral("s"))
                 .arg(maSent).arg(ref).arg(appr).arg(l.profId)
                 .arg(a.fullProfile ? QObject::tr("in full") : QObject::tr("acknowledged: MA only"));
    if (!cutNote.isEmpty()) a.note += QStringLiteral("; ") + cutNote;
    if (rearEnd) a.note += QObject::tr("; SoS Rear-end (the loco reports it)");
    if (!notes.isEmpty()) a.checks = notes + a.checks;
    a.ok = true;
    return a;
}

}  // namespace StationKavach
