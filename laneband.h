#ifndef LANEBAND_H
#define LANEBAND_H

// =============================================================================
//  LaneBand (session 121, UI revamp step 5) — what happened when, over the log
//  -----------------------------------------------------------------------------
//  A strip of lanes above a tab's log for its newest stretch of traffic:
//    Mode     the loco's mode as segments, named
//    Safety   emergency spells (fail) and overspeed (warning)
//    RFID     each tag read, numbered
//    Link     gaps where the source went quiet
//    Faults   raised (fail) and cleared (ok)
//  Click a lane to jump the log to that moment; hover a mark for what it is.
//
//  NOTHING NEW IS DECODED HERE
//    It draws RunReport::summarise() — the engine the run summary and the
//    incident pack already use — over a window, so the lanes can never say
//    something different from those reports.
//
//  COST
//    summarise() decodes the frames in its window, so the window is bounded:
//    the newest 15 minutes, or the newest 20,000 rows if that is shorter. It
//    rebuilds at most every 3 s, and only while visible. A source with
//    nothing the lanes can show (text diagnostics) shows no band at all.
// =============================================================================

#include <QPointer>
#include <QWidget>

#include "runreport.h"

class LogModel;
class QTimer;

class LaneBand : public QWidget
{
    Q_OBJECT
public:
    explicit LaneBand(QWidget *parent = nullptr);

    void setModel(LogModel *model, const QString &tabKey, const QString &tabName);
    void refreshLater();             // debounced; nothing while hidden
    void rebuildNow();
    // View ▸ Lanes over the log. Off hides the band and skips the work.
    void setEnabled2(bool on) { m_enabled = on; m_dirty = true; rebuildNow(); }

    static constexpr qint64 kWindowMs = 15 * 60 * 1000;
    static constexpr int    kMaxRows  = 20000;

    // What is drawn, for tests.
    const RunReport::Summary &summary() const { return m_sum; }
    qint64 fromMs() const { return m_from; }
    qint64 toMs() const { return m_to; }
    bool hasContent() const;
    QStringList laneNames() const;
    // The mark under a point, as its tooltip would say it; empty if none.
    QString describeAt(const QPoint &p) const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void timeClicked(qint64 epochMs);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    bool event(QEvent *e) override;
    void showEvent(QShowEvent *) override;

private:
    QRect track(int lane) const;
    int xFor(qint64 ms, const QRect &r) const;
    qint64 msFor(int x, const QRect &r) const;

    QPointer<LogModel> m_model;
    QString m_key, m_name;
    RunReport::Summary m_sum;
    qint64 m_from = 0, m_to = 0;
    QTimer *m_debounce = nullptr;
    bool m_dirty = true;
    bool m_enabled = true;
};

#endif // LANEBAND_H
