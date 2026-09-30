#include "testutil.h"

#include "markerscrollbar.h"
#include "logmodel.h"
#include "logentry.h"

#include <QSortFilterProxyModel>

// =============================================================================
//  Stepping between problems.
//
//  The minimap has always drawn where the errors and warnings are; there was
//  no way to move between them, so finding the three bad frames in a two-hour
//  capture meant dragging the scrollbar and squinting at the marks.
//
//  nextMarkedRow() answers the same question binRowMarks() answers for
//  drawing, one row at a time — same mapping, same severity test — so what
//  the operator lands on is always a row the minimap has a mark for. These
//  checks pin that agreement, and the decision not to wrap.
// =============================================================================

namespace {

LogEntryPtr mk(qint64 ms, Severity s = Severity::Info)
{
    auto e = QSharedPointer<LogEntry>::create();
    e->header.source_id = 33;
    e->header.kvchId    = 1;
    e->epochMs  = ms;
    e->severity = s;
    e->text     = QStringLiteral("row");
    e->cacheDerived();
    return e;
}

}  // namespace

TEST_SUITE(problemnav)
{
    // Rows 0..19, with a warning at 4, errors at 9 and 15.
    LogModel model(nullptr, 1000);
    {
        QVector<LogEntryPtr> v;
        for (int i = 0; i < 20; ++i) {
            Severity s = Severity::Info;
            if (i == 4)             { s = Severity::Warn;  }
            if (i == 9 || i == 15)  { s = Severity::Error; }
            v.append(mk(1000 + i, s));
        }
        model.appendEntries(v);
    }

    // ---- forward -----------------------------------------------------------
    {
        CHECK(nextMarkedRow(nullptr, &model, -1, +1) == 4,
              "from before the start, the first mark is the warning at 4");
        CHECK(nextMarkedRow(nullptr, &model, 4, +1) == 9, "then the error at 9");
        CHECK(nextMarkedRow(nullptr, &model, 9, +1) == 15, "then the one at 15");
        CHECK(nextMarkedRow(nullptr, &model, 15, +1) == -1,
              "and past the last one there is nothing — it does not wrap");
    }

    // ---- backward ----------------------------------------------------------
    {
        CHECK(nextMarkedRow(nullptr, &model, 20, -1) == 15, "from the end, back to 15");
        CHECK(nextMarkedRow(nullptr, &model, 15, -1) == 9, "then 9");
        CHECK(nextMarkedRow(nullptr, &model, 9, -1) == 4, "then 4");
        CHECK(nextMarkedRow(nullptr, &model, 4, -1) == -1,
              "and above the first there is nothing");
    }

    // ---- standing on a mark ------------------------------------------------
    // Pressing the key while already on a problem must move OFF it, or the
    // key looks broken on every second press.
    {
        CHECK(nextMarkedRow(nullptr, &model, 9, +1) != 9, "forward leaves the current row");
        CHECK(nextMarkedRow(nullptr, &model, 9, -1) != 9, "and so does backward");
    }

    // ---- errors only -------------------------------------------------------
    {
        CHECK(nextMarkedRow(nullptr, &model, -1, +1, /*errorsOnly=*/true) == 9,
              "errors-only skips the warning at 4");
    }

    // ---- through a filter --------------------------------------------------
    // The row numbers returned have to be PROXY rows, because that is what the
    // view selects with. Returning a source row here would scroll to a
    // plausible-looking wrong line whenever a filter was active.
    {
        QSortFilterProxyModel proxy;
        proxy.setSourceModel(&model);
        proxy.setFilterKeyColumn(-1);
        proxy.setFilterFixedString(QString());

        const int all = nextMarkedRow(&proxy, &model, -1, +1);
        CHECK(all == 4, "with an inert filter the answer is unchanged");

        // Hide everything: no mark can be reached, and it must say so rather
        // than returning a row that is not on screen.
        proxy.setFilterFixedString(QStringLiteral("nothing matches this string"));
        CHECK(proxy.rowCount() == 0, "the filter hides every row");
        CHECK(nextMarkedRow(&proxy, &model, -1, +1) == -1,
              "and there is no problem to go to");
    }

    // ---- degenerate inputs -------------------------------------------------
    {
        CHECK(nextMarkedRow(nullptr, nullptr, 0, +1) == -1, "null source");
        CHECK(nextMarkedRow(nullptr, &model, 0, 0) == -1, "no direction");
        LogModel empty(nullptr, 100);
        CHECK(nextMarkedRow(nullptr, &empty, -1, +1) == -1, "empty model");
        CHECK(nextMarkedRow(nullptr, &model, 999, +1) == -1,
              "a start beyond the end finds nothing rather than reading past it");
        CHECK(nextMarkedRow(nullptr, &model, -999, -1) == -1,
              "and so does one before the beginning");
    }
}
