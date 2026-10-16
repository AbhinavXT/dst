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
//
//  Session 291: several VCCs run at once, so the page keeps what it knows
//  per entry (an EntryView). The batch column has a row per VCC with its
//  phase and progress; the centre shows one of them in full: the selected
//  row (click another to switch). One card is simply a batch of one.
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
    // `title` replaces the card's name (session 291: "VCC 192.168.25.169").
    FlasherBatchRow(int cardType, const QString &fileName, QWidget *parent = nullptr,
                    const QString &title = QString());
    void setOutcome(Flasher::CardOutcome outcome, const QString &statusText);
    Flasher::CardOutcome outcome() const { return m_outcome; }
    QString statusText() const;

    // Session 291: the second line, when the file is the same on every row
    // (several VCCs): "Send 45 % · 980 KB/s · 12 resent".
    void setDetail(const QString &text);
    QString detail() const { return m_fileName; }

    // The row the centre is showing (several VCCs), marked with a bar.
    void setShown(bool shown);
    bool isShown() const { return m_shown; }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;   // re-elides the file name to the width it has
    void mousePressEvent(QMouseEvent *event) override;

private:
    Flasher::CardOutcome m_outcome = Flasher::CardOutcome::Queued;
    QLabel *m_status = nullptr;
    QLabel *m_file = nullptr;
    QString m_fileName;
    bool    m_shown = false;
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

    // Session 291: one entry's reports, while several run at once. The
    // index-less slots below report for the entry begun last.
    void onProgressFor(int index, const kflash::Progress &progress);
    void onBitmapFor(int index, const QByteArray &bitmap, quint32 totalBlocks);
    void appendLogFor(int index, int level, const QString &text);

    // Which entry the centre shows; a click on a row does this.
    void showEntry(int index);
    int  shownEntry() const { return m_shown; }
    int  rowCount() const { return m_batchRows.size(); }
    FlasherBatchRow *row(int index) const { return m_batchRows.value(index); }
    QString waitText() const;   // the power-cycle prompt, for the tests

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
    QString cardTitle() const;
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
    // What the page knows of one entry (session 291).
    struct EntryView {
        int               cardType = 0;
        QString           title;
        QString           subtitle;
        quint32           totalBlocks = 0;
        QElapsedTimer     clock;
        kflash::Progress  lastProgress;
        bool              haveProgress = false;
        kflash::Phase     phase = kflash::Phase::Idle;
        qint64            phaseStartMs[5] = { 0, 0, 0, 0, 0 };   // indexed by kflash::Phase
        qint64            phaseEndMs[5] = { 0, 0, 0, 0, 0 };
        bool              finished = false;
        bool              succeeded = false;
        qint64            endedMs = 0;   // the clock when it finished
        QByteArray        bitmap;
    };
    bool     parallel() const { return m_views.size() > 1; }
    bool     anyRunning() const;
    void     updateRow(int index);
    void     updateBatchCount();
    void     showStats(const EntryView &view);

    QWidget *buildBatchColumn();
    QWidget *buildCentre();
    QWidget *buildStatsColumn();
    QFrame  *makeStatCard(const QString &title, QLabel **value, QLabel **sub);
    void     restyle();
    void     updateStepper();
    void     updateWaitPrompt();

    // ---- the entries --------------------------------------------------------
    QVector<EntryView> m_views;
    int               m_currentIndex = -1;   // the entry begun last
    int               m_shown = -1;          // the entry the centre shows

    // ---- log ---------------------------------------------------------------
    QStringList       m_logLines;

    // ---- widgets -----------------------------------------------------------
    QVBoxLayout       *m_batchRowsLayout = nullptr;
    QVector<FlasherBatchRow *> m_batchRows;
    QLabel            *m_batchCount = nullptr;
    QLabel            *m_batchTitle = nullptr;
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
