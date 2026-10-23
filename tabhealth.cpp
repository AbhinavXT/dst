#include "tabhealth.h"

#include <QObject>
#include <QStringList>

TabHealth::Shown TabHealth::describe(const Reading &r)
{
    Shown s;
    if (r.name.isEmpty()) return s;

    QStringList parts{ r.name };
    if (r.silentMs < 0) {
        parts << QObject::tr("waiting for traffic");
        s.text = parts.join(QStringLiteral(" · "));
        s.tip = QObject::tr("Nothing has arrived on this tab yet.");
        return s;
    }

    parts << (r.rate >= 10 ? QObject::tr("%1/s").arg(qRound(r.rate))
              : r.rate > 0 ? QObject::tr("%1/s").arg(r.rate, 0, 'f', 1)
                           : QObject::tr("idle"));
    const double sec = r.silentMs / 1000.0;
    parts << (sec < 10 ? QObject::tr("%1 s ago").arg(sec, 0, 'f', 1) : QObject::tr("%1 s ago").arg(qint64(sec)));
    if (r.lateGaps > 0) parts << (r.lateGaps == 1 ? QObject::tr("1 gap") : QObject::tr("%1 gaps").arg(r.lateGaps));
    s.text = parts.join(QStringLiteral(" · "));

    const bool err = r.errSec > 0 && r.silentMs >= qint64(r.errSec) * 1000;
    const bool late = r.warnSec > 0 && r.silentMs >= qint64(r.warnSec) * 1000;
    const bool recentGap = r.sinceLastGapMs >= 0 && r.sinceLastGapMs <= kGapRecentMs;
    s.tone = err ? Tone::Fail : (late || recentGap) ? Tone::Warn : Tone::Ok;

    QStringList tip;
    if (err) tip << QObject::tr("Silent for %1 s: past the offline threshold (%2 s).").arg(qint64(sec)).arg(r.errSec);
    else if (late) tip << QObject::tr("Silent for %1 s: past the quiet threshold (%2 s).").arg(qint64(sec)).arg(r.warnSec);
    if (r.lateGaps > 0)
        tip << QObject::tr("%n time(s) since this tab opened, a kind of line came more than twice its usual "
                           "gap late (▲ in the Δt column): lines may be missing there.", nullptr, r.lateGaps);
    if (recentGap) tip << QObject::tr("The last was within a minute of the newest row.");
    tip << QObject::tr("Thresholds: Settings ▸ offline warning / error.");
    s.tip = tip.join(QLatin1Char('\n'));
    return s;
}
