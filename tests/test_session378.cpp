#include "testutil.h"

#include "capturedecoder.h"
#include "logentry.h"
#include "messageheader.h"
#include "packetmakerdialog.h"
#include "packetpreset.h"
#include "sessionkeystore.h"
#include "stationanswer.h"
#include "stationlayout.h"

#include <QCheckBox>
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
//  Session 378 — the Packet Maker answers a running loco: ARP -> AAP (preset,
//  fresh station random, session key from the loco's auth keys), LSRP -> the
//  form's SLRP with FRAME_NUM, LAST_REF_RFID, DIST_PKT_START and APPR_SIG_DIST
//  recomputed. Everything is checked against what the real station sent in
//  replay/2026-10-08/loco_1_1_08102026_105200.cap, placed with the Section_30
//  layout (tag 18 at 166864 m, 906 at 167064 m).
// =============================================================================

namespace {

QStringList capture()
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_105200.cap"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromLatin1(f.readAll()).split(QLatin1Char('\n'));
}

QHash<QString, qint64> rawOf(const QString &line)
{
    QHash<QString, qint64> raw;
    CaptureDecoder::describe(CaptureDecoder::parseLine(line), nullptr, 0, &raw);
    return raw;
}

// The first line of `type` ("@slrp_") with FRAME_NUM `fn` (-1: any), after `from`.
QString find(const QStringList &lines, const char *type, qint64 fn, int from = 0)
{
    for (int i = from; i < lines.size(); ++i)
        if (lines[i].startsWith(QLatin1String(type)) && (fn < 0 || rawOf(lines[i]).value(QStringLiteral("FRAME_NUM")) == fn))
            return lines[i].trimmed();
    return {};
}

QByteArray bytesOf(const QString &line) { return CaptureDecoder::parseLine(line).bytes; }

QByteArray receive(QUdpSocket &rx)
{
    QElapsedTimer t; t.start();
    while (!rx.hasPendingDatagrams() && t.elapsed() < 2000) { QCoreApplication::processEvents(); rx.waitForReadyRead(20); }
    return rx.hasPendingDatagrams() ? rx.receiveDatagram().data() : QByteArray();
}

} // namespace

