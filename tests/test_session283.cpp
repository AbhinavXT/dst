#include "testutil.h"

#include "dialogfocus.h"

#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QThread>
#include <QWidget>

// =============================================================================
//  Session 283 — a closed dialog gives focus back to the window that opened
//  it. Abhinav: exporting a route, or cancelling the export, in the Tag
//  Builder brought the DLConsole main window to the front. Here a "main
//  window" and a tool window parented to it, as MainWindow opens the Tag
//  Builder; the platform's part (activating the main window as the dialog
//  goes) is played by the test.
// =============================================================================

namespace {
// Long enough for both of DialogFocus's tries.
void spin(int ms = 400)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
}
}  // namespace

TEST_SUITE(session283)
{
    DialogFocus::install();
    DialogFocus::install();   // a second call does nothing

    QWidget main;
    main.setWindowTitle(QStringLiteral("main"));
    main.resize(400, 300);
    auto *tool = new QWidget(&main, Qt::Window);
    tool->setWindowTitle(QStringLiteral("tool"));
    tool->resize(300, 200);
    main.show();
    tool->show();
    tool->activateWindow();
    spin(100);
    CHECK(QApplication::activeWindow() == tool, "the tool window is active to begin with");

    // A dialog parented to the tool window, as QFileDialog::getSaveFileName(this, ...).
    {
        QDialog dlg(tool);
        CHECK(DialogFocus::returnTarget(&dlg) == tool, "a dialog returns to its parent's window");
        dlg.setModal(true);
        dlg.show();
        spin(50);
        dlg.hide();            // OK or Cancel
        main.activateWindow(); // the platform hands activation to the main window
        spin();
        CHECK(QApplication::activeWindow() == tool, "closed: the tool window is active again, not the main window");
    }

    // A dialog of a child widget returns to the child's window.
    {
        auto *inner = new QWidget(tool);
        QDialog dlg(inner);
        CHECK(DialogFocus::returnTarget(&dlg) == tool, "a child widget's dialog returns to the tool window");
    }

    // Two dialogs in a row (Into Configuration1.xml: open, then save as):
    // nothing is activated while the second is up.
    {
        QDialog first(tool), second(tool);
        first.setModal(true);
        second.setModal(true);
        first.show();
        spin(50);
        first.hide();
        second.show();
        spin();
        CHECK(QApplication::activeModalWidget() == &second, "the second dialog is still the modal one");
        CHECK(QApplication::activeWindow() != tool, "the tool window is not activated over the second dialog");
        second.hide();
        main.activateWindow();
        spin();
        CHECK(QApplication::activeWindow() == tool, "after the second: the tool window is active again");
    }

    // The window closed meanwhile: nothing to give focus to.
    {
        QDialog dlg(tool);
        dlg.show();
        spin(50);
        tool->hide();
        dlg.hide();
        main.activateWindow();
        spin();
        CHECK(QApplication::activeWindow() == &main, "a hidden tool window is not shown again");
        tool->show();
    }

    // A parentless dialog returns to the window active when it opened.
    {
        tool->activateWindow();
        spin(100);
        QDialog dlg;
        CHECK(DialogFocus::returnTarget(&dlg) == tool, "a parentless dialog returns to the active window");
        dlg.show();
        spin(50);
        dlg.hide();
        main.activateWindow();
        spin();
        CHECK(QApplication::activeWindow() == tool, "parentless, closed: the tool window is active again");
    }
}
