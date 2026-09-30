#include "testutil.h"

#include "findbar.h"
#include "logmodel.h"

#include <QLineEdit>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTest>

// =============================================================================
//  FindBar — holding the view against scroll lock.
//
//  The bug: with scroll lock on and traffic arriving, the viewport alternated
//  between the found row and the end of the log. Two independent causes, and
//  the fix needs both halves, so both are pinned here:
//
//    1. MainWindow scrolled to the bottom once per arriving batch regardless
//       of whether Find was parked on a match. That half is expressed here as
//       holdsView(): the owner is expected to skip its scrollToBottom() while
//       it is true, and to hear holdChanged(false) when the hold is released.
//
//    2. FindBar re-scanned on every model insert through its debounce timer,
//       and the rescan reset the cursor to match #0 and jumped there. That
//       alone dragged the operator back to the first match every 200 ms on a
//       live tab, with or without scroll lock. The rescan must now keep the
//       cursor on the same entry and leave the viewport alone.
//
//  The debounce is real (200 ms), so the checks below wait it out rather than
//  reaching in to fire it — waiting also proves the timer path itself is wired
//  the way the fix assumes.
// =============================================================================

namespace {

const int kDebounceWaitMs = 400;   // 200 ms debounce + slack for a loaded box

LogEntryPtr mkEntry(qint64 ms, const QString &text)
{
    auto e = QSharedPointer<LogEntry>::create();
    e->header.source_id = 33;
    e->header.kvchId    = 1;
    e->epochMs          = ms;
    e->text             = text;
    e->cacheDerived();
    return e;
}

// Everything a tab needs for the bar to be exercised the way MainWindow
// exercises it: model -> proxy -> view -> bar.
struct Fixture {
    LogModel               model{nullptr, 10000};
    QSortFilterProxyModel  proxy;
    QTableView             view;
    FindBar               *bar = nullptr;

    Fixture()
    {
        proxy.setSourceModel(&model);
        view.setModel(&proxy);
        bar = new FindBar(&view, &view);
        // The bar's whole notion of being active is isVisible(), and a
        // widget whose parent is hidden is never visible — so the view has
        // to be shown for any of this to mean anything. Headless is fine
        // (QT_QPA_PLATFORM=offscreen); nothing is ever looked at.
        view.resize(600, 400);
        view.show();
        QTest::qWait(20);
    }

    void append(const QVector<LogEntryPtr> &v) { model.appendEntries(v); }

    // Type into the bar the way a user does. The line edit is the only input
    // the bar has, so driving it directly is driving the real path.
    void type(const QString &s)
    {
        auto *edit = bar->findChild<QLineEdit *>();
        if (edit) { edit->setText(s); }
    }

    int currentProxyRow() const { return view.currentIndex().row(); }

    LogEntryPtr entryAtCurrent() const
    {
        const QModelIndex src = proxy.mapToSource(view.currentIndex());
        return src.isValid() ? model.entryAt(src.row()) : LogEntryPtr();
    }
};

}  // namespace

