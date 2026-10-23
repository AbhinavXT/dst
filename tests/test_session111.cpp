#include "testutil.h"

#include "logmodel.h"
#include "messagedispatcher.h"
#include "serialautobaud.h"
#include "serialconsolewindow.h"
#include "serialmanager.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>

#ifdef Q_OS_UNIX
#  include <poll.h>
#  include <termios.h>
#  include <unistd.h>
#  if defined(Q_OS_MACOS)
#    include <util.h>
#  else
#    include <pty.h>
#  endif
#endif

// =============================================================================
//  Session 111 — find the baud rate.
//
//  The stand-in card is a pty whose far end reads the speed the console set
//  (a pty shares one termios between its two ends) and, like a real card
//  read at the wrong rate, gives garbage unless that speed is its own:
//  57600. At 57600 it prints a real @dop1 line from
//  replay/loco_2_1_29062026_134128.cap.
// =============================================================================

namespace {

const QByteArray kDop1 =
    "@dop1_2_1 2026-06-29T13:41:29 14034 0C 17 17 17 17 0C 0C 0C 0C 17 17 17 17 17 17 17";
const QByteArray kGarbage = QByteArray::fromHex("8fe31cf0007e9bc3f806e0fe18");

#ifdef Q_OS_UNIX
// Run the event loop for `ms`, the card printing a line every 25 ms at
// whatever the console has set — right only at `cardBaud` (0: never right).
//
// Session 329: and only while the console has the port open. A real card's
// bytes on a line nobody is reading are lost, but a pty keeps them -- and a
// closed port's line reads back at its ORIGINAL speed (QSerialPort restores
// the termios on close), so the card printed garbage into the gap between
// Find and the reopen, the pty held it, and the reopened port read it: two
// garbage lines in the console about one run in five. No port open on the
// far end is a hang-up on the master.
void runCard(int master, speed_t cardBaud, int ms, int *linesAtRight = nullptr)
{
    QElapsedTimer t, tick;
    t.start();
    bool first = true;               // a line on entry, then every 25 ms
    while (t.elapsed() < ms) {
        if (!first) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            if (tick.elapsed() < 25) continue;
        }
        first = false;
        tick.restart();
        pollfd pfd{ master, POLLOUT, 0 };
        if (::poll(&pfd, 1, 0) >= 0 && (pfd.revents & POLLHUP)) continue;   // nobody listening
        termios tio{};
        tcgetattr(master, &tio);
        const bool right = cardBaud && cfgetospeed(&tio) == cardBaud;
        const QByteArray line = (right ? kDop1 : kGarbage) + "\r\n";
        (void)::write(master, line.constData(), size_t(line.size()));
        if (right && linesAtRight) ++*linesAtRight;
    }
}
#endif

}  // namespace

