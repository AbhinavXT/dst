#include "testutil.h"

#include "capturedecoder.h"
#include "packetbuilder.h"
#include "stationkavach.h"
#include "stationkavacheditor.h"
#include "stationlayout.h"
#include "stationlayoutwindow.h"

#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>

// =============================================================================
//  Session 424 — the Stationary KAVACH routes drawn on the Station Layout:
//  while the KAVACH tab is shown, the route selected and the MA's sections
//  on from it along their tags, the KAVACH signals lit by their aspect, the
//  MA's end marked; the route's line says the MA. The engine's sections
//  (sectionsFrom) are the ones answer() sends. Where the layout's tags do
//  not lie as a route's linking distances say, Checks says so.
//  Station 527's data (tests/fixtures/station_kavach/section30_527.json) on
//  its own layout, the built-in Section_30.xlsx, whose tags are real; the
//  MA against the real LSRP 40380 of replay/2026-10-08/loco_1_1_08102026_105200.cap.
// =============================================================================

namespace {

const char *const kFixture = DL_SRC_DIR "/tests/fixtures/station_kavach/section30_527.json";

QString section30()
{
    for (const QString &p : StationLayout::library())
        if (p.endsWith(QLatin1String("/Section_30.xlsx"))) return p;
    return QString();
}

QHash<QString, qint64> realLsrp(qint64 fn)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_105200.cap"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    for (const QString &l : QString::fromLatin1(f.readAll()).split(QLatin1Char('\n'))) {
        if (!l.startsWith(QLatin1String("@lsrp_"))) continue;
        QHash<QString, qint64> raw;
        CaptureDecoder::describe(CaptureDecoder::parseLine(l.trimmed()), nullptr, 0, &raw);
        if (raw.value(QStringLiteral("FRAME_NUM")) == fn) return raw;
    }
    return {};
}

bool checksSay(StationLayoutWindow &w, const QString &text, int *count = nullptr)
{
    auto *list = qobject_cast<QListWidget *>(w.tabs()->widget(6));
    if (!list) return false;
    int n = 0;
    for (int i = 0; i < list->count(); ++i) if (list->item(i)->text().contains(text)) ++n;
    if (count) *count = n;
    return n > 0;
}

bool hitSays(StationLayoutCanvas *c, const QString &text)
{
    for (const StationLayoutCanvas::Hit &h : c->hits()) if (h.text.contains(text)) return true;
    return false;
}

QStringList sectionRoutes(const StationLayoutCanvas::KavachView &v)
{
    QStringList out;
    for (const auto &k : v.sections) out << k.route;
    return out;
}

} // namespace

