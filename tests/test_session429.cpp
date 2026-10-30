#include "testutil.h"

#include "rfidtag.h"
#include "stationkavach.h"
#include "stationkavacheditor.h"
#include "stationlayout.h"
#include "stationlayoutwindow.h"
#include "tagbuilderwindow.h"

#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTableWidget>
#include <QTest>

// =============================================================================
//  Session 429 — every tag of a name the layout lists more than once drawn
//  where it is. Section_30.xlsx (station 527's layout, real tags) repeats one
//  5 km block three times: tags 802 … 902 three times each. Until now the
//  drawing kept one place per name (the last), so two of every three were
//  not drawn and a route could be drawn in one block and its MA end in
//  another. Each tag now has a key ("902", "902#2", "902#3"); clicking,
//  dragging, deleting, the Tag Builder and picking act on the one clicked;
//  a KAVACH route and a loco's MA are drawn through the tags their linking
//  distances lead to; a signal at a repeated foot tag is drawn at each.
// =============================================================================

namespace {

const char *const kFixture = DL_SRC_DIR "/tests/fixtures/station_kavach/section30_527.json";

qint64 locOf(const StationLayout::Layout &l, const QString &key)
{
    const int i = StationLayout::indexOfKey(l, key);
    return i < 0 ? -1 : StationLayout::info(l.tags.at(i)).absLoc;
}

int hitsSaying(const StationLayoutCanvas *c, const QString &text)
{
    int n = 0;
    for (const StationLayoutCanvas::Hit &h : c->hits()) n += h.text.contains(text);
    return n;
}

QStringList tagsOf(const StationKavachEditor *ed, int route)
{
    QStringList out;
    for (const StationKavach::RouteTag &t : ed->station().routes.value(route).tags) out << QStringLiteral("%1:%2").arg(t.dist).arg(t.tag);
    return out;
}

void clickTag(StationLayoutCanvas *c, const QString &key)
{
    c->grab();
    QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier, c->tagRect(key).center().toPoint());
}

} // namespace

