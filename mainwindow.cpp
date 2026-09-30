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

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // Crash recovery (session 79), before anything reads the workspace,
    // pop-out or layout keys: if the last run did not close cleanly, its
    // last snapshot is written into those keys, and the ordinary restore
    // further down does the rest. One restore path, not two.
    {
        WorkspaceSnapshot recovered;
        if (SessionRecovery::adoptAfterCrash(SessionRecovery::defaultPath(), &recovered)) {
            m_recoveredSummary = recovered.summary();
            if (recovered.savedAt.isValid()) {
                m_recoveredSummary += tr(", as it was at %1")
                    .arg(recovered.savedAt.toLocalTime().toString(QStringLiteral("HH:mm:ss")));
            }
        }
    }
    // Before any source tab exists: the bar is replaced wholesale, and the
    // selected tab is drawn heavier than Qt measures it (see useTabTooltips).
    // This is the bar in the screenshot where "L1_V1" rendered as "_1_V1".
    UiStyle::useTabTooltips(ui->tabWidget);

    setWindowTitle("DLConsole");
    // The old five-second label is superseded by the notification bar; it
    // stays hidden rather than being removed from the .ui file, so an
    // out-of-date .ui cannot break the build.
    ui->userMsglb->hide();

    // Placeholder shown while no tabs exist. Parented to the tab widget so
    // it sits over the empty pane rather than needing its own layout slot.
    m_emptyState = new QLabel(ui->tabWidget);
    m_emptyState->setAlignment(Qt::AlignCenter);
    m_emptyState->setWordWrap(true);
    m_emptyState->setTextFormat(Qt::RichText);
    m_emptyState->setStyleSheet(UiColor::mutedStyle());
    m_emptyState->hide();

    // Minimised tool windows appear here as named chips, far left of the
    // status bar (minimizeddock.h). Takes no space until one is minimised.
    m_windowDock = new MinimizedDock(this);
    statusBar()->addWidget(m_windowDock);
    // Live values pinned from the Loco Console (statuspins.h). Fed by the
    // dispatcher below, once it exists.
    m_statusPins = new StatusPins(this);
    statusBar()->addWidget(m_statusPins);

    m_notify = new NotificationBar(this);
    statusBar()->addWidget(m_notify, 1);
    connect(m_notify, &NotificationBar::historyRequested,
            this,     &MainWindow::onShowNotificationLog);
    ui->teToString->setReadOnly(true);
    ui->commandlabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    // Check Buffer page: the hex that lands here comes off tshark or out of
    // somebody's email, and until now the only thing that could be done with
    // it was ASCII conversion. Both real destinations for a loose buffer sit
    // one click away instead.
    {
        auto *bufRow = new QHBoxLayout;
        auto *bufToWb = new QPushButton(tr("Open in Decode Workbench"), ui->page_2);
        bufToWb->setToolTip(tr("Decode these bytes as a Kavach frame."));
        auto *bufToPm = new QPushButton(tr("Open in Packet Maker"), ui->page_2);
        bufToPm->setToolTip(tr("Fill the Packet Maker's fields from these bytes, "
                               "so the frame can be edited and re-sent."));
        bufRow->addWidget(bufToWb);
        bufRow->addWidget(bufToPm);
        bufRow->addStretch(1);
        const int convertIdx = ui->verticalLayout_2->indexOf(ui->pbConvert);
        ui->verticalLayout_2->insertLayout(convertIdx + 1, bufRow);

        connect(bufToWb, &QPushButton::clicked, this, [this]() {
            openBufferInWorkbench(ui->textEdit->toPlainText());
        });
        connect(bufToPm, &QPushButton::clicked, this, [this]() {
            openBufferInPacketMaker(ui->textEdit->toPlainText());
        });
    }

    // The DATE TIME checkbox now toggles whether the Time column is shown.
    ui->cbDatetime->setChecked(true);

    // -----------------------------------------------------------------
    //  Settings (2g): pull current values out of the INI on startup.
    //  Anything missing falls back to the default.
    // -----------------------------------------------------------------
    m_perTabCapacity = Settings::perTabCapacity();
    m_offlineWarnSec = Settings::offlineWarnSeconds();
    m_offlineErrSec  = Settings::offlineErrSeconds();
    m_theme          = ThemeUtil::fromString(Settings::theme());

    // -----------------------------------------------------------------
    //  Theme (2i): apply the palette before any widgets render so we
    //  don't get a brief flash of the wrong palette on startup. Already
    //  done in main() before MainWindow is constructed for the
    //  application-wide palette; this call makes sure any subsequent
    //  changes via the settings dialog can re-apply at runtime.
    // -----------------------------------------------------------------
    ThemeUtil::apply(m_theme);
    // ...and rebuild the stylesheet from it in the same breath. UiStyle
    // derives every colour from the palette at the moment it is called, so
    // re-applying one without the other leaves the two disagreeing: a light
    // palette under a dark sheet gives white input boxes on a dark window,
    // with menu text drawn in the light theme's near-black on the dark
    // theme's near-black. main() already does both; this makes the pair
    // impossible to separate no matter who applies the theme.
    UiStyle::apply();

    // -----------------------------------------------------------------
    //  Tab widget configuration (2e): close buttons, drag-to-reorder,
    //  right-click context menu.
    // -----------------------------------------------------------------
    ui->tabWidget->setTabsClosable(true);
    ui->tabWidget->setMovable(true);
    // Drag a tab out of the bar to pop it out into its own window.
    ui->tabWidget->tabBar()->installEventFilter(this);
    // Colour tags: redraw a tab's dot/label when its tag (or the theme) changes.
    connect(TabTags::instance(), &TabTags::changed, this, [this](const QString &key) {
        applyTabTag(key);
    });
    // Pop-outs that were open last time come back once their tab appears.
    {
        QSettings settings(Settings::iniPath(), QSettings::IniFormat);
        m_pendingPopouts = settings.value(QStringLiteral("ui/popouts")).toStringList();
    }
    ui->tabWidget->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->tabWidget, &QTabWidget::tabCloseRequested,
            this,          &MainWindow::onTabCloseRequested);
    connect(ui->tabWidget->tabBar(), &QTabBar::customContextMenuRequested,
            this,                    &MainWindow::onTabContextMenu);

    // -----------------------------------------------------------------
    //  Menu bar: built in mainwindow_menus.cpp (session 89).
    // -----------------------------------------------------------------
    const MenuRoots menus = buildMenus();

    // -----------------------------------------------------------------
    //  Status bar: built in mainwindow_status.cpp (session 89).
    // -----------------------------------------------------------------
    buildStatusBar();

    // -----------------------------------------------------------------
    //  Raw-bytes side panel (2h).
    // -----------------------------------------------------------------
    m_rawPanel = new RawBytesPanel(this);
    m_rawDock  = new QDockWidget(tr("Raw bytes"), this);
    m_rawDock->setObjectName("RawBytesDock");
    m_rawDock->setWidget(m_rawPanel);
    m_rawDock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    m_rawDock->setFeatures(QDockWidget::DockWidgetClosable
                           | QDockWidget::DockWidgetMovable
                           | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, m_rawDock);
    m_rawDock->resize(420, m_rawDock->height());

    // The panel raises a request; opening windows is this window's job.
    connect(m_rawPanel, &RawBytesPanel::openInDecodeWorkbenchRequested,
            this,       &MainWindow::openEntryInWorkbench);
    connect(m_rawPanel, &RawBytesPanel::openInPacketMakerRequested,
            this,       &MainWindow::openEntryInPacketMaker);

    // -----------------------------------------------------------------
    //  Pinned fields.
    // -----------------------------------------------------------------
    m_pinPanel = new PinPanel(this);
    m_pinDock  = new QDockWidget(tr("Pinned fields"), this);
    m_pinDock->setObjectName("PinnedFieldsDock");
    m_pinDock->setWidget(m_pinPanel);
    m_pinDock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    m_pinDock->setFeatures(QDockWidget::DockWidgetClosable
                           | QDockWidget::DockWidgetMovable
                           | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, m_pinDock);

    // FLOATING by default, unlike the other docks.
    //
    // A pinned field is watched WHILE something else is being done — a run is
    // driven from the DMI, or another window is in front — so the one place it
    // must not be is folded into the side of the window whose log you are
    // scrolling. Floating, it can sit on a second screen, or beside the console
    // rather than inside it.
    //
    // It is still a QDockWidget, so dragging it back into the window works and
    // restoreState() remembers that choice for next time. This is a default,
    // not a decision made on the operator's behalf.
    m_pinDock->setFloating(true);
    m_pinDock->resize(420, 260);
    m_pinDock->hide();     // opt-in: an empty panel is not worth a window

    // Place it beside the console the first time it is shown, not at whatever
    // corner Qt picks. Done on first show rather than here because the main
    // window has no final geometry yet during construction — restoreLayout()
    // has not run — so anything computed now would be measured against the
    // wrong rectangle.
    connect(m_pinDock, &QDockWidget::visibilityChanged,
            this, [this](bool visible) {
        if (!visible || !m_pinDock->isFloating() || m_pinPlaced) { return; }
        m_pinPlaced = true;
        const QRect g = frameGeometry();
        m_pinDock->move(g.right() - m_pinDock->width() - 40, g.top() + 80);
    });

    // Double-clicking a value asks to be shown where it came from.
    connect(m_pinPanel, &PinPanel::revealRequested,
            this, [this](const QString &key, qint64 epochMs) {
        // Straight to the frame that established the value. jumpToEntry
        // selects by timestamp, so a frame the model has since evicted simply
        // is not found — the tab still comes forward, which is as much as can
        // honestly be done once the row is gone.
        jumpToEntry(key, epochMs);
    });
    m_pinPanel->restore();

    // -----------------------------------------------------------------
    //  Watches. Same shape as the pin board and for the same reason: set up
    //  before a run, read during one, from wherever the operator is.
    // -----------------------------------------------------------------
    m_watchPanel = new WatchPanel(this);
    m_watchPanel->setNames(&m_nameMap);
    m_watchDock  = new QDockWidget(tr("Watches"), this);
    m_watchDock->setObjectName("WatchesDock");
    m_watchDock->setWidget(m_watchPanel);
    m_watchDock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    m_watchDock->setFeatures(QDockWidget::DockWidgetClosable
                             | QDockWidget::DockWidgetMovable
                             | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, m_watchDock);
    m_watchDock->setFloating(true);
    m_watchDock->resize(520, 280);
    m_watchDock->hide();
    m_watchPanel->restore();

    // A fire is announced through the notification centre, NOT only in the
    // panel. The panel may be behind another window, which is exactly the
    // situation a watch exists for — if it were only ever read by someone
    // already looking at it, a pin would have done.
    connect(m_watchPanel, &WatchPanel::watchFired,
            this, [this](const QString &label, const QString &expr,
                         const QString &src) {
        Q_UNUSED(expr)
        notify(NoteLevel::Warning,
               tr("Watch fired: %1%2").arg(label,
                   src.isEmpty() ? QString() : tr("  (%1)").arg(src)));

        // Hold the pinned values at the instant the condition was met, if the
        // operator asked for that. This is the pairing the two panels were
        // built for: the watch says WHEN, the frozen board says what
        // everything else was doing at the time — which is the state a test
        // record wants and the state that is gone three hundred frames later.
        if (m_pinPanel) { m_pinPanel->freezeNow(tr("watch: %1").arg(label)); }
        // Raise the panel so the evidence is one glance away rather than one
        // hunt through the Panels menu.
        if (m_watchDock) { m_watchDock->show(); m_watchDock->raise(); }
    });

    connect(m_watchPanel, &WatchPanel::revealRequested,
            this, [this](const LogEntryPtr &e, const QString &src) {
        if (!e.isNull()) { jumpToEntry(src, e->epochMs); }
    });

    // A watch with the bookmark action (session 82): the frame that fired it
    // is bookmarked with the watch's name as the note. Never un-bookmarked:
    // toggle() on an existing bookmark would remove it.
    connect(m_watchPanel, &WatchPanel::bookmarkRequested,
            this, [this](const LogEntryPtr &e, const QString &src, const QString &note) {
        if (e.isNull() || m_bookmarks.has(src, e->epochMs)) return;
        m_bookmarks.toggle(src, e->epochMs, e->text.left(120));
        for (int i = 0; i < m_bookmarks.count(); ++i) {
            const Bookmark &b = m_bookmarks.all().at(i);
            if (b.tabKey == src && b.epochMs == e->epochMs) { m_bookmarks.setNote(i, tr("watch: %1").arg(note)); break; }
        }
        e->bookmarked = true;
    });

    // Fill the chooser whenever the panel is opened or the tab changes: the
    // "seen here" half is per-tab, and an empty chooser was the state this
    // panel shipped in — every name had to be typed from memory.
    connect(m_pinDock, &QDockWidget::visibilityChanged,
            this, [this](bool v) { if (v) { refreshPinChoices(); } });

    connect(m_pinPanel, &PinPanel::plotRequested,
            this, [this](const QString &field, const QString &key) {
        // Plot it against the source the pin is following, not against
        // whichever tab happens to be in front.
        plotFieldIn(key, field);
    });

    // -----------------------------------------------------------------
    //  Load configuration files (color rules + friendly names).
    // -----------------------------------------------------------------
    const QString appDir = QCoreApplication::applicationDirPath();
    m_colorRules.loadFromFile(appDir + "/color_rules.json");
    m_nameMap   .loadFromFile(appDir + "/friendly_names.csv");
    m_rawPanel->setNameMap(&m_nameMap);

    // -----------------------------------------------------------------
    //  Rolling disk log writer (2b).
    // -----------------------------------------------------------------
    // Bookmarks: load before any traffic arrives so applyToModel() can
    // re-flag a reloaded session's rows.
    // Field catalogue: names, packets and symbolic values for the query
    // language. An absent file is not an error — raw field names still work.
    {
        const QString fm = QCoreApplication::applicationDirPath()
                           + QStringLiteral("/fieldmap.json");
        QString ferr;
        if (QFile::exists(fm) && FieldCatalog::instance().load(fm, &ferr)) {
            qInfo("Loaded %d field definitions from %s",
                  FieldCatalog::instance().fieldCount(), qPrintable(fm));
        } else if (QFile::exists(fm)) {
            qWarning("fieldmap.json: %s", qPrintable(ferr));
        }
    }

    m_bookmarks.load();

    connect(&m_tests, &TestAssertionEngine::assertionObserved,
            this,     &MainWindow::onAssertionObserved);
    connect(&m_tests, &TestAssertionEngine::assertionViolated,
            this,     &MainWindow::onAssertionViolated);
    // Auto-load testcases.json if it sits beside the binary, so a run does
    // not begin with a menu hunt.
    {
        const QString def = QCoreApplication::applicationDirPath()
                            + QStringLiteral("/testcases.json");
        QString terr;
        if (QFile::exists(def) && m_tests.load(def, &terr)) {
            qInfo("Loaded %d test case(s) from %s",
                  m_tests.assertions().size(), qPrintable(def));
        }
    }

    // Field inspector: schema decode of the selected row, byte-linked to
    // the hex panel. Tabbed with the raw-bytes dock rather than stacked, so
    // the two views of the same frame share the screen space.
    m_fieldPanel = new FieldInspector(this);
    m_fieldPanel->setDecoder(&kavachSchema());
    // Shared, so a loco identified while reading a log tab is still
    // identified in a compare pane.
    m_fieldPanel->setLocoIdentity(&m_locoIdentity);
    m_fieldDock = new QDockWidget(tr("Decoded fields"), this);
    m_fieldDock->setObjectName("fieldDock");
    m_fieldDock->setWidget(m_fieldPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_fieldDock);
    if (m_rawDock) tabifyDockWidget(m_rawDock, m_fieldDock);

    // The field inspector knows which field was right-clicked; this window
    // knows which tab it came from and where the pin board and the plot live.
    //
    // Wired HERE, immediately after the inspector is constructed, and not
    // fifty lines earlier with the rest of the dock wiring. That is where
    // these two used to be, where m_fieldPanel was still null, so both
    // connects failed at startup and "Pin this field" and "Plot this field"
    // silently did nothing for the whole life of the program. Qt says so on
    // stderr — "QObject::connect(FieldInspector, MainWindow): invalid
    // nullptr parameter" — which nobody reads in a release build.
    connect(m_fieldPanel, &FieldInspector::pinFieldRequested,
            this, &MainWindow::pinField);
    connect(m_fieldPanel, &FieldInspector::plotFieldRequested,
            this, &MainWindow::plotField);
    connect(m_fieldPanel, &FieldInspector::locateFieldRequested,
            this, [this](const QString &f) { showFieldIndex(f); });

    connect(m_fieldPanel, &FieldInspector::byteRangeSelected,
            this, [this](int from, int to) {
                if (m_rawPanel) m_rawPanel->highlightBytes(from, to);
            });
    connect(m_fieldPanel, &FieldInspector::decodeFailed,
            this,         &MainWindow::onDecodeFailed);

    // Decode failures. Hidden until something actually fails, so it costs
    // nothing on a healthy system but is impossible to miss when a schema
    // gap appears.
    m_failList = new QListWidget(this);
    EmptyState::attach(m_failList, tr("No decode failures.\n"
                                      "Frames that cannot be read appear here."));
    m_failList->setAlternatingRowColors(true);
    m_failDock = new QDockWidget(tr("Decode failures"), this);
    m_failDock->setObjectName("failDock");
    m_failDock->setWidget(m_failList);
    addDockWidget(Qt::BottomDockWidgetArea, m_failDock);
    m_failDock->hide();
    connect(m_failList, &QListWidget::currentRowChanged,
            this,       &MainWindow::onDecodeFailureActivated);

    // ---- source list -------------------------------------------------
    m_sourceFilter = new QLineEdit;
    m_sourceFilter->setPlaceholderText(tr("Filter sources…"));
    m_sourceFilter->setClearButtonEnabled(true);

    m_sourceList = new QTreeWidget;
    EmptyState::attach(m_sourceList, tr("No sources seen yet."));
    m_sourceList->setColumnCount(3);
    m_sourceList->setHeaderLabels({ tr("Source"), tr("Msgs"), tr("Silent") });
    m_sourceList->setRootIsDecorated(false);
    m_sourceList->setUniformRowHeights(true);
    m_sourceList->setAlternatingRowColors(true);
    m_sourceList->setSortingEnabled(true);
    m_sourceList->sortByColumn(0, Qt::AscendingOrder);
    // Fixed widths of 150 + 60 + default overflowed a dock narrower than
    // ~280px, which is where this dock naturally sits: the headers read
    // "Source | Msg: | Si" with a horizontal scrollbar under them. Let the
    // name take the slack and size the two numeric columns to their content,
    // so all three stay readable at any dock width.
    // Source name takes the slack; the two counters are fixed and narrow.
    // ResizeToContents on the counters was worse than the fixed widths it
    // replaced — the styled header text is wide, so the two of them ate
    // 190 of a 250px dock and left the source name reading "Sou".
    auto *sourceHeader = m_sourceList->header();
    sourceHeader->setSectionResizeMode(0, QHeaderView::Stretch);
    sourceHeader->setSectionResizeMode(1, QHeaderView::Fixed);
    sourceHeader->setSectionResizeMode(2, QHeaderView::Fixed);
    sourceHeader->setStretchLastSection(false);
    sourceHeader->setMinimumSectionSize(48);
    // Wide enough for the styled header text: the section padding is 8px a
    // side and the header is semibold, so 56 clipped "Msgs" to "Msg".
    m_sourceList->setColumnWidth(1, 64);
    m_sourceList->setColumnWidth(2, 74);

    auto *sourcePanel = new QWidget;
    auto *sourceLayout = new QVBoxLayout(sourcePanel);
    sourceLayout->setContentsMargins(2, 2, 2, 2);
    sourceLayout->setSpacing(2);
    sourceLayout->addWidget(m_sourceFilter);
    sourceLayout->addWidget(m_sourceList, 1);

    m_sourceDock = new QDockWidget(tr("Sources"), this);
    m_sourceDock->setObjectName("sourceDock");
    m_sourceDock->setWidget(sourcePanel);
    m_sourceDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, m_sourceDock);

    connect(m_sourceList, &QTreeWidget::itemActivated,
            this,         &MainWindow::onSourceActivated);
    connect(m_sourceList, &QTreeWidget::itemDoubleClicked,
            this,         &MainWindow::onSourceActivated);
    connect(m_sourceFilter, &QLineEdit::textChanged,
            this,           &MainWindow::onSourceFilterChanged);

    m_bookmarkList = new QListWidget(this);
    EmptyState::attach(m_bookmarkList,
                       tr("No bookmarks.\nCtrl+B marks the selected row."));
    m_bookmarkList->setAlternatingRowColors(true);
    m_bookmarkList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_bookmarkDock = new QDockWidget(tr("Bookmarks"), this);
    m_bookmarkDock->setObjectName("bookmarkDock");
    m_bookmarkDock->setWidget(m_bookmarkList);
    m_bookmarkDock->setAllowedAreas(Qt::RightDockWidgetArea
                                    | Qt::LeftDockWidgetArea
                                    | Qt::BottomDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, m_bookmarkDock);
    m_bookmarkDock->hide();          // opt-in; shown by the View menu

    connect(&m_bookmarks, &BookmarkStore::changed,
            this,         &MainWindow::onBookmarksChanged);
    connect(m_bookmarkList, &QListWidget::currentRowChanged,
            this,           &MainWindow::onBookmarkActivated);
    connect(m_bookmarkList, &QWidget::customContextMenuRequested,
            this,           &MainWindow::onBookmarkContextMenu);
    onBookmarksChanged();

    applyDiskLoggingSetting(Settings::diskLoggingEnabled());

    // Last thing in the constructor: every dock and toolbar restoreState()
    // needs to match must already exist, or Qt silently drops the ones it
    // cannot find and the layout comes back half-applied.
    restoreLayout();

    // Debounce: 2s after the last change. Long enough that dragging a dock
    // writes once rather than per pixel, short enough to survive a kill.
    m_layoutSaveTimer = new QTimer(this);
    m_layoutSaveTimer->setSingleShot(true);
    m_layoutSaveTimer->setInterval(2000);
    connect(m_layoutSaveTimer, &QTimer::timeout, this, &MainWindow::saveLayout);

    for (QDockWidget *d : { m_rawDock, m_fieldDock, m_failDock,
                           m_bookmarkDock, m_sourceDock }) {
        if (!d) continue;
        connect(d, &QDockWidget::dockLocationChanged,
                this, [this](Qt::DockWidgetArea) { scheduleLayoutSave(); });
        connect(d, &QDockWidget::visibilityChanged,
                this, [this](bool) { scheduleLayoutSave(); });
        connect(d, &QDockWidget::topLevelChanged,
                this, [this](bool) { scheduleLayoutSave(); });
    }


    // -----------------------------------------------------------------
    //  Pipeline.
    // -----------------------------------------------------------------
    const quint16 udp_port  = Settings::udpPort();
    const int     queue_cap = Settings::queueCapacity();

    m_receiver   = new UDPCommunication(udp_port, queue_cap, this);
    m_dispatcher = new MessageDispatcher(this);
    connect(m_dispatcher, &MessageDispatcher::entryAppended, m_statusPins, &StatusPins::onEntry);

    m_dispatcher->setColorRules(&m_colorRules);
    m_dispatcher->setNameMap   (&m_nameMap);
    m_dispatcher->setPerTabCapacity(m_perTabCapacity);
    m_dispatcher->setShowUtc(Settings::showUtc());
    m_dispatcher->setTheme(m_theme);            // (2i) push to all future models
    m_dispatcher->attachReceiver(m_receiver);

    connect(m_dispatcher, &MessageDispatcher::tabRequested,
            this,         &MainWindow::onTabRequested);
    connect(m_dispatcher, &MessageDispatcher::entryAppended,
            this,         &MainWindow::onEntryAppended);
    connect(m_dispatcher, &MessageDispatcher::entriesAppended,
            this,         &MainWindow::onEntriesAppended);

    // The tabs from the last run, now that the dispatcher they need exists.
    //
    // This was called right after restoreLayout(), which reads as the right
    // place — "restore the window, then restore its tabs" — and is thirty
    // lines too early: buildOrShowTab() asks m_dispatcher for the tab's
    // model, and the dispatcher is not constructed until here. It crashed on
    // the first start after a session that had tabs open, which is why it
    // survived testing: a console that had never seen traffic saved an empty
    // workspace and restored nothing.
    restoreWorkspace();

    // Undo, layouts, settings transfer, colour-blind colours, crash-recovery
    // snapshots (session 79; mainwindowsession.cpp). After the docks and
    // the status pins exist, which it wires its undo into.
    setupSessionFeatures(menus.file, menus.edit, menus.find, menus.view, menus.theme);

    connect(m_receiver, &UDPCommunication::bindSucceeded,
            this,       &MainWindow::onBindSucceeded);
    connect(m_receiver, &UDPCommunication::bindFailed,
            this,       &MainWindow::onBindFailed);

    m_receiver->start();

    // -----------------------------------------------------------------
    //  Periodic status update + last-seen check (2f).
    // -----------------------------------------------------------------
    m_statusTimer = new QTimer(this);
    m_statusTimer->setInterval(1000);
    connect(m_statusTimer, &QTimer::timeout,
            this,          &MainWindow::onStatusTick);
    m_statusTimer->start();

    m_userLabelTimer = new QTimer(this);
    m_userLabelTimer->setSingleShot(true);
    m_userLabelTimer->setInterval(5000);
    connect(m_userLabelTimer, &QTimer::timeout,
            this,             &MainWindow::onUserLabelTimeout);
}

