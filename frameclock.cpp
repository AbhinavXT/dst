#include "frameclock.h"

#include <QDateTime>
#include <QTime>

namespace FrameClock {

int secondsSinceMidnight(qint64 frameNum)
{
    if (frameNum < 0) { return -1; }
    const qint64 s = frameNum - kFrameNumOffset;
    // A frame number of 0 would mean one second before midnight, which is a
    // frame number nothing should be sending; treat it as not-a-time rather
    // than wrapping it into yesterday.
    if (s < 0 || s >= kSecondsPerDay) { return -1; }
    return int(s);
}

qint64 frameNumFor(int secondsSinceMidnight)
{
    if (secondsSinceMidnight < 0 || secondsSinceMidnight >= kSecondsPerDay) {
        return -1;
    }
    return qint64(secondsSinceMidnight) + kFrameNumOffset;
}

QString timeText(qint64 frameNum)
{
    const int s = secondsSinceMidnight(frameNum);
    if (s < 0) { return QString(); }
    return QTime(0, 0).addSecs(s).toString(QStringLiteral("HH:mm:ss"));
}

int skewSeconds(qint64 frameNum, const QDateTime &localNow)
{
    const int equip = secondsSinceMidnight(frameNum);
    if (equip < 0 || !localNow.isValid()) { return 0; }

    const QTime t = localNow.time();
    const int local = t.hour() * 3600 + t.minute() * 60 + t.second();

    int diff = equip - local;
    // Take the shorter way round the clock face. Without this, one second
    // after midnight against one second before reads as most of a day, and the
    // readout would scream at exactly the moment it should be quiet.
    if (diff >  kSecondsPerDay / 2) { diff -= kSecondsPerDay; }
    if (diff < -kSecondsPerDay / 2) { diff += kSecondsPerDay; }
    return diff;
}

Accept classify(int skew)
{
    // The window the far end applies, from the loco's own check:
    //
    //     if (TimeDiffInSeconds(pkt, own) <= -2 || > 4) reject
    //
    // where the difference is the packet's AGE at the receiver. Our packets
    // carry our clock, so their age at the equipment is exactly the skew
    // below: equipment time minus ours. A positive skew means our frames look
    // old to them; a negative one means they look like the future.
    //
    // Note the boundaries are not symmetric and neither is inclusive in the
    // same direction: 4 is still accepted, -2 is already rejected.
    if (skew > 4)  { return Accept::Stale; }
    if (skew <= -2) { return Accept::Ahead; }
    if (skew == 4 || skew == -1) { return Accept::Marginal; }
    return Accept::Ok;
}

QString acceptText(Accept a)
{
    switch (a) {
    case Accept::Ok:
        return QStringLiteral("within the window the equipment accepts");
    case Accept::Marginal:
        return QStringLiteral("at the edge of the window — a second's drift "
                              "either way and packets start being discarded");
    case Accept::Stale:
        return QStringLiteral("this laptop is behind the equipment by more "
                              "than 4 s: anything sent from here looks stale "
                              "and will be discarded");
    case Accept::Ahead:
        return QStringLiteral("this laptop is ahead of the equipment by more "
                              "than 2 s: anything sent from here looks like a "
                              "future frame and will be discarded");
    }
    return QString();
}

}  // namespace FrameClock
