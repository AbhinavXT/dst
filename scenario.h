#ifndef SCENARIO_H
#define SCENARIO_H

// =============================================================================
//  Test scenario (session 407) — the Loco Simulator's Scenario tab: a list
//  of steps, each a trigger and an action across both simulators, run the
//  same way every time.
//  -----------------------------------------------------------------------------
//  TRIGGERS  at a time (s after Run), at a tag (when reader 1 first sends it
//            after Run), at a location (m: the route position when a route
//            is open, else the odometer; when the loco reaches or passes it)
//  ACTIONS   the loco:    speed (km/h), accelerate / decelerate (km/h/s),
//                         hold
//            the readers: skip a tag (1 on, 0 off), a tag's fault (none,
//                         late, twice, crc; value: late's ms), a reader on
//                         or off (on: 1 or 2; value 1 / 0; session 409:
//                         kind ok or fail sets its health too)
//            the bench:   an air fault (on: BP / BC / MR; kind: none, stuck,
//                         slow, leak; value: slow's factor or leak's rate),
//                         the BIU silent (1) or talking (0), a BIU fault
//                         code raised (value, or text "0x1040")
//            a note, which only marks the recording
//  Each step fires once. A bench step with no Brake Test Bench open is not
//  done, and says so.
//
//  THE FILE (.scenario.json):
//    {"format":"dlconsole-scenario","version":1,"name":…,"steps":[
//      {"when":"time"|"tag"|"location","at":10,"tag":"904",
//       "do":"speed"|…,"on":"BC","kind":"stuck","value":0,"text":"…"}, …]}
// =============================================================================

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace Scenario {

enum class When { Time, Tag, Location };
enum class Do {
    Speed, Accelerate, Decelerate, Hold,
    SkipTag, TagFault, Reader,
    AirFault, BiuSilent, BiuFault,
    Note
};

struct Step {
    When    when = When::Time;
    double  at = 0;          // s (Time) or m (Location)
    QString tag;             // When::Tag
    Do      action = Do::Note;
    QString on;              // the tag, the reader ("1", "2") or the pressure ("BP", "BC", "MR")
    QString kind;            // TagFault: none / late / twice / crc; AirFault: none / stuck / slow / leak
    double  value = 0;
    QString text;            // Note; BiuFault's code as "0x1040"
};

struct Script {
    QString name;
    QVector<Step> steps;
};

QStringList whenNames();               // "at a time", "at a tag", "at a location"
QStringList doNames();                 // in Do's order: "speed", "accelerate", …
QString     doKey(Do d);               // the file's word: "speed", "airFault", …
bool        doFromKey(const QString &key, Do *out);
bool        isBenchAction(Do d);
// What the step does, in words: "at 10 s: BC stuck", "at tag 904: tag 906 crc".
QString     describe(const Step &step);
// False (and *why) when a step cannot be run as written: a tag trigger with
// no tag, an action with no target it needs, a kind it does not know.
bool        check(const Script &script, QString *why);

QByteArray  toJson(const Script &script);
bool        fromJson(const QByteArray &json, Script *out, QString *why = nullptr);

}  // namespace Scenario

#endif  // SCENARIO_H
