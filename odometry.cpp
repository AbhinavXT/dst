#include "odometry.h"

#include "capturedecoder.h"
#include "fieldplot.h"
#include "logmodel.h"
#include "rfidcheck.h"

#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Odometry {

double Read::perKm() const
{
    return spacingM > 0 ? errorM / spacingM * 1000.0 : std::numeric_limits<double>::quiet_NaN();
}

namespace {

double median(QVector<double> v)
{
    if (v.isEmpty()) return 0.0;
    std::sort(v.begin(), v.end());
    const int n = v.size();
    return n % 2 ? v.at(n / 2) : (v.at(n / 2 - 1) + v.at(n / 2)) / 2.0;
}

int sign(double d) { return d > 0 ? 1 : (d < 0 ? -1 : 0); }

}  // namespace

Report build(const LogModel *model)
{
    Report r;
    if (!model || model->count() == 0) return r;
    const QVector<RowFields> dmi = collectRowFields(model, QStringLiteral("dmi"),
        { QStringLiteral("abs_loco_loc"), QStringLiteral("train_speed") });

    int lastTag = -1;
    double lastTagLoc = 0.0;
    qint64 lastTagMs = 0;
    int lastDirection = 0;
    for (int i = 0; i < model->count(); ++i) {
        const LogEntryPtr e = model->entryAt(i);
        if (!e || captureTypeOf(e->text) != QLatin1String("rfid")) continue;
        const CaptureLine cap = CaptureDecoder::parseLine(e->text);
        if (!cap.valid) continue;
        const RfidInfo t = CaptureDecoder::decodeRfid(cap.bytes);
        if (!t.valid || !t.crcOk || t.dup != 0 || t.unique <= 0 || t.absLoc <= 0 || t.absLoc == RfidCheck::kNotApplicable)
            continue;
        if (t.unique == lastTag && double(t.absLoc) == lastTagLoc && e->epochMs - lastTagMs < kSameReadMs) continue;
        lastTag = t.unique;
        lastTagLoc = double(t.absLoc);
        lastTagMs = e->epochMs;

        Read rd;
        rd.row = i;
        rd.ms = e->epochMs;
        rd.tag = t.unique;
        rd.type = t.type;
        rd.tagLocM = double(t.absLoc);

        // The last @dmi before the read, by row (rows are in arrival order).
        const auto it = std::upper_bound(dmi.cbegin(), dmi.cend(), i, [](int row, const RowFields &f) { return row < f.row; });
        const int k = int(it - dmi.cbegin()) - 1;
        if (k < 0 || !dmi.at(k).has(QStringLiteral("abs_loco_loc"))) {
            rd.notCounted = QObject::tr("no @dmi before the read");
            r.reads << rd;
            continue;
        }
        const RowFields &before = dmi.at(k);
        rd.dmiLocM = double(before.raw.value(QStringLiteral("abs_loco_loc")));
        const qint64 speed = before.raw.value(QStringLiteral("train_speed"), 511);
        rd.speedKmh = double(speed);
        rd.sinceDmiMs = rd.ms - before.epochMs;
        if (rd.dmiLocM <= 0) rd.notCounted = QObject::tr("loco not localised (abs_loco_loc 0)");
        else if (rd.sinceDmiMs > kStaleMs) rd.notCounted = QObject::tr("no @dmi in the %1 s before").arg(kStaleMs / 1000);
        else if (speed == 511) rd.notCounted = QObject::tr("speed not known (511)");
        if (!rd.counted()) {
            r.reads << rd;
            continue;
        }

        auto locAt = [&dmi](int j) { return j >= 0 && j < dmi.size() ? double(dmi.at(j).raw.value(QStringLiteral("abs_loco_loc"))) : 0.0; };
        const double next = locAt(k + 1), prev = locAt(k - 1);
        rd.direction = (next > 0 && next != rd.dmiLocM) ? sign(next - rd.dmiLocM)
                     : (prev > 0 && prev != rd.dmiLocM) ? sign(rd.dmiLocM - prev) : lastDirection;
        if (rd.direction) lastDirection = rd.direction;
        rd.estimateM = rd.dmiLocM + rd.direction * rd.speedKmh / 3.6 * double(rd.sinceDmiMs) / 1000.0;
        rd.errorM = (rd.estimateM - rd.tagLocM) * (rd.direction ? rd.direction : 1);
        if (std::fabs(rd.errorM) > kJumpM)
            rd.notCounted = QObject::tr("over %1 m: a relocation or another tag, not drift").arg(kJumpM, 0, 'f', 0);
        r.reads << rd;
    }
    // Spacing: from the counted read before, in the same run and direction.
    const Read *prevCounted = nullptr;
    for (Read &rd : r.reads) {
        if (!rd.counted()) continue;
        rd.spacingM = (prevCounted && prevCounted->direction == rd.direction && rd.ms - prevCounted->ms < kRunGapMs)
                    ? std::fabs(rd.tagLocM - prevCounted->tagLocM) : -1.0;
        prevCounted = &rd;
    }
    summarise(r);
    return r;
}

