#include "testutil.h"

#include "benchrecord.h"
#include "benchrecordwindow.h"
#include "biuprotocol.h"
#include "biusim.h"
#include "brakebenchwindow.h"
#include "locosim.h"
#include "locosimwindow.h"
#include "rfidtag.h"
#include "settings.h"

#include <QFile>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>

#include <memory>

// =============================================================================
//  Session 404 — a recording of a whole test: every frame the Loco Simulator
//  and the Brake Test Bench send or receive, in one .benchrec file, started
//  and stopped with both simulators; and the viewer that reads it back. The
//  BIU command frames are session 360's (as the reviewed firmware sends
//  them: no BIU frame has been captured yet); the route is LocoTcasSimulator's
//  own Configuration1.xml.
// =============================================================================

using namespace Biu;

namespace {

QByteArray command404(quint8 a9, quint8 sa9)
{
    Command c;
    c.speedRaw = 1000;
    c.d1 = (1u << CmdHealth) | (1u << CmdSpeedValid);
    c.d2 = (1u << CmdA9Valid) | (1u << CmdSa9Valid);
    c.a9Raw = a9;
    c.sa9Raw = sa9;
    return toDatagram({ kCobCommand, encodeCommand(c) });
}

int count404(const BenchRecord::File &f, const QString &kind, int tx = -1)
{
    int n = 0;
    for (const BenchRecord::Item &i : f.items) n += i.kind == kind && (tx < 0 || int(i.tx) == tx) ? 1 : 0;
    return n;
}

void clear404()
{
    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    settings.remove(QStringLiteral("locosim"));
    settings.remove(QStringLiteral("brakebench"));
}

}  // namespace

