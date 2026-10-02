#include "testutil.h"

#include "logmodel.h"
#include "messagedispatcher.h"
#include "serialconsolewindow.h"
#include "seriallink.h"
#include "settings.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#  if defined(Q_OS_MACOS)
#    include <util.h>      // openpty on macOS (session 113: this ran on Linux only)
#  else
#    include <pty.h>
#  endif
#endif

// =============================================================================
//  Session 85: the serial port terminal, its line handling, and serial lines
//  fed into the console's own pipeline. End to end over a Linux
//  pseudo-terminal pair (the far end plays the IOA card).
// =============================================================================

namespace {

QStringList realLines85(const QString &prefix, int limit)
{
    QStringList out;
    const QDir dir(QStringLiteral(DL_SRC_DIR) + QStringLiteral("/replay"));
    for (const QString &f : dir.entryList({ QStringLiteral("loco_*.cap") }, QDir::Files, QDir::Name)) {
        QFile file(dir.filePath(f));
        if (!file.open(QIODevice::ReadOnly)) continue;
        while (!file.atEnd() && out.size() < limit) {
            const QString l = QString::fromLatin1(file.readLine()).trimmed();
            if (l.startsWith(prefix)) out << l;
        }
        if (out.size() >= limit) break;
    }
    return out;
}

void settle(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) { QApplication::processEvents(QEventLoop::AllEvents, 10); QTest::qWait(5); }
}

}  // namespace