TEST_SUITE(session429)
{
    StationLayout::Layout lay, fixture;
    CHECK(StationLayout::load(QStringLiteral(":/station_layouts/Section_30.xlsx"), &lay), "Section_30.xlsx loads");
    CHECK(StationLayout::load(QString::fromLatin1(kFixture), &fixture), "the station 527 fixture loads");
    lay.kavach = fixture.kavach;

    // ---- the keys ----
    const QStringList keys = StationLayout::tagKeys(lay);
    CHECK(keys.size() == lay.tags.size() && keys.count(QStringLiteral("902")) == 1 && keys.contains(QStringLiteral("902#3"))
              && !keys.contains(QStringLiteral("904#2")),
          "a key per tag: 902, 902#2, 902#3; 904 (listed once) just 904");
    CHECK(locOf(lay, QStringLiteral("902")) == 158620 && locOf(lay, QStringLiteral("902#2")) == 163620
              && locOf(lay, QStringLiteral("902#3")) == 168620 && StationLayout::indexOfKey(lay, QStringLiteral("902#4")) < 0,
          "each key the tag of its place in the file: 902 at 158620, 163620, 168620 m");
    CHECK(StationLayout::nameOfKey(QStringLiteral("902#2")) == QStringLiteral("902") && StationLayout::nameOfKey(QStringLiteral("12D"))
              == QStringLiteral("12D"), "a key's name");

    StationLayoutWindow w;
    w.resize(1400, 900);
    w.setStation(lay);
    StationLayoutCanvas *c = w.canvas();
    c->grab();

    // ---- every one drawn where it is ----
    const QRectF r1 = c->tagRect(QStringLiteral("902")), r2 = c->tagRect(QStringLiteral("902#2")), r3 = c->tagRect(QStringLiteral("902#3"));
    CHECK(!r1.isNull() && !r2.isNull() && !r3.isNull() && qAbs(r1.center().x() - c->xOf(158620)) < 0.5
              && qAbs(r2.center().x() - c->xOf(163620)) < 0.5 && qAbs(r3.center().x() - c->xOf(168620)) < 0.5,
          "the three tags 902 drawn at their three places");
    int drawn = 0;
    for (const StationLayoutCanvas::Hit &h : c->hits()) drawn += !h.tag.isEmpty();
    CHECK(drawn == lay.tags.size(), "one mark per tag of the file (was one per name)");
    CHECK(c->locAt(c->tagRect(QStringLiteral("802")).center().x()) <= 154010 && c->xOf(168820) <= c->width(),
          "the drawing spans all three blocks, 154 to 168.8 km");
    {
        bool each = true;
        const char *stations[] = { "501", "527", "528" };
        const QString tag2[] = { QStringLiteral("2"), QStringLiteral("2#2"), QStringLiteral("2#3") };
        for (int k = 0; k < 3; ++k) {
            const QString text = QStringLiteral("Signal S50 (id 13, station %1) at foot tag 2").arg(QLatin1String(stations[k]));
            bool at = false;
            for (const StationLayoutCanvas::Hit &h : c->hits())
                at = at || (h.text.startsWith(text) && qAbs(h.rect.center().x() - c->tagRect(tag2[k]).center().x()) < 1);
            each = each && at && hitsSaying(c, text) == 1;
        }
        CHECK(each, "the sheet's three S50 rows (stations 501, 527, 528), each at its own tag 2, block by block");
        CHECK(locOf(lay, QStringLiteral("2#2")) == 159660 && locOf(lay, QStringLiteral("2#3")) == 159660
                  && locOf(lay, QStringLiteral("12#3")) == 166330,
              "found: the file's third tag 2 is encoded at 159660 m, as the second (the third block's would be 164660 m): "
              "two S50s are drawn there, none in the third block");
    }
    CHECK(hitsSaying(c, QStringLiteral("Tag 902 (")) == 3 && hitsSaying(c, QStringLiteral("Tag 902#")) == 0,
          "hovering says the tag's name, not its key");

    // ---- the one clicked is the one acted on ----
    QSignalSpy sel(&w, &StationLayoutWindow::tagSelected);
    clickTag(c, QStringLiteral("902#2"));
    CHECK(w.table(0)->currentRow() == StationLayout::indexOfKey(lay, QStringLiteral("902#2"))
              && w.selectedTags() == (QStringList{ "902#2" }),
          "a click on the second 902 selects its row");
    CHECK(!sel.isEmpty() && sel.last().value(0).toString() == QStringLiteral("902"), "and says tag 902 to the windows that follow it");
    CHECK(w.moveTag(QStringLiteral("902#2"), 163630) && locOf(w.station(), QStringLiteral("902#2")) == 163630
              && locOf(w.station(), QStringLiteral("902")) == 158620 && locOf(w.station(), QStringLiteral("902#3")) == 168620,
          "dragged: only that one moves");
    CHECK(w.undo() && locOf(w.station(), QStringLiteral("902#2")) == 163620, "Undo");
    CHECK(w.shiftTags({ QStringLiteral("906#3") }, 5) && locOf(w.station(), QStringLiteral("906#3")) == locOf(lay, QStringLiteral("906#3")) + 5
              && locOf(w.station(), QStringLiteral("906")) == locOf(lay, QStringLiteral("906")),
          "shifted: only the one selected");
    w.undo();
    {
        const int dmBefore = w.station().lines.value(0).tags.count(QStringLiteral("906"));
        CHECK(w.deleteTags({ QStringLiteral("906#2") }) && w.station().tags.size() == lay.tags.size() - 1
                  && w.station().lines.value(0).tags.count(QStringLiteral("906")) == dmBefore - 1
                  && locOf(w.station(), QStringLiteral("906")) == locOf(lay, QStringLiteral("906"))
                  && locOf(w.station(), QStringLiteral("906#2")) == locOf(lay, QStringLiteral("906#3")),
              "deleted: that one, and its place in line DM; the first and third stay");
        w.undo();
    }
    {
        StationLayout::Tag t = lay.tags.at(StationLayout::indexOfKey(lay, QStringLiteral("898#3")));
        StationLayout::moveTag(&t, StationLayout::info(t).absLoc + 10);
        const QByteArray bytes = RfidTag::fromPages(t.pageX, t.pageY);
        w.openInTagBuilder(QStringLiteral("898#3"));
        CHECK(w.applyTag(QStringLiteral("898#3"), bytes) && locOf(w.station(), QStringLiteral("898#3")) == locOf(lay, QStringLiteral("898#3")) + 10
                  && locOf(w.station(), QStringLiteral("898")) == locOf(lay, QStringLiteral("898")) && w.station().tags.size() == lay.tags.size(),
              "the Tag Builder on the third 898: that one replaced, not refused for the other two");
        if (w.tagBuilder()) w.tagBuilder()->close();
        w.undo();
    }

    // ---- a KAVACH route through one block ----
    StationKavachEditor *ed = w.kavachEditor();
    w.tabs()->setCurrentWidget(ed);
    ed->selectRoute(1);
    c->grab();
    CHECK(c->kavachView().sections.size() == 2 && c->kavachView().sections[0].tags == (QStringList{ "18", "906", "898", "900" })
              && c->kavachView().sections[1].tags == (QStringList{ "900", "902", "904" }),
          "S43-A900 and its MA to A904 drawn in the first block, the only one holding tag 904");
    {
        StationKavach::Station st = ed->station();
        st.signalList[2].aspect = 1;   // A900 Red: one section, which block is open
        StationKavach::Route &r = st.routes[1];
        StationLayout::Layout l2 = lay;
        l2.kavach = StationKavach::toJson(st);
        const StationLayoutCanvas::KavachView v = kavachViewOf(l2, st, 1);
        CHECK(v.sections.size() == 1 && v.sections[0].tags.size() == r.tags.size(), "A900 Red: S43-A900 alone");
    }

    // ---- picking in the second block: from the tag clicked ----
    {
        StationKavach::Station st;
        StationKavach::fromJson(fixture.kavach, &st);
        const QVector<StationKavach::RouteTag> sent = st.routes[1].tags;
        st.routes[1].tags = { StationKavach::RouteTag{ 0, 18, 0 } };
        StationLayout::Layout l2 = lay;
        l2.kavach = StationKavach::toJson(st);
        w.setStation(l2);
        w.tabs()->setCurrentWidget(ed);
        ed->selectRoute(1);
        ed->setPicking(true);
        clickTag(c, QStringLiteral("906#2"));
        clickTag(c, QStringLiteral("898#2"));
        clickTag(c, QStringLiteral("900#2"));
        QStringList want;
        for (const StationKavach::RouteTag &t : sent) want << QStringLiteral("%1:%2").arg(t.dist).arg(t.tag);
        CHECK(tagsOf(ed, 1) == want, "906, 898, 900 of the second block clicked: station 527's 200, 656, 260 m");
        CHECK(!ed->isPicking(), "900 is A900's foot tag: picking done");
    }

    // ---- a loco's MA through the block it is in ----
    {
        StationKavach::Station st;
        StationKavach::fromJson(fixture.kavach, &st);
        StationKavach::Loco o;
        o.id = 1;
        o.absLoc = 162100;   // 36 m past the second 906 (162064 m)
        o.trainLength = 250;
        o.dir = 1;
        o.route = 1;
        o.refTag = 906;
        o.maRoutes = { 1, 2 };
        o.maSent = 1956;
        const StationLayoutCanvas::LiveView v = liveViewOf(lay, st, StationKavach::State::initial(st), { o });
        CHECK(v.locos.size() == 1 && v.locos[0].laneTag == QStringLiteral("906#2") && v.locos[0].maEnd == 161864 + 1956,
              "a loco at 162100 m: on the second 906; its MA from the second 18 (161864 m)");
        CHECK(v.locos[0].maTags == (QStringList{ "898#2", "900#2", "902#2" }), "through the second block's 898, 900, 902");
    }
}