TEST_SUITE(session404)
{
    QTemporaryDir dir;
    CHECK(dir.isValid(), "a temporary folder");

    // ---- the file ------------------------------------------------------------------------------
    {
        BenchRecord::Recorder recorder;
        const QString path = dir.filePath(QStringLiteral("sub/one.benchrec"));
        CHECK(recorder.start(path, 1000, QStringLiteral("a note")) && recorder.recording() && QFile::exists(path),
              "a recording starts, its folder made");
        BenchRecord::Item item;
        item.ms = 1500;
        item.kind = QStringLiteral("biu");
        item.tx = false;
        item.peer = QStringLiteral("192.168.25.126:50001");
        item.bytes = QByteArray::fromHex("90010B00");
        item.says = QStringLiteral("0x190 command, \"quoted\"");
        item.note = QStringLiteral("a note, with a comma");
        recorder.append(item);
        recorder.stop(2000);
        BenchRecord::File f;
        CHECK(BenchRecord::read(path, &f) && f.createdMs == 1000 && f.note == QLatin1String("a note") && !f.torn,
              "read back: its time and note");
        CHECK(f.items.size() == 3 && f.items.first().kind == QLatin1String("event") && f.items.last().kind == QLatin1String("event"),
              "started, the frame, stopped");
        const BenchRecord::Item &back = f.items.value(1);
        CHECK(back.ms == 1500 && back.kind == QLatin1String("biu") && !back.tx && back.peer == item.peer && back.bytes == item.bytes
                  && back.says == item.says && back.note == item.note,
              "every field of the frame as written");
        QFile append(path);
        append.open(QIODevice::Append);
        append.write("{\"t\":3000,\"k\":\"speed\",\"d\":\"tx\",\"b\":\"14 02");   // a crash mid-line
        append.close();
        CHECK(BenchRecord::read(path, &f) && f.torn && f.items.size() == 3, "a line cut short: the rest is read, the file marked torn");
        QFile other(dir.filePath(QStringLiteral("not.benchrec")));
        other.open(QIODevice::WriteOnly);
        other.write("hello\n");
        other.close();
        QString err;
        CHECK(!BenchRecord::read(other.fileName(), &f, &err) && !err.isEmpty(), "a file that is not a recording: refused, said why");
        CHECK(BenchRecord::defaultPath(0).endsWith(QLatin1String(".benchrec")) && BenchRecord::defaultPath(0).contains(QLatin1String("/bench/bench_")),
              "by default in the log folder's bench/");
    }

    // ---- both simulators, one recording ---------------------------------------------------------
    clear404();
    const QString path = dir.filePath(QStringLiteral("test.benchrec"));
    {
        qint64 t = 1782558147000LL;
        BenchRecord::Recorder recorder;
        auto bench = std::make_unique<BrakeBenchWindow>(nullptr);
        bench->setAttribute(Qt::WA_DeleteOnClose, false);
        bench->setClock([&t]() { return t; });
        bench->biu()->setSendSink([](const QByteArray &) {});
        bench->setAnalogSink([](const QByteArray &) {});
        bench->setListenPort(52977);
        LocoSimWindow loco(nullptr);
        loco.setAttribute(Qt::WA_DeleteOnClose, false);
        loco.setClock([&t]() { return t; });
        QVector<QByteArray> speedOut;
        loco.setSendSink([&speedOut](const QByteArray &m) { speedOut.append(m); });
        loco.setTagSink([](int, const QByteArray &) {});
        CHECK(!loco.toggleRecording(), "no recorder handed over: Record refused, said why");
        loco.setRecorder(&recorder);
        bench->setRecorder(&recorder);
        const QVector<RfidTag::Route> routes = RfidTag::readFile(QStringLiteral(":/tag_scenarios/LocoTcasSimulator/Configuration1.xml"));
        CHECK(!routes.isEmpty() && loco.loadRouteFile(QStringLiteral(":/tag_scenarios/LocoTcasSimulator/Configuration1.xml")),
              "the loco on the simulator's own route");
        loco.setRoute(0);
        loco.motion().setSpeed(72);

        CHECK(!loco.running() && !bench->biuRunning(), "both stopped before recording");
        CHECK(recorder.start(path, t) && loco.running() && bench->biuRunning(), "Record: both simulators start with it");
        auto *recordButton = loco.findChild<QPushButton *>(QStringLiteral("locoSimRecord"));
        CHECK(recordButton && recordButton->text().contains(QLatin1String("Stop")), "the Record button says Stop recording");

        const QString vcc = QStringLiteral("192.168.25.126");
        bench->biu()->receive(toDatagram({ kCobHeartbeatKavach, QByteArray(1, char(Operational)) }), vcc, 50001, t);
        for (int i = 0; i < 100; ++i) {
            if (i % 5 == 0) bench->biu()->receive(command404(70, 48), vcc, 50001, t);
            t += 50;
            bench->advance(0.05);
            if (i % 2 == 0) loco.tick();
        }
        bench->sendAnalogNow();
        recorder.stop(t);
        CHECK(!loco.running() && !bench->biuRunning(), "Stop recording: both simulators stop");

        BenchRecord::File f;
        CHECK(BenchRecord::read(path, &f), "the recording reads back");
        CHECK(count404(f, QStringLiteral("speed")) == speedOut.size() && speedOut.size() > 0,
              qPrintable(QStringLiteral("every speed message sent (%1)").arg(speedOut.size())));
        bool sameBytes = false;
        for (const BenchRecord::Item &i : f.items)
            if (i.kind == QLatin1String("speed")) { sameBytes = i.bytes == speedOut.first(); break; }
        CHECK(sameBytes, "the speed message's bytes, as sent");
        CHECK(count404(f, QStringLiteral("tag")) > 0, "the tag frames the readers sent");
        CHECK(count404(f, QStringLiteral("biu"), 0) >= 20, "the VCC's BIU frames received (commands, its heartbeat)");
        CHECK(count404(f, QStringLiteral("biu"), 1) > 0, "and the BIU's own frames sent");
        bool rxPeer = false, commandSays = false, heartbeat = false;
        for (const BenchRecord::Item &i : f.items) {
            if (i.kind != QLatin1String("biu")) continue;
            rxPeer = rxPeer || (!i.tx && i.peer.contains(vcc));
            commandSays = commandSays || (!i.tx && i.says.startsWith(QLatin1String("0x190")));
            heartbeat = heartbeat || (i.tx && i.says.contains(QLatin1String("eartbeat")));
        }
        CHECK(rxPeer && commandSays, "a received command: from the VCC, said as 0x190");
        CHECK(heartbeat, "the BIU's heartbeats too, whatever the Frames tab shows");
        CHECK(count404(f, QStringLiteral("analog")) > 0, "the analog card's frames");
        CHECK(count404(f, QStringLiteral("event")) == 2, "started and stopped");
        bool ordered = true;
        for (int i = 1; i < f.items.size(); ++i) ordered = ordered && f.items.at(i).ms >= f.items.at(i - 1).ms;
        CHECK(ordered, "in time order, on one clock");

        // ---- the viewer ------------------------------------------------------------------------
        BenchRecordWindow viewer;
        viewer.setAttribute(Qt::WA_DeleteOnClose, false);
        CHECK(viewer.openFile(path) && viewer.table()->rowCount() == f.items.size() && viewer.shownRows() == f.items.size(),
              "the viewer: one row per frame");
        viewer.setKindShown(QStringLiteral("biu"), false);
        CHECK(viewer.shownRows() == f.items.size() - count404(f, QStringLiteral("biu")), "biu off: its rows hidden");
        viewer.setKindShown(QStringLiteral("biu"), true);
        viewer.setFind(QStringLiteral("reader 1"));
        CHECK(viewer.shownRows() > 0 && viewer.shownRows() < f.items.size(), "find: the rows with every word");
        viewer.setFind(QString());
        CHECK(viewer.goTo(2.0) > 0 && f.items.at(viewer.goTo(2.0)).ms >= f.createdMs + 2000, "Go to 2 s: the first row at or after it");
        const QString csv = viewer.toCsv(false);
        CHECK(csv.startsWith(QLatin1String("Time,+ s,Kind,Dir,Peer,Says,Note,Bytes")) && csv.count(QLatin1String("\r\n")) == f.items.size() + 1,
              "CSV: the header, then every row");
        CHECK(viewer.exportCsv(dir.filePath(QStringLiteral("out.csv")), true) && QFile::exists(dir.filePath(QStringLiteral("out.csv"))),
              "exported");
        bench.reset();
    }
    clear404();
}
