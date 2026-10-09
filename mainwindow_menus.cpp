// =============================================================================
//  mainwindow_menus.cpp -- the menu bar and its actions (built once, from the constructor)
//  Part of MainWindow: split out of mainwindow.cpp in session 89 so each
//  concern can be read, and recompiled, on its own. No behaviour change.
// =============================================================================
#include "mainwindow.h"
#include <QSettings>
#include "frameclock.h"
#include "pinpanel.h"
#include "watchpanel.h"
#include "framenumberwatch.h"
#include "emptystate.h"
#include "uistyle.h"
#include "uicolors.h"
#include "ui_mainwindow.h"

#include "exporter.h"
#include "exportdialog.h"
#include "commandpalette.h"
#include "comparewindow.h"
#include "archivesearchwindow.h"
#include "fieldcatalog.h"
#include "fieldindexdialog.h"
#include "fieldinspector.h"
#include "fieldplot.h"
#include "filterbar.h"
#include "findbar.h"
#include "gototimestampdialog.h"
#include "logmodel.h"
#include "logtableview.h"
#include "logwriter.h"
#include "rawbytespanel.h"
#include "markerscrollbar.h"
#include "mergedwindow.h"
#include "notificationcenter.h"
#include "searchwindow.h"
#include "savedata.h"
#include "sessionfile.h"
#include "sessionwindow.h"
#include "settings.h"
#include "settingsdialog.h"
#include "stickymenu.h"
#include "schema/schemadecoder.h"
#include "timelineribbon.h"

#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QDateTime>
#include <QClipboard>
#include <QCloseEvent>
#include <QResizeEvent>
#include <QDebug>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QMouseEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QMessageBox>
#include <QMetaType>
#include <QRegularExpression>
#include <QSortFilterProxyModel>
#include <QStatusBar>
#include <QTabBar>
#include <QTableView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <limits>

#include "lococonsolewindow.h"
#include "brakingpanel.h"
#include "faultpanelwindow.h"
#include "decodeworkbench.h"
#include "flasherwindow.h"
#include "lococonfigwindow.h"
#include "minimizeddock.h"
#include "statuspins.h"
#include "presentationmode.h"
#include "tabpopoutwindow.h"
#include "undolog.h"
#include "clockskewalarm.h"
#include "dmipanel.h"
#include "dmitimetravel.h"
#ifdef DL_HAVE_SERIAL
#include "serialconsolewindow.h"
#include "serialmanager.h"
#endif
#include "runreportwindow.h"
#include "speeddistance.h"
#include "workspacesnapshot.h"
#include "tabtags.h"
#include "textzoom.h"
#include "windowgeometry.h"
#include "fieldsweepdialog.h"
#include "framediffwindow.h"
#include "roundtripwindow.h"
#include "packetmakerdialog.h"
#include "packetsequencedialog.h"
#include "dlrplayerdialog.h"
#include "sessionkeydialog.h"
#include "sessionkeystore.h"

// tshark -i any -f "udp port 50002" -T fields -e data

