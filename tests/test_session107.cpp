#include "testutil.h"

#include "logmodel.h"
#include "messagedispatcher.h"
#include "serialconsolewindow.h"
#include "serialmanager.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QElapsedTimer>

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
//  Session 107 — the terminal's display: hex rows, control characters, ANSI.
//
//  The hex view printed whatever chunk a read returned (uneven rows); now it
//  is 16-byte rows with an offset and an ASCII column. Control characters
//  and ANSI colour codes were shown raw; now escape codes are removed and
//  control bytes drawn as symbols — in the terminal only.
//
//  Real @dop1 line: replay/loco_2_1_29062026_134128.cap (its 16 payload
//  bytes are exactly one hex row).
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

TEST_SUITE(session107)
{
    // ---- hex rows ------------------------------------------------------------------
    {
        const QByteArray payload = QByteArray::fromHex(kDop1.mid(kDop1.indexOf(" 0C ") + 1).replace(' ', ""));
        CHECK(payload.size() == 16, "fixture: the real @dop1 payload is 16 bytes");
        CHECK(SerialHexDumper::formatRow(0, payload)
                  == QLatin1String("00000000  0C 17 17 17 17 0C 0C 0C  0C 17 17 17 17 17 17 17  |................|"),
              QByteArray("one row: offset, two groups of eight, ASCII column: ")
                  + SerialHexDumper::formatRow(0, payload).toUtf8());

        SerialHexDumper d;
        // The whole line as a card prints it, in uneven reads.
        const QByteArray wire = kDop1 + "\r\n";
        QVector<SerialHexDumper::Row> rows = d.feed(wire.left(7), 100);
        CHECK(rows.isEmpty() && d.hasPartial(), "7 bytes: no row yet");
        rows += d.feed(wire.mid(7, 30), 105);
        rows += d.feed(wire.mid(37), 110);
        CHECK(rows.size() == wire.size() / 16, "only whole 16-byte rows are emitted");
        CHECK(rows.size() >= 2 && rows[0].text.startsWith(QLatin1String("00000000  40 64 6F 70 31"))
                  && rows[0].text.endsWith(QLatin1String("|@dop1_2_1 2026-0|")),
              QByteArray("row 0: offset 0, '@dop1' in hex and ASCII: ") + rows.value(0).text.toUtf8());
        CHECK(rows.size() >= 2 && rows[1].text.startsWith(QLatin1String("00000010  ")),
              "row 1 at offset 0x10, whatever the reads were");
        CHECK(rows.size() >= 2 && rows[0].firstByteMs == 100 && rows[1].firstByteMs == 105,
              "a row keeps the time of its first byte");
        const int rest = wire.size() % 16;
        const QVector<SerialHexDumper::Row> tail = d.flushPartial();
        CHECK(tail.size() == 1 && tail[0].text.endsWith(QLatin1String("..|"))
                  && tail[0].text.size() == rows[0].text.size() - (16 - rest),
              "the partial row is padded so its ASCII column lines up");
        const QVector<SerialHexDumper::Row> next = d.feed("Z", 200);
        CHECK(next.isEmpty() && d.flushPartial().value(0).text.startsWith(
                  QStringLiteral("%1  5A").arg(wire.size(), 8, 16, QLatin1Char('0'))),
              "after a flush the offset carries on from where it stopped");
    }

    // ---- control characters and ANSI -----------------------------------------------
    CHECK(serialStripAnsi("\x1b[1;31mERROR\x1b[0m") == "ERROR", "CSI colour codes removed");
    CHECK(serialStripAnsi("\x1b]0;card title\x07ready") == "ready", "an OSC title (BEL-ended) removed");
    CHECK(serialStripAnsi("\x1b]2;t\x1b\\ok") == "ok", "an OSC title (ST-ended) removed");
    CHECK(serialStripAnsi("a\x1b" "7b") == "ab", "a two-byte ESC sequence removed");
    CHECK(serialStripAnsi("plain [31m text") == "plain [31m text", "text that only looks like a code is kept");
    CHECK(serialDisplayText("bell\x07") == QStringLiteral("bell␇"), "BEL drawn as ␇");
    CHECK(serialDisplayText(QByteArray("a\0b", 3)) == QStringLiteral("a␀b"), "NUL drawn as ␀");
    CHECK(serialDisplayText("x\x7fy") == QStringLiteral("x␡y"), "DEL drawn as ␡");
    CHECK(serialDisplayText("a\tb") == QStringLiteral("a\tb"), "a tab stays a tab");
    CHECK(serialDisplayText("\x1b[32m" + kDop1 + "\x1b[0m") == QString::fromLatin1(kDop1),
          "a coloured @dop1 line displays as the line");

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
        MessageDispatcher disp;
        SerialManager mgr(&disp);
        SerialConsoleWindow w(&mgr);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.findChild<QCheckBox *>(QStringLiteral("serialTimestamps"))->setChecked(false);
        SerialConfig c;
        c.portName = QString::fromLocal8Bit(name);
        w.setConfigToUi(c);
        CHECK(w.openPort(), "opens");

        const QByteArray coloured = "\x1b[31mRAD LINK FAIL\x1b[0m \x07";
        (void)::write(master, coloured.constData(), size_t(coloured.size()));
        (void)::write(master, "\r\n", 2);
        settle(200);
        CHECK(w.receivedText().contains(QStringLiteral("RAD LINK FAIL ␇"))
                  && !w.receivedText().contains(QChar(0x1b)),
              "the terminal shows the text, the bell as a symbol, and no escape codes");
        disp.drainNow();
        LogModel *m = disp.modelForKey(SerialManager::tabKeyFor(c.portName));
        CHECK(m && m->count() == 1 && m->entryAt(0)->text.contains(QChar(0x1b)),
              "the console tab gets the line exactly as it came");

        w.findChild<QCheckBox *>(QStringLiteral("serialSendHex"))->setChecked(false);
        CHECK(w.sendText(QStringLiteral("STATUS?")), "send");
        CHECK(w.receivedText().contains(QLatin1String("TX> STATUS?"))
                  && !w.receivedText().contains(QChar(0x240D)),
              "the echo shows what was sent, not its line end as symbols");

        auto *hex = w.findChild<QCheckBox *>(QStringLiteral("serialHexView"));
        hex->setChecked(true);
        const QByteArray wire = kDop1 + "\r\n";
        (void)::write(master, wire.constData(), 20);
        settle(15);
        (void)::write(master, wire.constData() + 20, size_t(wire.size() - 20));
        settle(250);
        const QString view = w.receivedText();
        CHECK(view.contains(QLatin1String("00000000  40 64 6F 70 31")) && view.contains(QLatin1String("00000010  ")),
              "the hex view shows 16-byte rows from offset 0, however the bytes arrived");
        CHECK(view.contains(QStringLiteral("%1  ").arg((wire.size() / 16) * 16, 8, 16, QLatin1Char('0'))),
              "and the partial last row once the line went quiet");
        w.closePort();
    }
    ::close(master);
#endif
}
