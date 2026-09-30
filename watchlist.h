#ifndef WATCHLIST_H
#define WATCHLIST_H
// =============================================================================
//  watchlist.{h,cpp} — conditions that fire.
//
//  A pin says what a field IS. A watch says tell me WHEN it becomes something.
//
//  During an acceptance run the operator is driving the DMI, not reading the
//  log. The moment worth catching — a mode transition, TSR_STATUS reaching 2,
//  an emergency bit setting — goes past in one frame among hundreds and is
//  found afterwards by scrolling, if it is found at all.
//
//  THE CONDITION IS A QUERY
//    Not a new little expression language. LogQuery already parses
//    `LOCO_MODE == 2`, `sev:error AND src:33_1`, `field:SIG_OV != 0` — it is
//    what the filter bar and the find bar's Query mode use, so an operator who
//    can write a filter can write a watch, and a watch that does not fire can
//    be pasted into the filter bar to find out why.
//
//  LATCHING, AND WHY
//    A watch fires ONCE and holds its evidence until re-armed. A condition
//    that is true for three hundred consecutive frames is one event, not three
//    hundred; a watch that re-announced itself per frame would be the thing
//    the operator turns off first, and then it is not watching anything.
//
//    Matches after the first are counted, not announced. "Fired at 15:02:31,
//    2,847 frames since" says both that it happened and that it is still true,
//    which are different facts and both worth having.
//
//  EVERY ENTRY, NOT A SAMPLE
//    The pin board samples one entry per delivered batch, because a pinned
//    value only has to be current. A watch may not: the whole point is the one
//    frame where the condition held. So this evaluates every entry it is
//    given, and the cost is real — a query with a field term decodes the frame
//    (cached on the entry by FieldIndex, but the first touch is not free).
//
//    That cost is accepted deliberately. It is bounded by the traffic rate,
//    it is paid only while a watch is armed, and the alternative — missing the
//    event — is the failure this exists to prevent.
// =============================================================================

#include <QString>
#include <QStringList>
#include <QVector>

#include "logentry.h"
#include "logquery.h"

class NameMap;

class WatchList
{
public:
    struct Watch {
        QString label;      // what the operator called it; may be empty
        QString expr;       // the query text, as typed

        // A watch whose query does not parse is kept, not dropped: the
        // operator sees what they typed and the error beside it, rather than
        // the row vanishing and leaving them to wonder whether it was added.
        QString parseError;
        int     errorOffset = -1;

        bool    armed = true;

        // Evidence, filled on the first match and held until re-armed.
        bool    fired      = false;
        qint64  firedAtMs  = 0;    // wall clock of the frame that fired it
        QString firedBy;           // source key
        QString evidence;          // the frame's text, so the row can be shown
        LogEntryPtr firedEntry;    // for "go to this frame"
        int     matches = 0;       // including the one that fired it

        // Session 82. An OCCURRENCE is a run of matches with no gap longer
        // than kOccurrenceGapMs: a condition true for three hundred frames
        // is one occurrence, true again a minute later is a second.
        int     occurrences = 0;
        qint64  lastMatchMs = 0;
        // What to do when it fires (saved with the watch).
        bool    beep = false;          // sound
        bool    bookmark = false;      // bookmark the frame
        bool    everyTime = false;     // announce every occurrence, not just the first

        bool valid() const { return parseError.isEmpty(); }
    };

    // Returns the index of the new watch, or -1 for an empty expression.
    // A watch with an unparseable query is still added — see parseError.
    int add(const QString &expr, const QString &label = QString());
    bool remove(int index);
    void clear();

    // Back to waiting, evidence discarded. The counted matches go with it:
    // a re-armed watch that kept its old count would report an event that
    // has not happened again.
    bool rearm(int index);
    void rearmAll();

    int count() const { return m_watches.size(); }
    static constexpr qint64 kOccurrenceGapMs = 3000;
    // Actions (session 82). Returns false for a bad index.
    bool setActions(int index, bool beep, bool bookmark, bool everyTime);
    const QVector<Watch> &watches() const { return m_watches; }

    // Offer one entry to every armed watch. Returns the indices of watches
    // that fired ON THIS ENTRY — not those already firing — so the caller can
    // announce an event once.
    QVector<int> observe(const LogEntryPtr &entry, const QString &sourceKey,
                         const NameMap *names = nullptr);

    QStringList toStrings() const;
    void fromStrings(const QStringList &lines);

private:
    struct Compiled {
        LogQuery query;
        bool     ok = false;
    };
    void compile(int index);

    QVector<Watch>    m_watches;
    QVector<Compiled> m_compiled;   // parallel to m_watches
};

#endif  // WATCHLIST_H
