#include "stationinterlock.h"

#include <QObject>

#include <algorithm>

namespace StationInterlock {

Track trackOf(const StationLayout::Layout &l)
{
    Track t;
    t.circuits = StationLayout::circuits(l);
    const QHash<QString, QString> lines = StationLayout::lineOfTag(l);
    for (auto it = lines.cbegin(); it != lines.cend(); ++it) {
        bool ok = false;
        const int n = it.key().toInt(&ok);   // a duplicate ("…D") is not a number: its main tag's line
        if (ok && !t.lineOfTag.contains(n)) t.lineOfTag.insert(n, it.value());
    }
    return t;
}

Occupancy occupancy(const Track &t, const QList<StationKavach::Loco> &locos)
{
    Occupancy out;
    QList<StationKavach::Loco> sorted = locos;
    std::sort(sorted.begin(), sorted.end(), [](const StationKavach::Loco &a, const StationKavach::Loco &b) { return a.id < b.id; });
    for (const StationKavach::Loco &o : sorted) {
        if (o.absLoc <= 0 || (o.dir != 1 && o.dir != 2)) {
            out.notes << QObject::tr("loco %1 has not said where it is (ABS_LOCO_LOC %2, direction %3): it occupies nothing")
                             .arg(o.id).arg(o.absLoc).arg(o.dir);
            continue;
        }
        if (!t.lineOfTag.contains(o.lastTag)) {
            out.notes << QObject::tr("loco %1: tag %2 is on no line of the layout: it occupies nothing").arg(o.id).arg(o.lastTag);
            continue;
        }
        const QString line = t.lineOfTag.value(o.lastTag);
        const qint64 rear = o.absLoc + (o.dir == 2 ? o.trainLength : -qint64(o.trainLength));
        const qint64 lo = std::min(o.absLoc, rear), hi = std::max(o.absLoc, rear);
        for (const StationLayout::Circuit &c : t.circuits)
            if (c.line == line && c.from <= hi && c.to >= lo && !out.by.contains(c.name)) out.by.insert(c.name, o.id);
    }
    return out;
}

// ---- session 442: locking and release ---------------------------------------------

namespace {

bool proceed(int aspect) { return !(aspect == 0 || aspect == 1 || aspect == 24); }

// The circuits of route `r` occupied, and by whom ("43T by loco 1").
QStringList occupiedIn(const StationKavach::Route &r, const StationKavach::State &st)
{
    QStringList out;
    for (const QString &c : r.trackCircuits) {
        if (st.occupiedBy.contains(c)) out << QObject::tr("%1 by loco %2").arg(c).arg(st.occupiedBy.value(c));
        else if (st.occupied.contains(c)) out << c;
    }
    return out;
}

bool pointsAsSet(const StationKavach::Route &r, const StationKavach::State &st)
{
    for (const QString &p : r.pointsNormal) if (st.reverse.contains(p)) return false;
    for (const QString &p : r.pointsReverse) if (!st.reverse.contains(p)) return false;
    return true;
}

// The route a signal that is off reads: the one set from it, else the one whose points are set.
int routeOf(const StationKavach::Station &s, const StationKavach::State &st, const QString &signal)
{
    if (st.routes.contains(signal)) return st.routes.value(signal);
    int first = -1;
    for (int i = 0; i < s.routes.size(); ++i) {
        if (s.routes[i].entry != signal) continue;
        if (pointsAsSet(s.routes[i], st)) return i;
        if (first < 0) first = i;
    }
    return first;
}

}  // namespace

QString setRoute(const StationKavach::Station &s, StationKavach::State *st, int route, int indicator)
{
    if (route < 0 || route >= s.routes.size()) return QObject::tr("no route %1").arg(route);
    const StationKavach::Route &r = s.routes[route];
    if (st->interlocking)
        for (const QString &p : r.pointsNormal + r.pointsReverse) {
            const bool want = r.pointsReverse.contains(p);
            if (want == st->reverse.contains(p)) continue;
            const QString why = pointLocked(s, *st, p);
            if (!why.isEmpty()) return QObject::tr("route %1 not set: %2").arg(r.name, why);
        }
    for (const QString &p : r.pointsNormal) st->reverse.remove(p);
    for (const QString &p : r.pointsReverse) st->reverse.insert(p);
    for (auto it = st->routes.begin(); it != st->routes.end();) {
        const StationKavach::Route &o = s.routes.at(it.value());
        bool clash = it.key() == r.entry;
        for (const QString &p : o.pointsNormal) clash = clash || r.pointsReverse.contains(p);
        for (const QString &p : o.pointsReverse) clash = clash || r.pointsNormal.contains(p);
        if (clash) { st->entered.remove(it.key()); it = st->routes.erase(it); } else ++it;
    }
    st->routes.insert(r.entry, route);
    const int now = st->aspectOf(s, r.entry);
    int aspect = now <= 1 || now == 15 ? 2 : now;
    if (!r.pointsReverse.isEmpty() && indicator > 0) aspect = 2 + indicator;
    st->aspect.insert(r.entry, aspect);
    return QString();
}

QString cancelRoute(const StationKavach::Station &s, StationKavach::State *st, const QString &entry)
{
    if (!st->routes.contains(entry)) return QObject::tr("no route set from %1").arg(entry);
    const QString name = s.routes.value(st->routes.value(entry)).name;
    if (st->interlocking && st->entered.contains(entry))
        return QObject::tr("route %1 not cancelled: a train is in it (released when its track circuits clear)").arg(name);
    st->routes.remove(entry);
    st->entered.remove(entry);
    st->aspect.insert(entry, 1);
    return QString();
}

QString pointLocked(const StationKavach::Station &s, const StationKavach::State &st, const QString &point)
{
    for (auto it = st.routes.cbegin(); it != st.routes.cend(); ++it) {
        const StationKavach::Route &r = s.routes.at(it.value());
        if (r.pointsNormal.contains(point) || r.pointsReverse.contains(point))
            return QObject::tr("point %1 is locked by route %2%3").arg(point, r.name,
                                                                       st.entered.contains(it.key()) ? QObject::tr(" (a train in it)") : QString());
    }
    return QString();
}

QStringList work(const StationKavach::Station &s, StationKavach::State *st)
{
    QStringList ev;
    if (!st->interlocking) return ev;
    // A route set: entered (its first track circuit occupied): its signal back to Red;
    // entered and every circuit clear again: released.
    QStringList entries = st->routes.keys();
    std::sort(entries.begin(), entries.end());
    for (const QString &entry : entries) {
        const StationKavach::Route &r = s.routes.at(st->routes.value(entry));
        if (r.trackCircuits.isEmpty()) continue;
        const QStringList occ = occupiedIn(r, *st);
        if (!st->entered.contains(entry)) {
            if (!st->isOccupied(r.trackCircuits.first())) continue;
            st->entered.insert(entry);
            if (proceed(st->aspectOf(s, entry))) {
                st->aspect.insert(entry, 1);
                ev << QObject::tr("%1 back to Red: a train in route %2 (%3)").arg(entry, r.name, occ.join(QStringLiteral(", ")));
            }
        } else if (occ.isEmpty()) {
            st->routes.remove(entry);
            st->entered.remove(entry);
            ev << QObject::tr("route %1 released: its track circuits clear").arg(r.name);
        }
    }
    // A signal cleared by hand, no route set: back to Red as a train passes it.
    for (const StationKavach::Signal &g : s.signalList) {
        if (st->routes.contains(g.name) || !proceed(st->aspectOf(s, g.name))) continue;
        const int ri = routeOf(s, *st, g.name);
        if (ri < 0 || s.routes[ri].trackCircuits.isEmpty() || !st->isOccupied(s.routes[ri].trackCircuits.first())) continue;
        st->aspect.insert(g.name, 1);
        ev << QObject::tr("%1 back to Red: a train in route %2 (%3)").arg(g.name, s.routes[ri].name, occupiedIn(s.routes[ri], *st).join(QStringLiteral(", ")));
    }
    return ev;
}

}  // namespace StationInterlock
