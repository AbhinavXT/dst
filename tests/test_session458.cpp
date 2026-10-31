#include "testutil.h"

#include "capturedecoder.h"
#include "logentry.h"
#include "messageheader.h"
#include "packetbuilder.h"
#include "packetmakerdialog.h"
#include "packetpreset.h"
#include "sessionkeystore.h"
#include "stationstretch.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QNetworkDatagram>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QUdpSocket>

// =============================================================================
//  Session 458 — a stretch of stations answering one loco as it goes.
//  scripts/kavach_sheets.py, given several sections, writes stretch.json (the
//  station files in the order they lie) and one drawing with the main lines
//  run through. StationStretch routes an ARP to the station it is for and an
//  LSRP to the station the loco is with; a dry run drives a loco along the
//  Down or Up main line.
//  - Committed: a stretch of HBL's NPL-TLPR IB (526) and LPI's S50 / S48
//    routes (527), from the extracts. Down Main: R-134 … R-146, then LPI's
//    R-2 …. The IB's S9/S2D route hands over to TLPR (501), which is not on
//    this stretch: the run says the loco was not handed over before R-2.
//  - Packet Maker given the stretch answers station 527's real ARP and LSRPs
//    (replay/2026-10-08/loco_1_1_08102026_105200.cap) as 527.
//  - Local: the whole pack, GGD 521 → LPI 527, when
//    validate_kavach_sheets.py has written it to
//    tests/fixtures/kavach_sheets/_out/ (the PDFs stay local).
// =============================================================================

namespace {

const char *kFix = DL_SRC_DIR "/schema/fixtures/kavach_sheets/";

QString writeStretch(const QTemporaryDir &tmp, const QStringList &files)
{
    QJsonArray st;
    for (const QString &f : files) st.append(QJsonObject{ { "file", QString::fromLatin1(kFix) + f } });
    const QString path = tmp.filePath(QStringLiteral("stretch.json"));
    QFile out(path);
    out.open(QIODevice::WriteOnly);
    out.write(QJsonDocument(QJsonObject{ { "format", "dlconsole-stretch" }, { "version", 1 }, { "stations", st } }).toJson());
    return path;
}

QStringList capture(const char *name)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/") + QLatin1String(name));
    f.open(QIODevice::ReadOnly);
    return QString::fromLatin1(f.readAll()).split(QLatin1Char('\n'));
}

QHash<QString, qint64> rawOf(const QString &line)
{
    QHash<QString, qint64> raw;
    CaptureDecoder::describe(CaptureDecoder::parseLine(line), nullptr, 0, &raw);
    return raw;
}

QByteArray receive(QUdpSocket &rx)
{
    QElapsedTimer t;
    t.start();
    while (!rx.hasPendingDatagrams() && t.elapsed() < 2000) { QCoreApplication::processEvents(); rx.waitForReadyRead(20); }
    return rx.hasPendingDatagrams() ? rx.receiveDatagram().data() : QByteArray();
}

} // namespace