TEST_SUITE(session85)
{
    // ---- line splitting -------------------------------------------------------------------
    {
        SerialLineSplitter sp;
        auto a = sp.feed("one\r\ntwo\nthr", 100);
        CHECK(a.size() == 2 && a[0].text == "one" && a[1].text == "two" && sp.pendingBytes() == 3,
              "CRLF and LF end lines; the rest waits");
        auto b = sp.feed("ee\r", 150);
        CHECK(b.size() == 1 && b[0].text == "three" && b[0].firstByteMs == 100,
              "a line keeps the time of its FIRST byte (when the card printed it)");
        auto c = sp.feed("\nfour\r", 200);
        CHECK(c.size() == 1 && c[0].text == "four", "a CR|LF split across reads is one line end, not an empty line");
        auto d = sp.feed("\r\n", 210);
        CHECK(d.size() == 1 && d[0].text.isEmpty(), "a blank line is still a line");
        sp.feed("prompt>", 300);
        CHECK(sp.flushIdle(500, 300).isEmpty(), "a partial line is not flushed while it is fresh");
        sp.feed(" x", 590);
        CHECK(sp.flushIdle(800, 300).isEmpty(), "nor while it is still trickling in (idle is from the last byte)");
        auto e = sp.flushIdle(900, 300);
        CHECK(e.size() == 1 && e[0].text == "prompt> x" && e[0].firstByteMs == 300, "quiet long enough: delivered");
        const QByteArray big(SerialLineSplitter::kMaxLine + 10, 'x');
        auto f = sp.feed(big, 1000);
        CHECK(f.size() == 1 && f[0].text.size() == SerialLineSplitter::kMaxLine && sp.pendingBytes() == 10,
              "a runaway line is cut at 4096 bytes, not held forever");
    }

    // ---- hex and escapes --------------------------------------------------------------------
    {
        bool ok = false;
        CHECK(serialParseHex(QStringLiteral("AA 55 0d0A"), &ok) == QByteArray("\xAA\x55\x0D\x0A", 4) && ok,
              "hex: spaced, packed and lower case");
        CHECK(serialParseHex(QStringLiteral("0xAA,0x55:01-02"), &ok) == QByteArray("\xAA\x55\x01\x02", 4) && ok,
              "hex: 0x prefixes and , : - separators");
        serialParseHex(QStringLiteral("AA 5"), &ok);
        CHECK(!ok, "an odd number of digits is refused");
        serialParseHex(QStringLiteral("AA GG"), &ok);
        CHECK(!ok, "a non-hex character is refused");
        CHECK(serialToHex(QByteArray("\x01\xAB\xff", 3)) == QLatin1String("01 AB FF"), "bytes print as spaced upper-case hex");
        CHECK(serialUnescape(QStringLiteral("a\\r\\n\\t\\x41\\\\\\q")) == QByteArray("a\r\n\tA\\\\q"),
              "text escapes \\r \\n \\t \\xHH \\\\; an unknown one is kept");
    }

    // ---- configuration ----------------------------------------------------------------------
    {
        SerialConfig c;
        CHECK(c.summary() == QLatin1String("115200 8N1"), "default 115200 8N1");
        c.baud = 9600; c.dataBits = QSerialPort::Data7; c.parity = QSerialPort::EvenParity;
        c.stopBits = QSerialPort::TwoStop; c.flow = QSerialPort::HardwareControl; c.portName = QStringLiteral("COM7");
        CHECK(c.summary() == QLatin1String("9600 7E2 RTS/CTS"), "9600 7E2 RTS/CTS");
        c.stopBits = QSerialPort::OneAndHalfStop; c.parity = QSerialPort::MarkParity; c.flow = QSerialPort::SoftwareControl;
        CHECK(c.summary() == QLatin1String("9600 7M1.5 XON/XOFF"), "mark parity, 1.5 stop bits, XON/XOFF");
        QSettings s(QDir::temp().filePath(QStringLiteral("dl_serial85.ini")), QSettings::IniFormat);
        c.save(s, QStringLiteral("t"));
        const SerialConfig r = SerialConfig::load(s, QStringLiteral("t"));
        CHECK(r.summary() == c.summary() && r.portName == QLatin1String("COM7"), "saved and loaded unchanged");
        s.setValue(QStringLiteral("bad/parity"), 1);   // 1 is not a QSerialPort::Parity
        s.setValue(QStringLiteral("bad/dataBits"), 12);
        s.setValue(QStringLiteral("bad/baud"), -5);
        const SerialConfig b = SerialConfig::load(s, QStringLiteral("bad"));
        CHECK(b.summary() == QLatin1String("115200 8N1"), "nonsense in the ini falls back to 115200 8N1");
        QFile::remove(s.fileName());
        CHECK(serialStandardBauds().contains(9600) && serialStandardBauds().contains(921600), "standard rates offered");
    }

    // ---- into the console's pipeline -----------------------------------------------------------
    const QStringList dop = realLines85(QStringLiteral("@dop1_"), 3);
    CHECK(dop.size() == 3, "real @dop lines (the IOA's output log) from replay/");
    {
        CHECK(SerialConsoleWindow::kvchForPort(QStringLiteral("COM3")) == SerialConsoleWindow::kvchForPort(QStringLiteral("com3"))
                  && SerialConsoleWindow::kvchForPort(QStringLiteral("COM3")) != SerialConsoleWindow::kvchForPort(QStringLiteral("COM4"))
                  && SerialConsoleWindow::kvchForPort(QString()) != 0,
              "a port's tab is stable (case-insensitive), distinct per port, never 0");
        MessageDispatcher disp;
        QSignalSpy tabs(&disp, &MessageDispatcher::tabRequested);
        const quint16 k = SerialConsoleWindow::kvchForPort(QStringLiteral("COM3"));
        disp.ingestLocal(SerialConsoleWindow::kSerialSourceId, k, dop.value(0).toUtf8(), 1234, QStringLiteral("Serial COM3"));
        disp.ingestLocal(SerialConsoleWindow::kSerialSourceId, k, dop.value(1).toUtf8(), 1300, QStringLiteral("Serial COM3"));
        disp.drainNow();
        const QString key = QStringLiteral("254_%1").arg(k);
        LogModel *m = disp.modelForKey(key);
        CHECK(m && m->count() == 2 && m->entryAt(0)->text == dop.value(0) && m->entryAt(0)->epochMs == 1234,
              "ingested lines land in their own tab, text and time as given");
        CHECK(tabs.count() == 1 && tabs.at(0).at(1).toString() == QLatin1String("Serial COM3"),
              "the tab is announced once, named after the port");
        CHECK(m && m->entryAt(1)->tabKey() == key, "the entry's own tab key agrees");
    }

#ifdef Q_OS_UNIX
    // ---- end to end over a pseudo-terminal ------------------------------------------------------
    int master = -1, slave = -1;
    char name[128] = {};
    const bool pty = ::openpty(&master, &slave, name, nullptr, nullptr) == 0;
    CHECK(pty, "a pseudo-terminal pair to stand in for the card");
    if (pty) {
        // Raw on the card side, so the tty layer passes bytes untouched.
        termios t{};
        tcgetattr(master, &t);
        cfmakeraw(&t);
        tcsetattr(master, TCSANOW, &t);
        ::fcntl(master, F_SETFL, ::fcntl(master, F_GETFL) | O_NONBLOCK);

        QSettings st(Settings::iniPath(), QSettings::IniFormat);
        const QVariant feedWas = st.value(QStringLiteral("serial/feed"));
        st.setValue(QStringLiteral("serial/feed"), true);
        // Session 113: the window saves its view and send choices on close
        // (Hex, line end, ...). Put every serial/ key back afterwards, or the
        // next suite's terminal inherits them — a later suite once found its
        // text sends refused as "not hex" because this one ended in Hex.
        QHash<QString, QVariant> serialWas;
        st.beginGroup(QStringLiteral("serial"));
        for (const QString &k : st.allKeys()) serialWas.insert(k, st.value(k));
        st.endGroup();

        MessageDispatcher disp;
        SerialConsoleWindow w(&disp);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.show();
        SerialConfig c;
        c.portName = QString::fromLocal8Bit(name);
        c.baud = 9600; c.parity = QSerialPort::EvenParity;
        w.setConfigToUi(c);
        CHECK(w.configFromUi().portName == c.portName && w.configFromUi().summary() == QLatin1String("9600 8E1"),
              "the UI holds the chosen line settings");
#ifdef Q_OS_LINUX
        // A Linux pseudo-terminal cannot do parity: the driver refuses it,
        // and the port must not open quietly at some other setting.
        CHECK(!w.openPort() && !w.link()->isOpen() && w.statusText().contains(QLatin1String("refused 9600 8E1")),
              QByteArray("a setting the driver refuses is not silently replaced: ") + w.statusText().toUtf8());
#else
        // A macOS pseudo-terminal accepts parity (session 113 found this the
        // first time the test ran here), so there is no refusal to check —
        // only that what opened is what was asked for.
        CHECK(w.openPort() && w.link()->config().summary() == QLatin1String("9600 8E1"),
              "macOS: the pty takes 8E1, and that is what opens");
        w.closePort();
#endif
        c.parity = QSerialPort::NoParity;
        w.setConfigToUi(c);
        CHECK(w.openPort() && w.link()->isOpen(), QByteArray("opens ") + QByteArray(name) + QByteArray(" at 9600 8N1: ") + w.statusText().toUtf8());
        CHECK(w.statusText().contains(QLatin1String("9600 8N1")) && w.windowTitle().contains(QLatin1String("9600 8N1")),
              "the status and title say what is open");

        // The card prints two log lines, the first split across writes.
        const QByteArray l0 = dop.value(0).toUtf8(), l1 = dop.value(1).toUtf8();
        ::write(master, l0.constData(), size_t(l0.size() / 2));
        settle(60);
        ::write(master, l0.constData() + l0.size() / 2, size_t(l0.size() - l0.size() / 2));
        ::write(master, "\r\n", 2);
        ::write(master, l1.constData(), size_t(l1.size()));
        ::write(master, "\r\n", 2);
        settle(250);
        CHECK(w.receivedText().contains(dop.value(0)) && w.receivedText().contains(dop.value(1)),
              "both lines appear in the terminal, the split one whole");
        disp.drainNow();
        LogModel *m = disp.modelForKey(w.tabKey());
        CHECK(m && m->count() == 2 && m->entryAt(0)->text == dop.value(0) && m->entryAt(1)->text == dop.value(1),
              "and in the console tab, in order, as the card printed them");
        CHECK(w.link()->rxBytes() == quint64(l0.size() + l1.size() + 4), "RX counts every byte");

        // Send: text with CR+LF, then hex, then a refusal.
        auto *ending = w.findChild<QComboBox *>(QStringLiteral("serialEnding"));
        auto *sendHex = w.findChild<QCheckBox *>(QStringLiteral("serialSendHex"));
        if (ending) ending->setCurrentIndex(3);
        if (sendHex) sendHex->setChecked(false);
        auto readCard = [master]() {
            settle(80);
            QByteArray got;
            char buf[512];
            ssize_t n;
            while ((n = ::read(master, buf, sizeof buf)) > 0) got.append(buf, int(n));
            return got;
        };
        CHECK(w.sendText(QStringLiteral("STATUS?")) && readCard() == QByteArray("STATUS?\r\n"),
              "text goes out with the chosen line end (CR+LF)");
        if (ending) ending->setCurrentIndex(0);
        if (sendHex) sendHex->setChecked(true);
        CHECK(w.sendText(QStringLiteral("AA 55 01 FE")) && readCard() == QByteArray("\xAA\x55\x01\xFE", 4),
              "hex goes out as exactly those bytes");
        CHECK(!w.sendText(QStringLiteral("AA 5")) && readCard().isEmpty() && w.statusText().contains(QLatin1String("Not hex")),
              "bad hex is refused and nothing is sent");
        CHECK(w.link()->txBytes() == 13, "TX counts what went out");
        CHECK(w.receivedText().contains(QLatin1String("TX> STATUS?")), "sent text is echoed in the terminal");
        auto *hist = w.findChild<QComboBox *>(QStringLiteral("serialSend"));
        CHECK(hist && hist->itemText(0) == QLatin1String("AA 55 01 FE") && hist->findText(QStringLiteral("STATUS?")) > 0,
              "what was sent is in the history, newest first");

        // A card that prints without a newline: delivered once it goes quiet.
        ::write(master, "login:", 6);
        settle(500);
        CHECK(w.receivedText().contains(QLatin1String("login:")), "a prompt with no newline appears once the line goes quiet");

        // Hold: the view freezes, nothing is lost, the console still gets it.
        auto *hold = w.findChild<QCheckBox *>(QStringLiteral("serialHold"));
        CHECK(hold != nullptr, "a Hold switch");
        if (hold) {
            const int fedBefore = disp.modelForKey(w.tabKey()) ? disp.modelForKey(w.tabKey())->count() : -1;
            hold->setChecked(true);
            ::write(master, "held-line-85\r\n", 14);
            settle(150);
            disp.drainNow();
            CHECK(!w.receivedText().contains(QLatin1String("held-line-85"))
                      && w.statusText().contains(QLatin1String("holding 1")),
                  "held: the line is not scrolled in, and the status says one is waiting");
            CHECK(disp.modelForKey(w.tabKey())->count() == fedBefore + 1,
                  "held: the console tab still receives it");
            hold->setChecked(false);
            CHECK(w.receivedText().contains(QLatin1String("held-line-85")), "released: the held line appears");
        }

        // Hex view shows the raw chunks.
        auto *hexView = w.findChild<QCheckBox *>(QStringLiteral("serialHexView"));
        if (hexView) hexView->setChecked(true);
        ::write(master, "\x01\x02\x7F", 3);
        settle(150);
        CHECK(w.receivedText().contains(QLatin1String("01 02 7F")), "the hex view shows the bytes as they came");
        if (hexView) hexView->setChecked(false);

        w.closePort();
        CHECK(!w.link()->isOpen() && w.statusText() == QLatin1String("Closed"), "closes");
        CHECK(!w.sendText(QStringLiteral("x")), "nothing is sent on a closed port");

        SerialConfig gone = c;
        gone.portName = QStringLiteral("/dev/does-not-exist-85");
        w.setConfigToUi(gone);
        CHECK(!w.openPort() && w.statusText().startsWith(QStringLiteral("\u2715")), "a missing port says why it did not open");
        w.close();
        st.sync();                      // pick up what the window just saved

        if (feedWas.isValid()) st.setValue(QStringLiteral("serial/feed"), feedWas);
        else st.remove(QStringLiteral("serial/feed"));
        st.beginGroup(QStringLiteral("serial"));
        for (const QString &k : st.allKeys()) if (!serialWas.contains(k)) st.remove(k);
        for (auto it = serialWas.constBegin(); it != serialWas.constEnd(); ++it) st.setValue(it.key(), it.value());
        st.endGroup();
        st.sync();
        ::close(master);
        ::close(slave);
    }
#endif
}
