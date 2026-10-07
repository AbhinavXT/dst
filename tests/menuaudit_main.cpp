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
#include <QToolBar>
#include <QTreeWidget>
#include <QElapsedTimer>
#include "logmodel.h"
#include "dmipanel.h"
#include "dmitimetravel.h"
#include "capturedecoder.h"
#include <QDateTime>
#include <QMouseEvent>
#include "uicolors.h"
#include "workspacesnapshot.h"
#include "clockskewalarm.h"
#include "findbar.h"
#include "sourcerowdelegate.h"
#include "layoutaudit.h"
#include <QCheckBox>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QToolButton>
#ifdef DL_HAVE_SERIAL
#  include "serialmanager.h"
#endif
#if defined(DL_HAVE_SERIAL) && defined(Q_OS_UNIX)
#  include <unistd.h>
#  if defined(Q_OS_MACOS)
#    include <util.h>
#  else
#    include <pty.h>
#  endif
#endif
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
    // Unbuffered (session 116): on Windows a piped stdout lost everything
    // after a buffer's worth, so the CI log stopped mid-line with no FAIL
    // and no summary. Unbuffered, even a crash leaves the log whole.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // Offscreen unless told otherwise, as the gate runs it (see tests/main.cpp).
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
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
    const TabTag savedTag = TabTags().tag(QStringLiteral("21_1"));   // tags live in the ini
    TabTags().setTag(QStringLiteral("21_1"), TabTag());

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
#ifdef DL_HAVE_SERIAL
        // Session 102: a chip per open port; none open, so none shown.
        QWidget *chips = w.findChild<QWidget *>(QStringLiteral("serialChips"));
        CHECK(chips && !chips->isVisible(), "the serial port chips are in the status bar, hidden with no port open");
        // Session 105: Tools > Serial Profiles, built when shown. The audit
        // saves none, so it offers nothing to open and says where to start.
        QMenu *profiles = w.findChild<QMenu *>(QStringLiteral("serialProfilesMenu"));
        if (profiles) emit profiles->aboutToShow();
        bool hint = false, openAllOff = false;
        for (QAction *a : profiles ? profiles->actions() : QList<QAction *>()) {
            if (a->text().startsWith(QLatin1String("No profiles yet")) && !a->isEnabled()) hint = true;
            if (a->text().contains(QLatin1String("all profiles")) && !a->isEnabled()) openAllOff = true;
        }
        CHECK(profiles && hint && openAllOff,
              "Tools > Serial Profiles: with none saved, Open all is off and it says how to make one");
#  ifdef Q_OS_UNIX
        // A port opened in MainWindow's manager shows a chip, with no
        // terminal window open, and the chip goes when the port closes.
        SerialManager *mgr = w.findChild<SerialManager *>();
        int master = -1, slave = -1;
        char name[256] = {};
        if (mgr && chips && ::openpty(&master, &slave, name, nullptr, nullptr) == 0) {
            ::close(slave);
            SerialConfig c;
            c.portName = QString::fromLocal8Bit(name);
            // Not fed to the console: later checks count this window's tabs.
            mgr->setFeed(c.portName, false);
            CHECK(mgr->open(c), "a port opens in MainWindow's serial manager");
            QApplication::processEvents();
            QToolButton *chip = chips->findChild<QToolButton *>(QStringLiteral("serialChip"));
            CHECK(chips->isVisible() && chip
                      && chip->text().contains(SerialManager::shortName(c.portName)),
                  "an open port shows a chip, named after it, with no terminal open");
            // Session 104: lines that do not decode (a wrong baud rate) turn
            // the chip into a warning within its one-second refresh.
            const QByteArray garbage = QByteArray::fromHex("8fe31cf0007e9bc3f806e0fe180d0a");
            for (int i = 0; i < 12; ++i) (void)::write(master, garbage.constData(), size_t(garbage.size()));
            QElapsedTimer waited;
            waited.start();
            while (waited.elapsed() < 1500) QApplication::processEvents(QEventLoop::AllEvents, 20);
            chip = chips->findChild<QToolButton *>(QStringLiteral("serialChip"));
            CHECK(chip && chip->text().startsWith(QChar(0x26A0)) && chip->toolTip().contains(QLatin1String("check baud")),
                  "a port whose lines do not decode shows a warning chip, and says what to check");
            mgr->closeAll();
            QApplication::processEvents();
            CHECK(!chips->isVisible(), "the chip goes when the port closes");
            ::close(master);
        } else {
            CHECK(mgr != nullptr, "MainWindow owns a serial manager");
        }
