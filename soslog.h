#ifndef SOSLOG_H
#define SOSLOG_H

// =============================================================================
//  SosLog (session 184) — the firmware's SoS state, from @sos / @sossrc / @sosev
//  -----------------------------------------------------------------------------
//  LKAVACH v1.2.9 keeps a table of up to 8 source locos and 2 stations
//  (sos_table_manager.c) and picks one target from it every second. No build
//  logs any of that yet; SOS_handoff/01_SOS_LOGGING_PACKETS.md asks for three
//  capture lines, and this unit reads them:
//
//    @sos     one snapshot: own loco, the chosen target, both station slots
//    @sossrc  one line per in-use source slot, right after its @sos (same
//             snap_id): position as the ARP said and as adjusted, distances,
//             threats, and every check re-run by the firmware's own functions
//    @sosev   one decision: a threat starting or ending (and why), a brake
//             applied or not applied (and why), a slot added or dropped
//
//  Layout version 1, little-endian, written byte by byte (no struct
//  padding). Decoded here directly, for speed over a whole log; the schema
//  (kavach.xml SOS_SNAPSHOT / SOS_SOURCE / SOS_EVENT) decodes the same bytes
//  for the inspector, and tests/test_session184.cpp checks the two agree on
//  every fixture frame. A frame of another version is counted, not guessed.
//
//  OBSERVED, NOT JUDGED
//    Everything here is what the loco logged it decided. The texts say "the
//    firmware ended it: ..." — never "wrongly". Verdicts belong to the
//    signatories.
//
//  NO REAL CAPTURE YET: schema/fixtures/sos_synthetic_loco{1,2}.log, made by
//  tests/sosgen from the README's own logging code, stand in.
// =============================================================================

#include "runreport.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

class LogModel;

