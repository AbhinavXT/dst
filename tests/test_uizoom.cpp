#include "testutil.h"

#include "presentationmode.h"
#include "settings.h"
#include "textzoom.h"
#include "uistyle.h"

#include <QApplication>
#include <QFontDatabase>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QStatusBar>
#include <QTableView>
#include <QToolBar>

#include <cmath>

// =============================================================================
//  Text size (TextZoom) and presentation mode (F11).
// =============================================================================

namespace {

void settle()
{
    for (int i = 0; i < 5; ++i) {
        QApplication::processEvents();
    }
}

bool near(double a, double b)
{
    return std::fabs(a - b) < 0.05;
}

}  // namespace

TEST_SUITE(textzoom)
{
    TextZoom::initialise();
    const int savedPercent = TextZoom::percent();
    TextZoom::reset();
    const double appPt = qApp->font().pointSizeF();

    QWidget host;
    auto *title = new QLabel(QStringLiteral("Title"), &host);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(appPt * 1.5);                 // an explicit size, like a section title
    title->setFont(titleFont);
    auto *boldOnly = new QLabel(QStringLiteral("Bold"), &host);
    QFont bold;
    bold.setBold(true);                                   // only the weight set: size inherited
    boldOnly->setFont(bold);
    auto *table = new QTableView(&host);
    table->verticalHeader()->setDefaultSectionSize(30);

    TextZoom::setPercent(125);
    settle();   // widgets that inherit the font get it through Qt's queued font-change event
    CHECK(TextZoom::percent() == 125, "125 % is a step");
    CHECK(near(qApp->font().pointSizeF(), appPt * 1.25), "the application font is 1.25x");
    CHECK(near(title->font().pointSizeF(), appPt * 1.5 * 1.25), "a widget's own size keeps its proportion");
    CHECK(near(boldOnly->font().pointSizeF(), appPt * 1.25), "a font that only sets bold is not scaled twice");
    CHECK(table->verticalHeader()->defaultSectionSize() == 38, "fixed rows grow with the text (30 -> 38)");
    CHECK(Settings::rowHeightFor(1) == 38, "and tables made from now on get the same row height");
    const QFont systemMono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    CHECK(near(UiStyle::monoFont().pointSizeF(), systemMono.pointSizeF() * 1.25),
          "the monospace font scales for windows opened later");

    // Repeated zooming must not drift.
    for (int round = 0; round < 10; ++round) {
        TextZoom::setPercent(160);
        TextZoom::setPercent(90);
    }
    TextZoom::reset();
    CHECK(near(title->font().pointSizeF(), appPt * 1.5), "after 20 changes the title is back to exactly its size");
    CHECK(table->verticalHeader()->defaultSectionSize() == 30, "and the rows to exactly 30");

    // Something else resizes the rows (a density change) between zooms.
    TextZoom::setPercent(125);
    table->verticalHeader()->setDefaultSectionSize(55);   // density "comfortable" at 125 %: 44 x 1.25
    TextZoom::reset();
    CHECK(table->verticalHeader()->defaultSectionSize() == 44, "a size set in between is taken as the new base");

    // Steps: snapping and the ends.
    TextZoom::setPercent(118);
    CHECK(TextZoom::percent() == 125, "an in-between value snaps to the nearest step");
    for (int i = 0; i < 20; ++i) {
        TextZoom::zoomIn();
    }
    CHECK(TextZoom::percent() == TextZoom::steps().last(), "zooming in stops at the largest step");
    for (int i = 0; i < 20; ++i) {
        TextZoom::zoomOut();
    }
    CHECK(TextZoom::percent() == TextZoom::steps().first(), "zooming out stops at the smallest");
    CHECK(qApp->font().pointSizeF() < appPt, "80 % really is smaller (UiStyle's 9 pt floor no longer undoes it)");
    UiStyle::apply();
    CHECK(qApp->font().pointSizeF() < appPt, "even after a theme change re-applies the style");

    // The boost (presentation) is on top and is not saved.
    TextZoom::reset();
    TextZoom::setBoost(1);
    CHECK(TextZoom::effectivePercent() == 110 && TextZoom::percent() == 100, "the boost adds a step without changing the choice");
    {
        QSettings settings(Settings::iniPath(), QSettings::IniFormat);
        CHECK(settings.value(QStringLiteral("ui/textZoom")).toInt() == 100, "and is not saved");
    }
    TextZoom::setBoost(0);

    TextZoom::setPercent(savedPercent);
}