#  endif
#endif
    }

    // ---- Session 118: the frame (top strip, rail, log header) ----------------
    {
        QToolBar *strip = w.findChild<QToolBar *>(QStringLiteral("statusStrip"));
        QToolBar *rail = w.findChild<QToolBar *>(QStringLiteral("toolsRail"));
        CHECK(strip && !strip->isMovable() && strip->isVisible(), "a fixed status strip across the top");
        CHECK(rail && rail->orientation() == Qt::Vertical && rail->isVisible(), "an icon rail down the left");
        // Every rail button is a command the menus also have: nothing is
        // reachable only from an icon.
        QStringList menuTexts;
        std::function<void(QMenu *)> collect = [&](QMenu *m) {
            emit m->aboutToShow();
            for (QAction *a : m->actions()) { if (a->menu()) collect(a->menu()); else menuTexts << a->text(); }
        };
        for (QAction *top : w.menuBar()->actions()) if (top->menu()) collect(top->menu());
        int railButtons = 0, orphans = 0, unnamed = 0;
        for (QToolButton *b : rail ? rail->findChildren<QToolButton *>() : QList<QToolButton *>()) {
            if (!b->objectName().startsWith(QLatin1String("rail_"))) continue;
            ++railButtons;
            if (!b->defaultAction() || !menuTexts.contains(b->defaultAction()->text())) ++orphans;
            if (b->accessibleName().isEmpty()) ++unnamed;
        }
        CHECK(railButtons >= 8, "the rail carries the everyday tools");
        CHECK(orphans == 0, "every rail button is a menu command too");
        CHECK(unnamed == 0, "every rail button has an accessible name");
        QWidget *header = w.findChild<QWidget *>(QStringLiteral("logHeader"));
        QWidget *right = w.findChild<QWidget *>(QStringLiteral("rightFrame"));
        QWidget *save = w.findChild<QWidget *>(QStringLiteral("pbSaveBtn"));
        CHECK(header && right && !right->isVisibleTo(&w) && save && save->parentWidget() == header,
              "the log header replaces the button column beside the log");
        QToolButton *more = w.findChild<QToolButton *>(QStringLiteral("logMore"));
        CHECK(more && more->menu() && more->menu()->actions().size() == 3,
              "clear tab, clear all and check buffer sit behind More");
        QLabel *bind = w.findChild<QLabel *>(QStringLiteral("frameClockLabel"));
        CHECK(bind && strip && strip->isAncestorOf(bind), "the clocks are in the top strip");
        QWidget *bindBox = w.findChild<QWidget *>(QStringLiteral("stripBind"));
        QLabel *bindChip = bindBox ? bindBox->findChild<QLabel *>() : nullptr;
        for (int i = 0; i < 50 && bindChip && bindChip->property("dlTone").toString() == QLatin1String("neutral"); ++i)
            QApplication::processEvents(QEventLoop::AllEvents, 20);
        CHECK(bindChip && bindChip->isVisibleTo(&w) && bindChip->property("dlRole").toString() == QLatin1String("chip")
                  && bindChip->property("dlTone").toString() != QLatin1String("neutral"),
              "the UDP chip is shown and says how the bind went");

        // Session 119: the Sources panel draws health, name and a meta line.
        QDockWidget *srcDock = w.findChild<QDockWidget *>(QStringLiteral("sourceDock"));
        QTreeWidget *srcList = srcDock ? srcDock->findChild<QTreeWidget *>() : nullptr;
        QElapsedTimer tick;
        tick.start();
        while (tick.elapsed() < 1300) QApplication::processEvents(QEventLoop::AllEvents, 20);   // one status tick
        int rows = 0, described = 0;
        for (int i = 0; srcList && i < srcList->topLevelItemCount(); ++i) {
            QTreeWidgetItem *it = srcList->topLevelItem(i);
            ++rows;
            if (!it->data(0, SourceRowDelegate::MetaRole).toString().isEmpty()
                && it->data(0, SourceRowDelegate::HealthRole).isValid()) ++described;
        }
        // Session 122: nothing visible outside every layout (an orphan is
        // drawn at its parent's corner, over whatever is there).
        {
            const QStringList orphans = LayoutAudit::orphans(&w);
            for (const QString &o : orphans) printf("      orphan: %s\n", qPrintable(o));
            CHECK(orphans.isEmpty(), "no visible widget in the main window outside every layout");
        }
        CHECK(srcList && dynamic_cast<SourceRowDelegate *>(srcList->itemDelegate()) != nullptr
                  && rows > 0 && described == rows,
              "every source row carries a health state and a meta line, drawn by its own delegate");
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

    // ---- session 158: the rail icons follow a theme change ----------------------
    // Drawn once at start-up, they kept the colour of that theme: dark-theme
    // (light) icons on a light window after switching back.
    {
        auto inkOf = [](const QIcon &icon) {
            const QImage img = icon.pixmap(18, 18).toImage().convertToFormat(QImage::Format_ARGB32);
            QColor best; int bestAlpha = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    if (img.pixelColor(x, y).alpha() > bestAlpha) { bestAlpha = img.pixelColor(x, y).alpha(); best = img.pixelColor(x, y); }
            return best;
        };
        auto near = [](const QColor &a, const QColor &b) {
            return qAbs(a.red() - b.red()) <= 8 && qAbs(a.green() - b.green()) <= 8 && qAbs(a.blue() - b.blue()) <= 8;
        };
        QToolButton *railDmi = w.findChild<QToolButton *>(QStringLiteral("rail_dmi"));
        QToolButton *more = w.findChild<QToolButton *>(QStringLiteral("logMore"));
        QAction *toggle = nullptr;
        if (view) for (QAction *a : view->actions())
            if (a->menu() && a->text().remove(QLatin1Char('&')) == QStringLiteral("Theme"))
                toggle = findAction(a->menu(), "Toggle Dark/Light");
        CHECK(railDmi && more && toggle, "fixture: the rail's DMI button, the log's More button, Toggle Dark/Light");
        if (railDmi && more && toggle) {
            bool allFollow = true;
            for (int i = 0; i < 2; ++i) {          // there and back
                toggle->trigger();
                QApplication::processEvents();
                const QColor text = qApp->palette().color(QPalette::WindowText);
                allFollow = allFollow && near(inkOf(railDmi->icon()), text) && near(inkOf(more->icon()), text);
            }
            CHECK(allFollow, "Toggle Dark/Light, twice: the rail and More icons are redrawn in each theme's text colour");
        }
    }

    // ---- session 158: right-click an @dmi row > Show on DMI -----------------------
    {
        QSettings ini(Settings::iniPath(), QSettings::IniFormat);
        const QVariant savedFollow = ini.value(QStringLiteral("dmi/followCursor"));
        auto *tabs = w.findChild<QTabWidget *>(QStringLiteral("tabWidget"));
        QTableView *table = nullptr;
        LogModel *lm = nullptr;
        if (tabs && tabs->count() > 0) {
            tabs->setCurrentIndex(0);
            table = tabs->widget(0)->findChild<QTableView *>();
            QAbstractItemModel *m = table ? table->model() : nullptr;
            while (auto *proxy = qobject_cast<QAbstractProxyModel *>(m)) m = proxy->sourceModel();
            lm = qobject_cast<LogModel *>(m);
        }
        CHECK(table && lm, "fixture: the first tab's table and model");
        if (table && lm) {
            const int base = lm->count();
            // A real frame: replay/loco_1_1_27062026_170159.cap, its first @dmi.
            LogEntryPtr dmi(new LogEntry);
            dmi->epochMs = QDateTime(QDate(2026, 6, 27), QTime(17, 2, 0)).toMSecsSinceEpoch();
            dmi->text = QStringLiteral(
                "@dmi_1_1 2026-06-27T17:02:00 3983 AA AA 74 02 01 0A 6F 00 71 21 00 00 00 00 00 00 00 00 00 00 1B 06 "
                "EA 07 11 02 00 00 00 00 00 01 00 00 00 00 00 0A 84 40 06 04 01 00 00 60 A2 75 82 07 00 00 1E 01 00 00 "
                "80 07 00 00 56 01 00 80 49 00 0F 02 00 00 FA 00 00 00 00 00 B9 01 15 2D 6A 0D 00 00 00 00 00 00 50 00 "
                "00 00 00 00 F0 3C 00 F4 01 0D 00 00 00 01 06 0F 00 00 01 00 18 CB 09 2A BB BB");
            LogEntryPtr other(new LogEntry);
            other->epochMs = dmi->epochMs + 500;
            other->text = QStringLiteral("audit row, not a DMI frame");
            lm->appendEntry(dmi);
            lm->appendEntry(other);
            QApplication::processEvents();

            auto *proxy = qobject_cast<QAbstractProxyModel *>(table->model());
            auto rowPos = [&](int sourceRow) {
                QModelIndex at = lm->index(sourceRow, 0);
                if (proxy) at = proxy->mapFromSource(at);
                table->scrollTo(at);
                return table->visualRect(at).center();
            };
            // Pops the row menu as a right-click does; `click` the item if it is there.
            auto rowMenu = [&](int sourceRow, bool click) {
                bool found = false;
                QTimer::singleShot(50, [&]() {
                    auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                    if (!menu) return;
                    QAction *show = menu->findChild<QAction *>(QStringLiteral("rowShowOnDmi"));
                    found = show && show->text().remove(QLatin1Char('&')) == QStringLiteral("Show on DMI");
                    if (found && click) {
                        // Clicked in the menu itself, so exec() returns it as chosen.
                        const QPoint at = menu->actionGeometry(show).center();
                        menu->setActiveAction(show);
                        QMouseEvent press(QEvent::MouseButtonPress, at, menu->mapToGlobal(at),
                                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                        QMouseEvent release(QEvent::MouseButtonRelease, at, menu->mapToGlobal(at),
                                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                        QApplication::sendEvent(menu, &press);
                        QApplication::sendEvent(menu, &release);
                    } else {
                        menu->hide();
                    }
                });
                emit table->customContextMenuRequested(rowPos(sourceRow));
                for (int i = 0; i < 5; ++i) QApplication::processEvents();
                return found;
            };
            auto dmiWindows = []() {
                QVector<DmiWindow *> out;
                for (QWidget *t : QApplication::topLevelWidgets())
                    if (auto *d = qobject_cast<DmiWindow *>(t)) if (d->isVisible()) out << d;
                return out;
            };

            CHECK(!rowMenu(base + 1, false), "a row that is not @dmi has no Show on DMI");
            CHECK(rowMenu(base, true), "an @dmi row has Show on DMI");
            QVector<DmiWindow *> open = dmiWindows();
            CHECK(open.size() == 1, "and it opens the DMI window");
            DmiWindow *d = open.value(0);
            const DmiFrameAt *f = d ? d->moment().frameFor(QStringLiteral("1_1")) : nullptr;
            CHECK(d && d->followCursor() && d->moment().valid, "following the cursor, at a moment");
            CHECK(f && f->cap.bytes == CaptureDecoder::parseLine(dmi->text).bytes && d->selectedSource() == QStringLiteral("1_1"),
                  "showing that row's frame, for its loco");

            // The cursor moves on (another row), then Show on DMI again: the
            // same window, back on the @dmi row.
            table->setCurrentIndex(proxy ? proxy->mapFromSource(lm->index(base + 1, 0)) : lm->index(base + 1, 0));
            CHECK(rowMenu(base, true), "Show on DMI, a second time");
            CHECK(dmiWindows().size() == 1, "reuses the open DMI window");
            CHECK(d && d->moment().atMs == dmi->epochMs, "at the @dmi row's moment");

            for (DmiWindow *x : dmiWindows()) x->close();
            for (int i = 0; i < 5; ++i) { QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QApplication::processEvents(); }
            lm->clear();
        }
        if (savedFollow.isValid()) ini.setValue(QStringLiteral("dmi/followCursor"), savedFollow);
        else ini.remove(QStringLiteral("dmi/followCursor"));
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
        w.tabTags()->setColor(QStringLiteral("21_1"), 1);
        w.tabTags()->setLabel(QStringLiteral("21_1"), QStringLiteral("Brake test"));
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

        w.tabTags()->setTag(QStringLiteral("21_1"), savedTag);
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
        // Session 97: Incident report opens a modal picker first (atMs +
        // before/after), so it is checked for presence/wiring only -- not
        // triggered, which would block this offscreen run on a dialog no
        // one can answer.
        QAction *irAct = monitorM ? findAction(monitorM, "Incident report") : nullptr;
        CHECK(irAct && irAct->shortcut() == QKeySequence(QStringLiteral("Ctrl+Alt+I")),
              "Tools > Monitor > Incident report, on Ctrl+Alt+I");
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
        // Session 99: the track diagram, like Speed vs distance -- opens
        // straight into the window, no modal picker, so it is safe to trigger.
        QAction *tdAct = monitorM ? findAction(monitorM, "Track diagram") : nullptr;
        CHECK(tdAct && tdAct->shortcut() == QKeySequence(QStringLiteral("Ctrl+Alt+K")),
              "Tools > Monitor > Track diagram, on Ctrl+Alt+K");
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
        const int tdBefore = countWindows("TrackDiagramWindow");
        if (sd) sd->trigger();
        if (rr) rr->trigger();
        if (tdAct) tdAct->trigger();
        for (int i = 0; i < 5; ++i) QApplication::processEvents();
        const bool opened = countWindows("SpeedDistanceWindow") == sdBefore + 1 && countWindows("RunReportWindow") == rrBefore + 1
                          && countWindows("TrackDiagramWindow") == tdBefore + 1;
        CHECK(opened, "all three open on the current tab");
        for (QWidget *t : QApplication::topLevelWidgets())
            if (t->inherits("SpeedDistanceWindow") || t->inherits("RunReportWindow") || t->inherits("TrackDiagramWindow")) t->close();
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

            const TabTag before = w.tabTags()->tag(QStringLiteral("21_1"));
            QString error;
            CHECK(w.importSettingsFrom(file, { QStringLiteral("tags") }, &error), "a tags-only import succeeds");
            CHECK(w.tabTags()->tag(QStringLiteral("21_1")).label == QLatin1String("imported"),
                  "and the tab has the imported tag");
            if (undo) undo->trigger();
            CHECK(w.tabTags()->tag(QStringLiteral("21_1")).label == before.label,
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
