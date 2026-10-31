#include "testutil.h"

#include "stationkavach.h"
#include "stationlayout.h"
#include "stationlayoutwindow.h"

#include <QAction>
#include <QLabel>
#include <QMenu>

// =============================================================================
//  Session 435 — the station's signals, route indicators and points changed
//  on the Station Layout drawing. Right-click a signal: its aspect (Red,
//  Yellow, Double Yellow, Green, calling-on, with a route indicator
//  position), Set route ▸ the routes from it (their points thrown, the entry
//  signal cleared: Yellow + its indicator's position on a diverging route),
//  Cancel route. Double-click a point to throw it, right-click for Normal /
//  Reverse. Live (Packet Maker answering) each change is sent back
//  (liveStateEdited); otherwise a what-if on the drawing.
//  On station_527.xml (real) with a KAVACH block of two routes from S48:
//  the straight one (19 Normal) and one through 19 and P18 Reverse.
// =============================================================================

namespace {

QAction *actionIn(QMenu *m, const QString &start)
{
    for (QAction *a : m->actions()) {
        if (a->text().startsWith(start)) return a;
        if (a->menu())
            if (QAction *in = actionIn(a->menu(), start)) return in;
    }
    return nullptr;
}

int indexOf(const QVector<StationLayout::Signal> &l, const QString &name)
{
    for (int i = 0; i < l.size(); ++i) if (l.at(i).name == name) return i;
    return -1;
}

int pointIndex(const StationLayout::Layout &l, const QString &name)
{
    for (int i = 0; i < l.points.size(); ++i) if (l.points.at(i).name == name) return i;
    return -1;
}

StationKavach::Station twoRoutes()
{
    StationKavach::Station s;
    s.stationId = 527;
    s.signalList << StationKavach::Signal{ QStringLiteral("S48"), 12, 0, 1 } << StationKavach::Signal{ QStringLiteral("S43"), 18, 0, 1 };
    StationKavach::Route main;
    main.name = QStringLiteral("S48-S43 DN Main");
    main.entry = QStringLiteral("S48");
    main.exit = QStringLiteral("S43");
    main.pointsNormal = QStringList{ QStringLiteral("19") };
    main.tags << StationKavach::RouteTag{ 0, 12, 0 } << StationKavach::RouteTag{ 534, 18, 0 };
    StationKavach::Route loop = main;
    loop.name = QStringLiteral("S48-UP Loop");
    loop.pointsNormal.clear();
    loop.pointsReverse = QStringList{ QStringLiteral("19"), QStringLiteral("P18") };
    s.routes << main << loop;
    return s;
}

} // namespace

