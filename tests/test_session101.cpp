#include "testutil.h"

#include "seriallink.h"


#ifdef Q_OS_UNIX
#  include <unistd.h>
#  if defined(Q_OS_MACOS)
#    include <util.h>
#  else
#    include <pty.h>
#  endif
#endif

// =============================================================================
//  Session 101 — three serial bugs found in testing.
//
//  1. A log line printed in two parts, 400 ms apart, was cut in two by the
//     300 ms idle flush; neither half decodes.
//  2. "login:" flushed on idle, then its CR LF arriving, gave a stray empty
//     line.
//  3. A port whose far end vanished reported ReadError (not ResourceError)
//     in a tight loop and never closed.
//
//  The @dop1 line is real: replay/loco_2_1_29062026_134128.cap.
// =============================================================================

namespace {

const QByteArray kDop1 =
    "@dop1_2_1 2026-06-29T13:41:29 14034 0C 17 17 17 17 0C 0C 0C 0C 17 17 17 17 17 17 17";

}  // namespace

TEST_SUITE(session101)
{
    // ---- 1. a log line in two printfs is one line ----------------------------
    {
        // Split where the tester split it: "… 0C 17 17 " | "17 17 0C 0C …".
        const int cut = kDop1.indexOf("17 17 0C 0C");
        CHECK(cut > 0, "fixture: the real line has the tester's split point");
        SerialLineSplitter sp;
        CHECK(sp.feed(kDop1.left(cut), 1000).isEmpty(), "first half waits");
        CHECK(sp.flushIdle(1400, 300).isEmpty(),
              "400 ms between two printfs does not cut an @ log line");
        auto done = sp.feed(kDop1.mid(cut) + "\r\n", 1400);
        CHECK(done.size() == 1 && done[0].text == kDop1,
              "the second half completes the same line, byte for byte");
        CHECK(done.size() == 1 && done[0].firstByteMs == 1000,
              "…stamped with the first half's time");

        // Still delivered if the card really did stop mid-line.
        sp.feed("@dop1_2_1 2026-06-29T13:41:30 14035 0C", 2000);
        CHECK(sp.flushIdle(2000 + 4999, 300).isEmpty(), "an @ partial is held under 5 s");
        auto late = sp.feed(QByteArray(), 0);
        CHECK(late.isEmpty(), "(feeding nothing changes nothing)");
        auto held = sp.flushIdle(2000 + SerialLineSplitter::kLogLineIdleMs, 300);
        CHECK(held.size() == 1 && held[0].text.startsWith("@dop1"),
              "…and delivered once the card has been quiet 5 s");

        // Closing the link flushes regardless (idleMs 0).
        sp.feed("@dop1 partial", 9000);
        CHECK(sp.flushIdle(9000, 0).size() == 1, "a forced flush ignores the @ hold");

        // A prompt is still a prompt.
        sp.feed("login:", 10000);
        CHECK(sp.flushIdle(10300, 300).size() == 1, "a non-@ partial still flushes at 300 ms");
    }

    // ---- 2. no stray empty line after a flushed prompt -----------------------
    {
        SerialLineSplitter sp;
        sp.feed("login:", 0);
        auto p = sp.flushIdle(300, 300);
        CHECK(p.size() == 1 && p[0].text == "login:", "prompt flushed on idle");
        CHECK(sp.feed("\r\n", 600).isEmpty(), "its CR LF ends it, not a new empty line");
        auto next = sp.feed("ok\r\n", 700);
        CHECK(next.size() == 1 && next[0].text == "ok", "the next line is normal");

        sp.feed("Password:", 1000);
        sp.flushIdle(1300, 300);
        CHECK(sp.feed("\r", 1500).isEmpty(), "a lone CR after a flush is swallowed");
        CHECK(sp.feed("\n", 1510).isEmpty(), "…and the LF after it too (one line end)");

        sp.feed("x>", 2000);
        sp.flushIdle(2300, 300);
        CHECK(sp.feed("\n", 2400).isEmpty(), "a lone LF after a flush is swallowed");
        auto blank = sp.feed("\r\n", 2500);
        CHECK(blank.size() == 1 && blank[0].text.isEmpty(),
              "a SECOND line end is a genuine blank line");

        sp.feed("y>", 3000);
        sp.flushIdle(3300, 300);
        auto more = sp.feed(" more\r\n", 3400);
        CHECK(more.size() == 1 && more[0].text == " more",
              "text after a flush is a new line, nothing swallowed");

        // Unchanged behaviour (session 85).
        SerialLineSplitter q;
        auto d = q.feed("a\r\n\r\nb\r\n", 0);
        CHECK(d.size() == 3 && d[1].text.isEmpty(), "blank lines without a flush still count");
    }

    // ---- 3. a port that dies is closed, not left firing errors ---------------
    {
        SerialLink link;
        int closedCount = 0, errors = 0;
        QObject::connect(&link, &SerialLink::closed, [&]() { ++closedCount; });
        QObject::connect(&link, &SerialLink::errorOccurred, [&](const QString &) { ++errors; });
        // Not open: errors are reported, nothing to close.
        link.onError(QSerialPort::ReadError);
        CHECK(errors == 1 && closedCount == 0, "an error on a closed link is reported, nothing closes");
    }
#ifdef Q_OS_UNIX
    {
        auto openPty = [](int *master, QString *slavePath) {
            int slave = -1;
            char name[256] = {};
            if (openpty(master, &slave, name, nullptr, nullptr) != 0) return false;
            ::close(slave);                 // the link opens it by name
            *slavePath = QString::fromLocal8Bit(name);
            return true;
        };

        for (const QSerialPort::SerialPortError e : { QSerialPort::ReadError, QSerialPort::WriteError,
                                                      QSerialPort::ResourceError }) {
            int master = -1;
            QString path;
            if (!openPty(&master, &path)) { CHECK(false, "openpty"); break; }
            SerialConfig cfg;
            cfg.portName = path;
            SerialLink link;
            int closedCount = 0;
            QObject::connect(&link, &SerialLink::closed, [&]() { ++closedCount; });
            const bool opened = link.open(cfg);
            CHECK(opened, "a pseudo-terminal opens as a serial port");
            if (opened) {
                link.onError(e);
                CHECK(!link.isOpen() && closedCount == 1,
                      QStringLiteral("error %1 closes the port").arg(int(e)).toLatin1().constData());
            }
            ::close(master);
        }

        // A non-fatal error repeating in a burst is a storm: close.
        {
            int master = -1;
            QString path;
            if (openPty(&master, &path)) {
                SerialConfig cfg;
                cfg.portName = path;
                SerialLink link;
                int errors = 0;
                QObject::connect(&link, &SerialLink::errorOccurred, [&](const QString &) { ++errors; });
                if (link.open(cfg)) {
                    // A recoverable error (not Resource/Read/Write). It was ParityError,
                    // which Qt 6 removed; UnknownError exists on both.
                    link.onError(QSerialPort::UnknownError);
                    CHECK(link.isOpen() && errors == 1, "one recoverable error: reported, port stays open");
                    for (int i = 0; i < 10; ++i) link.onError(QSerialPort::UnknownError);
                    CHECK(link.isOpen() && errors == 1, "a short burst is not re-reported per error");
                    for (int i = 0; i < 20; ++i) link.onError(QSerialPort::UnknownError);
                    CHECK(!link.isOpen(), "20 in 100 ms is a storm: port closed");
                    CHECK(errors == 2 && link.errorText().contains(QLatin1String("port closed")),
                          "…and the reason says so, once");
                }
                ::close(master);
            }
        }

        // Not tested end to end: closing a pty's far end on macOS makes Qt
        // report nothing at all (no error, no readyRead), so the tester's
        // ReadError storm (a virtual port) cannot be reproduced here.
    }
#endif
}
