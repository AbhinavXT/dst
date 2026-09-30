#include "testutil.h"

#include "udpsender.h"
#include "packetmakerdialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QUdpSocket>

// =============================================================================
//  Sending one frame to several destinations.
//
//  A station talks to more than one loco. The decision that shapes everything
//  else: sending the same frame to four peers is ONE transmission with four
//  recipients, not four transmissions. So a tick builds one frame, writes it
//  to every target, and the send index advances ONCE — because FRAME_NUM, the
//  MAC and the message-header seq all derive from that index, and advancing it
//  per destination would hand each peer a stream with holes in it (1, 5, 9 …).
// =============================================================================

TEST_SUITE(multidest)
{
    // Two listeners on loopback stand in for two peers.
    QUdpSocket a, b;
    CHECK(a.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)), "first listener bound");
    CHECK(b.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)), "second listener bound");
    const quint16 pa = a.localPort(), pb = b.localPort();

    UdpSender tx;
    const QVector<UdpSender::Target> both{
        { QStringLiteral("127.0.0.1"), pa },
        { QStringLiteral("127.0.0.1"), pb },
    };

    // ---- one frame reaches both ---------------------------------------------
    {
        QSignalSpy sentSpy(&tx, &UdpSender::sent);
        const QByteArray frame = QByteArray::fromHex("0102030405");
        CHECK(tx.sendOnce(frame, both), "the send reports success");

        CHECK(a.waitForReadyRead(1000), "the first peer receives it");
        CHECK(b.waitForReadyRead(1000), "and so does the second");

        QByteArray ga(int(a.pendingDatagramSize()), 0);
        QByteArray gb(int(b.pendingDatagramSize()), 0);
        a.readDatagram(ga.data(), ga.size());
        b.readDatagram(gb.data(), gb.size());
        CHECK(ga == frame && gb == frame, "both get the same bytes");

        // One transmission, so one 'sent' signal — not one per destination.
        CHECK(sentSpy.count() == 1,
              "one frame to two places counts as one send, not two");
    }

    // ---- the send index advances once per frame ------------------------------
    // This is the part that would be silently wrong: each peer must see a
    // continuous sequence, not every other number.
    {
        QVector<int> indices;
        UdpSender tx2;
        tx2.startInterval(
            [&indices](int i) -> QByteArray {
                indices.push_back(i);
                return QByteArray::fromHex("aa");
            },
            both, 5);

        QElapsedTimer t; t.start();
        while (indices.size() < 3 && t.elapsed() < 3000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        tx2.stop();

        CHECK(indices.size() >= 3, "the interval ran");
        if (indices.size() >= 3) {
            CHECK(indices[0] == 0 && indices[1] == 1 && indices[2] == 2,
                  "the frame index goes 0, 1, 2 — not 0, 2, 4 as it would if "
                  "each destination advanced it");
        }
    }

    // ---- a duplicate destination is one destination --------------------------
    {
        UdpSender tx3;
        tx3.startInterval([](int) { return QByteArray::fromHex("bb"); },
                          { { QStringLiteral("127.0.0.1"), pa },
                            { QStringLiteral("127.0.0.1"), pa } }, 1000);
        CHECK(tx3.targetCount() == 1,
              "the same address twice is one peer, not double every datagram");
        tx3.stop();
    }

    // ---- nothing resolvable is a refusal, not a silent no-op -----------------
    {
        UdpSender tx4;
        QSignalSpy errSpy(&tx4, &UdpSender::error);
        tx4.startInterval([](int) { return QByteArray::fromHex("cc"); },
                          { { QStringLiteral("nope.invalid.example"), 20000 } }, 100);
        CHECK(!tx4.isRunning(),
              "a run with no reachable destination does not start");
        CHECK(errSpy.count() >= 1, "and says why");
        tx4.stop();
    }

    // ---- one bad destination does not silence the good one -------------------
    {
        UdpSender tx5;
        tx5.startInterval([](int) { return QByteArray::fromHex("dd"); },
                          { { QStringLiteral("nope.invalid.example"), 20000 },
                            { QStringLiteral("127.0.0.1"), pa } }, 50);
        CHECK(tx5.isRunning(),
              "an unreachable peer is dropped and the reachable one still runs");
        CHECK(tx5.targetCount() == 1, "with only the resolvable target kept");
        tx5.stop();
    }

    // ---- the dialog's four rows ----------------------------------------------
    {
        PacketMakerDialog dlg;
        QGroupBox *more = nullptr;
        for (QGroupBox *g : dlg.findChildren<QGroupBox *>()) {
            if (g->title().contains(QStringLiteral("Also send to"))) { more = g; }
        }
        CHECK(more != nullptr, "the extra destinations have a home");
        CHECK(more && more->isCheckable() && !more->isChecked(),
              "folded away by default, so a single-peer send looks unchanged");

        int boxes = 0;
        for (QCheckBox *cb : more->findChildren<QCheckBox *>()) {
            if (cb->text().contains(QStringLiteral("Destination"))) { ++boxes; }
        }
        CHECK(boxes == 3, "three more destinations, four in total");
    }
}
