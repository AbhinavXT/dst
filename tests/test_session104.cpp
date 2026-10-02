#include "testutil.h"

#include "messagedispatcher.h"
#include "serialconsolewindow.h"
#include "serialmanager.h"
#include "uicolors.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QLabel>

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
//  Session 104 — line health per port.
//
//  Lines per second, the longest gap, the share of lines that decode, and
//  driver errors. A wrong baud rate shows up at once as ~0% decoding.
//
//  Real lines: replay/loco_2_1_29062026_134128.cap (@dop1) and
//  replay/loco_1_1_27062026_140226.cap (@lsrp).
// =============================================================================

namespace {

const QByteArray kDop1 =
    "@dop1_2_1 2026-06-29T13:41:29 14034 0C 17 17 17 17 0C 0C 0C 0C 17 17 17 17 17 17 17";
const QByteArray kLsrp =
    "@lsrp_1_1 2026-06-27T14:02:27 21441 02 07 0A 00 27 00 00 00 0F 02 A3 AC 57 "
    "40 00 01 40 9F FB 41 E0 F0 7D 00 10 FC 30 15 20 8D 00 F2 F3 26 DD C6 ED 59 9B";
// What a 115200 line looks like read at 9600: bytes, no '@', no shape.
const QByteArray kWrongBaud = QByteArray::fromHex("8f e3 1c f0 00 7e 9b c3 f8 06 e0 fe 18");

void settle(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

}  // namespace

TEST_SUITE(session104)
{
    // ---- what "decodes" means ------------------------------------------------
    CHECK(SerialLineHealth::lineDecodes(kDop1), "a real @dop1 line decodes");
    CHECK(SerialLineHealth::lineDecodes(kLsrp), "a real @lsrp line decodes");
    CHECK(!SerialLineHealth::lineDecodes(kWrongBaud), "wrong-baud garbage does not");
    CHECK(!SerialLineHealth::lineDecodes("login:"), "a prompt is not a capture line");
    CHECK(!SerialLineHealth::lineDecodes("@zzz_1_1 2026-06-29T13:41:29 1 00"),
          "an @ line of a type the console does not know does not count");

    // ---- the arithmetic ------------------------------------------------------
    {
        SerialLineHealth h;
        CHECK(h.stats(0).lines == 0 && h.stats(0).sinceLastMs == -1 && h.stats(0).decodePercent() == 100,
              "no lines: nothing to judge");
        // 10 lines a second for 4 s, one 2.5 s gap in the middle.
        qint64 t = 1000;
        for (int i = 0; i < 20; ++i) { h.note(true, t); t += 100; }
        t += 2500;
        for (int i = 0; i < 20; ++i) { h.note(true, t); t += 100; }
        const SerialLineHealth::Stats s = h.stats(t);
        CHECK(s.lines == 40 && s.decodePercent() == 100, "40 lines, all decoding");
        CHECK(s.longestGapMs == 2600, "the longest gap is the 2.5 s pause (plus the 100 ms step)");
        // The window is (2500, 7500]: the last five of the first batch
        // (2500..2900) and all twenty of the second.
        CHECK(qAbs(s.perSecond - 5.0) < 1e-9, "rate over the last 5 s: 25 lines in the window = 5.0/s");
        CHECK(h.stats(t + 60000).perSecond == 0.0, "a minute of silence: 0 lines/s");
        CHECK(h.stats(t + 60000).sinceLastMs == 60100, "…and the time since the last line says so");
        CHECK(!s.failing(), "healthy is not failing");
    }
    {
        SerialLineHealth h;
        for (int i = 0; i < SerialLineHealth::kJudgeAfter - 1; ++i) h.note(false, i);
        CHECK(!h.stats(100).failing(), "fewer than 10 lines: not judged yet");
        h.note(false, 50);
        CHECK(h.stats(100).failing() && h.stats(100).decodePercent() == 0, "10 garbage lines: failing");
        for (int i = 0; i < 30; ++i) h.note(true, 100 + i);
        CHECK(!h.stats(200).failing(), "once most lines decode, no longer failing (75%)");
    }

    // ---- driver errors are all counted, even inside a burst ----------------
    {
        SerialLink link;
        for (int i = 0; i < 5; ++i) link.onError(QSerialPort::UnknownError);
        CHECK(link.errorCount() == 5, "five errors in a burst count five, though reported once");
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
    auto print = [master](const QByteArray &line) {
        const QByteArray b = line + "\r\n";
        (void)::write(master, b.constData(), size_t(b.size()));
    };
    {
        MessageDispatcher disp;
        SerialManager mgr(&disp);
        SerialConsoleWindow w(&mgr);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        SerialConfig c;
        c.portName = QString::fromLocal8Bit(name);
        c.baud = 115200;
        w.setConfigToUi(c);
        CHECK(w.openPort(), "opens");

        for (int i = 0; i < 6; ++i) { print(kDop1); print(kLsrp); }
        settle(300);
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        SerialLineHealth::Stats h = mgr.health(c.portName, now);
        CHECK(h.lines == 12 && h.decodePercent() == 100, "12 real lines over the wire: 100% decode");
        settle(600);       // the terminal refreshes its labels every 500 ms
        CHECK(w.healthText().contains(QLatin1String("100% decode")),
              QByteArray("the terminal shows it: ") + w.healthText().toUtf8());

        // Now the card at the wrong baud.
        mgr.resetFedLines(c.portName);
        CHECK(mgr.health(c.portName, now).lines == 0, "Reset clears the health too");
        for (int i = 0; i < 12; ++i) print(kWrongBaud);
        settle(900);
        h = mgr.health(c.portName, QDateTime::currentMSecsSinceEpoch());
        CHECK(h.lines == 12 && h.decodePercent() == 0 && h.failing(), "garbage: 0% decode, failing");
        CHECK(w.healthText().contains(QLatin1String("check baud")),
              QByteArray("the terminal says what to check: ") + w.healthText().toUtf8());
        auto *label = w.findChild<QLabel *>(QStringLiteral("serialHealth"));
        CHECK(label && label->styleSheet() == UiColor::warningStyle(), "…in the warning colour");

        // Health counts lines whether or not they are fed to the console.
        mgr.setFeed(c.portName, false);
        print(kDop1);
        settle(200);
        CHECK(mgr.health(c.portName, QDateTime::currentMSecsSinceEpoch()).lines == 13,
              "Feed console off: the line still counts for health");

        // A new session starts clean.
        w.closePort();
        CHECK(w.openPort() && mgr.health(c.portName, QDateTime::currentMSecsSinceEpoch()).lines == 0,
              "reopening the port starts its health afresh");
        w.closePort();
    }
    ::close(master);
#endif
}
