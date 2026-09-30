#ifndef SESSIONKEYSTORE_H
#define SESSIONKEYSTORE_H
// =====================================================================
//  sessionkeystore.{h,cpp} -- the live glue between the capture stream
//  and the crypto.
//
//  A single store watches every log entry and keeps ONE STATE PER LOCO,
//  keyed by the loco id in the capture tag (@slrp_1_1 -> loco 1). A
//  session is between one loco and one station, so key material from
//  loco 2 has no business being combined with randoms from loco 1 --
//  which is what a single global state did when a capture carried more
//  than one loco.
//
//  Within a loco, last write wins. From @auth_keys frames the store
//  keeps the two key sets (windows + both 16-byte keys); from @rand_num
//  frames the loco/station randoms; from station packets (@slrp/@aap)
//  the real station id out of SOURCE_STN_ID. Each new frame of a kind
//  replaces the previous one, the session key is re-derived
//  (SessionKeyGen) from the newest of everything, and changed() fires.
//  The decoder uses that key to verify the CBC-MAC on captured SLRP
//  frames live, turning the whole chain (auth keys + randoms -> session
//  key -> MAC) into a PASS/FAIL badge.
//
//  Accessors take an optional locoId. Passing -1 (the default) means
//  "the loco whose state changed most recently", which is the right
//  answer for a single-loco capture and for a UI that shows one key.
//
//  This is the one place key material from the stream is retained, and
//  only in memory for the running session -- deliberately, because live
//  verification needs it. Nothing is written to disk here.
// =====================================================================
#include <QHash>
#include <QVector>
#include <QList>
#include <QObject>
#include <QByteArray>
#include <QDateTime>

#include "capturedecoder.h"       // CaptureLine, CapType
#include "logentry.h"             // LogEntryPtr
#include "sessionkeygen.h"        // KeySet

// One complete, usable set of session inputs, frozen at the moment it became
// complete: both auth key sets, the randoms, and the two ids -- plus the
// session key they derive to.
//
// The live state moves (last write wins); a snapshot does not. Every distinct
// combination the log has produced stays available, so a frame can be checked,
// or a frame built, against the material that was in force when it was sent
// rather than only against the newest. This is what the Decode Workbench and
// the Packet Maker offer in their session-key pickers.
//
// A combination is snapshotted only once it is COMPLETE (a key is derivable);
// half-filled states live in the store's private per-loco state and are not
// offered as something to sign or verify with. A combination that recurs --
// a loco moving 527 -> 501 -> 527 -- reuses its existing snapshot rather than
// piling up duplicates.
struct KeySnapshot {
    int        id = 0;                 // 1-based, stable for the session
    int        locoId = -1, stnId = -1;
    SessionKeyGen::KeySet set[2];
    quint16    locoRandom = 0, stnRandom = 0;
    QDateTime  firstSeen;              // log time this combination appeared
    QDateTime  lastSeen;               // log time it was last seen in force
    QString    changedBy;              // what made it new ("randoms", "stn id"…)
    SessionKeyGen::Result result;      // derived key + which set/key was chosen

    bool isValid() const { return id > 0 && result.ok; }
    // "#3  loco 1 · stn 527 · rnd 1111/2222 · set 1 key 2" — what the pickers show.
    QString label() const;
};

class SessionKeyStore : public QObject {
    Q_OBJECT
public:
    static SessionKeyStore &instance();

    // Cheap: only @auth_keys / @rand_num / @slrp / @aap lines are parsed;
    // everything else is rejected on a prefix check.
    void observe(const LogEntryPtr &entry);
    void observeLine(const CaptureLine &c);

    // Every loco seen, in first-seen order, and the one that changed last.
    // A capture whose tags carry no loco id files everything under -1.
    QList<int> locos() const { return m_order; }
    int activeLoco() const { return m_active; }

    // Operator override for the id parity of ONE loco. Until set, the ids
    // come from the stream: the loco id from the tag, the station id from
    // the newest SOURCE_STN_ID seen in a station packet for that loco.
    void setIds(int locoId, int stnId);

