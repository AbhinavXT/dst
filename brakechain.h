#ifndef BRAKECHAIN_H
#define BRAKECHAIN_H

// =============================================================================
//  Brake chain (session 453) — Tools ▸ Monitor ▸ Equipment ▸ Brake chain…
//  -----------------------------------------------------------------------------
//  Each brake KAVACH applied, and how far it got: one row per application
//  with the time from KAVACH's decision to each link the capture shows.
//
//    Decision        @dmi brake_type turns to NB / FSB / EB / LE (2-5), with
//                    the reasons RunReport::brakeEvents() finds (session 175)
//    Output          a DOP1 / DOP2 brake pin's output= bit changes
//                    (normal_brake, full_service_brake, emergency_brake,
//                    light_engine_brake), as logged: which value applies
//                    the brake is not interpreted
//    Relay feedback  a DIP *_relay_fb_* (or DIP_OLD *_fdbk) bit changes
//    BIU command     @biu_cmd's A9 / SA9 targets, their validity or the
//                    isolate bit differ from the command before
//    BIU pressure    @biu_press BP or BC moves kMoveKg from where it stood,
//                    then reaches the command's targets (within kAtTargetKg)
//    Analog pressure @analog_top / @analog_bottom channels 1 and 2 (BP, BC,
//                    kg/cm², Abhinav 2026-10-31: not yet seen non-zero), the
//                    same two links
//    Speed           @dmi train_speed first below the onset's by 1 km/h,
//                    then 0
//
//  Everything is observed; a link the capture does not show is "not seen",
//  never "failed". Each link is searched from kLeadMs before the decision
//  (a .cap stamps to the second, so a link may share it, or log first) to
//  the next decision, at most kFollowMs after.
// =============================================================================

#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

class LogModel;

namespace BrakeChain {

constexpr qint64 kLeadMs = 2000;
constexpr qint64 kFollowMs = 60000;
constexpr double kMoveKg = 0.1;
constexpr double kAtTargetKg = 0.1;

enum Step {
    Decision, Output, RelayFeedback, BiuCommand, BiuMoves, BiuAtTarget,
    AnalogMoves, AnalogAtTarget, SpeedFalls, Standstill, StepCount
};
QString stepName(int step);          // "BIU at target"

struct Link {
    qint64  ms = -1;                 // -1: not seen
    int     row = -1;                // the tab row it was read from
    QString what;                    // "DOP1 emergency_brake output 1 → 0"
    bool seen() const { return ms >= 0; }
};

struct Application {
    qint64      fromMs = 0, toMs = 0;    // the DMI's brake_type span
    int         row = -1;
    int         brakeType = 0;           // 2..5
    QString     type;                    // "4 (EMERGENCY_BRAKE)"
    QStringList reasons;
    double      speedAtOnset = -1;       // km/h; -1: no speed before it
    Link        links[StepCount];
    // links[step].ms - fromMs; -1 when not seen (a link may log first: < 0 is
    // allowed, so test seen(), not the sign).
    qint64 delayMs(int step) const { return links[step].seen() ? links[step].ms - fromMs : -1; }
};

struct Point { qint64 ms = 0; double v = 0; };
enum SeriesId { Speed, BiuBp, BiuBc, A9, Sa9, AnalogTopBp, AnalogTopBc, AnalogBottomBp, AnalogBottomBc, SeriesCount };
QString seriesName(int id);

struct Change { qint64 ms = 0; int row = -1; QString what; };

struct Chain {
    qint64 fromMs = 0, toMs = 0;
    QVector<Application> apps;
    QVector<Point> series[SeriesCount];
    QVector<Change> outputs, relays;
    int biuCommands = 0, biuPressures = 0, analogFrames = 0, dioFrames = 0;
};

Chain build(const LogModel *model);

// "+1.2 s", "−0.4 s", "+3 s" (whole seconds print without a decimal).
QString delayText(qint64 ms);
// One line per application, a column per link (delay in seconds, empty when
// not seen), for a spreadsheet.
QString toCsv(const Chain &c);

}  // namespace BrakeChain

#endif // BRAKECHAIN_H