TEST_SUITE(session435)
{
    StationLayoutWindow w;
    CHECK(w.openFile(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_527.xml")), "station_527.xml");
    StationLayout::Layout l = w.station();
    l.kavach = StationKavach::toJson(twoRoutes());
    w.setStation(l);
    const int s48 = indexOf(w.station().signalList, QStringLiteral("S48"));
    const int p19 = pointIndex(w.station(), QStringLiteral("P19")), p18 = pointIndex(w.station(), QStringLiteral("P18"));
    CHECK(s48 >= 0 && p19 >= 0 && p18 >= 0, "S48, P19, P18 in the layout");
    CHECK(!w.liveEditing(), "nothing changed yet: no state on the drawing");

    // ---- what-if: set the diverging route ----
    QMenu *m = w.signalMenuFor(s48);
    QAction *loop = m ? actionIn(m, QStringLiteral("S48-UP Loop")) : nullptr;
    CHECK(loop && actionIn(m, QStringLiteral("S48-S43 DN Main")) && loop->text().contains(QStringLiteral("19, P18 Reverse")),
          "S48's menu: Set route ▸ its two routes, the loop saying which points it reverses");
    if (loop) loop->trigger();
    delete m;
    CHECK(w.liveEditing() && !w.liveIsLive(), "a what-if (not live)");
    CHECK(w.liveStationState().reverse == (QSet<QString>{ QStringLiteral("19"), QStringLiteral("P18") }),
          "its points Reverse, by the KAVACH data's names");
    StationLayoutCanvas::LiveView v = w.canvas()->liveView();
    CHECK(v.pointsShown && v.reversePoints == (QSet<QString>{ QStringLiteral("P19"), QStringLiteral("P18") }),
          "drawn Reverse: the layout's P19 (\"19\" in the data) and P18");
    CHECK(v.routesSet.size() == 1 && v.routesSet.first().route == QStringLiteral("S48-UP Loop") && !v.routesSet.first().tags.isEmpty(),
          "the route drawn along its tags");
    CHECK(w.liveStationState().aspectOf(w.liveStation(), QStringLiteral("S48")) == 2, "S48 off: Yellow (no route indicator yet)");
    CHECK(w.liveState()->isVisible() || !w.liveState()->text().isEmpty(), "the line above the drawing says so");
    CHECK(w.liveState()->text().contains(QStringLiteral("what-if")) && w.liveState()->text().contains(QStringLiteral("S48-UP Loop")),
          "a what-if, the route set");

    // ---- its route indicator ----
    CHECK(w.setSignalIndicator(s48, 4) && w.station().signalList.at(s48).indicator == 4, "S48's route indicator at position 4");
    CHECK(w.setLiveRoute(1) && w.liveStationState().aspectOf(w.liveStation(), QStringLiteral("S48")) == 6,
          "the loop again: Yellow + position 4 (sigAspect 6)");
    w.resize(1200, 800);
    w.show();
    w.canvas()->grab();
    bool lit = false;
    for (const StationLayoutCanvas::Hit &h : w.canvas()->hits())
        lit = lit || (h.signal == s48 && h.text.contains(QStringLiteral("KAVACH, what-if: Yellow (route 6)")));
    CHECK(lit, "drawn lit, and said: what-if, Yellow (route 6)");
    const QByteArray xml = StationLayout::toXml(w.station(), QStringLiteral("x"));
    CHECK(xml.contains("signal=\"S48\" sig_id=\"3\" StationId=\"527\" indicator=\"4\"/>"), "the indicator saved in the XML");

    // ---- points thrown ----
    emit w.canvas()->pointThrowRequested(p19);
    CHECK(!w.liveStationState().reverse.contains(QStringLiteral("19")) && !w.canvas()->liveView().reversePoints.contains(QStringLiteral("P19")),
          "double-click P19: Normal");
    CHECK(w.canvas()->liveView().routesSet.isEmpty(), "the loop is no longer set (P19 the other way)");
    m = w.pointMenuFor(p18);
    QAction *normal = m ? actionIn(m, QStringLiteral("Normal")) : nullptr;
    CHECK(m && actionIn(m, QStringLiteral("Reverse"))->isChecked() && normal, "P18's menu: Reverse ticked");
    if (normal) normal->trigger();
    delete m;
    CHECK(w.liveStationState().reverse.isEmpty(), "P18 Normal too");
    CHECK(w.setLiveRoute(0) && w.canvas()->liveView().routesSet.size() == 1 && w.liveStationState().reverse.isEmpty(),
          "the main route: its point 19 stays Normal");
    m = w.signalMenuFor(s48);
    QAction *cancel = m ? actionIn(m, QStringLiteral("Cancel route")) : nullptr;
    if (cancel) cancel->trigger();
    delete m;
    CHECK(cancel && w.canvas()->liveView().routesSet.isEmpty() && w.liveStationState().aspectOf(w.liveStation(), QStringLiteral("S48")) == 1,
          "Cancel route: none set, S48 Red");
    m = w.signalMenuFor(s48);
    QAction *green = m ? actionIn(m, QStringLiteral("Green")) : nullptr;
    if (green) green->trigger();
    delete m;
    CHECK(w.liveStationState().aspectOf(w.liveStation(), QStringLiteral("S48")) == 11, "Aspect ▸ Green");

    // ---- live: each change sent back ----
    const StationKavach::Station ks = twoRoutes();
    QVector<StationKavach::State> sent;
    QObject::connect(&w, &StationLayoutWindow::liveStateEdited, &w, [&sent](const StationKavach::State &st) { sent << st; });
    w.showLiveStation(ks, StationKavach::State::initial(ks), {}, QStringLiteral("Packet Maker"));
    CHECK(w.liveIsLive() && w.canvas()->liveView().reversePoints.isEmpty() && sent.isEmpty(), "live: the station's state, nothing sent yet");
    CHECK(w.setLiveRoute(1) && sent.size() == 1 && sent.last().reverse.contains(QStringLiteral("P18"))
              && sent.last().aspect.value(QStringLiteral("S48")) == 6,
          "a route set: the state sent (points, S48 Yellow + position 4)");
    emit w.canvas()->pointThrowRequested(p18);
    CHECK(sent.size() == 2 && !sent.last().reverse.contains(QStringLiteral("P18")), "a point thrown: sent");
    CHECK(w.liveState()->text().startsWith(QStringLiteral("Live from Packet Maker")), "the line says live");
    w.clearLiveStation();
    CHECK(!w.liveEditing() && !w.canvas()->liveView().pointsShown, "no longer live: the drawing as the file");
    CHECK(!w.setLiveRoute(9), "no such route: refused");
}