MainWindow::MenuRoots MainWindow::buildMenus()
{
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    m_actSettings = fileMenu->addAction(tr("&Settings…"));
    connect(m_actSettings, &QAction::triggered,
            this,          &MainWindow::onActionSettings);
    fileMenu->addSeparator();

    // Export current tab. One menu item; the dialog asks for format
    // (CSV / JSON) plus which columns to include.
    QAction *actExport = fileMenu->addAction(tr("&Export current tab…"));
    actExport->setShortcut(QKeySequence("Ctrl+E"));
    connect(actExport, &QAction::triggered,
            this,      &MainWindow::onActionExport);

    // Open a recorded .dlr archive in a read-only viewer. Kept separate
    // from the live tabs on purpose — see SessionWindow's header.
    QAction *actOpenSession = fileMenu->addAction(tr("&Open recorded session…"));
    actOpenSession->setShortcut(QKeySequence::Open);
    connect(actOpenSession, &QAction::triggered,
            this,           &MainWindow::onActionOpenSession);

    // Replaying an archive as if it were arriving now sits with the other
    // ways of opening a session rather than in Tools: what is being chosen
    // is a source of traffic, not an operation on traffic already here.
    //
    // Ctrl+Shift+S, not Ctrl+Shift+R: it shared R with Reload schema, and
    // Qt answers an ambiguous shortcut by firing NEITHER action and warning
    // on stderr — so both were unreachable from the keyboard, silently.
    QAction *actStreamDlr = fileMenu->addAction(tr("Stream session (.dlr) as &live…"));
    actStreamDlr->setShortcut(QKeySequence("Ctrl+Shift+S"));
    connect(actStreamDlr, &QAction::triggered,
            this,         &MainWindow::onActionStreamSession);

    fileMenu->addSeparator();
    QAction *actQuit = fileMenu->addAction(tr("&Quit"));
    actQuit->setShortcut(QKeySequence::Quit);
    connect(actQuit, &QAction::triggered, this, &QWidget::close);

    // Edit menu — Find (Ctrl+F).  Filter HIDES rows; Find KEEPS all rows
    // visible and JUMPS to matches.  Both useful, distinct.
    auto *editMenu = menuBar()->addMenu(tr("&Edit"));
    QAction *actFind = editMenu->addAction(tr("&Find…"));
    actFind->setShortcut(QKeySequence::Find);   // platform Ctrl+F / Cmd+F
    connect(actFind, &QAction::triggered, this, &MainWindow::onActionFind);

    // Go to timestamp (Ctrl+G).  Jumps the current tab to the message at a
    // chosen instant.  Distinct from Find: Find matches text; this navigates
    // purely by time, which is what you want when correlating against an
    // external event log ("what was on this source at 14:32:07?").
    QAction *actGoto = editMenu->addAction(tr("&Go to timestamp…"));
    actGoto->setShortcut(QKeySequence("Ctrl+G"));
    connect(actGoto, &QAction::triggered,
            this,    &MainWindow::onActionGotoTimestamp);

    // Cross-tab search. Deliberately next to Find rather than in Tools:
    // it is the same question as Find, asked of every source at once.
    QAction *actSearchAll = editMenu->addAction(tr("Search &all sources…"));
    // NOT Ctrl+Shift+F: the Active Fault Panel already owns that. Two
    // actions on the same window sharing a shortcut makes Qt report an
    // ambiguous overload and fire neither.
    actSearchAll->setShortcut(QKeySequence("Ctrl+Shift+A"));
    connect(actSearchAll, &QAction::triggered,
            this,         &MainWindow::onActionSearchAll);

    // The fourth way of asking "where is it?", and the only one that looks
    // outside what is loaded. It belongs beside the other three.
    QAction *actSearchArchive =
        editMenu->addAction(tr("Search &recorded sessions…"));
    connect(actSearchArchive, &QAction::triggered,
            this,             &MainWindow::onActionSearchArchive);

    editMenu->addSeparator();
    // Ctrl+C is the message and nothing else.
    //
    // It used to paste Time/Source/Direction/Severity/Message, which is the
    // right thing for a report and the wrong thing for the other ninety-nine
    // uses: a capture line pasted into the Decode Workbench, into a mail, or
    // back into this program has to have the four columns stripped off it
    // first, every time. The full row keeps a key of its own one shift away,
    // and both are still in the right-click menu.
    QAction *actCopySel = editMenu->addAction(tr("&Copy message"));
    actCopySel->setShortcut(QKeySequence::Copy);
    actCopySel->setToolTip(tr("The message text of the selected rows, with "
                              "no other columns."));
    connect(actCopySel, &QAction::triggered,
            this,       &MainWindow::onActionCopySelection);

    // NOT Ctrl+Shift+C: Tools > Monitor > Compare tabs already holds it,
    // and two actions on one window sharing a key makes Qt report an
    // ambiguous overload and fire neither.
    QAction *actCopyRows = editMenu->addAction(tr("Copy selected &rows"));
    actCopyRows->setShortcut(QKeySequence("Ctrl+Alt+C"));
    actCopyRows->setToolTip(tr("Time, source, direction, severity and "
                               "message, tab-separated."));
    connect(actCopyRows, &QAction::triggered,
            this,        &MainWindow::onActionCopyRows);

    // Three actions that only mean anything together, two of them function
    // keys nobody reaches for through a menu anyway. One line in Edit says
    // where bookmarks live; the shortcuts stay global either way.
    auto *bookmarkMenu = editMenu->addMenu(tr("&Bookmarks"));
    QAction *actBookmark = bookmarkMenu->addAction(tr("Toggle &bookmark"));
    actBookmark->setShortcut(QKeySequence("Ctrl+B"));
    connect(actBookmark, &QAction::triggered,
            this,        &MainWindow::onActionToggleBookmark);

    QAction *actNextBm = bookmarkMenu->addAction(tr("Next bookmark"));
    actNextBm->setShortcut(QKeySequence("F2"));
    connect(actNextBm, &QAction::triggered, this, &MainWindow::onActionNextBookmark);

    QAction *actPrevBm = bookmarkMenu->addAction(tr("Previous bookmark"));
    actPrevBm->setShortcut(QKeySequence("Shift+F2"));
    connect(actPrevBm, &QAction::triggered, this, &MainWindow::onActionPrevBookmark);

    // Stepping between the marks the minimap already draws. F4 rather than
    // F3, which the find bar holds for find-next.
    editMenu->addSeparator();
    QAction *actNextProb = editMenu->addAction(tr("Next &problem"));
    actNextProb->setShortcut(QKeySequence("F4"));
    connect(actNextProb, &QAction::triggered, this, &MainWindow::onActionNextProblem);

    QAction *actPrevProb = editMenu->addAction(tr("Previous pro&blem"));
    actPrevProb->setShortcut(QKeySequence("Shift+F4"));
    connect(actPrevProb, &QAction::triggered, this, &MainWindow::onActionPrevProblem);

    auto *viewMenu = menuBar()->addMenu(tr("&View"));

    QAction *actMerged = viewMenu->addAction(tr("All sources, &chronological…"));
    Q_UNUSED(actMerged);
    actMerged->setShortcut(QKeySequence("Ctrl+M"));
    connect(actMerged, &QAction::triggered, this, &MainWindow::onActionMergedView);

    // Three groups, separated: what you are looking at, how it is laid out,
    // and how it is drawn.
    viewMenu->addSeparator();
    // Column visibility. Message is deliberately absent: hiding it would
    // leave a table of metadata about messages nobody can read.
    // Dock visibility. Without this a closed dock is UNRECOVERABLE: Qt only
    // offers its built-in toggle popup on a right-click in the toolbar or
    // dock area, which is undiscoverable and unavailable when the last dock
    // in an area has gone. Layout persistence made it permanent too — a
    // dock closed once stayed closed across every future launch.
    //
    // toggleViewAction() is used rather than hand-written actions because
    // it is two-way: the check state follows the dock when it is closed by
    // its own [x], not only when toggled from here.
    // Populated on aboutToShow, NOT here: the docks are created further
    // down the constructor, so building the list now would find every
    // pointer null and silently produce a menu containing only the reset
    // command. Deferring removes the ordering dependency entirely rather
    // than relying on someone remembering it.
    auto *panelsMenu = new StickyMenu(tr("&Panels"), this);
    viewMenu->addMenu(panelsMenu);
    connect(panelsMenu, &QMenu::aboutToShow, this, [this, panelsMenu]() {
        panelsMenu->clear();
        for (QDockWidget *d : { m_sourceDock, m_rawDock, m_fieldDock,
                                m_bookmarkDock, m_failDock, m_pinDock,
                                m_watchDock }) {
            if (d) panelsMenu->addAction(d->toggleViewAction());
        }
        panelsMenu->addSeparator();
        QAction *reset = panelsMenu->addAction(tr("&Reset panel layout"));
        connect(reset, &QAction::triggered,
                this, &MainWindow::onActionResetPanels);
    });

    // StickyMenu: these are independent toggles, and a menu that shuts
    // after each one makes hiding three columns a three-trip job.
    auto *columnsMenu = new StickyMenu(tr("&Columns"), this);
    viewMenu->addMenu(columnsMenu);
    {
        const QList<int> hiddenCols = Settings::hiddenColumns();
        const QVector<QPair<int, QString>> cols {
            { LogModel::ColTime,      tr("Time") },
            { LogModel::ColSource,    tr("Source") },
            { LogModel::ColFriendly,  tr("Name") },
            { LogModel::ColDirection, tr("Direction") },
            { LogModel::ColSeverity,  tr("Severity") },
        };
        for (const auto &c : cols) {
            QAction *a = columnsMenu->addAction(c.second);
            a->setCheckable(true);
            a->setChecked(!hiddenCols.contains(c.first));
            a->setData(c.first);
            connect(a, &QAction::triggered,
                    this, &MainWindow::onActionToggleColumn);
        }
    }

    // Row density. Grouped as exclusive actions rather than a submenu of
    // checkboxes so it is obvious only one applies.
    auto *densityMenu = viewMenu->addMenu(tr("Row &density"));
    auto *densityGroup = new QActionGroup(this);
    densityGroup->setExclusive(true);
    const QStringList densityNames{ tr("&Compact"), tr("&Normal"), tr("Co&mfortable") };
    const int currentDensity = Settings::rowDensity();
    for (int i = 0; i < densityNames.size(); ++i) {
        QAction *a = densityMenu->addAction(densityNames.at(i));
        a->setCheckable(true);
        a->setChecked(i == currentDensity);
        a->setData(i);
        densityGroup->addAction(a);
        connect(a, &QAction::triggered, this, &MainWindow::onActionSetDensity);
    }

    viewMenu->addSeparator();

    // Zone toggle. Local by default — control rooms think in local time —
    // but switchable, because the archives are UTC and correlating the two
    // is exactly when this matters.
    QAction *actUtc = viewMenu->addAction(tr("Show times in &UTC"));
    actUtc->setObjectName(QStringLiteral("actShowUtc"));
    actUtc->setCheckable(true);
    actUtc->setChecked(Settings::showUtc());
    connect(actUtc, &QAction::triggered, this, &MainWindow::onActionToggleUtc);

    // Theme: every theme as a radio item, so one can be tried with a click,
    // and the toggle, which flips between the last light and last dark
    // theme used (Sepia <-> Nord, not always back to the Ayu pair).
    QMenu *themeMenu = viewMenu->addMenu(tr("&Theme"));
    auto *themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);
    for (Theme theme : ThemeUtil::all()) {
        QAction *item = themeMenu->addAction(ThemeUtil::label(theme));
        item->setCheckable(true);
        item->setChecked(theme == m_theme);
        item->setData(QString::fromLatin1(ThemeUtil::toString(theme)));
        themeGroup->addAction(item);
        m_themeActions.append(item);
        connect(item, &QAction::triggered, this, [this, theme]() {
            Settings::setTheme(QString::fromLatin1(ThemeUtil::toString(theme)));
            onThemeChanged(QString::fromLatin1(ThemeUtil::toString(theme)));
        });
    }
    themeMenu->addSeparator();
    m_actToggleTheme = themeMenu->addAction(tr("Toggle &Dark/Light"));
    connect(m_actToggleTheme, &QAction::triggered,
            this,             &MainWindow::onActionToggleTheme);

    // Text size: one size for every window (textzoom.h). The shortcuts are
    // application-wide, so they work from the Loco Console or the Flasher
    // too, not only when the main window has focus.
    QMenu *zoomMenu = viewMenu->addMenu(tr("Te&xt size"));
    QAction *actZoomIn = zoomMenu->addAction(tr("&Larger"));
    actZoomIn->setShortcuts({ QKeySequence(QStringLiteral("Ctrl++")), QKeySequence(QStringLiteral("Ctrl+=")) });
    actZoomIn->setShortcutContext(Qt::ApplicationShortcut);
    connect(actZoomIn, &QAction::triggered, this, []() { TextZoom::zoomIn(); });
    QAction *actZoomOut = zoomMenu->addAction(tr("&Smaller"));
    actZoomOut->setShortcut(QKeySequence(QStringLiteral("Ctrl+-")));
    actZoomOut->setShortcutContext(Qt::ApplicationShortcut);
    connect(actZoomOut, &QAction::triggered, this, []() { TextZoom::zoomOut(); });
    QAction *actZoomReset = zoomMenu->addAction(tr("&Reset (100 %)"));
    actZoomReset->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
    actZoomReset->setShortcutContext(Qt::ApplicationShortcut);
    connect(actZoomReset, &QAction::triggered, this, []() { TextZoom::reset(); });
    zoomMenu->addSeparator();
    QAction *zoomNow = zoomMenu->addAction(QString());
    zoomNow->setEnabled(false);
    auto showZoom = [zoomNow]() {
        QString text = QObject::tr("Now: %1 %").arg(TextZoom::percent());
        if (TextZoom::boost() > 0) {
            text += QObject::tr("  (presentation: %1 %)").arg(TextZoom::effectivePercent());
        }
        zoomNow->setText(text);
    };
    showZoom();
    connect(TextZoom::notifier(), &TextZoom::Notifier::changed, this, showZoom);

    // Full screen / presentation (F11): whichever DLConsole window is active.
    QAction *actPresent = viewMenu->addAction(tr("&Full screen / presentation"));
    actPresent->setShortcut(QKeySequence(QStringLiteral("F11")));
    actPresent->setShortcutContext(Qt::ApplicationShortcut);
    actPresent->setToolTip(tr("Full screen, menus and status bar hidden, text one size larger.\n"
                              "F11 or Esc to come back."));
    connect(actPresent, &QAction::triggered, this, [this]() {
        QWidget *target = QApplication::activeWindow();
        // A dialog is not a thing to present: fall back to this window.
        if (target == nullptr || target->windowType() != Qt::Window) {
            target = this;
        }
        PresentationMode::instance()->toggle(target);
    });

    // Tools menu.
    //
    // Seventeen flat entries had become a wall to read, and the ones that
    // TRANSMIT sat between two that only read — Packet Maker directly above
    // "Selected row → Decode Workbench". Grouping by what the tool does to
    // the target puts every transmitting tool behind one labelled step, and
    // costs nothing in reach: the command palette (Ctrl+P) recurses into
    // submenus, so every action here is still one flat search away.
    //
    // The submenus are created up front, in display order; the actions below
    // attach to them wherever they happen to be built.
    auto *toolsMenu = menuBar()->addMenu(tr("&Tools"));

    auto *monitorMenu  = toolsMenu->addMenu(tr("&Monitor"));      // watch it run
    auto *inspectMenu  = toolsMenu->addMenu(tr("&Inspect"));      // read a frame
    auto *transmitMenu = toolsMenu->addMenu(tr("&Transmit"));     // SENDS packets
    toolsMenu->addSeparator();
    auto *testMenu     = toolsMenu->addMenu(tr("Test &cases"));   // acceptance runs
    auto *schemaMenu   = toolsMenu->addMenu(tr("&Schema"));       // the decoder itself
    toolsMenu->addSeparator();

    QAction *actCompare = monitorMenu->addAction(tr("&Compare tabs…"));
    actCompare->setShortcut(QKeySequence("Ctrl+Shift+C"));
    connect(actCompare, &QAction::triggered,
            this,       &MainWindow::onActionCompareTabs);

    QAction *actLocoConsole = monitorMenu->addAction(tr("&Live Loco Console…"));
    actLocoConsole->setShortcut(QKeySequence("Ctrl+Shift+L"));
    connect(actLocoConsole, &QAction::triggered,
            this,           &MainWindow::onActionLocoConsole);

    QAction *actFaultPanel = monitorMenu->addAction(tr("Active &Fault Panel…"));
    actFaultPanel->setShortcut(QKeySequence("Ctrl+Shift+F"));
    connect(actFaultPanel, &QAction::triggered,
            this,          &MainWindow::onActionFaultPanel);

    QAction *actBraking = monitorMenu->addAction(tr("&Braking Curves…"));
    // Ctrl+Shift+U (the @uba stream), not Ctrl+Shift+B: B has meant
    // "selected row -> Decode Workbench" since session 23, in this window,
    // the compare window and the session window. Session 57 bound it here
    // too, and Qt fires neither action on an ambiguous shortcut.
    actBraking->setShortcut(QKeySequence("Ctrl+Shift+U"));
    connect(actBraking, &QAction::triggered,
            this,       &MainWindow::onActionBrakingPanel);

    QAction *actPlotField = monitorMenu->addAction(tr("&Plot field over time…"));
    actPlotField->setShortcut(QKeySequence("Ctrl+Shift+P"));
    connect(actPlotField, &QAction::triggered,
            this,         &MainWindow::onActionPlotField);

    // Session 81: speed against track location, and the one-page summary.
    QAction *actSpeedDistance = monitorMenu->addAction(tr("Speed vs &distance…"));
    actSpeedDistance->setShortcut(QKeySequence("Ctrl+Alt+V"));
    actSpeedDistance->setToolTip(tr("Actual and permitted speed against track location, with targets and "
                                    "the firmware's braking curve (@dmi, @uba)"));
    connect(actSpeedDistance, &QAction::triggered, this, &MainWindow::onActionSpeedDistance);

    // Session 83: the loco pilot's panel, as Annexure-B lays it out.
    QAction *actDmi = monitorMenu->addAction(tr("D&MI (LP-OCIP)…"));
    actDmi->setShortcut(QKeySequence("Ctrl+Alt+D"));
    actDmi->setToolTip(tr("The Loco Pilot's Operation-cum-Indication Panel, drawn from @dmi as "
                          "RDSO Annexure-B (Amdt-3) specifies it"));
    connect(actDmi, &QAction::triggered, this, [this]() {
        auto *w = new DmiWindow(m_dispatcher, this);
        w->show();
        w->raise();
    });

    QAction *actRunReport = monitorMenu->addAction(tr("&Run summary report…"));
    actRunReport->setShortcut(QKeySequence("Ctrl+Alt+R"));
    actRunReport->setToolTip(tr("One page of what the current tab's capture shows: modes, speed, tags, "
                                "silences, clock skew, faults, reject conditions"));
    connect(actRunReport, &QAction::triggered, this, &MainWindow::onActionRunReport);

    // Session 171: the day split into missions (start of mission to the next).
    QAction *actMissionReport = monitorMenu->addAction(tr("&Mission report…"));
    actMissionReport->setObjectName(QStringLiteral("actMissionReport"));
    actMissionReport->setShortcut(QKeySequence("Ctrl+Alt+M"));
    actMissionReport->setToolTip(tr("Every mission in the current tab, start of mission to the next: start-up "
                                    "phases, modes, System_Failure / Trip, EB/FSB, speed, tags, radio, faults; "
                                    "saved as one HTML file"));
    connect(actMissionReport, &QAction::triggered, this, &MainWindow::onActionMissionReport);

    // Session 173: the radio and GSM links over time.
    QAction *actRadioHealth = monitorMenu->addAction(tr("Radio and GPS &health…"));
    actRadioHealth->setObjectName(QStringLiteral("actRadioHealth"));
    actRadioHealth->setToolTip(tr("The current tab's radio over time: DMI signal bars, no-radio spells against "
                                  "the radio holes announced, radios not OK, temperatures, power, GSM RSSI, GPS satellites / C/N0 / link"));
    connect(actRadioHealth, &QAction::triggered, this, &MainWindow::onActionRadioHealth);

    // Session 174: every fault as a bar, under the mode.
    QAction *actFaultTimeline = monitorMenu->addAction(tr("&Fault timeline…"));
    actFaultTimeline->setObjectName(QStringLiteral("actFaultTimeline"));
    actFaultTimeline->setToolTip(tr("The current tab's NMS faults and LCU elements as bars per card, under the "
                                    "loco's mode, with the faults raised at each System_Failure"));
    connect(actFaultTimeline, &QAction::triggered, this, &MainWindow::onActionFaultTimeline);

    // Session 176: the DIO inputs and outputs, under the mode.
    QAction *actCabIo = monitorMenu->addAction(tr("&Cab inputs and outputs…"));
    actCabIo->setObjectName(QStringLiteral("actCabIo"));
    actCabIo->setToolTip(tr("The current tab's DIO logs as a timeline: every input / output that changes "
                            "(DMI buttons, cab forward / reverse, horn, traction cut-off, brake relays), under the mode"));
    connect(actCabIo, &QAction::triggered, this, &MainWindow::onActionCabIo);

    // Session 97: one incident, read in one pass.
    QAction *actIncidentReport = monitorMenu->addAction(tr("&Incident report…"));
    actIncidentReport->setShortcut(QKeySequence("Ctrl+Alt+I"));
    actIncidentReport->setToolTip(tr("Pick a moment: the DMI at the key moments around it, the speed/permitted/"
                                     "target plot, mode changes, EB/FSB applications, reject findings and the "
                                     "raw frames, in one HTML file"));
    connect(actIncidentReport, &QAction::triggered, this, &MainWindow::onActionIncidentReport);

    // Session 98: two locos, one timeline.
    QAction *actTwoLoco = monitorMenu->addAction(tr("&Two-loco view…"));
    actTwoLoco->setShortcut(QKeySequence("Ctrl+Alt+T"));
    actTwoLoco->setToolTip(tr("Two tabs' location and speed over time, the gap between them, and SoS/"
                              "collision/head-on/rear-end events (@sos, else @lsos), and each loco as the other's SoS table had it"));
    connect(actTwoLoco, &QAction::triggered, this, &MainWindow::onActionTwoLocoView);

    // Session 184: the firmware's SoS table and decisions (@sos / @sossrc / @sosev).
    QAction *actSos = monitorMenu->addAction(tr("S&oS…"));
    actSos->setObjectName(QStringLiteral("actSos"));
    actSos->setShortcut(QKeySequence("Ctrl+Alt+O"));
    actSos->setToolTip(tr("What the loco's SoS logic knew and decided, moment by moment: every loco in its SoS "
                          "table with the checks run on it, the target it picked, and each decision in words "
                          "(@sos / @sossrc / @sosev)"));
    connect(actSos, &QAction::triggered, this, &MainWindow::onActionSosWindow);

    // Session 99: the track, by absolute location.
    QAction *actTrackDiagram = monitorMenu->addAction(tr("Trac&k diagram…"));
    actTrackDiagram->setShortcut(QKeySequence("Ctrl+Alt+K"));
    actTrackDiagram->setToolTip(tr("RFID tags, signals and the movement authority end on one line, by absolute "
                                   "location, with the loco riding it over a draggable time cursor"));
    connect(actTrackDiagram, &QAction::triggered, this, &MainWindow::onActionTrackDiagram);

    // Schema reload. Pairs with the decode-failure dock: edit kavach.xml,
    // reload, and the dock repopulates with whatever the new schema still
    // cannot handle — which is the decoder development loop.
    QAction *actLoadTests = testMenu->addAction(tr("&Load test cases…"));
    connect(actLoadTests, &QAction::triggered,
            this,         &MainWindow::onActionLoadTestCases);
    QAction *actTestReport = testMenu->addAction(tr("&Save observation report…"));
    connect(actTestReport, &QAction::triggered,
            this,          &MainWindow::onActionSaveTestReport);
    testMenu->addSeparator();
    QAction *actResetRun = testMenu->addAction(tr("&Reset run"));
    connect(actResetRun, &QAction::triggered,
            this,        &MainWindow::onActionResetTestRun);

    QAction *actReloadSchema = schemaMenu->addAction(tr("&Reload schema"));
    actReloadSchema->setShortcut(QKeySequence("Ctrl+Shift+R"));
    connect(actReloadSchema, &QAction::triggered,
            this,            &MainWindow::onActionReloadSchema);

    QAction *actFieldIndex = schemaMenu->addAction(tr("&Field index…"));
    connect(actFieldIndex, &QAction::triggered,
            this, [this] { showFieldIndex(QString()); });

    QAction *actChooseSchema = schemaMenu->addAction(tr("Use e&xternal schema file…"));
    connect(actChooseSchema, &QAction::triggered,
            this,            &MainWindow::onActionChooseSchema);

    QAction *actBuiltinSchema = schemaMenu->addAction(tr("Revert to built-in schema"));
    connect(actBuiltinSchema, &QAction::triggered, this, [this]() {
        Settings::setSchemaPath(QString());
        onActionReloadSchema();
    });

    QAction *actDecodeWb = inspectMenu->addAction(tr("&Decode Workbench…"));
    actDecodeWb->setShortcut(QKeySequence("Ctrl+Shift+D"));
    connect(actDecodeWb, &QAction::triggered,
            this,        &MainWindow::onActionDecodeWorkbench);

    QAction *actPacketMaker = transmitMenu->addAction(tr("&Packet Maker…"));
    actPacketMaker->setShortcut(QKeySequence("Ctrl+Shift+M"));
    connect(actPacketMaker, &QAction::triggered,
            this,           &MainWindow::onActionPacketMaker);

    // The two "send what I am looking at over there" entries. They live in
    // the menu as well as the row context menu on purpose: an action that
    // exists only in a context menu is invisible to the command palette,
    // which harvests the menu bar.
    QAction *actRowToWb = inspectMenu->addAction(
        tr("Selected row → Decode Workbenc&h"));
    actRowToWb->setShortcut(QKeySequence("Ctrl+Shift+B"));
    connect(actRowToWb, &QAction::triggered,
            this,       &MainWindow::onActionSelectedToWorkbench);

    QAction *actRowToPm = transmitMenu->addAction(
        tr("Selected row → Packet Ma&ker"));
    connect(actRowToPm, &QAction::triggered,
            this,       &MainWindow::onActionSelectedToPacketMaker);

    QAction *actFrameDiff = inspectMenu->addAction(tr("Frame &Diff…"));
    connect(actFrameDiff, &QAction::triggered, this, &MainWindow::onActionFrameDiff);

    QAction *actFieldSweep = transmitMenu->addAction(tr("Field S&weep…"));
    connect(actFieldSweep, &QAction::triggered, this, &MainWindow::onActionFieldSweep);

    // With the schema commands, not with the transmitting tools: it answers
    // a question about the schema, and the loop it belongs to is edit
    // kavach.xml → reload → validate against real traffic.
    schemaMenu->addSeparator();
    QAction *actRoundTrip = schemaMenu->addAction(tr("&Round-trip Validator…"));
    connect(actRoundTrip, &QAction::triggered, this, &MainWindow::onActionRoundTrip);

    QAction *actPacketSeq = inspectMenu->addAction(tr("Packet Se&quence…"));
    connect(actPacketSeq, &QAction::triggered,
            this,         &MainWindow::onActionPacketSequence);

    QAction *actSessionKey = toolsMenu->addAction(tr("Session &Key…"));
    actSessionKey->setShortcut(QKeySequence("Ctrl+Shift+K"));
    connect(actSessionKey, &QAction::triggered,
            this,          &MainWindow::onActionSessionKey);

    // Firmware Flasher: top level in Tools, like Session Key, rather than in
    // Transmit. Transmit is "send Kavach packets to equipment"; this writes
    // firmware onto a chassis in updater mode, which is its own mode of
    // working and gets its own window.
    //
    // Ctrl+Alt+F, not the Ctrl+Shift+F the handoff suggested: that has been
    // Monitor > Active Fault Panel for a long time, and Qt fires NEITHER
    // action on an ambiguous shortcut (the menu audit checks for exactly this).
    toolsMenu->addSeparator();
    QAction *actFlasher = toolsMenu->addAction(tr("Firmware &Flasher…"));
    actFlasher->setShortcut(QKeySequence("Ctrl+Alt+F"));
    connect(actFlasher, &QAction::triggered,
            this,       &MainWindow::onActionFirmwareFlasher);

    // Loco Configuration: the loco_config_v12 tool as a window. Beside the
    // flasher: both are "set up a loco" tools with their own window, and it
    // sends one datagram on an explicit, confirmed click rather than being a
    // Transmit-style packet builder.
    QAction *actLocoConfig = toolsMenu->addAction(tr("&Loco Configuration…"));
    actLocoConfig->setShortcut(QKeySequence("Ctrl+Alt+L"));
    connect(actLocoConfig, &QAction::triggered,
            this,          &MainWindow::onActionLocoConfig);

    // Session 85: a QCom-style serial terminal. The IOA's input / output /
    // analog logs only come out of a serial port; with "Feed console" on,
    // its lines land in a tab like UDP traffic. One window per port.
    QAction *actSerial = toolsMenu->addAction(tr("&Serial Port Terminal…"));
    actSerial->setShortcut(QKeySequence("Ctrl+Alt+S"));
    actSerial->setToolTip(tr("Open a serial port (baud, data bits, parity, stop bits, flow control), "
                             "watch and log it, send text or hex, and feed its lines into the console"));