TEST_SUITE(findhold)
{
    // ---- a hidden bar never holds the view ---------------------------------
    {
        Fixture f;
        QVector<LogEntryPtr> v;
        for (int i = 0; i < 20; ++i) {
            v << mkEntry(1000 + i, i % 5 == 0 ? QStringLiteral("STN_ID hit %1").arg(i)
                                              : QStringLiteral("noise %1").arg(i));
        }
        f.append(v);
        CHECK(!f.bar->holdsView(), "a bar that was never opened holds nothing");
    }

    // ---- parked on a match -> holds; closed -> releases ---------------------
    {
        Fixture f;
        QVector<LogEntryPtr> v;
        for (int i = 0; i < 20; ++i) {
            v << mkEntry(1000 + i, i % 5 == 0 ? QStringLiteral("STN_ID hit %1").arg(i)
                                              : QStringLiteral("noise %1").arg(i));
        }
        f.append(v);

        QSignalSpy holdSpy(f.bar, &FindBar::holdChanged);

        f.bar->activate();
        f.type(QStringLiteral("STN_ID"));
        QTest::qWait(kDebounceWaitMs);

        CHECK(f.bar->holdsView(), "parked on a match: the bar holds the view");
        CHECK(holdSpy.count() == 1, "holdChanged fired once on taking the hold");
        CHECK(holdSpy.count() > 0 && holdSpy.last().at(0).toBool() == true,
              "and reported the hold as taken");
        CHECK(f.currentProxyRow() == 0, "landed on the first match (row 0)");

        f.bar->close();
        CHECK(!f.bar->holdsView(), "closing the bar releases the view");
        CHECK(holdSpy.count() == 2, "holdChanged fired again on release");
        CHECK(holdSpy.count() > 1 && holdSpy.last().at(0).toBool() == false,
              "and reported the release, so the owner can resume following");
    }

    // ---- a query with no matches must not hold anything ---------------------
    {
        Fixture f;
        QVector<LogEntryPtr> v;
        for (int i = 0; i < 10; ++i) { v << mkEntry(1000 + i, QStringLiteral("noise")); }
        f.append(v);

        f.bar->activate();
        f.type(QStringLiteral("nothing matches this"));
        QTest::qWait(kDebounceWaitMs);

        CHECK(!f.bar->holdsView(),
              "no match to sit on: the tail keeps running");
    }

    // ---- THE REGRESSION: new traffic must not move the cursor or the view ---
    {
        Fixture f;
        QVector<LogEntryPtr> v;
        for (int i = 0; i < 30; ++i) {
            v << mkEntry(1000 + i, i % 10 == 0 ? QStringLiteral("STN_ID hit %1").arg(i)
                                               : QStringLiteral("noise %1").arg(i));
        }
        f.append(v);                       // matches at rows 0, 10, 20

        f.bar->activate();
        f.type(QStringLiteral("STN_ID"));
        QTest::qWait(kDebounceWaitMs);
        CHECK(f.currentProxyRow() == 0, "starts on the first match");

        // Walk to the third match, the way F3 does. findNext is a private
        // slot, which is exactly what a shortcut invokes, so going through
        // the meta-object is the same path and not a back door.
        QMetaObject::invokeMethod(f.bar, "findNext");
        QMetaObject::invokeMethod(f.bar, "findNext");
        const int parkedRow          = f.currentProxyRow();
        const LogEntryPtr parkedOn   = f.entryAtCurrent();
        CHECK(parkedRow == 20, "F3 twice reaches the third match");
        CHECK(f.bar->holdsView(), "still holding after navigating");

        // Now the tab gets busy — including more matching rows.
        QVector<LogEntryPtr> more;
        for (int i = 0; i < 40; ++i) {
            more << mkEntry(2000 + i, i % 10 == 0 ? QStringLiteral("STN_ID hit x%1").arg(i)
                                                  : QStringLiteral("noise x%1").arg(i));
        }
        f.append(more);
        QTest::qWait(kDebounceWaitMs);   // let the model-driven rescan run

        CHECK(f.currentProxyRow() == parkedRow,
              "traffic does not drag the cursor back to the first match");
        CHECK(f.entryAtCurrent() == parkedOn,
              "and it is still the same entry, not just the same row number");
        CHECK(f.bar->holdsView(),
              "the hold survives the rescan, so the owner keeps not scrolling");
    }

    // ---- typing after that still jumps: a user rescan is not a model one ----
    {
        Fixture f;
        QVector<LogEntryPtr> v;
        for (int i = 0; i < 30; ++i) {
            v << mkEntry(1000 + i, QStringLiteral("row %1 %2")
                                       .arg(i)
                                       .arg(i == 7 ? "NEEDLE" : "hay"));
        }
        f.append(v);

        f.bar->activate();
        f.type(QStringLiteral("hay"));
        QTest::qWait(kDebounceWaitMs);
        CHECK(f.currentProxyRow() == 0, "first query lands on its first match");

        f.type(QStringLiteral("NEEDLE"));
        QTest::qWait(kDebounceWaitMs);
        CHECK(f.currentProxyRow() == 7,
              "retyping still takes the user to the new first match");
        CHECK(f.bar->holdsView(), "and holds there");
    }
}
