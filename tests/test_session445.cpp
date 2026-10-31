#include "testutil.h"

#include "capturedecoder.h"
#include "packetbuilder.h"
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
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>

#include <algorithm>

// =============================================================================
//  Session 445 — the other SoS kinds. Head-on: a loco coming the other way
//  on the same line, ahead within the MA (the MA cut 100 m short of its
//  front), or one reporting EMERGENCY_STATUS 4. Shunt violation: past a Red
//  signal in Shunting mode. Foreign RFID: a tag the layout does not hold,
//  on no route. And the operator's: Station General SoS, an SoS to one loco.
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

QHash<QString, qint64> firstOf(const QVector<In> &ins, int loco, const char *type, int tag)
{
    for (const In &in : ins)
        if (in.loco == loco && in.line.startsWith(QLatin1String(type)) && in.raw.value(QStringLiteral("LAST_RFID_TAG")) == tag) return in.raw;
    return {};
}

} // namespace

TEST_SUITE(session445)
{
    PacketBuilder b;
    const Schema::Encoder &enc = b.encoder();
    StationLayout::Layout l527;
    CHECK(StationLayout::load(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_527.xml"), &l527), "station_527.xml");
    StationKavach::Station s = station527();
    s.tagLocs = StationLayout::tagLocationsByNumber(l527);
    s.tagLine = StationInterlock::trackOf(l527).lineOfTag;
    const QVector<In> ins = heard(linesOf(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_170217.cap")),
                                  linesOf(QStringLiteral(DL_SRC_DIR "/replay/loco_2_1_27062026_170217.cap")));
    const QHash<QString, qint64> at906 = firstOf(ins, 1, "@lsrp_", 906);
    CHECK(at906.value(QStringLiteral("ABS_LOCO_LOC")) == 162066, "loco 1's real LSRP at tag 906: 162066 m");
    StationKavach::State st = StationKavach::State::initial(s);

    // ---- loco 2 as it really was (UP line): no Head-on ----
    StationKavach::Loco two;
    StationKavach::note(&two, firstOf(ins, 2, "@arp_", 989));
    StationKavach::Loco one;
    StationKavach::Answer a = StationKavach::answer(s, st, &one, at906, enc, { two });
    CHECK(a.ok && maField(a, "DEST_LOCO_SOS") == 0 && maField(a, "MA_W_R_T_SIG") == 1956,
          "loco 2 (162648 m, Reverse) is on UM: loco 1 on DM gets no SoS, MA 1956 m");

    // ---- Head-on: a loco coming the other way on DM, ahead within the MA ----
    StationKavach::Loco against = two;
    against.lastTag = 898;
    against.absLoc = 162722;
    StationKavach::Loco one2;
    a = StationKavach::answer(s, st, &one2, at906, enc, { against });
    CHECK(a.ok && maField(a, "DEST_LOCO_SOS") == 6 && maField(a, "MA_W_R_T_SIG") == 758 && one2.sos == 6,
          QByteArray("loco 2 coming the other way at 162722 m on DM: SoS Head-on, MA cut to 858 - 100 = 758 m (") + a.note.toUtf8() + ")");
    CHECK(a.note.contains(QStringLiteral("SoS Head-on: loco 2 coming the other way on DM")), "said");
    against.absLoc = 166000;
    StationKavach::Loco one3;
    a = StationKavach::answer(s, st, &one3, at906, enc, { against });
    CHECK(a.ok && maField(a, "DEST_LOCO_SOS") == 0, "beyond the MA's end (166000 m): no SoS");
    QHash<QString, qint64> reports = at906;
    reports.insert(QStringLiteral("EMERGENCY_STATUS"), 4);
    StationKavach::Loco one4;
    a = StationKavach::answer(s, st, &one4, reports, enc);
    CHECK(a.ok && maField(a, "DEST_LOCO_SOS") == 6 && maField(a, "MA_W_R_T_SIG") == 1956, "the loco reports Head-On Collision (4): echoed, SoS 6");

    // ---- Shunt violation: past A904 at Red in Shunting mode (the real SPAD LSRP 50548) ----
    QHash<QString, qint64> spad;
    for (const QString &l : linesOf(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap")))
        if (l.startsWith(QLatin1String("@lsrp_")) && rawOf(l).value(QStringLiteral("FRAME_NUM")) == 50548) { spad = rawOf(l); break; }
    CHECK(spad.value(QStringLiteral("ABS_LOCO_LOC")) == 163821, "the real LSRP 50548: 163821 m, 1 m past A904");
    StationKavach::State red = st;
    red.aspect.insert(QStringLiteral("A904"), 1);
    StationKavach::Loco sp;
    a = StationKavach::answer(s, red, &sp, spad, enc);
    CHECK(a.ok && maField(a, "DEST_LOCO_SOS") == 4, "as it was (Trip mode): SPAD (4)");
    spad.insert(QStringLiteral("LOCO_MODE"), 10);
    StationKavach::Loco sh;
    a = StationKavach::answer(s, red, &sh, spad, enc);
    CHECK(a.ok && maField(a, "DEST_LOCO_SOS") == 7 && maField(a, "MA_W_R_T_SIG") == 0 && a.note.startsWith(QStringLiteral("Shunt violation")),
          "in Shunting mode (10): Shunt violation (7), MA 0");

    // ---- Foreign RFID: a tag the layout does not hold, read by a loco the station has placed ----
    StationKavach::Loco f;
    StationKavach::answer(s, st, &f, at906, enc);
    QHash<QString, qint64> odd = at906;
    odd.insert(QStringLiteral("LAST_RFID_TAG"), 1023);
    a = StationKavach::answer(s, st, &f, odd, enc);
    CHECK(a.ok && maField(a, "DEST_LOCO_SOS") == 1 && a.note.contains(QStringLiteral("SoS Foreign RFID: tag 1023 is not in the layout")),
          "tag 1023 (not in station_527.xml), loco 1 placed on S43-A900: SoS Foreign RFID (1)");
    QHash<QString, qint64> known = at906;
    known.insert(QStringLiteral("LAST_RFID_TAG"), 59);
    StationKavach::Loco f2 = f;
    a = StationKavach::answer(s, st, &f2, known, enc);
    CHECK(a.ok && maField(a, "DEST_LOCO_SOS") == 0, "tag 59 (in the layout, on no route): no SoS");

    // ---- every real LSRP of station 527's own run: none of these raised ----
    {
        StationKavach::Station fx = s;
        QHash<int, StationKavach::Loco> heardLocos;
        int answered = 0, raised = 0;
        for (const char *cap : { "/replay/2026-10-08/loco_1_1_08102026_103520.cap", "/replay/2026-10-08/loco_1_1_08102026_105200.cap" })
            for (const QString &l : linesOf(QStringLiteral(DL_SRC_DIR) + QLatin1String(cap))) {
                if (!l.startsWith(QLatin1String("@lsrp_"))) continue;
                const QHash<QString, qint64> r = rawOf(l);
                const StationKavach::Answer x = StationKavach::answer(fx, st, &heardLocos[1], r, enc);
                if (!x.ok) continue;
                ++answered;
                if (maField(x, "DEST_LOCO_SOS") != 0) ++raised;
            }
        CHECK(answered > 100 && raised == 0, QByteArray("8 Oct: ") + QByteArray::number(answered) + " LSRPs answered, none with an SoS (as the real station)");
    }

    // ---- the operator's ----
    StationKavach::State op = st;
    op.sos.insert(0, 8);
    StationKavach::Loco g;
    a = StationKavach::answer(s, op, &g, at906, enc);
    CHECK(a.ok && maField(a, "DEST_LOCO_SOS") == 8 && maField(a, "MA_W_R_T_SIG") == 1956 && a.note.contains(QStringLiteral("SoS 8 (the operator's)")),
          "Station General SoS: 8 to every loco, the MA kept");
    op.sos.insert(1, 7);
    StationKavach::Loco g2;
    a = StationKavach::answer(s, op, &g2, at906, enc);
    CHECK(a.ok && maField(a, "DEST_LOCO_SOS") == 7, "an SoS to loco 1 over the general one: 7");

    // ---- Station…: the controls ----
    StationKavachPanel panel({});
    StationKavach::State ps = st;
    panel.setStation(s, &ps);
    auto *general = panel.findChild<QCheckBox *>(QStringLiteral("skGeneralSos"));
    auto *loco = panel.findChild<QSpinBox *>(QStringLiteral("skSosLoco"));
    auto *kind = panel.findChild<QComboBox *>(QStringLiteral("skSosKind"));
    auto *send = panel.findChild<QPushButton *>(QStringLiteral("skSosSend"));
    auto *clear = panel.findChild<QPushButton *>(QStringLiteral("skSosClear"));
    CHECK(general && loco && kind && send && clear, "Station…: Station General SoS, loco, kind, Send SoS, Clear");
    if (!(general && loco && kind && send && clear)) return;
    general->setChecked(true);
    CHECK(ps.sos.value(0) == 8, "Station General SoS ticked: 8 to every loco");
    loco->setValue(2);
    kind->setCurrentIndex(kind->findData(6));
    send->click();
    CHECK(ps.sos.value(2) == 6 && panel.findChild<QLabel *>(QStringLiteral("skSosNow"))->text() == QStringLiteral("SoS sent: loco 2: 6"),
          "Head-on to loco 2, shown");
    clear->click();
    general->setChecked(false);
    CHECK(ps.sos.isEmpty(), "cleared, unticked: none");
}
