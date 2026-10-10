#include "simpreview.h"

#include <QHash>
#include <QSet>

namespace SimPreview {

namespace {

// The simulator's constants (IRS.h).
constexpr double kPi = 3.1428571;
constexpr double kWheel = 950 / 1000.0;
constexpr int    kPulsesPerRev = 30;
constexpr double kKmhToMs = 0.277777778;

struct Row {
    QString    id, tagName;
    int        tagType = 0;
    double     absLoc = 0, nextAbsLoc = 0;
    QByteArray bytes;
};

bool dupOf(const QByteArray &tag) { return RfidTag::summary(tag).duplicate; }
qint64 locOf(const QByteArray &tag) { return RfidTag::summary(tag).absLoc; }

}  // namespace

quint32 pulsesPerTick(double kmh)
{
    const double v = kmh * kKmhToMs;
    return quint32(((0.1 * v * kPulsesPerRev) / (kPi * kWheel)) * 1000);
}

double metresPerTick(double kmh)
{
    return (kPi * kWheel * pulsesPerTick(kmh)) / kPulsesPerRev / 1000;
}

Result run(const RfidTag::Route &route, const Options &o)
{
    Result r;
    if (route.dir != RfidTag::DirNominal && route.dir != RfidTag::DirReverse) {
        r.error = QStringLiteral("set the route's direction: the simulator moves by it");
        return r;
    }
    if (route.tags.size() < 2 || o.startRow < 0 || o.startRow >= route.tags.size()) {
        r.error = QStringLiteral("the route needs at least two tags and a start tag in it");
        return r;
    }
    if (o.speedKmh <= 0) {
        r.error = QStringLiteral("the speed must be above 0 (the simulator stops at 0)");
        return r;
    }

    // The rows as the simulator reads them.
    bool fileRows = true;
    for (const RfidTag::Tag &t : route.tags)
        fileRows = fileRows && t.file.present && RfidTag::fromPages(t.file.pageX, t.file.pageY) == t.bytes;
    QVector<Row> rows;
    if (fileRows) {
        for (const RfidTag::Tag &t : route.tags)
            rows.append({ t.file.rfidId, t.file.tagName, t.file.tagType, t.file.absLoc, t.file.nextAbsLoc, t.bytes });
    } else {
        const QVector<RfidTag::RouteRow> rr = RfidTag::routeRows(route.tags, route.dir);
        for (int i = 0; i < rr.size(); ++i)
            rows.append({ rr.at(i).rfidId, rr.at(i).tagName, rr.at(i).tagType, double(rr.at(i).absLoc),
                          double(rr.at(i).nextAbsLoc), route.tags.at(i).bytes });
    }
    r.fromFileRows = fileRows;
    r.pulsesPerTick = pulsesPerTick(o.speedKmh);
    r.metresPerTick = metresPerTick(o.speedKmh);
    if (r.pulsesPerTick == 0) {
        r.error = QStringLiteral("at %1 km/h the simulator counts 0 pulses a tick and stops").arg(o.speedKmh);
        return r;
    }
    QSet<QString> missing;
    for (const QString &m : o.missing) missing.insert(m.trimmed().toUpper());

    const double sign = route.dir == RfidTag::DirNominal ? 1 : -1;
    double d = rows.at(o.startRow).absLoc;
    QString current = QStringLiteral("0");
    bool secondFlag = false;
    int previous = -1;                                     // sendPreviousRfidData; -1 = reset
    QHash<int, QPair<int, QByteArray>> first, second;      // per tag type: row, tag
    QSet<int> passed, sentOn1;
    QHash<int, bool> mainSentOn2;                          // main rows: their tag went out on reader 2

    for (int tick = 1; tick <= o.maxTicks; ++tick) {
        d += sign * r.metresPerTick;
        int hit = -1;
        for (int i = 0; i < rows.size() && hit < 0; ++i) {
            const Row &x = rows.at(i);
            if ((x.absLoc <= d && x.nextAbsLoc > d) || (x.absLoc >= d && x.nextAbsLoc < d)) hit = i;
        }
        if (hit < 0) {
            r.ticks = tick;
            r.endDistance = d;
            r.endReason = QStringLiteral("no row spans %1 m: past the route's last row").arg(d, 0, 'f', 1);
            break;
        }
        passed.insert(hit);
        int s = hit;
        if (missing.contains(rows.at(hit).id.toUpper())) {
            s = previous;                                  // the simulator re-sends the previous row
        } else {
            previous = hit;
        }
        if (s < 0) continue;                               // nothing before it: nothing sent
        const Row &x = rows.at(s);
        if (x.tagType < 9 || x.tagType > 12) continue;     // no packet for that tag_type
        auto emitEvent = [&](int reader, int row, const QByteArray &tag) {
            r.events.append({ tick, tick * 0.1, d, reader, row, RfidTag::nameOf(tag), locOf(tag) });
        };
        if (current != x.id) {
            if (x.tagName == QLatin1String("main")) first.insert(x.tagType, { s, x.bytes });
            else second.insert(x.tagType, { s, x.bytes });
        }
        if (current != x.id && o.reader1) {
            emitEvent(1, s, x.bytes);
            sentOn1.insert(s);
        } else if (current == x.id && o.reader2) {
            const double gap = qAbs(d - x.absLoc);
            if (gap >= 20 && gap < 24) {
                const QPair<int, QByteArray> f = first.value(x.tagType, { -1, QByteArray() });
                if (!f.second.isEmpty() && locOf(f.second) != 0) {
                    emitEvent(2, f.first, f.second);
                    mainSentOn2.insert(f.first, true);
                    first.remove(x.tagType);
                }
                secondFlag = true;
            } else if (gap >= 24 && secondFlag) {
                const QPair<int, QByteArray> sc = second.value(x.tagType, { -1, QByteArray() });
                if (!sc.second.isEmpty() && dupOf(sc.second)) {
                    if (locOf(sc.second) != 0) {
                        emitEvent(2, sc.first, sc.second);
                        second.remove(x.tagType);
                    }
                    secondFlag = false;
                }
            }
        }
        current = x.id;
        r.ticks = tick;
        r.endDistance = d;
    }
    if (r.endReason.isEmpty()) r.endReason = QStringLiteral("stopped after %1 ticks").arg(o.maxTicks);

    // What the run went past without a reader-1 send: the last row (never
    // spanned) and missing tags.
    const int from = o.startRow;
    for (int i = from; i < rows.size(); ++i) {
        if (!sentOn1.contains(i) && (passed.contains(i) || i == rows.size() - 1))
            r.neverOnReader1 << rows.at(i).id;
        if (sentOn1.contains(i) && rows.at(i).tagName == QLatin1String("main") && !mainSentOn2.contains(i) && o.reader2)
            r.mainsMissedOnReader2 << rows.at(i).id;
    }
    r.ok = true;
    return r;
}

Against against(const Result &preview, const QVector<PlanRun::Read> &reads)
{
    Against a;
    a.readOf.fill(-1, preview.events.size());
    a.lagS.fill(0, preview.events.size());
    QHash<int, int> next;                                  // reader -> first read not yet looked at
    QSet<int> used;
    for (int i = 0; i < preview.events.size(); ++i) {
        const Event &e = preview.events.at(i);
        for (int k = next.value(e.reader); k < reads.size(); ++k) {
            if (reads.at(k).reader != e.reader || RfidTag::nameOf(reads.at(k).bytes) != e.tag) continue;
            a.readOf[i] = k;
            next.insert(e.reader, k + 1);
            used.insert(k);
            break;
        }
    }
    qint64 t0 = 0, firstMs = 0, lastMs = 0;
    bool any = false;
    for (int i = 0; i < a.readOf.size(); ++i) {
        const int k = a.readOf.at(i);
        if (k < 0) continue;
        const qint64 ms = reads.at(k).ms;
        if (!any) t0 = ms - qint64(preview.events.at(i).timeS * 1000);
        firstMs = any ? qMin(firstMs, ms) : ms;
        lastMs = any ? qMax(lastMs, ms) : ms;
        any = true;
        a.lagS[i] = (ms - t0) / 1000.0 - preview.events.at(i).timeS;
        a.maxLagS = qMax(a.maxLagS, qAbs(a.lagS.at(i)));
        ++a.matched;
    }
    for (int k = 0; any && k < reads.size(); ++k)
        if (!used.contains(k) && reads.at(k).ms >= firstMs && reads.at(k).ms <= lastMs) a.unpredicted << k;
    return a;
}

}  // namespace SimPreview
