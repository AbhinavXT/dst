#include "testutil.h"

#include "logmodel.h"
#include "messagedispatcher.h"
#include "serialconsolewindow.h"
#include "serialmanager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPushButton>
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
//  Session 102 — background serial capture.
//
//  The port and Feed console used to belong to the terminal window, so
//  closing it stopped the IOA logs. Now SerialManager (owned by MainWindow)
//  runs the ports; the window is a viewer. Settings are per port.
//
//  Real @dop1 lines: replay/loco_2_1_29062026_134128.cap and
//  replay/loco_1_2_27062026_170159.cap.
// =============================================================================

namespace {

const QByteArray kDopA =
    "@dop1_2_1 2026-06-29T13:41:29 14034 0C 17 17 17 17 0C 0C 0C 0C 17 17 17 17 17 17 17";
const QByteArray kDopB =
    "@dop1_1_2 2026-06-27T17:02:00 3814 0C 17 17 17 17 0C 0C 0C 0C 0C 17 17 17 17 17 17";

void settle(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

#ifdef Q_OS_UNIX
struct Pty {
    int master = -1;
    QString path;
    bool ok = false;
    Pty()
    {
        int slave = -1;
        char name[256] = {};
        if (::openpty(&master, &slave, name, nullptr, nullptr) != 0) return;
        termios t{};
        tcgetattr(master, &t);
        cfmakeraw(&t);
        tcsetattr(master, TCSANOW, &t);
        ::close(slave);
        path = QString::fromLocal8Bit(name);
        ok = true;
    }
    ~Pty() { if (master >= 0) ::close(master); }
    void print(const QByteArray &line) const
    {
        const QByteArray b = line + "\r\n";
        (void)::write(master, b.constData(), size_t(b.size()));
    }
};
#endif

int linksIn(const SerialManager &m) { return m.findChildren<SerialLink *>().size(); }

}  // namespace

TEST_SUITE(session102)
{
    // ---- naming and per-port settings (no port needed) ----------------------
    CHECK(SerialManager::kvchForPort(QStringLiteral("COM3")) == SerialConsoleWindow::kvchForPort(QStringLiteral("COM3")),
          "the tab key is the manager's, and the window agrees (session 85 tabs keep their keys)");
    CHECK(SerialManager::tabTitleFor(QStringLiteral("/dev/ttyUSB0")) == QLatin1String("Serial ttyUSB0"),
          "tab title uses the short name");
    CHECK(!SerialManager::settingsGroup(QStringLiteral("/dev/ttyUSB0")).mid(14).contains(QLatin1Char('/')),
          "a device path does not nest settings groups");
    {
        QTemporaryDir dir;
        QSettings s(dir.filePath(QStringLiteral("t.ini")), QSettings::IniFormat);
        SerialConfig a; a.portName = QStringLiteral("COM5"); a.baud = 115200;
        SerialConfig b; b.portName = QStringLiteral("COM6"); b.baud = 9600; b.parity = QSerialPort::EvenParity;
        SerialConfig c; c.portName = QStringLiteral("COM7"); c.baud = 57600;
        SerialManager::savePortSettings(s, a, true);
        SerialManager::savePortSettings(s, b, false);
        SerialManager::savePortSettings(s, c, true);
        SerialConfig got; bool feed = false;
        CHECK(SerialManager::loadPortSettings(s, QStringLiteral("COM5"), &got, &feed)
                  && got.summary() == QLatin1String("115200 8N1") && feed,
              "three IOA ports keep three settings: COM5");
        CHECK(SerialManager::loadPortSettings(s, QStringLiteral("com6"), &got, &feed)
                  && got.summary() == QLatin1String("9600 8E1") && !feed,
              "…COM6, its own baud, parity and feed (name case-insensitive)");
        CHECK(SerialManager::loadPortSettings(s, QStringLiteral("COM7"), &got, &feed) && got.baud == 57600,
              "…COM7, not overwritten by whichever closed last");
        CHECK(!SerialManager::loadPortSettings(s, QStringLiteral("COM9"), &got, &feed),
              "a port never saved reports so (the window falls back to serial/last)");
        CHECK(s.value(QStringLiteral("serial/lastPort")).toString() == QLatin1String("COM7"),
              "the last port used is remembered");
    }

#ifdef Q_OS_UNIX
    // ---- the capture outlives the window --------------------------------------
    {
        Pty card;
        CHECK(card.ok, "a pseudo-terminal to stand in for the IOA card");
        if (card.ok) {
            MessageDispatcher disp;
            SerialManager mgr(&disp);
            QSignalSpy changed(&mgr, &SerialManager::portsChanged);

            auto *w = new SerialConsoleWindow(&mgr);
            w->setAttribute(Qt::WA_DeleteOnClose, false);
            SerialConfig c; c.portName = card.path; c.baud = 9600;
            w->setConfigToUi(c);
            CHECK(linksIn(mgr) == 0, "choosing a port in the box makes no link (no link per keystroke)");
            w->findChild<QCheckBox *>(QStringLiteral("serialFeed"))->setChecked(true);
            CHECK(w->openPort() && mgr.openPorts() == QStringList{ card.path },
                  "Open in the window opens the port in the manager");
            CHECK(changed.count() >= 1, "the manager announces it (the status chip follows)");
            CHECK(w->statusText().contains(QLatin1String("keeps running")),
                  "the window says the port outlives it");

            card.print(kDopA);
            settle(250);
            disp.drainNow();
            LogModel *m = disp.modelForKey(SerialManager::tabKeyFor(card.path));
            CHECK(m && m->count() == 1 && m->entryAt(0)->text == QString::fromLatin1(kDopA),
                  "with the window open, the line reaches the console tab");
            CHECK(w->receivedText().contains(QString::fromLatin1(kDopA)), "…and the terminal");

            w->close();
            delete w;
            CHECK(mgr.openPorts() == QStringList{ card.path }, "closing the window leaves the port open");
            card.print(kDopB);
            settle(250);
            disp.drainNow();
            CHECK(m && m->count() == 2 && m->entryAt(1)->text == QString::fromLatin1(kDopB),
                  "with NO window open, the next line still reaches the console tab");
            CHECK(mgr.fedLines(card.path) == 2, "the manager counts what it fed");

            // Reopening the terminal shows the running port.
            SerialConsoleWindow again(&mgr);
            again.setAttribute(Qt::WA_DeleteOnClose, false);
            CHECK(again.link() && again.link()->isOpen()
                      && again.configFromUi().portName == card.path
                      && again.configFromUi().baud == 9600,
                  "a new terminal attaches to the running port, with its settings");
            CHECK(again.findChild<QPushButton *>(QStringLiteral("serialOpen"))->text() == QLatin1String("Close"),
                  "…and offers Close");
            CHECK(again.sendText(QStringLiteral("STATUS?")), "…and can send on it");

            // Feed console off, from the viewer, stops the feed.
            again.findChild<QCheckBox *>(QStringLiteral("serialFeed"))->setChecked(false);
            card.print(kDopA);
            settle(250);
            disp.drainNow();
            CHECK(m && m->count() == 2 && again.receivedText().count(QString::fromLatin1(kDopA)) >= 1,
                  "Feed console off: shown in the terminal, not fed to the tab");

            // Close stops it.
            again.closePort();
            CHECK(mgr.openPorts().isEmpty() && !again.link()->isOpen(), "Close in the window closes the port");
        }
    }

    // ---- a status chip opens a terminal on ITS port: showPort ----------------
    {
        Pty one, two;
        if (one.ok && two.ok) {
            MessageDispatcher disp;
            SerialManager mgr(&disp);
            SerialConfig a; a.portName = one.path; a.baud = 9600;
            SerialConfig b; b.portName = two.path; b.baud = 19200;
            mgr.setFeed(two.path, false);
            CHECK(mgr.open(a) && mgr.open(b) && mgr.openPorts().size() == 2, "two ports run at once");
            SerialConsoleWindow w(&mgr);
            w.setAttribute(Qt::WA_DeleteOnClose, false);
            w.showPort(two.path);
            CHECK(w.link() == mgr.link(two.path) && w.configFromUi().baud == 19200,
                  "showPort attaches to that port and shows its settings");
            CHECK(!w.findChild<QCheckBox *>(QStringLiteral("serialFeed"))->isChecked(),
                  "…and its own Feed console state");
            w.showPort(one.path);
            CHECK(w.link() == mgr.link(one.path) && mgr.link(two.path)->isOpen(),
                  "switching the viewer to another port leaves the first running");
            CHECK(mgr.link(QStringLiteral("x")) == nullptr && mgr.link(one.path.toUpper()) == mgr.link(one.path),
                  "links are found case-insensitively, and only once made");
            mgr.closeAll();
            CHECK(mgr.openPorts().isEmpty(), "closeAll closes every port");
        }
    }

    // ---- the standalone window still owns its port --------------------------
    {
        Pty card;
        if (card.ok) {
            MessageDispatcher disp;
            auto *w = new SerialConsoleWindow(&disp);
            w->setAttribute(Qt::WA_DeleteOnClose, false);
            SerialConfig c; c.portName = card.path; c.baud = 9600;
            w->setConfigToUi(c);
            CHECK(w->openPort(), "standalone window opens");
            SerialLink *l = w->link();
            w->close();
            CHECK(!l->isOpen(), "standalone: closing the window closes its port, as before");
            delete w;
        }
    }
#endif
}
