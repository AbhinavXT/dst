#ifndef RUNCOMPAREWINDOW_H
#define RUNCOMPAREWINDOW_H

// =============================================================================
//  Compare two runs (session 374) — the same scenario run twice (two runs at
//  the site, or before and after a firmware change), played in step with the
//  two DMIs side by side.
//  -----------------------------------------------------------------------------
//  Each run is one or more .dlr files, or a review pack (session 370). Each
//  has a start: the moment its clock reads T+0. By default the first @dmi of
//  its loco; "Align at" sets both to the first DMI frame, the first FSB or
//  EB, or the first change of mode; each start can also be typed. One clock
//  (T+mm:ss.s) drives both: each panel shows its loco's last @dmi at or before
//  start + T, muted when that frame is over 3 s old, as the DMI window draws
//  a stale frame. Under the panels, what differs between the two pictures at
//  this moment (DmiDiff, in the DMI's own words).
//
//  The window holds the @dmi lines only, not the whole recording: Open in
//  the recorded-session window for the rest.
// =============================================================================

#include <QHash>
#include <QPair>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include "dmidiff.h"
#include "dmipanel.h"

class ColorRules;
class QComboBox;
class QDateTimeEdit;
class QLabel;
class QPushButton;
class QSlider;
class QTableWidget;
class QTimer;
class StatusLine;

class RunCompareWindow : public QWidget
{
    Q_OBJECT
public:
    explicit RunCompareWindow(const ColorRules *rules, QWidget *parent = nullptr);

    enum Side { A = 0, B = 1 };

    // .dlr files, or one review pack (.zip). False, with `err`, when nothing
    // with @dmi could be read.
    bool loadRun(Side side, const QStringList &paths, QString *err = nullptr);
    QStringList locos(Side side) const;              // loco keys with @dmi ("1_1")
    void setLoco(Side side, const QString &key);
    QString loco(Side side) const;

    enum class AlignAt { FirstDmi, FirstBrake, FirstModeChange };
    // Both starts at that moment of each run. False, with `why`, when a run
    // has no such moment (its start is left as it was).
    bool align(AlignAt at, QString *why = nullptr);
    void setStart(Side side, qint64 ms);
    qint64 start(Side side) const { return m_run[side].startMs; }

    void setT(qint64 ms);                            // the shared clock, from T+0
    qint64 t() const { return m_t; }
    qint64 length() const;                           // the longer run, from its start

    void play();
    void pause();
    bool isPlaying() const { return m_playing; }
    void setSpeed(int times);                        // 1, 2, 4, 8, 16
    void advance(qint64 wallMs);                     // one timer step

    // What is on show.
    DmiState state(Side side) const;
    qint64 frameMs(Side side) const;                 // the frame on show, 0 = none
    QVector<DmiDiff::Item> differences() const;
    DmiView *view(Side side) const { return m_run[side].view; }
    QLabel *caption(Side side) const { return m_run[side].caption; }
    QTableWidget *diffTable() const { return m_diffs; }
    StatusLine *status() const { return m_status; }

private:
    struct Frame { qint64 ms; QString line; };
    struct Run {
        QStringList files;
        QHash<QString, QVector<Frame>> frames;     // by loco, in time order
        QString loco;
        qint64 startMs = 0;
        QComboBox *locoBox = nullptr;
        QDateTimeEdit *startEdit = nullptr;
        QLabel *title = nullptr;
        QLabel *caption = nullptr;
        DmiView *view = nullptr;
    };

    void buildSide(Side side, class QGridLayout *grid, int column);
    void openFiles(Side side);
    const Frame *frameAt(Side side, qint64 ms) const;
    void refresh();
    void refreshSide(Side side);

    const ColorRules *m_rules = nullptr;
    Run m_run[2];
    qint64 m_t = 0;
    bool m_playing = false;
    bool m_updating = false;
    qint64 m_lastTick = 0;
    QTimer *m_timer = nullptr;
    QSlider *m_slider = nullptr;
    QLabel *m_clock = nullptr;
    QPushButton *m_play = nullptr;
    QComboBox *m_speed = nullptr;
    QComboBox *m_alignBox = nullptr;
    QTableWidget *m_diffs = nullptr;
    QLabel *m_same = nullptr;
    StatusLine *m_status = nullptr;
};

#endif // RUNCOMPAREWINDOW_H
