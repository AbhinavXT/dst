#include "testutil.h"

#include "stationlayout.h"
#include "stationlayoutwindow.h"
#include "theme.h"
#include "uistyle.h"

#include <cmath>

// =============================================================================
//  Session 218 — Station Layout arranged as the RDSO Tag-TIN layouts are: DN
//  and UP main in the middle, loops around them, turnout tags on the points'
//  diagonals, the yard wider than the block sections. Real station files:
//  the built-in Lingampalli (527) and Gullaguda (521).
// =============================================================================

TEST_SUITE(session218)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    StationLayoutWindow w;
    w.resize(1180, 720);
    w.show();
    CHECK(w.openFile(QStringLiteral(":/station_layouts/station_lingampalli.xlsx")), "Lingampalli opens");
    StationLayoutCanvas *c = w.canvas();
    c->grab();

    // Lanes: DL3 DL2 DL1 above DM, UM, UL1 below (the file lists them so too,
    // but the default station.xlsx lists DM UM DL3 DL2 DL1 UL1).
    auto yOf = [&](const QString &tag) { return c->tagRect(tag).center().y(); };
    CHECK(yOf(QStringLiteral("46")) < yOf(QStringLiteral("32")) && yOf(QStringLiteral("32")) < yOf(QStringLiteral("23"))
          && yOf(QStringLiteral("23")) < yOf(QStringLiteral("566")) && yOf(QStringLiteral("566")) < yOf(QStringLiteral("423"))
          && yOf(QStringLiteral("423")) < yOf(QStringLiteral("19")),
          "DL3, DL2, DL1 above DN main; UP main, then UL1 below");

    // Tag 60 is on no line; point P30 (DM 160322 to DL1 160439) names it.
    const double y60 = yOf(QStringLiteral("60"));
    CHECK(y60 > yOf(QStringLiteral("23")) && y60 < yOf(QStringLiteral("566")), "turnout tag 60 drawn on P30, between DL1 and DM");
    bool onPoint = false;
    for (const StationLayoutCanvas::Hit &h : c->hits())
        if (h.tag == QLatin1String("60")) onPoint = h.text.contains(QLatin1String("point P30"));
    CHECK(onPoint, "its tooltip names the point");

    // The yard (points 160154..161720 m) gets most of the width.
    const double yard = c->xOf(161720) - c->xOf(160154), all = c->xOf(170750) - c->xOf(153412);
    CHECK(yard / all > 0.4, "the 1.6 km yard takes over 40 % of a 17 km drawing");
    bool roundTrip = true;
    for (const qint64 m : { 153412, 158000, 160154, 160900, 161720, 165000, 170750 })
        roundTrip = roundTrip && std::llabs(c->locAt(c->xOf(m)) - m) <= 20;
    CHECK(roundTrip, "screen x and location convert both ways, in and outside the yard");

    // Gullaguda: two loops, no tag on "(no line)".
    CHECK(w.openFile(QStringLiteral(":/station_layouts/station_gullaguda.xlsx")), "Gullaguda opens");
    c->grab();
    CHECK(yOf(QStringLiteral("530")) < yOf(QStringLiteral("664")) && yOf(QStringLiteral("623")) < yOf(QStringLiteral("537")),
          "LOOP DN above DN main, LOOP UP below UP main");
    bool noOrphan = true;
    for (const StationLayoutCanvas::Hit &h : c->hits())
        if (!h.tag.isEmpty() && h.text.contains(QLatin1String("(no line)"))) noOrphan = false;
    CHECK(noOrphan, "every turnout tag is on its point, none on (no line)");
}
