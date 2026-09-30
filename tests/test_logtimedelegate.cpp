#include "testutil.h"

#include "logmodel.h"
#include "logtableview.h"
#include "logtimedelegate.h"

#include <QTableView>

// =============================================================================
//  The Time column's painter.
//
//  Consecutive rows share almost every character of their timestamp. Reading
//  11:04:09.100 / 11:04:09.140 / 11:04:09.980, the eye walks past eight
//  identical characters to reach the three that differ, on every row, for
//  thousands of rows — the information is all in the tail and the ink was all
//  in the head.
//
//  Painting is not testable here; the DECISION is, and it is the whole
//  behaviour. What matters is that the muted run is never wrong: dimming one
//  character too many hides a digit that changed.
// =============================================================================

TEST_SUITE(logtimedelegate)
{
    using D = LogTimeDelegate;

    // ---- the ordinary case -------------------------------------------------
    {
        const int n = D::sharedPrefix(QStringLiteral("11:04:09.140"),
                                      QStringLiteral("11:04:09.100"));
        CHECK(n == 10,
              "everything up to the digit that changed is muted, and the "
              "digit that changed is not — .140 against .100 keeps the last "
              "two characters lit, not just the one that differs");
    }
    {
        const int n = D::sharedPrefix(QStringLiteral("11:04:10.000"),
                                      QStringLiteral("11:04:09.980"));
        CHECK(n == 6,
              "a second rolling over lights up from the seconds onward");
    }
    {
        const int n = D::sharedPrefix(QStringLiteral("12:00:00.000"),
                                      QStringLiteral("11:59:59.999"));
        CHECK(n == 1,
              "an hour rolling over is lit from the second character on — "
              "12 and 11 still share their leading 1, and claiming otherwise "
              "would mean dimming by meaning rather than by characters");
    }

    // ---- the first row has nothing above it --------------------------------
    {
        CHECK(D::sharedPrefix(QStringLiteral("11:04:09.100"), QString()) == 0,
              "with no row above, nothing is muted");
    }

    // ---- identical times stay readable -------------------------------------
    //
    // A burst arriving inside one millisecond is normal here. A row muted
    // end to end would read as disabled rather than as identical.
    {
        const QString same = QStringLiteral("11:04:09.100");
        const int n = D::sharedPrefix(same, same);
        CHECK(n == same.size() - 1,
              "two rows in the same millisecond still keep a character at "
              "full weight");
        CHECK(n < same.size(), "so the row never goes entirely grey");
    }

    // ---- the bookmark bullet is not part of the time -----------------------
    //
    // LogModel prefixes a bookmarked row's time with a bullet. Comparing the
    // rendered strings would make a bookmarked row share nothing with its
    // neighbour and light up entirely for the wrong reason.
    {
        CHECK(D::bulletOf(QStringLiteral("● 11:04:09.140"))
                  == QStringLiteral("● "),
              "the bullet is recognised");
        CHECK(D::bulletOf(QStringLiteral("11:04:09.140")).isEmpty(),
              "and absent when the row is not bookmarked");

        const int n = D::sharedPrefix(QStringLiteral("● 11:04:09.140"),
                                      QStringLiteral("11:04:09.100"));
        CHECK(n == 10,
              "a bookmarked row is still the same second as its neighbour, "
              "and is muted the same way");

        const int m = D::sharedPrefix(QStringLiteral("11:04:09.140"),
                                      QStringLiteral("● 11:04:09.100"));
        CHECK(m == 10, "and so is the row after a bookmarked one");
    }

    // ---- a shorter or longer neighbour -------------------------------------
    {
        CHECK(D::sharedPrefix(QStringLiteral("11:04"),
                              QStringLiteral("11:04:09.100")) == 4,
              "a shorter time is never over-run — the count stops inside "
              "the shorter of the two, one short of consuming it whole");
        CHECK(D::sharedPrefix(QString(), QStringLiteral("11:04:09.100")) == 0,
              "and an empty cell mutes nothing");
    }

    // ---- every log table gets it -------------------------------------------
    //
    // Installed by LogTableView::configure, so the log tabs, the compare
    // panes and anything else built through it agree. A column painted one
    // way in one window and another way beside it is worse than either.
    {
        QTableView view;
        LogTableView::configure(&view);
        CHECK(view.itemDelegateForColumn(LogModel::ColTime) != nullptr,
              "configure() installs the Time delegate");
        CHECK(view.itemDelegateForColumn(LogModel::ColMessage) == nullptr,
              "and leaves the other columns alone");

        // Configuring twice must not stack delegates: rebinding a compare
        // pane runs parts of this path again.
        QAbstractItemDelegate *first =
            view.itemDelegateForColumn(LogModel::ColTime);
        LogTableView::configure(&view);
        CHECK(view.itemDelegateForColumn(LogModel::ColTime) == first,
              "and configuring again keeps the one already there");
    }
}
