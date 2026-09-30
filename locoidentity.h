#ifndef LOCOIDENTITY_H
#define LOCOIDENTITY_H
// =============================================================================
//  locoidentity.{h,cpp} — which loco this console is watching.
//
//  Several reject rules turn on it: an SLRP addressed to another loco is one
//  the onboard would not process, and there is no way to say so without
//  knowing which loco "ours" is.
//
//  Learned rather than configured, because a configured ID is one more thing
//  to set before a run and one more thing to have wrong. ARP and LSRP carry
//  SOURCE_LOCO_ID, so a console that has seen either has been told.
//
//  THREE STATES, and the difference between the last two is the point.
//
//    Known      one loco has been seen on this source. Rules may fire.
//    Unknown    nothing seen yet. Rules must NOT fire, and the console has
//               to say it is not checking rather than stay quiet — quiet
//               reads identically to "checked, and fine".
//    Ambiguous  more than one loco seen on one source. Also not checking,
//               for a different reason, and worth saying differently: it
//               usually means the capture is station-side, or two runs have
//               been merged into one tab.
//
//  ARP RECEIVED is deliberately not learned from. `arprecv` is another loco's
//  ARP arriving at ours; its SOURCE_LOCO_ID is the other loco, and learning
//  from it would teach the console the wrong identity on every loco-to-loco
//  approach — exactly when the reject rules matter most.
// =============================================================================
#include <QHash>
#include <QSet>
#include <QString>

class LocoIdentity
{
public:
    enum class State { Unknown, Known, Ambiguous };

    struct Id {
        State  state = State::Unknown;
        qint64 value = 0;   // meaningful only when state == Known
        int    seen  = 0;   // how many distinct locos on this source
    };

    // Feed one decoded frame. `captype` decides whether it teaches anything;
    // `values` is the decoder's numeric map.
    void observe(const QString &sourceKey, const QString &captype,
                 const QHash<QString, qint64> &values);

    Id idFor(const QString &sourceKey) const;

    // Plain words for the status line, or empty when the identity is known
    // and there is nothing to explain.
    QString explain(const QString &sourceKey) const;

    // Everything the reject rules need that is not in the frame itself.
    // Returns the map to merge into a frame's decoded values: OWN_LOCO_ID
    // when it is known, and nothing at all when it is not — absence is what
    // stops the identity-dependent rules from firing, so there is no
    // "unknown" value for a rule to compare against by accident.
    QHash<QString, qint64> contextFor(const QString &sourceKey) const;

    void clear() { m_seen.clear(); }
    bool isEmpty() const { return m_seen.isEmpty(); }

    // Field the rules read. Not a real wire field, which is why it is spelled
    // differently from anything in kavach.xml.
    static const char *kOwnLocoId;

private:
    QHash<QString, QSet<qint64>> m_seen;   // source key -> loco ids seen
};

#endif  // LOCOIDENTITY_H