#ifdef DL_HAVE_SERIAL
    // Only a window is made here: no port is touched until the operator
    // presses Open in it. The console runs on Ethernet with no serial port
    // present, open, or even built in.
    connect(actSerial, &QAction::triggered, this, [this]() { openSerialTerminal(); });
    // Session 105: named port setups. Built when shown, from what is saved.
    QMenu *profilesMenu = toolsMenu->addMenu(tr("Serial Pro&files"));
    profilesMenu->setObjectName(QStringLiteral("serialProfilesMenu"));
    connect(profilesMenu, &QMenu::aboutToShow, this, [this, profilesMenu]() {
        profilesMenu->clear();
        QSettings s(Settings::iniPath(), QSettings::IniFormat);
        const QVector<SerialProfile> all = SerialProfile::loadAll(s);
        QAction *openAll = profilesMenu->addAction(tr("Open &all profiles"));
        openAll->setEnabled(!all.isEmpty());
        connect(openAll, &QAction::triggered, this, [this]() { openSerialProfiles(false); });
        QAction *closeAll = profilesMenu->addAction(tr("&Close all serial ports"));
        closeAll->setEnabled(m_serial && !m_serial->openPorts().isEmpty());
        connect(closeAll, &QAction::triggered, this, [this]() { if (m_serial) m_serial->closeAll(); });
        profilesMenu->addSeparator();
        if (all.isEmpty()) {
            QAction *none = profilesMenu->addAction(tr("No profiles yet \u2014 save one from the terminal"));
            none->setEnabled(false);
        }
        for (const SerialProfile &p : all) {
            const SerialLink *l = m_serial ? m_serial->link(p.config.portName) : nullptr;
            const bool open = l && l->isOpen();
            QAction *a = profilesMenu->addAction(
                tr("%1 \u2014 %2 %3%4").arg(p.name, p.config.portName, p.config.summary(),
                                           p.autoOpen ? tr(", opens at start") : QString()));
            a->setCheckable(true);
            a->setChecked(open);
            a->setToolTip(open ? tr("Running; click to open a terminal on it")
                               : tr("Open this port with these settings"));
            connect(a, &QAction::triggered, this, [this, p, open]() {
                if (open) { openSerialTerminal(p.config.portName); return; }
                m_serial->openProfilesAsync({ p });     // session 156: no wait; reported when done
            });
        }
    });
