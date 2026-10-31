#ifndef STATIONKAVACH_H
#define STATIONKAVACH_H
// =====================================================================
//  stationkavach.{h,cpp} -- session 422: a Stationary KAVACH, made of
//  data, answering a running loco.
//
//  THE DATA ("kavach" block of a Station Layout JSON, kept in
//  StationLayout::Layout::kavach): the station's signals and its routes,
//  one per row of the KAVACH Table of Control (RDSO/SPN/196/2020
//  Annexure-I, Amdt-6): entry signal -> exit signal, the RFID sequence
//  with linking distances (I.2.11.14), the points, track circuits and
//  TINs the route needs (I.2.11.9-13), the section type (I.2.11.4), the
//  OS authorised speed (I.2.11.17) and the route's track profile
//  (I.2.12: SSP, gradient, LC gates, turnouts, track conditions, TSR).
//  Every distance in a route counts from its entry signal's foot tag.
//
//  THE LIVE STATE (State): each signal's aspect, set by the operator as
//  the interlocking would; each point Normal / Reverse; the track
//  circuits and TINs occupied. The station checks the route against it
//  and SAYS what does not agree; it does not change the aspect.
//
//  THE ANSWER to an LSRP (answer()): the SLRP the station would send.
//    route   the one the loco is on: a route whose entry foot tag the loco
//            has just read (it has passed that signal), else the one it
//            was on, else any route holding the tag; between routes from
//            the same signal, the one whose points are set.
//    MA      in sections (I.2.11.16): the route the loco is on is section
//            1, each route on from its exit signal one more. The aspect of
//            the signal ahead (the route's exit) gives how many: Red 1,
//            Yellow 2, Double Yellow 3, Green 4 (the GGD example's
//            values; the file can change them). MA_W_R_T_SIG is the
//            distance from the start signal to the foot of the last
//            section's exit signal.
//    profile the routes of those sections end to end, cut at the MA end;
//            a new REF_PROF_ID whenever it changes, sent in full until the
//            loco's LAST_REF_PROFILE_NUM acknowledges it, the MA alone
//            after that.
//    SPAD    session 426: the loco's ABS_LOCO_LOC past the foot of the
//            route's exit signal while it shows Red, calling-on aside (the
//            layout's tag locations give where the ref tag is): DEST_LOCO_SOS
//            4, AUTHORITY_TYPE 0, MA 0, DIST_PKT_START 0, the MA alone, as
//            station 527 answered loco 1 past A904 (27 Jun 2026, 14:02).
//    ref     LAST_REF_RFID = the last tag the loco read, when it is on the
//            route; DIST_PKT_START = minus its distance from the start
//            signal; APPR_SIG_DIST = from it to the signal ahead.
//
//  Seen in replay/2026-10-08/loco_1_1_08102026_10{3520,5200}.cap
//  (station 527): MA 1650 m = 534 + 1116 (S43 Yellow, 2 sections); 1116 m
//  (A900 Red, 1); 1956 m = 1116 + 840 (A900 Yellow, 2); every profile cut
//  exactly at the MA end; the full profile sent until acknowledged.
//
//  Pure values; the Packet Maker sends and MACs. Anything that cannot be
//  sent correctly (a value too wide for its field, more rows than a
//  sub-packet holds) is refused by name, never clamped.
// =====================================================================
#include <QHash>
#include <QList>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include "schema/schemaencoder.h"   // Schema::SubEntry, Schema::Encoder