MainWindow::~MainWindow()
{
    // Reaching the destructor is not a crash either (a crash runs neither
    // this nor closeEvent), so the run is marked finished here too.
    SessionRecovery::markCleanExit(SessionRecovery::defaultPath());

    // Stop the receiver first — once it stops, no new entries can be queued
    // for the writer. Then flush+stop the writer. Finally tear down the UI.
    if (m_receiver) {
        m_receiver->quit();
        if (!m_receiver->wait(2000)) {
            qWarning() << "MainWindow: UDP receiver did not stop in time.";
        }
    }
    if (m_writer) {
        // stop() returns false if the worker is STILL RUNNING, in which
        // case deleting it is a fatal Qt error. Leak it instead: it owns
        // its own NameMap copy and holds no pointer back into us, so a
        // leaked writer at process exit is harmless. ~LogWriter's own
        // terminate() fallback is the backstop if it ever is deleted.
        if (m_writer->stop(5000)) {
            delete m_writer;    // not parented to us; clean up explicitly
        } else {
            qWarning() << "MainWindow: log writer still running at shutdown; "
                          "leaking it rather than deleting a live QThread.";
        }
        m_writer = nullptr;
    }
    delete ui;
}


void MainWindow::onEntryAppended(QString /*tabKey*/, LogEntryPtr /*entry*/)
{
    // Nothing per-entry left to do here. The disk hop moved to
    // onEntriesAppended so it can use the batched enqueue — it was the
    // last thing still paying a per-message mutex + condvar wake on the
    // GUI thread, which defeated the point of the batching above it.
    //
    // The signal itself stays: TabPopoutWindow and friends subscribe
    // to it, and their work genuinely is per-entry.
}