TEST_SUITE(session111)
{
    // ---- choosing ----------------------------------------------------------------------
    using R = SerialAutoBaud::Result;
    CHECK(SerialAutoBaud::pickBest({ R{ 9600, 8, 0 }, R{ 57600, 12, 12 }, R{ 115200, 30, 0 } }).rate == 57600,
          "the rate whose lines decode, not the one with the most bytes");
    CHECK(SerialAutoBaud::pickBest({ R{ 9600, 10, 0 }, R{ 115200, 30, 0 } }).rate == 0,
          "nothing decodes: no rate is claimed");
    CHECK(SerialAutoBaud::pickBest({ R{ 9600, 20, 5 }, R{ 19200, 6, 5 } }).rate == 19200,
          "a tie on decoded lines goes to the higher share");
    CHECK(SerialAutoBaud::plainlyRight(R{ 57600, 4, 4 }) && !SerialAutoBaud::plainlyRight(R{ 57600, 2, 2 })
              && !SerialAutoBaud::plainlyRight(R{ 57600, 10, 5 }),
          "plainly right: 3+ lines, 90%+ decoding");
    CHECK(SerialAutoBaud::defaultOrder().first() == 115200 && SerialAutoBaud::defaultOrder().size() == 12,
          "the common rates first, all twelve standard ones");
    CHECK(SerialAutoBaud::summary({ R{ 115200, 9, 0 }, R{ 57600, 12, 12 } }, R{ 57600, 12, 12 })
              == QStringLiteral("57600: 12 of 12 lines decode (115200: 0 of 9)"),
          "the summary gives the winner and how the others did");

#ifdef Q_OS_UNIX
    int master = -1, slave = -1;
    char name[256] = {};
    if (::openpty(&master, &slave, name, nullptr, nullptr) != 0) { CHECK(false, "openpty"); return; }
    termios t{};
    tcgetattr(master, &t);
    cfmakeraw(&t);
    tcsetattr(master, TCSANOW, &t);
    ::close(slave);
    SerialConfig base;
    base.portName = QString::fromLocal8Bit(name);

    // ---- the engine against a card at 57600 --------------------------------------------
    {
        SerialAutoBaud ab;
        QSignalSpy tried(&ab, &SerialAutoBaud::trying);
        QSignalSpy done(&ab, &SerialAutoBaud::finished);
        CHECK(ab.start(base, { 9600, 115200, 57600, 19200 }, 300), "starts");
        CHECK(!ab.start(base), "one at a time");
        QElapsedTimer clock;
        clock.start();
        while (done.isEmpty() && clock.elapsed() < 4000) runCard(master, B57600, 25);
        CHECK(done.size() == 1 && done.at(0).at(0).toBool() && done.at(0).at(1).toInt() == 57600,
              QByteArray("finds 57600: ") + done.value(0).value(2).toString().toUtf8());
        CHECK(tried.size() == 3, "stops as soon as a rate is plainly right (19200 never tried)");
        const QVector<R> res = ab.results();
        CHECK(res.size() == 3 && res[0].decoded == 0 && res[1].decoded == 0 && res[2].lines >= 3,
              "the wrong rates gave lines that did not decode");
    }

    // ---- a card that never prints @ lines ----------------------------------------------
    {
        SerialAutoBaud ab;
        QSignalSpy done(&ab, &SerialAutoBaud::finished);
        ab.start(base, { 9600, 57600 }, 200);
        QElapsedTimer clock;
        clock.start();
        while (done.isEmpty() && clock.elapsed() < 3000) runCard(master, 0, 25);
        CHECK(done.size() == 1 && !done.at(0).at(0).toBool()
                  && done.at(0).at(2).toString().startsWith(QLatin1String("no rate gave decodable @ lines")),
              "no rate decodes: says so, claims none");
    }

    // ---- a port that will not open -----------------------------------------------------
    {
        SerialAutoBaud ab;
        QSignalSpy done(&ab, &SerialAutoBaud::finished);
        SerialConfig gone;
        gone.portName = QStringLiteral("/dev/no-such-port-111");
        ab.start(gone, { 9600, 57600 }, 100);
        CHECK(done.size() == 1 && done.at(0).at(2).toString().contains(QLatin1String("would not open")),
              "a port that will not open stops it at once, saying so");
    }

    // ---- the terminal: Find, then open at it; the console sees no garbage ----------------
    {
        MessageDispatcher disp;
        SerialManager mgr(&disp);
        SerialConsoleWindow w(&mgr);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        SerialConfig c = base;
        c.baud = 9600;
        w.setConfigToUi(c);
        CHECK(w.openPort(), "open at the wrong rate");
        runCard(master, B57600, 200);
        CHECK(mgr.health(c.portName, QDateTime::currentMSecsSinceEpoch()).decoded == 0,
              "at 9600 nothing decodes (line health says so)");
        LogModel *tab = disp.modelForKey(SerialManager::tabKeyFor(c.portName));

        CHECK(w.findBaud({ 115200, 57600, 9600 }, 300), "Find");
        CHECK(!w.link()->isOpen(), "the port is closed while it looks");
        // Session 329: counted once the 9600 session's last lines are in --
        // a close hands them back, but through the event loop, and counting
        // before they landed read one of them as the probe's.
        QCoreApplication::processEvents();
        disp.drainNow();
        const int fedBefore = tab->count();
        QElapsedTimer clock;
        clock.start();
        while (w.autoBaud()->isRunning() && clock.elapsed() < 4000) runCard(master, B57600, 25);
        QCoreApplication::processEvents();
        disp.drainNow();
        // The reopen at the found rate does not wait for this line, so the
        // card's first good lines may already be in: what must not be is a
        // line of the probe's garbage.
        int garbage = 0;
        for (int i = fedBefore; i < tab->count(); ++i)
            if (!tab->entryAt(i)->text.startsWith(QLatin1Char('@'))) ++garbage;
        CHECK(garbage == 0, "the probe's garbage never reached the console tab");
        // Session 156: the open after a find does not wait for the driver.
        while (!w.link()->isOpen() && clock.elapsed() < 6000) runCard(master, B57600, 25);
        CHECK(w.link()->isOpen() && w.link()->config().baud == 57600 && w.configFromUi().baud == 57600,
              "found 57600, set it, and opened at it");
        CHECK(w.receivedText().contains(QLatin1String("baud: 57600:")), "the terminal says what it found");
        // A port opened mid-line may read one partial line, as from a real
        // card; the lines after it are what must decode.
        runCard(master, B57600, 100);
        const SerialLineHealth::Stats before = mgr.health(c.portName, QDateTime::currentMSecsSinceEpoch());
        int good = 0;
        runCard(master, B57600, 200, &good);
        const SerialLineHealth::Stats after = mgr.health(c.portName, QDateTime::currentMSecsSinceEpoch());
        CHECK(good > 0 && after.lines > before.lines && after.decoded - before.decoded == after.lines - before.lines,
              "and now the card's lines decode");
        w.closePort();
    }
    ::close(master);
#endif
}
