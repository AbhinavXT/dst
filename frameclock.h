#ifndef FRAMECLOCK_H
#define FRAMECLOCK_H
// =============================================================================
//  frameclock.{h,cpp} — the equipment's clock, read out of FRAME_NUM.
//
//  FRAME_NUM is seconds-since-midnight PLUS ONE. Confirmed against the
//  captures rather than taken on trust: across 15,800 ARP, LSRP and SLRP
//  frames in replay/, FRAME_NUM minus the seconds-since-midnight of the
//  capture timestamp is
//
//      +1   14839 frames        the relationship
//      +0     527 frames        the capture timestamp is truncated to the
//      +2     433 frames        second, so a frame stamped at .999 lands one
//      +3       1 frame         second either side
//
//  WHY THIS IS WORTH A READOUT
//    The loco checks every packet's FRAME_NUM against its own clock and
//    REJECTS what falls outside a narrow window — more than 4 s old, or more
//    than 2 s in the future (ProcessSkavachLkavachRegularPkt). The stationary
//    end does the same to what the loco sends.
//
//    So when this laptop's clock drifts from the equipment's, everything
//    DLConsole transmits — Packet Maker, replay, the round-trip validator —
//    starts being discarded at the far end, and the only symptom is silence.
//    Nothing on screen would say why. The skew below is what says why.
//
//  This is arithmetic over integers with no state and no Qt widgets, so the
//  awkward parts — the +1, midnight, the accept window's asymmetry — can be
//  tested directly.
// =============================================================================

#include <QString>

class QDateTime;

namespace FrameClock {

// Seconds in a day. FRAME_NUM is 17 bits (max 131071), so it holds a day's
// worth of seconds with room to spare and never wraps mid-day.
constexpr int kSecondsPerDay = 86400;

// FRAME_NUM = seconds since midnight + 1. Named rather than written as a bare
// 1 at each use, because it is the single fact this whole file rests on.
constexpr int kFrameNumOffset = 1;

// The equipment's time of day, in seconds since midnight, from a frame
// number. Returns -1 for a frame number that cannot be a time of day.
int secondsSinceMidnight(qint64 frameNum);

// The inverse: what FRAME_NUM the equipment would be using at this second of
// the day. Used to seed a built packet when nothing has been observed.
qint64 frameNumFor(int secondsSinceMidnight);

// "15:02:31", or empty for a frame number that is not a time.
QString timeText(qint64 frameNum);

// How far the equipment's clock is AHEAD of the given local time, in seconds.
// Negative means the equipment is behind.
//
// Wraps at midnight: 00:00:01 against 23:59:59 is +2 s, not -86398. A skew of
// half a day or more is meaningless either way, so the wrap is applied around
// the shorter path.
int skewSeconds(qint64 frameNum, const QDateTime &localNow);

// What the far end would do with a packet carrying this skew.
//
// The window is ASYMMETRIC and that asymmetry is the point: a packet may be up
// to 4 s old but only 2 s early, so a laptop running fast is rejected sooner
// than one running slow. From the loco's own check:
//
//     if (TimeDiffInSeconds(pkt, own) <= -2 || TimeDiffInSeconds(pkt, own) > 4)
//         reject
//
enum class Accept {
    Ok,        // inside the window
    Marginal,  // inside it, but within a second of an edge
    Stale,     // too old: the far end will discard it
    Ahead      // too far in the future: likewise
};
Accept classify(int skew);

// One line naming what classify() decided and what it means for sending.
QString acceptText(Accept a);

}  // namespace FrameClock

#endif  // FRAMECLOCK_H
