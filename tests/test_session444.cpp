#include "testutil.h"

#include "stationinterlock.h"
#include "stationkavach.h"
#include "stationlayout.h"


#include <algorithm>

// =============================================================================
//  Session 444 — a route that is not free is not set, and a signal whose
//  route is not free is held at Red: a circuit of it occupied, a route set
//  from another signal over one of its circuits, a point lying the wrong
//  way. Said why, each time.
//  Station 527's KAVACH data (the session 422 fixture) given the circuits
//  of station_527.xml that lie on its routes (S43-A900: 43T … 43CT;
//  A900-A904: 1629T … 1629BT) and the June route S50-S48 over DMT; the real
//  ARPs and LSRPs of loco 1 (DN) and loco 2 (UP) in
//  replay/loco_{1,2}_1_27062026_170217.cap, in frame order.
// =============================================================================

namespace {

int routeNamed(const StationKavach::Station &s, const QString &name)
{
    for (int i = 0; i < s.routes.size(); ++i) if (s.routes[i].name == name) return i;
    return -1;
}

// Station 527's KAVACH data with station_527.xml's circuits on its routes, S50-S48 over DMT,
// and two routes from S48 (main: 19 Normal; loop: 19 and P18 Reverse) for the locking.
StationKavach::Station station527()
{
    StationLayout::Layout fixture;
    StationKavach::Station s;
    StationLayout::load(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_kavach/section30_527.json"), &fixture);
    StationKavach::fromJson(fixture.kavach, &s);
    for (StationKavach::Route &r : s.routes) {
        if (r.entry == QLatin1String("S43")) r.trackCircuits = QStringList{ "43T", "43AT", "43BT", "43CT" };
        if (r.entry == QLatin1String("A900")) r.trackCircuits = QStringList{ "1629T", "1629AT", "1629BT" };
    }
    StationKavach::Signal s50;
    s50.name = QStringLiteral("S50");
    s50.footTag = 2;
    s50.aspect = 1;
    s.signalList.prepend(s50);
    StationKavach::Route r;
    r.name = QStringLiteral("S50-S48 DN");
    r.entry = QStringLiteral("S50");
    r.exit = QStringLiteral("S48");
    r.tags << StationKavach::RouteTag{ 0, 2, 0 } << StationKavach::RouteTag{ 1650, 12, 0 };
    r.trackCircuits = QStringList{ QStringLiteral("DMT") };
    s.routes << r;
    return s;
}

} // namespace

TEST_SUITE(session444)
{
    StationKavach::Station s = station527();
    const int s43 = routeNamed(s, QStringLiteral("S43-A900 DN Auto")), s50 = routeNamed(s, QStringLiteral("S50-S48 DN")),
              main = routeNamed(s, QStringLiteral("S48-S43 DN Main"));
    CHECK(s43 >= 0 && s50 >= 0 && main >= 0, "the routes");
    if (s43 < 0 || s50 < 0 || main < 0) return;
    s.routes[main].pointsNormal = QStringList{ QStringLiteral("19") };
    s.routes[s50].trackCircuits = QStringList{ QStringLiteral("50AT"), QStringLiteral("DMT") };
    StationKavach::Route up;
    up.name = QStringLiteral("A900-S43 UP");
    up.entry = QStringLiteral("A900");
    up.exit = QStringLiteral("S43");
    up.pktDir = 2;
    up.tags << StationKavach::RouteTag{ 0, 900, 0 } << StationKavach::RouteTag{ 1116, 18, 0 };
    up.trackCircuits = QStringList{ QStringLiteral("43CT"), QStringLiteral("43BT") };
    s.routes << up;
    const int upi = s.routes.size() - 1;

    // ---- not set: occupied ----
    StationKavach::State st = StationKavach::State::initial(s);
    st.interlocking = true;
    st.occupiedBy.insert(QStringLiteral("43AT"), 2);
    CHECK(StationInterlock::setRoute(s, &st, s43) == QStringLiteral("route S43-A900 DN Auto not set: occupied: 43AT by loco 2")
              && !st.routes.contains(QStringLiteral("S43")),
          "S43-A900 with 43AT occupied by loco 2: not set, said");
    st.occupiedBy.clear();

    // ---- not set: a route set the other way over its circuits ----
    CHECK(StationInterlock::setRoute(s, &st, upi).isEmpty(), "A900-S43 UP set");
    CHECK(StationInterlock::setRoute(s, &st, s43) == QStringLiteral("route S43-A900 DN Auto not set: it conflicts with route A900-S43 UP (set), both over 43BT"),
          "S43-A900 DN against it: not set, said");
    CHECK(StationInterlock::cancelRoute(s, &st, QStringLiteral("A900")).isEmpty() && StationInterlock::setRoute(s, &st, s43).isEmpty(),
          "A900-S43 cancelled: S43-A900 set");

    // ---- held at Red: a route set, another train in it before ours ----
    StationKavach::State held = StationKavach::State::initial(s);
    held.interlocking = true;
    CHECK(StationInterlock::setRoute(s, &held, s50).isEmpty() && held.aspectOf(s, QStringLiteral("S50")) == 2, "S50-S48 (50AT, DMT) set: S50 Yellow");
    held.occupiedBy.insert(QStringLiteral("DMT"), 2);
    QStringList ev = StationInterlock::work(s, &held);
    CHECK(ev.contains(QStringLiteral("S50 held at Red: route S50-S48 DN: occupied: DMT by loco 2")) && held.aspectOf(s, QStringLiteral("S50")) == 1
              && !held.entered.contains(QStringLiteral("S50")),
          "loco 2 in DMT, 50AT clear: S50 held at Red, the route not entered");

    // ---- held at Red: set by hand, a point the wrong way ----
    held.aspect.insert(QStringLiteral("S48"), 11);
    held.reverse.insert(QStringLiteral("19"));
    ev = StationInterlock::work(s, &held);
    CHECK(ev.contains(QStringLiteral("S48 held at Red: route S48-S43 DN Main: point 19 lies Reverse, the route needs it Normal"))
              && held.aspectOf(s, QStringLiteral("S48")) == 1,
          "S48 Green by hand, 19 Reverse: held at Red, said");
    held.reverse.remove(QStringLiteral("19"));
    held.aspect.insert(QStringLiteral("S48"), 11);
    CHECK(StationInterlock::work(s, &held).filter(QStringLiteral("S48")).isEmpty() && held.aspectOf(s, QStringLiteral("S48")) == 11,
          "19 Normal again: S48 Green stays");

    // ---- the answer still says it; off, nothing is held ----
    held.interlocking = false;
    held.aspect.insert(QStringLiteral("S48"), 11);
    held.reverse.insert(QStringLiteral("19"));
    CHECK(StationInterlock::work(s, &held).isEmpty() && held.aspectOf(s, QStringLiteral("S48")) == 11, "interlocking off: the operator's Green kept");
}
