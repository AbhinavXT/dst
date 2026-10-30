#include "testutil.h"

#include "capturedecoder.h"
#include "packetbuilder.h"
#include "stationkavach.h"
#include "stationkavacheditor.h"
#include "stationlayout.h"
#include "stationlayoutwindow.h"
#include "statusline.h"

#include <QFile>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTest>

// =============================================================================
//  Session 425 — a KAVACH route's tags picked by clicking them on the
//  Station Layout drawing: Pick on drawing, then the tags in the order the
//  loco reads them; each linking distance from the layout's tags. The last
//  one clicked again comes off; the exit signal's foot tag or Esc ends it.
//  Station 527's data (tests/fixtures/station_kavach/section30_527.json) on
//  the built-in Section_30.xlsx (real tags): route S43-A900 rebuilt from its
//  entry tag by four clicks gives the linking distances station 527 sent
//  (200, 656, 260 m), and the real LSRP 40380 of
//  replay/2026-10-08/loco_1_1_08102026_105200.cap is answered MA 1956 m again.
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

qint64 maFor(const QJsonObject &kavach, const QHash<QString, qint64> &lsrp, const Schema::Encoder &enc)
{
    StationKavach::Station s;
    if (!StationKavach::fromJson(kavach, &s)) return -1;
    StationKavach::Loco loco;
    const StationKavach::Answer a = StationKavach::answer(s, StationKavach::State::initial(s), &loco, lsrp, enc);
    for (const Schema::SubEntry &se : a.subs) if (se.type == 0) return se.values.value(QStringLiteral("MA_W_R_T_SIG"));
    return -1;
}

// The route's tags as "dist:tag" pairs.
QStringList tagsOf(const StationKavachEditor *ed, int route)
{
    QStringList out;
    if (route < 0 || route >= ed->station().routes.size()) return out;
    for (const StationKavach::RouteTag &t : ed->station().routes[route].tags) out << QStringLiteral("%1:%2").arg(t.dist).arg(t.tag);
    return out;
}

// A click on tag `name` where the drawing has it.
void clickTag(StationLayoutCanvas *c, const QString &name)
{
    c->grab();   // the hit areas are the last paint's
    QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier, c->tagRect(name).center().toPoint());
}

} // namespace

