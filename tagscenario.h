#ifndef TAGSCENARIO_H
#define TAGSCENARIO_H

// =============================================================================
//  Tag route scenarios (session 235) — the Tag Builder's Scenario menu.
//  -----------------------------------------------------------------------------
//  SIF section 14 runs one main / duplicate tag pair through a fixed list:
//  the duplicate missing, the main missing, both missing, the two
//  interchanged, both at one location, closer than 4 m, further than 4 m —
//  once with the duplicate in the nominal direction from the main (14.1-
//  14.10), once in the reverse (14.11-14.20). 14.25 jumbles tags. Each is a
//  copy of a working route with one change, made by hand today.
//
//  Here each change is one call on a route, a copy, never the original; a
//  location change re-encodes the tag and recomputes its CRC-30 as every
//  edit in the builder does. The whole section 14 set for a pair is one
//  call, named by clause.
//
//  Not made: 14.8 / 14.10 / 14.18 / 14.20 ("actual position" < or > 4 m).
//  They differ from 14.7 / 14.9 in where the tag is fixed on the track, not
//  in what it says, and a route holds what tags say.
// =============================================================================

#include "rfidtag.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace TagScenario {

enum Kind {
    AsIs,               // the route unchanged (the set's first clause)
    DuplicateMissing,
    MainMissing,
    BothMissing,
    Interchanged,       // the duplicate read before the main (rows swapped)
    SameLocation,       // the duplicate says the main's location
    GapUnder4,          // the duplicate says 3 m from the main, on its side
    GapOver4,           // 5 m
};
QString describe(Kind k);

struct Result {
    bool ok = false;
    RfidTag::Route route;
    QString why;        // when not ok
    QString change;     // "910D missing", "910D at 164613 m (3 m from 910)"
};

// `tag`: the main tag's name ("910") or its duplicate's ("910D").
Result apply(const RfidTag::Route &route, Kind kind, const QString &tag);

// The rows named, in reverse order where they stand (14.25's jumbled reads).
Result jumble(const RfidTag::Route &route, const QStringList &names);

// The section 14 set for the pair: (clause, kind), 14.1-14.9 when the
// duplicate says a location above the main's (nominal), 14.11-14.19 below.
struct Planned { QString clause; Kind kind; };
QVector<Planned> section14(const RfidTag::Route &route, const QString &tag, QString *why = nullptr);

} // namespace TagScenario

#endif // TAGSCENARIO_H
