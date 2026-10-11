#ifndef FLEETSUMMARY_H
#define FLEETSUMMARY_H

// =============================================================================
//  Fleet summary (session 215) — Tools ▸ Monitor ▸ Reports ▸ Fleet summary…
//  -----------------------------------------------------------------------------
//  A folder of logs, one row per loco per day. Each loco's files of one day
//  (81_1.log and its 81_1.001.log, ...) are read together into one model, and
//  the row is what the existing reports say about it:
//    RunReport   span, modes (time in each), EB / FSB and their reasons, top
//                speed, faults raised, reject-rule frames, tags, missions
//    RadioLinks  SLRP lost in contact, outages (session 214)
//    Odometry    the median error at the tags (session 213)
//    Missions    the start-up to Staff_Responsible, and its slowest step (219)
//    distance    the DMI (or LSRP) location's steps added up, steps over
//                500 m left out (relocations: SpeedDistance's rule)
//
//  FILES
//    DLConsole .log (the source from its header: source_id / kvch_id; the
//    time from its first column, to the ms) and .cap (the source from the
//    name, loco_<id>_<kvch>_...; the time from the @-line). The folder and
//    its subfolders. A file belongs to the day of its first frame (local
//    time): a log running past midnight stays with the day it started.
//    .dlr is not read: DLConsole writes a .log beside every .dlr.
//
//  Observed, not judged, like the reports it reads.
// =============================================================================

#include <QDate>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

class LogModel;

namespace Fleet {

struct Group {
    quint8  sourceId = 0;
    quint16 kvchId = 0;
    QString loco;                       // "81_1"
    QDate   day;
    QStringList files;                  // in time order
};

struct Row {
    QString loco;
    QDate   day;
    QStringList files;
    int     frames = 0;
    qint64  firstMs = 0, lastMs = 0;
    double  distanceKm = 0.0;
    QMap<QString, qint64> modeMs;       // mode name -> time in it
    int     eb = 0, fsb = 0;
    QString brakeReasons;               // the commonest, "Overspeed ×3, ..."
    double  maxSpeedKmh = 0.0;
    int     faults = 0;                 // raised
    int     rejectFrames = 0;
    int     tags = 0;
    int     missions = 0;
    int     startUps = 0;               // session 219: missions that reached Staff_Responsible
    qint64  startUpMedianMs = 0;        // start of mission to Staff_Responsible, their median
    QString slowestSteps;               // the step that took longest, commonest first: "Train config ×3; ..."
    int     slrpReceived = 0, slrpLost = 0, outages = 0;
    double  slrpLostPercent() const { return slrpReceived + slrpLost > 0 ? 100.0 * slrpLost / (slrpReceived + slrpLost) : 0.0; }
    int     odoReads = 0;
    double  odoMedianM = 0.0;
    QString error;                      // a file that could not be read
    QString modesText() const;          // "Full_Supervision 52 % · Staff_Responsible 30 % · ..."
};

// The .log / .cap files under `dir`, grouped by loco and day, in that order.
QVector<Group> scan(const QString &dir);

// Reads a group's files into a model of their own and summarises it.
// `keepGoing`, if set, is called before each file and every 50,000 frames (to keep a window
// painting); false stops, and an empty Row comes back.
Row summarise(const Group &group, const std::function<bool()> &keepGoing = {});

// The row from a model already read (summarise() calls it).
Row summariseModel(const LogModel *model, const QString &tabKey);

// The table as CSV, a header and one line per row.
QString toCsv(const QVector<Row> &rows);

}  // namespace Fleet

#endif // FLEETSUMMARY_H
