#ifndef SIMPREVIEW_H
#define SIMPREVIEW_H

// =============================================================================
//  RFID simulator preview (session 203) — the RFID Tag Builder's Simulator
//  tab. What the RFID simulator (LocoTcasSimulator, LocoDialog::
//  calculateNextDistance / sendingRfidData) would send for a route, tick by
//  tick, without the simulator, the serial ports or a loco unit.
//  -----------------------------------------------------------------------------
//  PORTED FROM THE SIMULATOR, its arithmetic included:
//    tick        100 ms (TimerWorker: msleep(100))
//    pulses      UINT_32(0.1 * v[m/s] * 30 / (PI * 0.95) * 1000), PI =
//                3.1428571, v = km/h * 0.277777778: truncated, as sent
//    distance    PI * 0.95 * pulses / 30 / 1000 m per tick, + for a route of
//                dir 1, - for dir 2, from the start tag's abs_loc
//    the row     the first row (in file order) with abs_loc <= d <
//                next_rfid_abs_loc, or abs_loc >= d > next_rfid_abs_loc. A
//                route's LAST row (next = its own location) never matches: the
//                simulator never sends the last tag of a route.
//    reader 1    sends a row's tag when the loco enters its span (a new row)
//    reader 2    keeps the last main tag and the last duplicate tag entered,
//                per tag type; sends the main one when the loco is 20-24 m past
//                the CURRENT row's abs_loc, then the duplicate one at >= 24 m.
//                A tick longer than 4 m (above about 144 km/h) can step over
//                the 20-24 m window: that main tag is then never sent on 2.
//    missing     a listed tag is skipped: the row before it stays current
//    the end     no row spans the location (past the last row): the
//                simulator would then look for the next station's main route;
//                the preview stops there
//  The simulator reads abs_loc, next_rfid_abs_loc, tag_type and tag_name from
//  the file's rows. For a route opened from a route.xml / Configuration1.xml
//  whose rows still hold the same tags, those are used; otherwise the values
//  route.xml would be written with (RfidTag::routeRows).
//  NOT modelled: acceleration and braking (constant speed), reverse
//  movement / roll-back, the second speed sensor, chaining to the next
//  station's route.
// =============================================================================

#include "planrun.h"
#include "rfidtag.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace SimPreview {

struct Options {
    double      speedKmh = 60;
    int         startRow = 0;
    bool        reader1 = true, reader2 = true;
    QStringList missing;          // rfid_id as the rows name them ("904", "904D")
    int         maxTicks = 2000000;
};

struct Event {
    int     tick = 0;
    double  timeS = 0;
    double  distance = 0;         // the simulator's location when it sent
    int     reader = 1;
    int     row = 0;              // the route row whose tag was sent
    QString tag;                  // its name ("904D")
    qint64  tagLoc = 0;           // the location the tag itself carries
};

struct Result {
    bool    ok = false;
    QString error;
    QVector<Event> events;
    int     ticks = 0;
    double  endDistance = 0;
    QString endReason;
    quint32 pulsesPerTick = 0;
    double  metresPerTick = 0;
    bool    fromFileRows = false; // the file's own abs_loc / next / tag_type
    QStringList neverOnReader1;   // rows the run passed whose tag reader 1 never sent
    QStringList mainsMissedOnReader2;
};

quint32 pulsesPerTick(double speedKmh);
double  metresPerTick(double speedKmh);
Result  run(const RfidTag::Route &route, const Options &options);

// Session 205: a preview against a loco log's tag reads (PlanRun::readsOf).
// Each reader's sends are matched in order to that reader's reads of the
// same tag (main / duplicate by name); a send with no such read later in the
// log is "not read". Times are lined up on the first matched send: its lag
// is 0 and every other one is the log's time minus the preview's. Reads
// between the first and the last matched one that match no send are "not
// predicted". The lag only means something for a run the simulator drove
// at the previewed speed from the previewed start tag.
struct Against {
    QVector<int>    readOf;       // per event: index into the reads, -1 = not read
    QVector<double> lagS;         // per event: log time - preview time (s), 0 if not read
    int             matched = 0;
    double          maxLagS = 0;  // the largest |lag|
    QVector<int>    unpredicted;  // reads (indices) inside the matched span that no send explains
};
Against against(const Result &preview, const QVector<PlanRun::Read> &reads);

}  // namespace SimPreview

#endif  // SIMPREVIEW_H