TEST_SUITE(session458)
{
    QTemporaryDir tmp;
    const QString path = writeStretch(tmp, { QStringLiteral("NPL-TLPR_IB/station_526.json"), QStringLiteral("LPI_routes/station_527.json") });
    CHECK(StationStretch::isStretchFile(path) && !StationStretch::isStretchFile(QString::fromLatin1(kFix) + "LPI_routes/station_527.json"),
          "a stretch file is told from a station file");
    StationStretch::Stretch s;
    QString err;
    CHECK(StationStretch::load(path, &s, &err) && s.members.size() == 2 && s.members[0].station.stationId == 526
              && s.members[1].station.stationId == 527,
          QByteArray("the stretch 526 → 527 loads ") + err.toUtf8());

    // ---- who answers ----
    QString why;
    CHECK(StationStretch::which(s, { { "SOURCE_LOCO_ID", 9 }, { "APPROACHING_STN_ID", 527 } }, true) == 1,
          "an ARP for 527: station 527");
    CHECK(StationStretch::which(s, { { "SOURCE_LOCO_ID", 9 }, { "APPROACHING_STN_ID", 501 } }, true, &why) == -1
              && why.contains(QStringLiteral("501")), "an ARP for 501, not on the stretch: nobody, said");
    CHECK(StationStretch::which(s, { { "SOURCE_LOCO_ID", 9 }, { "LAST_RFID_TAG", 134 } }, false) == 0
              && StationStretch::which(s, { { "SOURCE_LOCO_ID", 9 }, { "LAST_RFID_TAG", 2 } }, false) == 1,
          "an LSRP from a loco with nobody yet: the station whose routes hold its tag");
    s.with.insert(9, 1);
    CHECK(StationStretch::which(s, { { "SOURCE_LOCO_ID", 9 }, { "LAST_RFID_TAG", 134 } }, false) == 1,
          "a loco with 527 (its ARP answered): 527, whatever tag");
    s.with.clear();

    // ---- the Down main line, and a loco along it ----
    int dir = 0;
    const QStringList down = StationStretch::mainLine(s, true, &dir);
    CHECK(dir == 1 && down.value(0) == QStringLiteral("134") && down.indexOf(QStringLiteral("146")) < down.indexOf(QStringLiteral("2"))
              && !down.contains(QStringLiteral("134D")),
          "Down Main: Nominal, the IB's R-134 first, LPI's R-2 after R-146, main tags only");
    PacketBuilder pb;
    const QVector<StationStretch::Step> run = StationStretch::run(s, down, dir, 11, pb.encoder());
    CHECK(!run.isEmpty() && run[0].tag == 134 && run[0].station == 526 && run[0].ok && run[0].ma == 996 + 1042 + 1015
              && run[0].handTo == 501,
          "at R-134, every signal Green: 526 answers, MA 3053 m, handing over to TLPR (501)");
    int at2 = -1;
    for (int i = 0; i < run.size(); ++i) if (run[i].tag == 2) at2 = i;
    CHECK(at2 > 0 && run[at2].station == 527 && run[at2].event.contains(QStringLiteral("not handed over"))
              && run[at2].event.contains(QStringLiteral("527")),
          "at LPI's R-2: 527 answers; 501 is not on the stretch, so the loco was not handed over, and that is said");

    // ---- Packet Maker answers as the stretch ----
    {
        const QStringList lines = capture("loco_1_1_08102026_105200.cap");
        QUdpSocket rx;
        CHECK(rx.bind(QHostAddress(QHostAddress::LocalHost), 0), "loopback receiver binds");
        SessionKeyStore keys;
        for (const QString &l : lines)
            if (l.startsWith(QLatin1String("@auth_keys"))) keys.observeLine(CaptureDecoder::parseLine(l.trimmed()));
        QString aapLine, arpLine;
        for (const QString &l : lines) {
            if (l.startsWith(QLatin1String("@aap_"))) { aapLine = l.trimmed(); break; }
            if (l.startsWith(QLatin1String("@arp_"))) arpLine = l.trimmed();
        }
        PacketPreset preset;
        preset.captype = QStringLiteral("aap");
        preset.header = rawOf(aapLine);
        preset.header.insert(QStringLiteral("SOURCE_STN_ID"), 999);   // the stretch's station says who it is
        const QString presetPath = tmp.filePath(QStringLiteral("aap.json"));
        CHECK(!arpLine.isEmpty() && preset.save(presetPath), "the real ARP, an AAP preset");

        PacketMakerDialog dlg(nullptr, nullptr, &keys);
        dlg.findChild<QLineEdit *>(QStringLiteral("pmDestHost"))->setText(QStringLiteral("127.0.0.1"));
        if (QSpinBox *port = dlg.findChild<QSpinBox *>(QStringLiteral("pmDestPort"))) port->setValue(rx.localPort());
        QCheckBox *arm = nullptr;
        for (QCheckBox *cb : dlg.findChildren<QCheckBox *>())
            if (cb->text().contains(QStringLiteral("Arm"), Qt::CaseInsensitive)) arm = cb;
        auto *answer = dlg.findChild<QCheckBox *>(QStringLiteral("pmAnswerLoco"));
        auto *from = dlg.findChild<QComboBox *>(QStringLiteral("pmAnswerFrom"));
        CHECK(arm && answer && from, "the arm switch, the answer box and its source");
        if (!(arm && answer && from)) return;
        from->setCurrentIndex(1);
        dlg.findChild<QLineEdit *>(QStringLiteral("pmAnswerAap"))->setText(presetPath);
        dlg.findChild<QLineEdit *>(QStringLiteral("pmAnswerLayout"))->setText(path);
        answer->setChecked(true);
        CHECK(answer->isChecked() && dlg.answeringAsStretch() && dlg.stretch().members.size() == 2,
              "the stretch.json in the layout field: answering as its 2 stations");
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
        const QByteArray aap = receive(rx);
        const Schema::ParsedPacket ap = pb.encoder().parseBody(QStringLiteral("aap"), aap.mid(MessageHeader::SIZE));
        CHECK(!aap.isEmpty() && ap.ok && ap.header.value(QStringLiteral("SOURCE_STN_ID")) == 527
                  && dlg.stretch().with.value(int(rawOf(arpLine).value(QStringLiteral("SOURCE_LOCO_ID"))), -1) == 1,
              "the real ARP: answered by 527 (its AAP says 527, not the preset's 999); the loco is with 527");
        int answered = 0;
        qint64 stn = 0;
        for (const QString &l : lines) {
            if (!l.startsWith(QLatin1String("@lsrp_"))) continue;
            const QHash<QString, qint64> r = rawOf(l);
            if (r.value(QStringLiteral("LAST_RFID_TAG")) != 16 && r.value(QStringLiteral("LAST_RFID_TAG")) != 14) continue;
            dlg.observe(entry(l.trimmed()));
            const QByteArray got = receive(rx);
            if (got.isEmpty()) continue;
            ++answered;
            stn = pb.encoder().parseBody(QStringLiteral("slrp"), got.mid(MessageHeader::SIZE)).header.value(QStringLiteral("SOURCE_STN_ID"));
            if (answered >= 3) break;
        }
        CHECK(answered >= 1 && stn == 527 && dlg.stretchMember() == 1, "its LSRPs at R-14 / R-16: answered by 527's routes");
    }

    // ---- the whole pack (local) ----
    const QString whole = QStringLiteral(DL_SRC_DIR "/tests/fixtures/kavach_sheets/_out/stretch.json");
    if (!QFile::exists(whole)) {
        qInfo("  NOTE session458: %s not here (validate_kavach_sheets.py writes it from the local PDFs): the whole stretch not run",
              qPrintable(whole));
        return;
    }
    StationStretch::Stretch all;
    CHECK(StationStretch::load(whole, &all, &err) && all.members.size() == 8, "the whole stretch: 8 stations");
    QStringList order;
    for (const StationStretch::Member &m : all.members) order << QString::number(m.station.stationId);
    CHECK((order == QStringList{ "521", "522", "523", "524", "525", "526", "501", "527" }), "GGD 521 → … → LPI 527, as they lie");
    const QStringList line = StationStretch::mainLine(all, true, &dir);
    const QVector<StationStretch::Step> steps = StationStretch::run(all, line, dir, 11, pb.encoder());
    QStringList handed, missed;
    for (const StationStretch::Step &x : steps) {
        if (x.event.startsWith(QLatin1String("handed"))) handed << x.event.section(QLatin1Char(' '), -1);
        else if (!x.event.isEmpty()) missed << QStringLiteral("%1@%2").arg(x.station).arg(x.tag);
    }
    qInfo("  session458 whole stretch Down: %d tags; handed over to %s; not handed over: %s", int(steps.size()),
          qPrintable(handed.join(QStringLiteral(", "))), qPrintable(missed.join(QStringLiteral(", "))));
    CHECK(handed.contains(QStringLiteral("522")) && handed.contains(QStringLiteral("526")) && handed.contains(QStringLiteral("501"))
              && handed.contains(QStringLiteral("527")),
          "Down: handed over into 522, 526, 501 and 527");
    CHECK((missed == QStringList{ QStringLiteral("524@306") }),
          "SKP (523) hands over nowhere (its border RFIDs are its own): the loco reaches 524 at R-306 without one, said; "
          "the only one");
    CHECK(handed.contains(QStringLiteral("523")) && handed.contains(QStringLiteral("525")), "and into 523 and 525");
    // Up Main: said (what the sheets give is in the CHANGELOG), not pinned.
    const QStringList up = StationStretch::mainLine(all, false, &dir);
    QStringList uh, um;
    for (const StationStretch::Step &x : StationStretch::run(all, up, dir, 11, pb.encoder())) {
        if (x.event.startsWith(QLatin1String("handed"))) uh << x.event.section(QLatin1Char(' '), -1);
        else if (!x.event.isEmpty()) um << QStringLiteral("%1@%2").arg(x.station).arg(x.tag);
    }
    qInfo("  session458 whole stretch Up (dir %d): %d tags; handed over to %s; not handed over: %s", dir, int(up.size()),
          qPrintable(uh.join(QStringLiteral(", "))), qPrintable(um.join(QStringLiteral(", "))));
    CHECK(dir == 2 && !up.isEmpty(), "Up Main runs Reverse");
}
