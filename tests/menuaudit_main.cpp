// Opens the real MainWindow and exercises the View menu the way a user
// would: this is the check that would have caught the empty Panels submenu
// and the orphaned UTC action.
#include "mainwindow.h"
#include "settings.h"
#include <QApplication>
#include <QShortcut>
#include <QAbstractButton>
#include <QSettings>

#include "tablefindbar.h"
#include "theme.h"
#include "tabpopoutwindow.h"
#include "tabtags.h"
#include "minimizeddock.h"
#include "statuspins.h"
#include <QDockWidget>
#include <QMenuBar>
#include <QTabWidget>
#include <QHash>
#include <functional>
#include <QMenu>
#include <QTimer>
#include <cstdio>
#include <QAbstractProxyModel>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStatusBar>
#include <QTableView>
#include <QTemporaryDir>
#include <QToolButton>
#include "logmodel.h"
#include "uicolors.h"
#include "workspacesnapshot.h"
#include "clockskewalarm.h"
#include "findbar.h"
#include <QCheckBox>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QTableWidget>
#include "findbar.h"
#include <QCheckBox>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>

static QMenu* findMenu(QMenuBar *bar, const QString &title) {
    for (QAction *a : bar->actions())
        if (a->menu() && a->text().remove('&').startsWith(title)) return a->menu();
    return nullptr;
}
static QAction* findAction(QMenu *m, const QString &title) {
    for (QAction *a : m->actions())
        if (a->text().remove('&').startsWith(title)) return a;
    return nullptr;
}