void summarise(Report &r)
{
    QVector<double> err, perKm;
    double sum = 0.0;
    r.maxAbsM = 0.0;
    for (const Read &rd : r.reads) {
        if (!rd.counted()) continue;
        err << rd.errorM;
        sum += rd.errorM;
        r.maxAbsM = qMax(r.maxAbsM, std::fabs(rd.errorM));
        if (rd.spacingM > 0) perKm << rd.perKm();
    }
    r.counted = err.size();
    r.meanM = err.isEmpty() ? 0.0 : sum / err.size();
    r.medianM = median(err);
    r.withSpacing = perKm.size();
    r.medianPerKm = median(perKm);
}

QString summaryText(const Report &r)
{
    if (r.reads.isEmpty()) return QObject::tr("No main-tag @rfid read in this tab: nothing to measure.");
    QStringList parts;
    parts << QObject::tr("%1 tag read(s), %2 measured.").arg(r.reads.size()).arg(r.counted);
    if (r.counted) {
        parts << QObject::tr("Along the direction of travel the loco's own location was %1 m from the tag (median; "
                             "mean %2 m, largest %3 m; + = ahead of the tag).")
                     .arg(r.medianM, 0, 'f', 1).arg(r.meanM, 0, 'f', 1).arg(r.maxAbsM, 0, 'f', 1);
        if (r.withSpacing)
            parts << QObject::tr("Per km since the tag before: %1 m (median of %2).").arg(r.medianPerKm, 0, 'f', 1).arg(r.withSpacing);
    }
    if (r.counted < r.reads.size()) parts << QObject::tr("%1 not measured: see the reason in each row.").arg(r.reads.size() - r.counted);
    return parts.join(QLatin1Char(' '));
}

QString toTsv(const Report &r)
{
    QStringList lines;
    lines << QStringLiteral("time\ttag\ttag_loc_m\tdmi_loc_m\tspeed_kmh\tsince_dmi_ms\tdirection\testimate_m\terror_m\tspacing_m\terror_per_km\tnot_counted");
    for (const Read &rd : r.reads) {
        const bool c = rd.counted();
        lines << QStringList{
            QDateTime::fromMSecsSinceEpoch(rd.ms).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
            QString::number(rd.tag), QString::number(rd.tagLocM, 'f', 0), QString::number(rd.dmiLocM, 'f', 0),
            QString::number(rd.speedKmh, 'f', 0), QString::number(rd.sinceDmiMs), QString::number(rd.direction),
            rd.direction || c ? QString::number(rd.estimateM, 'f', 1) : QString(),
            rd.direction || c ? QString::number(rd.errorM, 'f', 2) : QString(),
            rd.spacingM > 0 ? QString::number(rd.spacingM, 'f', 0) : QString(),
            rd.spacingM > 0 ? QString::number(rd.perKm(), 'f', 2) : QString(),
            rd.notCounted }.join(QLatin1Char('\t'));
    }
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

}  // namespace Odometry
