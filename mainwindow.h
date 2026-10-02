#ifndef MAINWINDOW_H
#define MAINWINDOW_H

// =============================================================================
//  MainWindow
//  -----------------------------------------------------------------------------
//  Patch C additions:
//    – Settings infrastructure (INI-backed via the Settings helper).
//    – Theme (dark/light) toggle, applied app-wide and to all log models.
//    – Per-source last-seen tracking (offline indicator: tab text turns
//      amber after offlineWarnSec, red after offlineErrSec). Hidden tabs
//      keep tracking; reopening the tab shows the current state.
//    – Tab management UX: close button per tab, right-click context menu,
//      drag-to-reorder. Closing a tab HIDES it but keeps the model alive
//      and the disk log running. The tab reappears if a new message
//      arrives for that source, or via Window menu.
//    – Menu bar with File / View / Window / Help.
//
//  Owned objects:
//    – m_receiver   (UDPCommunication, lives on its own thread)
//    – m_dispatcher (MessageDispatcher, GUI-thread QObject)
//    – m_writer     (LogWriter, lives on its own thread)
//    – m_colorRules, m_nameMap (value members)
// =============================================================================

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QSet>
#include <QTimer>

#include "colorrules.h"
#include "locoidentity.h"
#include "framenumberwatch.h"
#include "exporter.h"
#include "bookmarks.h"
#include "notificationcenter.h"
#include "testassertions.h"
#include "logentry.h"
#include "logwriter.h"
#include "messagedispatcher.h"
#include "namemap.h"
#include "rawbytespanel.h"
#include "theme.h"
#include "udpcommunication.h"
#include "workspacesnapshot.h"
#include "clockskewalarm.h"

#include <climits>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
class DecodeWorkbench;
class FlasherWindow;
class LocoConfigWindow;
class MinimizedDock;
class TabPopoutWindow;
class FieldSweepDialog;
class FrameDiffWindow;
class RoundTripWindow;
class FilterBar;
class FindBar;
class PacketMakerDialog;
class QAction;
class QDockWidget;
class QListWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QLabel;
class QMenu;
class FrameNumberWatch;
class TabTags;
class SessionKeyStore;
class QTableView;
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // Session 96: the tab tags this window's tabs, pop-outs and loco consoles share.
    SessionKeyStore *sessionKeys() const { return m_sessionKeys; }
    TabTags *tabTags() const { return m_tabTags; }
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;
    // Dragging a tab out of the tab bar pops it out (see popOutTab).
    bool eventFilter(QObject *watched, QEvent *event) override;
    // The empty-state label is a manually placed child of the tab widget,
    // so nothing lays it out automatically.
    void resizeEvent(QResizeEvent *event) override;

