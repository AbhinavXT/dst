#include "testutil.h"

#include "seriallink.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>

#ifdef Q_OS_UNIX
#  include <termios.h>
#  include <unistd.h>
#  if defined(Q_OS_MACOS)
#    include <util.h>
#  else
#    include <pty.h>
#  endif
#endif

// =============================================================================
//  Session 103 — the serial port is read on its own thread.
//
//  Before, the port was read on the GUI thread: while the GUI was busy the
//  bytes waited in the driver and were stamped when the GUI got round to
//  them. The IOA's logs are compared against the VCC's by time, so a late
//  stamp is a wrong answer, not a cosmetic one.
//
//  Real @dop1 line: replay/loco_2_1_29062026_134128.cap.
// =============================================================================

namespace {

const QByteArray kDop1 =
    "@dop1_2_1 2026-06-29T13:41:29 14034 0C 17 17 17 17 0C 0C 0C 0C 17 17 17 17 17 17 17";

void settle(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

}  // namespace

TEST_SUITE(session103)
{
    // ---- pure parts ----------------------------------------------------------
    CHECK(serialLatencyTimerPath(QStringLiteral("/dev/ttyUSB0"))
              == QLatin1String("/sys/bus/usb-serial/devices/ttyUSB0/latency_timer"),
          "the Linux latency timer of an FTDI port");
    CHECK(serialLatencyTimerPath(QStringLiteral("COM3")).isEmpty(), "none for a Windows name");
    {
        QTemporaryDir dir;
        QSettings s(dir.filePath(QStringLiteral("t.ini")), QSettings::IniFormat);
        SerialConfig c; c.portName = QStringLiteral("COM5"); c.lowLatency = false;
        c.save(s, QStringLiteral("g"));
        CHECK(!SerialConfig::load(s, QStringLiteral("g")).lowLatency, "low latency off is remembered");
        CHECK(SerialConfig::load(s, QStringLiteral("none")).lowLatency, "and defaults on");
    }

    {
        SerialLink link;
        CHECK(link.readerThread() && link.readerThread()->isRunning()
                  && link.readerThread() != QThread::currentThread(),
              "each link reads on a thread of its own, not the GUI's");
    }

#ifdef Q_OS_UNIX
    int master = -1, slave = -1;
    char name[256] = {};
    if (::openpty(&master, &slave, name, nullptr, nullptr) != 0) { CHECK(false, "openpty"); return; }
    termios t{};
    tcgetattr(master, &t);
    cfmakeraw(&t);
    tcsetattr(master, TCSANOW, &t);
    ::close(slave);

    {
        // Declared before the link: its destructor closes the port, which
        // delivers into these.
        QVector<qint64> stamps;
        QList<QByteArray> lines;
        QStringList order;
        SerialLink link;
        QObject::connect(&link, &SerialLink::lineReceived, [&](const QByteArray &l, qint64 ms) {
            lines << l; stamps << ms; order << QStringLiteral("line");
        });
        QObject::connect(&link, &SerialLink::closed, [&]() { order << QStringLiteral("closed"); });

        SerialConfig c;
        c.portName = QString::fromLocal8Bit(name);
        c.baud = 9600;
        CHECK(link.open(c), "opens a pseudo-terminal");
        CHECK(!link.latencyNote().isEmpty(), "the low-latency request reports what became of it");

        // The card prints while the GUI thread is stuck for 600 ms.
        const QByteArray b = kDop1 + "\r\n";
        const qint64 printedAt = QDateTime::currentMSecsSinceEpoch();
        (void)::write(master, b.constData(), size_t(b.size()));
        QThread::msleep(600);                    // the GUI, busy: no events processed
        settle(100);
        CHECK(lines.size() == 1 && lines[0] == kDop1, "the line arrives whole");
        const qint64 lag = stamps.value(0) - printedAt;
        CHECK(lines.size() == 1 && lag >= 0 && lag < 200,
              QByteArray("stamped when it arrived, not when the busy GUI got to it (lag ")
                  + QByteArray::number(lag) + " ms, the GUI was blocked 600 ms)");

        // write / counters across the thread boundary.
        CHECK(link.write("STATUS?\r\n") == 9 && link.txBytes() == 9, "writes go out through the reader thread");
        CHECK(link.rxBytes() == quint64(b.size()), "RX counted on the reader thread");
        link.resetCounters();
        CHECK(link.rxBytes() == 0 && link.txBytes() == 0, "counters reset");

        // A partial line is delivered on close, and before closed().
        (void)::write(master, "@dop1_2_1 partial", 17);
        settle(50);
        order.clear();
        link.close();
        CHECK(order == QStringList({ QStringLiteral("line"), QStringLiteral("closed") })
                  && lines.last() == QByteArray("@dop1_2_1 partial"),
              "the held partial line arrives on close, before closed()");

        c.lowLatency = false;
        CHECK(link.open(c) && link.latencyNote().isEmpty(), "low latency off: nothing asked, nothing said");
        // Destroyed while open: the thread is stopped and joined, no hang.
    }
    ::close(master);
#endif
}