TEST_SUITE(session424)
{
    PacketBuilder b;
    StationLayout::Layout fixture;
    StationKavach::Station st;
    CHECK(StationLayout::load(QString::fromLatin1(kFixture), &fixture) && StationKavach::fromJson(fixture.kavach, &st),
          "the station 527 fixture loads");
    const QString lib = section30();
    StationLayout::Layout lay;
    CHECK(!lib.isEmpty() && StationLayout::load(lib, &lay), "the built-in Section_30.xlsx (station 527) loads");
    lay.kavach = fixture.kavach;

    // ---- the engine: the sections answer() sends ----
    {
        const StationKavach::Sections sx = StationKavach::sectionsFrom(st, StationKavach::State::initial(st), 1);
        CHECK(sx.routes == (QVector<int>{ 1, 2 }) && sx.offsets == (QVector<qint64>{ 0, 1116 }) && sx.maEnd == 1956
                  && sx.aspect == 2 && sx.wanted == 2 && sx.endsShort.isEmpty() && sx.broken.isEmpty(),
              "from S43-A900, A900 Yellow: S43-A900 then A900-A904 (from 1116 m), MA 1956 m");
        const QHash<QString, qint64> lsrp = realLsrp(40380);
        StationKavach::Loco loco;
        const StationKavach::Answer a = StationKavach::answer(st, StationKavach::State::initial(st), &loco, lsrp, b.encoder());
        qint64 ma = -1;
        for (const Schema::SubEntry &se : a.subs) if (se.type == 0) ma = se.values.value(QStringLiteral("MA_W_R_T_SIG"));
        CHECK(a.ok && loco.route == 1 && ma == sx.maEnd, "the real LSRP 40380 is answered from that route with that MA");

        StationKavach::State red = StationKavach::State::initial(st);
        red.aspect.insert(QStringLiteral("A900"), 1);
        const StationKavach::Sections r = StationKavach::sectionsFrom(st, red, 1);
        CHECK(r.routes == (QVector<int>{ 1 }) && r.maEnd == 1116, "A900 Red: one section, MA 1116 m");
        StationKavach::State green = StationKavach::State::initial(st);
        green.aspect.insert(QStringLiteral("A900"), 11);
        const StationKavach::Sections g = StationKavach::sectionsFrom(st, green, 1);
        CHECK(g.routes.size() == 2 && g.wanted == 4 && g.maEnd == 1956
                  && g.endsShort == QStringLiteral("A900 Green wants 4 sections; no route on from A904: MA ends there"),
              "A900 Green: 4 wanted, 2 known; said, MA where they end");
        StationKavach::Station bad = st;
        bad.routes[2].tags[0].tag = 901;
        const StationKavach::Sections k = StationKavach::sectionsFrom(bad, StationKavach::State::initial(bad), 1);
        CHECK(k.broken.contains(QStringLiteral("route A900-A904 DN Auto starts on tag 901, not on tag 900")),
              "a route not starting where the one before ends: named");
        CHECK(StationKavach::sectionsFrom(st, StationKavach::State::initial(st), 9).routes.isEmpty(), "no such route: no sections");
        CHECK(StationKavach::aspectName(10) == QStringLiteral("Double Yellow") && StationKavach::aspectName(15) == QStringLiteral("Calling-on"),
              "aspect names");
    }

    // ---- the engine: the layout's tags against the linking distances ----
    {
        // Section_30.xlsx's tags (decoded): three 5 km blocks, 18 … 902 in each, 904 only in the first.
        const QHash<int, QVector<qint64>> s30{ { 12, { 156330, 161330, 166330 } }, { 14, { 156605, 161605, 166605 } },
                                               { 16, { 156742, 161742, 166742 } }, { 18, { 156864, 161864, 166864 } },
                                               { 906, { 157064, 162064, 167064 } }, { 898, { 157720, 162720, 167720 } },
                                               { 900, { 157980, 162980, 167980 } }, { 902, { 158620, 163620, 168620 } },
                                               { 904, { 158820 } } };
        CHECK(StationKavach::locationChecks(st, s30).isEmpty(),
              "Section_30: every step of the three routes agrees with some block's tags (904: the first block's)");
        const QHash<int, QVector<qint64>> one{ { 900, { 167980 } }, { 902, { 168620 } }, { 904, { 158820 } } };
        CHECK(StationKavach::locationChecks(st, one)
                  == (QStringList{ "route A900-A904 DN Auto: tag 904 is 200 m on from tag 902; the layout's tags lie 9800 m back" }),
              "only the third block's 902: tag 904 9800 m back is said, the one line");
        const QHash<int, QVector<qint64>> two{ { 902, { 163620, 168620 } }, { 904, { 158820 } } };
        CHECK(StationKavach::locationChecks(st, two)
                  == (QStringList{ "route A900-A904 DN Auto: tag 904 is 200 m on from tag 902; none of the layout's 1 tag(s) 904 "
                                   "lies so from its 2 tag(s) 902" }),
              "a number listed twice, neither agreeing: said with the counts");
        StationKavach::Station rev = st;
        rev.routes[1].pktDir = 2;
        CHECK(StationKavach::locationChecks(rev, s30).size() == 3, "S43-A900 run Reverse over the same tags: each of its 3 steps is said");
        CHECK(StationKavach::locationChecks(st, {}).isEmpty(), "no locations: nothing said");
    }

    // ---- the view of a route on the layout ----
    {
        QStringList missing;
        const StationLayoutCanvas::KavachView v = kavachViewOf(lay, st, 1, &missing);
        CHECK(missing.isEmpty(), "every tag and foot tag of the 527 data is one of Section_30's tags");
        CHECK(sectionRoutes(v) == (QStringList{ "S43-A900 DN Auto", "A900-A904 DN Auto" }) && v.maText == QStringLiteral("MA 1956 m"),
              "S43-A900 selected: it and A900-A904 drawn, MA 1956 m");
        CHECK(v.sections.size() == 2 && v.sections[0].tags == (QStringList{ "18", "906", "898", "900" })
                  && v.sections[1].tags == (QStringList{ "900", "902", "904" }) && v.sections[1].from == 1116 && v.sections[1].to == 1956,
              "each section by the layout's tag names, placed from the start signal");
        bool a900 = false;
        for (const auto &g : v.signalList) a900 = a900 || (g.name == QLatin1String("A900") && g.footTag == QLatin1String("900") && g.aspect == 2);
        CHECK(v.signalList.size() == 4 && a900, "the 4 KAVACH signals at their foot tags; A900 Yellow");
        QStringList gone;
        const StationLayoutCanvas::KavachView none = kavachViewOf(StationLayout::Layout(), st, 1, &gone);
        CHECK(none.sections.size() == 2 && none.sections[0].tags.isEmpty() && none.signalList.isEmpty()
                  && gone.contains(QStringLiteral("906")) && gone.contains(QStringLiteral("12")),
              "a layout without those tags: nothing to draw, the tags named");
        CHECK(kavachViewOf(lay, st, -1).sections.isEmpty() && kavachViewOf(lay, st, -1).signalList.size() == 4,
              "no route selected: the signals only");
    }

    // ---- the window ----
    StationLayoutWindow w;
    w.resize(1400, 900);
    w.setStation(lay);
    StationKavachEditor *ed = w.kavachEditor();
    StationLayoutCanvas *c = w.canvas();
    CHECK(c->kavachView().isEmpty(), "on the Tags tab nothing of KAVACH is drawn");
    CHECK(checksSay(w, QStringLiteral("tag 902 is listed more than once")) && !checksSay(w, QStringLiteral(" m on from tag ")),
          "Section_30 in Station Layout: its repeated tags said as before; nothing about the routes' distances");
    {
        StationLayout::Layout moved = lay;
        for (StationLayout::Tag &t : moved.tags)
            if (t.name == QLatin1String("904")) StationLayout::moveTag(&t, 158850);
        w.setStation(moved);
        CHECK(checksSay(w, QStringLiteral("KAVACH: route A900-A904 DN Auto: tag 904 is 200 m on from tag 902; none of the layout's "
                                          "1 tag(s) 904 lies so from its 3 tag(s) 902")),
              "tag 904 moved 30 m: Checks says the route's distance and the layout's tags disagree");
        w.setStation(lay);
    }

    w.tabs()->setCurrentWidget(ed);
    ed->selectRoute(1);
    c->grab();
    CHECK(sectionRoutes(c->kavachView()) == (QStringList{ "S43-A900 DN Auto", "A900-A904 DN Auto" }),
          "the KAVACH tab, S43-A900 selected: it and the section on are drawn");
    const QPolygonF p0 = c->kavachPath(0);
    CHECK(p0.size() == 4 && qAbs(p0[0].x() - c->xOf(166864)) < 0.5 && qAbs(p0[3].x() - c->xOf(167980)) < 0.5,
          "along tags 18 … 900, at their own locations");
    CHECK(p0.size() == 4 && qAbs(p0[1].x() - c->tagRect(QStringLiteral("906")).center().x()) < 0.5,
          "through tag 906 where it is drawn");
    const QRectF end = c->kavachMaEndRect();
    CHECK(!end.isNull() && end.contains(QPointF(c->xOf(158820), end.center().y())), "the MA's end marked at tag 904");
    CHECK(hitSays(c, QStringLiteral("tag(s) 18, 906, 898, 900 listed more than once in the layout: each drawn at one of its places")),
          "Section_30 lists those tags three times: hovering the route says the drawing shows one of each");
    CHECK(hitSays(c, QStringLiteral("KAVACH route S43-A900 DN Auto (selected): 0 to 1116 m"))
              && hitSays(c, QStringLiteral("KAVACH route A900-A904 DN Auto: MA section 2 of 2, 1116 to 1956 m"))
              && hitSays(c, QStringLiteral("End of the MA: MA 1956 m")),
          "hovering says which route, which section, where the MA ends");
    CHECK(hitSays(c, QStringLiteral("Signal A900 (KAVACH data; not in the signals sheet) at foot tag 900\nKAVACH: Yellow")),
          "A900, not in Section_30's signals sheet, drawn at tag 900, Yellow");
    CHECK(hitSays(c, QStringLiteral("at foot tag 18\nKAVACH: Yellow")), "S43 from the signals sheet: its KAVACH aspect said");
    auto *label = ed->findChild<QLabel *>(QStringLiteral("skeRouteLabel"));
    CHECK(label && label->text() == QStringLiteral("Route S43-A900 DN Auto: S43 → A900, 1116 m. A900 Yellow: 2 sections → MA 1956 m"),
          "the route's line says the MA");

    // An aspect changed: the drawing and the line follow; Undo puts them back.
    auto *a900 = qobject_cast<QComboBox *>(ed->signalTable()->cellWidget(2, 3));
    CHECK(a900 && a900->currentData().toInt() == 2, "A900 starts Yellow");
    if (a900) a900->setCurrentIndex(a900->findData(1));
    c->grab();
    CHECK(ed->currentRoute() == 1 && sectionRoutes(c->kavachView()) == (QStringList{ "S43-A900 DN Auto" })
              && c->kavachView().maText == QStringLiteral("MA 1116 m"),
          "A900 Red: one section drawn, MA 1116 m");
    CHECK(!c->kavachMaEndRect().isNull() && c->kavachMaEndRect().contains(QPointF(c->xOf(167980), c->kavachMaEndRect().center().y())),
          "the MA's end moves to A900's foot tag");
    label = ed->findChild<QLabel *>(QStringLiteral("skeRouteLabel"));
    CHECK(label && label->text().endsWith(QStringLiteral("A900 Red: 1 section → MA 1116 m")), "the line too");
    w.undo();
    c->grab();
    CHECK(c->kavachView().sections.size() == 2 && c->kavachView().maText == QStringLiteral("MA 1956 m"), "Undo: Yellow, two sections");

    ed->selectRoute(2);
    label = ed->findChild<QLabel *>(QStringLiteral("skeRouteLabel"));
    CHECK(sectionRoutes(c->kavachView()) == (QStringList{ "A900-A904 DN Auto" })
              && label && label->text().endsWith(QStringLiteral("A904 Yellow: 1 section → MA 840 m (2 wanted; no route on from A904)")),
          "A900-A904: A904 Yellow wants 2, there is none on: one drawn, and said");
    ed->selectRoute(0);
    CHECK(sectionRoutes(c->kavachView()) == (QStringList{ "S48-S43 DN Main", "S43-A900 DN Auto" })
              && c->kavachView().maText == QStringLiteral("MA 1650 m"),
          "S48-S43, S43 Yellow: MA 1650 m, as station 527 sent it");

    QTemporaryDir dir;
    const QString png = dir.filePath(QStringLiteral("drawing.png"));
    CHECK(w.exportDrawing(png) && QFileInfo(png).size() > 0, "Export ▸ PNG with the route on it");

    w.tabs()->setCurrentIndex(0);
    CHECK(c->kavachView().isEmpty() && c->kavachMaEndRect().isNull(), "another tab: the drawing is the layout's alone");
    w.tabs()->setCurrentWidget(ed);
    CHECK(sectionRoutes(c->kavachView()).size() == 2, "back on KAVACH: drawn again");
    StationLayout::Layout noK = w.station();
    noK.kavach = QJsonObject();
    w.setStation(noK);
    CHECK(c->kavachView().isEmpty(), "a layout without KAVACH data: nothing drawn");
}
