#ifndef STATIONROUTES_H
#define STATIONROUTES_H
// =====================================================================
//  stationroutes.{h,cpp} -- session 460: a station's routes (Table of
//  Control rows) proposed from its layout.
//
//  FROM EACH SIGNAL, the way its foot tag's placement says it faces
//  (1 Nominal: locations rising; 2 Reverse; 6 both), along its line:
//    - a tag passed is the route's next tag; the first whose signal faces
//      the same way is the exit signal's foot tag, and the route ends;
//    - a point met facing (its other end further on) is a branch: on
//      along the line, the point Normal; or over it, the point Reverse,
//      the turnout's tags taken in order, on from its other end;
//    - a point met trailing, or a point both of whose ends are on the
//      line, is passed Normal.
//  A branch that runs off its line without meeting such a signal is
//  dropped; a signal none of whose branches reaches one is named.
//
//  EACH ROUTE: entry -> exit, its direction, its tags with the linking
//  distance from the tag before (their abs_loc apart, as HBL's ToC has
//  them) and dup_dir 1 when the duplicate lies 4 m back (station 527's
//  rule, kavach_sheets.py), DIST_DUP_TAG from the entry tag's pair, the
//  route's TIN (the entry foot tag's, the way it runs), the TINs it passes
//  ("N-50": each tag's TIN onward, as the firm's sheets and the drawing
//  name them), the track circuits on the stretches it runs, the points it
//  passes Normal and the points it takes Reverse. Section Station, FS.
//
//  AGAINST THE LINE: each line runs the way most of its signals face; a route
//  running more than 300 m against its lines (a crossover onto the other
//  main) is offered but not ticked. HBL's LPI loop routes cross the UP main
//  for 80 m, under it.
//
//  NOT PROPOSED: flank-protection points (beside the route, not on it:
//  the layout does not say which), track circuits a route wants occupied
//  (calling-on), OS speed, the profile. Seen on LPI (station_527.xml
//  against HBL's ToC): every route whose signals the layout has is
//  proposed with HBL's tags, distances and Reverse points; its Normal
//  points are HBL's less the flank ones.
// =====================================================================
#include "stationkavach.h"
#include "stationlayout.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace StationRoutes {

struct Proposal {
    enum Status { New, Same, Differs };
    StationKavach::Route route;
    Status status = New;
    int existing = -1;                 // the station's route it is the same as, or differs from
    QString line;                      // the line it starts on (the layout's name)
    // The metres it runs on lines whose signals mostly face the other way (a
    // crossover onto the other main), and the longest such line: offered,
    // not ticked, past kAgainstMetres.
    int against = 0;
    QString againstLine;
};
constexpr int kAgainstMetres = 300;

struct Result {
    QVector<Proposal> routes;
    QStringList notes;                 // signals that lead nowhere, foot tags that do not decode, ...
};

// The routes of `l`, compared with `existing` (its routes by entry, exit and
// Reverse points: the same tags and distances Same, else Differs).
Result propose(const StationLayout::Layout &l, const StationKavach::Station &existing);

// `picked` (indexes into r.routes) added to `s`: new routes appended; the
// signals they name that `s` lacks added at Red with their foot tags.
// Returns the number added.
int addTo(StationKavach::Station *s, const Result &r, const QVector<int> &picked);

QString statusName(Proposal::Status s);

}  // namespace StationRoutes

#endif // STATIONROUTES_H
