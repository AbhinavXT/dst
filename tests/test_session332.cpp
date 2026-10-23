#include "testutil.h"

#include "findbar.h"
#include "logentry.h"
#include "logmodel.h"
#include "queryhistory.h"
#include "querylineedit.h"

#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QMenu>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QToolButton>
#include <QtTest/QTest>

// =============================================================================
//  Session 332 — Find history: ▾ beside the Find box lists the last 20
//  searches, any mode, kept across restarts. Real rows:
//  replay/loco_1_1_27062026_170217.cap.
// =============================================================================

namespace {

QVector<LogEntryPtr> realRows(int max)
{
    QVector<LogEntryPtr> out;
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_170217.cap"));
    if (!f.open(QIODevice::ReadOnly)) return out;
    while (!f.atEnd() && out.size() < max) {
        const QString l = QString::fromLatin1(f.readLine()).trimmed();
        if (!l.startsWith(QLatin1Char('@'))) continue;
        auto e = QSharedPointer<LogEntry>::create();
        e->header.source_id = 21;
        e->header.kvchId = 1;
        e->epochMs = QDateTime::fromString(l.section(QLatin1Char(' '), 1, 1), Qt::ISODate).toMSecsSinceEpoch();
        e->severity = Severity::Info;
        e->text = l;
        e->cacheDerived();
        out << e;
    }
    return out;
}

void settle()
{
    QTest::qWait(260);
    QCoreApplication::processEvents();
}

QStringList menuTexts(QToolButton *b)
{
    emit b->menu()->aboutToShow();
    QStringList out;
    for (QAction *a : b->menu()->actions()) if (!a->isSeparator()) out << a->text();
    return out;
}

}  // namespace

TEST_SUITE(session332)
{
    QueryHistory::clearRecentFinds();

    // ---- the store --------------------------------------------------------------
    QueryHistory::rememberFind(QStringLiteral("  "), 0);
    CHECK(QueryHistory::recentFinds().isEmpty(), "a blank search is not remembered");
    QueryHistory::rememberFind(QStringLiteral("@arp"), 0);
    QueryHistory::rememberFind(QStringLiteral("@arp"), 0);
    QueryHistory::rememberFind(QStringLiteral("02 07 0D"), 3);
    QueryHistory::rememberFind(QStringLiteral("@arp"), 0);
    auto recent = QueryHistory::recentFinds();
    CHECK(recent.size() == 2 && recent.at(0).text == QStringLiteral("@arp") && recent.at(0).mode == 0
              && recent.at(1).text == QStringLiteral("02 07 0D") && recent.at(1).mode == 3,
          "newest first, once each, the mode kept with the text");
    QueryHistory::rememberFind(QStringLiteral("@arp"), 2);
    CHECK(QueryHistory::recentFinds().size() == 3, "the same text in another mode is another search");
    for (int i = 0; i < 30; ++i) QueryHistory::rememberFind(QStringLiteral("s%1").arg(i), 0);
    recent = QueryHistory::recentFinds();
    CHECK(recent.size() == QueryHistory::kMaxFinds && recent.first().text == QStringLiteral("s29"),
          "at most 20, the oldest dropped");
    QueryHistory::clearRecentFinds();

    // ---- the bar ----------------------------------------------------------------
    const QVector<LogEntryPtr> rows = realRows(400);
    CHECK(rows.size() == 400, "fixture: 400 real capture lines");
    LogModel model(nullptr, 100000);
    model.appendEntries(rows);
    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    QTableView view;
    view.setModel(&proxy);
    view.show();
    FindBar bar(&view);
    bar.activate();

    auto *edit = bar.findChild<QueryLineEdit *>(QStringLiteral("findEdit"));
    auto *hist = bar.findChild<QToolButton *>(QStringLiteral("findHistoryBtn"));
    auto *next = bar.findChild<QPushButton *>(QStringLiteral("findNextBtn"));
    CHECK(edit && hist && next && hist->isVisible(), "the bar has ▾ beside its input");
    if (!edit || !hist || !next) return;
    CHECK(menuTexts(hist) == QStringList{ QStringLiteral("(no searches yet)") }, "empty: it says so");

    edit->setText(QStringLiteral("@ar"));
    settle();
    edit->setText(QStringLiteral("@arp"));
    settle();
    CHECK(QueryHistory::recentFinds().isEmpty(), "typing alone remembers nothing (no prefixes)");
    next->click();
    CHECK(QueryHistory::recentFinds().size() == 1, "Next remembers the search");

    bar.setMode(FindBar::Mode::Regex);
    edit->setText(QStringLiteral("@lsrp_\\d+_\\d+"));
    settle();
    const int reHits = bar.matchRows().size();
    CHECK(reHits > 0, "fixture: the pattern matches the run's LSRP lines");
    emit edit->returnPressed();
    QStringList items = menuTexts(hist);
    CHECK(items.size() == 3 && items.at(0).startsWith(QStringLiteral("@lsrp_"))
              && items.at(0).endsWith(QStringLiteral("Regular expression")) && items.at(1).startsWith(QStringLiteral("@arp"))
              && items.at(2) == QStringLiteral("Clear recent searches"),
          "▾ lists both, newest first, each with its mode");

    // Picking one restores its mode and runs it.
    bar.setMode(FindBar::Mode::Hex);
    edit->setText(QStringLiteral("ff ee"));
    settle();
    QAction *pickRe = hist->menu()->actions().at(0);
    pickRe->trigger();
    CHECK(bar.mode() == FindBar::Mode::Regex && edit->text() == QStringLiteral("@lsrp_\\d+_\\d+")
              && bar.matchRows().size() == reHits,
          "picking the regex search: Regex mode again, its text, its matches at once");

    // Across a "restart": a new bar reads the same list.
    {
        FindBar again(&view);
        auto *h2 = again.findChild<QToolButton *>(QStringLiteral("findHistoryBtn"));
        CHECK(h2 && menuTexts(h2).size() == 3, "a new bar (another tab, a restart) offers the same searches");
    }

    QAction *clear = nullptr;
    for (QAction *a : hist->menu()->actions()) if (a->text() == QStringLiteral("Clear recent searches")) clear = a;
    if (clear) clear->trigger();
    CHECK(QueryHistory::recentFinds().isEmpty(), "Clear recent searches empties it");
}
