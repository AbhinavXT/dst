#include "testutil.h"

#include "minimizeddock.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QMainWindow>

// =============================================================================
//  MinimizedDock: named chips in the status bar for minimised tool windows.
//
//  Driven the way Windows drives it: setWindowState(Minimized) on a shown
//  tool window, then the event loop. What is checked is what the operator
//  sees: a chip with the window's name, the window out of the way, and one
//  click bringing it back as it was.
// =============================================================================

namespace {

void settle()
{
    for (int i = 0; i < 5; ++i) {
        QApplication::processEvents();
        // What the real event loop does for deleteLater(): processEvents()
        // alone does not run deferred deletes at the top level.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

// A window that refuses to close, like the Flasher mid-flash.
class StubbornWindow : public QMainWindow
{
public:
    explicit StubbornWindow(QWidget *parent) : QMainWindow(parent) { setWindowFlag(Qt::Window); }
protected:
    void closeEvent(QCloseEvent *event) override { event->ignore(); }
};

}  // namespace

TEST_SUITE(minimizeddock)
{
    QMainWindow main;
    auto *dock = new MinimizedDock(&main);
    main.show();

    auto *console = new QMainWindow(&main);
    console->setWindowFlag(Qt::Window);
    console->setWindowTitle(QStringLiteral("Live Loco Console"));
    CHECK(MinimizedDock::isToolWindow(console), "an owned window is a tool window");
    CHECK(!MinimizedDock::isToolWindow(&main), "the main window is not");

    console->show();
    settle();
    CHECK(dock->chipCount() == 0 && !dock->isVisible(), "no chips, nothing shown, until something is minimised");

    // ---- minimise: a named chip, the window out of the way ---------------------------
    console->setWindowState(Qt::WindowMinimized);
    settle();
    CHECK(dock->chipCount() == 1, "minimising adds a chip");
    CHECK(dock->chipTitles() == QStringList{ QStringLiteral("Live Loco Console") }, "named after the window");
    CHECK(!console->isVisible(), "and the window is hidden, so Windows draws no nameless stub");
    CHECK(dock->isVisible(), "the dock shows while it holds a chip");

    console->setWindowTitle(QStringLiteral("Live Loco Console — 7_1"));
    CHECK(dock->chipTitles().first() == QStringLiteral("Live Loco Console — 7_1"), "the chip follows a title change");

    // ---- restore -----------------------------------------------------------------------
    dock->restoreChip(0);
    settle();
    CHECK(console->isVisible() && !(console->windowState() & Qt::WindowMinimized),
          "clicking the chip brings the window back");
    CHECK(dock->chipCount() == 0 && !dock->isVisible(), "and the chip goes");

    // ---- a maximised window comes back maximised --------------------------------------
    console->setWindowState(Qt::WindowMaximized);
    settle();
    console->setWindowState(Qt::WindowMaximized | Qt::WindowMinimized);
    settle();
    CHECK(dock->chipCount() == 1, "a maximised window minimised gets a chip");
    dock->restoreChip(0);
    settle();
    CHECK(console->windowState() & Qt::WindowMaximized, "and comes back maximised");

    // ---- two windows, two chips, told apart -------------------------------------------
    auto *flasher = new QMainWindow(&main);
    flasher->setWindowFlag(Qt::Window);
    flasher->setWindowTitle(QStringLiteral("Firmware Flasher"));
    flasher->setAttribute(Qt::WA_DeleteOnClose);
    flasher->show();
    console->setWindowState(Qt::WindowMinimized);
    flasher->setWindowState(Qt::WindowMinimized);
    settle();
    CHECK(dock->chipTitles() == QStringList({ QStringLiteral("Live Loco Console — 7_1"), QStringLiteral("Firmware Flasher") }),
          "two minimised windows: two chips, each with its name");

    // ---- reopened another way (its menu item): the chip goes ---------------------------
    console->show();
    settle();
    CHECK(dock->chipTitles() == QStringList{ QStringLiteral("Firmware Flasher") },
          "a window shown again by other means loses its chip");

    // ---- ✕ on a chip closes the window -------------------------------------------------
    QPointer<QMainWindow> flasherGuard(flasher);
    dock->closeChip(0);
    settle();
    CHECK(flasherGuard.isNull(), "✕ closes the window (this one deletes itself)");
    CHECK(dock->chipCount() == 0, "and its chip goes with it");

    // A window that refuses to close keeps its chip.
    auto *stubborn = new StubbornWindow(&main);
    stubborn->setWindowTitle(QStringLiteral("Flashing"));
    stubborn->show();
    stubborn->setWindowState(Qt::WindowMinimized);
    settle();
    dock->closeChip(0);
    settle();
    CHECK(dock->chipCount() == 1, "a window that refuses to close keeps its chip");
    dock->restoreChip(0);
    settle();

    // ---- left alone: dialogs and the main window --------------------------------------
    QDialog dialog(&main);
    dialog.show();
    dialog.setWindowState(Qt::WindowMinimized);
    main.setWindowState(Qt::WindowMinimized);
    settle();
    CHECK(dock->chipCount() == 0, "dialogs and the main window are not docked");
    main.setWindowState(Qt::WindowNoState);
    dialog.close();
    delete stubborn;
    settle();
}
