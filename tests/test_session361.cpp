#include "testutil.h"
#include "layoutaudit.h"

#include "biuprotocol.h"
#include "biusim.h"
#include "brakebenchwindow.h"
#include "flasher/flashercore.h"
#include "settings.h"
#include "theme.h"
#include "uistyle.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QNetworkDatagram>
#include <QSettings>
#include <QTabWidget>
#include <QUdpSocket>

// =============================================================================
//  Session 361 — the Brake Test Bench answers all four VCCs.
//
//  docs/BRAKE_BENCH_NEXT.md item 4: "A loco has 4 VCCs. Only one sends the
//  BIU command, but the BIU's frames (heartbeat, NMT, status) must reach all
//  four so each keeps its state." Until now the bench answered the sender of
//  the last frame, or one fixed address. Now: a list, each frame one datagram
//  to every address on it, each on port 50001 unless it names its own.
//
//  The four addresses offered are the Firmware Flasher's per-loco controller
//  table (session 300). .126 and .101 are the PCs DLConsole runs on.
// =============================================================================

using namespace Biu;

namespace {

QByteArray command361()
{
    Command c;
    c.speedRaw = 1000;
    c.d1 = (1u << CmdHealth) | (1u << CmdSpeedValid);
    c.d2 = (1u << CmdA9Valid) | (1u << CmdSa9Valid);
    c.a9Raw = 88;
    c.sa9Raw = 24;
    return toDatagram({ kCobCommand, encodeCommand(c) });
}

// Each datagram the simulator sent, by where it went.
struct SentTo {
    QMap<QString, QVector<Frame>> by;
    int datagrams = 0;
    void add(const QByteArray &d, const QString &to) { Frame f; if (fromDatagram(d, &f)) by[to] << f; ++datagrams; }
    int count(const QString &to, quint16 cob) const
    {
        int n = 0;
        for (const Frame &f : by.value(to)) if (f.cob == cob) ++n;
        return n;
    }
    void clear() { by.clear(); datagrams = 0; }
};

QStringList loco1()
{
    QStringList ips;
    for (const Flasher::LocoController &c : Flasher::locoControllers())
        if (c.loco == 1) ips << c.ip;
    return ips;
}

void clearSaved361()
{
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    s.remove(QStringLiteral("brakebench"));
}

}  // namespace

