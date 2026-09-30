#ifndef CLOCKSKEWALARM_H
#define CLOCKSKEWALARM_H

// =============================================================================
//  ClockSkewAlarm (session 81)
//  -----------------------------------------------------------------------------
//  The status bar already shows the loco's and the station's clocks (from
//  FRAME_NUM) and how far apart they are, in red when the gap is outside the
//  window each end accepts: a packet more than 4 s old, or 2 s or more in the
//  future, is discarded. A red label is easy to miss during a run, and the
//  symptom it explains — each end silently ignoring the other — looks like
//  a radio problem.
//
//  This turns the gap into EPISODES: raised when the gap has been outside
//  the window for `holdMs` (a single late frame is not a clock problem),
//  cleared when it has been back inside for `holdMs`. Each episode keeps
//  when it started and ended and the worst gap, for the notification and
//  for the run report.
//
//  No data is not "in step": while either clock is unheard the state does
//  not change. Pure logic, fed from MainWindow's clock refresh; tested
//  directly.
// =============================================================================

#include "frameclock.h"

#include <QObject>
#include <QWidget>
#include <QString>
#include <QVector>

class ClockSkewAlarm : public QObject
{
    Q_OBJECT
public:
    struct Episode {
        qint64 startMs = 0;
        qint64 endMs   = -1;        // -1 while still outside
        int    worstGap = 0;        // seconds, signed (loco minus station)
        FrameClock::Accept kind = FrameClock::Accept::Stale;
        bool   open() const { return endMs < 0; }
        qint64 durationMs(qint64 nowMs) const { return (open() ? nowMs : endMs) - startMs; }
    };

    explicit ClockSkewAlarm(int holdMs = 3000, QObject *parent = nullptr);

    // One reading: `gapSeconds` = loco minus station. `valid` false when a
    // clock is not being heard.
    void sample(qint64 nowMs, bool valid, int gapSeconds);

    bool isRaised() const { return m_raised; }
    const QVector<Episode> &episodes() const { return m_episodes; }   // oldest first
    void clear();

    // "Loco 11391 s ahead of the station" / "... behind ..."
    static QString gapText(int gapSeconds);
    static QString durationText(qint64 ms);

signals:
    void raised(const ClockSkewAlarm::Episode &episode);
    void cleared(const ClockSkewAlarm::Episode &episode);

private:
    int    m_holdMs;
    bool   m_raised = false;
    qint64 m_outsideSince = -1;
    qint64 m_insideSince  = -1;
    int    m_worstPending = 0;
    FrameClock::Accept m_kindPending = FrameClock::Accept::Stale;
    QVector<Episode> m_episodes;
};

// =============================================================================
//  ClockHistory (session 82) — the clocks over the last half hour, for the
//  graph behind the frame-clock labels: loco minus station, and each of
//  them against this laptop. One sample a second at most.
// =============================================================================
struct FieldSeries;

class ClockHistory
{
public:
    struct Sample {
        qint64 ms = 0;
        bool   hasGap = false;  int gap = 0;         // loco − station, s
        bool   hasLoco = false; int locoSkew = 0;    // loco − laptop, s
        bool   hasStn = false;  int stnSkew = 0;     // station − laptop, s
    };
    static constexpr qint64 kKeepMs = 30LL * 60 * 1000;
    static constexpr qint64 kMinStepMs = 1000;

    // Adds unless the last sample is under kMinStepMs old; drops what is
    // older than kKeepMs. Returns whether it was added.
    bool add(const Sample &s);
    const QVector<Sample> &samples() const { return m_samples; }
    // The three as plot series (only those with any samples).
    QVector<FieldSeries> toSeries() const;

private:
    QVector<Sample> m_samples;
};

class FieldPlotCanvas;
class QLabel;
class QTimer;

// The graph: the three clocks on one axis in seconds, refreshed live.
class ClockHistoryWindow : public QWidget
{
    Q_OBJECT
public:
    ClockHistoryWindow(const ClockHistory *history, QWidget *parent = nullptr);
    FieldPlotCanvas *canvas() const { return m_canvas; }
public slots:
    void refresh();
private:
    const ClockHistory *m_history = nullptr;
    FieldPlotCanvas *m_canvas = nullptr;
    QLabel *m_note = nullptr;
    QTimer *m_timer = nullptr;
};

#endif // CLOCKSKEWALARM_H