#else
    actSerial->setEnabled(false);
    actSerial->setToolTip(tr("Not available: this DLConsole was built without Qt's Serial Port module. "
                             "Ethernet logs are unaffected."));
#endif

    m_menuWindow = menuBar()->addMenu(tr("&Window"));
    QAction *actShowAll = m_menuWindow->addAction(tr("Show All Hidden Tabs"));
    connect(actShowAll, &QAction::triggered,
            this,       &MainWindow::onActionShowAllTabs);
    m_menuWindow->addSeparator();
    connect(m_menuWindow, &QMenu::aboutToShow,
            this,         &MainWindow::rebuildWindowMenu);

    // Ctrl+P. Added to the Help menu rather than left as a hidden shortcut:
    // a discoverability feature nobody can discover is self-defeating.
    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction *actPalette = helpMenu->addAction(tr("&Run command…"));
    actPalette->setShortcut(QKeySequence("Ctrl+P"));
    connect(actPalette, &QAction::triggered,
            this,       &MainWindow::onActionCommandPalette);
    QAction *actAbout = helpMenu->addAction(tr("&About DLConsole"));
    connect(actAbout, &QAction::triggered, this, &MainWindow::onActionAbout);

    return MenuRoots{ fileMenu, editMenu, viewMenu, themeMenu, actFind };
}
