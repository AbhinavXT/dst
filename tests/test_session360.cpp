#include "testutil.h"
#include "layoutaudit.h"

#include "biuprotocol.h"
#include "biusim.h"
#include "brakebenchwindow.h"
#include "brakeplant.h"
#include "cabio.h"
#include "pressuregauge.h"
#include "settings.h"
#include "simwidgets.h"
#include "theme.h"
#include "uicolors.h"
#include "uistyle.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QNetworkDatagram>
#include <QScrollArea>
#include <QSettings>
#include <QTabWidget>
#include <QUdpSocket>

#include <cmath>

// =============================================================================
//  Session 360 — the Brake Test Bench: a BIU (RDSO MP.0.01.00.31 Rev.05
//  Annexure-1) over the firmware's UDP link (on the loco: CAN, session 376),
//  the air it moves, and the analog card that reads it.
//
//  Abhinav: "make a miniature biu simulator in dlconsole window to fully test
//  the brake pressure logic in firmware ... simulate all the packets that biu
//  sends make everything configurable ... heartbeat also", the analog card
//  and the brake pressures "separated into a fully new window".
//
//  No BIU frame has been captured yet: the bytes are worked from the spec's
//  tables, and the command frames are the ones the reviewed firmware
//  (brake_pressure_interface_user.c, its host test) sends, byte for byte.
// =============================================================================

using namespace Biu;

namespace {

bool near360(double a, double b, double tol = 0.011) { return std::fabs(a - b) <= tol; }

// A command datagram as the firmware sends it: [90 01][0B][8 bytes].
QByteArray commandDatagram(quint16 speedCms, quint8 d1, quint8 d2, quint8 a9, quint8 sa9)
{
    Command c;
    c.speedRaw = speedCms;
    c.d1 = d1;
    c.d2 = d2;
    c.a9Raw = a9;
    c.sa9Raw = sa9;
    return toDatagram({ kCobCommand, encodeCommand(c) });
}

QByteArray heartbeat(quint8 state) { return toDatagram({ kCobHeartbeatKavach, QByteArray(1, char(state)) }); }

// The frames a simulator sent, decoded.
struct Sent {
    QVector<Frame> frames;
    void add(const QByteArray &d) { Frame f; if (fromDatagram(d, &f)) frames << f; }
    QVector<Frame> of(quint16 cob) const { QVector<Frame> r; for (const Frame &f : frames) if (f.cob == cob) r << f; return r; }
    void clear() { frames.clear(); }
};

constexpr quint8 kHealthSpeed = (1u << CmdHealth) | (1u << CmdSpeedValid);   // 0x12
constexpr quint8 kBothValid = (1u << CmdA9Valid) | (1u << CmdSa9Valid);       // 0x0C

void clearSaved()
{
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    s.remove(QStringLiteral("brakebench"));
}

}  // namespace

