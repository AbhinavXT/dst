#include "testutil.h"

#include "biuprotocol.h"
#include "biusim.h"
#include "brakebenchwindow.h"
#include "locosim.h"
#include "locosimwindow.h"
#include "settings.h"

#include <QLabel>
#include <QSettings>

#include <cmath>

// =============================================================================
//  Session 399 — the normal brake (NB) in the Loco Simulator: its own rate,
//  and the whole way through: the VCC's NB command (A9 4.4, SA9 1.2) into the
//  Brake Test Bench's BIU, the air to BP 4.4 / BC 1.2, the loco slowing at the
//  NB rate. No BIU frame has been captured yet: the command frames are
//  session 360's, as the reviewed firmware sends them.
// =============================================================================

using namespace Biu;

namespace {

bool near399(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }

QByteArray command399(quint8 a9, quint8 sa9)
{
    Command c;
    c.speedRaw = 1000;
    c.d1 = (1u << CmdHealth) | (1u << CmdSpeedValid);
    c.d2 = (1u << CmdA9Valid) | (1u << CmdSa9Valid);
    c.a9Raw = a9;
    c.sa9Raw = sa9;
    return toDatagram({ kCobCommand, encodeCommand(c) });
}

void clearSaved399()
{
    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    settings.remove(QStringLiteral("locosim"));
    settings.remove(QStringLiteral("brakebench"));
}

}  // namespace

TEST_SUITE(session399)
{
    // ---- the rule with an NB rate of its own ------------------------------------------
    {
        const LocoSim::BrakeRates defaults;
        CHECK(defaults.normalMps2 == 0.3 && defaults.nbBcKgcm2 == 1.2,
              "by default NB is 0.3 m/s² at BC 1.2: what session 398 gave (half full service)");
        LocoSim::BrakeRates rates;
        rates.normalMps2 = 0.4;
        CHECK(near399(LocoSim::brakeDeceleration(rates, 4.4, 1.2), 0.4), "NB rate 0.4: NB (BP 4.4, BC 1.2) gives 0.4");
        CHECK(near399(LocoSim::brakeDeceleration(rates, 3.5, 2.4), 0.6), "FSB stays 0.6");
        CHECK(near399(LocoSim::brakeDeceleration(rates, 0.0, 2.4), 0.9), "EB stays 0.9");
        CHECK(near399(LocoSim::brakeDeceleration(rates, 4.4, 0.6), 0.2), "half NB's BC: half the NB rate");
        CHECK(near399(LocoSim::brakeDeceleration(rates, 4.0, 1.8), 0.5), "BC half way from NB to full service: half way, 0.5");
        CHECK(near399(LocoSim::brakeDeceleration(rates, 1.75, 1.2), 0.5),
              "NB's BC with BP half way under FSB: raised by half of emergency / full service, 0.5");
        CHECK(LocoSim::brakeDeceleration(rates, 5.0, 0.1) == 0, "BC 0.1: still no brake");
        rates.fullBcKgcm2 = 1.0;   // set under NB's BC: the NB point cannot sit inside
        CHECK(near399(LocoSim::brakeDeceleration(rates, 4.4, 0.5), 0.3) && near399(LocoSim::brakeDeceleration(rates, 4.4, 1.2), 0.6),
              "BC at full service set under 1.2: straight to full service, the NB point left out");
    }

    // ---- NB from the VCC, through the bench, to the loco -------------------------------
    clearSaved399();
    {
        qint64 t = 1782558147000LL;
        BrakeBenchWindow bench(nullptr);
        bench.setAttribute(Qt::WA_DeleteOnClose, false);
        bench.setClock([&t]() { return t; });
        bench.biu()->setSendSink([](const QByteArray &) {});
        bench.setAnalogSink([](const QByteArray &) {});
        for (int ch = 0; ch < BrakePlant::kChannels; ++ch) bench.setRates(ch, 1.0, 1.0);

        LocoSimWindow window(nullptr);
        window.setAttribute(Qt::WA_DeleteOnClose, false);
        window.setBrakeSource(LocoSim::BrakeSource::Air);   // session 418: the air way (the default is the command)
        window.setClock([&t]() { return t; });
        window.setSendSink([](const QByteArray &) {});
        window.followBench(&bench);
        auto *state = window.findChild<QLabel *>(QStringLiteral("locoSimBrakeState"));

        bench.biu()->setRunning(true, t);
        const QString vcc = QStringLiteral("192.168.25.126");
        bench.biu()->receive(toDatagram({ kCobHeartbeatKavach, QByteArray(1, char(Operational)) }), vcc, 50001, t);
        window.motion().setSpeed(80);
        CHECK(window.start(), "starts");
        const auto run = [&](int ticks, quint8 a9, quint8 sa9) {
            for (int i = 0; i < ticks; ++i) {
                if (i % 5 == 0) bench.biu()->receive(command399(a9, sa9), vcc, 50001, t);
                t += 50;
                bench.advance(0.05);
                window.tick();
            }
        };
        // NB: A9 88 (4.4), SA9 24 (1.2), every 250 ms.
        run(40, 88, 24);
        CHECK(near399(bench.pressure(BrakePlant::Bp), 4.4, 0.01) && near399(bench.pressure(BrakePlant::Bc), 1.2, 0.01),
              qPrintable(QStringLiteral("2 s into NB: BP 4.4, BC 1.2 (got %1, %2)")
                             .arg(bench.pressure(BrakePlant::Bp)).arg(bench.pressure(BrakePlant::Bc))));
        CHECK(window.motion().braking() && near399(window.motion().brake(), 0.3, 0.005),
              "the loco brakes at the NB rate, 0.3 m/s²");
        double before = window.motion().speed();
        run(20, 88, 24);
        CHECK(near399(window.motion().speed(), before - 0.3 * 3.6, 0.02), "1 s of NB: 1.08 km/h slower");
        CHECK(state && state->text().startsWith(QLatin1String("braking 0.30")), "the Drive tab says it: braking 0.30 m/s²");

        LocoSim::BrakeRates rates = window.brakeRates();
        rates.normalMps2 = 0.4;
        window.setBrakeRates(rates);
        CHECK(near399(window.motion().brake(), 0.4, 0.005) && state && state->text().startsWith(QLatin1String("braking 0.40")),
              "NB rate set to 0.4: the loco brakes at once at 0.4, and says so");
        before = window.motion().speed();
        run(20, 88, 24);
        CHECK(near399(window.motion().speed(), before - 0.4 * 3.6, 0.02), "1 s at 0.4: 1.44 km/h slower");

        // Release: A9 5.0, SA9 0. The loco keeps the speed it slowed to.
        run(60, 100, 0);
        CHECK(!window.motion().braking() && window.motion().drive() == LocoSim::Motion::Drive::Hold,
              "released: no brake, the drive at Hold");
        const double held = window.motion().speed();
        run(20, 100, 0);
        CHECK(held < 80 && window.motion().speed() == held, "and holds the speed NB left it at");
        window.stop();
    }
    {
        LocoSimWindow again(nullptr);
        again.setAttribute(Qt::WA_DeleteOnClose, false);
        CHECK(again.brakeRates().normalMps2 == 0.4, "the NB rate comes back the next time the window opens");
        again.show();
        for (int round = 0; round < 3; ++round) QCoreApplication::processEvents();
        CHECK(again.minimumSizeHint().width() <= 1366 && again.minimumSizeHint().height() <= 700,
              QByteArray("still fits a laptop (") + QByteArray::number(again.minimumSizeHint().width()) + "x"
                  + QByteArray::number(again.minimumSizeHint().height()) + ")");
        again.close();
    }
    clearSaved399();
}