private slots:
    // Dispatcher signals.
    void onTabRequested  (QString tabKey, QString friendlyName);
    void onEntryAppended  (QString tabKey, LogEntryPtr entry);
    void onEntriesAppended(QString tabKey, QVector<LogEntryPtr> entries);

    // Receiver signals.
    void onBindSucceeded(quint16 port);
    void onBindFailed   (QString reason);

    // Status-bar polling tick + per-source last-seen check.
    void onStatusTick();

    // Auto-hide for the transient "user message" toast.
    void onUserLabelTimeout();

    // ---- Tab management (2e) ----------------------------------------
    // Triggered by the QTabWidget close button.
    void onTabCloseRequested(int index);
    // Right-click context menu on a tab.
    void onTabContextMenu(const QPoint &pos);

    // ---- Menu actions -----------------------------------------------
    void onActionSettings();
    void onActionToggleTheme();
    void onActionShowAllTabs();
    void onActionAbout();
    void onActionFind();          // Ctrl+F — activate current tab's FindBar
    void onActionGotoTimestamp(); // Ctrl+G — jump to a time in current tab
    void onActionExport();        // File → Export current tab…
    void onActionOpenSession();   // File → Open recorded session…
    void onActionSearchAll();     // Edit → Search all sources… (Ctrl+Shift+A)
    void onActionSearchArchive(); // Edit → Search recorded sessions…
    // Open one .dlr in a session viewer.
    void openSessionAt(const QString &filePath, qint64 epochMs);
    void onActionMergedView();    // View → All sources chronological
    void onActionSetDensity();    // View → Row density → …
    void onActionToggleUtc();     // View → Show times in UTC
    void onActionToggleColumn();  // View → Columns → …
    void onActionResetPanels();   // View → Panels → Reset panel layout
    void onActionCommandPalette();// Ctrl+P
    void onSourceActivated(QTreeWidgetItem *item, int column);
    void onSourceFilterChanged(const QString &text);
    void onLogRowContextMenu(const QPoint &pos);
    void onActionCopySelection();
    void onActionCopyRows();
    void onShowNotificationLog();
    void onActionPlotField();     // Tools → Plot field over time…
    void onActionSpeedDistance(); // Tools → Monitor → Speed vs distance… (session 81)
    void onActionRunReport();     // Tools → Monitor → Run summary report… (session 81)
    void onActionIncidentReport(); // Tools → Monitor → Incident report… (session 97)
    void onActionTwoLocoView();    // Tools → Monitor → Two-loco view… (session 98)
    void onActionTrackDiagram();   // Tools → Monitor → Track diagram… (session 99)
    void onActionReloadSchema();  // Tools → Reload schema… (Ctrl+Shift+R)
    void onActionLoadTestCases(); // Tools → Test cases → Load…
    void onActionSaveTestReport();
    void onActionResetTestRun();
    void onAssertionObserved(const QString &id, const QString &title);
    void onAssertionViolated(const QString &id, const QString &title,
                             const QString &detail);
    void onActionChooseSchema();  // Tools → Use external schema file…
    // Turn a ribbon drag into a visible `after:… before:…` query.
    void applyTimeRangeFilter(const QString &tabKey, qint64 fromMs, qint64 toMs);
    void onActionToggleBookmark();      // Ctrl+B on the selected row
    void onActionNextProblem();      // F4      — next error or warning
    void onActionPrevProblem();      // Shift+F4
    void onActionNextBookmark();        // F2 / Shift+F2
    void onActionPrevBookmark();
    void onBookmarksChanged();          // repopulate the dock list
    void onBookmarkActivated(int row);
    void onBookmarkContextMenu(const QPoint &pos);
    void onDecodeFailed(const LogEntryPtr &entry, const QString &reason);
    void onDecodeFailureActivated(int row);
    // Bring the given tab forward and select the row nearest `epochMs`.
    // Shared by the search window's jump and anything else that needs to
    // land on a specific moment in a specific source.
    void jumpToEntry(const QString &tabKey, qint64 epochMs);
    void onActionCompareTabs();   // Tools → Compare tabs…
    void onActionLocoConsole();   // Tools → Live Loco Console…
    void onActionFaultPanel();    // Tools → Active Fault Panel…
    void onActionBrakingPanel();  // Tools → Braking Curves…
    void onActionDecodeWorkbench();  // Tools → Decode Workbench…
    void onActionPacketMaker();      // Tools → Packet Maker…
    // Tools / row context menu: take the selected row's buffer straight to
    // one of the two, instead of copying hex out of one window and pasting
    // it into another.
    void onActionSelectedToWorkbench();
    void onActionSelectedToPacketMaker();
    void onActionFrameDiff();        // Tools → Frame Diff… (with the selection, if two rows)
    void onActionFieldSweep();       // Tools → Field Sweep…
    void onActionRoundTrip();        // Tools → Round-trip Validator…
    void onActionStreamSession();    // Tools → Stream session (.dlr)…
    void onActionPacketSequence();   // Tools → Packet Sequence…
    void onActionSessionKey();       // Tools → Session Key…
    void onActionFirmwareFlasher();  // Tools → Firmware Flasher…
    void onActionLocoConfig();       // Tools → Loco Configuration…

    // ---- Live-applied settings --------------------------------------
    void onThemeChanged(QString themeName);
    void onPerTabCapacityChanged(int newCap);
    void onOfflineThresholdsChanged(int warnSec, int errSec);

    // UI handlers (auto-connected by name).
    void on_cbDatetime_stateChanged(int);
    void on_cbScrollLock_stateChanged(int);
    void on_pbSaveBtn_clicked();
    void onSaveFinished(const QString &filePath,
                        const QString &rawPath = QString(),
                        int rawRecords = 0);
    void onSaveFailed  (const QString &filePath, const QString &reason);
    void onDiskQuotaChanged(bool suspended, qint64 used, qint64 limit);
    void on_pbClearTabLogs_clicked();
    void on_pbclearAllLogs_clicked();
    void on_tabWidget_currentChanged(int index);
    void on_pbCheckBuffer_clicked();
    void on_pbConvert_clicked();
    void on_pbDetach_clicked();

