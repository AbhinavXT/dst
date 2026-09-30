#ifndef PACKETVARIATION_H
#define PACKETVARIATION_H

// =============================================================================
//  PacketVary
//  -----------------------------------------------------------------------------
//  Rules for changing header field values from one interval send to the next.
//
//  WHY THIS IS NOT A SPECIAL CASE FOR FRAME_NUM
//    The defect it fixes is specific — interval send repeated one buffer, so
//    FRAME_NUM never advanced and the MAC and CRC over that body were
//    identical on every datagram. But "increment the frame number" is only
//    the most common instance of a general need: hold a session open with an
//    advancing counter, walk a speed field across its range to watch the
//    console react, or push noise into a field to see what rejects it. A rule
//    table costs about the same as the special case and does not have to be
//    reopened for the second question anyone asks.
//
//  A rule is a pure function of the send index. Nothing accumulates between
//  ticks, so send N is reproducible without replaying sends 0..N-1 — which
//  matters when a run is being compared against a capture after the fact.
//  Random is the deliberate exception and says so.
//
//  Values are always clamped to the field's declared bit width. A rule that
//  runs past it wraps rather than silently truncating on the way to the wire,
//  because a truncated value is a different value and the operator should see
//  the one that was actually sent.
//
//  Header fields only, for now. Sub-packet repeats have per-row values with no
//  single name to address, and no obvious meaning for "advance" — that needs
//  its own design rather than being bolted on here.
// =============================================================================

#include <QJsonArray>
#include <QString>
#include <QVector>

namespace PacketVary {

// What the equipment is currently using, supplied by the caller. Kept
// out of this file's reach on purpose: this stays a pure value
// calculation and the ingest-facing singleton stays out of it.
//
// `sendsSinceChange` answers a question the plain observed number cannot.
// If the loco sends at 1 Hz and this runs at 200 ms, four consecutive
// frames would carry the SAME number — fine if a peer only checks
// freshness, wrong if it requires each frame to advance. With `step`
// non-zero the rule adds step per send since the observed value last
// moved, so consecutive frames differ and the sequence RE-ANCHORS every
// time the equipment moves on. That is the part a plain counter cannot
// do: it drifts, this cannot.
struct LiveFrame {
    qint64 value            = -1;   // -1 = nothing observed
    int    sendsSinceChange = 0;
    bool   valid() const { return value >= 0; }
};

struct Rule {
    enum Mode {
        Increment,   // start + step*i, wrapped
        Sweep,       // start .. max in `step`s, then back to start
        Random,      // uniform in [start, max]
        Live,        // whatever the equipment is currently using — see below
    };

    QString field;
    Mode    mode    = Increment;
    qint64  start   = 0;    // Increment: first value.  Sweep/Random: low bound
    qint64  step    = 1;    // Increment/Sweep step.    Ignored for Random
    qint64  max     = 0;    // Sweep/Random: high bound. Increment: 0 means
                            // "wrap at the field's width"
    bool    enabled = true;

    // Live is the second exception to "a rule is a pure function of the send
    // index", alongside Random, and for a better reason: the number wanted is
    // not one this program chooses at all. With nothing observed the rule
    // falls back to `start`, so a run does not silently send zeros.
    qint64 valueFor(int sendIndex, int bits, const LiveFrame &live = LiveFrame()) const;

    // One-line description for the UI, e.g. "FRAME_NUM += 1 (wrap at 17 bits)".
    QString describe(int bits) const;
};

// Largest value a field of `bits` can hold. 0 for bits <= 0.
qint64 maxForBits(int bits);

QJsonArray     toJson(const QVector<Rule> &rules);
QVector<Rule>  fromJson(const QJsonArray &arr);

QString modeName(Rule::Mode m);
Rule::Mode modeFromName(const QString &s);

}  // namespace PacketVary

#endif  // PACKETVARIATION_H
