#ifndef LANEBAND_H
#define LANEBAND_H

// =============================================================================
//  LaneBand (session 121, UI revamp step 5) — what happened when, over the log
//  -----------------------------------------------------------------------------
//  A strip of lanes above a tab's log for its newest stretch of traffic:
//    Mode     the loco's mode as segments, named
//    Safety   emergency spells (fail) and overspeed (warning)
//    RFID     the last tag read, as a span from its read to the next one,
//             named (session 164; was a tick per read)
//    Link     gaps where the source went quiet
//    Faults   raised (fail) and cleared (ok)
//  Click a lane to jump the log to that moment; hover a mark for what it is.
//
//  CUSTOM LANES (session 164)
//    Right-click ▸ Add lane ▸ packet ▸ field. One list for all tabs, owned
//    by MainWindow (ui/customLanes) and handed to every band; a tab whose
//    traffic does not carry that packet simply does not show the lane.
//    Drawn by what the values are: a field whose values render as names
//    (enums, flags) or that takes at most kMaxSpanValues distinct values
//    is drawn as named spans, like Mode; a measured number (with a unit:
//    km/h, m) or one taking many values as a small line graph, its value on
//    hover. Right-click a custom lane ▸ Remove.
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

// A custom lane: one field of one packet type ("lsrp", "TRAIN_SPEED").
struct CustomLane {
    QString type;
    QString field;
    QString key() const { return type + QLatin1Char('\t') + field; }
    bool operator==(const CustomLane &o) const { return type == o.type && field == o.field; }
};

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
    // Session 164: the custom lanes (MainWindow's list, the same for every tab).
    void setCustomLanes(const QVector<CustomLane> &lanes);
    const QVector<CustomLane> &customLanes() const { return m_customDefs; }
    static constexpr int kMaxSpanValues = 8;   // more distinct numbers: a graph

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

    // Session 164: the custom lanes this tab shows (those with data), and
    // whether each is drawn as a graph. For tests.
    QVector<CustomLane> shownCustomLanes() const;
    bool customLaneIsGraph(int i) const;
    // The context menu's choices, without the menu (tests): the packet ▸
    // field catalogue of this tab, and the custom lane under a point.
    QHash<QString, QStringList> addableFields() const;
    int customLaneAt(const QPoint &p) const;   // -1 if none
    QRect laneRect(int lane) const { return track(lane); }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void timeClicked(qint64 epochMs);
    // Session 164: from the right-click menu. MainWindow changes its list
    // and hands it to every band.
    void addLaneRequested(const CustomLane &lane);
    void removeLaneRequested(const CustomLane &lane);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    bool event(QEvent *e) override;
    void showEvent(QShowEvent *) override;

private:
    QRect track(int lane) const;
    int xFor(qint64 ms, const QRect &r) const;
    qint64 msFor(int x, const QRect &r) const;

    int laneCount() const { return kFixedLanes + m_custom.size(); }
    int labelWidth() const;
    static constexpr int kFixedLanes = 5;

    // One custom lane's samples in the window.
    struct LaneData {
        CustomLane       def;
        bool             graph = false;
        QVector<qint64>  ms;
        QVector<double>  value;      // graph
        QVector<QString> label;      // spans: the rendered value
        double           lo = 0, hi = 0;
    };
    void buildCustom();
    QVector<CustomLane> m_customDefs;
    QVector<LaneData>   m_custom;    // only the lanes with samples

    QPointer<LogModel> m_model;
    QString m_key, m_name;
    RunReport::Summary m_sum;
    qint64 m_from = 0, m_to = 0;
    QTimer *m_debounce = nullptr;
    bool m_dirty = true;
    bool m_enabled = true;
};

#endif // LANEBAND_H
