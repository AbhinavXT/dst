#include "testutil.h"

#include "stationkavach.h"
#include "stationlayout.h"
#include "stationlayoutwindow.h"

// =============================================================================
//  Session 436 — the track circuits on the Station Layout drawing, from the
//  circuits sheet (the station XML's <circuits>): their insulated joints
//  across the line at startx and endx, the section red when occupied.
//  Double-click one to occupy or clear it in the station's state (live:
//  sent, as a point or a signal is). station_527.xml: 31 circuits.
// =============================================================================

TEST_SUITE(session436)
{
    StationLayout::Layout l;
    QStringList notes;
    CHECK(StationLayout::load(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_527.xml"), &l), "station_527.xml");
    const QVector<StationLayout::Circuit> cs = StationLayout::circuits(l, &notes);
    CHECK(cs.size() == 31 && notes.isEmpty(), "its 31 track circuits (3 with a location only)");
    CHECK(cs.first().name == QStringLiteral("UST") && cs.first().line == QStringLiteral("DL3") && cs.first().from == 160535
              && cs.first().to == 161217 && cs.first().cid == 1,
          "UST (cid 1) on DL3, 160535 to 161217 m");
    bool ldm = false;
    for (const StationLayout::Circuit &c : cs) ldm = ldm || (c.name == QStringLiteral("L_DM") && c.from == 162210 && c.to == 162210);
    CHECK(ldm, "L_DM, a location and no ends in the file: at 162210 m");

    StationLayoutWindow w;
    CHECK(w.openFile(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_527.xml")), "opened");
    w.resize(1200, 800);
    w.show();
    StationLayoutCanvas *c = w.canvas();
    c->grab();
    int drawn = 0;
    QRectF ust;
    for (const StationLayoutCanvas::Hit &h : c->hits())
        if (!h.circuit.isEmpty()) {
            ++drawn;
            if (h.circuit == QStringLiteral("UST")) ust = h.rect;
        }
    CHECK(drawn == 31, "all 31 drawn (each line is in the layout)");
    CHECK(!ust.isNull() && qAbs(ust.left() - c->xOf(160535)) < 1 && qAbs(ust.right() - c->xOf(161217)) < 1,
          "UST from 160.535 to 161.217 km");

    emit c->circuitToggleRequested(QStringLiteral("UST"));
    CHECK(w.liveStationState().occupied.contains(QStringLiteral("UST")) && c->liveView().occupied.contains(QStringLiteral("UST")),
          "double-click: UST occupied (what-if)");
    c->grab();
    bool said = false;
    for (const StationLayoutCanvas::Hit &h : c->hits())
        said = said || (h.circuit == QStringLiteral("UST") && h.text.contains(QStringLiteral("Occupied by hand: double-click to change")));
    CHECK(said, "and said so");
    emit c->circuitToggleRequested(QStringLiteral("UST"));
    CHECK(!w.liveStationState().occupied.contains(QStringLiteral("UST")), "again: clear");

    StationKavach::Station ks;
    ks.stationId = 527;
    QVector<StationKavach::State> sent;
    QObject::connect(&w, &StationLayoutWindow::liveStateEdited, &w, [&sent](const StationKavach::State &st) { sent << st; });
    w.showLiveStation(ks, StationKavach::State::initial(ks), {}, QStringLiteral("Packet Maker"));
    emit c->circuitToggleRequested(QStringLiteral("DMT"));
    CHECK(sent.size() == 1 && sent.last().occupied == QSet<QString>{ QStringLiteral("DMT") }, "live: DMT occupied, sent to the station");
}