void MainWindow::onEntriesAppended(QString tabKey, QVector<LogEntryPtr> entries)
{
    // Batched UI update. Fires ~once every 30ms per tab with new
    // traffic. All the heavy per-entry work that used to run on the
    // GUI thread (scrollToBottom × N, updateLogCount × N, show-hidden-
    // tab check × N, lastSeenMs update × N) is now done ONCE per batch,
    // regardless of how many entries it contains. This is the fix for
    // the "thousand-packet burst hangs the UI" problem.
    if (entries.isEmpty()) return;

    // Pinned fields, from the LAST entry of the batch only. Decoding every
    // entry here would put schema decoding on the ingest path, which is the
    // one place in this program that must not get slower — the batching above
    // exists for exactly that reason. See PinBoard for what the sampling
    // costs and why it does not matter for the fields anyone pins.
    if (m_pinPanel && m_pinDock && m_pinDock->isVisible()
        && m_pinPanel->board().count() > 0) {
        m_pinPanel->observe(entries.last(), tabKey);
    }

    // Watches see EVERY entry, and regardless of whether the panel is open.
    //
    // Both differ from the pin board deliberately. A pinned value only has to
    // be current, so sampling the batch is free; a watch exists for the one
    // frame where the condition held, and a sampled watch would miss exactly
    // the event it was armed for. And a watch whose panel is closed is still
    // armed — closing a window is not disarming, and a watch that quietly
    // stopped watching would be worse than no watch.
    if (m_watchPanel && m_watchPanel->list().count() > 0) {
        for (const LogEntryPtr &e : entries) {
            m_watchPanel->observe(e, tabKey);
        }
    }

    // Disk first, and unconditionally: this must not depend on whether a
    // tab UI happens to exist for the key, otherwise the early return
    // below would silently stop persisting a source.
    if (m_writer) {
        m_writer->enqueueBatch(entries);
    }

    // Assertions see every entry, including sources with no tab open — a
    // test condition must not depend on which tab happens to be visible.
    for (const LogEntryPtr &e : entries) { m_tests.observe(e); }

    // The session-key store watches for @auth_keys / @rand_num to derive the
    // live session key used for MAC verification (cheap prefix gate inside).
    for (const LogEntryPtr &e : entries) { SessionKeyStore::instance().observe(e); }

    // ARP and LSRP carry the frame number the equipment is actually using.
    // The Packet Maker follows it so a built frame is not minutes away from
    // the live counter — see framenumberwatch.h.
    for (const LogEntryPtr &e : entries) { FrameNumberWatch::instance().observe(e); }

    // A running field sweep scores each value it sent by what came back, so it
    // needs every entry, not just the visible tab's. It ignores them unless a
    // step is actually in flight.
    if (m_fieldSweep) {
        for (const LogEntryPtr &e : entries) { m_fieldSweep->observe(e); }
    }

    auto it = m_tabs.find(tabKey);
    if (it == m_tabs.end()) return;

    // Mark the visuals stale rather than rebuilding here: each rebuilds
    // behind its own timer, so a burst costs one rebuild rather than one
    // per batch.
    if (it->ribbon) it->ribbon->refreshLater();
    if (it->marks)  it->marks->refreshLater();

    // last-seen: take the timestamp of the LAST entry in the batch.
    // Entries within a batch all share a near-identical timestamp
    // (single drainBatch tick) so picking any of them is fine; last
    // is conventional.
    it->lastSeenMs = entries.last()->epochMs;

    // Show tab if previously hidden — once per batch, not per entry.
    if (!it->visible) {
        showTab(*it);
    }

    // Scroll to bottom — once per batch. Without batching this used to
    // be N calls in a row, each potentially triggering a repaint, which
    // was the primary cause of the UI hang on heavy traffic.
    //
    // Suppressed while the tab's find bar is parked on a match. Scroll
    // lock and Find both want to decide where the viewport sits, and with
    // traffic arriving the two used to alternate — Find scrolled to the
    // match, the next batch scrolled to the tail, thirty times a second.
    // A request to look at one specific row is the more specific request,
    // so it wins; the bar says "following paused" so the stopped tab is
    // explained, and closing it (Esc) hands the view back.
    const bool findHoldsView = it->findBar && it->findBar->holdsView();
    if (it->scrollLock && it->view && !findHoldsView) {
        it->view->scrollToBottom();
    }

    // Count update — once per batch. updateLogCount reads model->count
    // and renders one QLabel; cheap, but multiplying by 1000 was wasteful.
    updateLogCount();
}


void MainWindow::rebuildSourceList()
{
    if (!m_sourceList) return;

    // Add only what is missing rather than clearing and refilling: a full
    // rebuild every second would fight the user's selection, scroll
    // position and sort order.
    QSet<QString> present;
    for (int i = 0; i < m_sourceList->topLevelItemCount(); ++i) {
        present.insert(m_sourceList->topLevelItem(i)->data(0, Qt::UserRole).toString());
    }

    for (auto it = m_tabs.constBegin(); it != m_tabs.constEnd(); ++it) {
        if (present.contains(it.key())) continue;
        auto *item = new QTreeWidgetItem(m_sourceList);
        item->setData(0, Qt::UserRole, it.key());
        item->setText(0, it->friendlyName.isEmpty() ? it.key() : it->friendlyName);
        item->setToolTip(0, it.key());
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
    }

    if (!m_sourceFilter->text().isEmpty()) {
        onSourceFilterChanged(m_sourceFilter->text());
    }
}

void MainWindow::refreshSourceList()
{
    if (!m_sourceList) return;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 warnMs = qint64(m_offlineWarnSec) * 1000;
    const qint64 errMs  = qint64(m_offlineErrSec)  * 1000;

    for (int i = 0; i < m_sourceList->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_sourceList->topLevelItem(i);
        const QString key = item->data(0, Qt::UserRole).toString();
        auto tabIt = m_tabs.constFind(key);
        if (tabIt == m_tabs.constEnd()) continue;

        LogModel *model = m_dispatcher->modelForKey(key);
        item->setText(1, model ? QString::number(model->count()) : QString());

        // Silence, in whole seconds. The same thresholds as the tab-title
        // colouring, read from the same members, so the two can never
        // disagree about whether a source is late.
        QString silent;
        QColor colour;
        if (tabIt->lastSeenMs > 0) {
            const qint64 gap = nowMs - tabIt->lastSeenMs;
            silent = QStringLiteral("%1s").arg(gap / 1000);
            // A source that has gone quiet is an error or a warning like any
            // other; these were two hand-picked pairs saying the same thing
            // in slightly different colours.
            if (m_offlineErrSec > 0 && gap >= errMs) {
                colour = UiColor::error();
            } else if (m_offlineWarnSec > 0 && gap >= warnMs) {
                colour = UiColor::warning();
            }
        } else {
            silent = QStringLiteral("—");
        }
        item->setText(2, silent);
        for (int c = 0; c < 3; ++c) item->setForeground(c, colour.isValid()
                                                              ? QBrush(colour)
                                                              : QBrush());

        // Italic marks a source whose tab is closed: it still exists and is
        // still receiving, you just are not looking at it.
        QFont f = item->font(0);
        f.setItalic(!tabIt->visible);
        for (int c = 0; c < 3; ++c) item->setFont(c, f);
    }
}


QVector<LogEntryPtr> MainWindow::selectedEntries(QTableView *view,
                                                 const QString &tabKey) const
{
    QVector<LogEntryPtr> out;
    if (!view || !view->selectionModel()) return out;

    LogModel *model = m_dispatcher->modelForKey(tabKey);
    auto it = m_tabs.constFind(tabKey);
    if (!model || it == m_tabs.constEnd() || !it->filterBar) return out;

    auto *proxy = it->filterBar->proxyModel();

    // selectedRows() comes back in selection order, which is the order the
    // user clicked. A report wants time order, so sort by the PROXY row —
    // that is what "as displayed" means, and it stays right when the view
    // is sorted by something other than time.
    QModelIndexList rows = view->selectionModel()->selectedRows();
    std::sort(rows.begin(), rows.end(),
              [](const QModelIndex &a, const QModelIndex &b) {
                  return a.row() < b.row();
              });

    out.reserve(rows.size());
    for (const QModelIndex &idx : rows) {
        const QModelIndex src = proxy ? proxy->mapToSource(idx) : idx;
        LogEntryPtr e = model->entryAt(src.row());
        if (e) out.append(e);
    }
    return out;
}

void MainWindow::onLogRowContextMenu(const QPoint &pos)
{
    auto *view = qobject_cast<QTableView*>(sender());
    if (!view) return;

    const QModelIndex idx = view->indexAt(pos);
    if (!idx.isValid()) return;

    const QString key = currentTabKey();
    auto it = m_tabs.find(key);
    if (it == m_tabs.end() || !it->filterBar) return;

    LogModel *model = m_dispatcher->modelForKey(key);
    if (!model) return;

    auto *proxy = it->filterBar->proxyModel();
    const QModelIndex src = proxy ? proxy->mapToSource(idx) : idx;
    LogEntryPtr entry = model->entryAt(src.row());
    if (!entry) return;

    // Everything selected, in view order — not just the row under the
    // cursor. Right-clicking inside a selection and getting only one row
    // would silently discard the span the user just made.
    const QVector<LogEntryPtr> selected = selectedEntries(view, key);
    const int n = qMax(1, selected.size());

    QMenu menu(this);
    QAction *actCopy     = menu.addAction(
        n > 1 ? tr("&Copy %1 messages").arg(n) : tr("&Copy message"));
    actCopy->setShortcut(QKeySequence::Copy);
    QAction *actCopyRow  = menu.addAction(
        n > 1 ? tr("Copy %1 &rows (tab-separated)").arg(n)
              : tr("Copy &row (tab-separated)"));
    actCopyRow->setShortcut(QKeySequence("Ctrl+Alt+C"));
    QAction *actCopyHdr  = menu.addAction(tr("Copy rows &with header"));
    menu.addSeparator();
    QAction *actBookmark = menu.addAction(
        entry->bookmarked ? tr("Remove &bookmark") : tr("Add &bookmark"));
    menu.addSeparator();
    // Diff needs exactly two rows: it compares A against B, and quietly
    // picking two out of five would be a guess at which the operator meant.
    const QVector<LogEntryPtr> sel = selectedEntries(it->view, key);
    // Named for how many are actually selected: "Diff these two rows" on a
    // four-row selection would be telling the operator the wrong thing about
    // what is about to happen.
    QAction *actDiff = menu.addAction(
        sel.size() > 2 ? tr("&Diff these %1 rows").arg(sel.size())
                       : tr("&Diff these two rows"));
    actDiff->setEnabled(sel.size() >= 2 && sel.size() <= FrameDiffWindow::maxFrames());
    if (sel.size() != 2) {
        actDiff->setToolTip(tr("Select exactly two rows to compare them."));
    }
    QAction *actToWb     = menu.addAction(tr("Open in Decode Workbenc&h"));
    QAction *actToPm     = menu.addAction(tr("Open in Packet Ma&ker"));
    // Both act on the row under the cursor, not on the whole selection:
    // each target window holds exactly one frame, so a multi-row selection
    // has no meaning here and quietly using the first row would be a lie.
    {
        const bool haveBuffer = !entryBufferText(entry).isEmpty();
        actToWb->setEnabled(haveBuffer);
        actToPm->setEnabled(haveBuffer);
        if (!haveBuffer) {
            const QString why = tr("This row carries no bytes — nothing to decode.");
            actToWb->setToolTip(why);
            actToPm->setToolTip(why);
        }
    }
    menu.addSeparator();
    QAction *actWhy      = menu.addAction(tr("&Why this colour?"));
    QAction *actFilterSrc = menu.addAction(
        tr("Filter to this source (%1)").arg(key));

    QAction *chosen = menu.exec(view->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == actCopy) {
        QVector<LogEntryPtr> rows = selected;
        if (rows.isEmpty()) rows.append(entry);
        QApplication::clipboard()->setText(formatMessagesForClipboard(rows));
    } else if (chosen == actCopyRow || chosen == actCopyHdr) {
        QVector<LogEntryPtr> rows = selected;
        if (rows.isEmpty()) rows.append(entry);
        QApplication::clipboard()->setText(
            formatEntriesForClipboard(rows, chosen == actCopyHdr,
                                      Settings::showUtc()));
        notify(NoteLevel::Info, tr("Copied %1 row(s)").arg(rows.size()));
    } else if (chosen == actBookmark) {
        onActionToggleBookmark();
    } else if (chosen == actWhy) {
        // The affordance that would have made the shipped colour_rules.json
        // bug obvious: decorative RAD/CAN rules were pinning severity to
        // info, so real errors rendered as info and nothing on screen said
        // which rule was responsible.
        QMessageBox box(this);
        box.setWindowTitle(tr("Colour rule attribution"));
        box.setIcon(QMessageBox::NoIcon);
        box.setText(tr("How this row was classified:"));
        box.setInformativeText(entry->text);
        box.setDetailedText(m_colorRules.explainClassification(entry->text));
        box.exec();
    } else if (chosen == actFilterSrc) {
        it->filterBar->setQuery(QStringLiteral("src:%1").arg(key));
    } else if (chosen == actDiff) {
        openFrameDiff(sel);
    } else if (chosen == actToWb) {
        openEntryInWorkbench(entry);
    } else if (chosen == actToPm) {
        openEntryInPacketMaker(entry);
    }
}

