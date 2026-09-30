#ifndef PINBOARD_H
#define PINBOARD_H
// =============================================================================
//  pinboard.{h,cpp} — fields kept in view while traffic runs.
//
//  THE PROBLEM
//    Watching one field means finding it again in every frame. LOCO_MODE is
//    somewhere in the middle of an LSRP; to see it change you scroll, decode,
//    read, and by then three more frames have arrived. The value is in the
//    traffic, it is decoded already, and the only reason it is hard to watch
//    is that nothing holds it still.
//
//  WHAT A PIN IS
//    A field name, optionally narrowed to one source, and the last value seen
//    for it — plus the value BEFORE that and when it changed. The previous
//    value is half the point: "Staff Responsible" on its own says less than
//    "Staff Responsible, was Stand By, 12 s ago".
//
//  SAMPLED, NOT EXHAUSTIVE — say so out loud
//    Decoding every arriving frame for every pin would put schema decoding on
//    the ingest path, which is the one place in this program that must not get
//    slower. So a pin is updated from the LAST entry of each delivered batch
//    per source, not from every entry.
//
//    The consequence is honest and worth stating: a field that changes and
//    changes back within a single batch shows neither transition. For the
//    fields anyone pins — mode, location, a status word — that is invisible.
//    For a field toggling every frame it would be wrong, and `sampled` says
//    so rather than leaving the operator to assume otherwise.
//
//  No Qt widgets here. The matching, the change detection and the
//  source-narrowing are all testable without a window.
// =============================================================================

#include <QString>
#include <QStringList>
#include <QVector>

#include "logentry.h"

class PinBoard
{
public:
    struct Pin {
        QString field;        // decoder-level field name, e.g. "LOCO_MODE"
        QString sourceKey;    // tab key to narrow to; empty = any source

        // Captype to narrow to; empty = any packet.
        //
        // Not the same question as the source, and both are needed. 35 field
        // names in the schema appear in more than one packet — FRAME_NUM is in
        // five of them — so an unnarrowed pin on FRAME_NUM shows whichever of
        // ARP, LSRP, SLRP or AAP arrived last, which is a reading of nothing
        // even when only one loco is on air.
        QString captype;

        // What was last seen. `value` is the DISPLAY string, not a number, so
        // an enum reads as its name rather than as the digit behind it —
        // which is the whole reason to look at it.
        QString value;
        QString prevValue;
        QString fromSource;   // which source supplied `value`
        qint64  atMs     = 0; // when `value` arrived (wall clock)
        qint64  changedMs = 0;// when it last became different (wall clock)

        // The LOG timestamps of those two frames, which is what it takes to
        // go and look at them. Not the same as the two above: those are when
        // this program saw the value, these are when the equipment sent it,
        // and a replayed session makes the difference obvious.
        //
        // Stored as timestamps rather than as entry pointers so a pin does
        // not keep a frame alive after the model has evicted it — a pin is
        // set up once and left for the length of a run, and holding the
        // frames it has passed through would be a slow leak.
        qint64  atEpochMs      = 0;
        qint64  changedEpochMs = 0;
        int     seen     = 0; // frames that carried the field

        bool valid() const { return !value.isEmpty() || seen > 0; }
        bool everChanged() const { return !prevValue.isEmpty(); }

        // The frame worth going to: where the value BECAME what it is. Before
        // it has ever changed, that is the frame it was first seen in — which
        // is still the frame that established the value.
        qint64 originEpochMs() const {
            return everChanged() ? changedEpochMs : atEpochMs;
        }
    };

    // Adding the same field and source twice is a no-op rather than a second
    // row: two identical pins would update in step and look like a bug.
    bool add(const QString &field, const QString &sourceKey = QString(),
             const QString &captype = QString());
    bool remove(int index);
    void clear();

    int count() const { return m_pins.size(); }
    const QVector<Pin> &pins() const { return m_pins; }

    // Offer an entry to every pin. Returns true when any pin's value changed,
    // so the caller can repaint only when there is something new.
    //
    // Does nothing while frozen.
    bool observe(const LogEntryPtr &entry, const QString &sourceKey,
                 qint64 nowMs);

    // FREEZING — hold every value where it is.
    //
    // The board answers "what is it now", which is the wrong tense the moment
    // something interesting happens: by the time the operator looks up from
    // the DMI, "now" has moved on and the state they wanted is three hundred
    // frames back. Freezing holds the whole board at one instant, which is
    // the state of the system at the moment worth reading.
    //
    // Values are held, not cleared: a frozen board still shows what each
    // field was, when it was last seen and when it last changed, all as of
    // the freeze. Nothing is lost by freezing except the updates, and those
    // resume on thaw with the next frame that carries the field.
    void freeze(qint64 nowMs, const QString &why = QString());
    void thaw();
    bool isFrozen() const { return m_frozenAtMs > 0; }
    qint64 frozenAtMs() const { return m_frozenAtMs; }
    QString frozenWhy() const { return m_frozenWhy; }

    // Round-trip for Settings. One line per pin, field and source separated,
    // so a hand-edited ini cannot silently produce half a pin.
    QStringList toStrings() const;
    void fromStrings(const QStringList &lines);

private:
    QVector<Pin> m_pins;
    qint64       m_frozenAtMs = 0;
    QString      m_frozenWhy;
};

#endif  // PINBOARD_H