int fails = 0;
#define CHECK(c,m) do{ if(!(c)){ printf("FAIL: %s\n",m); ++fails;} else printf("ok  : %s\n",m);}while(0)

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    // Seed a workspace so construction takes the restore path. Saved and put
    // back afterwards: this audit borrows the real ini.
    const QStringList savedTabs   = Settings::workspaceTabs();
    const QString     savedActive = Settings::workspaceActiveTab();
    const QChar sep(0x1F);
    Settings::setWorkspaceTabs({ QStringList{ "21_1", "L1_V1", "1" }.join(sep),
                                 QStringList{ "81_1", "81_1",  "1" }.join(sep) });
    Settings::setWorkspaceActiveTab(QStringLiteral("21_1"));

    // A pop-out that was open when the console last closed: it must come
    // back by itself once its tab exists (here: restored from the workspace).
    QSettings borrowedIni(Settings::iniPath(), QSettings::IniFormat);
    const QVariant savedPopouts = borrowedIni.value(QStringLiteral("ui/popouts"));
    borrowedIni.setValue(QStringLiteral("ui/popouts"), QStringList{ QStringLiteral("21_1") });
    borrowedIni.sync();
    const TabTag savedTag = TabTags::instance()->tag(QStringLiteral("21_1"));
    TabTags::instance()->setTag(QStringLiteral("21_1"), TabTag());

    MainWindow w;
    w.show();
    for (int i = 0; i < 10; ++i) { QApplication::processEvents(); }

    // Session 86: the console starts on Ethernet alone. Nothing serial is
    // created, let alone opened, until the operator asks for a terminal.
    {
        int serialObjects = 0;
        for (QObject *o : w.findChildren<QObject *>())
            if (o->inherits("QSerialPort") || o->inherits("SerialLink") || o->inherits("SerialConsoleWindow")) ++serialObjects;
        for (QWidget *t : QApplication::topLevelWidgets())
            if (t->inherits("SerialConsoleWindow")) ++serialObjects;
        CHECK(serialObjects == 0, "the console starts with no serial port, terminal or link");
    }

    QMenuBar *bar = w.menuBar();
    QMenu *view = findMenu(bar, "View");
    CHECK(view, "View menu exists");
    if (!view) return 1;

    QAction *utc = findAction(view, "Show times in UTC");
    CHECK(utc, "View > Show times in UTC is present");
    CHECK(utc && utc->isCheckable(), "and is checkable");

    QAction *panelsAct = findAction(view, "Panels");
    CHECK(panelsAct && panelsAct->menu(), "View > Panels is a submenu");

    QMenu *panels = panelsAct ? panelsAct->menu() : nullptr;
    if (panels) {
        emit panels->aboutToShow();      // what opening it does
        int toggles = 0;
        QStringList names;
        for (QAction *a : panels->actions()) {
            if (a->isSeparator()) continue;
            names << a->text().remove('&');
            if (a->isCheckable()) ++toggles;
        }
        printf("      panels: %s\n", qPrintable(names.join(" | ")));
        CHECK(toggles >= 5, "Panels lists every dock as a toggle (was empty)");
        CHECK(names.contains("Raw bytes"), "Raw bytes present");
        CHECK(names.contains("Decoded fields"), "Decoded fields present");
        CHECK(names.filter("Reset").size() == 1, "Reset panel layout present");
    }

    QAction *colsAct = findAction(view, "Columns");
    QMenu *cols = colsAct ? colsAct->menu() : nullptr;
    CHECK(cols, "View > Columns is a submenu");
    if (cols) {
        int n = 0;
        for (QAction *a : cols->actions()) if (a->isCheckable()) ++n;
        CHECK(n == 5, "five column toggles");
        // Toggling must change state and NOT be a one-shot.
        QAction *src = findAction(cols, "Source");
        if (src) {
            const bool before = src->isChecked();
            src->trigger();
            CHECK(src->isChecked() != before, "toggling a column flips it");
            src->trigger();
            CHECK(src->isChecked() == before, "and toggles back");
        }
    }

    // ---- the reorganised menus -------------------------------------------
    // Tools is six groups now, not seventeen entries. Each action still has
    // to be reachable, and it has to be in the group its name implies —
    // "Transmit" is only worth having if everything that sends is inside it.
    // ---- a saved workspace must not take the window down -------------------
    //
    // restoreWorkspace() was called thirty lines too early in the
    // constructor — before the dispatcher it asks for models — and every
    // start after a session with tabs open segfaulted. It survived testing
    // because a console that has never seen traffic saves an EMPTY
    // workspace and restores nothing, so the crash needed a real session to
    // happen first.
    //
    // This audit builds a real MainWindow, so seeding a workspace before it
    // is constructed is enough to reproduce that. Reaching this line at all
    // means the window came up.
    CHECK(!Settings::workspaceTabs().isEmpty(),
          "the window was constructed with a saved workspace in place");

    // Not just "it did not crash": the tabs have to actually come back. With
    // the call in the wrong place the guard fires and the window comes up
    // with nothing restored, which looks fine until you notice your tabs are
    // gone — so the count is what is checked.
    {
        auto *tabs = w.findChild<QTabWidget *>(QStringLiteral("tabWidget"));
        CHECK(tabs, "the tab widget is there");
        CHECK(tabs && tabs->count() == 2,
              "both saved tabs were restored");
    }

    QMenu *tools = findMenu(bar, "Tools");
    CHECK(tools, "Tools menu exists");

    auto sub = [](QMenu *m, const char *name) -> QMenu * {
        QAction *a = m ? findAction(m, QString::fromLatin1(name)) : nullptr;
        return a ? a->menu() : nullptr;
    };

    QMenu *monitor  = sub(tools, "Monitor");
    QMenu *inspect  = sub(tools, "Inspect");
    QMenu *transmit = sub(tools, "Transmit");
    QMenu *schema   = sub(tools, "Schema");
    QMenu *testcase = sub(tools, "Test cases");
    CHECK(monitor,  "Tools > Monitor");
    CHECK(inspect,  "Tools > Inspect");
    CHECK(transmit, "Tools > Transmit");
    CHECK(schema,   "Tools > Schema");
    CHECK(testcase, "Tools > Test cases");

    CHECK(monitor && findAction(monitor, "Live Loco Console"), "Monitor > Live Loco Console");

    // View > Theme: every theme, exactly one ticked, and the toggle.
    {
        QMenu *themeMenu = nullptr;
        if (view) {
            for (QAction *action : view->actions()) {
                if (action->menu() && action->text().remove(QLatin1Char('&')) == QStringLiteral("Theme")) {
                    themeMenu = action->menu();
                }
            }
        }
        CHECK(themeMenu != nullptr, "View > Theme exists");
        int themes = 0;
        int ticked = 0;
        bool hasToggle = false;
        if (themeMenu) {
            for (QAction *action : themeMenu->actions()) {
                // Themes are the exclusive group; the colour-blind switch
                // (session 79) is checkable too but is not a theme.
                if (action->isCheckable() && action->actionGroup()) {
                    ++themes;
                    if (action->isChecked()) {
                        ++ticked;
                    }
                }
                if (action->text().remove(QLatin1Char('&')).contains(QStringLiteral("Toggle Dark/Light"))) {
                    hasToggle = true;
                }
            }
        }
        CHECK(themes == ThemeUtil::all().size(), "View > Theme lists every theme");
        CHECK(ticked == 1, "exactly one theme is ticked");
        CHECK(hasToggle, "and Toggle Dark/Light is there");
    }

    // The Live Loco Console has Find: a TableFindBar under its tabs and
    // Ctrl+F bound in the window.
    {
        QAction *console = monitor ? findAction(monitor, "Live Loco Console") : nullptr;
        if (console) {
            console->trigger();
            QWidget *window = nullptr;
            for (QWidget *top : QApplication::topLevelWidgets()) {
                if (top->windowTitle() == QStringLiteral("Live Loco Console") && top->isVisible()) {
                    window = top;
                }
            }
            CHECK(window != nullptr, "Live Loco Console opens");
            bool hasBar = false;
            bool hasCtrlF = false;
            if (window) {
                hasBar = window->findChild<TableFindBar *>() != nullptr;
                for (QShortcut *shortcut : window->findChildren<QShortcut *>()) {
                    if (shortcut->key() == QKeySequence(QKeySequence::Find)) {
                        hasCtrlF = true;
                    }
                }
                window->close();
            }
            CHECK(hasBar, "the Live Loco Console has a find bar");
            CHECK(hasCtrlF, "and Ctrl+F opens it");
        }
    }
    CHECK(monitor && findAction(monitor, "Active Fault Panel"), "Monitor > Active Fault Panel");
    CHECK(monitor && findAction(monitor, "Plot field"), "Monitor > Plot field over time");
    CHECK(monitor && findAction(monitor, "Compare tabs"), "Monitor > Compare tabs");

    CHECK(inspect && findAction(inspect, "Decode Workbench"), "Inspect > Decode Workbench");
    CHECK(inspect && findAction(inspect, "Selected row"), "Inspect > Selected row -> Workbench");
    CHECK(inspect && findAction(inspect, "Frame Diff"), "Inspect > Frame Diff");
    CHECK(inspect && findAction(inspect, "Packet Sequence"), "Inspect > Packet Sequence");

    CHECK(transmit && findAction(transmit, "Packet Maker"), "Transmit > Packet Maker");
    CHECK(transmit && findAction(transmit, "Field Sweep"), "Transmit > Field Sweep");
    CHECK(transmit && findAction(transmit, "Selected row"), "Transmit > Selected row -> Packet Maker");
    // The point of the group: nothing that sends may sit outside it.
    CHECK(!findAction(tools, "Packet Maker") && !findAction(tools, "Field Sweep"),
          "no transmitting tool is left loose in Tools");

    CHECK(schema && findAction(schema, "Reload schema"), "Schema > Reload schema");
    CHECK(schema && findAction(schema, "Use external schema"), "Schema > Use external schema file");
    CHECK(schema && findAction(schema, "Revert to built-in"), "Schema > Revert to built-in");
    CHECK(schema && findAction(schema, "Round-trip Validator"), "Schema > Round-trip Validator");

    CHECK(testcase && findAction(testcase, "Load test cases"), "Test cases > Load");
    CHECK(testcase && findAction(testcase, "Save observation report"), "Test cases > Save report");
    CHECK(testcase && findAction(testcase, "Reset run"), "Test cases > Reset run");

    CHECK(tools && findAction(tools, "Session Key"), "Tools > Session Key stays top level");

    // The Firmware Flasher: top level in Tools as the handoff asked, and on a
    // shortcut of its own. Ctrl+Shift+F (the handoff's suggestion) belongs to
    // Monitor > Active Fault Panel; the uniqueness walk below would catch a
    // clash, this pins the intended binding.
    {
        QAction *flasher = tools ? findAction(tools, "Firmware Flasher") : nullptr;
        CHECK(flasher, "Tools > Firmware Flasher is top level");
        CHECK(flasher && flasher->shortcut() == QKeySequence("Ctrl+Alt+F"),
              "Firmware Flasher is on Ctrl+Alt+F");
        CHECK(!transmit || !findAction(transmit, "Firmware Flasher"),
              "and is not duplicated inside Transmit");
        QAction *fault = monitor ? findAction(monitor, "Active Fault Panel") : nullptr;
        CHECK(fault && fault->shortcut() == QKeySequence("Ctrl+Shift+F"),
              "Active Fault Panel keeps Ctrl+Shift+F");
        if (flasher) {
            flasher->trigger();
            QWidget *opened = nullptr;
            for (QWidget *top : QApplication::topLevelWidgets()) {
                if (top->windowTitle() == QStringLiteral("Firmware Flasher") && top->isVisible()) {
                    opened = top;
                }
            }
            CHECK(opened != nullptr, "triggering it opens the Firmware Flasher window");
            flasher->trigger();
            int count = 0;
            for (QWidget *top : QApplication::topLevelWidgets()) {
                if (top->windowTitle() == QStringLiteral("Firmware Flasher")) {
                    ++count;
                }
            }
            CHECK(count == 1, "and triggering it again raises the same window, not a second one");
            if (opened) {
                opened->close();
            }
        }
    }

    // Loco Configuration: top level in Tools beside the flasher, its own
    // shortcut, and one window however often it is triggered.
    {
        QAction *loco = tools ? findAction(tools, "Loco Configuration") : nullptr;
        CHECK(loco, "Tools > Loco Configuration is top level");
        CHECK(loco && loco->shortcut() == QKeySequence("Ctrl+Alt+L"),
              "Loco Configuration is on Ctrl+Alt+L");
        CHECK(!transmit || !findAction(transmit, "Loco Configuration"),
              "and is not duplicated inside Transmit");
        if (loco) {
            loco->trigger();
            loco->trigger();
            int count = 0;
            QWidget *opened = nullptr;
            for (QWidget *top : QApplication::topLevelWidgets()) {
                if (top->windowTitle() == QStringLiteral("Loco Configuration")) {
                    ++count;
                    if (top->isVisible()) {
                        opened = top;
                    }
                }
            }
            CHECK(opened != nullptr, "triggering it opens the Loco Configuration window");
            CHECK(count == 1, "and triggering it again raises the same window");
            if (opened) {
                opened->close();
            }
        }
    }

    QMenu *file = findMenu(bar, "File");
    CHECK(file && findAction(file, "Open recorded session"), "File > Open recorded session");
    CHECK(file && findAction(file, "Stream session"),
          "File > Stream session as live (moved out of Tools)");

    QMenu *edit = findMenu(bar, "Edit");
    CHECK(edit && findAction(edit, "Search recorded sessions"),
          "Edit > Search recorded sessions");
    CHECK(edit && findAction(edit, "Copy selected rows"), "Edit > Copy selected rows");
    QMenu *bookmarks = sub(edit, "Bookmarks");
    CHECK(bookmarks, "Edit > Bookmarks is a submenu");
    CHECK(bookmarks && findAction(bookmarks, "Toggle bookmark"), "Bookmarks > Toggle");
    CHECK(bookmarks && findAction(bookmarks, "Next bookmark"), "Bookmarks > Next");
    CHECK(bookmarks && findAction(bookmarks, "Previous bookmark"), "Bookmarks > Previous");

    // ---- shortcuts are unique --------------------------------------------
    // Ctrl+Shift+R was bound to both "Reload schema" and "Stream session as
    // live". Qt answers an ambiguous shortcut by firing NEITHER and warning
    // on stderr, so both were unreachable from the keyboard and nothing
    // said so. This walks every action in the bar and fails on a repeat.
    {
        QHash<QString, QString> seen;      // shortcut -> first path that claimed it
        QStringList clashes;
        std::function<void(QMenu *, const QString &)> walk =
            [&](QMenu *m, const QString &prefix) {
                if (!m) return;
                emit m->aboutToShow();     // lazily-built submenus (Panels)
                for (QAction *a : m->actions()) {
                    if (a->isSeparator()) continue;
                    const QString label = a->text().remove('&');
                    const QString path  = prefix.isEmpty() ? label
                                                           : prefix + " > " + label;
                    if (a->menu()) { walk(a->menu(), path); continue; }
                    const QString sc = a->shortcut().toString();
                    if (sc.isEmpty()) continue;
                    if (seen.contains(sc))
                        clashes << QString("%1 on both '%2' and '%3'")
                                       .arg(sc, seen.value(sc), path);
                    else
                        seen.insert(sc, path);
                }
            };
        for (QAction *top : bar->actions())
            if (top->menu()) walk(top->menu(), top->text().remove('&'));

        for (const QString &c : clashes) printf("      clash: %s\n", qPrintable(c));
        CHECK(clashes.isEmpty(), "no shortcut is claimed by two actions");

        // ---- the pinned-fields panel ------------------------------------
        //
        // Checked here rather than in dltests because MainWindow is not in
        // that binary, and every one of these is a property of how the window
        // sets the dock up.
        {
            QDockWidget *pin = w.findChild<QDockWidget *>(
                QStringLiteral("PinnedFieldsDock"));
            CHECK(pin != nullptr, "the pinned-fields panel exists");
            if (pin) {
                CHECK(pin->isFloating(),
                      "and floats by default — a value watched while a run is "
                      "driven from the DMI must not be folded into the side of "
                      "the window whose log is being scrolled");
                CHECK(!pin->isVisible(),
                      "but is not shown unasked: an empty one is not worth a "
                      "window");
                CHECK(pin->features() & QDockWidget::DockWidgetMovable,
                      "it can still be docked, so floating is a default and "
                      "not a decision made for the operator");
                CHECK(!pin->windowTitle().isEmpty(),
                      "and carries a title, which a floating window needs and "
                      "a docked one can do without");
            }
        }

        // ---- the watches panel ------------------------------------------
        {
            QDockWidget *watch = w.findChild<QDockWidget *>(
                QStringLiteral("WatchesDock"));
            CHECK(watch != nullptr, "the watches panel exists");
            if (watch) {
                CHECK(watch->isFloating(),
                      "and floats — a watch is read while the operator is at "
                      "the DMI, not while reading the log");
                CHECK(!watch->isVisible(),
                      "and is not shown unasked; it is raised when something "
                      "fires, which is the only time it demands attention");
            }
        }
        printf("      %d distinct shortcuts bound\n", seen.size());
    }

    // ---- everything is still one flat search away -------------------------
    // Grouping only costs nothing if the palette still finds it, so this
    // counts what harvestCommands() would reach.
    {
        int leaves = 0;
        std::function<void(QMenu *)> count = [&](QMenu *m) {
            if (!m) return;
            emit m->aboutToShow();
            for (QAction *a : m->actions()) {
                if (a->isSeparator() || a->text().isEmpty()) continue;
                if (a->menu()) count(a->menu()); else ++leaves;
            }
        };
        for (QAction *top : bar->actions()) if (top->menu()) count(top->menu());
        printf("      %d commands reachable from the palette\n", leaves);
        CHECK(leaves >= 40, "the palette still reaches every command");
    }

    // ---- pop-out windows, colour tags, text size, presentation ------------------
    {
        auto popoutsOpen = [&w]() {
            QList<TabPopoutWindow *> open;
            for (TabPopoutWindow *window : w.findChildren<TabPopoutWindow *>()) {
                if (window->isVisible()) {
                    open.append(window);
                }
            }
            return open;
        };
        auto savedPopoutKeys = []() {
            QSettings ini(Settings::iniPath(), QSettings::IniFormat);
            return ini.value(QStringLiteral("ui/popouts")).toStringList();
        };

        QList<TabPopoutWindow *> open = popoutsOpen();
        CHECK(open.size() == 1 && open.first()->key() == QStringLiteral("21_1"),
              "a pop-out open at the last shutdown comes back on start");

        // Tag the tab: dot and label on the tab, and on its pop-out.
        auto *tabs = w.findChild<QTabWidget *>(QStringLiteral("tabWidget"));
        TabTags::instance()->setColor(QStringLiteral("21_1"), 1);
        TabTags::instance()->setLabel(QStringLiteral("21_1"), QStringLiteral("Brake test"));
        int tabIndex = -1;
        for (int i = 0; tabs && i < tabs->count(); ++i) {
            if (tabs->tabText(i).startsWith(QStringLiteral("L1_V1"))) {
                tabIndex = i;
            }
        }
        CHECK(tabIndex >= 0 && !tabs->tabIcon(tabIndex).isNull(), "a colour tag puts a dot on the tab");
        CHECK(tabIndex >= 0 && tabs->tabText(tabIndex) == QStringLiteral("L1_V1 · Brake test"),
              "and its label after the tab's name");
        CHECK(!open.isEmpty() && open.first()->windowTitle() == QStringLiteral("L1_V1 · Brake test"),
              "the pop-out carries the tab's own name and label");
        CHECK(!open.isEmpty() && open.first()->windowTitle().contains(QStringLiteral("Brake test"))
                  && !open.first()->windowIcon().isNull(),
              "the tag follows the loco into its pop-out window");

        // Closed by the operator: not reopened next time.
        if (!open.isEmpty()) {
            open.first()->close();
            for (int i = 0; i < 5; ++i) { QApplication::processEvents(); }
        }
        CHECK(!savedPopoutKeys().contains(QStringLiteral("21_1")), "a pop-out the operator closed is forgotten");

        // The Detach button pops the current tab out (once: again just raises it).
        if (tabs && tabIndex >= 0) {
            tabs->setCurrentIndex(tabIndex);
        }
        QAbstractButton *detach = w.findChild<QAbstractButton *>(QStringLiteral("pbDetach"));
        CHECK(detach != nullptr, "the Detach button is there");
        if (detach) {
            detach->click();
            detach->click();
            for (int i = 0; i < 5; ++i) { QApplication::processEvents(); }
        }
        CHECK(popoutsOpen().size() == 1, "Detach pops the tab out, and a second press does not open a second copy");
        CHECK(savedPopoutKeys().contains(QStringLiteral("21_1")), "an open pop-out is remembered for the next start");
        for (TabPopoutWindow *window : popoutsOpen()) {
            CHECK(MinimizedDock::isToolWindow(window), "the pop-out is a tool window (minimise chips)");
            window->close();
        }
        for (int i = 0; i < 5; ++i) { QApplication::processEvents(); }

        // View > Text size and View > Full screen / presentation.
        QMenu *zoomMenu = nullptr;
        QAction *present = nullptr;
        if (view) {
            for (QAction *action : view->actions()) {
                const QString text = action->text().remove(QLatin1Char('&'));
                if (action->menu() && text == QStringLiteral("Text size")) {
                    zoomMenu = action->menu();
                }
                if (text.startsWith(QStringLiteral("Full screen"))) {
                    present = action;
                }
            }
        }
        CHECK(zoomMenu != nullptr, "View > Text size exists");
        bool larger = false;
        bool smaller = false;
        bool resetZoom = false;
        bool appWide = true;
        if (zoomMenu) {
            for (QAction *action : zoomMenu->actions()) {
                if (action->shortcuts().contains(QKeySequence(QStringLiteral("Ctrl++")))
                    && action->shortcuts().contains(QKeySequence(QStringLiteral("Ctrl+=")))) {
                    larger = true;
                }
                if (action->shortcut() == QKeySequence(QStringLiteral("Ctrl+-"))) {
                    smaller = true;
                }
                if (action->shortcut() == QKeySequence(QStringLiteral("Ctrl+0"))) {
                    resetZoom = true;
                }
                if (!action->shortcut().isEmpty() && action->shortcutContext() != Qt::ApplicationShortcut) {
                    appWide = false;
                }
            }
        }
        CHECK(larger && smaller && resetZoom, "Larger (Ctrl++ / Ctrl+=), Smaller (Ctrl+-), Reset (Ctrl+0)");
        CHECK(appWide, "and they work from every window, not only the main one");
        CHECK(present && present->shortcut() == QKeySequence(QStringLiteral("F11"))
                  && present->shortcutContext() == Qt::ApplicationShortcut,
              "View > Full screen / presentation on F11, from any window");

        // Values pinned from the Loco Console live in this window's status bar.
        StatusPins *pins = w.findChild<StatusPins *>();
        CHECK(pins != nullptr && StatusPins::instance() == pins,
              "the status bar holds the pinned values, reachable from the Loco Console");

        TabTags::instance()->setTag(QStringLiteral("21_1"), savedTag);
    }

    // ---- session 82: clock history, find bar All tabs ----
    {
        auto countTop = [](const char *cls) {
            int n = 0;
            for (QWidget *t : QApplication::topLevelWidgets()) if (t->inherits(cls) && t->isVisible()) ++n;
            return n;
        };
        QLabel *gapLabel = w.findChild<QLabel *>(QStringLiteral("frameClockLabel"));
        CHECK(gapLabel != nullptr, "the frame clock label is in the status bar");
        const int before = countTop("ClockHistoryWindow");
        if (gapLabel) {
            QMouseEvent release(QEvent::MouseButtonRelease, QPointF(3, 3), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(gapLabel, &release);
        }
        for (int i = 0; i < 3; ++i) QApplication::processEvents();
        CHECK(countTop("ClockHistoryWindow") == before + 1, "clicking the frame clock opens its 30-minute history");
        for (QWidget *t : QApplication::topLevelWidgets()) if (t->inherits("ClockHistoryWindow")) t->close();

        QPushButton *allTabs = nullptr;
        for (QPushButton *b : w.findChildren<QPushButton *>(QStringLiteral("findAllTabsBtn"))) { allTabs = b; break; }
        CHECK(allTabs != nullptr, "each tab's find bar has an All tabs button");
        FindBar *bar = allTabs ? qobject_cast<FindBar *>(allTabs->parentWidget()) : nullptr;
        if (bar) {
            bar->setMode(FindBar::Mode::Text);
            bar->setSearchText(QStringLiteral("audit"));
            allTabs->click();
            for (int i = 0; i < 3; ++i) QApplication::processEvents();
            QCheckBox *group = nullptr;
            for (QWidget *t : QApplication::topLevelWidgets())
                if (t->inherits("SearchWindow") && t->isVisible()) group = t->findChild<QCheckBox *>(QStringLiteral("searchGroupByTab"));
            CHECK(group && group->isChecked(), "All tabs opens the search window, grouped by tab");
            for (QWidget *t : QApplication::topLevelWidgets()) if (t->inherits("SearchWindow")) t->hide();
            bar->setSearchText(QString());
        }
    }

    // ---- session 82: watch occurrences ----
    {
        QTableWidget *watchTable = w.findChild<QTableWidget *>(QStringLiteral("watchTable"));
        bool timesColumn = false;
        for (int c = 0; watchTable && c < watchTable->columnCount(); ++c)
            if (watchTable->horizontalHeaderItem(c) && watchTable->horizontalHeaderItem(c)->text() == QLatin1String("Times")) timesColumn = true;
        CHECK(timesColumn, "the watch panel counts occurrences (Times)");
    }

    // ---- session 81: speed vs distance, run report, clock-skew alarm ----
    {
        QMenu *toolsM = findMenu(bar, "Tools");
        QMenu *monitorM = nullptr;
        if (toolsM) for (QAction *a : toolsM->actions())
            if (a->menu() && a->text().remove('&') == QLatin1String("Monitor")) monitorM = a->menu();
        QAction *sd = monitorM ? findAction(monitorM, "Speed vs distance") : nullptr;
        QAction *rr = monitorM ? findAction(monitorM, "Run summary report") : nullptr;
        QAction *dmiAct = monitorM ? findAction(monitorM, "DMI (LP-OCIP)") : nullptr;
        CHECK(dmiAct && dmiAct->shortcut() == QKeySequence(QStringLiteral("Ctrl+Alt+D")),
              "Tools > Monitor > DMI (LP-OCIP), on Ctrl+Alt+D");
        if (dmiAct) {
            dmiAct->trigger();
            for (int i = 0; i < 3; ++i) QApplication::processEvents();
            int dmiOpen = 0;
            for (QWidget *t : QApplication::topLevelWidgets()) if (t->inherits("DmiWindow") && t->isVisible()) ++dmiOpen;
            CHECK(dmiOpen == 1, "and opens the panel window");
            for (QWidget *t : QApplication::topLevelWidgets()) if (t->inherits("DmiWindow")) t->close();
        }
        // Session 85: the serial terminal.
        QAction *serialAct = toolsM ? findAction(toolsM, "Serial Port Terminal") : nullptr;
        CHECK(serialAct && serialAct->shortcut() == QKeySequence(QStringLiteral("Ctrl+Alt+S")),
              "Tools > Serial Port Terminal, on Ctrl+Alt+S");
#ifndef DL_HAVE_SERIAL
        CHECK(serialAct && !serialAct->isEnabled(), "built without Qt Serial Port: present, disabled");
        serialAct = nullptr;
#endif
        if (serialAct) {
            serialAct->trigger();
            for (int i = 0; i < 3; ++i) QApplication::processEvents();
            int open = 0;
            for (QWidget *t : QApplication::topLevelWidgets()) if (t->inherits("SerialConsoleWindow") && t->isVisible()) ++open;
            CHECK(open == 1, "and opens the terminal window");
#ifdef DL_HAVE_SERIAL
            QPushButton *openBtn = nullptr;
            for (QWidget *t : QApplication::topLevelWidgets())
                if (t->inherits("SerialConsoleWindow")) openBtn = t->findChild<QPushButton *>(QStringLiteral("serialOpen"));
            CHECK(openBtn && openBtn->text() == QLatin1String("Open"),
                  "with its port closed: opening the terminal opens no port");
#endif
            for (QWidget *t : QApplication::topLevelWidgets()) if (t->inherits("SerialConsoleWindow")) t->close();
        }
        CHECK(sd && sd->shortcut() == QKeySequence(QStringLiteral("Ctrl+Alt+V")),
              "Tools > Monitor > Speed vs distance, on Ctrl+Alt+V");
        CHECK(rr && rr->shortcut() == QKeySequence(QStringLiteral("Ctrl+Alt+R")),
              "Tools > Monitor > Run summary report, on Ctrl+Alt+R");
        auto countWindows = [](const char *cls) {
            int n = 0;
            for (QWidget *t : QApplication::topLevelWidgets()) if (t->inherits(cls) && t->isVisible()) ++n;
            return n;
        };
        auto *tabs = w.findChild<QTabWidget *>(QStringLiteral("tabWidget"));
        LogModel *seeded = nullptr;
        if (tabs && tabs->count() > 0) {
            tabs->setCurrentIndex(0);
            // Both refuse an empty tab ("Select a tab with messages first"):
            // give it a row.
            if (QTableView *view = tabs->widget(0)->findChild<QTableView *>()) {
                QAbstractItemModel *m = view->model();
                while (auto *proxy = qobject_cast<QAbstractProxyModel *>(m)) m = proxy->sourceModel();
                if (auto *lm = qobject_cast<LogModel *>(m)) {
                    if (lm->count() == 0) {
                        LogEntryPtr e(new LogEntry);
                        e->epochMs = 1000;
                        e->text = QStringLiteral("audit row");
                        lm->appendEntry(e);
                        seeded = lm;
                    }
                }
            }
        }
        const int sdBefore = countWindows("SpeedDistanceWindow"), rrBefore = countWindows("RunReportWindow");
        if (sd) sd->trigger();
        if (rr) rr->trigger();
        for (int i = 0; i < 5; ++i) QApplication::processEvents();
        const bool opened = countWindows("SpeedDistanceWindow") == sdBefore + 1 && countWindows("RunReportWindow") == rrBefore + 1;
        CHECK(opened, "both open on the current tab");
        for (QWidget *t : QApplication::topLevelWidgets())
            if (t->inherits("SpeedDistanceWindow") || t->inherits("RunReportWindow")) t->close();
        for (int i = 0; i < 5; ++i) { QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QApplication::processEvents(); }

        if (seeded) seeded->clear();   // leave the tab as the later checks expect it

        ClockSkewAlarm *alarm = w.skewAlarm();
        CHECK(alarm != nullptr, "the clock-skew alarm is running");
        if (alarm) {
            const int before = alarm->episodes().size();
            alarm->sample(1000, true, 30);
            alarm->sample(5000, true, 30);
            CHECK(alarm->isRaised() && alarm->episodes().size() == before + 1, "a gap held outside the window raises it");
            alarm->sample(6000, true, 0);
            alarm->sample(9500, true, 0);
            CHECK(!alarm->isRaised(), "and it clears when the clocks come back");
            alarm->clear();
        }
    }

    // ---- session 79: undo, layouts, crash recovery, settings, colour-blind ----
    {
        QMenu *editM = findMenu(bar, "Edit");
        QAction *undo = editM ? editM->actions().value(0) : nullptr;
        CHECK(undo && undo->text().remove('&').startsWith(QLatin1String("Undo"))
                  && undo->shortcut() == QKeySequence(QKeySequence::Undo),
              "Edit > Undo is the first item, on Ctrl+Z");

        // -- Clear this tab, then undo ------------------------------------------
        auto *tabs = w.findChild<QTabWidget *>(QStringLiteral("tabWidget"));
        LogModel *model = nullptr;
        for (int i = 0; tabs && i < tabs->count(); ++i) {
            if (!tabs->tabText(i).startsWith(QStringLiteral("L1_V1"))) continue;
            tabs->setCurrentIndex(i);
            if (QTableView *view = tabs->widget(i)->findChild<QTableView *>()) {
                QAbstractItemModel *m = view->model();
                while (auto *proxy = qobject_cast<QAbstractProxyModel *>(m)) m = proxy->sourceModel();
                model = qobject_cast<LogModel *>(m);
            }
        }
        CHECK(model != nullptr, "the L1_V1 tab's model is reachable");
        if (model) {
            for (int i = 0; i < 3; ++i) {
                LogEntryPtr e(new LogEntry);
                e->epochMs = 1000 + i;
                e->text = QStringLiteral("audit row %1").arg(i);
                model->appendEntry(e);
            }
            const int before = model->count();
            QAbstractButton *clear = w.findChild<QAbstractButton *>(QStringLiteral("pbClearTabLogs"));
            if (clear) clear->click();
            CHECK(model->count() == 0, "Clear this tab empties it");
            QToolButton *offer = nullptr;
            for (QToolButton *b : w.statusBar()->findChildren<QToolButton *>()) {
                if (b->text().contains(QStringLiteral("Undo Clear tab"))) offer = b;
            }
            CHECK(offer && offer->isVisibleTo(&w), "the status bar offers to undo it");
            CHECK(undo && undo->isEnabled() && undo->text().contains(QStringLiteral("Clear tab L1_V1")),
                  "and Edit > Undo names it");
            if (undo) undo->trigger();
            CHECK(model->count() == before && model->entryAt(0)->text == QLatin1String("audit row 0"),
                  "Ctrl+Z brings every row back, in order");
        }

        // -- layouts ---------------------------------------------------------------
        const QString layoutsPath = WindowLayoutStore::defaultPath();
        QByteArray savedLayouts;
        const bool hadLayouts = QFile::exists(layoutsPath);
        if (hadLayouts) { QFile f(layoutsPath); f.open(QIODevice::ReadOnly); savedLayouts = f.readAll(); }
        QFile::remove(layoutsPath);

        QMenu *viewM = findMenu(bar, "View");
        QAction *layoutsAct = viewM ? findAction(viewM, "Layouts") : nullptr;
        CHECK(layoutsAct && layoutsAct->menu(), "View > Layouts is a submenu");
        CHECK(w.saveCurrentLayoutAs(QStringLiteral("Audit wide")), "the current layout saves under a name");
        WorkspaceSnapshot narrow = w.captureWorkspace();
        for (WorkspaceTab &t : narrow.tabs) {
            if (t.key == QLatin1String("81_1")) t.visible = false;
        }
        narrow.popouts = QStringList{ QStringLiteral("21_1") };
        {
            WindowLayoutStore store;
            store.load();
            store.put(QStringLiteral("Audit narrow"), narrow);
            store.save();
        }
        const int tabsBefore = tabs ? tabs->count() : -1;
        CHECK(w.switchToLayout(QStringLiteral("Audit narrow")), "switching to a layout works");
        for (int i = 0; i < 5; ++i) QApplication::processEvents();
        CHECK(tabs && tabs->count() == tabsBefore - 1, "the tab the layout hides is hidden");
        int popped = 0;
        for (TabPopoutWindow *p : w.findChildren<TabPopoutWindow *>()) if (p->isVisible()) ++popped;
        CHECK(popped == 1, "and the tab it pops out is popped out");
        QAction *narrowAct = nullptr;
        if (layoutsAct && layoutsAct->menu()) {
            emit layoutsAct->menu()->aboutToShow();   // what opening it does
            for (QAction *a : layoutsAct->menu()->actions()) {
                if (a->text() == QLatin1String("Audit narrow")) narrowAct = a;
            }
        }
        CHECK(narrowAct && narrowAct->shortcut() == QKeySequence(QStringLiteral("Ctrl+Alt+2")),
              "each saved layout is in the menu, the first nine on Ctrl+Alt+1..9");
        if (undo) undo->trigger();
        for (int i = 0; i < 5; ++i) QApplication::processEvents();
        popped = 0;
        for (TabPopoutWindow *p : w.findChildren<TabPopoutWindow *>()) if (p->isVisible()) ++popped;
        CHECK(tabs && tabs->count() == tabsBefore && popped == 0, "Ctrl+Z goes back to the arrangement before the switch");
        CHECK(w.deleteLayout(QStringLiteral("Audit narrow")), "a layout deletes");
        if (undo) undo->trigger();
        { WindowLayoutStore store; store.load();
          CHECK(store.names() == QStringList({ "Audit wide", "Audit narrow" }), "and Ctrl+Z puts it back in its place"); }

        QFile::remove(layoutsPath);
        if (hadLayouts) { QFile f(layoutsPath); f.open(QIODevice::WriteOnly); f.write(savedLayouts); }

        // -- crash recovery ----------------------------------------------------------
        CHECK(Settings::sessionRunning(), "a running console is marked running");
        w.writeRecoverySnapshot(true);
        WorkspaceSnapshot written;
        CHECK(SessionRecovery::read(SessionRecovery::defaultPath(), &written)
                  && written.tabs.size() == w.captureWorkspace().tabs.size(),
              "the recovery snapshot is written while it runs");
        {
            // What the next start sees after a crash: the flag still set and a
            // snapshot with a tab this console never had.
            WorkspaceSnapshot crashed = written;
            crashed.tabs.append({ QStringLiteral("55_1"), QStringLiteral("CRASHED_TAB"), true });
            crashed.activeTab = QStringLiteral("55_1");
            SessionRecovery::write(SessionRecovery::defaultPath(), crashed);
            Settings::setSessionRunning(true);
            MainWindow second;
            for (int i = 0; i < 5; ++i) QApplication::processEvents();
            CHECK(!second.recoveredSummary().isEmpty(), "the next start knows the last run did not close cleanly");
            auto *tabs2 = second.findChild<QTabWidget *>(QStringLiteral("tabWidget"));
            bool found = false;
            for (int i = 0; tabs2 && i < tabs2->count(); ++i) {
                if (tabs2->tabText(i).startsWith(QStringLiteral("CRASHED_TAB"))) found = (i == tabs2->currentIndex());
            }
            CHECK(found, "and comes back with the crashed run's tabs, the same one in front");
        }
        CHECK(!QFile::exists(SessionRecovery::defaultPath()) && !Settings::sessionRunning(),
              "a clean close leaves no recovery file and the flag down");
        Settings::setSessionRunning(true);   // `w` is still running

        // -- settings import ---------------------------------------------------------
        {
            QTemporaryDir tmp;
            const QString file = tmp.filePath(QStringLiteral("s.json"));
            QJsonObject ini;
            ini[QStringLiteral("ui/tabTags/21_1")] = QStringLiteral("3|imported");
            QJsonObject tagsSection;
            tagsSection[QStringLiteral("ini")] = ini;
            QJsonObject locoContent;
            locoContent[QStringLiteral("format")] = 1;
            locoContent[QStringLiteral("configs")] = QJsonArray{ QJsonObject{ { QStringLiteral("name"), QStringLiteral("x") } } };
            QJsonObject locoSection;
            locoSection[QStringLiteral("file")] = locoContent;
            QJsonObject sections;
            sections[QStringLiteral("tags")] = tagsSection;
            sections[QStringLiteral("loco_configs")] = locoSection;
            QJsonObject root;
            root[QStringLiteral("format")] = QStringLiteral("dlconsole-settings");
            root[QStringLiteral("version")] = 1;
            root[QStringLiteral("sections")] = sections;
            QFile f(file); f.open(QIODevice::WriteOnly); f.write(QJsonDocument(root).toJson()); f.close();

            const TabTag before = TabTags::instance()->tag(QStringLiteral("21_1"));
            QString error;
            CHECK(w.importSettingsFrom(file, { QStringLiteral("tags") }, &error), "a tags-only import succeeds");
            CHECK(TabTags::instance()->tag(QStringLiteral("21_1")).label == QLatin1String("imported"),
                  "and the tab has the imported tag");
            if (undo) undo->trigger();
            CHECK(TabTags::instance()->tag(QStringLiteral("21_1")).label == before.label,
                  "Ctrl+Z puts the old tags back");

            QMenu *toolsM = findMenu(bar, "Tools");
            QAction *locoAct = toolsM ? findAction(toolsM, "Loco Configuration") : nullptr;
            if (locoAct) locoAct->trigger();
            error.clear();
            CHECK(!w.importSettingsFrom(file, { QStringLiteral("loco_configs") }, &error)
                      && error.contains(QLatin1String("Close the Loco Configuration")),
                  "importing loco configurations while that window is open is refused, with why");
            for (QWidget *top : QApplication::topLevelWidgets()) {
                if (top->windowTitle() == QStringLiteral("Loco Configuration")) top->close();
            }
            for (int i = 0; i < 5; ++i) { QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QApplication::processEvents(); }
        }
        QAction *exportAct = file ? findAction(file, "Export settings") : nullptr;
        QAction *importAct = file ? findAction(file, "Import settings") : nullptr;
        CHECK(exportAct && importAct, "File > Export settings and Import settings");

        // -- colour-blind-safe status colours ------------------------------------------
        QMenu *themeM = nullptr;
        if (viewM) for (QAction *a : viewM->actions())
            if (a->menu() && a->text().remove('&') == QLatin1String("Theme")) themeM = a->menu();
        QAction *cb = themeM ? findAction(themeM, "Colour-blind-safe") : nullptr;
        CHECK(cb && cb->isCheckable(), "View > Theme > Colour-blind-safe status colours is a toggle");
        if (cb) {
            const bool was = cb->isChecked();
            const QColor okBefore = UiColor::ok();
            cb->trigger();
            CHECK(UiColor::colorBlindSafe() != was && Settings::colorBlindSafe() != was,
                  "it switches the colours and is remembered");
            CHECK(UiColor::ok() != okBefore, "the ok colour actually changes");
            cb->trigger();
            CHECK(UiColor::colorBlindSafe() == was, "and switches back");
        }
    }

        Settings::setWorkspaceTabs(savedTabs);
    Settings::setWorkspaceActiveTab(savedActive);
    {
        QSettings ini(Settings::iniPath(), QSettings::IniFormat);
        if (savedPopouts.isValid()) {
            ini.setValue(QStringLiteral("ui/popouts"), savedPopouts);
        } else {
            ini.remove(QStringLiteral("ui/popouts"));
        }
    }

    printf("\n%s\n", fails ? "MENU AUDIT FAILED" : "menu audit passed");
    return fails ? 1 : 0;
}