TEST_SUITE(session361)
{
    const QStringList four = loco1();
    CHECK(four == QStringList({ QStringLiteral("192.168.25.16"), QStringLiteral("192.168.25.102"),
                                QStringLiteral("192.168.25.103"), QStringLiteral("192.168.25.104") }),
          "loco 1's four VCC controllers: .16 .102 .103 .104 (session 300's table)");

    // ---- the list's syntax ------------------------------------------------------------------------
    {
        QStringList hosts;
        QString why;
        CHECK(BiuSimulator::parseHosts(QStringLiteral(" 192.168.25.16, 192.168.25.102;192.168.25.103  127.0.0.1:50012 "), &hosts, &why)
                  && hosts.size() == 4 && hosts.last() == QLatin1String("127.0.0.1:50012"),
              "commas, semicolons and spaces separate; a.b.c.d:port is kept");
        CHECK(BiuSimulator::parseHosts(QString(), &hosts, &why) && hosts.isEmpty(), "an empty list parses as empty");
        CHECK(!BiuSimulator::parseHosts(QStringLiteral("192.168.25"), &hosts, &why) && why.contains(QLatin1String("192.168.25")),
              "\"192.168.25\" refused (QHostAddress alone reads it as 192.168.0.25)");
        CHECK(!BiuSimulator::parseHosts(QStringLiteral("192.168.25.256"), &hosts, &why), "an octet above 255 refused");
        CHECK(!BiuSimulator::parseHosts(QStringLiteral("192.168.25.16:0"), &hosts, &why)
                  && !BiuSimulator::parseHosts(QStringLiteral("192.168.25.16:65536"), &hosts, &why),
              "port 0 and 65536 refused");
        CHECK(!BiuSimulator::parseHosts(QStringLiteral("192.168.25.16, 192.168.25.16"), &hosts, &why) && why.contains(QLatin1String("twice")),
              "an address listed twice refused");

        BiuSimulator sim;
        CHECK(sim.setReplyHosts(four) && sim.replyHosts() == four, "the four set");
        CHECK(!sim.setReplyHosts({ QStringLiteral("192.168.25.16"), QStringLiteral("vcc2") }, &why) && sim.replyHosts() == four,
              "a bad entry refuses the whole list, and the old one stays");
        CHECK(sim.replyTargets().size() == 4 && sim.replyTargets().first() == QLatin1String("192.168.25.16:50001"),
              "each on the reply port, 50001");
        sim.setReplyHosts({ QStringLiteral("192.168.25.16"), QStringLiteral("127.0.0.1:50012") });
        CHECK(sim.replyTargets().last() == QLatin1String("127.0.0.1:50012"), "an entry's own port wins over 50001");
        sim.setReplyHost(QString());
        CHECK(sim.replyHosts().isEmpty() && sim.replyTargets().isEmpty(), "empty: the sender again, nobody until one sends");
    }

    // ---- every frame reaches all four -------------------------------------------------------------
    {
        BiuSimulator sim;
        SentTo sent;
        sim.setSendToSink([&](const QByteArray &d, const QString &to) { sent.add(d, to); });
        sim.setReplyHosts(four);
        qint64 t = 1000;
        sim.setRunning(true, t);
        for (const QString &ip : four)
            CHECK(sent.count(ip + QStringLiteral(":50001"), kCobHeartbeatBiu) == 1, qPrintable(ip + QStringLiteral(" gets the boot-up")));
        CHECK(sim.framesSent() == 1 && sim.datagramsSent() == 4, "one frame, four datagrams");

        // The commanding VCC is one of them; the BIU answers all four, not just it.
        sim.receive(toDatagram({ kCobHeartbeatKavach, QByteArray(1, char(PreOperational)) }), four[1], 50001, t);
        sim.receive(command361(), four[1], 50001, t);
        sim.tick(t);
        sent.clear();
        const int framesBefore = sim.framesSent();
        for (int i = 0; i < 20; ++i) { t += 50; sim.tick(t); }      // 1 s
        bool all = true, same = true;
        for (const QString &ip : four) {
            const QString to = ip + QStringLiteral(":50001");
            all = all && sent.count(to, kCobHeartbeatBiu) == 2 && sent.count(to, kCobPressures) == 4
                  && sent.count(to, kCobCodes) == 4 && sent.count(to, kCobDiscretes) == 4 && sent.count(to, kCobNmt) >= 1;
            same = same && sent.by.value(to).size() == sent.by.value(four[0] + QStringLiteral(":50001")).size();
        }
        CHECK(all, "in 1 s each of the four gets 2 heartbeats, 4 of each status frame and the NMT start");
        CHECK(same && sent.datagrams == 4 * (sim.framesSent() - framesBefore), "every frame, to each of the four");
        CHECK(sent.by.size() == 4, "and nobody else (not the sender's own port)");
        CHECK(sim.lastPeer().startsWith(four[1]) && sim.inReplyList(four[1]), "the command came from .102, which is listed");

        // A frame log item names all four, once per frame.
        QVector<BiuSimulator::LogItem> items;
        QObject::connect(&sim, &BiuSimulator::logged, [&](const BiuSimulator::LogItem &it) { items << it; });
        t += 300;
        sim.tick(t);
        bool named = !items.isEmpty();
        for (const BiuSimulator::LogItem &it : items) named = named && it.tx && it.peer.count(QLatin1Char(',')) == 3;
        CHECK(named, "the Frames tab gets one row per frame, naming the four");

        // A command from an address off the list is pointed out.
        items.clear();
        sim.receive(command361(), QStringLiteral("192.168.25.77"), 50001, t);
        CHECK(!items.isEmpty() && !items.last().tx && items.last().note.contains(QLatin1String("not in the reply list")),
              "a command from .77, not listed: the log says it is sent nothing");
        items.clear();
        sim.receive(command361(), four[0], 50001, t);
        CHECK(!items.isEmpty() && !items.last().note.contains(QLatin1String("reply list")), "from a listed VCC: no such note");
    }

    // ---- sender mode is as it was -----------------------------------------------------------------
    {
        BiuSimulator sim;
        SentTo sent;
        sim.setSendToSink([&](const QByteArray &d, const QString &to) { sent.add(d, to); });
        qint64 t = 1000;
        sim.setRunning(true, t);
        CHECK(sent.by.contains(QString()) && sent.datagrams == 1, "nobody heard from yet: the boot-up goes nowhere in particular");
        sim.receive(command361(), QStringLiteral("192.168.25.16"), 40000, t);
        sent.clear();
        t += 300;
        sim.tick(t);
        CHECK(sent.by.size() == 1 && sent.by.contains(QStringLiteral("192.168.25.16:50001")),
              "the sender only, on 50001 (not its own source port)");
        sim.receive(command361(), QStringLiteral("192.168.25.16"), 40000, t);
        CHECK(!sim.inReplyList(QStringLiteral("192.168.25.16")), "sender mode has no list to be off");
    }

    // ---- over real sockets: two VCCs on localhost -------------------------------------------------
    {
        BiuSimulator sim;
        QString why;
        CHECK(sim.listen(0, &why), qPrintable(QStringLiteral("the BIU binds (%1)").arg(why)));
        QUdpSocket a, b;
        CHECK(a.bind(QHostAddress(QHostAddress::LocalHost), 0) && b.bind(QHostAddress(QHostAddress::LocalHost), 0), "fixture: two VCC sockets");
        CHECK(sim.setReplyHosts({ QStringLiteral("127.0.0.1:%1").arg(a.localPort()), QStringLiteral("127.0.0.1:%1").arg(b.localPort()) }),
              "both listed, each with its port");
        qint64 t = QDateTime::currentMSecsSinceEpoch();
        sim.setRunning(true, t);
        a.writeDatagram(command361(), QHostAddress(QHostAddress::LocalHost), sim.listenPort());   // only A commands
        int gotA = 0, gotB = 0;
        QElapsedTimer el;
        el.start();
        while (el.elapsed() < 2000 && (gotA < 3 || gotB < 3 || !sim.hasCommand())) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            sim.tick(QDateTime::currentMSecsSinceEpoch());
            a.waitForReadyRead(5);
            for (QUdpSocket *s : { &a, &b })
                while (s->hasPendingDatagrams()) {
                    Frame f;
                    if (fromDatagram(s->receiveDatagram().data(), &f) && f.cob >= kCobPressures) ++(s == &a ? gotA : gotB);
                }
        }
        CHECK(sim.hasCommand(), "A's command arrived");
        CHECK(gotA >= 3 && gotB >= 3, qPrintable(QStringLiteral("status frames reached both, the silent one too (%1, %2)").arg(gotA).arg(gotB)));
        sim.close();
    }

    // ---- the window ---------------------------------------------------------------------------------
    clearSaved361();
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    {
        BrakeBenchWindow w(nullptr);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        qint64 t = 1000;
        w.setClock([&t]() { return t; });
        SentTo sent;
        w.biu()->setSendToSink([&](const QByteArray &d, const QString &to) { sent.add(d, to); });
        auto *hosts = w.findChild<QLineEdit *>(QStringLiteral("biuReplyHosts"));
        CHECK(hosts && w.biu()->replyHosts().isEmpty() && hosts->text() == four.join(QStringLiteral(", ")),
              "starts answering the sender, with loco 1's four ready in the list");
        CHECK(w.replyListText().contains(QLatin1String("hear nothing")), "and says the other VCCs hear nothing");

        w.setReplyTo(four.join(QStringLiteral(", ")), 50001);
        CHECK(w.biu()->replyHosts() == four, "these VCCs: the four");
        CHECK(w.replyListText().contains(QStringLiteral("4 VCCs")) && w.replyListText().contains(QStringLiteral("Loco 1 \u00B7 VCC 2 controller 2")),
              "the tab lists them by controller");
        w.biu()->setRunning(true, t);
        w.biu()->receive(command361(), four[2], 50001, t);
        w.advance(0.5);
        CHECK(w.replyListText().contains(QStringLiteral("192.168.25.103:50001  \u00B7  Loco 1 \u00B7 VCC 2 controller 1  \u00B7  sends the command")),
              "and marks the one that sends the command");
        int reached = 0;
        for (const QString &ip : four) reached += sent.count(ip + QStringLiteral(":50001"), kCobPressures) > 0;
        CHECK(reached == 4, "the bench's 0x220 reaches all four");

        // A typo is shown, kept, and the BIU keeps the last good list.
        hosts->setText(QStringLiteral("192.168.25.16, 192.168.25"));
        emit hosts->editingFinished();
        CHECK(w.biu()->replyHosts() == four && w.replyListText().contains(QLatin1String("not an IPv4 address")),
              "a typo: marked, and the four still answered");
        {
            QSettings s(Settings::iniPath(), QSettings::IniFormat);
            CHECK(s.value(QStringLiteral("brakebench/replyHost")).toString() == QLatin1String("192.168.25.16, 192.168.25")
                      && s.value(QStringLiteral("brakebench/replyFixed")).toBool(),
                  "and saved as typed, to be fixed");
        }
        w.biu()->receive(command361(), QStringLiteral("192.168.25.77"), 50001, t);
        hosts->setText(four.join(QStringLiteral(", ")));
        emit hosts->editingFinished();
        CHECK(w.replyListText().contains(QLatin1String("192.168.25.77, which is not listed")), "a commander off the list is pointed out");
        w.biu()->setRunning(false, t);

        // Reopened: the list and the mode come back.
        BrakeBenchWindow w2(nullptr);
        w2.setAttribute(Qt::WA_DeleteOnClose, false);
        CHECK(w2.biu()->replyHosts() == four, "reopened: the four again");

        // The Network tab still fits a 1366 px laptop.
        w.show();
        auto *tabs = w.findChild<QTabWidget *>();
        int widest = 0;
        if (tabs)
            for (int i = 0; i < tabs->count(); ++i)
                if (tabs->tabText(i) == QLatin1String("Network")) {
                    tabs->setCurrentIndex(i);
                    QCoreApplication::processEvents();
                    widest = tabs->widget(i)->minimumSizeHint().width();
                }
        CHECK(widest > 0 && widest <= 1366 - 40, qPrintable(QStringLiteral("the Network tab is %1 px wide").arg(widest)));
        const QStringList loose = LayoutAudit::orphans(&w);
        CHECK(loose.isEmpty(), qPrintable(QStringLiteral("no loose widget (%1)").arg(loose.join(QStringLiteral(", ")))));
        w.close();
    }
    clearSaved361();
}
