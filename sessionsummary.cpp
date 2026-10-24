#include "sessionsummary.h"

#include "logmodel.h"

SessionSummary::Source SessionSummary::summarize(const LogModel *model, qint64 gapOverMs)
{
    Source s;
    if (!model) { return s; }
    qint64 prev = -1;
    for (int r = 0; r < model->rowCount(); ++r) {
        const LogEntryPtr e = model->entryAt(r);
        if (!e) { continue; }
        ++s.records;
        if (e->severity == Severity::Error)     { ++s.errors; }
        else if (e->severity == Severity::Warn) { ++s.warnings; }
        if (e->epochMs <= 0) { continue; }
        if (s.firstMs < 0 || e->epochMs < s.firstMs) { s.firstMs = e->epochMs; }
        if (e->epochMs > s.lastMs)                   { s.lastMs = e->epochMs; }
        if (prev > 0 && e->epochMs > prev) {
            const Gap g{ prev, e->epochMs };
            if (g.ms() > s.longest.ms()) { s.longest = g; }
            if (g.ms() > gapOverMs)      { s.gaps.append(g); }
        }
        prev = e->epochMs;
    }
    return s;
}
