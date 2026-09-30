#ifndef DMITIMETRAVEL_H
#define DMITIMETRAVEL_H

// =============================================================================
//  DMI time travel (session 84)
//  -----------------------------------------------------------------------------
//  "What did the loco pilot see at 12:58:44?"  The DMI window, set to
//  "Follow cursor", shows the panel as it stood at the moment the operator
//  has picked anywhere in the console:
//
//    - the current row of a tab in the main window (live or loaded),
//    - the current row of a tab in a recorded-session (.dlr) window,
//    - the cursor of a replay (.cap) window: timeline drag, slider, arrow
//      keys, playback, event stepping, the event log.
//
//  THE RULE
//    For each loco, the panel is its LATEST @dmi AT OR BEFORE the moment:
//    that is what was on the screen then, because the DMI holds a frame
//    until the next one replaces it. "At or before" is by row order in the
//    tab that was clicked (so the clicked @dmi row itself is exact) and by
//    receipt time in the other tabs. A frame more than 3 s before the
//    moment is drawn muted, as a stale live frame is; nothing in the
//    10 minutes before counts as "no @dmi" rather than an ancient frame.
//
//  WHO WINS
//    Whoever moved last. Every row change and cursor move OFFERS a
//    resolver to the broker, which stores it (no work) and, only while a
//    DMI window is following, runs it. So a console with no DMI window
//    following pays nothing per click, and a DMI window that starts
//    following picks up the moment the operator last pointed at.
// =============================================================================

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

#include "capturedecoder.h"
#include "logentry.h"

class LogModel;

// One loco's @dmi frame, as it stood at the moment.
struct DmiFrameAt {
    QString     key;          // loco_ctrl ("1_1")
    CaptureLine cap;          // the frame itself
    qint64      frameMs = 0;  // when it arrived (tabs) / its RTC (replay)
};

// The picked moment and what each loco's panel showed then.
struct DmiMoment {
    bool    valid = false;             // a moment has been picked
    qint64  atMs  = 0;                 // the moment
    QString origin;                    // "Loco 1 · row 1,234" / "Replay loco_1_1_…cap"
    QString preferredKey;              // loco of the clicked row / the replay's active source
    QVector<DmiFrameAt> frames;        // one per loco, sorted by key

    const DmiFrameAt *frameFor(const QString &key) const;
    QStringList keys() const;
};

constexpr qint64 kDmiLookbackMs = 10 * 60 * 1000;   // older than this is "none"
constexpr qint64 kDmiStaleMs    = 3000;             // older than this is drawn muted

// "@dmi_1_1 2026-…" -> "1_1"; empty for anything that is not an @dmi line.
QString dmiKeyOfText(const QString &text);
// First source row whose epochMs is after `ms` (binary search; rows are in
// arrival order). rowCount() when none is.
int logModelRowAfter(const LogModel *model, qint64 ms);
// Where `entry` is now (rows move up as a live model trims its oldest):
// `hint` if it is still there, else found by time; -1 once trimmed away.
int logModelRowOf(const LogModel *model, const LogEntry *entry, int hint);

using DmiResolver = std::function<DmiMoment()>;

// The resolver a tab offers for its current row (source row `hint`): finds
// the row again when it runs, and looks across `models`. Models that have
// gone by then are skipped.
DmiResolver dmiTabResolver(const QVector<LogModel *> &models, LogModel *clicked,
                           const LogEntryPtr &entry, int hint, const QString &origin);

// The moment at row `clickedRow` (source row) of `clicked`, with each loco's
// latest @dmi at or before it across `models` (the clicked model may be in
// the list or not). `origin` is the human label for the status line.
DmiMoment dmiMomentFromModels(const QVector<const LogModel *> &models,
                              const LogModel *clicked, int clickedRow,
                              const QString &origin,
                              qint64 lookbackMs = kDmiLookbackMs);

// ---- the broker ---------------------------------------------------------------
class DmiTimeTravel : public QObject
{
    Q_OBJECT
public:
    using Resolver = DmiResolver;

    static DmiTimeTravel *instance();

    // A publisher says "the cursor is here now". Cheap: the resolver is only
    // run while someone follows (now, or when the next follower arrives, if
    // `owner` still exists by then).
    void offer(QObject *owner, Resolver resolver);

    // A DMI window starts / stops following. Destroyed followers drop out.
    void follow(QObject *follower);
    void unfollow(QObject *follower);
    bool hasFollowers() const { return !m_followers.isEmpty(); }

    const DmiMoment &last() const { return m_last; }
    void reset();                        // tests: forget everything

signals:
    void momentChanged(const DmiMoment &moment);

private:
    explicit DmiTimeTravel(QObject *parent = nullptr) : QObject(parent) {}
    void resolveNow();

    QVector<QPointer<QObject>> m_followers;
    QPointer<QObject> m_owner;
    bool      m_hasResolver = false;
    Resolver  m_resolver;
    DmiMoment m_last;
};

#endif // DMITIMETRAVEL_H
