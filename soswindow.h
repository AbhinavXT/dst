#ifndef SOSWINDOW_H
#define SOSWINDOW_H

// =============================================================================
//  SoS window (sessions 184, 185, 187) — Tools ▸ Monitor ▸ SoS…
//  -----------------------------------------------------------------------------
//  What the loco's SoS logic knew and decided, moment by moment, from
//  @sos / @sossrc / @sosev (soslog.h):
//
//    picker     the tab (a loco's log) to read
//    time       a slider over its snapshots, ◀ / ▶ to the previous / next
//               decision, and Follow the log: the snapshot at or before the
//               row picked in any tab or replay (the DMI's time travel)
//    strip      (185) the snapshot drawn: one lane per TIN, the trains by
//               threat, the target ringed, the ARP's own position dashed,
//               the trigger distances (from @linfo) shaded (sosstrip.h)
//    decision   (185) what the loco is reacting to, in a few lines: the
//               target, the status its own ARP broadcasts, the flags that
//               set the SoS speed limit, what the DMI shows, and any flag
//               the DMI does not show
//    sources    one row per loco in the SoS table: its threats, whether it
//               is the closest of its kind, gap / SoS / collision distances,
//               position as adjusted, and the firmware's checks (same TIN,
//               ranges, adjacent line, adjusted, station check, passed),
//               each with its why on hover; the station slots under them
//    events     every decision, in words; the ones at or before the cursor
//               plain, the rest muted; double-click jumps the log there
//
//    two logs   (187) a second tab beside the decisions: what another loco
//               sent (its log's broadcast emergency status), when this loco
//               heard it and what it decided about it (sosrelay.h)
//
//  It reports what the loco logged. No verdicts.
// =============================================================================

#include "soslog.h"

#include <QWidget>

class MessageDispatcher;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QSplitter;
class QTableWidget;
class SosRelayPanel;
class SosStrip;
class QTabWidget;
class StatusLine;

class SosWindow : public QWidget
{
    Q_OBJECT
public:
    explicit SosWindow(MessageDispatcher *dispatcher, QWidget *parent = nullptr);
    ~SosWindow() override;

    // The tab read; for the tests, and for a tab that just got @sos.
    void setSource(const QString &key);
    QString sourceKey() const { return m_key; }
    const SosLog::Timeline &timeline() const { return m_t; }

    // The snapshot shown (index into timeline().snaps; -1 = none).
    int  currentSnapshot() const { return m_index; }
    void setCurrentSnapshot(int index);
    // The snapshot at or before `ms`.
    void showMoment(qint64 ms);
    void setFollowLog(bool on);

    QComboBox    *picker() const { return m_picker; }
    QSlider      *slider() const { return m_slider; }
    QLabel       *readout() const { return m_readout; }
    QTableWidget *sourceTable() const { return m_sources; }
    SosStrip     *strip() const { return m_strip; }
    QLabel       *decision() const { return m_decision; }
    SosRelayPanel *relay() const { return m_relay; }
    QTabWidget   *lowerTabs() const { return m_lowerTabs; }
    QTableWidget *eventTable() const { return m_events; }
    QCheckBox    *followBox() const { return m_follow; }
    StatusLine   *status() const { return m_status; }

    // Column of the source table, by header text; -1 if none.
    int sourceColumn(const QString &header) const;

signals:
    // Double-click on a decision: show this moment in that tab's log.
    void jumpRequested(const QString &key, qint64 ms);

public slots:
    void rebuild();
    void refreshPicker();

protected:
    void showEvent(QShowEvent *e) override;

private:
    void fillSources();
    void fillEvents();
    void fillDecision();
    void markEvents();
    void stepEvent(int direction);

    MessageDispatcher *m_dispatcher = nullptr;
    QString m_key;
    SosLog::Timeline m_t;
    int m_index = -1;
    bool m_built = false;   // session 193: rebuild() has run for m_key

    QComboBox    *m_picker = nullptr;
    QSlider      *m_slider = nullptr;
    QPushButton  *m_prevEvent = nullptr, *m_nextEvent = nullptr;
    QCheckBox    *m_follow = nullptr;
    QLabel       *m_readout = nullptr;
    QSplitter    *m_split = nullptr;
    SosStrip     *m_strip = nullptr;      // session 185
    QLabel       *m_decision = nullptr;   // session 185
    SosRelayPanel *m_relay = nullptr;     // session 187
    QTabWidget   *m_lowerTabs = nullptr;  // session 187: Decisions | Two logs
    QTableWidget *m_sources = nullptr;
    QTableWidget *m_events = nullptr;
    StatusLine   *m_status = nullptr;
};

#endif // SOSWINDOW_H
