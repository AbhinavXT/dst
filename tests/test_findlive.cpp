#include "testutil.h"

#include "findbar.h"
#include "logentry.h"
#include "logmodel.h"
#include "querylineedit.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QtTest/QTest>

// =============================================================================
//  Find while traffic is arriving.
//
//  Reported from the field: with packets coming in, Find crawled — and the
//  moment the ethernet cable came out it was instant again.
//
//  The cause was that EVERY model change was treated as a renumbering. It is
//  true of a filter change, a re-sort or a row falling off the front, and it
//  is the reason narrowing has to be given up in those cases. It is NOT true
//  of an append, which is what live traffic does: rows land at the end, every
//  row already scanned keeps its number, and its verdict with it. Treating
//  that as dirty meant a full scan of the whole tab for every batch that
//  arrived, so cost grew with the square of the session while it ran, and
//  dropped to nothing the instant it stopped.
//
//  What is worth testing is that the tail scan gives the SAME answer as a
//  full one — a faster search that is wrong is not a faster search — and
//  that it really is a tail scan and not a full one wearing a hat.
// =============================================================================

namespace {

LogEntryPtr mk(const QString &text, qint64 ms)
{
    auto e = QSharedPointer<LogEntry>::create();
    e->header.source_id = 21;
    e->header.kvchId    = 1;
    e->epochMs   = ms;
    e->severity  = Severity::Info;
    e->text      = text;
    e->cacheDerived();
    return e;
}

// Settle the debounce timer the bar uses to coalesce arriving batches.
void settle()
{
    QTest::qWait(260);
    QCoreApplication::processEvents();
}

}  // namespace

TEST_SUITE(findlivetraffic)
{
    LogModel model(nullptr, 100000);
    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    QTableView view;
    view.setModel(&proxy);
    view.show();

    FindBar bar(&view);
    bar.activate();

    auto *edit = bar.findChild<QueryLineEdit *>(QStringLiteral("findEdit"));
    CHECK(edit != nullptr, "the bar has an input box");

    // A first batch, and a search over it.
    model.appendEntries({
        mk(QStringLiteral("STN_ID 4 upcoming"), 1000),   // 0  hit
        mk(QStringLiteral("quiet"),             2000),   // 1
        mk(QStringLiteral("STN_ID 7 upcoming"), 3000),   // 2  hit
    });
    edit->setText(QStringLiteral("STN_ID"));
    settle();

    CHECK(bar.matchRows() == (QVector<int>{ 0, 2 }),
          "the first scan finds both");

    // ---- traffic arrives ---------------------------------------------------
    //
    // Appended, which is what a live source does. The two hits already found
    // are still hits and still at rows 0 and 2.
    model.appendEntries({
        mk(QStringLiteral("more quiet"),         4000),  // 3
        mk(QStringLiteral("STN_ID 9 upcoming"),  5000),  // 4  hit
    });
    settle();

    CHECK(bar.matchRows() == (QVector<int>{ 0, 2, 4 }),
          "the new row is found and the old ones are kept, in row order");

    // Several batches without a search change — the live case, repeated.
    for (int i = 0; i < 5; ++i) {
        model.appendEntries({ mk(QStringLiteral("STN_ID batch"), 6000 + i * 10) });
    }
    settle();
    CHECK(bar.matchRows().size() == 8,
          "every batch adds its hits");

    // ---- the tail scan agrees with a full one ------------------------------
    //
    // The real risk of an incremental scan is a subtly different answer, so
    // the same model is searched from scratch by a second bar that never saw
    // the appends and has nothing to reuse.
    {
        QTableView fresh;
        fresh.setModel(&proxy);
        fresh.show();
        FindBar cold(&fresh);
        cold.activate();
        auto *coldEdit =
            cold.findChild<QueryLineEdit *>(QStringLiteral("findEdit"));
        coldEdit->setText(QStringLiteral("STN_ID"));
        settle();

        CHECK(cold.matchRows() == bar.matchRows(),
              "a bar that scanned the whole model in one go agrees exactly "
              "with the one that scanned it in pieces");
    }

    // ---- changing the search still rescans everything ----------------------
    //
    // The append path is only sound for the SAME search. A different one has
    // to look at the rows it never asked about.
    {
        edit->setText(QStringLiteral("quiet"));
        settle();
        CHECK(bar.matchRows() == (QVector<int>{ 1, 3 }),
              "a new pattern is answered from the whole model, including the "
              "rows the previous search had already dismissed");
    }

    // ---- and so does a change that really does renumber --------------------
    //
    // A filter is the case the dirty flag exists for: row 2 of the proxy is
    // a different message afterwards, so nothing carried over would be true.
    {
        edit->setText(QStringLiteral("STN_ID"));
        settle();
        const int before = bar.matchRows().size();
        CHECK(before > 0, "there are hits to lose");

        proxy.setFilterFixedString(QStringLiteral("upcoming"));
        settle();

        const QVector<int> after = bar.matchRows();
        bool allInRange = true;
        for (int r : after) {
            if (r < 0 || r >= proxy.rowCount()) { allInRange = false; }
        }
        CHECK(allInRange,
              "after a filter every match row is a row that still exists — "
              "carrying the old numbers across would point at other messages");

        // And they are the right rows, not merely valid ones.
        bool allMatch = true;
        for (int r : after) {
            const QString cell =
                proxy.data(proxy.index(r, LogModel::ColMessage)).toString();
            if (!cell.contains(QStringLiteral("STN_ID"))) { allMatch = false; }
        }
        CHECK(allMatch, "and each really does contain the pattern");

        proxy.setFilterFixedString(QString());
        settle();
    }

    // ---- regex and hex are not excluded ------------------------------------
    //
    // Appending needs only "is this the same search", which is true of every
    // mode. Deciding it on the DECODED pattern would have quietly excluded
    // Regex and Hex, whose decoded text is empty — the two modes with the
    // costliest per-row test, and so the two that need this most.
    {
        auto *modeBox = bar.findChild<QComboBox *>(QStringLiteral("findModeBox"));
        CHECK(modeBox != nullptr, "the bar has a mode selector");
        if (modeBox) {
            modeBox->setCurrentIndex(modeBox->findData(int(FindBar::Mode::Regex)));
            edit->setText(QStringLiteral("STN_ID [0-9]+"));
            settle();
            const int before = bar.matchRows().size();
            CHECK(before > 0, "the regex matches something");

            model.appendEntries({ mk(QStringLiteral("STN_ID 42 upcoming"), 9000) });
            settle();
            CHECK(bar.matchRows().size() == before + 1,
                  "and a row arriving under a regex search is picked up too");
        }
    }
}
