#include "testutil.h"

#include "comparewindow.h"
#include "findbar.h"
#include "logentry.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "namemap.h"
#include "settings.h"

#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QTableView>
#include <QTest>

// =============================================================================
//  Typing into a compare pane's find bar.
//
//  Reported: the console crashes as soon as anything is typed into Find in a
//  compare tab. The log tabs are fine, and the difference between them is the
//  model shape — a log tab shows a LogModel through a filter proxy, a compare
//  pane binds the LogModel straight to the view.
//
//  So the reproduction is the typing, not the opening: the bar appears, and
//  the first keystroke is what reaches the scan.
// =============================================================================

namespace {

LogEntryPtr mk(const QString &text, qint64 ms)
{
    auto e = QSharedPointer<LogEntry>::create();
    e->header.source_id = 21;
    e->header.kvchId    = 1;
    e->epochMs  = ms;
    e->severity = Severity::Info;
    e->text     = text;
    e->rawBytes = QByteArray::fromHex("0a1b2c3d");
    e->cacheDerived();
    return e;
}

}  // namespace

TEST_SUITE(comparefind)
{
    const bool savedDetached = Settings::findDetached();
    const int  savedMode     = Settings::findMode();
    Settings::setFindDetached(false);      // the in-pane strip, as reported
    Settings::setFindMode(0);

    NameMap names;
    MessageDispatcher dispatcher;

    // A pane binds nothing until a source exists to pick, so the sources are
    // created before the window: this reproduces the real order, where the
    // find bar is built against a view that has NO model yet and is given one
    // afterwards.
    LogModel *m21 = dispatcher.ensureModel(QStringLiteral("21_1"));
    LogModel *m33 = dispatcher.ensureModel(QStringLiteral("33_1"));
    CHECK(m21 && m33, "two sources exist to compare");
    m21->appendEntries({ mk(QStringLiteral("STN_ID 4 upcoming"), 1000),
                         mk(QStringLiteral("quiet"),             2000),
                         mk(QStringLiteral("STN_ID 7 upcoming"), 3000) });
    m33->appendEntries({ mk(QStringLiteral("other source"), 1500) });

    CompareWindow win(&dispatcher, &names);
    win.show();
    QTest::qWait(20);

    const QList<FindBar *> bars = win.findChildren<FindBar *>();
    CHECK(bars.size() >= 2, "the window has a find bar per pane");
    if (bars.isEmpty()) { return; }

    // The pane showing the rows to be found, and the bar that belongs to it.
    QTableView *view = nullptr;
    FindBar *bar = nullptr;
    for (QTableView *v : win.findChildren<QTableView *>()) {
        auto *lm = qobject_cast<LogModel *>(v->model());
        if (!lm || lm->count() < 3) { continue; }
        view = v;
        for (FindBar *b : bars) {
            if (b->parentWidget() && b->parentWidget()->isAncestorOf(v)) { bar = b; break; }
        }
        if (!bar) { bar = bars.first(); }
        break;
    }
    CHECK(view != nullptr,
          "a pane binds a LogModel directly — no proxy, which is the whole "
          "difference from a log tab");
    if (!view) { return; }

    bar->activate();
    QTest::qWait(20);

    // ---- the keystroke -------------------------------------------------------
    //
    // Reaching this line at all is most of the check: the reported failure is
    // a crash, and a crash takes the process with it rather than failing a
    // CHECK. What follows only runs if the scan survived.
    bar->setSearchText(QStringLiteral("STN_ID"));
    QTest::qWait(300);

    CHECK(bar->matchCount() == 2,
          "typing into a compare pane's find bar scans it and finds both rows");
    CHECK(bar->matchCursor() == 0, "and parks on the first");

    // ---- every mode, against the same unproxied model ------------------------
    //
    // The entry-shaped modes are the ones that used to reach for the proxy.
    for (const auto &m : { FindBar::Mode::Text, FindBar::Mode::Extended,
                           FindBar::Mode::Regex, FindBar::Mode::Hex,
                           FindBar::Mode::Query }) {
        bar->setMode(m);
        QTest::qWait(20);
        bar->setSearchText(QStringLiteral("STN_ID"));
        QTest::qWait(250);
    }
    CHECK(true, "no mode crashes on a directly bound LogModel");

    bar->setMode(FindBar::Mode::Hex);
    bar->setSearchText(QStringLiteral("0a 1b"));
    QTest::qWait(250);
    CHECK(bar->matchCount() == 3,
          "and Hex reads the datagram through a pane with no proxy");

    bar->setMode(FindBar::Mode::Query);
    bar->setSearchText(QStringLiteral("sev:info"));
    QTest::qWait(250);
    CHECK(bar->matchCount() == 3, "as does a query");

    // ---- new traffic reaches a compare pane's find ---------------------------
    //
    // The pane's view is built EMPTY and given a model when a source is
    // picked, so a bar wired once in its constructor would be listening to a
    // model the view never had. Find would sit at the count it first reported
    // while the log grew underneath it.
    bar->setMode(FindBar::Mode::Text);
    bar->setSearchText(QStringLiteral("STN_ID"));
    QTest::qWait(250);
    CHECK(bar->matchCount() == 2, "two matches before anything else arrives");

    m21->appendEntries({ mk(QStringLiteral("STN_ID 9 upcoming"), 4000) });
    QTest::qWait(400);
    CHECK(bar->matchCount() == 3,
          "a message arriving in the pane is picked up without retyping");

    // ---- switching the pane's source -----------------------------------------
    //
    // A row number means something different once the view is showing another
    // model, so the old match set must not be carried across.
    {
        QComboBox *picker = nullptr;
        for (QComboBox *c : win.findChildren<QComboBox *>()) {
            if (c->findData(QStringLiteral("33_1")) >= 0
                && c->currentData().toString() == QStringLiteral("21_1")) {
                picker = c; break;
            }
        }
        if (picker) {
            picker->setCurrentIndex(picker->findData(QStringLiteral("33_1")));
            QTest::qWait(400);
            CHECK(bar->matchCount() == 0,
                  "switching the pane to a source with no match empties the "
                  "match set rather than leaving rows of the old one in it");
        }
    }

    bar->close();
    Settings::setFindDetached(savedDetached);
    Settings::setFindMode(savedMode);
}
