#ifndef BRAKINGPANEL_H
#define BRAKINGPANEL_H

// =============================================================================
//  BrakingPanel
//  ---------------------------------------------------------------------------
//  One window for everything the uniform-braking module says: the target it is
//  braking towards, the curve it computed, the segments that curve is built
//  from, and how all of that changed over the session.
//
//  WHY A SCRUBBER AND NOT A LIVE VIEW
//    A braking curve is only interesting in motion. Live-only would answer
//    "what is the curve now", but the questions that actually get asked are
//    "what was it when the brake applied" and "when did the target change" —
//    both of which are about a moment in the past. So the panel indexes every
//    @uba frame in the tab and puts a scrubber across them: pick a time, see
//    the curve that was in force at that time.
//
//    Live capture still works — new frames extend the timeline, and FOLLOW
//    keeps the scrubber pinned to the newest one. Un-following is automatic
//    the moment the operator drags, because yanking the view back to "now"
//    while someone is reading an older frame is the single most annoying thing
//    a live panel can do.
//
//  AXES LOCKED BY DEFAULT
//    Autoscaling per frame makes a stationary curve appear to move and a
//    collapsing one appear stationary. The panel fits the axes once, over the
//    whole session, and holds them; the operator can unlock to fit the current
//    frame instead.
// =============================================================================

#include <QMainWindow>
#include <QSet>
#include <QVector>

#include "brakingcurves.h"
#include "logentry.h"

class BrakingCurvePlot;
class ChangeStrip;
class MessageDispatcher;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSlider;
class QSpinBox;
class QTableWidget;
class QToolButton;

class BrakingPanel : public QMainWindow
{
    Q_OBJECT

public:
    explicit BrakingPanel(MessageDispatcher *dispatcher, QWidget *parent = nullptr);

signals:
    // The operator asked to see the capture line this curve came from.
    // Carries a TIME, not a row: the main window already resolves a moment to
    // a row (jumpToEntry), and a row index captured here would go stale the
    // moment the model's ring buffer wraps.
    void jumpToTimeRequested(QString tabKey, qint64 epochMs);

private slots:
    void onTabChanged(int index);
    void onScrubbed(int value);
    void onStepPrev();
    void onStepNext();
    void onFollowToggled(bool on);
    void onLockToggled(bool on);
    void onGhostCountChanged(int n);
    void onChangesOnlyToggled(bool on);
    void onPrevChange();
    void onNextChange();
    void onSearch();          // Enter / Find — advance to the next match
    void onSearchModeChanged(int index);
    void onSearchTextChanged(const QString &text);
    void onCurveFilterChanged(int index);
    void onEntryAppended(QString tabKey, LogEntryPtr entry);
    void onReload();
    void onJumpToRow();

private:
    void rebuildTabList();
    void reloadSnapshots();
    void applyIndex(int index);
    void refreshTables(const Braking::Cycle &cycle);
    void refreshRangeLock();
    void rebuildChangeIndex();
    void runSearch(bool advance);
    void updateStatus();

    // The scrubber addresses either every frame or only the frames where the
    // curve changed; these convert between the two.
    int  scrubToSnap(int scrubValue) const;
    int  snapToScrub(int snapIndex) const;
    void setScrubForSnap(int snapIndex);

    MessageDispatcher *m_dispatcher = nullptr;

    QComboBox   *m_tabCombo   = nullptr;
    QComboBox   *m_curveCombo = nullptr;
    QSlider     *m_scrub      = nullptr;
    QLabel      *m_timeLabel  = nullptr;
    QLabel      *m_status     = nullptr;
    QCheckBox   *m_follow     = nullptr;
    QCheckBox   *m_lock       = nullptr;
    QSpinBox    *m_ghosts     = nullptr;
    QSpinBox    *m_ghostSpan  = nullptr;
    QToolButton *m_prev       = nullptr;
    QToolButton *m_next       = nullptr;
    QToolButton *m_prevChange = nullptr;
    QToolButton *m_nextChange = nullptr;
    QCheckBox   *m_changesOnly = nullptr;
    ChangeStrip *m_strip      = nullptr;
    QComboBox   *m_searchMode = nullptr;
    QLineEdit   *m_searchBox  = nullptr;
    QLabel      *m_searchInfo = nullptr;

    BrakingCurvePlot *m_plot     = nullptr;
    QTableWidget     *m_summary  = nullptr;
    QTableWidget     *m_segments = nullptr;

    QString                    m_tabKey;
    QVector<Braking::Snapshot> m_snaps;
    // Frames grouped into computation cycles. The scrubber addresses CYCLES:
    // one cycle is one recalculation of every target ahead of the loco, which
    // is the unit an operator reasons about — a single frame is one target out
    // of several and is meaningless on its own.
    QVector<Braking::Cycle>    m_cycles;
    int                        m_index  = -1;
    bool                       m_hitCap = false;

    // Indices into m_snaps where the curve differs from the frame before it.
    // Always contains 0 when there is at least one frame.
    QVector<int>               m_changeIdx;
    QSet<quint64>              m_distinctSeen;
    int                        m_distinctFrames = 0;

    QVector<int>               m_matches;     // snapshot indices hit by the search
    int                        m_matchPos = -1;

    // The loco builds every target on a 10 ms cycle, so @uba arrives at up to
    // 100 frames/s. The old 20 000 cap was barely three minutes of that — it
    // would have silently truncated any real run. At 100 Hz this is ~33 min.
    // The cap still exists (decoding every row of a long session costs real
    // time) and the status line still says when it bit, because a timeline
    // that stops early without saying so lies about when things happened.
    static constexpr int kSnapshotCap = 200000;
};

#endif // BRAKINGPANEL_H
