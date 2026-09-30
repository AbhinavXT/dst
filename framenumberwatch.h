#ifndef FRAMENUMBERWATCH_H
#define FRAMENUMBERWATCH_H
// =====================================================================
//  framenumberwatch.{h,cpp} -- the frame number the equipment is
//  currently using.
//
//  FRAME_NUM was seeded from seconds-since-midnight, which is the right
//  SHAPE but not the right number: it is whatever this laptop's clock
//  says, and the equipment's counter is whatever the equipment says. A
//  frame built with a number minutes away from the live one is a frame a
//  peer may treat as stale or replayed, and nothing on screen would
//  explain why it was ignored.
//
//  ARP and LSRP carry FRAME_NUM and one or the other arrives constantly,
//  so the live value is simply there to be read. This watches the ingest
//  path for them and remembers the most recent, per source and overall.
//
//  WHAT IT DOES NOT DO
//    It does not guess. With no traffic seen there is no observation, and
//    the caller is expected to fall back to the clock and say so — an
//    invented number presented as observed is worse than an obviously
//    local one.
//
//  COST
//    The bit offset of FRAME_NUM is worked out once per packet type from
//    the schema and then reused, so an observation is a bounded bit read
//    rather than a full decode. It is on the ingest path for every frame,
//    which is the reason it is not simply parseBody().
//
//  THREADING
//    GUI thread only, like SessionKeyStore: it is fed from the same
//    ingest slot and read by dialogs.
// =====================================================================
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>

#include "capturedecoder.h"
#include "logentry.h"

class FrameNumberWatch : public QObject
{
    Q_OBJECT

public:
    static FrameNumberWatch &instance();

    struct Seen {
        qint64  value     = -1;   // the FRAME_NUM last read
        qint64  atMs      = 0;    // when that frame arrived
        QString captype;          // which packet it came from
        int     locoId    = -1;
        bool    valid() const { return value >= 0; }
    };

    // Cheap: anything that is not arp or lsrp is rejected on the captype
    // before any bits are touched.
    void observe(const LogEntryPtr &entry);
    void observeLine(const CaptureLine &c);

    // The most recent observation from any source, and from one loco.
    //
    // These are the LOCO's counter — arp and lsrp — and they are what the
    // Packet Maker seeds a built frame from. The station's counter is kept
    // apart deliberately: they are two clocks that are supposed to agree, and
    // merging them into one answer would hide the moment they stop agreeing,
    // which is the only interesting thing about having both.
    Seen latest() const { return m_latest; }
    Seen latestFor(int locoId) const { return m_byLoco.value(locoId); }

    // The stationary end's counter, out of the SLRP header. Never mixed into
    // latest() above.
    Seen latestStation() const { return m_station; }

    // How old the latest observation is, in ms. -1 when there is none.
    qint64 ageMs() const;

    void clear();

signals:
    // Emitted when a newer frame number is seen. Carries the value so a
    // dialog can follow it without polling.
    void observed(qint64 value, int locoId);

private:
    FrameNumberWatch() = default;

    // Bit offset and width of FRAME_NUM within a captured frame of this
    // type, worked out once from the schema. width 0 means "this packet has
    // no FRAME_NUM", which is also cached so it is only asked once.
    struct Field { int bitOffset = -1; int bits = 0; };
    Field fieldFor(const QString &captype);

    QHash<QString, Field> m_fields;
    Seen                  m_station;
    QHash<int, Seen>      m_byLoco;
    Seen                  m_latest;
};

#endif  // FRAMENUMBERWATCH_H
