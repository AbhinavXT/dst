#include "radiolinks.h"

#include "fieldplot.h"
#include "frameclock.h"
#include "logmodel.h"

#include <QMap>
#include <QObject>
#include <QStringList>
#include <algorithm>
#include <cmath>

namespace RadioLinks {
namespace {

// FRAME_NUM b after a, across midnight.
int step(qint64 a, qint64 b)
{
    qint64 d = b - a;
    if (d < -FrameClock::kSecondsPerDay / 2) d += FrameClock::kSecondsPerDay;
    if (d >  FrameClock::kSecondsPerDay / 2) d -= FrameClock::kSecondsPerDay;
    return int(d);
}

int medianLow(QVector<int> v)
{
    if (v.isEmpty()) return 0;
    std::sort(v.begin(), v.end());
    return v.at((v.size() - 1) / 2);
}

}  // namespace

void count(Link &link, const QVector<QPair<qint64, qint64>> &frames)
{
    link.frames = frames.size();
    if (frames.isEmpty()) return;
    QVector<int> steps;
    for (int i = 1; i < frames.size(); ++i) {
        const int d = step(frames.at(i - 1).second, frames.at(i).second);
        if (d > 0) steps << d;
        link.longestGapMs = qMax(link.longestGapMs, frames.at(i).first - frames.at(i - 1).first);
    }
    link.cycleS = medianLow(steps);
    const int cycle = qMax(1, link.cycleS);

    // Contact segments, split at outages; expected over each segment's span.
    qint64 segFirst = frames.first().second;
    int spanS = 0;
    auto closeSegment = [&]() { link.expected += (spanS + cycle / 2) / cycle + 1; };
    for (int i = 1; i < frames.size(); ++i) {
        const int d = step(frames.at(i - 1).second, frames.at(i).second);
        if (d == 0) { ++link.repeats; continue; }
        if (d > kOutageS || d < 0) {
            closeSegment();
            link.outages << Outage{ frames.at(i - 1).first, frames.at(i).first, d };
            segFirst = frames.at(i).second;
            spanS = 0;
            continue;
        }
        if (d >= 2 * cycle) ++link.gaps;
        spanS = step(segFirst, frames.at(i).second);
    }
    closeSegment();
    link.lost = qMax(0, link.expected - (link.frames - link.repeats));
}

Report build(const LogModel *model, qint64 fromMs, qint64 toMs)
{
    Report r;
    if (!model || model->count() == 0) return r;
    const QVector<RowFields> slrp = collectRowFields(model, QStringLiteral("slrp"),
        { QStringLiteral("FRAME_NUM"), QStringLiteral("SOURCE_STN_ID") }, 200000, fromMs, toMs);
    const QVector<RowFields> lsrp = collectRowFields(model, QStringLiteral("lsrp"), { QStringLiteral("FRAME_NUM") }, 200000, fromMs, toMs);
    const QVector<RowFields> arp = collectRowFields(model, QStringLiteral("arp"), { QStringLiteral("FRAME_NUM") }, 200000, fromMs, toMs);

    auto pairs = [](const QVector<RowFields> &rows) {
        QVector<QPair<qint64, qint64>> out;
        for (const RowFields &f : rows)
            if (f.has(QStringLiteral("FRAME_NUM"))) out << qMakePair(f.epochMs, f.raw.value(QStringLiteral("FRAME_NUM")));
        return out;
    };

    // The loco's own clock: LSRP and ARP merged in row order.
    QVector<const RowFields *> own;
    for (const RowFields &f : lsrp) own << &f;
    for (const RowFields &f : arp) own << &f;
    std::sort(own.begin(), own.end(), [](const RowFields *a, const RowFields *b) { return a->row < b->row; });

    QMap<int, QVector<QPair<qint64, qint64>>> byStation;
    QMap<int, QVector<int>> ages;
    int k = -1;                                            // the last own frame before this SLRP
    for (const RowFields &f : slrp) {
        if (!f.has(QStringLiteral("FRAME_NUM")) || !f.has(QStringLiteral("SOURCE_STN_ID"))) continue;
        const int stn = int(f.raw.value(QStringLiteral("SOURCE_STN_ID")));
        const qint64 fn = f.raw.value(QStringLiteral("FRAME_NUM"));
        byStation[stn] << qMakePair(f.epochMs, fn);
        while (k + 1 < own.size() && own.at(k + 1)->row < f.row) ++k;
        if (k < 0 || !own.at(k)->has(QStringLiteral("FRAME_NUM")) || f.epochMs - own.at(k)->epochMs > kOwnClockMs) continue;
        const qint64 sinceMs = f.epochMs - own.at(k)->epochMs;
        ages[stn] << step(fn, own.at(k)->raw.value(QStringLiteral("FRAME_NUM")) + qRound64(sinceMs / 1000.0));
    }
    for (auto it = byStation.cbegin(); it != byStation.cend(); ++it) {
        Link l;
        l.type = QStringLiteral("slrp");
        l.station = it.key();
        l.name = QObject::tr("SLRP from station %1").arg(it.key());
        count(l, it.value());
        l.ages = ages.value(it.key());
        l.ageMedian = medianLow(l.ages);
        for (const int a : l.ages) {
            l.ageMax = qMax(l.ageMax, a);
            const FrameClock::Accept acc = FrameClock::classify(a);
            l.stale += acc == FrameClock::Accept::Stale ? 1 : 0;
            l.ahead += acc == FrameClock::Accept::Ahead ? 1 : 0;
        }
        r.links << l;
    }
    const QVector<QPair<QString, const QVector<RowFields> *>> sent{ { QStringLiteral("lsrp"), &lsrp }, { QStringLiteral("arp"), &arp } };
    for (const auto &s : sent) {
        if (s.second->isEmpty()) continue;
        Link l;
        l.type = s.first;
        l.name = QObject::tr("%1 (sent)").arg(s.first.toUpper());
        count(l, pairs(*s.second));
        r.links << l;
    }
    return r;
}

QString summaryText(const Report &r)
{
    if (r.links.isEmpty()) return QObject::tr("No SLRP, LSRP or ARP in this tab: no frames to count.");
    QStringList parts;
    for (const Link &l : r.links) {
        if (l.type != QLatin1String("slrp")) continue;
        QString s = QObject::tr("SLRP from station %1: %2 received, %3 lost in contact (%4 %), %5 outage(s) over %6 s.")
                        .arg(l.station).arg(l.frames).arg(l.lost).arg(l.lostPercent(), 0, 'f', 1)
                        .arg(l.outages.size()).arg(kOutageS);
        if (!l.ages.isEmpty())
            s += QObject::tr(" Age on arrival %1 s (median), up to %2 s; %3 the loco would not process (over 4 s old or 2 s early).")
                     .arg(l.ageMedian).arg(l.ageMax).arg(l.stale + l.ahead);
        parts << s;
    }
    if (parts.isEmpty()) parts << QObject::tr("No SLRP received.");
    return parts.join(QLatin1Char(' '));
}

}  // namespace RadioLinks
