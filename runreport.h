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
//    emergency status              LSRP EMERGENCY_STATUS != 0, as episodes
//    RFID tags                     LSRP LAST_RFID_TAG, each change
//    speed                         DMI (or LSRP) speed; above-permitted
//                                  episodes from DMI speed_limit_permissible
//    clock skew                    SLRP FRAME_NUM against the loco's own
//                                  (LSRP/ARP) FRAME_NUM heard just before it,
//                                  outside the accept window, as episodes
//    faults                        NMS fault frames: each fault raised and
//                                  cleared
//    reject conditions             SLRP frames matching a reject rule
//                                  (rejectrules.xml), counted by clause
//  Every list is capped (the count is always the full count).
// =============================================================================

#include <QMap>
#include <QString>
#include <QVector>

class LogModel;

namespace RunReport {

struct Options {
    qint64 gapThresholdMs = 5000;
    int    maxListRows    = 200;
};

struct Gap        { qint64 fromMs = 0; qint64 toMs = 0; QString lastBefore; };
struct Change     { qint64 ms = 0; int row = -1; QString from; QString to; };
struct Episode    { qint64 fromMs = 0; qint64 toMs = 0; QString what; double worst = 0.0; int row = -1; };
struct FaultEvent { qint64 ms = 0; bool raised = true; QString text; };

struct Summary {
    QString tabKey, tabName;
    int     rows = 0;
    qint64  firstMs = 0, lastMs = 0;
    QMap<QString, int>  packetCounts;      // type -> rows
    QVector<Gap>        gaps;
    QVector<Change>     modeChanges;
    QString             firstMode;
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
    QMap<QString, int>  rejectClauses;     // "31.16.1 PKT_DIR" -> frames
    int                 slrpFrames = 0;
};

Summary summarise(const LogModel *model, const QString &tabKey, const QString &tabName,
                  const Options &options = Options());

QString toHtml(const Summary &s, const Options &options = Options());

}  // namespace RunReport

#endif // RUNREPORT_H
