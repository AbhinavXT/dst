#ifndef RUNREPORT_H
#define RUNREPORT_H

// =============================================================================
//  Run summary report (session 81) — Tools ▸ Monitor ▸ Run summary report…
//  -----------------------------------------------------------------------------
//  One page at the end of a run: what the capture of one tab shows, read in
//  one pass rather than scrolled through. Written for the test record, so it
//  follows the acceptance tooling's rule: it reports what was OBSERVED, and
//  passes no verdict. Verdicts belong to the signatories.
//
//  What it reads (from the tab's own rows; nothing live):
//    span and packet counts        every row
//    silences in the traffic       every row, gaps over a threshold
//    loco mode changes             LSRP LOCO_MODE
//    start of mission (168)        ARP in Stand_By with no direction, tag
//                                  or location, as episodes
//    emergency status              LSRP EMERGENCY_STATUS != 0, as episodes
//    brakes (175)                  DMI EB/FSB, each with its reasons
//    self SoS (178)                NMS LOCO_SELF_SOS start..end; COLLISION_DETECTION
//    RFID tags                     LSRP LAST_RFID_TAG, each change
//    speed                         DMI (or LSRP) speed; above-permitted
//                                  episodes from DMI speed_limit_permissible
//    clock skew                    SLRP FRAME_NUM against the loco's own
//                                  (LSRP/ARP) FRAME_NUM heard just before it,
//                                  outside the accept window, as episodes
//    faults                        NMS fault frames: each fault raised and
//                                  cleared
//    reject conditions             SLRP and received-ARP frames matching a
//                                  reject rule (rejectrules.xml), by clause
//  Every list is capped (the count is always the full count).
// =============================================================================

#include <QMap>
#include <QString>
#include <QVector>

#include <cmath>

class LogModel;

namespace RunReport {

struct Options {
    qint64 gapThresholdMs = 5000;
    int    maxListRows    = 200;
    // Session 97: restrict summarise() to rows with fromMs <= epochMs <=
    // toMs (the incident report's window). Unset (the default) reads the
    // whole tab, exactly as before.
    bool   hasWindow = false;
    qint64 fromMs = 0;
    qint64 toMs   = 0;
};

struct Gap        { qint64 fromMs = 0; qint64 toMs = 0; QString lastBefore; };
struct Change     { qint64 ms = 0; int row = -1; QString from; QString to; };
struct Episode    { qint64 fromMs = 0; qint64 toMs = 0; QString what; double worst = 0.0; int row = -1; };

// Turns a boolean condition sampled over time into distinct episodes (an
// overspeed spell, an emergency-status spell, ...): consecutive active
// samples extend the current episode and keep its worst value; an inactive
// sample closes it. Shared with IncidentReport (incidentreport.h), which
// tracks EB/FSB applications the same way RunReport tracks overspeed.
struct EpisodeTracker {
    QVector<Episode> *out = nullptr;
    bool open = false;
    void observe(bool active, qint64 ms, int row, const QString &what, double worst, bool worseIsMore = true)
    {
        if (active) {
            if (!open) {
                Episode e; e.fromMs = e.toMs = ms; e.what = what; e.worst = worst; e.row = row;
                out->append(e);
                open = true;
            } else {
                Episode &e = out->last();
                e.toMs = ms;
                const bool worse = worseIsMore ? std::abs(worst) > std::abs(e.worst) : false;
                if (worse) { e.worst = worst; e.what = what; }
            }
        } else {
            open = false;
        }
    }
};
struct FaultEvent { qint64 ms = 0; bool raised = true; QString text; };

// Session 175: an EB / FSB application (@dmi brake_type 4 / 3) and what the
// capture says about why, from three places, all observed:
//   NMS    BRAKE_APPLICATION_REASON (@nmshlth) within kBrakeReasonWindowMs
//          of the onset ("Overspeed", "No LP Acknowledge", "Loco Specific SoS")
//   DMI    the context message and system alarm shown with it ("SOS - Other
//          Loco Manual", "System Fault, Isolate or Restart KAVACH")
//   DMI    collision_loco_id, when the DMI names another loco
// Nothing is inferred: with none of these, the event says so.
constexpr qint64 kBrakeReasonWindowMs = 10000;
struct BrakeEvent {
    qint64      fromMs = 0, toMs = 0;
    int         row = -1;
    QString     type;          // the brake_type text at onset
    QStringList reasons;       // "NMS: 3  (Overspeed)", "DMI: SOS - Other Loco Manual", ...
    QString reasonsText() const;
};
QVector<BrakeEvent> brakeEvents(const LogModel *model, qint64 fromMs = 0, qint64 toMs = 0);

struct Summary {
    QString tabKey, tabName;
    int     rows = 0;
    qint64  firstMs = 0, lastMs = 0;
    QMap<QString, int>  packetCounts;      // type -> rows
    QVector<Gap>        gaps;
    QVector<Change>     modeChanges;
    QString             firstMode;
    // Session 168: each spell of ARPs in the start-of-mission state
    // (CaptureDecoder::isStartOfMission). fromMs / row: the first such ARP
    // after any other; toMs: the last; what: the LOCO_MODE of the ARP that
    // followed ("" while the window ends still in it).
    QVector<Episode>    missionStarts;
    QVector<Episode>    emergencies;
    QVector<Change>     tagReads;          // to = tag number, from = previous
    int                 distinctTags = 0;
    QString             speedSource;       // "dmi" / "lsrp" / ""
    double              maxSpeedKmh = 0.0;
    qint64              maxSpeedMs = 0;
    QVector<Episode>    overspeed;         // worst = km/h above permitted
    QVector<Episode>    clockSkew;         // worst = seconds (signed)
    int                 skewComparisons = 0;
    QVector<FaultEvent> faults;
    QVector<BrakeEvent> brakes;            // session 175: EB/FSB with reasons
    // Session 178: the loco's own SoS, from NMS LOCO_SELF_SOS: a start
    // (1 Manual SoS / 3 Unusual stop start) to its end (2 / 4); repeats of
    // either are folded; a start never ended runs to the end (what ends "(no end)").
    QVector<Episode>    selfSos;
    // NMS COLLISION_DETECTION ("loco 2, code 1"); repeats within 5 s folded.
    QVector<Change>     collisionDetections;   // to = the value
    QMap<QString, int>  rejectClauses;     // "31.16.1 PKT_DIR" -> frames
    int                 slrpFrames = 0;
    // Session 170: received ARPs (arprecv) judged by the same rules: the
    // own-ID one ("received ARP from this loco's own ID") in practice.
    QMap<QString, int>  arpRecvRejects;    // "DLConsole  SOURCE_LOCO_ID" -> frames
    int                 arpRecvFrames = 0;
    qint64              arpRecvOwnFirstMs = 0, arpRecvOwnLastMs = 0;
};

Summary summarise(const LogModel *model, const QString &tabKey, const QString &tabName,
                  const Options &options = Options());

QString toHtml(const Summary &s, const Options &options = Options());

}  // namespace RunReport

#endif // RUNREPORT_H