private:
    // One per known source. NOT one per visible tab — a hidden tab still
    // has a TabUi entry, with its `visible` flag set to false. That way
    // when a new message arrives for a hidden source, we know to make
    // its tab visible again, and the model + last-seen tracking
    // continue uninterrupted while it's hidden.
    struct TabUi {
        QString     tabKey;
        QTableView *view      = nullptr;
        FilterBar  *filterBar = nullptr;
        class TimelineRibbon  *ribbon = nullptr;
        class LaneBand        *lanes  = nullptr;   // session 121
        class MarkerScrollBar *marks  = nullptr;
        FindBar    *findBar   = nullptr;     // (Ctrl+F overlay) — hidden by default
        QWidget    *container = nullptr;
        QString     friendlyName;
        bool        scrollLock = false;
        bool        visible    = true;
        qint64      lastSeenMs = 0;
    };

    Ui::MainWindow *ui;

    // Owned pipeline objects.
    UDPCommunication  *m_receiver   = nullptr;
    MessageDispatcher *m_dispatcher = nullptr;
    // Create (or tear down) the disk writer to match the current setting.
    // Called at startup and whenever the operator toggles continuous
    // logging in Settings. A disabled writer is destroyed rather than
    // merely idled, so it holds no file handles and no NameMap copy.
    void applyDiskLoggingSetting(bool enabled);

    // Window geometry, dock arrangement and column widths. Restored at the
    // very end of construction (docks must exist first) and saved on close.
    void restoreLayout();
    void saveLayout();
    // Debounced save. Layout changes arrive in bursts while a dock is being
    // dragged or a column resized, and closeEvent alone is not enough — a
    // diagnostic box that gets killed rather than closed would lose the
    // arrangement every time.
    void scheduleLayoutSave();
    QTimer *m_layoutSaveTimer = nullptr;
    // Apply the stored column widths to a newly created tab's view, so a
    // source that appears an hour into a session matches the others.
    void applyColumnWidths(QTableView *view) const;

    // Row height + wrap behaviour, applied to one view or to every open
    // tab. Called on tab creation and whenever the density changes.
    void applyDensity(QTableView *view) const;
    void applyDensityToAllTabs();
    // Hide/show columns per the stored set, on one view or all tabs.
    void applyColumnVisibility(QTableView *view) const;
    void applyColumnVisibilityToAllTabs();
    // Entries for every selected row, in displayed order.
    QVector<LogEntryPtr> selectedEntries(QTableView *view,
                                         const QString &tabKey) const;

    // ---- "open this buffer over there" -------------------------------
    //
    // The text handed over for one entry comes from entryBufferText()
    // (logentry.h), so the row menu, the raw-bytes panel and anything else
    // that grows this feature all send the same thing.
    //
    // Open (or raise) the workbench / packet maker on `bufferText`. Both
    // reuse the window last opened this way while it is still alive: a
    // right-click that spawns a new window every time buries the desktop,
    // and the whole point is to look at one frame after another. The Tools
    // menu still opens fresh, independent windows.
    void openBufferInWorkbench(const QString &bufferText);
    void openBufferInPacketMaker(const QString &bufferText);
    // Same two, from an entry; report through the notification centre when
    // the entry carries no bytes worth sending.
    void openEntryInWorkbench(const LogEntryPtr &entry);
    void openEntryInPacketMaker(const LogEntryPtr &entry);
    // The entry under the current selection of the active tab, or null.
    LogEntryPtr currentSelectedEntry() const;

    // Diff the two selected rows, or open the window empty. Reuses the window
    // it last opened: comparing frame after frame is the normal way to use it.
    void openFrameDiff(const LogEntryPtr &a, const LogEntryPtr &b);
    void openFrameDiff(const QVector<LogEntryPtr> &entries);

    // Every entry in every tab, hidden ones included, as pointer copies.
    // The round-trip validator scans the log the operator actually has,
    // and a hidden tab is still traffic that was captured.
    QVector<LogEntryPtr> allLogEntries() const;

    // The tabs that were open last time, restored empty and in order.
    // Saved on close; see Settings::workspaceTabs for what is deliberately
    // left out of it.
    void saveWorkspace() const;
    void restoreWorkspace();

    // ---- session 79: layouts, crash recovery, undo, settings transfer ----
    // (mainwindowsession.cpp)
