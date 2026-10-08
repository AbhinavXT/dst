#include "twolocoview.h"

#include "capturedecoder.h"
#include "fieldplot.h"
#include "logmodel.h"

#include <QSet>
#include <algorithm>

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

// Lowest and highest KNOWN location in a trace; false if none is known.
bool knownSpan(const SpeedDistance::Trace &t, double *lo, double *hi, int *unknown)
{
    bool any = false;
    *unknown = 0;
    for (const SpeedDistance::Sample &s : t.samples) {
        if (!SpeedDistance::locationKnown(s)) { ++*unknown; continue; }
        if (!any) { *lo = *hi = s.locM; any = true; continue; }
        *lo = qMin(*lo, s.locM);
        *hi = qMax(*hi, s.locM);
    }
    return any;
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

namespace {
// The gap and the plausibility checks, once both traces are in `p`.
void finishPair(Pair &p, qint64 toleranceMs, double warnApartM);
}  // namespace

Pair build(const LogModel *modelA, const QString &keyA,
          const LogModel *modelB, const QString &keyB,
          qint64 fromMs, qint64 toMs, qint64 toleranceMs, double warnApartM)
{
    Pair p;
    p.keyA = keyA;
    p.keyB = keyB;
    p.a = SpeedDistance::extract(modelA, 200000, fromMs, toMs);
    p.b = SpeedDistance::extract(modelB, 200000, fromMs, toMs);
    p.eventsA = extractEvents(modelA, fromMs, toMs);
    p.eventsB = extractEvents(modelB, fromMs, toMs);
    addModeEvents(p.a, &p.eventsA);
    addModeEvents(p.b, &p.eventsB);
    finishPair(p, toleranceMs, warnApartM);
    return p;
}

Pair buildHeard(const LogModel *modelA, const QString &keyA,
                const LogModel *heardBy, qint64 heardId, const QString &keyB,
                qint64 fromMs, qint64 toMs, qint64 toleranceMs, double warnApartM)
{
    Pair p;
    p.keyA = keyA;
    p.keyB = keyB;
    p.a = SpeedDistance::extract(modelA, 200000, fromMs, toMs);
    p.b = heardTrace(heardBy, heardId, fromMs, toMs);
    p.eventsA = extractEvents(modelA, fromMs, toMs);
    p.eventsB = heardEvents(heardBy, heardId, fromMs, toMs);
    addModeEvents(p.a, &p.eventsA);
    addModeEvents(p.b, &p.eventsB);
    finishPair(p, toleranceMs, warnApartM);
    return p;
}

namespace {
void finishPair(Pair &p, qint64 toleranceMs, double warnApartM)
{
    const QString &keyA = p.keyA, &keyB = p.keyB;

    for (const SpeedDistance::Sample &sa : p.a.samples) {
        if (!SpeedDistance::locationKnown(sa)) continue;
        const int idx = sampleAtOrBefore(p.b.samples, sa.epochMs);
        if (idx < 0) continue;
        const SpeedDistance::Sample &sb = p.b.samples.at(idx);
        if (sa.epochMs - sb.epochMs > toleranceMs) continue;
        if (!SpeedDistance::locationKnown(sb)) continue;
        p.gap << GapSample{ sa.epochMs, sb.locM - sa.locM, sa.locM, sb.locM };
    }

    double aLo = 0, aHi = 0, bLo = 0, bHi = 0;
    p.hasLocA = knownSpan(p.a, &aLo, &aHi, &p.unknownA);
    p.hasLocB = knownSpan(p.b, &bLo, &bHi, &p.unknownB);
    if (p.hasLocA && p.hasLocB) { p.minLocM = qMin(aLo, bLo); p.maxLocM = qMax(aHi, bHi); }
    else if (p.hasLocA)         { p.minLocM = aLo; p.maxLocM = aHi; }
    else if (p.hasLocB)         { p.minLocM = bLo; p.maxLocM = bHi; }

    if (p.hasLocA && p.hasLocB && !rangesOverlap(aLo, aHi, bLo, bHi))
        p.apartM = qMax(bLo - aHi, aLo - bHi);
    if (p.hasLocA && p.hasLocB && !rangesOverlap(aLo, aHi, bLo, bHi) && p.apartM > warnApartM) {
        p.plausible = false;
        p.warning = warnApartM > 0.0
            ? QStringLiteral("%1's and %2's reported locations are %3 km apart at their nearest — check "
                             "these are on the same section of track before trusting the gap.")
                  .arg(keyA, keyB).arg(p.apartM / 1000.0, 0, 'f', 1)
            : QStringLiteral("%1's and %2's reported locations do not overlap at all — check these are on the "
                             "same section of track before trusting the gap below.").arg(keyA, keyB);
    } else if (!p.a.isEmpty() && !p.b.isEmpty() && (!p.hasLocA || !p.hasLocB)) {
        p.plausible = false;
        p.warning = (!p.hasLocA && !p.hasLocB)
            ? QStringLiteral("%1 and %2 both report 0 m throughout (never localised on an RFID tag): "
                             "there is no gap to show.").arg(keyA, keyB)
            : QStringLiteral("%1 reports 0 m throughout (never localised on an RFID tag): "
                             "there is no gap to show.").arg(p.hasLocA ? keyB : keyA);
    }
}
}  // namespace

// ---- Session 172 ------------------------------------------------------------------------

namespace {
// The loco's own ID, from its own ARP / LSRP (the last one seen).
qint64 ownIdOf(const LogModel *model)
{
    for (int i = model->count() - 1; i >= 0; --i) {
        const LogEntryPtr e = model->entryAt(i);
        if (!e) continue;
        const QString type = captureTypeOf(e->text);
        if (type != QLatin1String("arp") && type != QLatin1String("lsrp")) continue;
        QHash<QString, qint64> raw;
        CaptureDecoder::describe(CaptureDecoder::parseLine(e->text), nullptr, 0, &raw);
        if (raw.contains(QStringLiteral("SOURCE_LOCO_ID"))) return raw.value(QStringLiteral("SOURCE_LOCO_ID"));
    }
    return -1;
}

// Each received ARP from another loco (CRC not failing), in row order.
template <typename F>
void forEachHeard(const LogModel *model, qint64 fromMs, qint64 toMs, F &&f)
{
    if (!model) return;
    const qint64 own = ownIdOf(model);
    for (int i = 0; i < model->count(); ++i) {
        const LogEntryPtr e = model->entryAt(i);
        if (!e || !e->text.startsWith(QLatin1String("@arprecv_"))) continue;
        if ((fromMs > 0 && e->epochMs < fromMs) || (toMs > 0 && e->epochMs > toMs)) continue;
        const CaptureLine cap = CaptureDecoder::parseLine(e->text);
        if (!cap.valid || (cap.crcChecked && !cap.crcOk)) continue;
        QHash<QString, qint64> raw;
        const QVector<FieldRow> rows = CaptureDecoder::describe(cap, nullptr, 0, &raw);
        if (!raw.contains(QStringLiteral("SOURCE_LOCO_ID"))) continue;
        const qint64 id = raw.value(QStringLiteral("SOURCE_LOCO_ID"));
        if (id == own) continue;
        f(i, e->epochMs, id, raw, rows);
    }
}

QString rowValue(const QVector<FieldRow> &rows, const char *name)
{
    for (const FieldRow &r : rows)
        if (r.field.trimmed() == QLatin1String(name)) return r.value.trimmed();
    return QString();
}
}  // namespace

QVector<qint64> heardLocos(const LogModel *model)
{
    QSet<qint64> ids;
    forEachHeard(model, 0, 0, [&](int, qint64, qint64 id, const QHash<QString, qint64> &, const QVector<FieldRow> &) { ids.insert(id); });
    QVector<qint64> out(ids.begin(), ids.end());
    std::sort(out.begin(), out.end());
    return out;
}

SpeedDistance::Trace heardTrace(const LogModel *model, qint64 locoId, qint64 fromMs, qint64 toMs)
{
    SpeedDistance::Trace t;
    t.source = QStringLiteral("arprecv");
    bool first = true;
    forEachHeard(model, fromMs, toMs, [&](int row, qint64 ms, qint64 id, const QHash<QString, qint64> &raw, const QVector<FieldRow> &rows) {
        if (id != locoId) return;
        const qint64 speed = raw.value(QStringLiteral("TRAIN_SPEED"), 511);
        if (speed == 511) { ++t.rowsSkipped; return; }          // unidentified
        SpeedDistance::Sample smp;
        smp.row = row;
        smp.epochMs = ms;
        smp.locM = double(raw.value(QStringLiteral("ABS_LOCO_LOC")));
        smp.speedKmh = double(speed);
        smp.mode = rowValue(rows, "LOCO_MODE");
        t.samples << smp;
        t.maxSpeedKmh = qMax(t.maxSpeedKmh, smp.speedKmh);
        if (SpeedDistance::locationKnown(smp)) {
            t.minLocM = first ? smp.locM : qMin(t.minLocM, smp.locM);
            t.maxLocM = first ? smp.locM : qMax(t.maxLocM, smp.locM);
            first = false;
        }
    });
    return t;
}

Events heardEvents(const LogModel *model, qint64 locoId, qint64 fromMs, qint64 toMs)
{
    Events out;
    RunReport::EpisodeTracker sos{ &out.sos }, headOn{ &out.headOn }, rearEnd{ &out.rearEnd };
    forEachHeard(model, fromMs, toMs, [&](int row, qint64 ms, qint64 id, const QHash<QString, qint64> &raw, const QVector<FieldRow> &rows) {
        if (id != locoId) return;
        const qint64 em = raw.value(QStringLiteral("EMERGENCY_STATUS"), 0);
        const QString what = rowValue(rows, "EMERGENCY_STATUS");
        sos.observe(em == 1 || em == 2 || em == 6, ms, row, what, 0.0, false);
        headOn.observe(em == 4, ms, row, what, 0.0, false);
        rearEnd.observe(em == 5, ms, row, what, 0.0, false);
    });
    return out;
}

void addModeEvents(const SpeedDistance::Trace &trace, Events *events)
{
    RunReport::EpisodeTracker trip{ &events->trip }, failure{ &events->failure };
    for (const SpeedDistance::Sample &s : trace.samples) {
        if (s.mode.isEmpty()) continue;
        trip.observe(s.mode.startsWith(QLatin1String("7 ")), s.epochMs, s.row, s.mode, 0.0, false);
        failure.observe(s.mode.startsWith(QLatin1String("12 ")), s.epochMs, s.row, s.mode, 0.0, false);
    }
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
