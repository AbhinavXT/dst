#include "testutil.h"

#include "locosimwindow.h"
#include "rfidtag.h"
#include "routestrip.h"
#include "settings.h"
#include "simpreview.h"
#include "stationlayoutwindow.h"

#include <QCoreApplication>
#include <QLabel>
#include <QMenu>
#include <QSettings>

#include <cmath>

// =============================================================================
//  Session 411 — a line of a Station Layout in the Loco Simulator: its tags in
//  the order a loco meets them, each edit on the layout followed (the loco
//  stays where it is), the loco and the tags it sent drawn on the layout.
//  The built-in station.xlsx (the Python tool's), its real tags.
// =============================================================================

namespace {

QStringList names411(const RfidTag::Route &r)
{
    QStringList n;
    for (const RfidTag::Tag &t : r.tags) n << t.name;
    return n;
}

}  // namespace

TEST_SUITE(session411)
{
    {
        QSettings settings(Settings::iniPath(), QSettings::IniFormat);
        settings.remove(QStringLiteral("locosim"));
    }
    Settings::setStationLayoutLastFile(QString());
    StationLayoutWindow w;
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    CHECK(w.reopenLast(), "the built-in layout opens");

    // The longest line, and another one.
    QString line, other;
    int most = 0;
    for (const StationLayout::Line &ln : w.station().lines)
        if (ln.tags.size() > most) { most = ln.tags.size(); line = ln.line; }
    for (const StationLayout::Line &ln : w.station().lines)
        if (ln.line != line && !ln.tags.isEmpty()) { other = ln.line; break; }
    CHECK(most > 3 && !other.isEmpty(), qPrintable(QStringLiteral("line %1 has %2 tags; another line, %3").arg(line).arg(most).arg(other)));
    const QHash<QString, qint64> loc = StationLayout::tagLocations(w.station());

    // ---- the route, in the order a loco meets the tags ----------------------------------------------
    const RfidTag::Route up = w.lineAsSimRoute(line, RfidTag::DirNominal);
    const RfidTag::Route down = w.lineAsSimRoute(line, RfidTag::DirReverse);
    QStringList asLine = names411(w.lineAsRoute(line)), upNames = names411(up), downNames = names411(down);
    bool rising = true, falling = true;
    for (int i = 1; i < up.tags.size(); ++i) rising = rising && loc.value(up.tags.at(i - 1).name) <= loc.value(up.tags.at(i).name);
    for (int i = 1; i < down.tags.size(); ++i) falling = falling && loc.value(down.tags.at(i - 1).name) >= loc.value(down.tags.at(i).name);
    std::sort(asLine.begin(), asLine.end());
    std::sort(upNames.begin(), upNames.end());
    std::sort(downNames.begin(), downNames.end());
    CHECK(up.dir == RfidTag::DirNominal && rising && upNames == asLine,
          "dir 1: the line's tags (with their duplicates), by location rising, the direction set");
    CHECK(down.dir == RfidTag::DirReverse && falling && downNames == asLine, "dir 2: the same tags, by location falling");
    CHECK(w.lineAsSimRoute(line, RfidTag::DirUnset).tags.isEmpty() && w.lineAsSimRoute(QStringLiteral("no-such-line"), 1).tags.isEmpty(),
          "no direction, or no such line: no route");

    // ---- run it ------------------------------------------------------------------------------------------
    CHECK(!w.runLineInSimulator(line, RfidTag::DirNominal), "no simulator to run it in: refused");
    LocoSimWindow sim(nullptr);
    sim.setAttribute(Qt::WA_DeleteOnClose, false);
    sim.setSendSink([](const QByteArray &) {});
    QSet<QString> sent;
    sim.setTagSink([&sent](int, const QByteArray &frame) { sent.insert(RfidTag::nameOf(frame.mid(4, 16))); });
    w.setLocoSimulator([&sim]() { return &sim; });
    CHECK(w.runLineInSimulator(line, RfidTag::DirNominal) == &sim && sim.followsLayout() && sim.layoutLine() == line
              && sim.routeCount() == 1,
          "the line runs in the simulator, which follows the layout");
    const QVector<SimPreview::Row> rows = SimPreview::rowsOf(up);
    CHECK(!rows.isEmpty() && sim.routePosition() == rows.first().absLoc && sim.routeTags().size() == up.tags.size(),
          "the loco at the first tag it meets");
    CHECK(w.simulatorLine() == line && !w.canvas()->simLocoRect().isNull() && w.simulatorState()->isVisibleTo(&w)
              && w.simulatorState()->text().contains(line),
          "the layout draws the loco on that line's lane, and says where it is");
    CHECK(std::fabs(w.canvas()->simLoco() - loc.value(up.tags.first().name)) < 1, "at its first tag's own location");

    // ---- drive it ----------------------------------------------------------------------------------------
    qint64 t = 1000;
    sim.setClock([&t]() { return t; });
    sim.motion().setSpeed(60);
    sim.start();
    for (int i = 0; i < 400 && sent.size() < 2; ++i) {
        t += 100;
        sim.tick();
    }
    const double at = sim.routePosition();
    CHECK(sent.size() >= 2 && at > rows.first().absLoc, qPrintable(QStringLiteral("driven to %1 m: %2 tags sent").arg(at).arg(sent.size())));
    bool marked = !sent.isEmpty();
    for (const QString &n : sent) marked = marked && (w.canvas()->simTags().value(n) & RouteStrip::SentOn1);
    CHECK(marked, "each tag sent is marked on the layout, as sent on reader 1");
    CHECK(w.canvas()->simLoco() > loc.value(up.tags.first().name), "the loco drawn further along");
    sim.stop();

    // ---- edits on the layout ------------------------------------------------------------------------------
    // A tag of the line ahead of the loco, moved 3 m on.
    QString ahead;
    for (const RfidTag::Tag &tag : up.tags)
        if (loc.value(tag.name) > at + 50 && !tag.name.endsWith(QLatin1Char('D'))) { ahead = tag.name; break; }
    CHECK(!ahead.isEmpty() && w.moveTag(ahead, loc.value(ahead) + 3), qPrintable(QStringLiteral("tag %1 moved 3 m on").arg(ahead)));
    QByteArray simBytes, layoutBytes;
    for (const auto &p : sim.routeTags()) if (p.first == ahead) simBytes = p.second;
    for (const StationLayout::Tag &tag : w.station().tags)
        if (tag.name == ahead) layoutBytes = RfidTag::fromPages(tag.pageX, tag.pageY);
    CHECK(!simBytes.isEmpty() && simBytes == layoutBytes, "the simulator's route has the moved tag's new bytes");
    CHECK(sim.routePosition() == at && sim.followsLayout(), "and the loco stays where it was");
    // An edit on another line: this line's route as it was.
    const QVector<QPair<QString, QByteArray>> before = sim.routeTags();
    QString otherTag;
    for (const StationLayout::Line &ln : w.station().lines) if (ln.line == other) otherTag = ln.tags.first();
    w.moveTag(otherTag, StationLayout::tagLocations(w.station()).value(otherTag) + 1);
    CHECK(sim.routeTags() == before && sim.routePosition() == at && w.simulatorLine() == line,
          "an edit on another line leaves the route, the loco and the drawing as they were");
    w.undo();
    w.undo();
    for (const auto &p : sim.routeTags()) if (p.first == ahead) simBytes = p.second;
    CHECK(simBytes == up.tags.value(names411(up).indexOf(ahead)).bytes, "Undo: the tag back where it was, in the simulator too");

    // ---- the menus -----------------------------------------------------------------------------------------
    QMenu *menu = w.contextMenuFor(QString(), line, loc.value(ahead), false);
    QMenu *sub = nullptr;
    for (QAction *a : menu->actions())
        if (a->menu() && a->text() == QStringLiteral("Line %1 in the Loco Simulator").arg(line)) sub = a->menu();
    CHECK(sub && sub->actions().size() == 2 && sub->actions().at(1)->text().startsWith(QLatin1String("Dir 2")),
          "right-click on a line: Line … in the Loco Simulator ▸ Dir 1 / Dir 2");
    if (sub) sub->actions().at(1)->trigger();
    CHECK(sim.followsLayout() && sim.routePosition() == SimPreview::rowsOf(down).first().absLoc,
          "Dir 2 from it: the loco at the line's other end");
    delete menu;

    // ---- leaving the layout -------------------------------------------------------------------------------
    StationLayout::Layout noLine = w.station();
    for (int i = 0; i < noLine.lines.size(); ++i)
        if (noLine.lines.at(i).line == line) noLine.lines.remove(i--);
    w.setStation(noLine);
    CHECK(!sim.followsLayout() && sim.routeCount() == 1 && w.simulatorLine().isEmpty() && !w.simulatorState()->isVisibleTo(&w),
          "the line gone from the layout: its last route stays in the simulator, the loco leaves the drawing");
    w.reopenLast();
    w.runLineInSimulator(line, RfidTag::DirNominal);
    sim.loadRouteFile(QStringLiteral(":/tag_scenarios/LocoTcasSimulator/Configuration1.xml"));
    CHECK(!sim.followsLayout() && w.simulatorLine().isEmpty(), "another route file opened: the layout no longer draws the loco");
    w.runLineInSimulator(line, RfidTag::DirNominal);
    {
        auto *gone = new StationLayoutWindow;
        gone->setStation(w.station());
        gone->setLocoSimulator([&sim]() { return &sim; });
        gone->runLineInSimulator(line, RfidTag::DirNominal);
        CHECK(sim.followsLayout() && w.simulatorLine().isEmpty() && gone->simulatorLine() == line,
              "a second layout takes it over: the first no longer draws the loco");
        delete gone;
        CHECK(!sim.followsLayout() && sim.routeCount() == 1, "that layout closed: the route stays");
    }
}
