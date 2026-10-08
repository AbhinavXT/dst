#include "testutil.h"

#include "logmodel.h"
#include "messagedispatcher.h"
#include "serialconsolewindow.h"
#include "serialmanager.h"
#include "serialportscan.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QMutex>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

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
//  Session 156 — serial: the right card after a reconnect, no GUI stalls.
//
//  1. Adapters without a usable serial number (CH340, PL2303, clone FTDI)
//     are found again by the USB socket they are plugged into, not by a
//     port name that swaps when they are replugged in the other order; and
//     after any reconnect the port's first log lines must be of a type its
//     tab had before, or they are held out of the console.
//  2. The search for lost adapters runs on a scan thread, once per tick for
//     every lost port, never on the GUI thread.
//  3. Opening a port does not wait for the driver.
//
//  The enumerator is a stand-in (the USB sockets are invented: there is no
//  USB here); the ports are real ptys. Real lines from
//  replay/loco_2_1_29062026_134128.cap. There is no IOA input card in
//  replay/, so the "other card" is played by real @nmshlth frames: what the
//  check looks at is a type the tab never had.
// =============================================================================

namespace {

const QByteArray kDop1 =
    "@dop1_2_1 2026-06-29T13:41:29 14034 0C 17 17 17 17 0C 0C 0C 0C 17 17 17 17 17 17 17";
const QByteArray kDop2 =
    "@dop2_2_1 2026-06-29T13:41:29 14035 0C 17 17 17 17 0C 0C 0C 0C 17 17 17 17 17 17 17";
const QByteArray kHlth =
    "@nmshlth_2_1 2026-06-29T13:41:29 14036 02 06 0B 00 42 00 00 00 BB BB 18 00 38 00 01 00 00 02 00 01 01 "
    "1D 06 1A 0D 29 1D 0A 00 01 01 00 02 01 00 0D 00 00 0E 00 00 0F 00 00 10 00 00 11 00 00 00 29 00 00";

constexpr quint16 kCh340Vid = 0x1a86, kCh340Pid = 0x7523;

void settle(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

template <typename Pred>
bool waitFor(Pred pred, int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        if (pred()) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return pred();
}

SerialPortSnapshot snap(const QString &name, const QString &serial, const QString &location,
                        quint16 vid = kCh340Vid, quint16 pid = kCh340Pid)
{
    SerialPortSnapshot s;
    s.name = name;
    s.systemLocation = QStringLiteral("/dev/") + name;
    s.serial = serial;
    s.location = location;
    s.vid = vid;
    s.pid = pid;
    return s;
}

SerialAdapterId lostId(const QString &lastName, const QString &serial, const QString &location,
                       quint16 vid = kCh340Vid, quint16 pid = kCh340Pid)
{
    SerialAdapterId id;
    id.lastName = lastName;
    id.serial = serial;
    id.location = location;
    id.vid = vid;
    id.pid = pid;
    return id;
}

QString planFor(const QVector<SerialReconnectPlan> &plans, int index, SerialMatchBy *by = nullptr)
{
    for (const SerialReconnectPlan &p : plans)
        if (p.index == index) { if (by) *by = p.by; return p.portName; }
    if (by) *by = SerialMatchBy::None;
    return QString();
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
        if (::write(master, b.constData(), size_t(b.size())) < 0) { /* the test sees the line missing */ }
    }
    // As the enumerator would list it, plugged into `location`.
    SerialPortSnapshot at(const QString &location) const
    {
        SerialPortSnapshot s;
        s.name = path.section(QLatin1Char('/'), -1);
        s.systemLocation = path;
        s.location = location;
        s.vid = kCh340Vid;
        s.pid = kCh340Pid;
        return s;
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

int countOf(const QStringList &texts, const QByteArray &line)
{
    return texts.count(QString::fromLatin1(line));
}

// A stand-in enumerator the tests can change while the scan thread reads it.
struct FakeBus {
    QMutex mutex;
    SerialPortList ports;
    int delayMs = 0;
    QThread *ranOn = nullptr;
    int scans = 0;
    // Like a real enumerator: what is plugged in when it starts, delivered
    // when it finishes.
    SerialPortList read()
    {
        int delay = 0;
        SerialPortList seen;
        {
            QMutexLocker l(&mutex);
            ranOn = QThread::currentThread();
            ++scans;
            delay = delayMs;
            seen = ports;
        }
        if (delay > 0) QThread::msleep(static_cast<unsigned long>(delay));
        return seen;
    }
    void set(const SerialPortList &p) { QMutexLocker l(&mutex); ports = p; }
    void setDelay(int ms) { QMutexLocker l(&mutex); delayMs = ms; }
    int scanCount() { QMutexLocker l(&mutex); return scans; }
    QThread *thread() { QMutexLocker l(&mutex); return ranOn; }
};

// Occupies a link's reader thread for `ms`: a driver that is slow to open.
void stallReader(SerialLink *link, int ms)
{
    auto *blocker = new QObject;
    blocker->moveToThread(link->readerThread());
    QMetaObject::invokeMethod(blocker, [blocker, ms]() {
        QThread::msleep(static_cast<unsigned long>(ms));
        blocker->deleteLater();
    }, Qt::QueuedConnection);
}

}  // namespace

TEST_SUITE(session156)
{
    // =========================================================================
    //  1. Which adapter is which — the planner, on its own
    // =========================================================================
    {
        // Two CH340s (no serial numbers), IOA Input in socket L1 as ttyUSB0
        // and IOA Output in L2 as ttyUSB1. Both pulled; plugged back in the
        // OTHER order, so the kernel hands out the names the other way round.
        const QVector<SerialAdapterId> lost{ lostId(QStringLiteral("/dev/ttyUSB0"), QString(), QStringLiteral("L1")),
                                             lostId(QStringLiteral("/dev/ttyUSB1"), QString(), QStringLiteral("L2")) };
        const SerialPortList swapped{ snap(QStringLiteral("ttyUSB0"), QString(), QStringLiteral("L2")),
                                      snap(QStringLiteral("ttyUSB1"), QString(), QStringLiteral("L1")) };
        const QVector<SerialReconnectPlan> plans = SerialScan::planReconnects(lost, swapped);
        SerialMatchBy by = SerialMatchBy::None;
        CHECK(planFor(plans, 0, &by) == QLatin1String("/dev/ttyUSB1") && by == SerialMatchBy::Location,
              "replugged in the other order: IOA Input follows its socket to ttyUSB1 (the name would have said ttyUSB0)");
        CHECK(planFor(plans, 1) == QLatin1String("/dev/ttyUSB0"), "and IOA Output to ttyUSB0");
        CHECK(plans.size() == 2, "each gets one adapter");
    }
    {
        // Clone FTDI chips: both report serial A50285BI.
        const SerialPortList clones{ snap(QStringLiteral("ttyUSB0"), QStringLiteral("A50285BI"), QStringLiteral("L1"), 0x0403, 0x6001),
                                     snap(QStringLiteral("ttyUSB1"), QStringLiteral("A50285BI"), QStringLiteral("L2"), 0x0403, 0x6001) };
        const SerialAdapterId id = SerialScan::identify(QStringLiteral("/dev/ttyUSB0"), clones);
        CHECK(id.serial.isEmpty() && id.location == QLatin1String("L1"),
              "a serial number another adapter shares is not kept; the socket is");
        CHECK(!SerialScan::serialIsUnique(clones, QStringLiteral("A50285BI")), "shared serial is not unique");

        // A genuine serial, and a clone of it plugged in later elsewhere.
        const SerialAdapterId real = lostId(QStringLiteral("COM5"), QStringLiteral("FT1X2Y3Z"), QStringLiteral("L1"), 0x0403, 0x6001);
        const SerialPortList now{ snap(QStringLiteral("COM9"), QStringLiteral("FT1X2Y3Z"), QStringLiteral("L3"), 0x0403, 0x6001),
                                  snap(QStringLiteral("COM7"), QStringLiteral("FT1X2Y3Z"), QStringLiteral("L1"), 0x0403, 0x6001) };
        CHECK(planFor(SerialScan::planReconnects({ real }, now), 0) == QLatin1String("COM7"),
              "a serial that has become shared is settled by the socket");
    }
    {
        // An adapter with its own serial, not back; another sits at its old name.
        const SerialAdapterId id = lostId(QStringLiteral("COM5"), QStringLiteral("FT1X2Y3Z"), QString(), 0x0403, 0x6001);
        const SerialPortList ports{ snap(QStringLiteral("COM5"), QString(), QStringLiteral("L9")) };
        CHECK(SerialScan::planReconnects({ id }, ports).isEmpty(),
              "an adapter known by serial is not replaced by whatever now has its name");
    }
    {
        // No serial, moved to another socket: the only free CH340 there is.
        const SerialAdapterId id = lostId(QStringLiteral("COM5"), QString(), QStringLiteral("L1"));
        const SerialPortList ports{ snap(QStringLiteral("COM8"), QString(), QStringLiteral("L4")),
                                    snap(QStringLiteral("COM3"), QString(), QStringLiteral("L5"), 0x0403, 0x6001) };
        SerialMatchBy by = SerialMatchBy::None;
        CHECK(planFor(SerialScan::planReconnects({ id }, ports), 0, &by) == QLatin1String("COM8") && by == SerialMatchBy::Model,
              "moved to another socket: the only free adapter of its model");
        // Two lost CH340s and one comes back somewhere new: it could be either.
        const SerialAdapterId other = lostId(QStringLiteral("COM6"), QString(), QStringLiteral("L2"));
        CHECK(SerialScan::planReconnects({ id, other }, ports).isEmpty(),
              "with two lost of that model, a newcomer in an unknown socket is given to neither");
        // A different model in its own socket is a different adapter.
        const SerialPortList ftdiThere{ snap(QStringLiteral("COM5"), QString(), QStringLiteral("L1"), 0x0403, 0x6001) };
        CHECK(SerialScan::planReconnects({ id }, ftdiThere).isEmpty(), "another model in its socket is not it");
    }
    {
        const SerialAdapterId uart = lostId(QStringLiteral("/dev/ttyS0"), QString(), QString(), 0, 0);
        const SerialPortList ports{ snap(QStringLiteral("ttyS0"), QString(), QString(), 0, 0) };
        SerialMatchBy by = SerialMatchBy::None;
        CHECK(planFor(SerialScan::planReconnects({ uart }, ports), 0, &by) == QLatin1String("/dev/ttyS0") && by == SerialMatchBy::Name,
              "a built-in UART (known by nothing else) is found by its name, full path kept");
        CHECK(SerialScan::planReconnects({ uart }, ports, { QStringLiteral("/dev/ttyS0") }).isEmpty(),
              "never a port that is already open");
        const SerialAdapterId moved = lostId(QStringLiteral("/dev/ttyUSB0"), QString(), QStringLiteral("L1"));
        const SerialPortList renamed{ snap(QStringLiteral("ttyUSB3"), QString(), QStringLiteral("L1")) };
        CHECK(planFor(SerialScan::planReconnects({ moved }, renamed), 0) == QLatin1String("/dev/ttyUSB3"),
              "a path-named port comes back as a path");
    }
#ifdef Q_OS_UNIX
    // Unix only (session 163): the console reads /dev/serial/by-path on
    // Linux alone, and on Windows QFile::link makes .lnk shortcuts, not the
    // symlinks udev makes, so this failed there on CI.
    {
        // /dev/serial/by-path: the link names are the sockets.
        QTemporaryDir dir;
        QFile(dir.filePath(QStringLiteral("ttyUSB0"))).open(QIODevice::WriteOnly);
        QFile(dir.filePath(QStringLiteral("ttyUSB1"))).open(QIODevice::WriteOnly);
        QDir().mkpath(dir.filePath(QStringLiteral("by-path")));
        QFile::link(dir.filePath(QStringLiteral("ttyUSB0")), dir.filePath(QStringLiteral("by-path/pci-0000:00:14.0-usb-0:2:1.0-port0")));
        QFile::link(dir.filePath(QStringLiteral("ttyUSB1")), dir.filePath(QStringLiteral("by-path/pci-0000:00:14.0-usb-0:3:1.0-port0")));
        QFile::link(dir.filePath(QStringLiteral("gone")), dir.filePath(QStringLiteral("by-path/dangling")));
        const QHash<QString, QString> locs = SerialScan::locationsFromByPath(dir.filePath(QStringLiteral("by-path")));
        CHECK(locs.value(QStringLiteral("ttyUSB0")) == QLatin1String("pci-0000:00:14.0-usb-0:2:1.0-port0")
                  && locs.value(QStringLiteral("ttyUSB1")) == QLatin1String("pci-0000:00:14.0-usb-0:3:1.0-port0")
                  && locs.size() == 2,
              "udev's by-path links give each device its socket; a dangling link is skipped");
    }
#endif
    CHECK(SerialManager::captureTypeOf(kDop1) == QLatin1String("dop1")
              && SerialManager::captureTypeOf(kHlth) == QLatin1String("nmshlth")
              && SerialManager::captureTypeOf("@analog_top_1_1 2026-06-27T14:02:26 1 00") == QLatin1String("analog_top")
              && SerialManager::captureTypeOf("boot ok").isEmpty(),
          "the packet type, without the loco's suffix");

    // =========================================================================
    //  2. The scan thread
    // =========================================================================
    {
        FakeBus bus;
        bus.setDelay(300);
        SerialPortScanner scanner;
        scanner.setScanFunction([&bus]() { return bus.read(); });
        QSignalSpy got(&scanner, &SerialPortScanner::scanned);
        QElapsedTimer t;
        t.start();
        scanner.request();
        scanner.request();
        scanner.request();
        CHECK(t.elapsed() < 100, QByteArray("asking for a scan does not wait for it (") + QByteArray::number(t.elapsed()) + " ms)");
        CHECK(waitFor([&]() { return scanner.scansDone() == 2; }, 3000), "three asks during a scan are one more scan, not three");
        settle(400);
        CHECK(scanner.scansDone() == 2 && got.count() == 2, "and no more");
        CHECK(bus.thread() && bus.thread() != QThread::currentThread(), "the enumerator runs off the GUI thread");
    }

#ifdef Q_OS_UNIX
    // =========================================================================
    //  1, end to end: a swap of two socket-identified adapters
    // =========================================================================
    {
        Pty a, b;
        if (!a.ok() || !b.ok()) { CHECK(false, "two ptys"); return; }
        FakeBus bus;
        bus.set({ a.at(QStringLiteral("L1")), b.at(QStringLiteral("L2")) });
        MessageDispatcher disp;
        SerialManager mgr(&disp);
        mgr.setReconnectIntervalMs(50);
        mgr.setPortScanFunction([&bus]() { return bus.read(); });

        SerialConfig in;  in.portName = a.path;
        SerialConfig out; out.portName = b.path;
        CHECK(mgr.open(in) && mgr.open(out), "IOA Input on A (socket L1), IOA Output on B (socket L2)");
        mgr.setLabel(a.path, QStringLiteral("IOA Input"));
        mgr.setLabel(b.path, QStringLiteral("IOA Output"));
        CHECK(waitFor([&]() { return mgr.adapterOf(a.path).location == QLatin1String("L1")
                                     && mgr.adapterOf(b.path).location == QLatin1String("L2"); }, 2000),
              "each is known by its socket, from a scan after it opened");
        const QString tabIn = SerialManager::tabKeyFor(a.path);
        const QString tabOut = SerialManager::tabKeyFor(b.path);
        a.print(kHlth);                         // the input card
        b.print(kDop1);                         // the output card
        b.print(kDop2);
        settle(250);
        CHECK(countOf(tabTexts(disp, tabIn), kHlth) == 1 && countOf(tabTexts(disp, tabOut), kDop1) == 1,
              "lines before the loss reach their tabs");

        // Both pulled; plugged back in the other order.
        SerialLink *linkIn = mgr.link(a.path);
        SerialLink *linkOut = mgr.link(b.path);
        bus.set({});
        linkIn->onError(QSerialPort::ResourceError);
        linkOut->onError(QSerialPort::ResourceError);
        CHECK(mgr.reconnectingPorts().size() == 2, "both lost, both waited for");
        bus.set({ a.at(QStringLiteral("L2")), b.at(QStringLiteral("L1")) });
        CHECK(waitFor([&]() { return linkIn->isOpen() && linkOut->isOpen(); }, 3000), "both come back");
        CHECK(linkIn->config().portName == b.path && linkOut->config().portName == a.path,
              "each followed its socket: Input is now on B, Output on A");
        CHECK(tabTexts(disp, tabIn).last().contains(QLatin1String("USB socket")),
              QByteArray("the tab says how it was found: ") + tabTexts(disp, tabIn).last().toUtf8());

        // The cards keep talking, through their own adapters.
        b.print(kHlth);                         // the input card, now behind B
        a.print(kDop1);                         // the output card, now behind A
        settle(300);
        CHECK(countOf(tabTexts(disp, tabIn), kHlth) == 2 && countOf(tabTexts(disp, tabIn), kDop1) == 0,
              "the IOA Input tab gets the input card's lines, and none of the output card's");
        CHECK(countOf(tabTexts(disp, tabOut), kDop1) == 2 && countOf(tabTexts(disp, tabOut), kHlth) == 0,
              "and the IOA Output tab the output card's");
        CHECK(!mgr.isSuspect(b.path) && !mgr.isSuspect(a.path), "neither is held: both were confirmed by their first line");
        CHECK(mgr.label(b.path) == QLatin1String("IOA Input") && mgr.label(a.path) == QLatin1String("IOA Output"),
              "names follow the entries, so the chips read right");
        mgr.closeAll();
    }

    // =========================================================================
    //  1, the backstop: reconnected to the wrong card anyway
    // =========================================================================
    {
        Pty a, c;
        if (!a.ok() || !c.ok()) { CHECK(false, "two ptys"); return; }
        MessageDispatcher disp;
        SerialManager mgr(&disp);
        mgr.setReconnectIntervalMs(50);
        // The old name-only finder, made to pick wrong: as two swapped CH340s did.
        QString back;
        mgr.setPortFinder([&](const QString &, const QString &) { return back; });
        QSignalSpy suspectSpy(&mgr, &SerialManager::portSuspect);

        SerialConfig in; in.portName = a.path;
        CHECK(mgr.open(in), "IOA Input opens");
        const QString tab = SerialManager::tabKeyFor(a.path);
        a.print(kHlth);
        settle(200);

        // ---- back on the right card: a banner, then a known type -------------
        mgr.link(a.path)->onError(QSerialPort::ResourceError);
        back = a.path;
        CHECK(waitFor([&]() { return mgr.link(a.path)->isOpen(); }, 2000), "back on A");
        CHECK(mgr.isVerifying(a.path), "its lines wait until they show which card it is");
        a.print("IOA boot v2.3");
        settle(150);
        CHECK(!tabTexts(disp, tab).contains(QStringLiteral("IOA boot v2.3")), "a banner is no evidence: held");
        a.print(kHlth);
        settle(200);
        QStringList t = tabTexts(disp, tab);
        CHECK(!mgr.isVerifying(a.path) && t.size() >= 2 && t.at(t.size() - 2) == QLatin1String("IOA boot v2.3")
                  && t.last() == QString::fromLatin1(kHlth),
              "a known type confirms it: the held lines go in, in order");

        // ---- back on the WRONG card ------------------------------------------------
        mgr.link(a.path)->onError(QSerialPort::ResourceError);
        back = c.path;                           // the output card's adapter
        CHECK(waitFor([&]() { return mgr.link(c.path) && mgr.link(c.path)->isOpen(); }, 2000), "reconnected to C");
        const int before = tabTexts(disp, tab).size();
        for (int i = 0; i < SerialManager::kVerifyLines; ++i) c.print(i % 2 ? kDop2 : kDop1);
        settle(300);
        CHECK(mgr.isSuspect(c.path) && suspectSpy.count() == 1, "five lines of types the tab never had: held as a different card");
        t = tabTexts(disp, tab);
        CHECK(countOf(t, kDop1) == 0 && countOf(t, kDop2) == 0, "not one of them reached the IOA Input tab");
        CHECK(t.size() == before + 1 && t.last().contains(QLatin1String("different card"))
                  && t.last().contains(QLatin1String("@dop1")) && t.last().contains(QLatin1String("@nmshlth")),
              QByteArray("the tab says why, naming what it had and what came: ") + t.last().toUtf8());
        c.print(kDop1);
        settle(150);
        CHECK(countOf(tabTexts(disp, tab), kDop1) == 0, "it stays held");
        CHECK(mgr.link(c.path)->isOpen(), "but open: visible in its terminal, still recorded nowhere wrong");

        auto *w = new SerialConsoleWindow(&mgr);
        w->setAttribute(Qt::WA_DeleteOnClose, false);
        w->showPort(c.path);
        auto *feedAnyway = w->findChild<QPushButton *>(QStringLiteral("serialFeedAnyway"));
        CHECK(feedAnyway && !feedAnyway->isHidden() && w->statusText().contains(QLatin1String("different card")),
              "the terminal says so, and offers Feed anyway");
        feedAnyway->click();
        c.print(kDop1);
        settle(200);
        CHECK(!mgr.isSuspect(c.path) && countOf(tabTexts(disp, tab), kDop1) == 1 && feedAnyway->isHidden(),
              "Feed anyway: the operator's word; what it sends now goes in");
        delete w;

        // ---- reconnected, a couple of strangers, then silence -------------------------
        mgr.link(c.path)->onError(QSerialPort::ResourceError);
        back = a.path;
        CHECK(waitFor([&]() { return mgr.link(a.path)->isOpen() && mgr.isVerifying(a.path); }, 2000), "back on A, checking");
        a.print(kHlth);                          // a type it has not had since Feed anyway (dop only)
        a.print(kHlth);
        CHECK(waitFor([&]() { return mgr.isSuspect(a.path); }, int(SerialManager::kVerifyMs) + 1500),
              "a few lines of strange types, then quiet: held when the time is up");

        // ---- reconnected to a quiet card: fed, and said so ---------------------------------
        mgr.feedAnyway(a.path);
        a.print(kHlth);
        settle(150);
        mgr.link(a.path)->onError(QSerialPort::ResourceError);
        CHECK(waitFor([&]() { return mgr.link(a.path)->isOpen() && mgr.isVerifying(a.path); }, 2000), "back again");
        CHECK(waitFor([&]() { return !mgr.isVerifying(a.path); }, int(SerialManager::kVerifyMs) + 1500), "the check ends");
        t = tabTexts(disp, tab);
        CHECK(!mgr.isSuspect(a.path) && t.last().contains(QLatin1String("could not confirm")),
              "no log line at all: nothing to judge by — fed, and the tab says it was not confirmed");
        a.print(kHlth);
        settle(150);
        CHECK(tabTexts(disp, tab).last() == QString::fromLatin1(kHlth), "and its lines go in");

        // A Close and an Open is the operator's word too.
        mgr.close(a.path);
        CHECK(mgr.open(in) && !mgr.isSuspect(a.path) && !mgr.isVerifying(a.path), "an explicit Open is not checked");
        mgr.closeAll();
    }

    // =========================================================================
    //  2. Reconnect searches off the GUI thread
    // =========================================================================
    {
        Pty a, b, c;
        if (!a.ok() || !b.ok() || !c.ok()) { CHECK(false, "three ptys"); return; }
        FakeBus bus;
        bus.set({ a.at(QStringLiteral("L1")), b.at(QStringLiteral("L2")), c.at(QStringLiteral("L3")) });
        MessageDispatcher disp;
        SerialManager mgr(&disp);
        mgr.setReconnectIntervalMs(50);
        mgr.setPortScanFunction([&bus]() { return bus.read(); });
        for (const Pty *p : { &a, &b, &c }) {
            SerialConfig cfg;
            cfg.portName = p->path;
            mgr.open(cfg);
        }
        CHECK(waitFor([&]() { return mgr.adapterOf(c.path).location == QLatin1String("L3"); }, 2000), "three open, identified");

        // Three adapters out, and an enumerator as slow as Windows can be.
        bus.set({});
        bus.setDelay(400);
        for (const Pty *p : { &a, &b, &c }) mgr.link(p->path)->onError(QSerialPort::ResourceError);
        settle(500);                              // let the first scan start
        const int scansBefore = bus.scanCount();
        qint64 worst = 0, last = QDateTime::currentMSecsSinceEpoch();
        QTimer beat;
        beat.setInterval(10);
        QObject::connect(&beat, &QTimer::timeout, [&]() {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            worst = qMax(worst, now - last);
            last = now;
        });
        beat.start();
        settle(2000);
        beat.stop();
        const int scans = bus.scanCount() - scansBefore;
        CHECK(worst < 150, QByteArray("with three adapters out and a 400 ms enumerator, the GUI never stalls (worst gap ")
                               + QByteArray::number(worst) + " ms)");
        CHECK(scans >= 3 && scans <= 7, QByteArray("one scan at a time for all three, not one per port per tick (")
                                            + QByteArray::number(scans) + " scans in 2 s)");
        CHECK(bus.thread() != QThread::currentThread(), "on the scan thread");
        bus.setDelay(0);
        bus.set({ a.at(QStringLiteral("L1")), b.at(QStringLiteral("L2")), c.at(QStringLiteral("L3")) });
        CHECK(waitFor([&]() { return mgr.openPorts().size() == 3; }, 3000), "and when they are back, all three reopen");

        // A scan already running when an adapter is pulled still lists it.
        // That list says nothing about the adapter being back.
        CHECK(waitFor([&]() { return !mgr.scanner()->busy(); }, 2000), "scanner idle");
        bus.setDelay(400);
        mgr.scanner()->request();                 // takes its snapshot: a is there
        settle(100);
        bus.set({ b.at(QStringLiteral("L2")), c.at(QStringLiteral("L3")) });
        mgr.link(a.path)->onError(QSerialPort::ResourceError);
        settle(1500);                             // the stale scan lands, then fresh ones
        CHECK(mgr.isReconnecting(a.path) && !mgr.link(a.path)->isOpen(),
              "a scan that began before the loss does not reopen the port (its pty is still there)");
        bus.setDelay(0);
        bus.set({ a.at(QStringLiteral("L1")), b.at(QStringLiteral("L2")), c.at(QStringLiteral("L3")) });
        CHECK(waitFor([&]() { return mgr.link(a.path)->isOpen(); }, 3000), "a scan after it does");
        mgr.closeAll();
    }

    // =========================================================================
    //  3. Opening without waiting
    // =========================================================================
    {
        Pty a;
        if (!a.ok()) { CHECK(false, "a pty"); return; }
        SerialLink link;
        QSignalSpy opened(&link, &SerialLink::opened);
        QSignalSpy failed(&link, &SerialLink::openFailed);
        SerialConfig cfg;
        cfg.portName = a.path;

        // A driver that takes 600 ms to open.
        stallReader(&link, 600);
        QElapsedTimer t;
        t.start();
        link.openAsync(cfg);
        const qint64 asyncMs = t.elapsed();
        CHECK(asyncMs < 100 && link.isOpening() && !link.isOpen(),
              QByteArray("openAsync returns at once while the driver is busy (") + QByteArray::number(asyncMs) + " ms)");
        CHECK(waitFor([&]() { return opened.count() == 1; }, 3000) && link.isOpen() && !link.isOpening(),
              "and opened() follows");
        QSignalSpy lines(&link, &SerialLink::lineReceived);
        a.print(kDop1);
        CHECK(waitFor([&]() { return lines.count() == 1; }, 1000), "lines flow");
        link.close();

        // The blocking form, for contrast: the same busy driver holds the caller.
        stallReader(&link, 600);
        t.restart();
        CHECK(link.open(cfg), "open() still works");
        CHECK(t.elapsed() >= 450, QByteArray("open() waits for the driver (") + QByteArray::number(t.elapsed()) + " ms) — what Open used to do");
        link.close();

        // Cancelled while opening: nothing opens, nothing is announced.
        opened.clear();
        stallReader(&link, 300);
        link.openAsync(cfg);
        link.close();
        settle(500);
        CHECK(opened.isEmpty() && !link.isOpen() && !link.isOpening(), "a close while opening cancels it");

        // A port that is not there.
        SerialConfig gone;
        gone.portName = QStringLiteral("/dev/no-such-port-156");
        link.openAsync(gone);
        CHECK(waitFor([&]() { return failed.count() == 1; }, 2000) && !link.isOpen(), "a failed open says so by openFailed()");
    }
    {
        // The terminal's Open button.
        Pty a;
        if (!a.ok()) { CHECK(false, "a pty"); return; }
        SerialManager mgr(nullptr);
        auto *w = new SerialConsoleWindow(&mgr);
        w->setAttribute(Qt::WA_DeleteOnClose, false);
        SerialConfig cfg;
        cfg.portName = a.path;
        w->setConfigToUi(cfg);
        stallReader(mgr.linkFor(a.path), 500);
        auto *openBtn = w->findChild<QPushButton *>(QStringLiteral("serialOpen"));
        QElapsedTimer t;
        t.start();
        openBtn->click();
        CHECK(t.elapsed() < 100 && w->statusText().contains(QLatin1String("Opening")) && openBtn->text() == QLatin1String("Cancel"),
              QByteArray("Open returns at once and says Opening, with Cancel (") + QByteArray::number(t.elapsed()) + " ms)");
        const bool nowOpen = waitFor([&]() { return w->statusText().contains(QLatin1String("open,")); }, 3000);
        // Session 163: failed once on Linux CI still "Opening" after 3 s, and
        // not reproduced here. The link's own state, and how long, say where.
        SerialLink *lk = mgr.link(a.path);
        CHECK(nowOpen && openBtn->text() == QLatin1String("Close") && w->receivedText().contains(QLatin1String("opened")),
              QByteArray("then it is open, as before (") + w->statusText().toUtf8() + " / " + openBtn->text().toUtf8()
                  + " · link " + (lk && lk->isOpen() ? "open" : "not open") + (lk && lk->isOpening() ? ", opening" : "")
                  + " · " + QByteArray::number(t.elapsed()) + " ms since Open)");
        openBtn->click();
        CHECK(!mgr.link(a.path)->isOpen(), "Close closes");
        cfg.portName = QStringLiteral("/dev/no-such-port-156");
        w->setConfigToUi(cfg);
        openBtn->click();
        CHECK(waitFor([&]() { return w->statusText().startsWith(QStringLiteral("✕")); }, 2000),
              QByteArray("a port that will not open shows its error: ") + w->statusText().toUtf8());
        delete w;
    }
    {
        // Profiles, without waiting — and never by an old name that is now another card.
        Pty a, b;
        if (!a.ok() || !b.ok()) { CHECK(false, "two ptys"); return; }
        FakeBus bus;
        bus.set({ a.at(QStringLiteral("L1")), b.at(QStringLiteral("L2")) });
        MessageDispatcher disp;
        SerialManager mgr(&disp);
        mgr.setPortScanFunction([&bus]() { return bus.read(); });
        QSignalSpy done(&mgr, &SerialManager::profilesOpened);

        SerialProfile input;
        input.name = QStringLiteral("IOA Input");
        input.config.portName = QStringLiteral("/dev/no-longer-this-name");   // renamed since it was saved
        input.usbLocation = QStringLiteral("L1");
        SerialProfile analog;
        analog.name = QStringLiteral("IOA Analog");
        analog.config.portName = b.path;          // its old name, now another adapter's
        analog.usbSerial = QStringLiteral("GONE-156");
        mgr.openProfilesAsync({ input, analog });
        CHECK(waitFor([&]() { return done.count() == 1; }, 3000), "one report when all have finished");
        const QList<QVariant> args = done.isEmpty() ? QList<QVariant>() : done.first();
        const QStringList failedList = args.size() > 1 ? args.at(1).toStringList() : QStringList();
        CHECK(args.size() == 2 && args.at(0).toInt() == 1, "one opened");
        CHECK(mgr.openPorts() == QStringList{ a.path } && mgr.label(a.path) == QLatin1String("IOA Input"),
              "IOA Input found by its socket under its new name");
        CHECK(failedList.size() == 1 && failedList.first().contains(QLatin1String("not plugged in"))
                  && (!mgr.link(b.path) || !mgr.link(b.path)->isOpen()),
              "IOA Analog's adapter is missing: B, now at its old name, is NOT opened as IOA Analog");
        done.clear();
        mgr.openProfilesAsync({ input });
        CHECK(waitFor([&]() { return done.count() == 1; }, 2000) && done.first().at(0).toInt() == 1
                  && done.first().at(1).toStringList().isEmpty(),
              "a second Open all leaves a running profile running");
        mgr.closeAll();
    }
#endif
}
