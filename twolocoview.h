#ifndef TWOLOCOVIEW_H
#define TWOLOCOVIEW_H

// =============================================================================
//  Two-loco view (session 98) — Tools ▸ Monitor ▸ Two-loco view…
//  -----------------------------------------------------------------------------
//  Two tabs (two locos), one timeline: both locos' location and speed, the
//  gap between them, and SoS/collision/head-on/rear-end events, all against
//  time rather than track location — two locos are rarely at the same
//  location at the same moment, so time is the only axis both sides agree on.
//
//  POSITION AND SPEED
//    Reuses SpeedDistance::extract() per tab exactly as it stands (session
//    81) — no new decoding. Each loco's trace is read on its OWN
//    abs_loco_loc/@dmi (or @lsrp) scale.
//
//  THE GAP, AND WHY IT IS NOT TRUSTED BLINDLY
//    abs_loco_loc is not inherently a shared coordinate: a loco's own
//    location reads ~0 until it localises off its own RFID tag
//    (replaywindow.cpp's track-diagram view anchors several keys to a
//    shared origin for exactly this reason). This view does NOT do that
//    anchoring — it takes the two traces' locations at face value and
//    subtracts them, which is only meaningful when both locos are already
//    reporting on the same section. So Pair::plausible is set to false
//    (with Pair::warning saying why) whenever the two locos' location
//    ranges do not overlap at all — the cheapest check that catches "these
//    are two unrelated locos on different lines" without claiming to solve
//    general track alignment. The gap is still computed and shown; the
//    warning is the operator's cue to read it with that in mind.
//
//  SESSION 186: @sos
//    A log with @sos (soslog.h) gives the events from the firmware's own
//    threat spells, a trace when it has no @dmi/@lsrp, and bByA / aByB:
//    where each loco's SoS table had the other — drawn dashed beside the
//    other's own trace, so a difference between the two (an adjustment
//    tag, a stale ARP) shows as daylight between the lines.
//
//  SoS / COLLISION-TARGET / HEAD-ON / REAR-END
//    All already on the wire in ONE packet, @lsos (LOCO_SOS, schema), per
//    loco: is_access_sos_recvd / is_unusual_stop_recvd / is_train_parted_recvd
//    (tracked together as "SoS"), is_head_on_collision_recvd and
//    is_rear_end_collision_recvd (collision_distance is each one's
//    magnitude). Tracked into episodes with RunReport::EpisodeTracker
//    (runreport.h) — the same open/extend/close pattern RunReport uses for
//    overspeed and IncidentReport uses for EB/FSB, not a fourth copy of it.
// =============================================================================

#include "runreport.h"
#include "speeddistance.h"

#include <QString>
#include <QVector>

class LogModel;

namespace TwoLocoView {

// One loco's SoS/collision events in a tab.
struct Events {
    QVector<RunReport::Episode> sos;       // access SOS / unusual stop / train parted
    QVector<RunReport::Episode> headOn;    // is_head_on_collision_recvd
    QVector<RunReport::Episode> rearEnd;   // is_rear_end_collision_recvd
    // Session 172: from the loco's mode (its trace's samples).
    QVector<RunReport::Episode> trip;      // 7 (Trip)
    QVector<RunReport::Episode> failure;   // 12 (System_Failure)
};

// ---- Session 172: a loco as another loco heard it -----------------------------------
//
//  A loco's received ARPs (arprecv) carry the OTHER loco's ABS_LOCO_LOC,
//  TRAIN_SPEED, LOCO_MODE and EMERGENCY_STATUS, about every 2 s. So one
//  loco's log is enough for both: its own trace, and the other's as heard.
//  Excluded: the loco's own ID (its own ARP coming back, session 170) and
//  frames whose CRC fails (a garbled ID is not another loco).

// The other locos `model`'s received ARPs come from, ascending.
QVector<qint64> heardLocos(const LogModel *model);
// One heard loco's trace (source "arprecv") and its events.
SpeedDistance::Trace heardTrace(const LogModel *model, qint64 locoId, qint64 fromMs = 0, qint64 toMs = 0);
Events heardEvents(const LogModel *model, qint64 locoId, qint64 fromMs = 0, qint64 toMs = 0);
// Trip / System_Failure episodes from a trace's modes, into `events`.
void addModeEvents(const SpeedDistance::Trace &trace, Events *events);

// The SoS events in `model`, windowed if fromMs/toMs > 0 (both, like
// collectRowFields). Session 186: from @sos (the firmware's own threat
// spells, soslog.h) when the log has it; else from @lsos, as before.
Events extractEvents(const LogModel *model, qint64 fromMs = 0, qint64 toMs = 0);

struct GapSample {
    qint64 ms = 0;
    double gapM = 0.0;
    double aLocM = 0.0, bLocM = 0.0;   // the two locations it is the difference of
};

// Both locos' traces, events, and the gap between them, time-matched within
// `toleranceMs` (a gap sample needs a B sample no more than this long before
// the A sample it is paired with; otherwise that instant is skipped rather
// than interpolated across a silence).
struct Pair {
    QString keyA, keyB;
    SpeedDistance::Trace a, b;
    Events  eventsA, eventsB;
    QVector<GapSample> gap;
    bool    plausible = true;
    QString warning;

    // Known locations only (SpeedDistance::locationKnown).
    bool   hasLocA = false, hasLocB = false;
    double minLocM = 0.0, maxLocM = 1.0;   // over both locos
    double apartM = 0.0;                   // nearest points of the two known ranges; 0 if they overlap
    int    unknownA = 0, unknownB = 0;     // samples at 0 m

    // Session 186: where each loco's own SoS table (@sossrc) had the other:
    // bByA from A's log (B's position after SOSWithAdjustment, as A used
    // it), aByB from B's. gapM = other - own, as in `gap`. Empty without @sos.
    QVector<GapSample> bByA, aByB;
};

// Session 186: a tab's trace from @sos (own_abs_loc, sensor_speed taken as
// m/s), for a log with @sos but neither @dmi nor @lsrp. Source "sos".
SpeedDistance::Trace sosTrace(const LogModel *model, qint64 fromMs = 0, qint64 toMs = 0);

Pair build(const LogModel *modelA, const QString &keyA,
          const LogModel *modelB, const QString &keyB,
          qint64 fromMs = 0, qint64 toMs = 0, qint64 toleranceMs = 2000,
          double warnApartM = 0.0);

// Session 172: B is loco `heardId` as `heardBy`'s received ARPs report it.
Pair buildHeard(const LogModel *modelA, const QString &keyA,
                const LogModel *heardBy, qint64 heardId, const QString &keyB,
                qint64 fromMs = 0, qint64 toMs = 0, qint64 toleranceMs = 3000,
                double warnApartM = 0.0);

// CSV of the gap samples (time, gap_m), for export.
QString gapToCsv(const Pair &pair);

}  // namespace TwoLocoView

#endif // TWOLOCOVIEW_H
