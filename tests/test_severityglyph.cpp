#include "testutil.h"
#include "logmodel.h"

#include <QSharedPointer>

// Severity must be distinguishable WITHOUT colour. Red/orange/grey alone
// fails for red-green colour blindness (~1 man in 12), for greyscale
// printouts, and for a screenshot pasted into a monochrome report.
TEST_SUITE(severityglyph)
{
    LogModel m(nullptr, 100);
    QVector<LogEntryPtr> v;
    for (Severity s : { Severity::Info, Severity::Warn, Severity::Error }) {
        auto e = QSharedPointer<LogEntry>::create();
        e->epochMs = 1000; e->header.source_id = 1; e->header.kvchId = 1;
        e->severity = s; e->text = "x"; e->cacheDerived();
        v.append(e);
    }
    m.appendEntries(v);

    auto sevText = [&m](int row) {
        return m.data(m.index(row, LogModel::ColSeverity),
                      Qt::DisplayRole).toString();
    };

    const QString info = sevText(0), warn = sevText(1), err = sevText(2);

    // The core requirement: three distinct, non-empty labels.
    CHECK(!info.isEmpty(), "info is not blank — 'routine' and 'unclassified' "
                           "must not look identical");
    CHECK(!warn.isEmpty(), "warn is labelled");
    CHECK(!err.isEmpty(),  "error is labelled");
    CHECK(info != warn && warn != err && info != err,
          "all three severities are textually distinct");

    // Each carries a glyph, so the distinction survives with no colour at
    // all. Checked by character rather than by exact string so the wording
    // can change without breaking the accessibility guarantee.
    auto hasGlyph = [](const QString &s) {
        return !s.isEmpty() && !s.at(0).isLetterOrNumber() && !s.at(0).isSpace();
    };
    CHECK(hasGlyph(info), "info has a leading glyph");
    CHECK(hasGlyph(warn), "warn has a leading glyph");
    CHECK(hasGlyph(err),  "error has a leading glyph");
    CHECK(info.at(0) != warn.at(0), "info and warn glyphs differ");
    CHECK(warn.at(0) != err.at(0),  "warn and error glyphs differ");
    CHECK(info.at(0) != err.at(0),  "info and error glyphs differ");

    // The word survives too: glyphs alone would be a different
    // accessibility problem (screen readers, unfamiliar symbols).
    CHECK(warn.contains("WARN"), "warn keeps its word");
    CHECK(err.contains("ERR"),   "error keeps its word");
    CHECK(info.contains("info"), "info keeps its word");

    // Substring matching against the display text still works — the filter
    // bar's defensive fallback path compares this way, and exact equality
    // would have broken silently when the glyph was added.
    CHECK(err.contains("ERR") && !warn.contains("ERR"),
          "'ERR' substring matches error only");
    CHECK(warn.contains("WARN"), "'WARN' substring matches warn");

    // Colour is still applied — the glyph is an ADDITIONAL channel, not a
    // replacement, since colour is faster to scan for those who can see it.
    const QVariant fg = m.data(m.index(2, LogModel::ColSeverity),
                               Qt::ForegroundRole);
    CHECK(fg.isValid(), "error still carries a foreground colour");
}