TEST_SUITE(presentationmode)
{
    TextZoom::initialise();
    const int savedPercent = TextZoom::percent();
    TextZoom::reset();

    QMainWindow window;
    QMenu *fileMenu = window.menuBar()->addMenu(QStringLiteral("File"));
    QAction *withShortcut = fileMenu->addAction(QStringLiteral("Do it"));
    withShortcut->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+J")));
    window.statusBar()->showMessage(QStringLiteral("ready"));
    QToolBar *toolbar = window.addToolBar(QStringLiteral("tools"));
    auto *field = new QLineEdit(&window);
    window.setCentralWidget(field);
    window.show();
    settle();

    PresentationMode *mode = PresentationMode::instance();
    mode->toggle(&window);
    settle();
    CHECK(window.windowState() & Qt::WindowFullScreen, "F11: full screen");
    CHECK(!window.menuBar()->isVisible() && !window.statusBar()->isVisible() && !toolbar->isVisible(),
          "menu bar, status bar and toolbar hidden");
    CHECK(window.actions().contains(withShortcut), "menu shortcuts are kept working on the window itself");
    CHECK(TextZoom::effectivePercent() == 110, "text one step larger while presenting");

    mode->toggle(&window);
    settle();
    CHECK(!(window.windowState() & Qt::WindowFullScreen), "F11 again: back from full screen");
    CHECK(window.menuBar()->isVisible() && window.statusBar()->isVisible() && toolbar->isVisible(),
          "every bar is back");
    CHECK(!window.actions().contains(withShortcut), "the borrowed shortcut is handed back");
    CHECK(TextZoom::effectivePercent() == 100, "text back to its size");

    // A maximised window comes back maximised.
    window.showMaximized();
    settle();
    mode->toggle(&window);
    settle();
    mode->toggle(&window);
    settle();
    CHECK(window.windowState() & Qt::WindowMaximized, "a maximised window comes back maximised");

    // Two windows: the boost lasts until the last one leaves.
    QMainWindow second;
    second.show();
    mode->enter(&window);
    mode->enter(&second);
    mode->leave(&window);
    CHECK(TextZoom::effectivePercent() == 110, "still presenting one window: still larger");
    mode->leave(&second);
    CHECK(TextZoom::effectivePercent() == 100 && mode->presentingCount() == 0, "none presenting: normal size");

    // A window destroyed while presenting does not leave the boost behind.
    {
        QMainWindow shortLived;
        shortLived.show();
        mode->enter(&shortLived);
        CHECK(TextZoom::effectivePercent() == 110, "presenting a short-lived window");
    }
    settle();
    CHECK(TextZoom::effectivePercent() == 100, "destroyed while presenting: the boost is gone");

    // Esc leaves presentation, except when typing in a field.
    window.showNormal();
    window.activateWindow();
    settle();
    CHECK(QApplication::activeWindow() == &window, "the test window is active (Esc goes to the active window)");
    mode->enter(&window);
    settle();
    field->setFocus();
    QKeyEvent escInField(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(field, &escInField);
    CHECK(mode->isPresenting(&window), "Esc in a text field is left to the field");
    window.menuBar()->setFocus();
    toolbar->setFocus();
    field->clearFocus();
    QKeyEvent escElsewhere(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&window, &escElsewhere);
    CHECK(!mode->isPresenting(&window), "Esc anywhere else leaves presentation");

    TextZoom::setPercent(savedPercent);
}