    // ---- per-loco queries; locoId < 0 means the active loco -----------
    bool haveSet(int i, int locoId = -1) const;
    bool haveRandoms(int locoId = -1) const;
    quint16 locoRandom(int locoId = -1) const;
    quint16 stnRandom(int locoId = -1) const;
    const SessionKeyGen::KeySet &keySet(int i, int locoId = -1) const;

    int locoId(int locoId = -1) const;    // the id used for the parity sum
    int stnId(int locoId = -1) const;

    // The derived session key (empty until derivable), and the selection.
    QByteArray sessionKey(int locoId = -1) const;
    bool haveSessionKey(int locoId = -1) const;
    const SessionKeyGen::Result &selection(int locoId = -1) const;

    // The log time the window rule was evaluated at for this loco (the RTC of
    // the newest frame that fed it). Invalid when nothing has been seen.
    QDateTime logTime(int locoId = -1) const;

    // One line for the active loco; statusAll() covers every loco seen and
    // is what a display should show once there is more than one.
    QString status(int locoId = -1) const;
    QString statusAll() const;

    // ---- snapshots ----------------------------------------------------
    // Every complete combination the log has produced, in creation order.
    QVector<KeySnapshot> snapshots() const { return m_snaps; }
    // One by id, or null. Ids are stable for the life of the store.
    const KeySnapshot *snapshot(int id) const;
    // The snapshot currently in force for a loco (-1 = active loco), or 0.
    int currentSnapshotId(int locoId = -1) const;

    // Live MAC check for a captured frame of `captype`, against the key of
    // `locoId` -- the loco the frame was captured on, NOT the active one.
    // Returns None for types that carry no MAC.
    enum class Mac { None, NoKey, Pass, Fail };
    Mac verifyMac(const QString &captype, const QByteArray &frame,
                  int locoId = -1) const;
    // The same check against one specific snapshot, for a caller that has let
    // the operator choose which material to test against.
    Mac verifyMacWith(const QString &captype, const QByteArray &frame,
                      int snapshotId) const;
    static bool typeHasMac(const QString &captype);

    // SOURCE_STN_ID straight out of a station frame, without a schema
    // decode -- this runs on every @slrp/@aap line on the ingest path, so
    // it reads the one fixed bit range directly. -1 if the type has no
    // such field or the frame is too short. test_sessionkeystore pins it
    // against the schema decode so the two cannot drift apart.
    static int stationIdFromFrame(CapType type, const QByteArray &frame);

    void clear();

signals:
    void changed();

private:
    SessionKeyStore() = default;

    // Everything the derivation needs for one loco.
    struct LocoState {
        SessionKeyGen::KeySet set[2];
        bool     haveRandoms = false;
        quint16  locoRandom = 0, stnRandom = 0;
        int      locoId = -1, stnId = -1;
        // The log's own clock: the RTC of the most recent frame that fed this
        // state. The set-1-vs-set-2 window rule is a question about when the
        // traffic happened, not about when someone opened the file.
        QDateTime lastSeen;
        bool     idsManual = false;
        // Has a real SOURCE_STN_ID ever been seen for this loco? The ctrl id
        // on a @rand_num tag is NOT a station id; it is tolerated only as a
        // placeholder until a station packet supplies the true value, and it
        // must not overwrite one afterwards.
        bool     stnFromStation = false;
        SessionKeyGen::Result result;
    };

    // Snapshot the loco's current combination if it is complete and not one
    // we already hold; otherwise just mark the existing one as still in force.
    void captureSnapshot(int locoId, const QString &changedBy);

    LocoState &stateFor(int locoId);              // creates on first sight
    const LocoState *find(int locoId) const;      // null when unknown
    const LocoState *resolve(int locoId) const;   // find(), or the active one
    void rederive(int locoId, const QString &changedBy = QString());

    QVector<KeySnapshot>  m_snaps;
    QHash<int, int>       m_currentSnap;   // loco -> snapshot id in force
    int                   m_nextSnapId = 1;

    QHash<int, LocoState> m_states;
    QList<int>            m_order;     // first-seen order, for display
    int                   m_active = -1;
};

#endif  // SESSIONKEYSTORE_H
