#include "testutil.h"

#include "fieldcatalog.h"
#include "followpill.h"
#include "mainwindow.h"
#include "messagedispatcher.h"

#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QFile>
#include <QScrollBar>
#include <QTabWidget>
#include <QTableView>

// =============================================================================
//  Session 337 — "▼ N new rows": with Follow on, scrolling up pauses that
//  tab; arriving rows are counted on a button over its bottom edge, and the
//  button (or scrolling back to the end) resumes. Real run:
//  replay/loco_1_1_27062026_170217.cap, fed in two halves.
// =============================================================================

namespace {
int feed(MessageDispatcher &disp, int from, int count)
{
    QFile run(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_170217.cap"));
    if (!run.open(QIODevice::ReadOnly)) return 0;
    int n = 0, fed = 0;
    while (!run.atEnd() && fed < count) {
        const QByteArray l = run.readLine().trimmed();
        if (!l.startsWith('@')) continue;
        if (n++ < from) continue;
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3) continue;
        disp.ingestLocal(21, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(),
                         QString());
        ++fed;
    }
    disp.drainNow();
    QApplication::processEvents();
    return fed;
}

bool atEnd(QTableView *t)
{
    QScrollBar *b = t->verticalScrollBar();
    return b->value() >= b->maximum() - 2;
}
}  // namespace

TEST_SUITE(session337)
{
    const bool catalogWasLoaded = FieldCatalog::instance().isLoaded();
    [&]() {
        MainWindow w;
        w.resize(1200, 800);
        w.show();
        auto *disp = w.findChild<MessageDispatcher *>();
        CHECK(disp && feed(*disp, 0, 400) == 400, "fixture: 400 real rows into 21_1");
        if (!disp) return;
        auto *tabs = w.findChild<QTabWidget *>(QStringLiteral("tabWidget"));
        QTableView *view = tabs && tabs->currentWidget() ? tabs->currentWidget()->findChild<QTableView *>() : nullptr;
        auto *pill = view ? view->findChild<FollowPill *>(QStringLiteral("followPill")) : nullptr;
        auto *follow = w.findChild<QCheckBox *>(QStringLiteral("cbScrollLock"));
        CHECK(view && pill && follow, "the tab's table has a pill; the window has Follow");
        if (!view || !pill || !follow) return;
        CHECK(!pill->isVisible(), "hidden while nothing is waiting");

        follow->setChecked(true);
        QApplication::processEvents();
        feed(*disp, 400, 100);
        CHECK(atEnd(view) && !pill->isVisible(), "following at the end: new rows scroll in, no pill");

        // The operator scrolls up to read.
        view->verticalScrollBar()->setValue(0);
        QApplication::processEvents();
        CHECK(!pill->atTail(), "scrolled up: no longer at the tail");
        const int fed = feed(*disp, 500, 120);
        CHECK(view->verticalScrollBar()->value() == 0, "rows arrive: the view stays where it was read");
        CHECK(pill->isVisible() && pill->pending() == fed && pill->text() == QStringLiteral("▼ %1 new rows").arg(fed),
              QByteArray("the pill counts them: ") + pill->text().toUtf8());
        CHECK(follow->isChecked(), "Follow itself stays on: this is a pause");
        const QRect vp = view->viewport()->geometry();
        CHECK(vp.contains(pill->geometry().center()) && pill->geometry().bottom() <= vp.bottom(),
              "over the bottom of the rows");
        feed(*disp, 620, 30);
        CHECK(pill->pending() == fed + 30, "more rows: the count grows");

        pill->click();
        QApplication::processEvents();
        CHECK(atEnd(view) && !pill->isVisible() && pill->pending() == 0, "clicked: at the newest row, pill gone");
        feed(*disp, 650, 40);
        CHECK(atEnd(view) && !pill->isVisible(), "and following again");

        // Scrolling back down by hand resumes too.
        view->verticalScrollBar()->setValue(0);
        feed(*disp, 690, 20);
        CHECK(pill->isVisible(), "up again: paused");
        view->verticalScrollBar()->setValue(view->verticalScrollBar()->maximum());
        QApplication::processEvents();
        CHECK(!pill->isVisible(), "scrolled to the end by hand: resumed, pill gone");

        // Follow off: no pill, whatever the position.
        view->verticalScrollBar()->setValue(0);
        feed(*disp, 710, 10);
        follow->setChecked(false);
        QApplication::processEvents();
        CHECK(!pill->isVisible(), "Follow turned off: the pill goes");
        feed(*disp, 720, 10);
        CHECK(!pill->isVisible() && view->verticalScrollBar()->value() == 0, "and rows arriving add none");
    }();
    if (!catalogWasLoaded) FieldCatalog::instance().clear();
}
