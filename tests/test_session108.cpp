#include "testutil.h"

#include "messagedispatcher.h"
#include "serialconsolewindow.h"
#include "serialfilesender.h"
#include "serialmanager.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QProgressBar>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>

#ifdef Q_OS_UNIX
#  include <fcntl.h>
#  include <termios.h>
#  include <unistd.h>
#  if defined(Q_OS_MACOS)
#    include <util.h>
#  else
#    include <pty.h>
#  endif
#endif

// =============================================================================
//  Session 108 — paced file send.
//
//  "Send file" was one write: no pacing, no progress, no Stop, and the file
//  echoed as one giant TX> line. Now: N-byte chunks D ms apart, or line by
//  line waiting for the card's prompt; a progress bar, Stop, and a summary
//  in the terminal instead of the echo.
//
//  The file is real: replay/loco_2_1_29062026_134128.cap, the card's own
//  capture, sent back byte for byte.
// =============================================================================

namespace {

void settle(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

QByteArray realFile()
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_2_1_29062026_134128.cap"));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

}  // namespace

TEST_SUITE(session108)
{
    const QByteArray file = realFile();
    CHECK(file.size() > 4000, "fixture: a real capture file from replay/");

    // ---- how a file is cut -------------------------------------------------------
    {
        SerialSendOptions o;
        o.chunkBytes = 1000;
        const QVector<QByteArray> c = serialSendPieces(file, o);
        CHECK(c.size() == (file.size() + 999) / 1000 && c.first().size() == 1000,
              "chunks: 1000 bytes each, the last what is left");
        QByteArray joined;
        for (const QByteArray &p : c) joined += p;
        CHECK(joined == file, "…and together they are the file");
        o.mode = SerialSendOptions::Mode::Lines;
        const QVector<QByteArray> l = serialSendPieces("one\r\ntwo\nthree", o);
        CHECK(l == QVector<QByteArray>({ "one\r\n", "two\n", "three" }),
              "lines keep their own line end; a last line without one still goes");
        o.prompt = QStringLiteral(">");
        CHECK(o.summary().contains(QStringLiteral("“>”")), "the summary names the prompt");
    }
    {
        QTemporaryDir dir;
        QSettings s(dir.filePath(QStringLiteral("t.ini")), QSettings::IniFormat);
        SerialSendOptions o;
        o.mode = SerialSendOptions::Mode::Lines;
        o.prompt = QStringLiteral("OK>");
        o.delayMs = 7;
        o.save(s);
        const SerialSendOptions b = SerialSendOptions::load(s);
        CHECK(b.mode == o.mode && b.prompt == o.prompt && b.delayMs == 7, "the choices are remembered");
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
    ::fcntl(master, F_SETFL, ::fcntl(master, F_GETFL) | O_NONBLOCK);
    QByteArray card;                             // what the card has received
    auto drain = [&]() {
        char buf[4096];
        ssize_t n;
        while ((n = ::read(master, buf, sizeof buf)) > 0) card.append(buf, int(n));
    };

    SerialLink link;
    SerialConfig cfg;
    cfg.portName = QString::fromLocal8Bit(name);
    if (!link.open(cfg)) { CHECK(false, "opens"); ::close(master); return; }

    // ---- chunks, paced, byte for byte ------------------------------------------------
    {
        SerialFileSender s;
        QSignalSpy prog(&s, &SerialFileSender::progress);
        QSignalSpy done(&s, &SerialFileSender::finished);
        SerialSendOptions o;
        o.chunkBytes = 512;
        o.delayMs = 15;
        QElapsedTimer clock;
        clock.start();
        CHECK(s.start(&link, file, o), "starts");
        CHECK(!s.start(&link, file, o), "a second send while one runs is refused");
        while (done.isEmpty() && clock.elapsed() < 5000) { settle(10); drain(); }
        settle(50); drain();
        const int pieces = (file.size() + 511) / 512;
        CHECK(done.size() == 1 && done.at(0).at(0).toBool(), "finishes ok");
        CHECK(card == file, "the card received the file exactly");
        CHECK(clock.elapsed() >= (pieces - 1) * 15, "paced: at least the delays between chunks");
        CHECK(prog.size() == pieces + 1 && prog.last().at(0).toLongLong() == file.size(),
              "progress after every chunk, ending at the total");
    }

    // ---- Stop -------------------------------------------------------------------------
    {
        card.clear();
        SerialFileSender s;
        QSignalSpy done(&s, &SerialFileSender::finished);
        SerialSendOptions o;
        o.chunkBytes = 256;
        o.delayMs = 40;
        s.start(&link, file, o);
        settle(100);
        s.stop();
        settle(150); drain();
        CHECK(done.size() == 1 && !done.at(0).at(0).toBool()
                  && done.at(0).at(1).toString().startsWith(QLatin1String("stopped at")),
              "Stop ends it and says where");
        CHECK(card.size() > 0 && card.size() < file.size() && file.startsWith(card),
              "what went before Stop is the start of the file, and nothing after");
        settle(200); drain();
        CHECK(card.size() == s.bytesSent(), "nothing more goes after Stop");
    }

    // ---- line by line, waiting for the card's prompt -----------------------------------
    {
        card.clear();
        const QByteArray script = "SET A 1\r\nSET B 2\r\nGO\r\n";
        SerialFileSender s;
        QSignalSpy done(&s, &SerialFileSender::finished);
        SerialSendOptions o;
        o.mode = SerialSendOptions::Mode::Lines;
        o.prompt = QStringLiteral("OK>");
        o.delayMs = 0;
        o.promptTimeoutMs = 2000;
        s.start(&link, script, o);
        // The card: answers each whole line with its prompt, a little later.
        int answered = 0;
        QElapsedTimer clock;
        clock.start();
        while (done.isEmpty() && clock.elapsed() < 3000) {
            settle(10);
            drain();
            const int lines = card.count('\n');
            if (lines > answered) {
                CHECK(lines == answered + 1, "the next line waits for the prompt (never two at once)");
                settle(30);
                (void)::write(master, "\r\nOK> ", 6);
                answered = lines;
            }
        }
        CHECK(done.size() == 1 && done.at(0).at(0).toBool() && card == script,
              "every line went, each after the card said OK>");
    }
    {
        card.clear();
        SerialFileSender s;
        QSignalSpy done(&s, &SerialFileSender::finished);
        SerialSendOptions o;
        o.mode = SerialSendOptions::Mode::Lines;
        o.prompt = QStringLiteral("OK>");
        o.promptTimeoutMs = 200;
        settle(150); drain();        // the last test's late "OK>" must not count here
        s.start(&link, "ONE\r\nTWO\r\n", o);
        settle(400); drain();
        CHECK(done.size() == 1 && !done.at(0).at(0).toBool()
                  && done.at(0).at(1).toString().contains(QLatin1String("after line 1 of 2")),
              QByteArray("no prompt: it gives up and names the line: ") + done.value(0).value(1).toString().toUtf8());
        CHECK(card == "ONE\r\n", "…having sent only the first line");
    }

    // ---- the terminal: a summary, not a giant TX> line ---------------------------------
    link.close();
    {
        card.clear();
        MessageDispatcher disp;
        SerialManager mgr(&disp);
        SerialConsoleWindow w(&mgr);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.setConfigToUi(cfg);
        CHECK(w.openPort(), "terminal opens");
        SerialSendOptions o;
        o.chunkBytes = 1024;
        o.delayMs = 5;
        CHECK(w.sendFileData(file, QStringLiteral("loco.cap"), o), "send from the terminal");
        CHECK(w.findChild<QProgressBar *>(QStringLiteral("serialSendProgress"))->isVisible() || !w.isVisible(),
              "a progress bar while it runs");
        QElapsedTimer clock;
        clock.start();
        while (w.fileSender()->isRunning() && clock.elapsed() < 5000) { settle(10); drain(); }
        // The last chunks may still be in the port's write buffer: a pty
        // drains slowly. Wait for them to reach the card.
        while (card.size() < file.size() && clock.elapsed() < 8000) { settle(10); drain(); }
        const QString view = w.receivedText();
        CHECK(view.contains(QLatin1String("sending loco.cap")) && view.contains(QLatin1String("sent "))
                  && !view.contains(QLatin1String("TX> @")),
              "the terminal shows start and end lines, not the file as one TX> line");
        CHECK(card == file, "and the card got it all");
        w.closePort();
    }
    ::close(master);
#endif
}
