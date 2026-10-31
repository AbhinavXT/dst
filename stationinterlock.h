#ifndef STATIONINTERLOCK_H
#define STATIONINTERLOCK_H
// =====================================================================
//  stationinterlock.{h,cpp} -- session 441 on: the station's own
//  signalling, worked from where the locos say they are.
//
//  THE TRACK (Track): the layout's track circuits (StationLayout::circuits:
//  name, line, from startx to endx) and the line each tag number lies on
//  (the lines sheet).
//
//  OCCUPANCY (occupancy(), session 441): a loco occupies every track
//  circuit of its line that its train covers: from ABS_LOCO_LOC back
//  TRAIN_LENGTH against MOVEMENT_DIR (Nominal: rising km), as its last ARP
//  or LSRP says. Its line is the line of the tag it last read
//  (LAST_RFID_TAG). A loco whose tag lies on no line, or that has not said
//  where it is (ABS_LOCO_LOC 0, direction unidentified) occupies nothing,
//  and that is said. A train across a point onto another line is counted
//  on its tag's line only.
//
//  LOCKING AND RELEASE (session 442), while State::interlocking:
//   - a route set (setRoute) locks its points: a locked point is not thrown
//     (pointLocked says why), the route not cancelled once a train is in it;
//   - a train in it (its first track circuit occupied) puts its entry
//     signal back to Red; a signal cleared by
//     hand, with no route set, goes back to Red so too (the route from it
//     whose points are set);
//   - when a train has been in it and its circuits are all clear again, the
//     route is released: its points free, nothing set from that signal.
//  A route without track circuits is never seen entered: its signal stays
//  the operator's.
//
//  CONFLICTS (session 444), while State::interlocking: a route is not set
//  while a circuit of it is occupied or a route set from another signal runs
//  over one of its circuits (said why). A signal off whose route is not
//  free (a point lying the wrong way, a circuit occupied: by hand, or a
//  route set and no train in it yet) is held at Red.
//
//  AUTOMATIC SIGNALS (session 443), while State::interlocking: a signal
//  every route from which is an Autoblock section (TRAIN_SECTION_TYPE 2,
//  with track circuits) and has no route set from it works itself: Red
//  while a circuit of the section ahead is occupied, else one step up from
//  the signal at its end: Red (or calling-on, stop board) -> Yellow,
//  Yellow -> Double Yellow, Double Yellow or Green -> Green.
//
//  TRAIN LENGTH (session 448): the times a loco's front and then its rear
//  cross a track-circuit joint of its line (measure()), each interpolated
//  between the frames either side (FRAME_NUM, and ABS_LOCO_LOC of the
//  frame's first report: ARPs repeat a frame number as it moves on): the
//  distance the loco runs between them is its train length. Sent as
//  TRN_LEN_INFO Start / End (StationKavach::answer, State::trainLength).
//
//  Pure values; Packet Maker keeps the locos and the State.
// =====================================================================
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

#include "stationkavach.h"
#include "stationlayout.h"

namespace StationInterlock {

struct Track {
    QVector<StationLayout::Circuit> circuits;
    QHash<int, QString> lineOfTag;     // tag number -> line (main tags; first line wins)
    bool isEmpty() const { return circuits.isEmpty(); }
};
Track trackOf(const StationLayout::Layout &l);

struct Occupancy {
    QHash<QString, int> by;            // track circuit -> the loco occupying it
    QStringList notes;                 // a loco that occupies nothing, and why
};
// Where each loco is by its last ARP / LSRP (StationKavach::note).
Occupancy occupancy(const Track &t, const QList<StationKavach::Loco> &locos);

// Its points thrown, its entry signal off (indicator: the entry signal's
// route indicator position, 0 none); a route set from the same signal, or
// through its points the other way, gives way. Empty, or why not.
QString setRoute(const StationKavach::Station &s, StationKavach::State *st, int route, int indicator = 0);
// Its entry signal to Red, nothing set from it. Refused while a train is in it.
QString cancelRoute(const StationKavach::Station &s, StationKavach::State *st, const QString &entry);
// Why point `point` cannot be thrown now (a set route holds it); empty: free.
QString pointLocked(const StationKavach::Station &s, const StationKavach::State &st, const QString &point);
// Session 448: after StationKavach::note: the Start (its front over a joint
// of its line) or the End (its rear over that joint) found since its report
// before, kept in the loco to be sent. What was found, or empty.
QString measure(const Track &t, StationKavach::Loco *loco);
// A time in frames (FRAME_NUM, fractional) as a frame and a 10 ms offset.
void frameAndOffset(double t, int *frame, int *offset);

// The interlocking's step after the occupancy changed (or an aspect, a
// route): what it did, one line each.
QStringList work(const StationKavach::Station &s, StationKavach::State *st);

}  // namespace StationInterlock

#endif  // STATIONINTERLOCK_H