void MainWindow::onSourceActivated(QTreeWidgetItem *item, int /*column*/)
{
    if (!item) return;
    const QString key = item->data(0, Qt::UserRole).toString();
    auto it = m_tabs.find(key);
    if (it == m_tabs.end()) return;

    // Opening a closed tab is the point of the list — otherwise a source
    // you closed becomes unreachable except through the Window menu.
    if (!it->visible) showTab(*it);

    const int idx = ui->tabWidget->indexOf(it->container);
    if (idx >= 0) ui->tabWidget->setCurrentIndex(idx);
}

void MainWindow::onSourceFilterChanged(const QString &text)
{
    const QString needle = text.trimmed();
    for (int i = 0; i < m_sourceList->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_sourceList->topLevelItem(i);
        item->setHidden(!NameMap::matchesFilter(
            item->text(0), item->data(0, Qt::UserRole).toString(), needle));
    }
}


// -----------------------------------------------------------------------
//  UI handlers
// -----------------------------------------------------------------------
void MainWindow::on_cbDatetime_stateChanged(int)
{
    // Toggle the Time column's visibility on every existing tab.
    const bool show = ui->cbDatetime->isChecked();
    for (const TabUi &t : qAsConst(m_tabs)) {
        if (t.view) t.view->setColumnHidden(LogModel::ColTime, !show);
    }
}

void MainWindow::on_cbScrollLock_stateChanged(int)
{
    const QString currentKey = currentTabKey();
    if (currentKey.isEmpty()) return;

    auto it = m_tabs.find(currentKey);
    if (it == m_tabs.end()) return;
    it->scrollLock = ui->cbScrollLock->isChecked();
    if (it->findBar) it->findBar->setScrollLockActive(it->scrollLock);
    // Turning follow back on should go to the end now rather than at the
    // next batch — unless Find is holding the view, in which case the hold
    // stands until the bar is closed.
    if (it->scrollLock && it->view
        && !(it->findBar && it->findBar->holdsView())) {
        it->view->scrollToBottom();
    }
}

