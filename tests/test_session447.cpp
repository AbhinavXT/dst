#include "testutil.h"

#include "capturedecoder.h"
#include "packetbuilder.h"
#include "stationkavacheditor.h"
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
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>

// =============================================================================
//  Session 447 — the hand-over to the next station. A route with a next
//  station (Station Layout ▸ KAVACH ▸ Routes ▸ Next station, "next_stn"):
//  while the MA reaches it, NEXT_STN_COMM 1 and APPR_STN_ILC_IBS_ID. Packet
//  Maker answering as the station leaves an ARP to another station
//  (APPROACHING_STN_ID) unanswered, the loco's place still noted: the real
//  ARPs of loco 1 approaching station 501 (loco_1_1_26062026_162418.cap).
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

namespace {

qint64 maField(const StationKavach::Answer &a, const char *field)
{
    for (const Schema::SubEntry &x : a.subs) if (x.type == 0) return x.values.value(QLatin1String(field), -1);
    return -1;
}

} // namespace

TEST_SUITE(session447)
{
    PacketBuilder b;
    const Schema::Encoder &enc = b.encoder();
    StationKavach::Station s = station527();
    const int a900 = routeNamed(s, QStringLiteral("A900-A904 DN Auto"));
    CHECK(a900 >= 0, "route A900-A904");
    if (a900 < 0) return;
    s.routes[a900].nextStn = 501;
    StationKavach::Station back;
    CHECK(StationKavach::fromJson(StationKavach::toJson(s), &back) && back.routes[a900].nextStn == 501
              && StationKavach::toJson(s).value(QStringLiteral("routes")).toArray().at(a900).toObject().value(QStringLiteral("next_stn")).toInt() == 501,
          "next_stn 501 written and read back");

    QHash<QString, qint64> lsrp;
    for (const QString &l : linesOf(QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_105200.cap")))
        if (l.startsWith(QLatin1String("@lsrp_")) && rawOf(l).value(QStringLiteral("FRAME_NUM")) == 40380) { lsrp = rawOf(l); break; }
    CHECK(lsrp.value(QStringLiteral("LAST_RFID_TAG")) == 906, "the real LSRP 40380 at tag 906");
    StationKavach::State st = StationKavach::State::initial(s);
    StationKavach::Loco loco;
    StationKavach::Answer a = StationKavach::answer(s, st, &loco, lsrp, enc);
    CHECK(a.ok && maField(a, "NEXT_STN_COMM") == 1 && maField(a, "APPR_STN_ILC_IBS_ID") == 501 && maField(a, "MA_W_R_T_SIG") == 1956
              && a.note.contains(QStringLiteral("hands over to station 501 (NEXT_STN_COMM, route A900-A904 DN Auto)")),
          "A900 Yellow: the MA reaches A900-A904: NEXT_STN_COMM 1, station 501");
    QString err;
    const QByteArray body = enc.encodeBody(QStringLiteral("slrp"), a.header, a.subs, &err);
    const Schema::ParsedPacket pp = enc.parseBody(QStringLiteral("slrp"), body);
    CHECK(!body.isEmpty() && pp.ok && pp.subs.value(0).values.value(QStringLiteral("APPR_STN_ILC_IBS_ID")) == 501,
          QByteArray("it encodes and reads back: APPR_STN_ILC_IBS_ID 501 ") + err.toUtf8());
    st.aspect.insert(QStringLiteral("A900"), 1);
    a = StationKavach::answer(s, st, &loco, lsrp, enc);
    CHECK(a.ok && maField(a, "NEXT_STN_COMM") == 0 && maField(a, "MA_W_R_T_SIG") == 1116, "A900 Red: the MA ends at A900: no hand-over");

    // ---- the editor's column ----
    {
        StationLayoutWindow w;
        CHECK(w.openFile(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_kavach/section30_527.json")), "the 527 file in Station Layout");
        StationKavachEditor *ed = w.kavachEditor();
        QTableWidget *routes = ed->routeTable();
        int col = -1;
        for (int c = 0; c < routes->columnCount(); ++c)
            if (routes->horizontalHeaderItem(c)->text() == QStringLiteral("Next station")) col = c;
        CHECK(col >= 0, "Routes: a Next station column");
        if (col < 0) return;
        if (!routes->item(2, col)) routes->setItem(2, col, new QTableWidgetItem);
        routes->item(2, col)->setText(QStringLiteral("501"));
        CHECK(ed->station().routes[2].nextStn == 501, "501 typed for A900-A904: its next station");
        routes->item(2, col)->setText(QStringLiteral("70000"));
        CHECK(ed->station().routes[2].nextStn == 501 && w.status()->text().contains(QStringLiteral("1 to 65535")), "70000: refused, said");
    }

    // ---- Packet Maker: an ARP to station 501 is not station 527's ----
    QStringList cap = linesOf(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_26062026_162418.cap"));
    QString arp501, aapLine;
    for (const QString &l : cap) {
        if (l.startsWith(QLatin1String("@arp_")) && arp501.isEmpty() && rawOf(l).value(QStringLiteral("APPROACHING_STN_ID")) == 501) arp501 = l;
        if (l.startsWith(QLatin1String("@aap_")) && aapLine.isEmpty()) aapLine = l;
    }
    CHECK(!arp501.isEmpty() && !aapLine.isEmpty(), "a real ARP approaching station 501, and station 527's AAP");
    QTemporaryDir tmp;
    StationLayout::Layout l527;
    StationLayout::load(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_527.xml"), &l527);
    l527.kavach = StationKavach::toJson(s);
    const QString layPath = tmp.filePath(QStringLiteral("station_527.xml"));
    StationLayout::save(layPath, l527);
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
    if (!(arm && answer && from)) { CHECK(false, "Packet Maker's controls"); return; }
    from->setCurrentIndex(1);
    dlg.findChild<QLineEdit *>(QStringLiteral("pmAnswerAap"))->setText(presetPath);
    dlg.findChild<QLineEdit *>(QStringLiteral("pmAnswerLayout"))->setText(layPath);
    answer->setChecked(true);
    arm->setChecked(true);
    auto e = LogEntryPtr::create();
    e->text = arp501;
    e->epochMs = CaptureDecoder::parseLine(arp501).rtc.toMSecsSinceEpoch();
    e->header.source_id = 21;
    dlg.observe(e);
    auto *status = dlg.findChild<StatusLine *>();
    const QString said = status ? status->text() : QString();
    CHECK(said.contains(QStringLiteral("is for station 501: not answered by station 527")),
          QByteArray("the ARP to 501: not answered, said (") + said.toUtf8() + ")");
    CHECK(dlg.stationLocos().contains(1) && dlg.stationLocos().value(1).absLoc == rawOf(arp501).value(QStringLiteral("ABS_LOCO_LOC")),
          "where loco 1 is: still noted");
}