namespace SosLog {

constexpr int kVersion = 1;

// Threat / source codes: @sos agg_threat, @sosev aux1. 0..5 = SOS_ThreatType_t.
enum Threat {
    ThreatNone = 0, HeadOn = 1, RearEnd = 2, UnusualStop = 3, ManualSos = 4, TrainParted = 5,
    StationGeneral = 6, DestGeneral = 7, StationHeadOn = 8, StationRearEnd = 9, Spad = 10,
    ShuntingLimit = 11
};

enum EventCode {
    EvSrcAdded = 1, EvSrcEvicted = 2, EvSrcDroppedFull = 3, EvSrcQuietReleased = 4,
    EvSrcTimeout = 5, EvArpRejected = 6, EvThreatStart = 7, EvThreatEnd = 8,
    EvTargetRemoved = 9, EvBrakeApplied = 10, EvBrakeSkipped = 11, EvStnAdded = 12,
    EvStnEvicted = 13, EvStnDroppedFull = 14, EvStnQuietReleased = 15, EvStnSosStart = 16,
    EvStnSosEnd = 17, EvDestLocoSos = 18, EvTableReset = 19, EvSelfUnusualAlarm = 20,
    EvSelfUnusualEnd = 21, EvSelfUnusualAck = 22
};

// @sossrc threat_flags / closest_flags bits.
enum ThreatBit { BitManual = 0x01, BitUnusual = 0x02, BitHeadOn = 0x04, BitRearEnd = 0x08, BitParted = 0x10 };

// @sossrc eval_flags bits.
enum EvalBit {
    EvalAdjusted = 0x0001, EvalAdjacent = 0x0002, EvalAdjFromTag = 0x0004,
    EvalAdjFromProfile = 0x0008, EvalAdjFailOpen = 0x0010, EvalStationOkFirmware = 0x0020,
    EvalTargetRemoved = 0x0040, EvalManualWouldProcess = 0x0080, EvalUnusualWouldProcess = 0x0100,
    EvalSameTin = 0x0200, EvalWithinTrigger = 0x0400, EvalWithinCollision = 0x0800,
    EvalStationOkSlot = 0x1000
};

// @sos lp_flags bits (loco_params.is_*_recvd).
enum LpBit {
    LpAccessSos = 0x0001, LpUnusualStop = 0x0002, LpTrainParted = 0x0004, LpHeadOn = 0x0008,
    LpRearEnd = 0x0010, LpAddEmSos = 0x0020, LpDestLocoSos = 0x0040, LpStnHeadOn = 0x0080,
    LpStnRearEnd = 0x0100, LpStnShunting = 0x0200
};

struct Station {
    bool    inUse = false;
    quint8  flags = 0;           // bit0 additional emergency SoS, bit1 last gen_sos_call, bit2 closest
    int     id = 0;
    quint32 ageMs = 0;
    quint32 absLocM = 0;
    double  distM = 0.0, sosDistM = 0.0;
    bool addEmSos() const { return flags & 0x01; }
    bool genSosCall() const { return flags & 0x02; }
    bool closest() const { return flags & 0x04; }
};

struct Source {
    int     slot = -1;
    quint32 snapId = 0;
    quint32 locoId = 0;
    quint32 ageMs = 0;
    quint32 frameNo = 0;
    int     emergency = 0;        // the ARP's EMERGENCY_STATUS, as received
    int     mode = 0;
    int     speedRaw = 0;         // the ARP's TRAIN_SPEED, raw
    qint32  rawLocM = 0;          // the ARP's location, before SOSWithAdjustment
    int     rawDir = 0;
    int     lastTag = 0;
    int     approachingStation = 0;
    qint32  locM = 0;             // after adjustment: what the checks use
    int     dir = 0;
    int     tin = 0;
    int     lengthM = 0;
    double  distM = 0.0, sosDistM = 0.0, collisionDistM = 0.0;
    quint8  threats = 0;          // ThreatBit
    quint8  closest = 0;          // ThreatBit: closest of its kind
    quint16 eval = 0;             // EvalBit
    bool has(int threatBit) const { return threats & threatBit; }
    bool check(int evalBit) const { return eval & evalBit; }
};

struct Snapshot {
    int     row = -1;             // source row in the tab
    qint64  epochMs = 0;
    int     why = 0;              // 0 periodic, 1 after ARP, 2 after AEP, 3 after DEST_LOCO_SOS
    quint32 snapId = 0, tickMs = 0;
    double  ownLocM = 0.0;
    int     ownDir = 0, ownMode = 0, ownTin = 0, ownLengthM = 0;
    double  ownSpeed = 0.0;       // sensor_speed, its own unit
    qint32  ownMaM = 0;
    int     ownStation = 0, ownTag = 0;
    bool    rearTagFound = false;
    int     rearSect = 0xFF;
    int     ownEmergency = 0;     // what the loco's own ARP broadcasts
    int     accessReqStation = 0; // access_req_recvd_stn_id (one global)
    int     nSourceSlots = 0;
    quint16 inUseMask = 0, activeMask = 0;
    int     aggThreat = 0;        // Threat
    bool    aggStationWon = false;
    quint32 collisionLoco = 0;
    int     aggStation = 0;
    double  collisionDistM = 0.0, sosDistM = 0.0;
    int     sosStation = 0;
    quint16 lp = 0, dmi = 0;
    int     lastDestLocoSos = 0;
    quint8  self = 0;             // bit0 alarm, bit1 acknowledged, bit2 standstill, bit3 in block section
    QVector<Station> stations;
    QVector<Source>  sources;     // its @sossrc lines