void MainWindow::on_pbSaveBtn_clicked()
{
    const QString currentKey = currentTabKey();
    if (currentKey.isEmpty()) return;

    LogModel *model = m_dispatcher->modelForKey(currentKey);
    if (!model) return;

    const QDateTime now = QDateTime::currentDateTime();
    const QString dateStr = now.date().toString("ddMMyyyy");
    const QString timeStr = now.time().toString("HHmmss");

    const QString friendly = m_dispatcher->friendlyNameFor(currentKey);
    const QString fileName = QString("%1_%2_%3.log").arg(friendly, dateStr, timeStr);
    const QString filepath = QString("%1%2%3").arg("SAVED_LOGS",
                                                   QDir::separator(),
                                                   fileName);

    // Source/kvch from the tab key, so the .dlr identifies itself when
    // reopened. The key is authoritative here; the first snapshot row might
    // be a synthetic banner with a zeroed header.
    quint8  srcId = 0;
    quint16 kvId  = 0;
    const int us = currentKey.indexOf(QLatin1Char('_'));
    if (us > 0) {
        srcId = static_cast<quint8> (currentKey.left(us).toInt());
        kvId  = static_cast<quint16>(currentKey.mid(us + 1).toInt());
    }

    auto *worker = new SaveData(filepath, friendly, model->snapshot(),
                                srcId, kvId, Settings::saveIncludesRaw());
    connect(worker, &SaveData::saveFinished, this, &MainWindow::onSaveFinished);
    connect(worker, &SaveData::saveFailed,   this, &MainWindow::onSaveFailed);
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void MainWindow::onSaveFinished(const QString &filePath,
                                const QString &rawPath,
                                int rawRecords)
{
    if (!rawPath.isEmpty()) {
        notify(NoteLevel::Info,
               tr("Saved log and %1 raw record(s)").arg(rawRecords),
               tr("%1\n%2\n\nThe .dlr can be reopened with "
                  "File > Open recorded session.")
                   .arg(filePath, rawPath));
    } else {
        // Say WHY there is no sidecar. Silence would leave the operator to
        // discover the absence later, when they try to reopen it.
        notify(NoteLevel::Info,
               Settings::saveIncludesRaw()
                   ? tr("Log saved — no raw bytes in this tab, so no .dlr")
                   : tr("Log saved — 'include raw datagrams when saving' "
                        "is off, so no .dlr"),
               filePath);
    }
}

void MainWindow::onSaveFailed(const QString &filePath, const QString &reason)
{
    // Failure has to be as visible as success, otherwise the operator walks
    // away believing the log was captured.
    QMessageBox::warning(this, tr("Save failed"),
                         tr("Could not write the log to:\n%1\n\n%2")
                             .arg(filePath, reason));
}


void MainWindow::on_pbCheckBuffer_clicked()
{
    if (ui->stackedWidget->currentIndex() == 0) {
        ui->stackedWidget->setCurrentIndex(1);
        ui->pbCheckBuffer->setText(tr("Back to log"));
    } else {
        ui->stackedWidget->setCurrentIndex(0);
        ui->pbCheckBuffer->setText(tr("Check buffer"));
    }
}

void MainWindow::on_pbConvert_clicked()
{
    ui->teToString->setText(hexToAsciiString(ui->textEdit->toPlainText()));
}

QString MainWindow::hexToAsciiString(const QString &hexData) const
{
    // Section 1e fix preserved: prepend (not append) on odd-length input.
    QString cleaned = hexData;
    cleaned.remove(QRegularExpression("[^0-9A-Fa-f]"));
    if (cleaned.size() % 2 != 0) {
        cleaned.prepend('0');
    }
    const QByteArray bytes = QByteArray::fromHex(cleaned.toUtf8());
    QString ascii;
    ascii.reserve(bytes.size());
    for (char b : bytes) {
        ascii.append((b >= 32 && b <= 126) ? QChar(b) : QChar('.'));
    }
    return ascii;
}


bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // The frame-clock labels (session 82): a click opens their history.
    if ((watched == m_lblFrameClock || watched == m_lblStationClock || watched == m_lblClockGap)
        && event->type() == QEvent::MouseButtonRelease
        && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
        showClockHistory();
        return true;
    }
    QTabBar *bar = ui->tabWidget->tabBar();
    if (watched != bar) {
        return QMainWindow::eventFilter(watched, event);
    }
    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            m_tabDragIndex = bar->tabAt(mouse->pos());
            m_tabDragStart = mouse->pos();
        }
    } else if (event->type() == QEvent::MouseButtonRelease) {
        m_tabDragIndex = -1;
    } else if (event->type() == QEvent::MouseMove && m_tabDragIndex >= 0) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        // Out of the bar by a clear margin, up or down: that is "pull it
        // out", not "reorder along the bar" (which stays Qt's own).
        const int margin = 40;
        if ((mouse->buttons() & Qt::LeftButton)
            && (mouse->pos().y() < -margin || mouse->pos().y() > bar->height() + margin)) {
            QString key;
            QWidget *page = ui->tabWidget->widget(m_tabDragIndex);
            for (auto it = m_tabs.constBegin(); it != m_tabs.constEnd(); ++it) {
                if (it->container == page) {
                    key = it.key();
                }
            }
            m_tabDragIndex = -1;
            // End the bar's own drag-to-reorder cleanly before opening the window.
            QMouseEvent release(QEvent::MouseButtonRelease, m_tabDragStart, Qt::LeftButton,
                                Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(bar, &release);
            if (!key.isEmpty()) {
                popOutTab(key, mouse->globalPos());
            }
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}


// -----------------------------------------------------------------------
//  Helper: turn a proxy-model index (from the table view's selection
//  model) back into the LogEntry it represents. The view is bound to the
//  proxy, not the source LogModel, so we mapToSource() before fetching
//  the entry. Returns null on any anomaly (invalid index, no source row,
//  unknown tab) — the side panel handles null gracefully.
// -----------------------------------------------------------------------
void MainWindow::onActionNextProblem() { gotoProblem(+1); }
void MainWindow::onActionPrevProblem() { gotoProblem(-1); }

void MainWindow::gotoProblem(int dir)
{
    const QString key = currentTabKey();
    auto it = m_tabs.find(key);
    if (it == m_tabs.end() || !it->view) { return; }

    QAbstractItemModel *proxy = it->view->model();
    LogModel *source = m_dispatcher ? m_dispatcher->modelForKey(key) : nullptr;
    if (!proxy || !source) { return; }

    const QModelIndex cur = it->view->currentIndex();
    // From the top when nothing is selected and we are going forward, from
    // the bottom when going back — so the first press always finds the
    // nearest problem in the direction asked for rather than doing nothing.
    const int from = cur.isValid() ? cur.row()
                                   : (dir > 0 ? -1 : proxy->rowCount());

    const int row = nextMarkedRow(proxy, source, from, dir);
    if (row < 0) {
        // Saying so matters: silence here is indistinguishable from a key
        // that did not register.
        statusBar()->showMessage(dir > 0
            ? tr("No further errors or warnings below this row.")
            : tr("No errors or warnings above this row."), 3000);
        return;
    }

    const QModelIndex target = proxy->index(row, 0);
    it->view->setCurrentIndex(target);
    it->view->scrollTo(target, QAbstractItemView::PositionAtCenter);
    it->view->setFocus();
}

void MainWindow::offerDmiMoment(const QString &tabKey, const QModelIndex &proxyIndex)
{
    // Cheap unless a DMI window follows: the broker only stores the resolver.
    const auto *proxy = qobject_cast<const QSortFilterProxyModel*>(proxyIndex.model());
    if (!proxyIndex.isValid() || !proxy || !m_dispatcher) return;
    const QModelIndex src = proxy->mapToSource(proxyIndex);
    auto *model = qobject_cast<LogModel*>(proxy->sourceModel());
    if (!src.isValid() || !model) return;
    QVector<LogModel*> models;
    for (auto it = m_tabs.constBegin(); it != m_tabs.constEnd(); ++it) {
        if (LogModel *m = m_dispatcher->modelForKey(it.key())) models.append(m);
    }
    const QString name = m_tabs.value(tabKey).friendlyName;
    DmiTimeTravel::instance()->offer(
        this, dmiTabResolver(models, model, model->entryAt(src.row()), src.row(),
                             tr("tab %1").arg(name.isEmpty() ? tabKey : name)));
}

LogEntryPtr MainWindow::entryFromProxyIndex(const QModelIndex &proxyIndex) const
{
    if (!proxyIndex.isValid()) return LogEntryPtr();

    const auto *proxy = qobject_cast<const QSortFilterProxyModel*>(
        proxyIndex.model());
    if (!proxy) return LogEntryPtr();

    const QModelIndex srcIndex = proxy->mapToSource(proxyIndex);
    if (!srcIndex.isValid()) return LogEntryPtr();

    const auto *model = qobject_cast<const LogModel*>(proxy->sourceModel());
    if (!model) return LogEntryPtr();

    return model->entryAt(srcIndex.row());
}

// =============================================================================
//  Patch C — tab management (2e)
// =============================================================================


// =============================================================================
//  Patch C — Window menu (lists hidden tabs so the user can reopen them)
// =============================================================================


// =============================================================================
//  Patch C — Settings (2g)
// =============================================================================

void MainWindow::onActionSettings()
{
    SettingsDialog dlg(this);

    // Wire the live-applied signals through to MainWindow's reactors.
    connect(&dlg, &SettingsDialog::themeChanged,
            this, &MainWindow::onThemeChanged);
    connect(&dlg, &SettingsDialog::perTabCapacityChanged,
            this, &MainWindow::onPerTabCapacityChanged);
    connect(&dlg, &SettingsDialog::offlineThresholdsChanged,
            this, &MainWindow::onOfflineThresholdsChanged);
    connect(&dlg, &SettingsDialog::minFreeBytesChanged,
            this,  [this](qint64 n) {
                if (m_writer) m_writer->setMinFreeBytes(n);
            });
    connect(&dlg, &SettingsDialog::maxFolderBytesChanged,
            this,  [this](qint64 n) {
                if (m_writer) m_writer->setMaxFolderBytes(n);
            });
    connect(&dlg, &SettingsDialog::rawCaptureChanged,
            this,  [](bool) {
                // Applied when the writer is next created; changing it on a
                // running writer mid-file would leave a .dlr that starts or
                // stops partway through, which SessionReader would then have
                // to reason about. Toggling continuous logging off and on
                // picks it up cleanly.
            });
    connect(&dlg, &SettingsDialog::diskLoggingEnabledChanged,
            this,  &MainWindow::applyDiskLoggingSetting);
    connect(&dlg, &SettingsDialog::diskRotationBytesChanged,
            this, [this](qint64 bytes) {
                // Applied live now that LogWriter::setRotationBytes exists;
                // this used to be a restart-only setting.
                if (m_writer) m_writer->setRotationBytes(bytes);
            });

    dlg.exec();
}

void MainWindow::onThemeChanged(QString themeName)
{
    const Theme t = ThemeUtil::fromString(themeName);
    if (t == m_theme) return;
    m_theme = t;
    ThemeUtil::apply(t);

    // Remember the last theme of each kind, for the Dark/Light toggle, and
    // keep the Theme menu's tick where the theme actually is (the Settings
    // dialog can change it too).
    {
        QSettings settings(Settings::iniPath(), QSettings::IniFormat);
        if (ThemeUtil::isDark(t)) {
            settings.setValue(QStringLiteral("ui/lastDarkTheme"), QString::fromLatin1(ThemeUtil::toString(t)));
        } else {
            settings.setValue(QStringLiteral("ui/lastLightTheme"), QString::fromLatin1(ThemeUtil::toString(t)));
        }
    }
    for (QAction *item : m_themeActions) {
        item->setChecked(item->data().toString() == QLatin1String(ThemeUtil::toString(t)));
    }
    // The stylesheet is derived from the palette, so it has to be rebuilt
    // from the new one — a sheet left over from the old theme would win
    // over the palette everywhere it names a colour.
    UiStyle::apply();
    if (m_dispatcher) m_dispatcher->setTheme(t);

    // Re-apply staleness colors so they pick the right palette variant.
    refreshTabHealth();
    // Tag dots are drawn in the theme's tag colours.
    applyAllTabTags();
}

void MainWindow::onPerTabCapacityChanged(int newCap)
{
    m_perTabCapacity = newCap;
    if (m_dispatcher) m_dispatcher->setPerTabCapacity(newCap);

    // Apply to every existing model so the user sees the change live.
    // The dispatcher's setPerTabCapacity only affects models created
    // *after* the call; existing ones need an explicit setCapacity.
    for (auto it = m_tabs.constBegin(); it != m_tabs.constEnd(); ++it) {
        LogModel *m = m_dispatcher->modelForKey(it.key());
        if (m) m->setCapacity(newCap);
    }
}

void MainWindow::onOfflineThresholdsChanged(int warnSec, int errSec)
{
    m_offlineWarnSec = warnSec;
    m_offlineErrSec  = errSec;
    refreshTabHealth();
}

// =============================================================================
//  Patch C — Theme toggle action (View menu shortcut for 2i)
// =============================================================================

void MainWindow::onActionToggleTheme()
{
    // To the last-used theme of the other kind; Ayu Light / Dark the first time.
    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    Theme next = Theme::Dark;
    if (ThemeUtil::isDark(m_theme)) {
        next = ThemeUtil::fromString(settings.value(QStringLiteral("ui/lastLightTheme"),
                                                    QStringLiteral("light")).toString());
        if (ThemeUtil::isDark(next)) {
            next = Theme::Light;
        }
    } else {
        next = ThemeUtil::fromString(settings.value(QStringLiteral("ui/lastDarkTheme"),
                                                    QStringLiteral("dark")).toString());
        if (!ThemeUtil::isDark(next)) {
            next = Theme::Dark;
        }
    }
    Settings::setTheme(ThemeUtil::toString(next));
    onThemeChanged(ThemeUtil::toString(next));
}

// =============================================================================
//  Patch C — About
// =============================================================================

void MainWindow::onActionAbout()
{
    QMessageBox::about(this, tr("About DLConsole"),
        tr("<h3>DLConsole 1.0</h3>"
           "<p>Diagnostic console for the Kavach signalling backend.</p>"
           "<p>Listens on UDP/%1, displays one tab per "
           "(source_id, kvch_id) pair, rolls a per-source disk log "
           "under <code>%2</code>, and applies the colour rules "
           "from <code>color_rules.json</code> next to the exe.</p>"
           "<p>CRL-GAD &nbsp;·&nbsp; built with Qt %3</p>")
            .arg(Settings::udpPort())
            .arg(Settings::diskLogRoot())
            .arg(qVersion()));
}

// =============================================================================
//  Find (Ctrl+F)
// =============================================================================

void MainWindow::onActionFind()
{
    // Activate the FindBar of the currently-active tab. If no tab is
    // active, do nothing — user has nothing to find.
    const QString key = currentTabKey();
    if (key.isEmpty()) return;
    auto it = m_tabs.constFind(key);
    if (it == m_tabs.constEnd()) return;
    if (it->findBar) it->findBar->activate();
}

// =============================================================================
//  Go to timestamp (Ctrl+G)
//  -----------------------------------------------------------------------------
//  Navigate the current tab to the message nearest a chosen instant in time.
//
//  Operates on the PROXY, not the source model — same discipline as Find and
//  Export. If a filter is active, we land on the first VISIBLE row that
//  satisfies the request, never on a hidden one. That's what the operator
//  expects: "go to 14:30" should land somewhere they can actually see.
//
//  We resolve the target by a single LINEAR scan of proxy rows rather than a
//  binary search. Binary search would be O(log n) but only correct if proxy
//  rows are monotonic in time. They are today (no view enables sorting, so
//  the proxy preserves source/arrival order, which is time order). The moment
//  anyone turns on header-click sorting, a binary search would silently
//  return wrong rows. A linear scan over <=200k rows costs a few milliseconds
//  on a deliberate keypress and is correct under ANY sort/filter state. Cheap
//  insurance against a subtle future regression — fail-safe over fast.
// =============================================================================

void MainWindow::onActionGotoTimestamp()
{
    const QString key = currentTabKey();
    if (key.isEmpty()) return;
    auto it = m_tabs.constFind(key);
    if (it == m_tabs.constEnd() || !it->view) return;

    auto *proxy = qobject_cast<QSortFilterProxyModel*>(it->view->model());
    if (!proxy) return;
    auto *src = qobject_cast<const LogModel*>(proxy->sourceModel());
    if (!src) return;

    if (proxy->rowCount() <= 0) {
        notify(NoteLevel::Info, tr("No messages in this tab to go to."));
        return;
    }

    // The span and the resolution both live in GotoTimestamp now, shared with
    // the recorded-session window so the two cannot answer differently.
    const GotoTimestamp::Span span = GotoTimestamp::spanOf(proxy, src);
    if (!span.ok) return;

    // Seed from the row at the top of the viewport, so the dialog opens near
    // where the operator already is rather than at the start of the log.
    qint64 seedMs = span.minMs;
    const int topRow = it->view->rowAt(0);
    if (topRow >= 0) {
        const qint64 topMs = GotoTimestamp::epochAtProxyRow(proxy, src, topRow);
        if (topMs >= 0) seedMs = topMs;
    }

    GotoTimestampDialog dlg(seedMs, span.minMs, span.maxMs, this);
    if (dlg.exec() != QDialog::Accepted) return;

    const int bestRow = GotoTimestamp::resolveRow(proxy, src, dlg.targetMs(),
                                                  int(dlg.mode()));
    if (bestRow < 0) return;
    const qint64 bestMs = GotoTimestamp::epochAtProxyRow(proxy, src, bestRow);

    const QModelIndex landing = proxy->index(bestRow, LogModel::ColTime);
    it->view->setCurrentIndex(landing);
    it->view->scrollTo(landing, QAbstractItemView::PositionAtCenter);
    it->view->setFocus();

    notify(NoteLevel::Info, tr("Jumped to %1  (row %2 of %3)")
            .arg(QDateTime::fromMSecsSinceEpoch(bestMs).toString("HH:mm:ss.zzz"))
            .arg(bestRow + 1)
            .arg(proxy->rowCount()));
}

// =============================================================================
//  Export to CSV / JSON
//  -----------------------------------------------------------------------------
//  Both actions snapshot the PROXY of the current tab — meaning the export
//  honors any active filter. Worker thread (Exporter) does the I/O.
// =============================================================================

namespace {
// Small helper: build the snapshot of LogEntries in the proxy's row order
// for the currently-active tab. Returns an empty QVector if the tab has
// no entries or anything looks wrong. Lives in an anonymous namespace
// here because it's only used by the two export slots.
QVector<LogEntryPtr> snapshotProxy(QTableView *view)
{
    QVector<LogEntryPtr> out;
    if (!view) return out;
    auto *proxy = qobject_cast<const QSortFilterProxyModel*>(view->model());
    if (!proxy) return out;
    const auto *src = qobject_cast<const LogModel*>(proxy->sourceModel());
    if (!src) return out;

    const int rows = proxy->rowCount();
    out.reserve(rows);
    for (int r = 0; r < rows; ++r) {
        const QModelIndex pIdx = proxy->index(r, 0);
        const QModelIndex sIdx = proxy->mapToSource(pIdx);
        LogEntryPtr e = src->entryAt(sIdx.row());
        if (e) out.append(e);
    }
    return out;
}
}  // anonymous namespace

void MainWindow::onActionExport()
{
    const QString key = currentTabKey();
    if (key.isEmpty()) return;
    auto it = m_tabs.constFind(key);
    if (it == m_tabs.constEnd() || !it->view) return;

    // Build a default filename like "LK_1_VCC_1_20260509_143015.<ext>".
    // The extension matches the last-used format; the dialog will swap
    // it if the user changes the radio.
    const QString stem = QString("%1_%2")
        .arg(m_dispatcher->friendlyNameFor(key))
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    const QString defaultExt = (m_lastExportFormat == Exporter::JSON) ? "json" : "csv";
    const QString defaultPath = QString("%1/%2.%3")
        .arg(QDir::homePath(), stem, defaultExt);

    ExportDialog dlg(defaultPath, m_lastExportFormat,
                     m_lastExportColumns, this);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString path = dlg.path();
    if (path.isEmpty()) return;

    // Remember the format + columns for next time within this session.
    m_lastExportFormat  = dlg.format();
    m_lastExportColumns = dlg.columns();

    QVector<LogEntryPtr> snap = snapshotProxy(it->view);
    if (snap.isEmpty()) {
        // Not modal: the operator asked for something that cannot happen
        // yet. Blocking the window to say so is disproportionate, and the
        // notification history means it cannot be missed either.
        notify(NoteLevel::Warning,
               tr("Nothing to export — the current tab is empty."));
        return;
    }

    auto *worker = new Exporter(path, m_lastExportFormat,
                                std::move(snap), &m_nameMap,
                                m_lastExportColumns);
    connect(worker, &Exporter::exportFinished,
            this, [this](QString p, int rows) {
                notify(NoteLevel::Info, tr("Exported %1 rows to:\n%2")
                                            .arg(rows).arg(p));
            });
    connect(worker, &Exporter::exportFailed,
            this, [this](QString p, QString reason) {
                QMessageBox::warning(this, tr("Export failed"),
                    tr("Could not write %1:\n%2").arg(p, reason));
            });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

// =============================================================================
//  Tools — Compare tabs (side-by-side viewer)
// =============================================================================

void MainWindow::restoreLayout()
{
    const QByteArray geom = Settings::mainWindowGeometry();
    if (!geom.isEmpty()) restoreGeometry(geom);

    const QByteArray state = Settings::mainWindowState();
    if (!state.isEmpty()) {
        // A false return means the blob was written by a different layout
        // version or is corrupt. Nothing to do: the defaults set up during
        // construction are still in place, which is the correct fallback.
        restoreState(state, Settings::kLayoutVersion);
    }

    // The decode-failure dock is meant to appear only when something fails.
    // restoreState() will happily bring it back visible from a session where
    // it did, so re-hide it if it has nothing to show — otherwise it comes
    // back empty every launch and stops meaning anything.
    if (m_failDock && m_failRefs.isEmpty()) m_failDock->hide();
}

void MainWindow::scheduleLayoutSave()
{
    // Never during construction: restoreLayout() has not run yet, so the
    // arrangement on screen is the code's defaults, and persisting those
    // would overwrite what the user actually chose last session.
    if (m_layoutSaveTimer) m_layoutSaveTimer->start();
}

void MainWindow::saveLayout()
{
    Settings::setMainWindowGeometry(saveGeometry());
    Settings::setMainWindowState(saveState(Settings::kLayoutVersion));

    // Column widths are taken from whichever tab is current: they are kept
    // identical across tabs, so any one of them is representative.
    const QString key = currentTabKey();
    auto it = m_tabs.find(key);
    if (it != m_tabs.end() && it->view) {
        QList<int> widths;
        for (int c = 0; c < LogModel::ColumnCount; ++c) {
            widths << it->view->columnWidth(c);
        }
        Settings::setLogColumnWidths(widths);
    }
}

// These three now forward to LogTableView, which is where they live so the
// compare panes and the session viewer answer the same settings the same
// way. Kept as members because half the window calls them by name.
void MainWindow::applyDensity(QTableView *view) const
{
    LogTableView::applyDensity(view);
}

void MainWindow::applyDensityToAllTabs()
{
    for (auto it = m_tabs.begin(); it != m_tabs.end(); ++it) {
        applyDensity(it->view);
        // The header caches the old default for rows already laid out, so
        // an explicit reset is needed or existing rows keep their previous
        // height while new ones arrive at the new one.
        if (it->view) it->view->verticalHeader()->reset();
    }
}

void MainWindow::onActionCommandPalette()
{
    // Harvested fresh on every invocation rather than cached: enabled
    // state, shortcuts and even the action set can change at runtime, and
    // a stale palette offering a command that no longer applies is worse
    // than no palette.
    CommandPalette palette(harvestCommands(menuBar()), this);
    palette.exec();
}

void MainWindow::onActionResetPanels()
{
    // Put every dock back where the constructor placed it and make it
    // visible. This is the escape hatch for a layout that has become
    // unusable — a dock dragged somewhere odd, or several closed at once.
    for (QDockWidget *d : { m_sourceDock, m_rawDock, m_fieldDock,
                            m_bookmarkDock, m_failDock }) {
        if (d) d->setFloating(false);
    }
    if (m_sourceDock)   addDockWidget(Qt::LeftDockWidgetArea,   m_sourceDock);
    if (m_rawDock)      addDockWidget(Qt::RightDockWidgetArea,  m_rawDock);
    if (m_fieldDock)    addDockWidget(Qt::RightDockWidgetArea,  m_fieldDock);
    if (m_bookmarkDock) addDockWidget(Qt::RightDockWidgetArea,  m_bookmarkDock);
    if (m_failDock)     addDockWidget(Qt::BottomDockWidgetArea, m_failDock);

    if (m_rawDock && m_fieldDock) tabifyDockWidget(m_rawDock, m_fieldDock);

    for (QDockWidget *d : { m_sourceDock, m_rawDock, m_fieldDock,
                            m_bookmarkDock }) {
        if (d) d->show();
    }
    // The failure dock stays hidden unless it has something to show — it is
    // meant to appear when a decode fails, not to sit there empty.
    if (m_failDock) {
        if (m_failRefs.isEmpty()) m_failDock->hide(); else m_failDock->show();
    }
    if (m_rawDock) m_rawDock->raise();

    scheduleLayoutSave();
    notify(NoteLevel::Info, tr("Panel layout reset"));
}

void MainWindow::onActionToggleColumn()
{
    auto *action = qobject_cast<QAction*>(sender());
    if (!action) return;

    const int col = action->data().toInt();
    QList<int> hidden = Settings::hiddenColumns();
    if (action->isChecked()) hidden.removeAll(col);
    else if (!hidden.contains(col)) hidden.append(col);

    Settings::setHiddenColumns(hidden);
    applyColumnVisibilityToAllTabs();
}

void MainWindow::applyColumnVisibility(QTableView *view) const
{
    LogTableView::applyColumnVisibility(view);
}

void MainWindow::applyColumnVisibilityToAllTabs()
{
    for (auto it = m_tabs.begin(); it != m_tabs.end(); ++it) {
        applyColumnVisibility(it->view);
    }
}

void MainWindow::onActionCopySelection()
{
    const QString key = currentTabKey();
    auto it = m_tabs.find(key);
    if (it == m_tabs.end() || !it->view) return;

    const QVector<LogEntryPtr> rows = selectedEntries(it->view, key);
    if (rows.isEmpty()) return;

    QApplication::clipboard()->setText(formatMessagesForClipboard(rows));
    // Named, because the two copies now differ and a silent one would leave
    // the operator to discover which they got by pasting it.
    notify(NoteLevel::Info, rows.size() > 1
                                ? tr("Copied %1 messages").arg(rows.size())
                                : tr("Copied the message"));
}

void MainWindow::onActionCopyRows()
{
    const QString key = currentTabKey();
    auto it = m_tabs.find(key);
    if (it == m_tabs.end() || !it->view) return;

    const QVector<LogEntryPtr> rows = selectedEntries(it->view, key);
    if (rows.isEmpty()) return;

    QApplication::clipboard()->setText(
        formatEntriesForClipboard(rows, false, Settings::showUtc()));
    notify(NoteLevel::Info, tr("Copied %1 row(s) with columns").arg(rows.size()));
}

void MainWindow::onActionToggleUtc()
{
    auto *action = qobject_cast<QAction*>(sender());
    const bool utc = action ? action->isChecked() : !Settings::showUtc();
    Settings::setShowUtc(utc);

    // Both the ingest path (for entries yet to arrive) and every existing
    // model, whose cached strings were rendered in the other zone.
    if (m_dispatcher) {
        m_dispatcher->setShowUtc(utc);
        for (const QString &key : m_dispatcher->knownKeys()) {
            if (LogModel *m = m_dispatcher->modelForKey(key)) m->setShowUtc(utc);
        }
    }

    notify(NoteLevel::Info,
           utc ? tr("Times shown in UTC — matching the log files")
               : tr("Times shown in local time"));
}

void MainWindow::onActionSetDensity()
{
    auto *action = qobject_cast<QAction*>(sender());
    if (!action) return;

    Settings::setRowDensity(action->data().toInt());
    applyDensityToAllTabs();

    const int d = Settings::rowDensity();
    notify(NoteLevel::Info, tr("Row density: %1 (%2 px)")
                               .arg(action->text().remove('&'))
                               .arg(Settings::rowHeightFor(d)));
}

void MainWindow::applyColumnWidths(QTableView *view) const
{
    LogTableView::applyColumnWidths(view);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (m_emptyState && m_emptyState->isVisible()) {
        m_emptyState->resize(ui->tabWidget->size());
    }
}

// The record separator: ASCII unit separator, because a source key or a
// friendly name can contain almost any printable character and a comma or
// colon would split the wrong number of fields.
//
// NOT written as "\x1f" inside a longer literal: a hex escape in C++ is
// greedy, so "...\x1f1" is the single character U+01F1, not a separator
// followed by a '1'. That compiles, runs, and silently writes records
// nothing can read back — the tests below caught it doing exactly that.
static const QChar kWsSep(0x1F);

void MainWindow::saveWorkspace() const
{
    QStringList records;
    // Tab-widget order first, so the order comes back as the operator left
    // it rather than as the hash happens to iterate.
    QStringList seen;
    for (int i = 0; i < ui->tabWidget->count(); ++i) {
        QWidget *w = ui->tabWidget->widget(i);
        for (auto it = m_tabs.constBegin(); it != m_tabs.constEnd(); ++it) {
            if (it->container != w) { continue; }
            records << QStringList{ it->tabKey, it->friendlyName,
                                    QStringLiteral("1") }.join(kWsSep);
            seen << it->tabKey;
            break;
        }
    }
    // Then the hidden ones, which are still sources the operator chose to
    // watch — closing a tab hides it, it does not forget it.
    for (auto it = m_tabs.constBegin(); it != m_tabs.constEnd(); ++it) {
        if (seen.contains(it->tabKey)) { continue; }
        records << QStringList{ it->tabKey, it->friendlyName,
                                QStringLiteral("0") }.join(kWsSep);
    }

    Settings::setWorkspaceTabs(records);

    QString active;
    const int cur = ui->tabWidget->currentIndex();
    if (cur >= 0) {
        QWidget *w = ui->tabWidget->widget(cur);
        for (auto it = m_tabs.constBegin(); it != m_tabs.constEnd(); ++it) {
            if (it->container == w) { active = it->tabKey; break; }
        }
    }
    Settings::setWorkspaceActiveTab(active);
}

void MainWindow::restoreWorkspace()
{
    if (!Settings::restoreWorkspace()) { return; }
    if (!m_dispatcher) {
        // Called before the pipeline exists. Refusing loudly beats the
        // segfault that this cost once already.
        qWarning() << "restoreWorkspace() called before the dispatcher exists "
                      "— no tabs restored";
        return;
    }

    const QStringList records = Settings::workspaceTabs();
    if (records.isEmpty()) { return; }

    for (const QString &rec : records) {
        const QStringList parts = rec.split(kWsSep);
        if (parts.size() < 3) { continue; }            // written by an older build
        const QString key  = parts.at(0);
        const QString name = parts.at(1);
        if (key.isEmpty()) { continue; }

        // Building the tab creates the model too, so traffic that arrives
        // for this source later routes into the tab already on screen
        // instead of making a second one.
        buildOrShowTab(key, name);

        if (parts.at(2) != QLatin1String("1")) {
            auto it = m_tabs.find(key);
            if (it != m_tabs.end()) { hideTab(*it); }
        }
    }

    const QString active = Settings::workspaceActiveTab();
    if (!active.isEmpty()) {
        auto it = m_tabs.find(active);
        if (it != m_tabs.end() && it->container) {
            const int idx = ui->tabWidget->indexOf(it->container);
            if (idx >= 0) { ui->tabWidget->setCurrentIndex(idx); }
        }
    }
    rebuildWindowMenu();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Pop-outs still open are closed by the application, not the operator:
    // remember where they are and that they were open, for the next start.
    m_shuttingDown = true;
    for (auto it = m_popouts.constBegin(); it != m_popouts.constEnd(); ++it) {
        if (it.value()) {
            WindowGeometry::save(it.value(), TabPopoutWindow::geometryKey(it.key()));
        }
    }
    savePopoutKeys();
    saveWorkspace();
    // Saved here rather than in the destructor: by the time ~MainWindow
    // runs, child widgets may already be part-way through teardown and
    // saveState() would capture a layout nobody chose.
    saveLayout();
    // A clean exit: no recovery next time (session 79).
    if (m_recoveryTimer) m_recoveryTimer->stop();
    SessionRecovery::markCleanExit(SessionRecovery::defaultPath());
    QMainWindow::closeEvent(event);
}

void MainWindow::applyDiskLoggingSetting(bool enabled)
{
    if (!enabled) {
        if (m_writer) {
            // Tear the writer down rather than leaving it idle. An idle
            // writer still holds open file handles and a NameMap copy, and
            // "logging is off" should mean the files are closed and
            // available to move or delete.
            if (m_writer->stop(5000)) {
                delete m_writer;
            } else {
                qWarning() << "MainWindow: log writer would not stop; "
                              "leaking it rather than deleting a live QThread.";
            }
            m_writer = nullptr;
        }
        m_diskQuotaSuspended = false;
        onStatusTick();
        return;
    }

    if (m_writer) {
        // Already running — just push the live-adjustable settings through.
        m_writer->setMaxFolderBytes(Settings::maxFolderBytes());
        m_writer->setMinFreeBytes(Settings::minFreeBytes());
        m_writer->setRotationBytes(Settings::diskRotationBytes());
        return;
    }

    m_writer = new LogWriter(Settings::diskLogRoot(),
                             Settings::diskRotationBytes(),
                             nullptr);
    m_writer->setNameMap(m_nameMap);   // writer keeps its own copy
    m_writer->setRawCapture(Settings::rawCapture());
    m_writer->setMaxFolderBytes(Settings::maxFolderBytes());
    m_writer->setMinFreeBytes(Settings::minFreeBytes());

    // Queued across the thread boundary by default, which is what we want:
    // the signal originates on the worker.
    connect(m_writer, &LogWriter::quotaSuspendedChanged,
            this,     &MainWindow::onDiskQuotaChanged);

    m_writer->start();
    m_diskQuotaSuspended = false;
    onStatusTick();
}

void MainWindow::onDiskQuotaChanged(bool suspended, qint64 used, qint64 limit)
{
    m_diskQuotaSuspended = suspended;
    m_diskQuotaUsed      = used;
    m_diskQuotaLimit     = limit;
    onStatusTick();

    const double usedMiB  = double(used)  / (1024.0 * 1024.0);
    const double limitMiB = double(limit) / (1024.0 * 1024.0);

    if (suspended) {
        // A suspension nobody notices is silent data loss — but an Error
        // notification does not expire and shows a persistent count, so it
        // cannot be missed without also blocking a window the operator may
        // be actively reading during an incident. The full explanation goes
        // in the detail, visible in the history.
        notify(NoteLevel::Error,
               tr("Disk logging suspended — folder limit reached "
                  "(%1 of %2 MiB)")
                   .arg(usedMiB, 0, 'f', 1).arg(limitMiB, 0, 'f', 1),
               tr("Nothing has been deleted. Free space under:\n%1\n\n"
                  "Logging resumes on its own within about 30 seconds of "
                  "space becoming available, or raise the limit in Settings.")
                   .arg(Settings::diskLogRoot()));
    } else {
        notify(NoteLevel::Info, tr("Disk logging resumed (%1 MiB of %2 MiB used)")
                                   .arg(usedMiB, 0, 'f', 1)
                                   .arg(limitMiB, 0, 'f', 1));
    }
}

void MainWindow::applyTimeRangeFilter(const QString &tabKey,
                                      qint64 fromMs, qint64 toMs)
{
    auto it = m_tabs.find(tabKey);
    if (it == m_tabs.end() || !it->filterBar) return;

    // Expressed as a visible query rather than as hidden state. The
    // operator can see exactly what was applied, adjust the bounds by hand,
    // combine it with other terms, and clear it the same way as any other
    // filter — none of which is true of a range stored invisibly on the
    // side.
    const QString expr =
        QStringLiteral("after:%1 before:%2")
            .arg(QDateTime::fromMSecsSinceEpoch(fromMs).toString(Qt::ISODateWithMs),
                 QDateTime::fromMSecsSinceEpoch(toMs).toString(Qt::ISODateWithMs));

    it->filterBar->setQuery(expr);

    notify(NoteLevel::Info, tr("Filtered to %1 – %2. Clear the filter box to show everything again.")
            .arg(QDateTime::fromMSecsSinceEpoch(fromMs).toString("HH:mm:ss.zzz"),
                 QDateTime::fromMSecsSinceEpoch(toMs).toString("HH:mm:ss.zzz")));
}


void MainWindow::onActionChooseSchema()
{
    const QString current = Settings::schemaPath();
    const QString picked = QFileDialog::getOpenFileName(
        this, tr("Choose schema file"),
        current.isEmpty() ? QCoreApplication::applicationDirPath() : current,
        tr("Schema XML (*.xml);;All files (*)"));
    if (picked.isEmpty()) return;

    Settings::setSchemaPath(picked);
    onActionReloadSchema();
}

void MainWindow::refreshPinChoices()
{
    if (!m_pinPanel) { return; }

    const Schema::Decoder &dec = kavachSchema();

    // What this tab has actually carried. Sampled from the head of the model
    // rather than all of it: this runs on every tab change, and the answer
    // stops improving after a few hundred rows.
    QStringList seen;
    const QString key = currentTabKey();
    if (!key.isEmpty()) {
        if (LogModel *m = m_dispatcher->modelForKey(key)) {
            seen = discoverFieldNames(m, dec);
        }
    }
    // The chooser is nested by packet, so it needs the schema split that
    // way rather than as one alphabetical run of five hundred names.
    QVector<PinPacketFields> byPacket;
    for (const Schema::Decoder::PacketFields &pf : dec.fieldsByCaptype()) {
        byPacket.push_back({ pf.packet, pf.captype, pf.fields });
    }

    // Packets before fields: picking a field out of a packet group sets the
    // narrowing combo, so that combo has to be filled by the time the field
    // menu can be opened.
    m_pinPanel->setAvailablePackets(byPacket);
    m_pinPanel->setAvailableFields(seen, byPacket);

    QStringList keys = m_tabs.keys();
    keys.sort();
    m_pinPanel->setAvailableSources(keys);
}

void MainWindow::pinField(const QString &fieldName)
{
    if (!m_pinPanel || !m_pinDock) { return; }

    // Narrowed to the tab it was clicked in. An operator pointing at a value
    // in one source's frame means THAT source's value; an unnarrowed pin would
    // then start showing whichever loco spoke last, which is not what they
    // asked for and would look like the pin was broken.
    // Narrowed to the tab AND the packet it was clicked in. The operator is
    // pointing at a field in one packet of one source; a pin that then took
    // the same field name from a different packet would be answering a
    // question they did not ask.
    pinFieldNarrowed(fieldName, currentTabKey(),
                     m_fieldPanel ? m_fieldPanel->currentCaptype() : QString());
}

// The pinning itself, with the narrowing supplied rather than read off this
// window's own inspector. Split out so a field pinned from a compare pane
// lands narrowed the same way — same source, same packet — instead of the
// compare window growing its own half of this.
void MainWindow::pinFieldNarrowed(const QString &fieldName,
                                  const QString &key,
                                  const QString &type)
{
    if (!m_pinPanel || !m_pinDock) { return; }
    if (!m_pinPanel->board().add(fieldName, key, type)) {
        notify(NoteLevel::Info, tr("%1 is already pinned.").arg(fieldName));
        m_pinDock->show();
        m_pinDock->raise();
        return;
    }
    m_pinPanel->persist();

    // Show the dock, because a pin that lands in a hidden panel is a click
    // that appeared to do nothing.
    m_pinDock->show();
    m_pinDock->raise();
    notify(NoteLevel::Info, tr("Pinned %1 from %2.").arg(fieldName, key));
}

void MainWindow::plotField(const QString &fieldName)
{
    plotFieldIn(currentTabKey(), fieldName);
}

void MainWindow::plotFieldIn(const QString &tabKey, const QString &fieldName)
{
    const QString key = tabKey;
    LogModel *model = key.isEmpty() ? nullptr : m_dispatcher->modelForKey(key);
    if (!model || model->count() == 0) {
        notify(NoteLevel::Info, tr("Select a tab with messages first."));
        return;
    }
    auto *plot = new FieldPlotWindow(model, key, this);
    connect(plot, &FieldPlotWindow::jumpRequested, this, &MainWindow::jumpToEntry);
    // Opened ON the field that was asked for, rather than on the chooser: the
    // operator has already named it by right-clicking it.
    plot->plotField(fieldName);
    plot->show();
    plot->raise();
}


void MainWindow::onActionPlotField()
{
    const QString key = currentTabKey();
    LogModel *model = key.isEmpty() ? nullptr : m_dispatcher->modelForKey(key);
    if (!model || model->count() == 0) {
        notify(NoteLevel::Info, tr("Select a tab with messages first."));
        return;
    }

    // One window per invocation rather than a reused singleton: comparing
    // two fields side by side is the common follow-up, and that needs two
    // windows open at once.
    auto *plot = new FieldPlotWindow(model, key, this);
    connect(plot, &FieldPlotWindow::jumpRequested,
            this, &MainWindow::jumpToEntry);
    plot->show();
    plot->raise();
}

void MainWindow::onActionLoadTestCases()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load test cases"),
        QCoreApplication::applicationDirPath() + "/testcases.json",
        tr("Test cases (*.json);;All files (*)"));
    if (path.isEmpty()) return;

    QString err;
    if (!m_tests.load(path, &err)) {
        QMessageBox::warning(this, tr("Test cases"),
                             tr("Could not load %1:\n%2").arg(path, err));
        return;
    }
    int broken = 0;
    for (const TestAssertion &a : m_tests.assertions()) if (!a.queryOk) ++broken;

    notify(broken ? NoteLevel::Warning : NoteLevel::Info,
           tr("Loaded %1 test case(s)%2")
               .arg(m_tests.assertions().size())
               .arg(broken ? tr(" — %1 with an unusable query").arg(broken)
                           : QString()),
           path);
}

