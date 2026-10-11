#ifndef ODOMETRY_H
#define ODOMETRY_H

// =============================================================================
//  Odometry at the tags (session 213) — Tools ▸ Monitor ▸ Equipment ▸ Odometry at the tags…
//  -----------------------------------------------------------------------------
//  How far the loco's own location had drifted from the track when it read
//  each RFID tag. The loco relocates on every tag, so the difference is the
//  odometry error gathered since the tag before.
//
//  HOW
//    For each main tag read (@rfid, CRC good, duplication 0; a second read of
//    the same tag within kSameReadMs -- the other reader -- is the same
//    pass): the last @dmi before it gives abs_loco_loc and train_speed; the
//    loco's own location at the read is that location moved on by
//    train_speed for the time since that frame (the DMI comes about once a
//    second, 15-30 m apart at speed). Error = that estimate minus the tag's
//    abs_loc, signed along the direction of travel: + means the loco thought
//    it was further along than the tag.
//    Direction: from the @dmi location after the read (else before it).
//
//  NOT COUNTED (listed, with the reason)
//    the loco not localised (abs_loco_loc 0), no @dmi within kStaleMs before
//    the read, speed not known (511), and an error over kJumpM: that is a
//    relocation or another tag under this id, not drift.
//
//  Observed, not judged: no limit is graded. A median error that grows with
//  the tag spacing points at the wheel diameter; one that does not, at a
//  fixed offset (antenna position, read latency).
// =============================================================================

#include <QString>
#include <QVector>

class LogModel;

namespace Odometry {

constexpr qint64 kSameReadMs = 10000;   // the same tag again within this: the other reader, same pass
constexpr qint64 kStaleMs    = 2000;    // the @dmi before the read must be this recent
constexpr double kJumpM      = 50.0;    // a larger error is a relocation, not drift
constexpr qint64 kRunGapMs   = 600000;  // the tag before must be this recent to give a spacing

struct Read {
    int     row = -1;                   // the @rfid row
    qint64  ms = 0;
    int     tag = 0;                    // unique id
    int     type = 0;
    double  tagLocM = 0.0;
    double  dmiLocM = 0.0;              // the @dmi before the read
    double  speedKmh = 0.0;
    qint64  sinceDmiMs = 0;
    int     direction = 0;              // +1 locations rising, -1 falling, 0 unknown
    double  estimateM = 0.0;            // the loco's own location at the read
    double  errorM = 0.0;               // along the direction of travel
    double  spacingM = -1.0;            // from the tag before in this run; < 0 none
    QString notCounted;                 // empty: counted
    bool counted() const { return notCounted.isEmpty(); }
    // Metres of error per km of tag spacing; NaN without a spacing.
    double perKm() const;
};

struct Report {
    QVector<Read> reads;
    int    counted = 0;
    double medianM = 0.0, meanM = 0.0, maxAbsM = 0.0;
    double medianPerKm = 0.0;           // over the counted reads with a spacing
    int    withSpacing = 0;
};

Report build(const LogModel *model);

// The statistics over `reads` (build() calls it; separate to test it alone).
void summarise(Report &r);

QString summaryText(const Report &r);

// The reads, tab-separated, for the clipboard.
QString toTsv(const Report &r);

}  // namespace Odometry

#endif // ODOMETRY_H
