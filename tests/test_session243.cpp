#include "testutil.h"

#include "logentry.h"
#include "packetmakerdialog.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QNetworkDatagram>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QUdpSocket>

// =============================================================================
//  Session 243 — Packet Maker's reply watch. After each send, the send log's
//  Reply column says what came back: the first frame of a watched type and
//  how soon, or a watched field's change ("LOCO_MODE 2 → 6 after 300 ms"),
//  or that nothing did within the window. The loco frames are two real
//  consecutive LSRPs from replay/loco_1_1_26062026_162418.cap where
//  LOCO_MODE goes 2 (Staff_Responsible) → 6 (On_Sight).
// =============================================================================

namespace {

const char kSlrpRef6[] =
    "91 B9 AA E6 04 1E 80 00 08 40 6F 8E 68 01 80 00 00 00 00 01 7C AA 08 20 "
    "00 11 63 0D 0C 58 10 B1 00 F4 84 00 22 05 09 BA 20 14 00 00 34 C6 08 5A "
    "40 F4 93 00 54 E8 60 00 00 87 BC 04 99 E0 1A 3E 80 80 F1 02 84 38 0C 11 "
    "30 38 22 41 08 7A 04 83 23 8A 29 0E 08 41 38 40 95 0A 60 61 64 E0 00 03 "
    "43 38 68 60 42 C0 3E A4 18 46 67 50 A7 6E";
const char kLsrpMode2[] =
    "@lsrp_1_1 2026-06-26T16:29:16 1112 02 07 0A 00 27 00 00 00 0F 02 A3 AE 7D D0 00 01 40 9D "
    "5C 81 20 90 7D 0C 90 40 50 0D 00 00 00 78 34 A8 37 56 19 1D 46";
const char kLsrpMode6[] =
    "@lsrp_1_1 2026-06-26T16:29:18 1139 02 07 0A 00 27 00 00 00 0F 02 A3 AE 7D F0 00 01 40 9D "
    "63 41 40 A0 7D 0B 50 C0 50 0D 20 20 10 A4 00 79 73 87 2A 50 E2";

LogEntryPtr entry(const char *line, qint64 epochMs, quint8 source = 21)
{
    auto e = LogEntryPtr::create();
    e->text = QString::fromLatin1(line);
    e->epochMs = epochMs;
    e->header.source_id = source;
    return e;
}

qint64 now() { return QDateTime::currentMSecsSinceEpoch(); }

} // namespace

TEST_SUITE(session243)
{
    QUdpSocket rx;
    CHECK(rx.bind(QHostAddress(QHostAddress::LocalHost), 0), "loopback receiver binds");
    PacketMakerDialog dlg;
    dlg.loadBuffer(QString::fromLatin1(kSlrpRef6));
    QSpinBox *portSpin = nullptr;
    for (QSpinBox *sb : dlg.findChildren<QSpinBox *>())
        if (sb->parent() == &dlg && sb->maximum() == 65535 && sb->value() == 20000) { portSpin = sb; break; }
    QPushButton *build = nullptr, *sendOnce = nullptr;
    for (QPushButton *b : dlg.findChildren<QPushButton *>()) {
        if (b->text().contains(QStringLiteral("Verify")))    build = b;
        if (b->text().contains(QStringLiteral("Send Once"))) sendOnce = b;
    }
    QCheckBox *arm = nullptr;
    for (QCheckBox *cb : dlg.findChildren<QCheckBox *>())
        if (cb->text().contains(QStringLiteral("Arm"), Qt::CaseInsensitive)) { arm = cb; break; }
    auto *log = dlg.findChild<QTableWidget *>(QStringLiteral("pmSendLog"));
    CHECK(portSpin && build && sendOnce && arm && log, "the send controls and the log are found");
    if (!(portSpin && build && sendOnce && arm && log)) return;
    portSpin->setValue(rx.localPort());
    const auto send = [&]() {
        build->click();
        arm->setChecked(true);
        sendOnce->click();
        QElapsedTimer t; t.start();
        while (!rx.hasPendingDatagrams() && t.elapsed() < 2000) { QCoreApplication::processEvents(); rx.waitForReadyRead(20); }
        while (rx.hasPendingDatagrams()) rx.receiveDatagram();
    };
    const auto reply = [&](int row) {
        const QTableWidgetItem *it = log->item(row, 7);
        return it ? it->text() : QString();
    };

    // ---- off by default ---------------------------------------------------------------------
    send();
    CHECK(log->rowCount() == 1 && reply(0).isEmpty(), "with nothing to watch the Reply cell stays empty");

    // ---- a field's change -------------------------------------------------------------------
    dlg.setReplyWatchForTest(QStringLiteral("lsrp, arp"), QStringLiteral("LOCO_MODE"), 1000);
    dlg.observe(entry(kLsrpMode2, now() - 100));               // the value before the send
    send();
    CHECK(reply(1) == QStringLiteral("watching…"), "a send starts a watch");
    dlg.observe(entry(kLsrpMode2, now() + 50));
    CHECK(reply(1) == QStringLiteral("watching…"), "the same LOCO_MODE again is not a reply");
    dlg.observe(entry(kLsrpMode6, now() + 300, PacketMakerDialog::kLoopbackSourceId));
    CHECK(reply(1) == QStringLiteral("watching…"), "the Packet Maker's own loopback is not a reply");
    dlg.observe(entry(kLsrpMode6, now() + 300));
    const QString r1 = reply(1);
    CHECK(r1.startsWith(QStringLiteral("LOCO_MODE 2 → 6 after ")) && r1.endsWith(QLatin1String(" ms (lsrp)")),
          "the change is named with its delay: " + r1.toUtf8());
    const int ms = r1.section(QLatin1Char(' '), 5, 5).toInt();
    CHECK(ms >= 300 && ms < 2300, "the delay is measured from the send (" + QByteArray::number(ms) + " ms)");

    // ---- no change in the window ------------------------------------------------------------
    send();
    dlg.observe(entry(kLsrpMode6, now() + 100));
    dlg.closeExpiredReplies(now() + 1500);
    CHECK(reply(2) == QLatin1String("no change within 1000 ms (LOCO_MODE = 6)"), "window closed: " + reply(2).toUtf8());
    CHECK(reply(1) == r1, "an earlier answer is left alone");

    // ---- the first frame of a type ----------------------------------------------------------
    dlg.setReplyWatchForTest(QStringLiteral("lsrp"), QString(), 1000);
    send();
    dlg.observe(entry("@aap_1_1 2026-06-26T16:29:18 1 00", now() + 10));
    CHECK(reply(3) == QStringLiteral("watching…"), "a type not watched is not a reply");
    dlg.observe(entry(kLsrpMode2, now() + 40));
    CHECK(reply(3).startsWith(QLatin1String("lsrp after ")), "the first lsrp answers: " + reply(3).toUtf8());
    send();
    dlg.closeExpiredReplies(now() + 1500);
    CHECK(reply(4) == QLatin1String("nothing within 1000 ms"), "silence is said: " + reply(4).toUtf8());

    const QString csv = dlg.sendLogCsv();
    CHECK(csv.startsWith(QLatin1String("time,send,packet,frame_num,to,bytes,datagram_hex,reply\n"))
              && csv.contains(QStringLiteral("LOCO_MODE 2 → 6")),
          "the CSV carries the replies");
}