void MainWindow::onActionSaveTestReport()
{
    if (m_tests.assertions().isEmpty()) {
        notify(NoteLevel::Warning, tr("No test cases loaded."));
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QString suggested =
        QStringLiteral("test-observations-%1.html")
            .arg(QDateTime::fromMSecsSinceEpoch(now, Qt::UTC)
                     .toString(QStringLiteral("yyyyMMdd-HHmmss")));

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save observation report"), suggested,
        tr("HTML report (*.html);;CSV (*.csv)"));
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Save failed"),
                             tr("Could not write %1:\n%2").arg(path, f.errorString()));
        return;
    }
    const bool csv = path.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive);
    f.write(csv ? m_tests.buildReportCsv().toUtf8()
                : m_tests.buildReportHtml(tr("KAVACH test observations"),
                                          now).toUtf8());
    f.close();

    notify(NoteLevel::Info,
           tr("Saved observations: %1 of %2 conditions seen")
               .arg(m_tests.observedCount()).arg(m_tests.assertions().size()),
           path);
}

void MainWindow::onActionResetTestRun()
{
    m_tests.resetObservations();
    notify(NoteLevel::Info, tr("Test run reset — observations cleared"));
}

void MainWindow::onAssertionObserved(const QString &id, const QString &title)
{
    // Info, so it lands in the notification history with a timestamp:
    // "when was 32.9.3 first satisfied" is the question this answers.
    notify(NoteLevel::Info, tr("Observed %1 — %2").arg(id, title));
}

