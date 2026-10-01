#include "twolocoview.h"

#include "fieldplot.h"
#include "logmodel.h"

#include <QDateTime>

namespace TwoLocoView {
namespace {

bool rangesOverlap(double aLo, double aHi, double bLo, double bHi)
{
    return aLo <= bHi && bLo <= aHi;
}

// Last index in `v` (sorted ascending by epochMs, as SpeedDistance::extract
// builds it) with epochMs <= ms, or -1.
int sampleAtOrBefore(const QVector<SpeedDistance::Sample> &v, qint64 ms)
{
    int lo = 0, hi = v.size();
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (v.at(mid).epochMs <= ms) lo = mid + 1; else hi = mid;
    }
    return lo - 1;
}

}  // namespace

Events extractEvents(const LogModel *model, qint64 fromMs, qint64 toMs)
{
    Events out;
    if (!model) return out;
    bool capped = false;
    const QVector<RowFields> rows = collectRowFields(model, QStringLiteral("lsos"),
        { QStringLiteral("is_access_sos_recvd"), QStringLiteral("is_unusual_stop_recvd"),
          QStringLiteral("is_train_parted_recvd"), QStringLiteral("is_head_on_collision_recvd"),
          QStringLiteral("is_rear_end_collision_recvd"), QStringLiteral("sos_distance"),
          QStringLiteral("collision_distance") },
        200000, fromMs, toMs, &capped);

    RunReport::EpisodeTracker sos{ &out.sos }, headOn{ &out.headOn }, rearEnd{ &out.rearEnd };
    for (const RowFields &r : rows) {
        const bool access  = r.raw.value(QStringLiteral("is_access_sos_recvd")) != 0;
        const bool unusual = r.raw.value(QStringLiteral("is_unusual_stop_recvd")) != 0;
        const bool parted  = r.raw.value(QStringLiteral("is_train_parted_recvd")) != 0;
        QStringList kinds;
        if (access)  kinds << QStringLiteral("access SOS");
        if (unusual) kinds << QStringLiteral("unusual stop");
        if (parted)  kinds << QStringLiteral("train parted");
        const double sosDist = double(r.raw.value(QStringLiteral("sos_distance")));
        sos.observe(access || unusual || parted, r.epochMs, r.row, kinds.join(QStringLiteral(", ")), sosDist, false);

        const double collDist = double(r.raw.value(QStringLiteral("collision_distance")));
        headOn.observe(r.raw.value(QStringLiteral("is_head_on_collision_recvd")) != 0, r.epochMs, r.row,
                       QStringLiteral("head-on, %1 m").arg(collDist, 0, 'f', 0), collDist, false);
        rearEnd.observe(r.raw.value(QStringLiteral("is_rear_end_collision_recvd")) != 0, r.epochMs, r.row,
                        QStringLiteral("rear-end, %1 m").arg(collDist, 0, 'f', 0), collDist, false);
    }
    return out;
}

Pair build(const LogModel *modelA, const QString &keyA,
          const LogModel *modelB, const QString &keyB,
          qint64 fromMs, qint64 toMs, qint64 toleranceMs)
{
    Pair p;
    p.keyA = keyA;
    p.keyB = keyB;
    p.a = SpeedDistance::extract(modelA, 200000, fromMs, toMs);
    p.b = SpeedDistance::extract(modelB, 200000, fromMs, toMs);
    p.eventsA = extractEvents(modelA, fromMs, toMs);
    p.eventsB = extractEvents(modelB, fromMs, toMs);

    for (const SpeedDistance::Sample &sa : p.a.samples) {
        const int idx = sampleAtOrBefore(p.b.samples, sa.epochMs);
        if (idx < 0) continue;
        const SpeedDistance::Sample &sb = p.b.samples.at(idx);
        if (sa.epochMs - sb.epochMs > toleranceMs) continue;
        p.gap << GapSample{ sa.epochMs, sb.locM - sa.locM };
    }

    if (!p.a.isEmpty() && !p.b.isEmpty()
        && !rangesOverlap(p.a.minLocM, p.a.maxLocM, p.b.minLocM, p.b.maxLocM)) {
        p.plausible = false;
        p.warning = QStringLiteral(
            "%1's and %2's reported locations do not overlap at all — check these are on the "
            "same section of track before trusting the gap below.").arg(keyA, keyB);
    }
    return p;
}

QString gapToCsv(const Pair &pair)
{
    QString out = QStringLiteral("time_local,epoch_ms,gap_m\n");
    for (const GapSample &g : pair.gap) {
        out += QDateTime::fromMSecsSinceEpoch(g.ms).toString(QStringLiteral("HH:mm:ss.zzz"))
             + QLatin1Char(',') + QString::number(g.ms) + QLatin1Char(',')
             + QString::number(g.gapM, 'f', 2) + QLatin1Char('\n');
    }
    return out;
}

}  // namespace TwoLocoView
