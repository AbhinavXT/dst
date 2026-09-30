#ifndef REPLAYWINDOW_H
#define REPLAYWINDOW_H

// =============================================================================
//  ReplayWindow  -  scrub a recorded .cap file (the @-capture stream).
//  Two axis modes:
//    Spatial   x = loco / RFID absolute location (m)  -> "where on the track"
//    Temporal  x = loco RTC time                      -> "when"
//  RFID reads are plotted as labelled markers on the axis; a draggable cursor
//  selects a moment, and the per-type field tabs show the decoded state as it
//  was at that point (latest frame of each type at or before the cursor).
// =============================================================================

#include <QMainWindow>
#include <QWidget>
#include <QVector>
#include <QHash>
#include <QMap>
#include <QPair>
#include <QStringList>

#include "capturedecoder.h"
#include "dmitimetravel.h"

class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QTabWidget;
class QTableWidget;
class QTimer;
class QKeyEvent;
class QStackedWidget;
class QLineEdit;

struct ReplayRec {
    CaptureLine  line;
    CaptureIndex idx;            // single-field seek values
    qint64       tMs    = 0;
    double       pos    = 0.0;
    bool         isRfid = false;
    RfidInfo     rfid;
    int          keyIndex = 0;   // which source/key (index into ReplayWindow::m_keys)
};

// A noteworthy occurrence derived from the record stream (one merged, ordered
// list across all packet types). Drives both the Event Log view and the
// glanceable colour ribbon on the timeline.
struct ReplayEvent {
    enum Sev { Info = 0, Warn = 1, Error = 2 };
    int     recIndex = 0;        // index into ReplayWindow::m_recs
    qint64  tMs      = 0;
    double  pos      = 0.0;
    QString key;                 // loco_ctrl
    QString type;                // packet type label
    Sev     sev      = Info;
    QString summary;
};

// Pure function: scan the records once and emit the ordered event list.
QVector<ReplayEvent> extractEvents(const QVector<ReplayRec> &recs);

// Snapshot of loco state at the cursor, derived from the latest arp/lsrp frame
// at or before it. Rendered as the moving "loco box" on the track view.
struct LocoVizState {
    bool    valid   = false;   // an arp/lsrp frame was found at/before the cursor
    bool    active  = false;   // this is the key the inspector / strip is focused on
    double  absLocM = 0.0;     // absolute location (metres) used to place the box
    qint64  tMs     = 0;       // time of the frame this box represents (temporal x)
    QString keyLabel;          // loco_ctrl, shown above the box
    QString locoId;
    QString speed;             // "45 km/h" / "unidentified"
    QString mode;              // "Full_Supervision"
    QString emergency;         // "No Emergency"
    QString trainLen;          // "320 m"
    QString lastTag;           // last RFID tag id
    QString srcLabel;          // "arp" / "lsrp"
    int     dir   = 0;         // 0 unknown, 1 nominal (right), 2 reverse (left)
    int     emSev = 0;         // 0 none, 1 warn, 2 critical
};

// ---- timeline strip (custom-painted axis + RFID markers + cursor) ----
class TimelineWidget : public QWidget
{
    Q_OBJECT
public:
    enum Mode { Spatial, Temporal };
    explicit TimelineWidget(QWidget *parent = nullptr);

    void setData(const QVector<ReplayRec> *recs);
    void setEvents(const QVector<ReplayEvent> *ev);   // colour ribbon source
    void setLocoStates(const QVector<LocoVizState> &states);  // one box per key
    void setProfile(const SlrpProfile &p);                    // SLRP look-ahead lane
    void setPlotSeries(const QVector<QPair<int,double>> &pts, const QString &label);
    void clearPlot();
    bool hasPlot() const { return m_hasPlot; }
    void setMode(Mode m);
    void setCursorIndex(int idx);
    int  cursorIndex() const { return m_cursor; }

signals:
    void cursorMoved(int index);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;

private:
    double valueOf(int i) const;
    int    nearestIndex(double v) const;
    int    valueToX(double v) const;
    double xToValue(int px) const;
    void   recomputeRange();
    void   updateMinHeight();   // sized for header + profile/plot lane + track

    const QVector<ReplayRec> *m_recs = nullptr;
    const QVector<ReplayEvent> *m_events = nullptr;
    QVector<LocoVizState> m_locos;           // one entry per key with a loco frame
    SlrpProfile m_profile;                   // active loco's latest SLRP look-ahead
    QVector<QPair<int,double>> m_plot;       // (record index, numeric value) for the field plot
    QString m_plotLabel;
    double  m_plotLo = 0.0, m_plotHi = 1.0;
    bool    m_hasPlot = false;
    Mode   m_mode   = Spatial;
    int    m_cursor = 0;
    double m_min = 0.0, m_max = 1.0;
    int    m_margin = 48;
};

class ReplayWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit ReplayWindow(const QString &capPath, QWidget *parent = nullptr);

    explicit ReplayWindow(const QStringList &capPaths, QWidget *parent = nullptr);

protected:
    // Remember size/position for the next opening.
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onCursorMoved(int index);
    void onModeChanged(int index);
    void onKeyChanged(int index);        // active-key (source) selector
    void onPlayPause();
    void onTick();
    void onJumpFieldChanged(int index);
    void onJumpGo();
    void onJumpStep(int dir);            // -1 prev, +1 next
    void onScroll(int value);           // scroll slider

protected:
    void keyPressEvent(QKeyEvent *e) override;

private:
    void loadFiles(const QStringList &paths);   // parse N captures, merge by time, per-key pos
    void addTypeTab(CapType t);
    void refreshState(int index);
    int  fieldValue(const ReplayRec &r, int field) const;   // -1 if N/A
    void setCursor(int index, bool fromTimeline = false);
    void buildEventLog();        // construct the Event Log panel
    void populateEventLog();     // fill rows from m_events
    void applyEventFilter();     // show/hide rows per text + severity mask
    void plotField(CapType type, const QString &fieldName);  // build + show field-over-time plot
    void clearPlot();

    // ---- export (offline, synchronous) ------------------------------
    void exportEvents();             // Event Log -> CSV, honouring the active filter
    void exportRecords(bool asJson); // records -> CSV (flat wire) or JSON (full decode)

    QVector<ReplayRec>       m_recs;
    QHash<int, qint64>       m_tagLoc;               // RFID unique -> abs_loc (for SLRP abs annotations)
    QVector<ReplayEvent>     m_events;
    TimelineWidget          *m_timeline = nullptr;
    QComboBox               *m_modeBox  = nullptr;
    QComboBox               *m_keyBox   = nullptr;   // active source selector (multi-key)
    QStringList              m_keys;                 // distinct keys, stable order
    int                      m_activeKey = 0;        // index into m_keys
    QPushButton             *m_play     = nullptr;
    QLabel                  *m_readout  = nullptr;
    QComboBox               *m_jumpField = nullptr;
    QComboBox               *m_jumpValue = nullptr;
    QSlider                 *m_scroll    = nullptr;
    QStackedWidget          *m_stack    = nullptr;   // [0]=inspector tabs, [1]=event log
    QTabWidget              *m_tabs     = nullptr;
    QWidget                 *m_eventPanel = nullptr;
    QTableWidget            *m_eventLog = nullptr;
    QLineEdit               *m_eventFilter = nullptr;
    QLabel                  *m_eventCount  = nullptr;
    int                      m_sevMask  = 0x7;        // bit0 Info, bit1 Warn, bit2 Error
    bool                     m_exportActiveOnly = false; // export scope: active key vs all
    QMap<int, QTableWidget*> m_typeTables;
    QTimer                  *m_timer    = nullptr;
    int                      m_cursor   = 0;
    // Session 82: speed and event stepping.
    QComboBox               *m_speed      = nullptr;
    QComboBox               *m_eventLevel = nullptr;
    double                   m_playClockMs = 0.0;
    int                      m_stepTicks   = 0;
public:
    // Index of the next (dir +1) or previous (-1) event record from `fromRec`
    // at `minSeverity` or above; -1 if none. And the step itself.
    int  nextEventIndex(int fromRec, int dir, int minSeverity) const;
    void stepEvent(int dir);
    int  cursorIndex() const { return m_cursor; }
    int  recordCount() const { return m_recs.size(); }
    const QVector<ReplayEvent> &events() const { return m_events; }
    // Session 84: the DMI at record `index` — each source's latest @dmi at
    // or before it — and the button that opens a DMI window following it.
    DmiMoment dmiMomentAt(int index) const;
    void openDmi();
    void setActiveKey(int index);
private:
    void offerDmi(int index);
    QVector<QVector<int>>    m_dmiIdx;       // per key: record indices of its @dmi, ascending
    QString                  m_title;
private:
    CapType                  m_plotType  = CapType::Unknown;   // currently plotted field
    QString                  m_plotFieldName;
};

#endif // REPLAYWINDOW_H
