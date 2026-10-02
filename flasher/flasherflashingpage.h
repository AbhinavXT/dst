#ifndef FLASHERFLASHINGPAGE_H
#define FLASHERFLASHINGPAGE_H
// =============================================================================
//  flasherflashingpage.{h,cpp} -- page 1 of the Firmware Flasher
//  (Flashing.dc.html)
//
//  Everything on this page is a VIEW of the run. It never talks to the
//  engine: the window forwards FlashWorker's signals here (progress, bitmap,
//  log lines) and forwards the two buttons back as signals. Keeping the page
//  passive means the batch rules stay in one place (BatchPlan) and the page
//  can be driven in a test with made-up Progress values.
// =============================================================================
#include <QElapsedTimer>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include "flashercore.h"

class BlockMapWidget;
class QFrame;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTimer;
class QVBoxLayout;

// -----------------------------------------------------------------------------
//  The four-step phase bar: Session -> Send + early repair -> Repair -> Accepted
// -----------------------------------------------------------------------------
class FlasherPhaseStepper : public QWidget
{
    Q_OBJECT
public:
    explicit FlasherPhaseStepper(QWidget *parent = nullptr);

    enum Step { StepSession = 0, StepSend, StepRepair, StepAccepted, StepCount };
    enum class StepState { Pending, Active, Done, Failed };

    void reset();
    void setStep(int step, StepState state, double fraction, const QString &label, const QString &time);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    // For the tests.
    StepState stateOf(int step) const { return m_states[step]; }
    QString   labelOf(int step) const { return m_labels[step]; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    StepState m_states[StepCount];
    double    m_fractions[StepCount];
    QString   m_labels[StepCount];
    QString   m_times[StepCount];
};

// -----------------------------------------------------------------------------
//  One card in the batch column: status dot, name, file, status word.
// -----------------------------------------------------------------------------
class FlasherBatchRow : public QWidget
{
    Q_OBJECT
public:
    FlasherBatchRow(int cardType, const QString &fileName, QWidget *parent = nullptr);
    void setOutcome(Flasher::CardOutcome outcome, const QString &statusText);
    Flasher::CardOutcome outcome() const { return m_outcome; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;   // re-elides the file name to the width it has

private:
    Flasher::CardOutcome m_outcome = Flasher::CardOutcome::Queued;
    QLabel *m_status = nullptr;
    QLabel *m_file = nullptr;
    QString m_fileName;
};

class FlasherFlashingPage : public QWidget
{
    Q_OBJECT
public:
    explicit FlasherFlashingPage(QWidget *parent = nullptr);

    // A new run: rebuild the card column, clear the log.
    void beginBatch(const Flasher::BatchPlan &plan);

    // A card is starting: header, block map, stepper, stats reset.
    void beginCard(int index, const Flasher::BatchEntry &entry);

    // A card has finished (the plan has already recorded the outcome).
    void finishCard(int index, const Flasher::BatchEntry &entry);

    // Mark the not-run cards once the batch ends.
    void refreshBatchRows(const Flasher::BatchPlan &plan);

    // How long the engine will keep calling the updater (the profile's
    // updaterWaitSeconds), for the power-cycle countdown.
    void setUpdaterWaitMs(qint64 waitMs) { m_updaterWaitMs = waitMs; }

    // Button feedback, driven by the window once it has acted.
    void setAborting();

    // For the tests: is the "power-cycle the chassis now" prompt showing?
    bool waitingForUpdater() const;

    // "Repair round N of M": M comes from the tuning the card runs with.
    void setMaxRounds(int rounds) { m_maxRounds = rounds; }

    BlockMapWidget *blockMap() const { return m_blockMap; }
    QStringList logLines() const { return m_logLines; }   // plain text, for history and Save .log

public slots:
    void onProgress(const kflash::Progress &progress);
    void appendLog(int level, const QString &text);

signals:
    void abortRequested();

private slots:
    void tick();              // elapsed clock and phase times
    void copyLog();
    void saveLog();

private:
    QWidget *buildBatchColumn();
    QWidget *buildCentre();
    QWidget *buildStatsColumn();
    QFrame  *makeStatCard(const QString &title, QLabel **value, QLabel **sub);
    void     restyle();
    void     updateStepper();
    void     updateWaitPrompt();

    // ---- current card ----------------------------------------------------
    int               m_currentIndex = -1;
    int               m_currentCardType = 0;
    QElapsedTimer     m_cardClock;
    kflash::Progress  m_lastProgress;
    bool              m_haveProgress = false;
    kflash::Phase     m_phase = kflash::Phase::Idle;
    qint64            m_phaseStartMs[5] = { 0, 0, 0, 0, 0 };   // indexed by kflash::Phase
    qint64            m_phaseEndMs[5] = { 0, 0, 0, 0, 0 };
    bool              m_cardFinished = false;
    bool              m_cardSucceeded = false;

    // ---- log ---------------------------------------------------------------
    QStringList       m_logLines;

    // ---- widgets -----------------------------------------------------------
    QVBoxLayout       *m_batchRowsLayout = nullptr;
    QVector<FlasherBatchRow *> m_batchRows;
    QLabel            *m_batchCount = nullptr;
    QPushButton       *m_abortButton = nullptr;
    QLabel            *m_cardTitle = nullptr;
    QLabel            *m_cardSubtitle = nullptr;
    QLabel            *m_elapsed = nullptr;
    QFrame            *m_waitBanner = nullptr;
    QLabel            *m_waitText = nullptr;
    FlasherPhaseStepper *m_stepper = nullptr;
    BlockMapWidget    *m_blockMap = nullptr;
    QLabel            *m_legendHeld = nullptr;
    QLabel            *m_legendRepaired = nullptr;
    QLabel            *m_legendMissing = nullptr;
    QLabel            *m_statBlocks = nullptr;
    QLabel            *m_statBlocksSub = nullptr;
    QLabel            *m_statRound = nullptr;
    QLabel            *m_statRoundSub = nullptr;
    QLabel            *m_statResent = nullptr;
    QLabel            *m_statResentSub = nullptr;
    QLabel            *m_statRate = nullptr;
    QLabel            *m_statRateSub = nullptr;
    QLabel            *m_statRtt = nullptr;
    QLabel            *m_statRttSub = nullptr;
    QPlainTextEdit    *m_log = nullptr;
    QTimer            *m_tickTimer = nullptr;
    QList<QFrame *>    m_cards;
    QList<QLabel *>    m_sectionLabels;
    int                m_maxRounds = 100;
    qint64             m_updaterWaitMs = 60000;
};

#endif  // FLASHERFLASHINGPAGE_H
