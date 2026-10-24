#ifndef SESSIONSUMMARY_H
#define SESSIONSUMMARY_H

// =============================================================================
//  SessionSummary  (session 349)
//  -----------------------------------------------------------------------------
//  One pass over a recorded tab: its span, counts, and where the source went
//  quiet. The .dlr viewer's Summary dock shows it; a quiet span is listed so
//  the operator can jump to its edge. Read in the model's order (arrival
//  order, as recorded); a step back in time is not a gap.
// =============================================================================

#include <QVector>

class LogModel;

namespace SessionSummary {

struct Gap { qint64 fromMs = 0; qint64 toMs = 0; qint64 ms() const { return toMs - fromMs; } };

struct Source {
    int    records  = 0;
    int    errors   = 0;
    int    warnings = 0;
    qint64 firstMs  = -1;   // -1: no row carries a time
    qint64 lastMs   = -1;
    QVector<Gap> gaps;      // longer than the threshold, in order
    Gap    longest;         // the longest step between rows, gap or not
};

Source summarize(const LogModel *model, qint64 gapOverMs);

}  // namespace SessionSummary

#endif  // SESSIONSUMMARY_H
