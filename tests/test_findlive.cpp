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
//  Session 157: every arriving batch restarted the 200 ms debounce timer, so
//  under steady traffic the search the operator had typed waited for a gap
//  in the packets. Now a search covers the rows present when it ran: rows
//  arriving afterwards start no scan and are not searched until the search
//  is run again; rows falling off the front only renumber the matches.
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
    // The search covers the rows that were there when it ran. New rows are
    // not searched, and the matches already found stay where they are.
    model.appendEntries({
        mk(QStringLiteral("more quiet"),         4000),  // 3
        mk(QStringLiteral("STN_ID 9 upcoming"),  5000),  // 4  hit, not searched
    });
    settle();
    CHECK(bar.matchRows() == (QVector<int>{ 0, 2 }),
          "a row arriving after the search is not searched");

    for (int i = 0; i < 5; ++i) {
        model.appendEntries({ mk(QStringLiteral("STN_ID batch"), 6000 + i * 10) });
    }
    settle();
    CHECK(bar.matchRows() == (QVector<int>{ 0, 2 }),
          "nor are several batches of them");

    // ---- running the search again covers what has arrived ------------------
    edit->setText(QString());
    edit->setText(QStringLiteral("STN_ID"));
    settle();
    CHECK(bar.matchRows() == (QVector<int>{ 0, 2, 4, 5, 6, 7, 8, 9 }),
          "searching again covers every row there now");
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
              "and agrees exactly with a bar that never saw the traffic");
    }

    // ---- a search typed while traffic flows still runs ---------------------
    //
    // The field report itself: batches every 50 ms used to restart the
    // 200 ms debounce forever, so the typed search never ran.
    {
        edit->setText(QStringLiteral("quiet"));
        for (int i = 0; i < 12; ++i) {
            model.appendEntries({ mk(QStringLiteral("noise"), 10000 + i) });
            QTest::qWait(50);
        }
        CHECK(bar.matchRows() == (QVector<int>{ 1, 3 }),
              "a search typed during steady traffic runs 200 ms after typing, "
              "not after the traffic stops");
    }

    // ---- a change that really does renumber --------------------------------
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

    // ---- regex too ---------------------------------------------------------
    {
        auto *modeBox = bar.findChild<QComboBox *>(QStringLiteral("findModeBox"));
        CHECK(modeBox != nullptr, "the bar has a mode selector");
        if (modeBox) {
            modeBox->setCurrentIndex(modeBox->findData(int(FindBar::Mode::Regex)));
            edit->setText(QStringLiteral("STN_ID [0-9]+"));
            settle();
            const int before = bar.matchRows().size();
            CHECK(before > 0, "the regex matches something");

            model.appendEntries({ mk(QStringLiteral("STN_ID 42 upcoming"), 90000) });
            settle();
            CHECK(bar.matchRows().size() == before,
                  "a row arriving under a regex search waits for the next search");
            modeBox->setCurrentIndex(modeBox->findData(int(FindBar::Mode::Text)));
        }
    }
}

// Rows falling off the front (the tab at its capacity) renumber the matches
// without a rescan: the rest keep pointing at the same messages, and the
// cursor stays on its message.
TEST_SUITE(findliveeviction)
{
    LogModel model(nullptr, 10);         // trims 1 row (10%) per overflow
    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    QTableView view;
    view.setModel(&proxy);
    view.show();

    FindBar bar(&view);
    bar.activate();
    auto *edit = bar.findChild<QueryLineEdit *>(QStringLiteral("findEdit"));

    QVector<LogEntryPtr> rows;
    for (int i = 0; i < 10; ++i)
        rows << mk(i % 3 == 0 ? QStringLiteral("STN_ID %1").arg(i) : QStringLiteral("quiet"), 1000 + i);
    model.appendEntries(rows);                         // hits at 0, 3, 6, 9
    edit->setText(QStringLiteral("STN_ID"));
    settle();
    CHECK(bar.matchRows() == (QVector<int>{ 0, 3, 6, 9 }), "four hits");
    CHECK(bar.matchCursor() == 0, "on the first");

    model.appendEntries({ mk(QStringLiteral("STN_ID new"), 5000) });   // row 0 evicted
    settle();
    CHECK(bar.matchRows() == (QVector<int>{ 2, 5, 8 }),
          "the evicted hit is gone and the others moved up one row, not rescanned "
          "(the new row is not searched)");
    CHECK(bar.matchCursor() == 0, "the cursor moved to the next hit");
    bool same = true;
    for (int r : bar.matchRows())
        if (!proxy.data(proxy.index(r, LogModel::ColMessage)).toString().contains(QStringLiteral("STN_ID")))
            same = false;
    CHECK(same, "each match row is still a matching message");
}
