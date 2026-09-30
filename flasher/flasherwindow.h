#ifndef FLASHERWINDOW_H
#define FLASHERWINDOW_H
// =============================================================================
//  FlasherWindow
//  -----------------------------------------------------------------------------
//  Tools ▸ Firmware Flasher…  --  flash .appimage builds onto a Kavach chassis
//  (VCC + the three IOA cards) over UDP.
//
//  SHAPE
//    A standalone QMainWindow like DecodeWorkbench, not a tab: a flash run
//    owns the link to the board and should feel like its own mode. Unlike
//    the workbench there is only ever ONE of these -- two windows flashing
//    the same chassis at once would interleave META/DATA on the wire -- so
//    MainWindow keeps a QPointer and raises the existing one.
//
//      QStackedWidget
//        0  FlasherQueuePage     choose images, pre-flight, "Flash N cards"
//        1  FlasherFlashingPage  the run: batch list, phases, block map, log
//        2  FlasherSummaryPage   what happened to each card
//      FlasherHistoryDialog      the audit trail (app bar ▸ History)
//      FlasherProfileDialog      one bench/site's settings (app bar ▸ ⚙)
//
//  THREADING
//    One QThread + one FlashWorker for the window's lifetime. The worker
//    runs ONE card per call (FlashEngine::run is blocking).
//
//  ONE CARD PER RUN
//    The Kavach updater runs only after a power-on, listens for 5 s, and
//    leaves for the application once a card is done (Boot_Switch jumps into
//    the app; it does not reset back into the updater). So every card needs
//    its own power cycle, and Flash sends to exactly one card: press Flash,
//    the engine keeps offering META for the profile's wait window, the
//    operator power-cycles the chassis, the updater answers. BatchPlan still
//    carries the run (a plan of one), so the Summary and history code paths
//    are the ones already tested.
//    Abort calls FlashWorker::requestCancel() DIRECTLY, not queued: the
//    worker thread is busy inside run() and would not see a queued call
//    until the card was over.
//
//  WHAT THIS WINDOW DOES NOT DO
//    It does not change the wire protocol (flash_engine.* is the handoff's,
//    unmodified) and it never touches DLConsole's capture socket: the engine
//    opens its own UDP socket on an ephemeral port.
// =============================================================================
#include <QMainWindow>
#include <QPointer>

#include "flashercore.h"

class FlashWorker;
class FlasherFlashingPage;
class FlasherHistoryDialog;
class FlasherQueuePage;
class FlasherSummaryPage;
class QButtonGroup;
class QComboBox;
class QFrame;
class QLabel;
class QStackedWidget;
class QThread;
class QToolButton;

class FlasherWindow : public QMainWindow
{
    Q_OBJECT
public:
    // `dataDirectory` is where the profiles file and the history log live.
    // Empty (the normal case) means beside dlconsole.ini; the tests pass a
    // temporary directory.
    explicit FlasherWindow(QWidget *parent = nullptr, const QString &dataDirectory = QString());
    ~FlasherWindow() override;

    bool isFlashing() const { return m_flashing; }

    enum Page { QueuePageIndex = 0, FlashingPageIndex = 1, SummaryPageIndex = 2 };
    int currentPage() const;

    // For the tests.
    FlasherQueuePage    *queuePage() const { return m_queuePage; }
    FlasherFlashingPage *flashingPage() const { return m_flashingPage; }
    FlasherSummaryPage  *summaryPage() const { return m_summaryPage; }
    const Flasher::BatchPlan &plan() const { return m_plan; }
    QString profilesPath() const;
    QString historyPath() const;

    // The same thing the Flash button does, minus the Engineer-mode name
    // confirmation box when `confirmNames` is false (the tests' path).
    bool startBatch(bool confirmNames = true);

    // Abort without the confirmation box (the Abort button asks first, then
    // calls this). Public for the tests.
    void abortCurrent();

signals:
    // Emitted when the run has been recorded, after history has been
    // written. The tests wait on this.
    void batchFinished();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onFlashRequested();
    void onProgress(const kflash::Progress &progress);
    void onLogLine(int level, const QString &text);
    void onCardFinished(const kflash::FlashResult &result);
    void onAbortRequested();
    void onModeChanged();
    void onProfileChosen(int comboIndex);
    void editProfile();
    void openHistory();
    void exportReport();
    void tryAgain();
    void refreshAppBarTarget();

private:
    QWidget *buildAppBar();
    void     restyleAppBar();
    void     setFlashingState(bool flashing);
    void     startNextCard();
    void     finishBatch();
    void     reloadProfileCombo();
    void     applyActiveProfile();
    void     saveProfiles();
    void     showPage(int page);

    // ---- data -----------------------------------------------------------------
    QString                 m_dataDirectory;
    Flasher::ProfileStore   m_profiles;
    Flasher::HistoryLog     m_history;
    Flasher::Mode           m_mode = Flasher::Mode::Engineer;

    // ---- the run --------------------------------------------------------------
    QThread                *m_thread = nullptr;
    FlashWorker            *m_worker = nullptr;
    Flasher::BatchPlan      m_plan;
    Flasher::FlashProfile   m_batchProfile;      // snapshot taken at batch start
    QString                 m_batchId;
    QString                 m_batchIp;
    quint16                 m_batchPort = 0;
    bool                    m_flashing = false;
    bool                    m_currentAlreadyHeld = false;
    bool                    m_closeWhenIdle = false;

    // ---- widgets --------------------------------------------------------------
    QFrame                 *m_appBar = nullptr;
    QLabel                 *m_appIcon = nullptr;
    QLabel                 *m_appTitle = nullptr;
    QLabel                 *m_profileLabel = nullptr;
    QComboBox              *m_profileCombo = nullptr;
    QToolButton            *m_engineerButton = nullptr;
    QToolButton            *m_operatorButton = nullptr;
    QButtonGroup           *m_modeGroup = nullptr;
    QLabel                 *m_flashingBadge = nullptr;
    QLabel                 *m_targetLabel = nullptr;
    QToolButton            *m_historyButton = nullptr;
    QToolButton            *m_gearButton = nullptr;
    QStackedWidget         *m_stack = nullptr;
    FlasherQueuePage       *m_queuePage = nullptr;
    FlasherFlashingPage    *m_flashingPage = nullptr;
    FlasherSummaryPage     *m_summaryPage = nullptr;
    QPointer<FlasherHistoryDialog> m_historyDialog;
    int                     m_profileComboPrevious = 0;
    class UndoLog *m_undo = nullptr;   // session 79: undo of Delete profile
public:
    UndoLog *undoLog();
};

#endif  // FLASHERWINDOW_H