namespace StationKavach {

using Row = QHash<QString, qint64>;    // one repeat row: schema field name -> value

struct Signal {
    QString name;
    int     footTag = 0;
    qint64  sigInfo = 0;               // CUR_SIG_INFO (17 bits) when it is the signal ahead
    int     aspect = 1;                // sigAspect: the aspect the file starts with
};

struct RouteTag { int dist = 0, tag = 0, dupDir = 0; };   // dist: from the tag before

struct Route {
    QString name, entry, exit;         // entry / exit: signal names
    int     pktDir = 1;                // 1 Nominal, 2 Reverse: the direction of travel
    int     sectionType = 0;           // secType
    int     authority = 2;             // authType: 1 OS, 2 FS, 3 SR
    int     osSpeed = 0;               // speed6: AUTHORIZED_SPEED under OS authority
    QStringList pointsNormal, pointsReverse, trackCircuits, tins;
    QVector<RouteTag> tags;            // tags[0] = entry foot tag (dist 0); last = exit foot tag
    int     distDupTag = 0;
    QVector<int> adjTins;
    int     tin = 0;
    bool    locReset = false;
    int     locResetStart = 0, adjLocoDir = 0;
    qint64  absLocCorrection = 0;
    QVector<Row> ssp, gradient, lc, turnouts, trackConditions, tsr;   // schema field names
    int     length() const;            // entry foot to exit foot, m
};

struct Station {
    int stationId = 0, stnVersion = 0;
    QVector<Signal> signalList;
    QVector<Route>  routes;
    QHash<int, int> sections;          // aspect -> MA sections, overriding the defaults
    // Session 426: tag number -> the abs_loc of each tag of that number in
    // the layout (StationLayout::tagLocationsByNumber): not in the "kavach"
    // block. With it the station sees a loco past the Red signal ahead
    // (SPAD); empty, it cannot.
    QHash<int, QVector<qint64>> tagLocs;
    // Session 445: tag number -> its line (the lines sheet): a loco coming
    // the other way on the same line is seen (Head-on). Empty: it is not.
    QHash<int, QString> tagLine;
    int signalIndex(const QString &name) const;
    QStringList pointNames() const;    // every point, track circuit and TIN a route names
    QStringList trackCircuitNames() const;
    QStringList tinNames() const;
};

// The default number of MA sections for an aspect: Red, calling-on, stop
// board and unidentified 1; every Yellow 2; Double Yellow 3; Green and the
// AG marker 4.
int defaultSections(int aspect);
int sectionsFor(const Station &s, int aspect);
// "Red", "Double Yellow", "Yellow (route 33)", ...: an aspect code in words.
QString aspectName(int aspect);

// The "kavach" block. *err and false on a block that is not one; a row it
// cannot read is named in *notes.
bool fromJson(const QJsonObject &o, Station *out, QString *err = nullptr, QStringList *notes = nullptr);
QJsonObject toJson(const Station &s);

// What a route would not do as written, one line each: a signal or tag
// that is not there, a route whose exit is not the foot tag it ends on,
// a profile longer than its route. Reported, never corrected.
QStringList checks(const Station &s);
// Session 423: the tags a signal or route names that the layout's tags
// (`tagIds`, their unique ids) do not hold. Nothing when it has no tags.
QStringList tagChecks(const Station &s, const QSet<int> &tagIds);

// Session 424: where the layout's tags (`tagLocs`: tag number -> the
// abs_loc of each tag of that number it holds) do not lie as a route's
// linking distances say: the next tag `dist` m on in the route's direction
// (Nominal: rising locations). A number the layout holds more than once
// (a section drawn block after block) agrees when any of its tags does.
// Tags the layout lacks are tagChecks()'s.
QStringList locationChecks(const Station &s, const QHash<int, QVector<qint64>> &tagLocs);

struct State {
    QHash<QString, int>  aspect;       // signal -> sigAspect
    QSet<QString>        reverse;      // points lying Reverse (the rest Normal)
    QSet<QString>        occupied;     // track circuits and TINs occupied (by hand)
    // Session 441: track circuit -> the loco occupying it, from where the
    // locos' ARPs and LSRPs put them (StationInterlock::occupancy).
    QHash<QString, int>  occupiedBy;
    bool isOccupied(const QString &name) const { return occupied.contains(name) || occupiedBy.contains(name); }
    // Session 442: the routes set (entry signal -> route index), those a
    // train has entered, and whether the station works its signals from
    // the track circuits (StationInterlock::work; off: the operator's alone).
    QHash<QString, int>  routes;
    QSet<QString>        entered;
    bool                 interlocking = false;
    // Session 445: SoS the operator sends: loco id -> sosType (1 Foreign
    // RFID, 6 Head-on, 7 Shunt violation, 8 Station General); 0: every loco.
    QHash<int, int>      sos;
    // Session 446: ask the loco before shortening its MA (REQ_SHORTEN_MA:
    // "New, Future Use" in Annexure-C; station 527 never sent it). Off: the
    // shorter MA is sent at once, as before.
    bool                 askShorten = false;
    QHash<QString, int>  authority;    // route -> authType, overriding the file's
    QSet<QString>        tsrOff;       // "route/id": TSRs switched off
    static State initial(const Station &s);
    int aspectOf(const Station &s, const QString &signal) const;
};

// What the station remembers of one loco between answers.
struct Loco {
    // Session 427: where the loco was by its last LSRP (noted from every
    // one): a loco ahead cuts another's MA.
    int     id = 0;
    qint64  absLoc = -1;               // ABS_LOCO_LOC; -1: not yet heard
    int     trainLength = 0, dir = 0;  // TRAIN_LENGTH, MOVEMENT_DIR
    int     lastTag = -1;              // session 441: LAST_RFID_TAG (its line is the loco's)
    int     route = -1;
    int     refTag = -1;
    int     profId = 0;                // last REF_PROF_ID issued (0: none yet)
    QByteArray profKey;                // what that profile held
    // Session 428: what it was last given (Station Layout draws it): the
    // MA's sections (route indexes, [0] the route it is on), MA_W_R_T_SIG,
    // DEST_LOCO_SOS. maSent -1: nothing sent yet.
    QVector<int> maRoutes;
    qint64  maSent = -1;
    int     sos = 0;
    bool    shortRejected = false;     // session 446: it rejected a shortened MA (kept until it holds no longer)
};

// Session 441: where the loco is by an ARP or an LSRP (both carry
// ABS_LOCO_LOC, TRAIN_LENGTH, MOVEMENT_DIR, LAST_RFID_TAG): id, absLoc,
// trainLength, dir, lastTag. answer() notes every LSRP so.
void note(Loco *loco, const QHash<QString, qint64> &pkt);

struct Answer {
    bool    ok = false;
    QString why;                       // refused: the reason
    QHash<QString, qint64> header;     // the SLRP's flat fields (PKT_LENGTH is the builder's)
    QVector<Schema::SubEntry> subs;
    bool    fullProfile = false;
    QString note;                      // made: one line, what was sent
    QStringList checks;                // what the live state does not agree with
};

// Session 424: the MA's sections from route `route`, as answer() takes
// them: the route, then on from each exit signal (the route whose points
// are set, in the same direction), as many as the route's exit signal's
// aspect gives. Station Layout draws them; answer() sends them.
struct Sections {
    QVector<int>    routes;            // route indexes; [0] = `route`
    QVector<qint64> offsets;           // each one's start, from the start signal, m
    qint64  maEnd = 0;                 // MA_W_R_T_SIG
    int     aspect = 0, wanted = 1;    // the exit signal's aspect, the sections it gives
    QString endsShort;                 // fewer than wanted: no route on (said; the MA ends there)
    QString broken;                    // a route that does not start where the one before ends (refused)
};
Sections sectionsFrom(const Station &s, const State &st, int route);

// The SLRP answering `lsrp` (its fields by name). `loco` is read and
// updated only when the answer is made, but for where it is (id, absLoc,
// trainLength, dir: from every LSRP). `enc` gives the field widths
// every value is checked against.
// Session 427: `others`, the other locos the station has heard. One ahead
// on the MA's routes, the same way, cuts the MA to its ABS_LOCO_LOC less its
// TRAIN_LENGTH less kRearMargin (the profile stays the sections'). A loco
// reporting EMERGENCY_STATUS 5 is answered DEST_LOCO_SOS 5 (Rear-end).
// Both as station 527 answered loco 1 behind loco 2 in
// replay/loco_{1,2}_1_27062026_151052.cap.
// Session 445: the other SoS kinds. A loco coming the other way on the
// same line, ahead within the MA: DEST_LOCO_SOS 6 (Head-on), the MA cut
// kRearMargin short of its front; one reporting EMERGENCY_STATUS 4 is
// answered 6 too. Past the foot of a Red signal in LOCO_MODE 10 (Shunting):
// 7 (Shunt violation) where SPAD would be 4. A tag the layout does not hold,
// on no route, read by a loco the station has placed: 1 (Foreign RFID).
// The operator's (State::sos) over all of these; the MA is kept (the SoS is
// what brakes), but for SPAD / Shunt violation.
// Session 446, with State::askShorten: an MA shorter than the one the loco holds on the same route
// (a signal gone back; not a loco ahead, not an SoS) is first asked for:
// REQ_SHORTEN_MA 1, NEW_MA the new one, MA_W_R_T_SIG the one it holds. The
// loco's NEW_MA_REPLY 1 (granted): the new MA sent; 2 (rejected): the one it
// holds kept, not asked again while the MA would be shorter.
constexpr qint64 kRearMargin = 100;
Answer answer(const Station &s, const State &st, Loco *loco, const QHash<QString, qint64> &lsrp,
              const Schema::Encoder &enc, const QList<Loco> &others = {});

}  // namespace StationKavach

#endif  // STATIONKAVACH_H
