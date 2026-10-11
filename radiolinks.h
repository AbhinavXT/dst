#ifndef RADIOLINKS_H
#define RADIOLINKS_H

// =============================================================================
//  Radio links (session 214) — the frame-loss table in Radio and GPS health.
//  -----------------------------------------------------------------------------
//  Frames counted by their FRAME_NUM (the sender's seconds since midnight
//  + 1), per link:
//    SLRP from each station (SOURCE_STN_ID): what the loco received
//    LSRP and ARP: what the loco itself sent, as logged
//
//  cycle      the median FRAME_NUM step (2 s in every capture so far):
//             read from the traffic, not assumed
//  lost       within contact, expected (the FRAME_NUM span over the cycle,
//             + 1) minus received. Counted over the span, not gap by gap:
//             a station's steps jitter 1 / 3 around 2 and that is no loss
//  gaps       steps of two cycles or more within contact
//  outage     a step over kOutageS: no contact (out of range, radio down,
//             powered off), listed with its length, not counted as lost
//  age        SLRP only: the loco's clock at arrival (its last own
//             LSRP / ARP FRAME_NUM, heard within kOwnClockMs, moved on by
//             the time since) minus the frame's. Whole seconds. Over 4 s
//             old or 2 s early the loco would not process it
//             (FrameClock::classify).
//
//  Observed, not judged: a loss rate is shown, not graded.
// =============================================================================

#include <QString>
#include <QVector>

class LogModel;

namespace RadioLinks {

constexpr int    kOutageS    = 30;      // a FRAME_NUM step over this: no contact
constexpr qint64 kOwnClockMs = 10000;   // the loco's own FRAME_NUM must be this recent for an age

struct Outage {
    qint64 fromMs = 0, toMs = 0;        // arrival of the frames either side
    int    seconds = 0;                 // the FRAME_NUM step
};

struct Link {
    QString name;                       // "SLRP from station 527", "LSRP (sent)", "ARP (sent)"
    QString type;                       // "slrp", "lsrp", "arp"
    int     station = -1;               // SLRP: SOURCE_STN_ID
    int     frames = 0, repeats = 0;    // repeats: the same FRAME_NUM again
    int     cycleS = 0;
    int     expected = 0, lost = 0, gaps = 0;
    QVector<Outage> outages;
    qint64  longestGapMs = 0;           // between arrivals
    // SLRP age on arrival.
    QVector<int> ages;
    int     ageMedian = 0, ageMax = 0, stale = 0, ahead = 0;
    double  lostPercent() const { return expected > 0 ? 100.0 * lost / expected : 0.0; }
};

struct Report {
    QVector<Link> links;                // SLRP by station, then LSRP, ARP
};

Report build(const LogModel *model, qint64 fromMs = 0, qint64 toMs = 0);

// One link's counts from its (arrival ms, FRAME_NUM) pairs, in arrival order
// (build() calls it; separate to test the rules alone). Leaves the ages alone.
void count(Link &link, const QVector<QPair<qint64, qint64>> &frames);

QString summaryText(const Report &r);

}  // namespace RadioLinks

#endif // RADIOLINKS_H