TEST_SUITE(session360)
{
    // ---- the protocol --------------------------------------------------------------------------
    {
        // The firmware's NB on WAP7 formation at 10 m/s, from its host test.
        const QByteArray nb = QByteArray::fromHex("90010BE803120C58180000");
        Frame f;
        QString why;
        CHECK(fromDatagram(nb, &f, &why) && f.cob == 0x190 && f.data.size() == 8, "the firmware's NB datagram: COB-ID 0x190, 8 bytes");
        Command c;
        CHECK(decodeCommand(f.data, &c), "decodes as a command");
        CHECK(c.speedRaw == 1000 && near360(c.speedMs(), 10.0), "speed 0x03E8 = 10.00 m/s");
        CHECK(near360(c.a9(), 4.4) && near360(c.sa9(), 1.2) && c.a9Valid() && c.sa9Valid(), "A9 88 = 4.4, SA9 24 = 1.2, both valid");
        CHECK(c.bit1(CmdHealth) && c.bit1(CmdSpeedValid) && !c.bit1(CmdDynamicBraking), "healthy, speed valid, no dynamic braking");
        CHECK(toDatagram(f) == nb && encodeCommand(c) == f.data, "and back to the same bytes");

        CHECK(!fromDatagram(QByteArray::fromHex("9001"), &f, &why) && why.contains(QLatin1String("3-byte")), "2 bytes: no header");
        CHECK(!fromDatagram(QByteArray::fromHex("90010CE803120C5818000000"), &f, &why), "length byte 12: more than 3 + 8");
        CHECK(!fromDatagram(QByteArray::fromHex("90010BE803"), &f, &why) && why.contains(QLatin1String("11")), "length 11, 5 bytes came");
        CHECK(toDatagram({ kCobHeartbeatBiu, QByteArray(1, char(Operational)) }) == QByteArray::fromHex("100704" "05"),
              "the BIU's heartbeat: 10 07 04 05");
        CHECK(toDatagram({ kCobNmt, QByteArray::fromHex("0120") }) == QByteArray::fromHex("000005" "0120"),
              "NMT start KAVACH: 00 00 05 01 20");

        CHECK(kgToByte(4.4) == 88 && kgToByte(2.16) == 43 && kgToByte(5.0) == 100 && kgToByte(-1) == 0 && kgToByte(13) == 255,
              "0.05 kg/cm2 per bit, rounded, clamped");

        Status s;
        s.raw[Bp] = 100; s.raw[Bc] = 24; s.raw[Mr] = 180;
        s.valid[Bp] = s.valid[Mr] = true;
        const QByteArray p = encodePressures(s);
        CHECK(p.size() == 8 && quint8(p.at(0)) == 100 && quint8(p.at(7)) == 0x05, "pressures: bytes 0-6, validity bit n for byte n");
        s.isolationCounter = 3; s.faultCode = 0x1040; s.displayCode = 0x2005;
        CHECK(encodeCodes(s) == QByteArray::fromHex("0300401005200000"), "codes lower byte first: 03 00 40 10 05 20 00 00");
        s.d1 = 0x01; s.d2 = 0x04; s.d3 = 0xF0;
        CHECK(encodeDiscretes(s) == QByteArray::fromHex("0104F00000000000"), "discretes: bytes 0-2, the rest spare");
        Status back;
        CHECK(decodePressures(p, &back) && back.raw[Mr] == 180 && back.valid[Mr] && !back.valid[Bc], "pressures read back");
        CHECK(decodeCodes(encodeCodes(s), &back) && back.faultCode == 0x1040, "codes read back");

        CHECK(codeText(0x1040) == QLatin1String("BIU ISOLATED") && codeText(0x100A) == QLatin1String("Spare")
                  && codeText(0x2005).contains(QLatin1String("isolate switch")), "the spec's code lists");
        CHECK(faultCodes().size() == 14 && displayCodes().size() == 5, "14 fault codes, 5 display codes (Annexure-1 5, 6)");
        CHECK(validLimit(Bp) == 6.0 && validLimit(Bc) == 4.0 && validLimit(Mr) == 10.5, "validity limits from the validity byte");
        CHECK(describe(f).contains(QLatin1String("A9 4.40")) && describe(f).contains(QLatin1String("Speed valid")),
              "a command described for the log");
    }

    // ---- the air --------------------------------------------------------------------------------
    {
        BrakePlant p;
        CHECK(p.value(BrakePlant::Bp) == 5.0 && p.value(BrakePlant::Bc) == 0 && p.value(BrakePlant::Mr) == 9.0, "starts released, MR 9");
        p.setRates(BrakePlant::Bp, 2.0, 0.5);
        p.setTarget(BrakePlant::Bp, 3.0);
        CHECK(p.applying(BrakePlant::Bp), "BP falling is applying");
        p.step(0.5);
        CHECK(near360(p.value(BrakePlant::Bp), 4.0), "BP falls at its apply rate (2/s)");
        p.step(5);
        CHECK(p.value(BrakePlant::Bp) == 3.0 && p.settled(BrakePlant::Bp), "and stops at the target");
        p.setTarget(BrakePlant::Bp, 5.0);
        p.step(1);
        CHECK(near360(p.value(BrakePlant::Bp), 3.5), "rises back at its release rate (0.5/s)");
        p.setFault(BrakePlant::Bp, BrakePlant::Fault::Stuck);
        p.step(10);
        CHECK(near360(p.value(BrakePlant::Bp), 3.5), "stuck: does not move");
        p.setFault(BrakePlant::Bp, BrakePlant::Fault::Slow, 5);
        p.step(1);
        CHECK(near360(p.value(BrakePlant::Bp), 3.6), "slow x5: a fifth of the rate");
        p.setFault(BrakePlant::Mr, BrakePlant::Fault::Leak, 0.5);
        p.step(2);
        CHECK(near360(p.value(BrakePlant::Mr), 8.0), "MR leaking 0.5/s, whatever its target");
    }

    // ---- the BIU, on injected time ----------------------------------------------------------------
    {
        BiuSimulator sim;
        Sent sent;
        sim.setSendSink([&](const QByteArray &d) { sent.add(d); });
        qint64 t = 1000;
        const QString vcc = QStringLiteral("192.168.25.126");

        sim.setRunning(true, t);
        CHECK(sent.frames.size() == 1 && sent.frames[0].cob == 0x710 && sent.frames[0].data == QByteArray(1, '\0'),
              "starting: the boot-up message first (0x710 [00])");
        sim.tick(t);
        CHECK(sent.of(kCobPressures).size() == 1 && sent.of(kCobCodes).size() == 1 && sent.of(kCobDiscretes).size() == 1,
              "then the three status frames at once");
        sent.clear();
        for (int i = 0; i < 20; ++i) { t += 50; sim.tick(t); }      // 1 s
        CHECK(sent.of(kCobHeartbeatBiu).size() == 2 && sent.of(kCobPressures).size() == 4,
              qPrintable(QStringLiteral("in 1 s: 2 heartbeats (500 ms), 4 of each status frame (250 ms); got %1, %2")
                             .arg(sent.of(kCobHeartbeatBiu).size()).arg(sent.of(kCobPressures).size())));
        CHECK(!sent.of(kCobHeartbeatBiu).isEmpty() && quint8(sent.of(kCobHeartbeatBiu).last().data.at(0)) == Operational,
              "the heartbeat says operational");

        // KAVACH boots: the BIU starts it, and keeps doing so until it is operational.
        sent.clear();
        sim.receive(heartbeat(PreOperational), vcc, 50001, t);
        for (int i = 0; i < 10; ++i) { t += 50; sim.tick(t); }
        const QVector<Frame> nmt = sent.of(kCobNmt);
        CHECK(nmt.size() == 1 && nmt[0].data == QByteArray::fromHex("0120"), "KAVACH pre-operational: NMT start 0x20");
        CHECK(sim.replyTarget() == QLatin1String("192.168.25.126:50001"), "answers the sender, on 50001");
        sim.receive(heartbeat(Operational), vcc, 50001, t);
        sent.clear();
        for (int i = 0; i < 20; ++i) { t += 50; sim.tick(t); }
        CHECK(sent.of(kCobNmt).isEmpty() && sim.kavachState() == Operational, "operational: no more NMT");

        // FSB from KAVACH: A9 3.5, SA9 2.4.
        CHECK(!sim.targets(t).controlling && sim.targets(t).why.contains(QLatin1String("No command")), "no command: not braking");
        sim.receive(commandDatagram(1000, kHealthSpeed, kBothValid, 70, 48), vcc, 50001, t);
        BiuSimulator::Targets tg = sim.targets(t);
        CHECK(tg.controlling && tg.bpSet && tg.bcSet && near360(tg.bp, 3.5) && near360(tg.bc, 2.4), "FSB: BP to 3.5, BC to 2.4");
        Status st = sim.buildStatus(t);
        CHECK(st.raw[A9Ref] == 70 && st.raw[Sa9Ref] == 48 && st.valid[A9Ref], "the references say what the BIU works to");
        CHECK((st.d1 & (1u << StPilotOverrideByBiu)) && (st.d1 & (1u << StPowerCutoffRelay)) && (st.d1 & (1u << StHealthy)),
              "D1: healthy, overriding the pilot, power cut off");
        CHECK((st.d3 & (1u << StBpSelectValveOn)) && (st.d3 & (1u << StBcSelectValveOn)) && !(st.d3 & (1u << StEmergencyValveOn))
                  && (st.d3 & 0xF0) == 0xF0, "D3: BP and BC valves on, emergency valve not, all healthy");
        sim.setLivePressure(Bp, 6.5);
        CHECK(!sim.buildStatus(t).valid[Bp], "BP 6.5 reported: not valid (above 6.0)");
        sim.setValidMode(Bp, BiuSimulator::ValidMode::Valid);
        CHECK(sim.buildStatus(t).valid[Bp], "forced valid");
        sim.setValidMode(Bp, BiuSimulator::ValidMode::Auto);
        sim.setLivePressure(Bp, 3.5);
        sim.setPressureManual(Mr, true, 7.0);
        CHECK(sim.buildStatus(t).raw[Mr] == 140, "MR by hand: 7.0");
        sim.setPressureManual(Mr, false, 7.0);

        // A9 not valid: the BIU leaves BP to the pilot.
        sim.receive(commandDatagram(1000, kHealthSpeed, 1u << CmdSa9Valid, 70, 48), vcc, 50001, t);
        tg = sim.targets(t);
        CHECK(!tg.bpSet && tg.bcSet, "A9 not valid: BP not commanded, BC still");

        // EB: BP to 0, the emergency valve.
        sim.receive(commandDatagram(1000, kHealthSpeed, kBothValid, 0, 48), vcc, 50001, t);
        CHECK(sim.buildStatus(t).d3 & (1u << StEmergencyValveOn), "EB (A9 0): emergency valve on");

        // Forced bits.
        sim.setBitMode(1, StHealthy, BiuSimulator::BitMode::Off);
        sim.setBitMode(3, StBcSelectValveOk, BiuSimulator::BitMode::Off);
        st = sim.buildStatus(t);
        CHECK(!(st.d1 & 1) && !(st.d3 & (1u << StBcSelectValveOk)), "BIU unhealthy and the BC valve faulty, by hand");
        sim.setBitMode(1, StHealthy, BiuSimulator::BitMode::Auto);
        sim.setBitMode(3, StBcSelectValveOk, BiuSimulator::BitMode::Auto);

        // Isolation: counted, 0x1040 raised, held until KAVACH acks it.
        sim.setBitMode(1, StManualIsolate, BiuSimulator::BitMode::On);
        t += 50; sim.tick(t);
        CHECK(sim.isolated() && !sim.targets(t).controlling && sim.isolationCounter() == 1, "isolated: not braking, counted once");
        CHECK(sim.faultQueue() == QVector<quint16>{ 0x1040 }, "0x1040 BIU ISOLATED raised");
        for (int i = 0; i < 6; ++i) { t += 50; sim.tick(t); }
        CHECK(sim.lastStatus().faultCode == 0x1040, "and sent");
        sim.raiseFault(0x1004);
        t += 130;
        sim.receive(commandDatagram(1000, kHealthSpeed, kBothValid | (1u << CmdFaultAck), 0, 48), vcc, 50001, t);
        CHECK(sim.faultQueue() == QVector<quint16>{ 0x1004 } && sim.lastFaultAckMs() > 0, "acked: the next code is up");
        sim.receive(commandDatagram(1000, kHealthSpeed, kBothValid | (1u << CmdFaultAck), 0, 48), vcc, 50001, t);
        CHECK(sim.faultQueue() == QVector<quint16>{ 0x1004 }, "an ack before 0x1004 has gone out does not clear it");
        sim.setBitMode(1, StManualIsolate, BiuSimulator::BitMode::Auto);
        sim.clearFaults();
        sim.setWaitForAck(false);
        sim.raiseDisplay(0x2001);
        for (int i = 0; i < 6; ++i) { t += 50; sim.tick(t); }
        sim.receive(commandDatagram(1000, kHealthSpeed, kBothValid | (1u << CmdDisplayAck), 0, 48), vcc, 50001, t);
        CHECK(sim.displayQueue() == QVector<quint16>{ 0x2001 }, "not waiting for acks: the code stays");
        sim.setWaitForAck(true);
        sim.receive(commandDatagram(1000, kHealthSpeed, kBothValid | (1u << CmdDisplayAck), 0, 48), vcc, 50001, t);
        CHECK(sim.displayQueue().isEmpty() && sim.lastDisplayAckMs() >= 0, "waiting again: the display ack clears it");

        // The brake panel test.
        sim.setPanelTestSeconds(2);
        sim.setPanelTestPasses(false);
        sim.receive(commandDatagram(1000, kHealthSpeed | 1u, kBothValid, 0, 48), vcc, 50001, t);
        CHECK(sim.buildStatus(t).d2 == (1u << StPanelTestProgress), "test asked: in progress");
        t += 2100;
        sim.receive(commandDatagram(1000, kHealthSpeed | 1u, kBothValid, 0, 48), vcc, 50001, t);
        CHECK(sim.buildStatus(t).d2 == (1u << StPanelTestFail), "after 2 s: failed, as set");
        sim.receive(commandDatagram(1000, kHealthSpeed, kBothValid, 0, 48), vcc, 50001, t);
        CHECK(sim.buildStatus(t).d2 == 0, "request dropped: the test bits clear");

        // KAVACH goes quiet: emergency, after 1.5 s.
        sim.receive(commandDatagram(1000, kHealthSpeed, kBothValid, 88, 24), vcc, 50001, t);
        sim.receive(heartbeat(Operational), vcc, 50001, t);
        t += 1400;
        CHECK(!sim.kavachLost(t) && near360(sim.targets(t).bp, 4.4), "1.4 s quiet: still NB");
        t += 200;
        tg = sim.targets(t);
        CHECK(sim.kavachLost(t) && tg.controlling && tg.bpSet && tg.bp == 0, "1.6 s quiet: KAVACH lost, emergency (Annexure-1 4 c)");
        sim.setOnLoss(BiuSimulator::OnLoss::Hold);
        CHECK(near360(sim.targets(t).bp, 4.4), "hold: the last command");
        sim.setOnLoss(BiuSimulator::OnLoss::Release);
        CHECK(!sim.targets(t).controlling, "release: the pilot's brakes");
        sim.setOnLoss(BiuSimulator::OnLoss::ApplyEb);

        // What is wrong with what KAVACH sends is said.
        QStringList notes;
        QObject::connect(&sim, &BiuSimulator::logged, [&](const BiuSimulator::LogItem &i) { if (!i.tx) notes << i.note; });
        sim.receive(toDatagram({ 0x181, encodeCommand(Command()) }), vcc, 50001, t);
        CHECK(!notes.isEmpty() && notes.last().contains(QLatin1String("0x190")), "COB-ID 0x181: says the BIU takes 0x190");
        sim.receive(heartbeat(PreOperational), vcc, 50001, t);
        sim.receive(commandDatagram(1000, kHealthSpeed, kBothValid, 88, 24), vcc, 50001, t);
        CHECK(notes.last().contains(QLatin1String("pre-operational"), Qt::CaseInsensitive),
              "a command while KAVACH says pre-operational is noted");
        const int before = sim.commandCount();
        sim.receive(toDatagram({ kCobCommand, QByteArray::fromHex("E803120C") }), vcc, 50001, t);
        CHECK(sim.commandCount() == before && notes.last().contains(QLatin1String("not taken")), "a 4-byte command is not taken");
        sim.receive(commandDatagram(1000, kHealthSpeed, kBothValid, 140, 100), vcc, 50001, t);
        CHECK(notes.last().contains(QLatin1String("above 6.0")) && notes.last().contains(QLatin1String("above 4.0")),
              "A9 7.0 / SA9 5.0 marked valid: both noted");

        // Silence: nothing at all.
        sim.setSilent(true);
        sent.clear();
        for (int i = 0; i < 20; ++i) { t += 50; sim.tick(t); }
        CHECK(sent.frames.isEmpty(), "silenced: no frame, no heartbeat");
        sim.setSilent(false);
        sim.setFrameOn(1, false);
        sent.clear();
        for (int i = 0; i < 10; ++i) { t += 50; sim.tick(t); }
        CHECK(sent.of(kCobCodes).isEmpty() && !sent.of(kCobPressures).isEmpty(), "the codes frame off: the others go on");
    }

    // ---- over a real socket ---------------------------------------------------------------------
    {
        BiuSimulator sim;
        QString why;
        CHECK(sim.listen(0, &why) && sim.listenPort() > 0, qPrintable(QStringLiteral("the BIU binds a UDP port (%1)").arg(why)));
        QUdpSocket vcc;
        CHECK(vcc.bind(QHostAddress(QHostAddress::LocalHost), 0), "fixture: a VCC socket on localhost");
        sim.setReplyPort(vcc.localPort());
        qint64 t = QDateTime::currentMSecsSinceEpoch();
        sim.setRunning(true, t);
        vcc.writeDatagram(commandDatagram(500, kHealthSpeed, kBothValid, 70, 48), QHostAddress(QHostAddress::LocalHost), sim.listenPort());
        QVector<Frame> got;
        QElapsedTimer el;
        el.start();
        while (el.elapsed() < 2000 && (got.size() < 3 || !sim.hasCommand())) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            sim.tick(QDateTime::currentMSecsSinceEpoch());
            vcc.waitForReadyRead(10);
            while (vcc.hasPendingDatagrams()) {
                Frame f;
                if (fromDatagram(vcc.receiveDatagram().data(), &f) && f.cob >= kCobPressures) got << f;
            }
        }
        CHECK(sim.hasCommand() && near360(sim.lastCommand().a9(), 3.5), "the command arrived over UDP");
        CHECK(sim.lastPeer().startsWith(QLatin1String("127.0.0.1:")), "from the VCC socket's address");
        CHECK(got.size() >= 3, qPrintable(QStringLiteral("the status frames came back to it (%1)").arg(got.size())));
        sim.close();
    }

    // ---- the window ------------------------------------------------------------------------------
    clearSaved();
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    {
        BrakeBenchWindow w(nullptr);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        qint64 t = 1000;
        w.setClock([&t]() { return t; });
        Sent sent;
        w.biu()->setSendSink([&](const QByteArray &d) { sent.add(d); });
        QVector<QByteArray> analog;
        w.setAnalogSink([&](const QByteArray &b) { analog << b; });
        CHECK(w.listenPort() == 51235 && w.biu()->replyPort() == 50001 && w.source() == BrakeBenchWindow::Source::Biu,
              "opens on UDP 51235, answering port 50001, following the BIU");
        CHECK(w.analogFollows() && !w.biuRunning(), "the analog card follows the bench; the BIU waits for Start");
        for (int ch = 0; ch < 3; ++ch) { w.setRates(ch, 1.0, 1.0); w.setAnalogScale(ch, 10); }

        w.biu()->setRunning(true, t);
        const QString vcc = QStringLiteral("192.168.25.126");
        w.biu()->receive(heartbeat(Operational), vcc, 50001, t);
        w.biu()->receive(commandDatagram(1000, kHealthSpeed, kBothValid, 0, 70), vcc, 50001, t);   // EB, BC 3.5
        for (int i = 0; i < 20; ++i) {
            t += 50;
            w.advance(0.05);
            if (i % 5 == 0) w.biu()->receive(commandDatagram(1000, kHealthSpeed, kBothValid, 0, 70), vcc, 50001, t);
        }
        CHECK(near360(w.pressure(BrakePlant::Bp), 4.0, 0.06) && near360(w.pressure(BrakePlant::Bc), 1.0, 0.06),
              "1 s after EB: BP 4.0, BC 1.0 at 1/s");
        const CabIo::PressureReading a0 = CabIo::currentToPressure(w.analogMa(0), w.analogScale(0));
        CHECK(near360(a0.kgcm2, w.pressure(BrakePlant::Bp), 0.02), "the analog card's BP reads the bench");
        const QVector<Frame> pf = sent.of(kCobPressures);
        Status st;
        CHECK(!pf.isEmpty() && decodePressures(pf.last().data, &st) && std::fabs(byteToKg(st.raw[Bp]) - w.pressure(BrakePlant::Bp)) < 0.11,
              "and the BIU reports it in 0x220");
        w.setAnalogOffset(BrakePlant::Bp, 0.5);
        CHECK(near360(CabIo::currentToPressure(w.analogMa(0), 10).kgcm2, w.pressure(BrakePlant::Bp) + 0.5, 0.02),
              "an analog offset: the card reads 0.5 more than the BIU");
        w.setAnalogOffset(BrakePlant::Bp, 0);
        w.setPlantFault(BrakePlant::Bc, BrakePlant::Fault::Stuck, 0);
        const double bc = w.pressure(BrakePlant::Bc);
        for (int i = 0; i < 20; ++i) { t += 50; w.advance(0.05); }
        CHECK(w.pressure(BrakePlant::Bc) == bc, "BC stuck on the Bench tab: it stays");
        w.setPlantFault(BrakePlant::Bc, BrakePlant::Fault::None, 0);
        for (int i = 0; i < 200; ++i) {
            t += 50;
            w.advance(0.05);
            if (i % 5 == 0) w.biu()->receive(commandDatagram(1000, kHealthSpeed, kBothValid, 0, 70), vcc, 50001, t);
        }
        CHECK(near360(w.pressure(BrakePlant::Bp), 0) && near360(w.pressure(BrakePlant::Bc), 3.5), "EB settles: BP 0, BC 3.5");

        // KAVACH goes quiet while released: the BIU brakes.
        w.biu()->receive(commandDatagram(1000, kHealthSpeed, kBothValid, 100, 0), vcc, 50001, t);
        for (int i = 0; i < 200; ++i) { t += 50; w.advance(0.05); if (i < 20 && i % 5 == 0) w.biu()->receive(heartbeat(Operational), vcc, 50001, t); }
        CHECK(w.biu()->kavachLost(t) && near360(w.pressure(BrakePlant::Bp), 0), "KAVACH lost: BP to 0");

        w.sendAnalogNow();
        CHECK(analog.size() == 1 && analog[0].size() == 34, "the analog card still sends its 34-byte frame");

        // Layout: every tab fits a 1366 x 768 laptop.
        w.show();
        auto *tabs = w.findChild<QTabWidget *>();
        CHECK(tabs && tabs->count() == 6, "six tabs");
        int widest = 0;
        QString widestTab;
        if (tabs)
            for (int i = 0; i < tabs->count(); ++i) {
                tabs->setCurrentIndex(i);
                QCoreApplication::processEvents();
                auto *sa = qobject_cast<QScrollArea *>(tabs->widget(i));
                const int wpx = sa && sa->widget() ? sa->widget()->minimumSizeHint().width() : tabs->widget(i)->minimumSizeHint().width();
                if (wpx > widest) { widest = wpx; widestTab = tabs->tabText(i); }
            }
        CHECK(widest <= 1366 - 40, qPrintable(QStringLiteral("the widest tab (%1) is %2 px").arg(widestTab).arg(widest)));
        CHECK(w.minimumSizeHint().height() <= 700, qPrintable(QStringLiteral("the window fits 768 px tall (%1)").arg(w.minimumSizeHint().height())));
        const QStringList loose = LayoutAudit::orphans(&w);
        CHECK(loose.isEmpty(), qPrintable(QStringLiteral("no visible widget outside every layout (%1)").arg(loose.join(QStringLiteral(", ")))));
        w.flushLog();
        CHECK(w.logRows() > 0, "the Frames tab logs the traffic");
        w.close();
    }

    // ---- every theme: what the bench paints itself ------------------------------------------------
    {
        const int savedTheme = UiColor::activeTheme();
        int themes = 0;
        QStringList shortIn;
        for (bool safe : { false, true }) {
            UiColor::setColorBlindSafe(safe);
            for (Theme th : ThemeUtil::all()) {
                ThemeUtil::apply(th);
                ++themes;
                const QString where = QString::fromLatin1(ThemeUtil::toString(th)) + (safe ? QStringLiteral(" (safe)") : QString());
                const QColor base = qApp->palette().color(QPalette::Base);
                // A lit chip: Base text on accent. A plain one: muted on Base.
                if (UiColor::contrastRatio(base, UiColor::accent()) + 1e-9 < CabIoColors::textFloor()) shortIn << where + QStringLiteral(" lit chip");
                if (UiColor::contrastRatio(UiColor::muted(), base) + 1e-9 < CabIoColors::textFloor()) shortIn << where + QStringLiteral(" plain chip");
            }
        }
        UiColor::setColorBlindSafe(false);
        ThemeUtil::apply(Theme(savedTheme));
        CHECK(themes == 2 * ThemeUtil::all().size(), "fixture: every theme (14 since session 434), colour-blind-safe off and on");
        CHECK(shortIn.isEmpty(), qPrintable(QStringLiteral("chips hold the text floor in every theme; short in: %1").arg(shortIn.join(QStringLiteral(", ")))));
    }
    clearSaved();
}
