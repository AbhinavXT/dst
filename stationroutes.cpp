#include "stationroutes.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>

namespace StationRoutes {

namespace {

QString tr(const char *s) { return QCoreApplication::translate("StationRoutes", s); }

constexpr int kMaxPoints = 8;          // points taken Reverse on one route
constexpr int kMaxRoutes = 500;

struct TagAt { QString name; int number = 0; qint64 loc = 0; int placement = 0, tinNom = 0, tinRev = 0; };
struct Seg { QString line; double from = 0, to = 0; };
struct Path {
    QStringList tags;                  // main tag names, in the order the loco meets them
    QStringList normal, reverse;
    QVector<Seg> segs;
};

QString bare(QString n)
{
    n = n.trimmed();
    if (n.startsWith(QLatin1Char('P'), Qt::CaseInsensitive)) n.remove(0, 1);
    return n;
}

QString cleanTag(QString n)
{
    n = n.trimmed();
    if (n.endsWith(QLatin1String(".0"))) n.chop(2);
    return n;
}

bool faces(int placement, int dir)
{
    return placement == 6 || (placement == 1 && dir > 0) || (placement == 2 && dir < 0);
}

}  // namespace

QString statusName(Proposal::Status s)
{
    switch (s) {
    case Proposal::New: return tr("new");
    case Proposal::Same: return tr("already there");
    case Proposal::Differs: return tr("differs from the station's");
    }
    return {};
}

Result propose(const StationLayout::Layout &l, const StationKavach::Station &existing)
{
    Result res;
    // The main tags that decode, by name (the first of a name listed twice).
    QHash<QString, TagAt> tags;
    QHash<QString, qint64> dupLoc;     // main name -> its duplicate's abs_loc
    for (const StationLayout::Tag &t : l.tags) {
        const StationLayout::TagInfo in = StationLayout::info(t);
        if (!in.ok) continue;
        if (t.name.endsWith(QLatin1Char('D'))) {
            if (!dupLoc.contains(t.name.chopped(1))) dupLoc.insert(t.name.chopped(1), in.absLoc);
            continue;
        }
        bool ok = false;
        const int number = t.name.toInt(&ok);
        if (!ok || tags.contains(t.name)) continue;
        tags.insert(t.name, TagAt{ t.name, number, in.absLoc, in.placement, in.tinNom, in.tinRev });
    }
    QHash<QString, QStringList> onLine;   // line id -> its main tags
    QHash<QString, QString> lineOf, lineName;
    for (const StationLayout::Line &ln : l.lines) {
        lineName.insert(ln.line, ln.name.isEmpty() ? ln.line : ln.name);
        for (const QString &n : ln.tags) {
            const QString c = cleanTag(n);
            if (!tags.contains(c)) continue;
            onLine[ln.line] << c;
            if (!lineOf.contains(c)) lineOf.insert(c, ln.line);
        }
    }
    QHash<QString, QStringList> signalsAt;   // foot tag -> station signals
    for (const StationLayout::Signal &s : l.signalList)
        if (s.route.isEmpty() && !s.name.trimmed().isEmpty()) signalsAt[cleanTag(s.footTag)] << s.name.trimmed();
    const QVector<StationLayout::Circuit> circuits = StationLayout::circuits(l);
    // Which way each line runs: the sum of its signals' facings (+1 Nominal, -1 Reverse).
    QHash<QString, int> lineWay;
    for (const StationLayout::Signal &s : l.signalList) {
        const QString foot = cleanTag(s.footTag);
        if (!s.route.isEmpty() || !tags.contains(foot) || !lineOf.contains(foot)) continue;
        const int pl = tags.value(foot).placement;
        lineWay[lineOf.value(foot)] += pl == 1 ? 1 : pl == 2 ? -1 : 0;
    }
    // A point's name as the station's routes write it: "30" for the layout's P30 when they do.
    // When the station's routes write the layout's points without their P (LPI: 30 for P30), all are written so.
    const QStringList kavachPoints = existing.pointNames();
    bool bareNames = false;
    for (const StationLayout::Point &pt : l.points)
        bareNames = bareNames || (bare(pt.name) != pt.name && kavachPoints.contains(bare(pt.name)) && !kavachPoints.contains(pt.name));
    auto pointName = [bareNames](const QString &n) { return bareNames ? bare(n) : n; };

    struct Found { QString entry, exit; Path path; int dir; };
    QVector<Found> found;
    // On `line` from `x` the way `dir` runs. `came`: the point just crossed (its landing end is behind).
    std::function<void(const QString &, const QString &, double, int, Path, int, const QString &)> walk;
    walk = [&](const QString &entry, const QString &line, double x, int dir, Path p, int depth, const QString &came) {
        if (found.size() >= kMaxRoutes) return;
        struct Ev { double at; int kind; QString tag; int point = -1; bool fromEnd1 = true; };
        QVector<Ev> ev;
        for (const QString &t : onLine.value(line))
            if ((tags.value(t).loc - x) * dir > 0) ev.append({ double(tags.value(t).loc), 0, t });
        for (int i = 0; i < l.points.size(); ++i) {
            const StationLayout::Point &pt = l.points.at(i);
            if (pt.name == came) continue;
            if (pt.line1 == pt.line2) {
                if (pt.line1 == line && (pt.loc1 - x) * dir > 0) ev.append({ double(pt.loc1), 1, QString(), i, true });
                continue;
            }
            if (pt.line1 == line && (pt.loc1 - x) * dir > 0) ev.append({ double(pt.loc1), 1, QString(), i, true });
            if (pt.line2 == line && (pt.loc2 - x) * dir > 0) ev.append({ double(pt.loc2), 1, QString(), i, false });
        }
        std::stable_sort(ev.begin(), ev.end(), [dir](const Ev &a, const Ev &b) {
            return a.at * dir < b.at * dir || (a.at == b.at && a.kind < b.kind);
        });
        for (const Ev &e : ev) {
            if (e.kind == 0) {
                p.tags << e.tag;
                const TagAt &t = tags[e.tag];
                if (signalsAt.contains(e.tag) && faces(t.placement, dir)) {
                    p.segs.append({ line, x, double(t.loc) });
                    for (const QString &exit : signalsAt.value(e.tag)) found.append({ entry, exit, p, dir });
                    return;
                }
                continue;
            }
            const StationLayout::Point &pt = l.points.at(e.point);
            const QString name = pointName(pt.name);
            if (pt.line1 == pt.line2) { p.normal << name; continue; }
            const QString other = e.fromEnd1 ? pt.line2 : pt.line1;
            const double there = double(e.fromEnd1 ? pt.loc2 : pt.loc1);
            if ((there - e.at) * dir > 0 && depth < kMaxPoints) {   // facing: over it too
                Path q = p;
                q.reverse << name;
                q.segs.append({ line, x, e.at });
                QVector<TagAt> turnout;
                for (const QString &n : pt.tags.split(QLatin1Char(','), Qt::SkipEmptyParts))
                    if (tags.contains(cleanTag(n))) turnout << tags.value(cleanTag(n));
                std::stable_sort(turnout.begin(), turnout.end(), [dir](const TagAt &a, const TagAt &b) { return a.loc * dir < b.loc * dir; });
                for (const TagAt &t : turnout) q.tags << t.name;
                walk(entry, other, there - dir * 0.5, dir, q, depth + 1, pt.name);
            }
            p.normal << name;
        }
    };

    QStringList seenEntry;
    for (const StationLayout::Signal &s : l.signalList) {
        const QString name = s.name.trimmed();
        if (!s.route.isEmpty() || name.isEmpty() || seenEntry.contains(name)) continue;
        seenEntry << name;
        const QString foot = cleanTag(s.footTag);
        if (!tags.contains(foot)) { res.notes << tr("signal %1: its foot tag %2 is not a tag that decodes").arg(name, foot); continue; }
        if (!lineOf.contains(foot)) { res.notes << tr("signal %1: its foot tag %2 is on no line").arg(name, foot); continue; }
        const TagAt &t = tags[foot];
        if (!faces(t.placement, 1) && !faces(t.placement, -1)) {
            res.notes << tr("signal %1: its foot tag %2 is placed %3, not at a signal foot: which way it faces is not known")
                             .arg(name, foot).arg(t.placement);
            continue;
        }
        const int before = found.size();
        for (int dir : { 1, -1 }) {
            if (!faces(t.placement, dir)) continue;
            Path p;
            p.tags << foot;
            walk(name, lineOf.value(foot), double(t.loc), dir, p, 0, QString());
        }
        if (found.size() == before) res.notes << tr("signal %1: no signal ahead facing its way: no route from it").arg(name);
    }

    QHash<QString, int> names;
    for (const Found &f : found) {
        StationKavach::Route r;
        r.entry = f.entry;
        r.exit = f.exit;
        r.pktDir = f.dir > 0 ? 1 : 2;
        r.sectionType = 0;
        r.authority = 2;
        r.pointsNormal = f.path.normal;
        r.pointsReverse = f.path.reverse;
        r.pointsNormal.removeDuplicates();
        const TagAt &first = tags[f.path.tags.first()];
        r.tin = f.dir > 0 ? first.tinNom : first.tinRev;
        r.distDupTag = dupLoc.contains(first.name) ? int(std::llabs(dupLoc.value(first.name) - first.loc)) : 4;
        qint64 prev = first.loc;
        for (int i = 0; i < f.path.tags.size(); ++i) {
            const TagAt &t = tags[f.path.tags.at(i)];
            StationKavach::RouteTag rt;
            rt.tag = t.number;
            rt.dist = i == 0 ? 0 : int(std::llabs(t.loc - prev));
            rt.dupDir = dupLoc.contains(t.name) && dupLoc.value(t.name) < t.loc ? 1 : 0;
            r.tags << rt;
            prev = t.loc;
            if (i > 0) {
                const int tin = f.dir > 0 ? t.tinNom : t.tinRev;
                const QString n = QStringLiteral("N-%1").arg(tin);
                if (tin && !r.tins.contains(n)) r.tins << n;
            }
        }
        QHash<QString, double> againstOn;
        for (const Seg &sg : f.path.segs) {
            const double lo = qMin(sg.from, sg.to), hi = qMax(sg.from, sg.to);
            if (lineWay.value(sg.line) * f.dir < 0) againstOn[sg.line] += hi - lo;
            for (const StationLayout::Circuit &c : circuits) {
                if (c.line != sg.line || r.trackCircuits.contains(c.name)) continue;
                const bool on = c.from == c.to ? (c.from > lo && c.from < hi) : (qMin(hi, double(c.to)) - qMax(lo, double(c.from)) > 0);
                if (on) r.trackCircuits << c.name;
            }
        }
        Proposal p;
        p.line = lineName.value(lineOf.value(first.name));
        double longest = 0;
        for (auto it = againstOn.cbegin(); it != againstOn.cend(); ++it) {
            p.against += int(it.value());
            if (it.value() > longest) { longest = it.value(); p.againstLine = lineName.value(it.key(), it.key()); }
        }
        const QString base = QStringLiteral("%1-%2 %3").arg(r.entry, r.exit, p.line);
        const int n = ++names[base];
        r.name = n == 1 ? base : QStringLiteral("%1 (%2)").arg(base).arg(n);
        // Against the station's own routes: the same entry, exit and Reverse points.
        QSet<QString> rev;
        for (const QString &x : r.pointsReverse) rev.insert(bare(x));
        for (int i = 0; i < existing.routes.size(); ++i) {
            const StationKavach::Route &e = existing.routes.at(i);
            if (e.entry != r.entry || e.exit != r.exit) continue;
            QSet<QString> erev;
            for (const QString &x : e.pointsReverse) erev.insert(bare(x));
            bool sameTags = e.tags.size() == r.tags.size();
            for (int k = 0; sameTags && k < e.tags.size(); ++k)
                sameTags = e.tags.at(k).tag == r.tags.at(k).tag && e.tags.at(k).dist == r.tags.at(k).dist;
            if (sameTags) { p.status = Proposal::Same; p.existing = i; break; }
            if (erev == rev) { p.status = Proposal::Differs; p.existing = i; }
        }
        p.route = r;
        res.routes << p;
    }
    if (found.size() >= kMaxRoutes) res.notes << tr("stopped at %1 routes").arg(kMaxRoutes);
    return res;
}

int addTo(StationKavach::Station *s, const Result &r, const QVector<int> &picked)
{
    int added = 0;
    for (int i : picked) {
        if (i < 0 || i >= r.routes.size()) continue;
        const StationKavach::Route &route = r.routes.at(i).route;
        for (const auto &sig : { qMakePair(route.entry, route.tags.value(0).tag), qMakePair(route.exit, route.tags.value(route.tags.size() - 1).tag) })
            if (s->signalIndex(sig.first) < 0) {
                StationKavach::Signal k;
                k.name = sig.first;
                k.footTag = sig.second;
                k.aspect = 1;
                s->signalList << k;
            }
        s->routes << route;
        ++added;
    }
    return added;
}

}  // namespace StationRoutes