public:
    // The arrangement now, and putting one back (View ▸ Layouts, crash
    // recovery). Public for the menu audit.
    WorkspaceSnapshot captureWorkspace() const;
    void applyWorkspace(const WorkspaceSnapshot &snapshot);
    class UndoLog *undoLog() const { return m_undo; }
    // What the last start recovered after an unclean exit ("" if nothing).
    QString recoveredSummary() const { return m_recoveredSummary; }
    // Write the crash-recovery snapshot now if it changed (the timer's job;
    // public so the audit need not wait for it).
    void writeRecoverySnapshot(bool force = false);
    // Layout commands without their dialogs, for the audit.
    bool saveCurrentLayoutAs(const QString &name);
    bool switchToLayout(const QString &name);
    bool deleteLayout(const QString &name);
    // Settings import without the file dialog: the chosen sections of a
    // bundle file, undoable. Returns false with the reason in *error.
    bool importSettingsFrom(const QString &path, const QStringList &sectionIds, QString *error);
private:
    void setupSessionFeatures(QMenu *fileMenu, QMenu *editMenu, QAction *editFirst,
                              QMenu *viewMenu, QMenu *themeMenu);
    void rebuildLayoutsMenu();
    void onActionSaveLayoutAs();
    void onActionExportSettings();
    void onActionImportSettings();
    void onToggleColorBlindSafe(bool on);
    void afterSettingsImported(const QStringList &sectionIds);
    void showUndoOffer(const QString &label);
    void pushBookmarkUndo(const QString &label, const QVector<Bookmark> &before);

    class UndoLog     *m_undo = nullptr;
    class ClockSkewAlarm *m_skewAlarm = nullptr;   // session 81
    ClockHistory       m_clockHistory;             // session 82
    int                m_clockGapNow = INT_MIN;
    void recordClockHistory();
public:
    ClockSkewAlarm *skewAlarm() const { return m_skewAlarm; }
    const ClockHistory &clockHistory() const { return m_clockHistory; }
    void showClockHistory();