TEST_SUITE(session378)
{
    const QStringList lines = capture();
    StationLayout::Layout lay;
    CHECK(StationLayout::load(QStringLiteral(":/station_layouts/Section_30.xlsx"), &lay), "Section_30 layout loads");
    const QHash<int, qint64> tags = StationAnswer::tagLocations(lay);
    CHECK(tags.value(18) == 166864 && tags.value(906) == 167064, "tags 18 and 906 placed from the layout");

    const QString slrp18 = find(lines, "@slrp_", 40378);    // ref 18, dps 0, appr 1116
    const QString lsrp906 = find(lines, "@lsrp_", 40380);   // the loco has read tag 906
    const QString slrp906 = find(lines, "@slrp_", 40380);   // the station's answer: ref 906
    CHECK(!slrp18.isEmpty() && !lsrp906.isEmpty() && !slrp906.isEmpty(), "the real SLRP/LSRP/SLRP frames are found");
    const QHash<QString, qint64> lsrp = rawOf(lsrp906), want = rawOf(slrp906);

    // ---- the pure part --------------------------------------------------------
    {
        PacketBuilder b;
        Schema::ParsedPacket pp = b.encoder().parseBody(QStringLiteral("slrp"), bytesOf(slrp18));
        QHash<QString, qint64> h = pp.header;
        QVector<Schema::SubEntry> subs = pp.subs;
        int ref = -1;
        const StationAnswer::Reply r = StationAnswer::slrpFor(lsrp, tags, &ref, &h, &subs);
        CHECK(r.ok, "the LSRP is answered");
        CHECK(ref == 906 && h.value("LAST_REF_RFID") == 906, "ref moves to the tag the loco read (906)");
        CHECK(h.value("DIST_PKT_START") == want.value("DIST_PKT_START") && want.value("DIST_PKT_START") == -200,
              "DIST_PKT_START -200, as the station sent");
        qint64 appr = -1;
        for (const Schema::SubEntry &s : subs) if (s.type == 0) appr = s.values.value("APPR_SIG_DIST");
        CHECK(appr == 916 && appr == want.value("APPR_SIG_DIST"), "APPR_SIG_DIST 916, as the station sent");
        CHECK(h.value("FRAME_NUM") == 40380, "FRAME_NUM from the LSRP");
        CHECK(subs.value(0).values.value("FRAME_OFFSET") == 0 && pp.subs.value(0).values.value("FRAME_OFFSET") == 3,
              "FRAME_OFFSET 3 -> 0: the reply answers that very LSRP");

        // A tag not in the layout keeps the ref in use.
        QHash<QString, qint64> l2 = lsrp; l2.insert("LAST_RFID_TAG", 1000);
        QHash<QString, qint64> h2 = pp.header; QVector<Schema::SubEntry> s2 = pp.subs;
        const StationAnswer::Reply r2 = StationAnswer::slrpFor(l2, tags, &ref, &h2, &s2);
        CHECK(r2.ok && h2.value("LAST_REF_RFID") == 906 && r2.note.contains("1000"), "unknown tag: ref kept, and said");

        // Reverse: the shift flips sign.
        QHash<QString, qint64> h3 = pp.header; h3.insert("PKT_DIR", 2); QVector<Schema::SubEntry> s3 = pp.subs;
        int ref3 = -1;
        const StationAnswer::Reply r3 = StationAnswer::slrpFor(lsrp, tags, &ref3, &h3, &s3);
        CHECK(r3.ok && h3.value("DIST_PKT_START") == 200, "reverse: DIST_PKT_START +200");

        // Tag 900 (167980 m) is the signal; past it the MA cannot be counted.
        QHash<int, qint64> far = tags; far.insert(906, 168100);
        QHash<QString, qint64> h4 = pp.header; QVector<Schema::SubEntry> s4 = pp.subs;
        int ref4 = -1;
        const StationAnswer::Reply r4 = StationAnswer::slrpFor(lsrp, far, &ref4, &h4, &s4);
        CHECK(!r4.ok && r4.why.contains("APPR_SIG_DIST") && h4.value("LAST_REF_RFID") == 18,
              "a ref beyond the signal is refused, the form untouched");

        QHash<int, qint64> none;
        int ref5 = -1;
        CHECK(!StationAnswer::slrpFor(lsrp, none, &ref5, &h4, &s4).ok, "the form's ref not in the layout: refused");
    }

    // ---- the dialog, against the real station's bytes ----------------------------
    QUdpSocket rx;
    CHECK(rx.bind(QHostAddress(QHostAddress::LocalHost), 0), "loopback receiver binds");
    SessionKeyStore keys;
    for (const QString &l : lines)
        if (l.startsWith(QLatin1String("@auth_keys"))) keys.observeLine(CaptureDecoder::parseLine(l.trimmed()));
    CHECK(keys.haveSet(0, 1) && keys.haveSet(1, 1), "loco 1's auth keys are in the store");

    const QString aapLine = find(lines, "@aap_", -1);
    QString arpLine;
    for (const QString &l : lines) {
        if (l.trimmed() == aapLine) break;
        if (l.startsWith(QLatin1String("@arp_"))) arpLine = l.trimmed();
    }
    const QHash<QString, qint64> aapReal = rawOf(aapLine);
    QTemporaryDir tmp;
    PacketPreset preset;
    preset.captype = QStringLiteral("aap");
    preset.header = aapReal;
    const QString presetPath = tmp.filePath(QStringLiteral("aap.json"));
    CHECK(preset.save(presetPath), "the AAP preset is written");

    PacketMakerDialog dlg(nullptr, nullptr, &keys);
    dlg.loadBuffer(slrp18);
    for (QSpinBox *sb : dlg.findChildren<QSpinBox *>())
        if (sb->parent() == &dlg && sb->maximum() == 65535 && sb->value() == 20000) sb->setValue(rx.localPort());
    QCheckBox *arm = nullptr;
    for (QCheckBox *cb : dlg.findChildren<QCheckBox *>())
        if (cb->text().contains(QStringLiteral("Arm"), Qt::CaseInsensitive)) arm = cb;
    auto *answer = dlg.findChild<QCheckBox *>(QStringLiteral("pmAnswerLoco"));
    auto *log = dlg.findChild<QTableWidget *>(QStringLiteral("pmSendLog"));
    CHECK(arm && answer && log, "the arm switch, the answer box and the log are found");
    if (!(arm && answer && log)) return;

    answer->setChecked(true);
    CHECK(!answer->isChecked(), "no AAP preset: answering does not start");
    dlg.findChild<QLineEdit *>(QStringLiteral("pmAnswerAap"))->setText(presetPath);
    dlg.findChild<QLineEdit *>(QStringLiteral("pmAnswerLayout"))->setText(QStringLiteral(":/station_layouts/Section_30.xlsx"));
    answer->setChecked(true);
    CHECK(answer->isChecked(), "with the preset and the layout it starts");

    auto entry = [](const QString &line) {
        auto e = LogEntryPtr::create();
        e->text = line;
        e->epochMs = CaptureDecoder::parseLine(line).rtc.toMSecsSinceEpoch();
        e->header.source_id = 21;
        return e;
    };
    dlg.setStationRandomForTest(quint16(aapReal.value("STN_RND_NUM_RS")));
    dlg.observe(entry(arpLine));
    CHECK(!rx.hasPendingDatagrams() && log->rowCount() == 0, "disarmed: nothing is sent");
    arm->setChecked(true);
    dlg.observe(entry(arpLine));
    const QByteArray aapSent = receive(rx);
    CHECK(aapSent.mid(MessageHeader::SIZE) == bytesOf(aapLine),
          "the AAP is the real station's, byte for byte (MAC included)");
    CHECK(log->rowCount() == 1 && log->item(0, 2)->text() == QLatin1String("aap")
          && log->item(0, 7)->text().contains(QStringLiteral("answers ARP")), "the log names the AAP and what it answers");

    dlg.observe(entry(lsrp906));
    const QByteArray slrpSent = receive(rx);
    CHECK(slrpSent.mid(MessageHeader::SIZE) == bytesOf(slrp906),
          "the SLRP is the real station's answer to that LSRP, byte for byte");
    CHECK(log->rowCount() == 2 && log->item(1, 7)->text().contains(QStringLiteral("ref tag 906")), "the log says what moved");

    const QString otherLoco = QString(lsrp906).replace(QStringLiteral("@lsrp_1_1"), QStringLiteral("@arprecv_1_1"));
    dlg.observe(entry(otherLoco));
    CHECK(!rx.hasPendingDatagrams() && log->rowCount() == 2, "an ARP heard from another loco is not answered");
}
