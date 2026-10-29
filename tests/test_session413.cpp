#include "testutil.h"

#include "minimizeddock.h"

#include <QApplication>
#include <QMainWindow>
#include <QMenu>
#include <QStatusBar>
#include <QToolButton>

// =============================================================================
//  Session 413 — the minimised windows' chips side by side. Abhinav: "Sometimes
//  minimized windows superimpose on other minimized windows in the bottom of
//  dlconsole, they should be side by side." With more chips than the status
//  bar had room for, it squeezed them over one another. Now each keeps its
//  width; what does not fit in half the main window goes into "+N more".
// =============================================================================

namespace {

void settle413()
{
    for (int i = 0; i < 5; ++i) {
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

bool sideBySide(const QList<QWidget *> &chips)
{
    for (int i = 1; i < chips.size(); ++i)
        if (chips.at(i)->geometry().left() < chips.at(i - 1)->geometry().right()) return false;
    for (QWidget *c : chips)
        if (c->width() < c->sizeHint().width()) return false;   // not squeezed
    return true;
}

}  // namespace

TEST_SUITE(session413)
{
    QMainWindow main;
    main.resize(900, 500);
    auto *dock = new MinimizedDock(&main);
    main.statusBar()->addWidget(dock);
    main.statusBar()->addWidget(new QWidget(&main), 1);   // the notification bar beside it
    main.show();
    settle413();

    QList<QMainWindow *> tools;
    for (int i = 1; i <= 8; ++i) {
        auto *w = new QMainWindow(&main);
        w->setWindowFlag(Qt::Window);
        w->setWindowTitle(QStringLiteral("Decode Workbench · @dmi 14:02:%1").arg(10 + i));
        w->show();
        tools << w;
    }
    settle413();
    for (QMainWindow *w : tools) w->setWindowState(Qt::WindowMinimized);
    settle413();

    CHECK(dock->chipCount() == 8, "eight minimised: eight chips");
    const int shown = dock->shownChipCount();
    CHECK(shown >= 1 && shown < 8 && dock->overflowCount() == 8 - shown,
          qPrintable(QStringLiteral("900 px wide: %1 in the row, %2 behind \"+N more\"").arg(shown).arg(8 - shown)));
    CHECK(sideBySide(dock->shownChips()), "those in the row side by side, each at its own width");
    CHECK(dock->width() <= 450, qPrintable(QStringLiteral("the row within half the window (%1 px)").arg(dock->width())));
    QToolButton *more = dock->moreButton();
    CHECK(more && more->isVisible() && more->text() == QStringLiteral("+%1 more").arg(8 - shown)
              && more->menu()->actions().size() == 8 - shown,
          "\"+N more\", its menu one entry per window not in the row");
    CHECK(more && more->geometry().left() >= dock->shownChips().last()->geometry().right(), "after the chips, not over them");

    // From the menu: that window back, one chip fewer.
    const QString title = more ? more->menu()->actions().first()->text() : QString();
    if (more) more->menu()->actions().first()->trigger();
    settle413();
    bool back = false;
    for (QMainWindow *w : tools) back = back || (w->windowTitle() == title && w->isVisible());
    CHECK(back && dock->chipCount() == 7, qPrintable(QStringLiteral("picked from the menu: \"%1\" back, 7 chips").arg(title)));

    // Wider: more in the row.
    main.resize(2400, 500);
    settle413();
    CHECK(dock->shownChipCount() > shown - 1 && sideBySide(dock->shownChips()),
          qPrintable(QStringLiteral("2400 px wide: %1 in the row, still side by side").arg(dock->shownChipCount())));
    dock->setWidthBudget(100000);
    CHECK(dock->shownChipCount() == 7 && !dock->moreButton()->isVisibleTo(dock), "room for all: no \"+N more\"");
    dock->setWidthBudget(-1);

    for (QMainWindow *w : tools) w->close();
    settle413();
}