TEST_SUITE(session425)
{
    PacketBuilder b;
    const QHash<QString, qint64> lsrp = realLsrp(40380);
    StationLayout::Layout fixture, lay;
    StationKavach::Station st;
    CHECK(StationLayout::load(QString::fromLatin1(kFixture), &fixture) && StationKavach::fromJson(fixture.kavach, &st),
          "the station 527 fixture loads");
    CHECK(!section30().isEmpty() && StationLayout::load(section30(), &lay), "the built-in Section_30.xlsx loads");
    const QVector<StationKavach::RouteTag> sentTags = st.routes[1].tags;
    st.routes[1].tags = { StationKavach::RouteTag{ 0, 18, 0 } };   // S43-A900: its entry foot tag only
    lay.kavach = StationKavach::toJson(st);
    CHECK(maFor(lay.kavach, lsrp, b.encoder()) != 1956, "S43-A900 cut to its entry tag: LSRP 40380 is not answered MA 1956 m");

    StationLayoutWindow w;
    w.resize(1400, 900);
    w.setStation(lay);
    StationKavachEditor *ed = w.kavachEditor();
    StationLayoutCanvas *c = w.canvas();
    w.tabs()->setCurrentWidget(ed);
    ed->selectRoute(1);
    QPushButton *pick = ed->pickButton();
    CHECK(pick && pick->isEnabled() && !pick->isChecked() && !c->isPicking(), "Pick on drawing is there, off");
    ed->listTabs()->setCurrentIndex(StationKavachEditor::Ssp);
    pick->click();
    CHECK(pick->isChecked() && c->isPicking() && ed->listTabs()->currentIndex() == StationKavachEditor::Tags,
          "on: the drawing picks, the Tags table is shown");
    CHECK(w.status()->text().contains(QStringLiteral("click the route's tags")), "the status line says what to do");

    // ---- four clicks rebuild S43-A900 as station 527 sent it ----
    const QString firstTagBits = w.station().tags.isEmpty() ? QString() : w.station().tags.first().pageX;
    clickTag(c, QStringLiteral("906"));
    CHECK(tagsOf(ed, 1) == (QStringList{ "0:18", "200:906" }), "906 clicked: 200 m on from 18 (from the layout's tags)");
    CHECK(w.tabs()->currentWidget() == ed && ed->currentRoute() == 1, "the KAVACH tab and the route stay");
    CHECK(w.status()->message() == QStringLiteral("Tag 906 added to route S43-A900 DN Auto, 200 m on from tag 18 (200 m)"),
          "the status line says it");
    clickTag(c, QStringLiteral("898"));
    CHECK(tagsOf(ed, 1) == (QStringList{ "0:18", "200:906", "656:898" }), "898: 656 m on");
    clickTag(c, QStringLiteral("900"));
    QStringList sent;
    for (const StationKavach::RouteTag &t : sentTags) sent << QStringLiteral("%1:%2").arg(t.dist).arg(t.tag);
    CHECK(tagsOf(ed, 1) == sent, "900: 260 m on; the tags are station 527's: 0:18 200:906 656:898 260:900");
    CHECK(!pick->isChecked() && !c->isPicking(), "900 is A900's foot tag (the exit): picking ends");
    CHECK(w.status()->message() == QStringLiteral("Tag 900 is A900's foot tag: route S43-A900 DN Auto is 1116 m, picking done"),
          "and says so");
    CHECK(maFor(w.station().kavach, lsrp, b.encoder()) == 1956, "the real LSRP 40380 is answered MA 1956 m again");
    CHECK(!w.station().tags.isEmpty() && w.station().tags.first().pageX == firstTagBits, "clicking picked: no tag was moved");
    CHECK(w.undo() && tagsOf(ed, 1) == (QStringList{ "0:18", "200:906", "656:898" }), "each pick one Undo step");

    // ---- taking off, refusals ----
    pick->click();
    clickTag(c, QStringLiteral("898"));
    CHECK(tagsOf(ed, 1) == (QStringList{ "0:18", "200:906" }) && w.status()->text().contains(QStringLiteral("Tag 898 taken off")),
          "the last tag clicked again: taken off");
    const QJsonObject k = w.station().kavach;
    clickTag(c, QStringLiteral("18"));
    CHECK(w.station().kavach == k && w.status()->message() == QStringLiteral("Tag 18 is already in route S43-A900 DN Auto (row 1)"),
          "a tag already in the route: refused by name");
    clickTag(c, QStringLiteral("16"));
    CHECK(w.station().kavach == k
              && w.status()->message() == QStringLiteral("Tag 16 does not lie on from tag 906 in route S43-A900 DN Auto's direction "
                                                     "(Nominal: rising locations)"),
          "a tag behind the last: refused");
    CHECK(ed->pickTag(902, 168620) && tagsOf(ed, 1).last() == QStringLiteral("1556:902"),
          "a tag skipped over is taken as the next: 902 is 1556 m on from 906");
    ed->pickTag(902, 168620);

    QTest::keyClick(c, Qt::Key_Escape);
    CHECK(!pick->isChecked() && !c->isPicking(), "Esc ends picking");
    pick->click();
    w.measureButton()->setChecked(true);
    CHECK(!pick->isChecked() && !c->isPicking(), "Measure ends picking");
    w.measureButton()->setChecked(false);
    pick->click();
    w.tabs()->setCurrentIndex(0);
    CHECK(!pick->isChecked() && !c->isPicking(), "another tab ends picking");
    w.tabs()->setCurrentWidget(ed);
    ed->selectRoute(1);
    QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier, c->tagRect(QStringLiteral("898")).center().toPoint());
    CHECK(w.tabs()->currentIndex() == 0, "not picking, a tag click selects it in the Tags table as before");

    // ---- a route with no entry tag yet; a Reverse route ----
    {
        StationKavach::Station s2 = st;
        s2.routes[1].tags = { StationKavach::RouteTag{ 0, 0, 0 } };
        s2.routes[2].pktDir = 2;
        s2.routes[2].tags = { StationKavach::RouteTag{ 0, 900, 0 } };
        StationLayout::Layout l2 = lay;
        l2.kavach = StationKavach::toJson(s2);
        w.setStation(l2);
        w.tabs()->setCurrentWidget(ed);
        ed->selectRoute(1);
        ed->setPicking(true);
        clickTag(c, QStringLiteral("18"));
        CHECK(tagsOf(ed, 1) == (QStringList{ "0:18" }) && w.status()->text().contains(QStringLiteral("starts at tag 18")),
              "no entry tag yet: the first picked is it");
        clickTag(c, QStringLiteral("18"));
        CHECK(tagsOf(ed, 1) == (QStringList{ "0:18" }) && w.status()->text().contains(QStringLiteral("entry foot tag: it stays")),
              "the entry tag clicked again stays");
        ed->selectRoute(2);
        clickTag(c, QStringLiteral("898"));
        CHECK(tagsOf(ed, 2) == (QStringList{ "0:900", "260:898" }), "a Reverse route: 898, 260 m back in locations, is on");
        clickTag(c, QStringLiteral("902"));
        CHECK(tagsOf(ed, 2) == (QStringList{ "0:900", "260:898" }) && w.status()->text().contains(QStringLiteral("Reverse: falling")),
              "and 902 (rising) is refused");
        ed->selectRoute(-1);
        CHECK(!pick->isEnabled() && !pick->isChecked() && !c->isPicking(), "no route selected: nothing to pick for");
        CHECK(!ed->pickTag(18, 166864) && w.status()->text().contains(QStringLiteral("Select a route first")),
              "a pick with no route: refused");
    }
}
