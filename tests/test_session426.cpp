#include "testutil.h"

#include "capturedecoder.h"
#include "logmodel.h"
#include "messageheader.h"
#include "packetbuilder.h"
#include "packetmakerdialog.h"
#include "packetpreset.h"
#include "sessionkeystore.h"
#include "stationkavach.h"
#include "stationlayout.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLineEdit>
#include <QNetworkDatagram>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QUdpSocket>

// =============================================================================
//  Session 426 — the Stationary KAVACH sees a SPAD: a loco whose
//  ABS_LOCO_LOC is past the foot of the Red signal ahead is answered SoS
//  SPAD (DEST_LOCO_SOS 4), AUTHORITY_TYPE 0, MA 0, DIST_PKT_START 0, the MA
//  alone, REF_PROF_ID kept. Checked against station 527's real answers to
//  loco 1 standing 1 m past A904 at Red in
//  replay/loco_1_1_27062026_140226.cap (14:02:27-14:06:04, LSRP 50548 on):
//  every LSRP there answered as the station did, MAC aside (that capture
//  holds no session keys). The station's tag locations are the built-in
//  Section_30.xlsx's (station 527's layout, real tags).
// =============================================================================

namespace {

const char *const kCapture = DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap";

QStringList captureLines()
{
    QFile f(QString::fromLatin1(kCapture));
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

QString find(const QStringList &lines, const char *prefix, qint64 fn)
{
    static const PacketBuilder pb;
    for (const QString &l : lines) {
        if (!l.startsWith(QLatin1String(prefix))) continue;
        // An SLRP's FRAME_NUM through the encoder's parser; the LSRP's / ARP's from describe().
        const qint64 f = QLatin1String(prefix) == QLatin1String("@slrp_")
                             ? pb.encoder().parseBody(QStringLiteral("slrp"), CaptureDecoder::parseLine(l).bytes).header.value(QStringLiteral("FRAME_NUM"), -1)
                             : rawOf(l).value(QStringLiteral("FRAME_NUM"), -1);
        if (f == fn) return l;
    }
    return QString();
}

// The real SLRP in `line` against our answer: the bodies as the encoder
// writes them, our FRAME_NUM in the real one (the station answered a frame
// later), MAC and CRC aside.
bool sameAsReal(const Schema::Encoder &enc, const StationKavach::Answer &a, const QString &line, QString *why = nullptr)
{
    Schema::ParsedPacket pp = enc.parseBody(QStringLiteral("slrp"), CaptureDecoder::parseLine(line).bytes);
    if (!pp.ok) { if (why) *why = pp.error; return false; }
    pp.header.insert(QStringLiteral("FRAME_NUM"), a.header.value(QStringLiteral("FRAME_NUM")));
    QHash<QString, qint64> ours = a.header;
    ours.insert(QStringLiteral("PKT_LENGTH"), pp.header.value(QStringLiteral("PKT_LENGTH")));
    QString e1, e2;
    const QByteArray real = enc.encodeBody(QStringLiteral("slrp"), pp.header, pp.subs, &e1);
    const QByteArray mine = enc.encodeBody(QStringLiteral("slrp"), ours, a.subs, &e2);
    if (why) *why = QStringLiteral("%1 | %2 | real %3 | ours %4").arg(e1, e2, QString::fromLatin1(real.toHex(' ')),
                                                                       QString::fromLatin1(mine.toHex(' ')));
    return !real.isEmpty() && real == mine;
}

qint64 maField(const StationKavach::Answer &a, const char *field)
{
    for (const Schema::SubEntry &s : a.subs) if (s.type == 0) return s.values.value(QLatin1String(field), -1);
    return -1;
}

QByteArray receive(QUdpSocket &rx)
{
    QElapsedTimer t; t.start();
    while (!rx.hasPendingDatagrams() && t.elapsed() < 2000) { QCoreApplication::processEvents(); rx.waitForReadyRead(20); }
    return rx.hasPendingDatagrams() ? rx.receiveDatagram().data() : QByteArray();
}

} // namespace

TEST_SUITE(session426)
{
    PacketBuilder b;
    const Schema::Encoder &enc = b.encoder();
    StationLayout::Layout fixture, s30;
    StationKavach::Station st;
    CHECK(StationLayout::load(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_kavach/section30_527.json"), &fixture)
              && StationKavach::fromJson(fixture.kavach, &st),
          "the station 527 fixture loads");
    CHECK(StationLayout::load(QStringLiteral(":/station_layouts/Section_30.xlsx"), &s30), "Section_30.xlsx loads");
    const QHash<int, QVector<qint64>> locs = StationLayout::tagLocationsByNumber(s30);
    CHECK(locs.value(902) == (QVector<qint64>{ 158620, 163620, 168620 }) && locs.value(904) == (QVector<qint64>{ 158820 }),
          "Section_30's tags by number: 902 in each of its three blocks, 904 in the first");
    st.tagLocs = locs;
    StationKavach::State red = StationKavach::State::initial(st);
    red.aspect.insert(QStringLiteral("A904"), 1);

    const QStringList lines = captureLines();
    const QString lsrpLine = find(lines, "@lsrp_", 50548);
    const QHash<QString, qint64> lsrp = rawOf(lsrpLine);
    CHECK(lsrp.value(QStringLiteral("ABS_LOCO_LOC")) == 163821 && lsrp.value(QStringLiteral("LAST_RFID_TAG")) == 902
              && lsrp.value(QStringLiteral("LOCO_MODE")) == 7,
          "the real LSRP 50548: loco 1 at 163821 m, last tag 902, in Trip");

    // ---- the answer, against the real one ----
    {
        StationKavach::Loco loco;
        loco.profId = 4;   // the profile station 527 had given it
        const StationKavach::Answer a = StationKavach::answer(st, red, &loco, lsrp, enc);
        CHECK(a.ok && maField(a, "DEST_LOCO_SOS") == 4 && maField(a, "AUTHORITY_TYPE") == 0 && maField(a, "MA_W_R_T_SIG") == 0
                  && maField(a, "APPR_SIG_DIST") == 200 && a.subs.size() == 1,
              "A904 Red, the loco 1 m past its foot: SoS SPAD, no authority, MA 0, A904 200 m from tag 902, the MA alone");
        CHECK(a.note == QStringLiteral("SPAD: loco 1 m past A904 at Red: SoS SPAD, no authority, MA 0; ref tag 902"),
              "the send-log note says it");
        CHECK(a.checks.contains(QStringLiteral("loco past A904 at Red (SPAD)")), "and the Station… panel's checks");
        QString why;
        CHECK(sameAsReal(enc, a, find(lines, "@slrp_", 50549), &why), "the station's SLRP 50549 field for field, MAC aside");
        if (!why.isEmpty() && !sameAsReal(enc, a, find(lines, "@slrp_", 50549))) qWarning("%s", qPrintable(why));
        CHECK(loco.profId == 4 && loco.route == 2 && loco.refTag == 902, "the loco's profile kept; its route A900-A904, ref 902");
    }
    // Every LSRP of the SPAD stretch answered as the station answered the next frame.
    {
        int pairs = 0, same = 0;
        StationKavach::Loco loco;
        loco.profId = 4;
        for (const QString &l : lines) {
            if (!l.startsWith(QLatin1String("@lsrp_"))) continue;
            const QHash<QString, qint64> r = rawOf(l);
            const qint64 fn = r.value(QStringLiteral("FRAME_NUM"));
            if (fn < 50548 || fn > 50764) continue;
            const QString real = find(lines, "@slrp_", fn + 1);
            if (real.isEmpty()) continue;
            ++pairs;
            if (sameAsReal(enc, StationKavach::answer(st, red, &loco, r, enc), real)) ++same;
        }
        const QByteArray msg = QStringLiteral("all %1 LSRPs from 50548 to 50764 answered as station 527 did (%2 the same)")
                                   .arg(pairs).arg(same).toUtf8();
        CHECK(pairs == 59 && same == pairs, msg.constData());
    }

    // ---- when it is not a SPAD ----
    {
        StationKavach::Loco loco;
        const StationKavach::Answer y = StationKavach::answer(st, StationKavach::State::initial(st), &loco, lsrp, enc);
        CHECK(y.ok && maField(y, "DEST_LOCO_SOS") == 0 && maField(y, "MA_W_R_T_SIG") == 840, "A904 Yellow: no SoS, the MA to A904");
        QHash<QString, qint64> at = lsrp;
        at.insert(QStringLiteral("ABS_LOCO_LOC"), 163820);
        StationKavach::Loco l2;
        const StationKavach::Answer foot = StationKavach::answer(st, red, &l2, at, enc);
        CHECK(foot.ok && maField(foot, "DEST_LOCO_SOS") == 0 && maField(foot, "MA_W_R_T_SIG") == 840,
              "at the foot of A904 (163820 m), not past it: no SoS");
        StationKavach::State callOn = red;
        callOn.aspect.insert(QStringLiteral("A904"), 15);
        StationKavach::Loco l3;
        CHECK(maField(StationKavach::answer(st, callOn, &l3, lsrp, enc), "DEST_LOCO_SOS") == 0, "A904 calling-on: past it is allowed");
        StationKavach::Station noLocs = st;
        noLocs.tagLocs.clear();
        StationKavach::Loco l4;
        CHECK(maField(StationKavach::answer(noLocs, red, &l4, lsrp, enc), "DEST_LOCO_SOS") == 0,
              "without the layout's tag locations the station cannot see it: no SoS");
        StationKavach::State a900 = red;
        a900.aspect.insert(QStringLiteral("A900"), 1);
        a900.aspect.insert(QStringLiteral("A904"), 2);
        QHash<QString, qint64> before = lsrp;
        before.insert(QStringLiteral("LAST_RFID_TAG"), 898);
        before.insert(QStringLiteral("ABS_LOCO_LOC"), 162990);   // 10 m past A900 (block 2: 900 at 162980)
        StationKavach::Loco l5;
        const StationKavach::Answer p900 = StationKavach::answer(st, a900, &l5, before, enc);
        CHECK(p900.ok && maField(p900, "DEST_LOCO_SOS") == 4 && maField(p900, "APPR_SIG_DIST") == 260
                  && p900.note.startsWith(QStringLiteral("SPAD: loco 10 m past A900 at Red")),
              "last tag 898 on S43-A900, 10 m past A900 at Red: SPAD at A900, 260 m from 898");
        StationKavach::Station rev = st;
        rev.routes[2].pktDir = 2;
        QHash<QString, qint64> back = lsrp;
        back.insert(QStringLiteral("MOVEMENT_DIR"), 2);
        StationKavach::Loco l6;
        CHECK(maField(StationKavach::answer(rev, red, &l6, back, enc), "DEST_LOCO_SOS") == 0,
              "the route run Reverse: 163821 m is behind tag 902, not past A904");
    }

    // ---- Packet Maker answering as the station: the tag locations come from the layout file ----
    {
        QTemporaryDir tmp;
        StationLayout::Layout lay = s30;
        StationKavach::Station file = st;
        file.tagLocs.clear();
        for (StationKavach::Signal &g : file.signalList) if (g.name == QLatin1String("A904")) g.aspect = 1;
        lay.kavach = StationKavach::toJson(file);
        const QString layPath = tmp.filePath(QStringLiteral("s30_527.json"));
        CHECK(StationLayout::save(layPath, lay), "Section_30 with station 527's KAVACH data (A904 at Red) saved");
        QString aapLine, arpLine;
        for (const QString &l : lines) {
            if (l.startsWith(QLatin1String("@aap_")) && aapLine.isEmpty()) aapLine = l;
            if (l.startsWith(QLatin1String("@arp_")) && arpLine.isEmpty()) arpLine = l;
        }
        PacketPreset preset;
        preset.captype = QStringLiteral("aap");
        preset.header = rawOf(aapLine);
        const QString presetPath = tmp.filePath(QStringLiteral("aap.json"));
        CHECK(!arpLine.isEmpty() && preset.save(presetPath), "an ARP and the AAP preset from the capture");

        QUdpSocket rx;
        CHECK(rx.bind(QHostAddress(QHostAddress::LocalHost), 0), "loopback receiver binds");
        SessionKeyStore keys;
        PacketMakerDialog dlg(nullptr, nullptr, &keys);
        dlg.findChild<QLineEdit *>(QStringLiteral("pmDestHost"))->setText(QStringLiteral("127.0.0.1"));
        if (QSpinBox *port = dlg.findChild<QSpinBox *>(QStringLiteral("pmDestPort"))) port->setValue(rx.localPort());
        QCheckBox *arm = nullptr;
        for (QCheckBox *cb : dlg.findChildren<QCheckBox *>())
            if (cb->text().contains(QStringLiteral("Arm"), Qt::CaseInsensitive)) arm = cb;
        auto *answer = dlg.findChild<QCheckBox *>(QStringLiteral("pmAnswerLoco"));
        auto *from = dlg.findChild<QComboBox *>(QStringLiteral("pmAnswerFrom"));
        auto *log = dlg.findChild<QTableWidget *>(QStringLiteral("pmSendLog"));
        CHECK(arm && answer && from && log, "the arm switch, the answer box, its source and the log are found");
        if (!(arm && answer && from && log)) return;
        from->setCurrentIndex(1);
        dlg.findChild<QLineEdit *>(QStringLiteral("pmAnswerAap"))->setText(presetPath);
        dlg.findChild<QLineEdit *>(QStringLiteral("pmAnswerLayout"))->setText(layPath);
        answer->setChecked(true);
        CHECK(answer->isChecked(), "answering as station 527 starts");
        auto entry = [](const QString &line) {
            auto e = LogEntryPtr::create();
            e->text = line;
            e->epochMs = CaptureDecoder::parseLine(line).rtc.toMSecsSinceEpoch();
            e->header.source_id = 21;
            return e;
        };
        dlg.setStationRandomForTest(quint16(preset.header.value("STN_RND_NUM_RS")));
        arm->setChecked(true);
        dlg.observe(entry(arpLine));
        receive(rx);   // the AAP
        dlg.observe(entry(lsrpLine));
        const QByteArray sent = receive(rx).mid(MessageHeader::SIZE);
        const QByteArray real = CaptureDecoder::parseLine(find(lines, "@slrp_", 50549)).bytes;
        Schema::ParsedPacket ours = enc.parseBody(QStringLiteral("slrp"), sent);
        Schema::ParsedPacket theirs = enc.parseBody(QStringLiteral("slrp"), real);
        qint64 sos = -1;
        for (const Schema::SubEntry &s : ours.subs) if (s.type == 0) sos = s.values.value(QStringLiteral("DEST_LOCO_SOS"));
        CHECK(ours.ok && sos == 4 && sent.size() == real.size(), "LSRP 50548 answered SoS SPAD, as long as the real one");
        // The real one's FRAME_NUM is a frame later and its profile id 4 (ours: a loco new to the station, 0).
        theirs.header.insert(QStringLiteral("FRAME_NUM"), ours.header.value(QStringLiteral("FRAME_NUM")));
        theirs.header.insert(QStringLiteral("REF_PROF_ID"), ours.header.value(QStringLiteral("REF_PROF_ID")));
        QString e1, e2;
        CHECK(ours.ok && theirs.ok
                  && enc.encodeBody(QStringLiteral("slrp"), ours.header, ours.subs, &e1)
                         == enc.encodeBody(QStringLiteral("slrp"), theirs.header, theirs.subs, &e2),
              "sent from the layout file: the real one's fields (MAC aside)");
        CHECK(log->item(log->rowCount() - 1, 7)->text().contains(QStringLiteral("SPAD: loco 1 m past A904 at Red")),
              "the send log says SPAD");
    }
}
