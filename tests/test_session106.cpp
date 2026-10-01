#include "testutil.h"

#include "logmodel.h"
#include "messagedispatcher.h"
#include "serialconsolewindow.h"
#include "serialmanager.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPushButton>
#include <QSignalSpy>

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
//  Session 106 — auto-reconnect.
//
//  A port the driver lost (the adapter pulled) is waited for and reopened
//  when the same adapter comes back — found by USB serial number under any
//  name, since Windows renumbers COM ports — into the same console tab, with
//  "lost" and "reconnected, gap N s" lines so the gap in the log is visible.
//
//  The enumerator is replaced by a stand-in so the adapter can vanish and
//  return under a new name on cue; the ports themselves are real ptys.
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

#ifdef Q_OS_UNIX
struct Pty {
    int master = -1;
    QString path;
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
    }
    ~Pty() { if (master >= 0) ::close(master); }
    bool ok() const { return master >= 0; }
    void print(const QByteArray &line) const
    {
        const QByteArray b = line + "\r\n";
        (void)::write(master, b.constData(), size_t(b.size()));
    }
};
#endif

QStringList tabTexts(MessageDispatcher &disp, const QString &key)
{
    disp.drainNow();
    QStringList out;
    if (LogModel *m = disp.modelForKey(key))
        for (int i = 0; i < m->count(); ++i) out << m->entryAt(i)->text;
    return out;
}

}  // namespace

TEST_SUITE(session106)
{
    CHECK(SerialManager::findPortBySerial(QString(), QStringLiteral("/dev/no-such-port-106")).isEmpty(),
          "an adapter that is not there is not found");
    CHECK(SerialManager::findPortBySerial(QStringLiteral("NO-SUCH-SERIAL-106"), QStringLiteral("COM5")).isEmpty(),
          "a serial number no adapter has is not found, whatever the old name");

#ifdef Q_OS_UNIX
    Pty a, b;
    if (!a.ok() || !b.ok()) { CHECK(false, "two ptys"); return; }

    MessageDispatcher disp;
    QSignalSpy tabsMade(&disp, &MessageDispatcher::tabRequested);
    SerialManager mgr(&disp);
    mgr.setReconnectIntervalMs(50);
    // The stand-in enumerator: nothing while "unplugged", then the adapter
    // under whatever name `back` holds.
    QString back;
    int asked = 0;
    mgr.setPortFinder([&](const QString &, const QString &) { ++asked; return back; });

    SerialProfile p;
    p.name = QStringLiteral("IOA Input");
    p.config.portName = a.path;
    p.config.baud = 57600;
    CHECK(mgr.openProfile(p), "IOA Input opens on the first pty");
    const QString tab = SerialManager::tabKeyFor(a.path);
    a.print(kDop1);
    settle(200);
    CHECK(tabTexts(disp, tab).size() == 1, "a line before the loss reaches its tab");

    auto *w = new SerialConsoleWindow(&mgr);
    w->setAttribute(Qt::WA_DeleteOnClose, false);
    w->showPort(a.path);

    // ---- the adapter is pulled ---------------------------------------------------
    mgr.link(a.path)->onError(QSerialPort::ResourceError);
    CHECK(mgr.openPorts().isEmpty() && mgr.reconnectingPorts() == QStringList{ a.path },
          "a port the driver lost is waited for, not forgotten");
    CHECK(tabTexts(disp, tab).last().contains(QLatin1String("lost")),
          "the console tab says it was lost (the gap is visible where the log is read)");
    CHECK(w->receivedText().contains(QLatin1String("lost")) && w->statusText().contains(QLatin1String("reconnecting")),
          "the terminal says so too");
    CHECK(w->findChild<QPushButton *>(QStringLiteral("serialOpen"))->text() == QLatin1String("Stop waiting"),
          "and offers to stop waiting");

    settle(300);
    CHECK(asked >= 3 && mgr.isReconnecting(a.path), "while it is gone, it keeps looking");

    // ---- it comes back, renumbered ------------------------------------------------
    back = b.path;
    settle(300);
    CHECK(mgr.openPorts() == QStringList{ b.path } && mgr.link(b.path) == mgr.link(a.path),
          "back under a new name: the same port, reopened");
    CHECK(mgr.link(b.path)->config().baud == 57600 && mgr.label(b.path) == QLatin1String("IOA Input"),
          "with its settings and its profile name");
    const QStringList texts = tabTexts(disp, tab);
    CHECK(texts.last().contains(QLatin1String("reconnected as")) && texts.last().contains(QLatin1String("gap")),
          QByteArray("the tab says it is back and how long it was gone: ") + texts.last().toUtf8());
    b.print(kDop1);
    settle(200);
    CHECK(tabTexts(disp, tab).last() == QString::fromLatin1(kDop1),
          "lines from the new name carry on in the SAME tab");
    // (modelForKey makes a model on demand, so the announcements are counted.)
    CHECK(tabsMade.count() == 1, "no second tab for the new name");
    CHECK(mgr.health(b.path, QDateTime::currentMSecsSinceEpoch()).lines == 2,
          "health continues across the reconnect (the gap shows as longest gap)");
    CHECK(w->configFromUi().portName == b.path && w->receivedText().contains(QLatin1String("reconnected")),
          "the terminal follows it to the new name");

    // ---- Close stops a wait -------------------------------------------------------
    back.clear();
    mgr.link(b.path)->onError(QSerialPort::ResourceError);
    CHECK(mgr.isReconnecting(b.path), "lost again");
    w->closePort();
    const int before = asked;
    settle(200);
    CHECK(!mgr.isReconnecting(b.path) && asked == before, "Stop waiting ends the search");

    // ---- a Close is not a loss -------------------------------------------------------
    back = a.path;
    CHECK(mgr.open(p.config), "opens again");
    mgr.close(a.path);
    settle(200);
    CHECK(mgr.openPorts().isEmpty() && mgr.reconnectingPorts().isEmpty(),
          "a port the operator closed stays closed");

    // ---- same name, the real finder: a pty path that is still there --------------------
    mgr.setPortFinder(&SerialManager::findPortBySerial);
    CHECK(mgr.open(p.config), "open with the real enumerator");
    mgr.link(a.path)->onError(QSerialPort::ReadError);
    settle(300);
    CHECK(mgr.openPorts() == QStringList{ a.path } && tabTexts(disp, tab).last().startsWith(QStringLiteral("── reconnected ")),
          "with no serial number, found again by its name");
    mgr.closeAll();
    delete w;
#endif
}