void MainWindow::onAssertionViolated(const QString &id, const QString &title,
                                     const QString &detail)
{
    // Warning, not Info: a precondition that went unanswered is the one
    // thing during a run that needs looking at now, and Warning-level
    // notifications persist longer and stand out.
    notify(NoteLevel::Warning,
           tr("%1 not answered — %2").arg(id, title), detail);
}

void MainWindow::onActionReloadSchema()
{
    const QString path = Settings::schemaPath();

    QString err;
    const bool ok = reloadKavachSchema(path, &err);

    if (!ok) {
        // The decoder is empty now, not stale — say so plainly, because the
        // symptom otherwise ("nothing decodes") looks identical to a schema
        // that simply does not cover this traffic.
        QMessageBox::critical(
            this, tr("Schema reload failed"),
            tr("%1\n\n%2\n\nNo schema is loaded, so nothing will decode. "
               "Fix the file and reload, or clear the external schema path "
               "to go back to the built-in copy.")
                .arg(path.isEmpty() ? tr("(built-in schema)") : path, err));
        if (m_fieldPanel) m_fieldPanel->clear();
        return;
    }

    // No separate "parsed but empty" check here: Decoder::load() already
    // rejects a schema with no <packet> elements and says so, and that
    // message reaches the operator through the failure dialog above.
    const Schema::Decoder &dec = kavachSchema();

    // Stale results everywhere: the failure list was produced by the OLD
    // schema, and its whole purpose is telling you what the CURRENT one
    // misses. Clearing it is the point of the reload loop — edit, reload,
    // see what is still unhandled.
    if (m_failList) {
        m_failList->clear();
        m_failRefs.clear();
        m_failDock->setWindowTitle(tr("Decode failures"));
        m_failDock->hide();
    }

    // Re-decode whatever is selected so the effect is immediately visible
    // rather than requiring the operator to click away and back.
    if (m_fieldPanel) {
        const QString key = currentTabKey();
        auto it = m_tabs.find(key);
        if (it != m_tabs.end() && it->view) {
            m_fieldPanel->showEntry(entryFromProxyIndex(it->view->currentIndex()));
        }
    }

    // The field index is built from the schema, so it is stale the moment
    // the schema changes — and the reload loop is exactly when someone is
    // looking at it.
    refreshFieldIndex();

    notify(NoteLevel::Info, tr("Schema reloaded from %1 — %2 packet(s), %3 struct(s), %4 enum(s)")
            .arg(path.isEmpty() ? tr("built-in") : QFileInfo(path).fileName())
            .arg(dec.packetCount()).arg(dec.structCount()).arg(dec.enumCount()));
}

