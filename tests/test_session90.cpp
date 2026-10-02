#include "testutil.h"

#include "udpcommunication.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QThread>
#include <QUdpSocket>

#include <functional>

// =============================================================================
//  Session 90: UDP drops counted by cause — malformed datagrams (network or
//  sender) apart from queue-full drops (this PC fell behind) — through a real
//  socket on loopback.
// =============================================================================

namespace {

QByteArray frame90(quint8 dst, quint16 len, const QByteArray &body)
{
    #pragma pack(push, 1)   // portable packing; __attribute__((packed)) is GCC-only (session 115)
    struct { quint8 src, dst, mid; quint16 len, kv; } h{ 33, dst, 7, len, 1 };
    #pragma pack(pop)
    return QByteArray(reinterpret_cast<const char *>(&h), 7) + body;
}

bool waitFor90(const std::function<bool()> &done, int ms = 2000)
{
    QElapsedTimer t; t.start();
    while (!done() && t.elapsed() < ms) { QCoreApplication::processEvents(); QThread::msleep(5); }
    return done();
}

}  // namespace

TEST_SUITE(session90)
{
    // ---- the words the operator sees ---------------------------------------------------------
    CHECK(dropStatusText(0, 0) == QLatin1String("Drops: 0"), "no drops: as before");
    CHECK(dropStatusText(3, 0) == QStringLiteral("Drops: 3 malformed"), "only malformed: says so");
    CHECK(dropStatusText(0, 120) == QStringLiteral("Drops: 120 queue full"), "only queue full: says so");
    CHECK(dropStatusText(3, 120) == QStringLiteral("Drops: 3 malformed \u00B7 120 queue full"), "both, each named");
    const QString tip = dropStatusTooltip(3, 120);
    CHECK(tip.contains(QLatin1String("network or sender")) && tip.contains(QLatin1String("PC problem"))
              && tip.contains(QLatin1String("3")) && tip.contains(QLatin1String("120")),
          "the tooltip says which cause points where");
    CHECK(dropBannerLines(0, 0).isEmpty(), "no new drops: no banner");
    const QStringList both = dropBannerLines(2, 5);
    CHECK(both.size() == 2 && both[0].contains(QLatin1String("2 malformed")) && both[1].contains(QLatin1String("5 message(s)"))
              && both[1].contains(QLatin1String("queue full")),
          "new drops of both kinds: one banner row per cause");
    CHECK(dropBannerLines(0, 4).size() == 1 && dropBannerLines(4, 0).size() == 1, "one cause: one row");

    // ---- malformed, through a real socket ----------------------------------------------------
    {
        UDPCommunication recv(50190, 50000, nullptr);
        bool bound = false;
        QObject::connect(&recv, &UDPCommunication::bindSucceeded, [&](quint16) { bound = true; });
        recv.start();
        CHECK(waitFor90([&] { return bound; }), "receiver bound");
        QUdpSocket s;
        s.writeDatagram(QByteArray(3, 'x'), QHostAddress::LocalHost, 50190);                       // shorter than a header
        s.writeDatagram(QByteArray(9000, 'x'), QHostAddress::LocalHost, 50190);                    // oversize
        s.writeDatagram(frame90(101, 50, QByteArray("short")), QHostAddress::LocalHost, 50190);    // message_len overruns
        s.writeDatagram(frame90(55, 4, QByteArray("nope")), QHostAddress::LocalHost, 50190);       // not for us: not a drop
        s.writeDatagram(frame90(101, 4, QByteArray("good")), QHostAddress::LocalHost, 50190);      // good
        CHECK(waitFor90([&] { return recv.malformedCount() == 3 && recv.receivedCount() == 1; }),
              QByteArray("three bad datagrams are malformed, the good one is received (malformed ")
                  + QByteArray::number(recv.malformedCount()) + ", received " + QByteArray::number(recv.receivedCount()) + ")");
        CHECK(recv.queueFullCount() == 0, "none of that is the PC's fault");
        CHECK(recv.droppedCount() == 3, "the total is the sum, as before");
        recv.quit(); recv.wait(2000);
    }

    // ---- queue full: a GUI side that does not consume -----------------------------------------
    {
        UDPCommunication recv(50191, 2, nullptr);      // at most 2 in flight
        bool bound = false;
        QObject::connect(&recv, &UDPCommunication::bindSucceeded, [&](quint16) { bound = true; });
        recv.start();
        CHECK(waitFor90([&] { return bound; }), "second receiver bound");
        QUdpSocket s;
        for (int i = 0; i < 5; ++i) {
            s.writeDatagram(frame90(101, 4, QByteArray("good")), QHostAddress::LocalHost, 50191);
            QThread::msleep(5);
        }
        CHECK(waitFor90([&] { return recv.receivedCount() + recv.queueFullCount() == 5; }),
              "every good datagram is either taken or counted");
        CHECK(recv.receivedCount() == 2 && recv.queueFullCount() == 3 && recv.malformedCount() == 0,
              QByteArray("nothing consumed, limit 2: two taken, three queue-full, none malformed (")
                  + QByteArray::number(recv.receivedCount()) + "/" + QByteArray::number(recv.queueFullCount()) + ")");
        recv.notifyMessagesConsumed(2);
        s.writeDatagram(frame90(101, 4, QByteArray("good")), QHostAddress::LocalHost, 50191);
        CHECK(waitFor90([&] { return recv.receivedCount() == 3; }) && recv.queueFullCount() == 3,
              "once the GUI catches up, messages are taken again");
        recv.quit(); recv.wait(2000);
    }
}
