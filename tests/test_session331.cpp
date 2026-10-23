#include "testutil.h"

#include "fieldcatalog.h"
#include "logmodel.h"
#include "mainwindow.h"
#include "messagedispatcher.h"
#include "navhistory.h"

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QSortFilterProxyModel>
#include <QTabWidget>
#include <QTableView>

// =============================================================================
//  Session 331 — Edit ▸ Back / Forward (Alt+Left / Alt+Right): back to where
//  the last jump left from. Real run: replay/loco_1_1_27062026_170217.cap,
//  fed as two sources so a jump can change tabs.
// =============================================================================

namespace {

void feed(MessageDispatcher &disp, int source, int maxLines)
{
    QFile run(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_170217.cap"));
    if (!run.open(QIODevice::ReadOnly)) return;
    int n = 0;
    while (!run.atEnd() && n < maxLines) {
        const QByteArray l = run.readLine().trimmed();
        if (!l.startsWith('@')) continue;
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3) continue;
        disp.ingestLocal(source, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch()
                                           + n, QString());
        ++n;
    }
    disp.drainNow();
}

QTableView *currentTable(MainWindow &w)
{
    auto *tabs = w.findChild<QTabWidget *>(QStringLiteral("tabWidget"));
    return tabs && tabs->currentWidget() ? tabs->currentWidget()->findChild<QTableView *>() : nullptr;
}

LogModel *modelOf(QTableView *t)
{
    QAbstractItemModel *m = t ? t->model() : nullptr;
    while (auto *p = qobject_cast<QAbstractProxyModel *>(m)) m = p->sourceModel();
    return qobject_cast<LogModel *>(m);
}

// The selected row's time in the current tab, -1 when none.
qint64 selectedMs(MainWindow &w)
{
    QTableView *t = currentTable(w);
    LogModel *m = modelOf(t);
    auto *proxy = t ? qobject_cast<QAbstractProxyModel *>(t->model()) : nullptr;
    if (!m || !proxy || !t->currentIndex().isValid()) return -1;
    const LogEntryPtr e = m->entryAt(proxy->mapToSource(t->currentIndex()).row());
    return e ? e->epochMs : -1;
}

void select(QTableView *t, LogModel *m, int sourceRow)
{
    auto *proxy = qobject_cast<QAbstractProxyModel *>(t->model());
    t->setCurrentIndex(proxy->mapFromSource(m->index(sourceRow, 0)));
}

void jump(MainWindow &w, const QString &key, qint64 ms)
{
    QMetaObject::invokeMethod(&w, "jumpToEntry", Q_ARG(QString, key), Q_ARG(qint64, ms));
    QApplication::processEvents();
}

}  // namespace

TEST_SUITE(session331)
{
    // ---- the history on its own ---------------------------------------------------
    {
        NavHistory h;
        const NavPoint a{ QStringLiteral("21_1"), 1000 }, b{ QStringLiteral("21_1"), 2000 },
                       c{ QStringLiteral("51_1"), 3000 };
        h.record(NavPoint());
        h.record(NavPoint{ QStringLiteral("21_1"), -1 });
        CHECK(!h.canBack(), "no row selected: nothing recorded");
        h.record(a); h.record(a);
        CHECK(h.back(b) == a && !h.canBack(), "the same place twice is one entry; Back goes to it");
        CHECK(h.canForward() && h.forward(a) == b, "Forward returns to where Back left");
        h.record(c);
        CHECK(!h.canForward(), "a new jump after Back drops Forward, like a browser");
        CHECK(h.back(a) == c, "Back from the place on top skips it rather than standing still");
        NavHistory big;
        for (int i = 0; i < NavHistory::kMax + 20; ++i) big.record(NavPoint{ QStringLiteral("21_1"), i });
        int n = 0;
        NavPoint here{ QStringLiteral("x"), 0 };
        while (big.canBack()) { here = big.back(here); ++n; }
        CHECK(n == NavHistory::kMax && here.epochMs == 20, "at most 100 places; the oldest dropped");
    }

    // ---- in the window ----------------------------------------------------------
    // MainWindow loads the field catalogue; put it back as found, or later
    // suites (assertions) decode under its renames.
    const bool catalogWasLoaded = FieldCatalog::instance().isLoaded();
    [&]() {
        MainWindow w;
        auto *disp = w.findChild<MessageDispatcher *>();
        CHECK(disp, "fixture: the window's dispatcher");
        if (!disp) return;
        feed(*disp, 21, 600);
        feed(*disp, 51, 600);
        QApplication::processEvents();

        auto *back = w.findChild<QAction *>(QStringLiteral("actNavBack"));
        auto *fwd = w.findChild<QAction *>(QStringLiteral("actNavForward"));
        CHECK(back && fwd && back->shortcut() == QKeySequence(QStringLiteral("Alt+Left"))
                  && fwd->shortcut() == QKeySequence(QStringLiteral("Alt+Right")),
              "Edit > Back (Alt+Left) and Forward (Alt+Right)");
        CHECK(back && !back->isEnabled() && fwd && !fwd->isEnabled(), "both greyed out before any jump");
        if (!back || !fwd) return;

        jump(w, QStringLiteral("21_1"), 0);
        QTableView *t21 = currentTable(w);
        LogModel *m21 = modelOf(t21);
        CHECK(m21 && m21->count() == 600, "fixture: 600 real rows in 21_1");
        if (!m21 || m21->count() != 600) return;
        LogModel *m51 = disp->modelForKey(QStringLiteral("51_1"));
        CHECK(m51 && m51->count() == 600, "fixture: and in 51_1");
        if (!m51) return;

        const qint64 at10 = m21->entryAt(10)->epochMs, at400 = m21->entryAt(400)->epochMs,
                     at51 = m51->entryAt(250)->epochMs;
        select(t21, m21, 10);
        jump(w, QStringLiteral("21_1"), at400);
        CHECK(selectedMs(w) == at400 && back->isEnabled() && !fwd->isEnabled(), "a jump lands, and Back lights up");

        jump(w, QStringLiteral("51_1"), at51);
        CHECK(selectedMs(w) == at51, "a jump to the other tab");

        back->trigger(); QApplication::processEvents();
        CHECK(selectedMs(w) == at400 && modelOf(currentTable(w)) == m21, "Back: the first tab again, at the row it left");
        CHECK(fwd->isEnabled(), "and Forward lights up");
        back->trigger(); QApplication::processEvents();
        CHECK(selectedMs(w) == at10, "Back again: the row selected before the first jump");
        CHECK(!back->isEnabled(), "nothing further back: greyed out");
        fwd->trigger(); QApplication::processEvents();
        fwd->trigger(); QApplication::processEvents();
        CHECK(selectedMs(w) == at51 && modelOf(currentTable(w)) == m51 && !fwd->isEnabled(),
              "Forward twice: back in the other tab, at its row");

        // Rows arriving after a jump do not move the place: it is a time, not a row.
        back->trigger(); QApplication::processEvents();
        feed(*disp, 21, 50);
        QApplication::processEvents();
        fwd->trigger(); QApplication::processEvents();
        back->trigger(); QApplication::processEvents();
        CHECK(selectedMs(w) == at400, "new rows in between: Back still lands on the same moment");
    }();
    if (!catalogWasLoaded) FieldCatalog::instance().clear();
}
