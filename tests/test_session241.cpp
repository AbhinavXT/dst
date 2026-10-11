#include "testutil.h"

#include "packetmakerdialog.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QNetworkDatagram>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QUdpSocket>

// =============================================================================
//  Session 241 — Packet Maker's send log. Every datagram that leaves the
//  window is a row: time, send number, packet, FRAME_NUM, where it reached,
//  size and the exact bytes (header included); Export writes it as CSV. The
//  frame is the bufferload suite's real SLRP reference frame 6 (replay/),
//  sent over loopback.
// =============================================================================

namespace {

const char kSlrpRef6[] =
    "91 B9 AA E6 04 1E 80 00 08 40 6F 8E 68 01 80 00 00 00 00 01 7C AA 08 20 "
    "00 11 63 0D 0C 58 10 B1 00 F4 84 00 22 05 09 BA 20 14 00 00 34 C6 08 5A "
    "40 F4 93 00 54 E8 60 00 00 87 BC 04 99 E0 1A 3E 80 80 F1 02 84 38 0C 11 "
    "30 38 22 41 08 7A 04 83 23 8A 29 0E 08 41 38 40 95 0A 60 61 64 E0 00 03 "
    "43 38 68 60 42 C0 3E A4 18 46 67 50 A7 6E";

} // namespace

TEST_SUITE(session241)
{
    QUdpSocket rx;
    CHECK(rx.bind(QHostAddress(QHostAddress::LocalHost), 0), "loopback receiver binds");

    PacketMakerDialog dlg;
    dlg.loadBuffer(QString::fromLatin1(kSlrpRef6));
    QCoreApplication::processEvents();

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
    CHECK(log->rowCount() == 0, "the log starts empty");

    auto receive = [&rx]() -> QByteArray {
        QElapsedTimer t; t.start();
        while (!rx.hasPendingDatagrams() && t.elapsed() < 2000) {
            QCoreApplication::processEvents();
            rx.waitForReadyRead(20);
        }
        return rx.hasPendingDatagrams() ? rx.receiveDatagram().data() : QByteArray();
    };

    QByteArray got[2];
    for (int i = 0; i < 2; ++i) {
        build->click();
        arm->setChecked(true);
        QCoreApplication::processEvents();
        sendOnce->click();
        got[i] = receive();
    }
    CHECK(!got[0].isEmpty() && !got[1].isEmpty(), "two datagrams arrived");
    CHECK(log->rowCount() == 2, "two rows logged");
    if (log->rowCount() == 2) {
        const QString hex = log->item(1, 6)->text().remove(QLatin1Char(' '));
        CHECK(QByteArray::fromHex(hex.toLatin1()) == got[1], "the logged bytes are the bytes that arrived, header included");
        CHECK(log->item(1, 5)->text().toInt() == got[1].size(), "size column");
        CHECK(log->item(1, 2)->text() == QLatin1String("slrp"), "packet column");
        CHECK(log->item(1, 4)->text() == QStringLiteral("127.0.0.1:%1").arg(rx.localPort()),
              "To: " + log->item(1, 4)->text().toUtf8());
        bool num = false; log->item(1, 3)->text().toLongLong(&num);
        CHECK(num, "FRAME_NUM column holds the number built");
    }
    const QStringList csv = dlg.sendLogCsv().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    CHECK(csv.size() == 3 && csv[0] == QLatin1String("time,send,packet,frame_num,to,bytes,datagram_hex,reply"),
          "CSV: a header and two rows");

    QPushButton *clear = nullptr;
    for (QPushButton *b : log->parentWidget()->findChildren<QPushButton *>())
        if (b->text() == QLatin1String("Clear")) clear = b;
    if (clear) clear->click();
    CHECK(log->rowCount() == 0, "Clear empties the log");
}
