#include "testutil.h"

#include "capturedecoder.h"
#include "logmodel.h"
#include "packetmakerdialog.h"
#include "packetpreset.h"
#include "sessionkeystore.h"
#include "stationinterlock.h"
#include "stationkavach.h"
#include "stationkavachpanel.h"
#include "stationlayout.h"
#include "stationlayoutwindow.h"
#include "statusline.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QFile>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>

#include <algorithm>

// =============================================================================
//  Session 442 — the station works its signals: a route set locks its
//  points; a train in it (its first track circuit occupied) puts its signal
//  back to Red; once every circuit is clear again the route is released. A
//  signal cleared by hand goes back to Red as a train passes it too.
//  Station 527's KAVACH data (the session 422 fixture) given the circuits
//  of station_527.xml that lie on its routes (S43-A900: 43T … 43CT;
//  A900-A904: 1629T … 1629BT) and the June route S50-S48 over DMT; the real
//  ARPs and LSRPs of loco 1 (DN) and loco 2 (UP) in
//  replay/loco_{1,2}_1_27062026_170217.cap, in frame order.
// =============================================================================

namespace {

QStringList linesOf(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QStringList out;
    for (const QString &l : QString::fromLatin1(f.readAll()).split(QLatin1Char('\n'))) out << l.trimmed();
    return out;
}

QHash<QString, qint64> rawOf(const QString &line)
{
    QHash<QString, qint64> raw;
    CaptureDecoder::describe(CaptureDecoder::parseLine(line), nullptr, 0, &raw);
    return raw;
}

struct In { qint64 fn; int loco; QString line; QHash<QString, qint64> raw; };

QVector<In> heard(const QStringList &one, const QStringList &two)
{
    QVector<In> ins;
    for (int k = 0; k < 2; ++k)
        for (const QString &l : (k ? two : one))
            if (l.startsWith(QLatin1String("@arp_")) || l.startsWith(QLatin1String("@lsrp_"))) {
                const QHash<QString, qint64> r = rawOf(l);
                ins.append({ r.value(QStringLiteral("FRAME_NUM")), k + 1, l, r });
            }
    std::stable_sort(ins.begin(), ins.end(), [](const In &x, const In &y) { return x.fn != y.fn ? x.fn < y.fn : x.loco < y.loco; });
    return ins;
}

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

TEST_SUITE(session442)
{
    StationLayout::Layout l527;
    CHECK(StationLayout::load(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_527.xml"), &l527), "station_527.xml");
    const StationInterlock::Track track = StationInterlock::trackOf(l527);
    const StationKavach::Station s = station527();
    const int s50 = routeNamed(s, QStringLiteral("S50-S48 DN")), s43 = routeNamed(s, QStringLiteral("S43-A900 DN Auto"));
    CHECK(s50 >= 0 && s43 >= 0, "routes S50-S48 and S43-A900");
    if (s50 < 0 || s43 < 0) return;

    // ---- the real run: routes set, put back as loco 1 enters them, S50-S48 released behind it ----
    StationKavach::State st = StationKavach::State::initial(s);
    st.interlocking = true;
    CHECK(StationInterlock::setRoute(s, &st, s50).isEmpty() && StationInterlock::setRoute(s, &st, s43).isEmpty()
              && st.aspectOf(s, QStringLiteral("S50")) == 2 && st.aspectOf(s, QStringLiteral("S43")) == 2,
          "S50-S48 and S43-A900 set: S50 Yellow (was Red), S43 Yellow");
    const QVector<In> ins = heard(linesOf(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_170217.cap")),
                                  linesOf(QStringLiteral(DL_SRC_DIR "/replay/loco_2_1_27062026_170217.cap")));
    QHash<int, StationKavach::Loco> locos;
    QStringList events;
    qint64 releasedAt = -1, s43RedAt = -1;
    for (const In &in : ins) {
        StationKavach::note(&locos[in.loco], in.raw);
        st.occupiedBy = StationInterlock::occupancy(track, locos.values()).by;
        for (const QString &e : StationInterlock::work(s, &st)) {
            events << e;
            if (e.startsWith(QStringLiteral("route S50-S48 DN released"))) releasedAt = in.raw.value(QStringLiteral("ABS_LOCO_LOC"));
            if (e.startsWith(QStringLiteral("S43 back to Red"))) s43RedAt = in.raw.value(QStringLiteral("ABS_LOCO_LOC"));
        }
    }
    CHECK(events.value(0) == QStringLiteral("S50 back to Red: a train in route S50-S48 DN (DMT by loco 1)"),
          QByteArray("loco 1's first frame (161254 m, in DMT): S50 back to Red (") + events.value(0).toUtf8() + ")");
    CHECK(releasedAt > 161330 + 250 && releasedAt < 161650,
          QByteArray("S50-S48 released once loco 1's rear left DMT (161330 m): at ") + QByteArray::number(releasedAt));
    bool s43Said = false;
    for (const QString &x : events) s43Said = s43Said || x == QStringLiteral("S43 back to Red: a train in route S43-A900 DN Auto (43T by loco 1)");
    CHECK(s43RedAt >= 161900 && s43RedAt < 161960 && s43Said,
          QByteArray("S43 back to Red as loco 1's front entered 43T (161900 m): at ") + QByteArray::number(s43RedAt));
    CHECK(st.routes.contains(QStringLiteral("S43")) && st.entered.contains(QStringLiteral("S43")) && !st.routes.contains(QStringLiteral("S50")),
          "at the end: S43-A900 set with a train in it (43BT, 43CT occupied); S50-S48 gone");
    CHECK(events.size() == 3, QByteArray("three things done, nothing for loco 2 on UP (") + events.join(" | ").toUtf8() + ")");
    CHECK(!StationInterlock::cancelRoute(s, &st, QStringLiteral("S43")).isEmpty() && st.routes.contains(QStringLiteral("S43")),
          "S43-A900 not cancelled while a train is in it");

    // ---- locking: two routes from S48 through point 19 ----
    StationKavach::Station two = s;
    const int main = routeNamed(two, QStringLiteral("S48-S43 DN Main"));
    CHECK(main >= 0, "S48-S43 DN Main");
    if (main < 0) return;
    two.routes[main].pointsNormal = QStringList{ QStringLiteral("19") };
    StationKavach::Route loop = two.routes[main];
    loop.name = QStringLiteral("S48-UP Loop");
    loop.pointsNormal.clear();
    loop.pointsReverse = QStringList{ QStringLiteral("19"), QStringLiteral("P18") };
    two.routes << loop;
    const int lp = two.routes.size() - 1;
    StationKavach::State lk = StationKavach::State::initial(two);
    lk.interlocking = true;
    CHECK(StationInterlock::setRoute(two, &lk, main).isEmpty(), "S48-S43 main set");
    CHECK(StationInterlock::pointLocked(two, lk, QStringLiteral("19")) == QStringLiteral("point 19 is locked by route S48-S43 DN Main")
              && StationInterlock::pointLocked(two, lk, QStringLiteral("P18")).isEmpty(),
          "19 locked by it; P18 free");
    CHECK(StationInterlock::setRoute(two, &lk, lp) == QStringLiteral("route S48-UP Loop not set: point 19 is locked by route S48-S43 DN Main")
              && !lk.reverse.contains(QStringLiteral("19")),
          "the loop through 19 Reverse: refused, 19 stays Normal");
    CHECK(StationInterlock::cancelRoute(two, &lk, QStringLiteral("S48")).isEmpty() && lk.aspectOf(two, QStringLiteral("S48")) == 1
              && StationInterlock::setRoute(two, &lk, lp).isEmpty() && lk.reverse.contains(QStringLiteral("19")),
          "main cancelled (no train in it), S48 Red; the loop set, 19 Reverse");
    lk.interlocking = false;
    lk.routes.clear();
    CHECK(StationInterlock::setRoute(two, &lk, main).isEmpty() && StationInterlock::work(two, &lk).isEmpty(),
          "interlocking off: nothing worked");

    // ---- a signal cleared by hand, no route set ----
    StationKavach::State hand = StationKavach::State::initial(s);
    hand.interlocking = true;
    hand.aspect.insert(QStringLiteral("A900"), 11);
    hand.occupied.insert(QStringLiteral("1629T"));
    CHECK(StationInterlock::work(s, &hand) == QStringList{ QStringLiteral("A900 back to Red: a train in route A900-A904 DN Auto (1629T)") }
              && hand.aspectOf(s, QStringLiteral("A900")) == 1,
          "A900 Green by hand, 1629T occupied (by hand): back to Red");

    // ---- the drawing: a point a set route holds is not thrown ----
    {
        StationLayoutWindow w;
        CHECK(w.openFile(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_527.xml")), "station_527.xml in Station Layout");
        StationKavach::State live = StationKavach::State::initial(two);
        live.interlocking = true;
        w.showLiveStation(two, live, {}, QStringLiteral("Packet Maker"));
        CHECK(w.setLiveRoute(main) && w.liveStationState().routes.value(QStringLiteral("S48")) == main, "main set on the drawing");
        CHECK(!w.setLivePoint(QStringLiteral("P19"), true) && !w.liveStationState().reverse.contains(QStringLiteral("19"))
                  && w.status()->text().endsWith(QStringLiteral("Not thrown: point 19 is locked by route S48-S43 DN Main")),
              QByteArray("P19 not thrown, said (") + w.status()->text().toUtf8() + ")");
        CHECK(w.setLivePoint(QStringLiteral("P18"), true), "P18 (no route holds it) thrown");
    }

    // ---- Packet Maker: on with track circuits; loco 1's ARP at tag 906 puts S43 back ----
    QTemporaryDir tmp;
    StationLayout::Layout lay = l527;
    lay.kavach = StationKavach::toJson(s);
    const QString layPath = tmp.filePath(QStringLiteral("station_527.xml"));
    CHECK(StationLayout::save(layPath, lay), "station_527.xml with the KAVACH data saved");
    QString aapLine;
    for (const QString &x : linesOf(QStringLiteral(DL_SRC_DIR "/replay/loco_2_1_27062026_170217.cap")))
        if (x.startsWith(QLatin1String("@aap_"))) { aapLine = x; break; }
    PacketPreset preset;
    preset.captype = QStringLiteral("aap");
    preset.header = rawOf(aapLine);
    const QString presetPath = tmp.filePath(QStringLiteral("aap.json"));
    preset.save(presetPath);
    SessionKeyStore keys;
    PacketMakerDialog dlg(nullptr, nullptr, &keys);
    dlg.findChild<QLineEdit *>(QStringLiteral("pmDestHost"))->setText(QStringLiteral("127.0.0.1"));
    QCheckBox *arm = nullptr;
    for (QCheckBox *cb : dlg.findChildren<QCheckBox *>())
        if (cb->text().contains(QStringLiteral("Arm"), Qt::CaseInsensitive)) arm = cb;
    auto *answer = dlg.findChild<QCheckBox *>(QStringLiteral("pmAnswerLoco"));
    auto *from = dlg.findChild<QComboBox *>(QStringLiteral("pmAnswerFrom"));
    auto *stationBtn = dlg.findChild<QPushButton *>(QStringLiteral("pmStationPanel"));
    if (!(arm && answer && from && stationBtn)) { CHECK(false, "Packet Maker's controls"); return; }
    from->setCurrentIndex(1);
    dlg.findChild<QLineEdit *>(QStringLiteral("pmAnswerAap"))->setText(presetPath);
    dlg.findChild<QLineEdit *>(QStringLiteral("pmAnswerLayout"))->setText(layPath);
    answer->setChecked(true);
    stationBtn->click();
    arm->setChecked(true);
    StationKavachPanel *panel = dlg.stationPanel();
    auto *box = panel ? panel->findChild<QCheckBox *>(QStringLiteral("skInterlocking")) : nullptr;
    CHECK(box && box->isChecked() && dlg.stationState()->interlocking, "Station…: Interlocking on (the layout has track circuits)");
    CHECK(dlg.stationState()->aspectOf(s, QStringLiteral("S43")) == 2, "S43 Yellow as the file starts it");
    QString arp906;
    for (const In &in : ins)
        if (in.loco == 1 && in.line.startsWith(QLatin1String("@arp_")) && in.raw.value(QStringLiteral("LAST_RFID_TAG")) == 906) { arp906 = in.line; break; }
    auto e = LogEntryPtr::create();
    e->text = arp906;
    e->epochMs = CaptureDecoder::parseLine(arp906).rtc.toMSecsSinceEpoch();
    e->header.source_id = 21;
    dlg.observe(e);
    QCoreApplication::processEvents();
    auto *log = panel ? panel->findChild<QPlainTextEdit *>(QStringLiteral("skEvents")) : nullptr;
    CHECK(dlg.stationState()->aspectOf(s, QStringLiteral("S43")) == 1 && log
              && log->toPlainText().startsWith(QStringLiteral("S43 back to Red: a train in route S43-A900 DN Auto (43T by loco 1")),
          "loco 1's ARP at tag 906: S43 back to Red, said in Station…");
    if (box) box->setChecked(false);
    CHECK(!dlg.stationState()->interlocking, "Interlocking unticked: off");
}