    int expectedSources() const;  // popcount(inUseMask)
    bool complete() const { return sources.size() == expectedSources(); }
    const Source *source(quint32 locoId) const;
};

struct Event {
    int     row = -1;
    qint64  epochMs = 0;
    quint32 tickMs = 0, snapId = 0;
    int     code = 0, aux1 = 0, aux2 = 0;
    quint32 id = 0;
    double  distM = 0.0, ownLocM = 0.0, ownSpeed = 0.0;
    int     ownMode = 0;
};

// Byte decoders. False (and nothing written) for a short frame or another version.
bool decodeSnapshot(const QByteArray &b, Snapshot *out);
bool decodeSource(const QByteArray &b, Source *out);
bool decodeEvent(const QByteArray &b, Event *out);

// One tab's SoS log, in row order.
struct Timeline {
    QVector<Snapshot> snaps;
    QVector<Event>    events;
    int otherVersion = 0;     // frames of a layout version this build does not read
    int orphanSources = 0;    // @sossrc with no matching @sos before it
    bool isEmpty() const { return snaps.isEmpty() && events.isEmpty(); }
    // Index of the last snapshot at or before `ms` / source row `row`; -1 if none.
    int snapAtOrBefore(qint64 ms) const;
    int snapAtOrBeforeRow(int row) const;
};

// Every @sos/@sossrc/@sosev row of `model`, windowed if fromMs/toMs > 0 (both).
Timeline extract(const LogModel *model, qint64 fromMs = 0, qint64 toMs = 0);
// Does `model` carry any @sos line? (Cheap: stops at the first.)
bool hasSos(const LogModel *model);

// ---- words ---------------------------------------------------------------------
QString threatName(int threat);                 // "Manual SoS", "Head-on", ...
QString threatBitsText(quint8 bits);            // "manual SoS, head-on" / "none"
QString dirName(int dir);                       // "nominal" / "reverse" / "undefined"
QString emergencyName(int status);              // "2 (SoS)"
QString endReasonText(int reason);
QString eventText(const Event &e);              // one line, plain words
// What the loco is reacting to at this snapshot, a few lines.
QStringList decisionLines(const Snapshot &s);
// A source's checks, each "label" with on/off. Order is the table's.
struct Check { QString label; bool on = false; bool warn = false; QString tip; };
QVector<Check> checks(const Source &src);

// ---- spells and brakes, for lanes and reports -----------------------------------
//  A spell is one threat from one source (loco or station), from its start
//  event to its end (end reason named), or to the timeout / table reset /
//  eviction that cleared it. One still open when the log ends runs to the
//  end and says "(no end)".
struct Spell {
    qint64  fromMs = 0, toMs = 0;
    int     row = -1;             // the start event's row
    int     threat = 0;
    quint32 source = 0;           // loco id, or station id for station threats
    bool    station = false;
    double  startDistM = 0.0, endDistM = 0.0;
    int     endReason = 0;        // SOS_END_*; 0 = closed by timeout/reset/evict, or open
    QString endedBy;              // words: "the source stopped sending it", "timeout", ...
    bool    open = false;
    QString what() const;         // "Manual SoS from loco 2"
};
QVector<Spell> spells(const Timeline &t);
// Brake decisions: @sosev 10 (applied) and 11 (not applied), in order.
QVector<Event> brakeDecisions(const Timeline &t);

// RunReport-shaped, for the lanes and the reports.
QVector<RunReport::Episode> spellEpisodes(const Timeline &t);
QVector<RunReport::Change>  brakeChanges(const Timeline &t);   // to = the decision in words

// ---- two logs: what one loco sent, and what another did with it (phase D) ---------
//  Every change of the sender's own emergency status (its @sos own_emergency_status,
//  which is what its ARP broadcasts), followed in the receiver's log: the first
//  @sossrc for the sender whose last_emergency_sts shows the new value (heard),
//  and the receiver's first decision about the sender after that (acted on).
//  The two logs' clocks are the two locos' RTCs, so the delays are only as good
//  as those clocks agree; a delay under 0 is shown as such, not hidden.
struct Relay {
    qint64  sentMs = 0;
    int     sentRow = -1;
    int     fromStatus = 0, toStatus = 0;
    qint64  heardMs = -1;         // -1: not heard within kRelayWindowMs
    int     heardRow = -1;
    qint64  actedMs = -1;         // -1: no decision about the sender within the window
    int     actedRow = -1;
    QString acted;                // eventText of that decision
    bool    tracked = false;      // the receiver had the sender in its table at all
};
constexpr qint64 kRelayWindowMs = 20000;
QVector<Relay> relay(const Timeline &sender, const Timeline &receiver, quint32 senderId);

// The loco id a tab key names ("2_1" -> 2); 0 if none.
quint32 locoOfKey(const QString &key);

}  // namespace SosLog

#endif // SOSLOG_H