private:
    QAction           *m_actUndo = nullptr;
    class QToolButton *m_undoButton = nullptr;
    QTimer            *m_undoOfferTimer = nullptr;
    QTimer            *m_recoveryTimer = nullptr;
    QMenu             *m_layoutsMenu = nullptr;
    QAction           *m_actColorBlind = nullptr;
    QByteArray         m_lastRecoveryJson;
    QString            m_recoveredSummary;
    QString            m_lastLayout;

    QPointer<FrameDiffWindow>   m_frameDiff;
    // Held so live entries can be fed to a running sweep: the sweep scores each
    // value by what arrived after it, which only this window sees.
    QPointer<FieldSweepDialog>  m_fieldSweep;
    QPointer<RoundTripWindow>   m_roundTrip;
    QPointer<DecodeWorkbench>   m_bufferWorkbench;
    // One flasher at a time: two windows flashing the same chassis would
    // interleave their META/DATA on the wire. Reopening raises this one.
    QPointer<FlasherWindow>     m_flasher;
    // One Loco Configuration window: its configurations file is edited in
    // place, and two windows would save over each other.
    QPointer<LocoConfigWindow>  m_locoConfig;
    MinimizedDock              *m_windowDock = nullptr;   // chips for minimised tool windows
    class StatusPins           *m_statusPins = nullptr;   // live values pinned from the Loco Console

    // ---- pop-out windows (live copies of tabs) --------------------------------
    // One per tab key. Keys still open at shutdown are saved and reopened on
    // the next start as soon as that loco's tab appears (m_pendingPopouts).
    QHash<QString, QPointer<TabPopoutWindow>> m_popouts;
    QStringList m_pendingPopouts;
    bool        m_shuttingDown = false;
    int         m_tabDragIndex = -1;     // tab under a left-button press
    QPoint      m_tabDragStart;
    void popOutTab(const QString &key, const QPoint &globalPos = QPoint());
    void savePopoutKeys() const;
    void reopenPendingPopout(const QString &key);

    // ---- tab colour tags --------------------------------------------------------
    void applyTabTag(const QString &key);    // empty key: every tab
    void applyAllTabTags();
    QPointer<PacketMakerDialog> m_bufferPacketMaker;


    class SearchWindow *m_searchWin = nullptr;   // non-modal, reused
    class MergedWindow *m_mergedWin = nullptr;   // WA_DeleteOnClose

    BookmarkStore  m_bookmarks;
    class FieldInspector *m_fieldPanel   = nullptr;
    QDockWidget          *m_fieldDock    = nullptr;
    QDockWidget          *m_failDock     = nullptr;
    QListWidget          *m_failList     = nullptr;
    // Parallel to m_failList rows. Bounded — see the cap in the .cpp.
    QVector<QPair<QString, qint64>> m_failRefs;

    // Scalable index of every source seen this session.
    //
    // The tab bar stops being usable somewhere around fifteen tabs, and the
    // dispatcher's ceiling is 512 — so the tab bar cannot be the primary
    // way to reach a source. This list is: it is filterable, shows message
    // count and silence, and opens a tab on demand. The tab bar becomes
    // "what I have open" rather than "everything that exists".
    // Single feedback surface. notify() replaces the five-second status
    // label; everything it is given is kept in a readable history.
    // Shown in place of the tab area before any traffic arrives.
    //
    // A blank pane is the least informative thing a diagnostic tool can
    // present at exactly the moment the user most needs orientation: on
    // first launch it says nothing about whether the app is listening,
    // on which port, or whether the silence is a fault. All of that is in
    // the status bar, which is not where the eye goes.
    QLabel *m_emptyState   = nullptr;
    bool    m_bindOk       = false;
    quint16 m_boundPort    = 0;
    QString m_bindError;
    void updateEmptyState();

    // Watches live traffic for the conditions a test case asserts. Fed
    // from the same batched signal as everything else.
    TestAssertionEngine m_tests;

    class NotificationBar *m_notify = nullptr;
    void notify(NoteLevel level, const QString &text,
                const QString &detail = QString());

    QDockWidget   *m_sourceDock   = nullptr;
    QLineEdit     *m_sourceFilter = nullptr;
    QTreeWidget   *m_sourceList   = nullptr;
    void rebuildSourceList();
    void refreshSourceList();

    QDockWidget   *m_bookmarkDock = nullptr;
    QListWidget   *m_bookmarkList = nullptr;
    // Step direction state for F2/Shift+F2 within the current tab.
    void stepBookmark(int direction);
    LogWriter         *m_writer     = nullptr;
    // Latched from the writer's quota signal so updateStatusBar() can show
    // the suspension without polling across a thread boundary.
    bool               m_diskQuotaSuspended = false;
    qint64             m_diskQuotaUsed  = 0;
    qint64             m_diskQuotaLimit = 0;
    ColorRules         m_colorRules;
    LocoIdentity       m_locoIdentity;
    NameMap            m_nameMap;

    QHash<QString, TabUi> m_tabs;          // tabKey → state (visible OR hidden)
    QHash<QString, int>   m_tabIndex;      // tabKey → current tab widget index
                                            // (only valid while visible)

    // Status-bar widgets.
    QLabel *m_lblBindStatus = nullptr;
    QLabel *m_lblQueueDepth = nullptr;
    QLabel *m_lblDropStatus = nullptr;
    QLabel *m_lblDiskStatus = nullptr;
    class PinPanel   *m_pinPanel = nullptr;
    QDockWidget      *m_pinDock  = nullptr;
    class WatchPanel *m_watchPanel = nullptr;
    QDockWidget      *m_watchDock  = nullptr;
    // Whether the floating pin window has been given a position yet. Placed
    // once, on first show; after that it is the operator's to move.
    bool              m_pinPlaced = false;

    QLabel *m_lblStationClock = nullptr;
    QLabel *m_lblClockGap     = nullptr;

    // The equipment's clock, read out of FRAME_NUM, and how far this laptop
    // has drifted from it. Standing state, not an event, so it is a plain
    // label and not a StatusLine: it is true until the next frame changes it
    // and must never expire.
    QLabel *m_lblFrameClock = nullptr;
    // Raised by the field inspector's context menu; see the definitions.
    // Refill the pin panel's field and source choosers. Cheap enough to run
    // on every tab change; see the definition for why it samples.
    void    refreshPinChoices();

    // Which packets carry a field. Opened from the Schema menu with nothing
    // filled in, or from a field the operator right-clicked.
    void    showFieldIndex(const QString &prefill);
    void    refreshFieldIndex();
    class FieldIndexDialog *m_fieldIndex = nullptr;

    void    pinField(const QString &fieldName);
    void    pinFieldNarrowed(const QString &fieldName, const QString &tabKey,
                             const QString &captype);
    void    plotField(const QString &fieldName);
    // Same, against a named source rather than whichever tab is in front.
    void    plotFieldIn(const QString &tabKey, const QString &fieldName);

    void    refreshFrameClock();

    // Paints one clock label — loco or station. Both go through it so the two
    // are presented identically, which is what makes them comparable at a
    // glance.
    void    paintClockLabel(QLabel *lbl, const QString &who,
                            const FrameNumberWatch::Seen &seen, qint64 ageMs);

    // Right-side dock.
    QDockWidget   *m_rawDock  = nullptr;
    RawBytesPanel *m_rawPanel = nullptr;

    // Menu actions (held so we can toggle their state).
    QAction *m_actSettings    = nullptr;
    QAction *m_actToggleTheme = nullptr;
    QList<QAction *> m_themeActions;   // View > Theme, one per ThemeUtil::all()
    QMenu   *m_menuWindow     = nullptr;   // populated dynamically with hidden tabs

    QTimer *m_statusTimer    = nullptr;
    QTimer *m_userLabelTimer = nullptr;

    FrameNumberWatch *m_frameWatch = nullptr;
    class TabTags    *m_tabTags = nullptr;
    class SessionKeyStore *m_sessionKeys = nullptr;
    // Session 102: the serial ports, running whether or not a terminal
    // window is open; a status-bar chip per open port.
    class SerialManager *m_serial = nullptr;
    QWidget *m_serialChips = nullptr;
    QAction *m_serialChipsAction = nullptr;   // its slot in the top strip (session 118)
    // Session 119: each source's count and when, for its rate.
    QHash<QString, QPair<int, qint64>> m_sourceRates;
    bool m_lanesOn = true;                    // View > Lanes over the log (session 121)
    // Session 118: the frame — top strip, rail, log header.
    class QToolBar *m_strip = nullptr;
    class QToolBar *m_rail = nullptr;
    QLabel *m_headerTitle = nullptr;
    QLabel *m_headerMeta = nullptr;
    class QToolButton *m_moreBtn = nullptr;   // session 96: owned here, fed by live traffic      // session 96: owned here, shared with pop-outs and loco consoles   // session 95: owned here, handed to the Packet Makers
    quint64 m_lastReportedMalformed = 0;   // session 90: drops by cause
    quint64 m_lastReportedQueueFull = 0;
    int     m_perTabCapacity    = 200'000;

    // Live tunables (mirrored from Settings, updatable via the dialog).
    int   m_offlineWarnSec = 10;
    int   m_offlineErrSec  = 30;
    Theme m_theme          = Theme::Light;

    // Session-only memory of the last export configuration. We don't
    // persist these to INI because column choices are usually
    // situational ("this analysis I want bytes; the next one minimal");
    // a permanent setting would feel wrong.
    Exporter::Format      m_lastExportFormat   = Exporter::CSV;
    Exporter::ColumnFlags m_lastExportColumns  = Exporter::ColAll;

    // Helpers.
    void   buildOrShowTab(const QString &tabKey, const QString &friendlyName);
    void   gotoProblem(int dir);     // shared by the two slots above
    void   showTab(TabUi &tab);
    void   hideTab(TabUi &tab);
    void   updateLogCount();
    void   emitDropBanner(quint64 newMalformed, quint64 newQueueFull);
    QString hexToAsciiString(const QString &hexData) const;
    QString currentTabKey() const;
    LogEntryPtr entryFromProxyIndex(const QModelIndex &proxyIndex) const;
    // Session 84: tell the DMI time-travel broker where the cursor is.
    void offerDmiMoment(const QString &tabKey, const QModelIndex &proxyIndex);

    // Session 89: construction pieces, each in its own file.
    struct MenuRoots {                 // what the rest of the constructor needs back
        QMenu   *file = nullptr, *edit = nullptr, *view = nullptr, *theme = nullptr;
        QAction *find = nullptr;
    };
    MenuRoots buildMenus();            // mainwindow_menus.cpp
    void buildStatusBar();             // mainwindow_status.cpp
    void buildShell();                 // mainwindow_shell.cpp (session 118)
    void refreshHeader();              // the log header's source name
    void openSerialTerminal(const QString &port = QString());   // mainwindow_tools.cpp
    void rebuildSerialChips();                                   // mainwindow_status.cpp
    void refreshSerialChips();                                   // tooltips and health, each second
    // Open saved serial profiles (all, or only the auto-open ones); a
    // failure is a notification naming the profile, never a dialog.
    void openSerialProfiles(bool autoOpenOnly);                  // mainwindow_tools.cpp

    // 2f: refresh tab text colors based on staleness + visibility.
    void refreshTabHealth();
    void rebuildWindowMenu();   // populated with currently-hidden tabs
};

#endif // MAINWINDOW_H
