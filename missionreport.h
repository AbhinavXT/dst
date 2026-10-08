#ifndef MISSIONREPORT_H
#define MISSIONREPORT_H

// =============================================================================
//  Missions (session 171) — Tools ▸ Monitor ▸ Mission report…
//  -----------------------------------------------------------------------------
//  A day's capture split into missions. A MISSION STARTS at a start of
//  mission (session 168: the first ARP in Stand_By with no direction, tag
//  or location after any other ARP) and ENDS at the next one; the last runs
//  to the end of the log. Traffic before the first start of mission is
//  "mission 0", begun before the log.
//
//  For each mission, read in one pass from the tab's rows:
//    phases        Stand_By (ARP), self test and train configuration (the
//                  DMI's alarm messages), then the first Staff_Responsible,
//                  On_Sight, Limited / Full_Supervision
//    modes         every mode change, and the time in each mode: the DMI's
//                  loco_mode while it sends, the loco's own ARP LOCO_MODE
//                  while the DMI is silent (over 3 s: the DMI is logged in
//                  bursts, and silent through a System_Failure restart)
//    outcome       the highest supervision reached; System_Failure, Trip
//    brakes        EB / FSB episodes (DMI brake_type 4 / 3)
//    speed, place  the highest DMI speed; the span of located positions
//                  (abs_loco_loc 0 = not known, left out)
//    tags          RFID tags read (@rfid unique), in order
//    radio         time the DMI showed no radio (signal_strength 0)
//    faults        NMS faults raised in it
//  Like the run summary, it reports what was OBSERVED and passes no verdict.
// =============================================================================

#include "runreport.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

class LogModel;

namespace Missions {

struct Mission {
    int     index = 0;                 // 1.. ; 0 = before the first start of mission
    qint64  fromMs = 0, toMs = 0;      // toMs: the next start of mission, or the last row
    int     fromRow = -1;
    bool    endsWithLog = false;       // no later start of mission in the log

    // Phases (0 = not seen in this mission).
    qint64  standbyToMs = 0;           // last ARP in the start-of-mission state
    QString afterStandby;              // the ARP LOCO_MODE that followed it
    qint64  selfTestFromMs = 0, selfTestToMs = 0;
    qint64  trainConfigFromMs = 0, trainConfigToMs = 0;
    qint64  firstSrMs = 0, firstOsMs = 0, firstLsMs = 0, firstFsMs = 0;

    QString modeSource;                // "DMI", "ARP" or "DMI + ARP"
    QVector<RunReport::Change> modes;  // mode changes
    QMap<QString, qint64> timeInMode;  // mode text -> ms
    QString firstMode, lastMode, highestMode;
    bool    systemFailure = false;
    qint64  systemFailureMs = 0;
    int     trips = 0;

    QVector<RunReport::Episode> eb, fsb;
    QVector<RunReport::BrakeEvent> brakes;   // session 175: EB/FSB with their reasons
    double  maxSpeedKmh = 0;
    qint64  maxSpeedMs = 0;
    qint64  minLocM = 0, maxLocM = 0;  // 0 = no located frame
    QStringList tags;                  // tag ids read, in order, repeats collapsed
    qint64  noRadioMs = 0;
    int     dmiFrames = 0;
    int     arpModeFrames = 0;         // ARPs whose mode was taken (the DMI silent)
    QStringList faultsRaised;          // "subsystem / module: fault", first raise each

    qint64 durationMs() const { return toMs - fromMs; }
    // Start of mission to the first Staff_Responsible (0 = not reached).
    qint64 startUpMs() const { return firstSrMs ? firstSrMs - fromMs : 0; }
};

struct Options {
    int     maxListRows = 200;
    qint64  noRadioGapCapMs = 5000;    // a longer gap between @dmi is not counted as "no radio"
    qint64  dmiSilentMs = 3000;        // the ARP's mode is taken once the DMI has been silent this long
    // For the report's header: the tab dropped its oldest rows.
    bool    tabFull = false;
    int     tabRows = 0;
};

QVector<Mission> split(const LogModel *model, const Options &options = Options());

QString toHtml(const QVector<Mission> &missions, const QString &tabKey, const QString &tabName,
               const Options &options = Options());

// "4 (Full_Supervision)" -> "Full_Supervision"
QString modeName(const QString &mode);

}  // namespace Missions

#endif // MISSIONREPORT_H