void MainWindow::onActionMergedView()
{
    if (m_mergedWin) {
        m_mergedWin->raise();
        m_mergedWin->activateWindow();
        return;
    }
    m_mergedWin = new MergedWindow(m_dispatcher, &m_nameMap, m_theme,
                                   Settings::perTabCapacity(), this);
    connect(m_mergedWin, &MergedWindow::jumpRequested,
            this,        &MainWindow::jumpToEntry);
    // WA_DeleteOnClose is set on the window, so clear our pointer when it
    // goes rather than leaving a dangling one for the next Ctrl+M.
    connect(m_mergedWin, &QObject::destroyed,
            this, [this]() { m_mergedWin = nullptr; });
    m_mergedWin->show();
}

void MainWindow::onActionToggleBookmark()
{
    const QString key = currentTabKey();
    auto it = m_tabs.find(key);
    if (it == m_tabs.end() || !it->view || !it->filterBar) return;

    const QModelIndex cur = it->view->currentIndex();
    if (!cur.isValid()) {
        notify(NoteLevel::Info, tr("Select a row first, then Ctrl+B."));
        return;
    }

    auto *proxy = it->filterBar->proxyModel();
    const QModelIndex src = proxy ? proxy->mapToSource(cur) : cur;
    LogModel *model = m_dispatcher->modelForKey(key);
    if (!model) return;

    LogEntryPtr e = model->entryAt(src.row());
    if (!e) return;

    const bool added = m_bookmarks.toggle(key, e->epochMs, e->text);
    e->bookmarked = added;

    // Repaint just that row's time cell rather than the whole model.
    const QModelIndex tl = model->index(src.row(), LogModel::ColTime);
    emit model->dataChanged(tl, tl, { Qt::DisplayRole });

    if (m_bookmarkDock && added) m_bookmarkDock->show();
}

void MainWindow::onActionNextBookmark() { stepBookmark(+1); }
void MainWindow::onActionPrevBookmark() { stepBookmark(-1); }

void MainWindow::stepBookmark(int direction)
{
    const QString key = currentTabKey();
    auto it = m_tabs.find(key);
    if (it == m_tabs.end() || !it->view || !it->filterBar) return;

    LogModel *model = m_dispatcher->modelForKey(key);
    auto *proxy = it->filterBar->proxyModel();
    if (!model || !proxy) return;

    // Step through what is VISIBLE, not through the store: a bookmark
    // hidden by the current filter would otherwise scroll the view to a row
    // the operator cannot see, which reads as the shortcut being broken.
    const int rows = proxy->rowCount();
    const int from = it->view->currentIndex().isValid()
                         ? it->view->currentIndex().row()
                         : (direction > 0 ? -1 : rows);

    // Shared with the session window, so the two cannot drift apart.
    const int r = nextBookmarkedRow(proxy, model, from, direction);
    if (r >= 0) {
        const QModelIndex landing = proxy->index(r, LogModel::ColTime);
        it->view->setCurrentIndex(landing);
        it->view->scrollTo(landing, QAbstractItemView::PositionAtCenter);
        it->view->setFocus();
        return;
    }

    notify(NoteLevel::Info, direction > 0
                               ? tr("No further bookmarks in this tab.")
                               : tr("No earlier bookmarks in this tab."));
}

void MainWindow::onBookmarksChanged()
{
    if (!m_bookmarkList) return;

    // Rebuilding wholesale would fight the user's selection; block signals
    // so the repopulate doesn't fire currentRowChanged and trigger a jump.
    const QSignalBlocker blocker(m_bookmarkList);
    m_bookmarkList->clear();

    for (const Bookmark &b : m_bookmarks.all()) {
        const QString friendly = m_nameMap.lookupByKey(b.tabKey);
        QString label = QStringLiteral("%1  %2  %3")
                            .arg(QDateTime::fromMSecsSinceEpoch(b.epochMs)
                                     .toString("MM-dd HH:mm:ss.zzz"),
                                 friendly,
                                 b.note.isEmpty() ? b.messageSnippet : b.note);
        auto *item = new QListWidgetItem(label);
        item->setToolTip(b.messageSnippet.isEmpty() ? label : b.messageSnippet);
        m_bookmarkList->addItem(item);
    }

    if (m_bookmarkDock) {
        m_bookmarkDock->setWindowTitle(
            m_bookmarks.count() > 0 ? tr("Bookmarks (%1)").arg(m_bookmarks.count())
                                    : tr("Bookmarks"));
    }
}

void MainWindow::onBookmarkActivated(int row)
{
    if (row < 0 || row >= m_bookmarks.count()) return;
    const Bookmark &b = m_bookmarks.all().at(row);
    jumpToEntry(b.tabKey, b.epochMs);
}

void MainWindow::onBookmarkContextMenu(const QPoint &pos)
{
    const int row = m_bookmarkList->currentRow();
    if (row < 0 || row >= m_bookmarks.count()) return;

    QMenu menu(this);
    QAction *actNote   = menu.addAction(tr("Edit note…"));
    QAction *actRemove = menu.addAction(tr("Remove"));
    menu.addSeparator();
    QAction *actClear  = menu.addAction(tr("Remove all"));

    QAction *chosen = menu.exec(m_bookmarkList->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == actNote) {
        bool ok = false;
        const QString note = QInputDialog::getText(
            this, tr("Bookmark note"), tr("Note:"), QLineEdit::Normal,
            m_bookmarks.all().at(row).note, &ok);
        if (ok) m_bookmarks.setNote(row, note);
    } else if (chosen == actRemove) {
        const Bookmark b = m_bookmarks.all().at(row);
        const QVector<Bookmark> before = m_bookmarks.all();
        m_bookmarks.remove(row);
        pushBookmarkUndo(tr("Remove bookmark"), before);
        // Clear the paint flag on the live entry too, if it's still around.
        if (LogModel *m = m_dispatcher->modelForKey(b.tabKey)) {
            const int n = m->count();
            for (int i = n - 1; i >= 0; --i) {
                LogEntryPtr e = m->entryAt(i);
                if (e && e->epochMs == b.epochMs) { e->bookmarked = false; break; }
            }
        }
    } else if (chosen == actClear) {
        if (QMessageBox::question(
                this, tr("Remove all bookmarks"),
                tr("Remove all %1 bookmarks?\n\nEdit ▸ Undo (Ctrl+Z) brings them back.")
                    .arg(m_bookmarks.count()))
            == QMessageBox::Yes) {
            const QVector<Bookmark> before = m_bookmarks.all();
            m_bookmarks.clear();
            for (const TabUi &t : qAsConst(m_tabs)) {
                if (LogModel *m = m_dispatcher->modelForKey(t.tabKey)) {
                    for (int i = 0; i < m->count(); ++i) {
                        if (LogEntryPtr e = m->entryAt(i)) e->bookmarked = false;
                    }
                }
            }
            pushBookmarkUndo(tr("Remove all bookmarks"), before);
        }
    }
}

void MainWindow::pushBookmarkUndo(const QString &label, const QVector<Bookmark> &before)
{
    QPointer<MainWindow> self(this);
    m_undo->push(label, [self, before]() {
        if (!self) return false;
        self->m_bookmarks.replaceAll(before);
        // The paint flag lives on the entries: put it back where it applies.
        for (const TabUi &t : qAsConst(self->m_tabs)) {
            if (LogModel *m = self->m_dispatcher->modelForKey(t.tabKey)) {
                self->m_bookmarks.applyToModel(m, t.tabKey);
            }
        }
        return true;
    });
}

void MainWindow::onActionSearchAll()
{
    if (!m_searchWin) {
        m_searchWin = new SearchWindow(m_dispatcher, &m_nameMap, this);
        connect(m_searchWin, &SearchWindow::jumpRequested,
                this,        &MainWindow::jumpToEntry);
    }

    // Seed from the current tab's filter box, so "I found it here, now show
    // me everywhere" is one keystroke rather than a retype.
    const QString key = currentTabKey();
    auto it = m_tabs.find(key);
    if (it != m_tabs.end() && it->filterBar) {
        const QString existing = it->filterBar->filterText();
        if (!existing.isEmpty() && m_searchWin->isHidden()) {
            m_searchWin->setQueryText(existing, false);
        }
    }

    m_searchWin->show();
    m_searchWin->raise();
    m_searchWin->activateWindow();
}

void MainWindow::jumpToEntry(const QString &tabKey, qint64 epochMs)
{
    auto it = m_tabs.find(tabKey);
    if (it == m_tabs.end()) return;

    // The tab may be hidden (no traffic for a while, or manually closed);
    // a jump has to be able to bring it back or the result is a silent
    // no-op that looks like a broken double-click.
    if (!it->visible) showTab(*it);

    const int idx = ui->tabWidget->indexOf(it->container);
    if (idx >= 0) ui->tabWidget->setCurrentIndex(idx);

    auto *proxy = it->filterBar ? it->filterBar->proxyModel() : nullptr;
    if (!proxy) return;

    // Nearest row by time. The row may be filtered out of view entirely,
    // in which case the closest visible neighbour is the honest landing
    // point — better than doing nothing and better than silently clearing
    // the operator's filter.
    LogModel *model = m_dispatcher->modelForKey(tabKey);
    if (!model) return;

    const int rows = proxy->rowCount();
    int    bestRow = -1;
    qint64 bestKey = 0;
    for (int r = 0; r < rows; ++r) {
        const QModelIndex src = proxy->mapToSource(proxy->index(r, 0));
        LogEntryPtr e = model->entryAt(src.row());
        if (!e) continue;
        const qint64 d = qAbs(e->epochMs - epochMs);
        if (bestRow < 0 || d < bestKey) { bestRow = r; bestKey = d; }
    }
    if (bestRow < 0) {
        notify(NoteLevel::Warning, tr("That message is hidden by this tab's filter."));
        return;
    }

    const QModelIndex landing = proxy->index(bestRow, LogModel::ColTime);
    it->view->setCurrentIndex(landing);
    it->view->scrollTo(landing, QAbstractItemView::PositionAtCenter);
    it->view->setFocus();
}

void MainWindow::onActionSearchArchive()
{
    // A new window each time: comparing two archive queries side by side is
    // a normal thing to want, and each carries its own worker thread.
    auto *win = new ArchiveSearchWindow(&m_colorRules, &m_nameMap, this);
    connect(win, &ArchiveSearchWindow::openSessionRequested,
            this, &MainWindow::openSessionAt);
    win->show();
    win->raise();
}

void MainWindow::openSessionAt(const QString &filePath, qint64 epochMs)
{
    Q_UNUSED(epochMs);   // SessionWindow has no seek API yet; see below.

    auto *win = new SessionWindow(&m_colorRules, &m_nameMap, m_theme, this);
    win->setBookmarkStore(&m_bookmarks);
    const qint64 n = win->loadFiles({ filePath });
    win->show();

    if (n == 0) {
        notify(NoteLevel::Error, tr("Could not read any records from %1").arg(filePath));
    }
}

void MainWindow::onActionOpenSession()
{
    const QString start = Settings::diskLogRoot();
    const QStringList paths = QFileDialog::getOpenFileNames(
        this,
        tr("Open recorded session"),
        start,
        tr("DLConsole sessions (*%1);;All files (*)")
            .arg(QLatin1String(SessionFile::kExtension)));

    if (paths.isEmpty()) return;

    // Parented to us so the borrowed ColorRules / NameMap outlive it, and
    // WA_DeleteOnClose handles the rest.
    auto *win = new SessionWindow(&m_colorRules, &m_nameMap, m_theme, this);
    // The same store the live tabs use: a row bookmarked while reviewing a
    // recording is the same row bookmarked live.
    win->setBookmarkStore(&m_bookmarks);
    const qint64 n = win->loadFiles(paths);

    if (n == 0) {
        // Nothing readable. Show the window anyway — its status bar carries
        // the per-file reasons, which is more useful than a bare error box.
        win->show();
        notify(NoteLevel::Error, tr("No records could be read from the "
                                    "selected file(s)."));
        return;
    }

    win->show();
    notify(NoteLevel::Info, tr("Loaded %1 records from %2 session file(s)")
                               .arg(n).arg(paths.size()));
}

void MainWindow::refreshFieldIndex()
{
    // The decoder development loop is edit kavach.xml, reload, look again.
    // A field index still showing the old schema is worse than none.
    if (m_fieldIndex) { m_fieldIndex->reload(); }
}

void MainWindow::showFieldIndex(const QString &prefill)
{
    // One dialog, reused. Opening a second when the first is already on
    // screen behind the main window looks like the menu did nothing.
    if (!m_fieldIndex) {
        m_fieldIndex = new FieldIndexDialog(&kavachSchema(), this);
    }
    if (!prefill.isEmpty()) { m_fieldIndex->setFilter(prefill); }
    m_fieldIndex->show();
    m_fieldIndex->raise();
    m_fieldIndex->activateWindow();
}


// =============================================================================
//  Tools — Live Loco Console (separate window, fed by the @-tag capture stream)
// =============================================================================


