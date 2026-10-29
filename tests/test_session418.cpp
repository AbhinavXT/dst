#include "testutil.h"

#include "biuprotocol.h"
#include "biusim.h"
#include "brakebenchwindow.h"
#include "locosim.h"
#include "locosimwindow.h"
#include "settings.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QSettings>

#include <cmath>
#include <memory>

// =============================================================================
//  Session 418 — the Loco Simulator brakes as LocoTcasSimulator does, from the
//  BIU's own command: A9 / SA9 read as NB, FSB, EB or LE, that brake's fixed
//  rate at once (or over a build-up time), until the VCC releases. The
//  simulator's extra deceleration field is not used: the real 8-byte command
//  is. No BIU frame has been captured yet: the command frames are session
//  360's, as the reviewed firmware sends them.
// =============================================================================

using namespace Biu;

namespace {

bool near418(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }

QByteArray command418(quint8 a9, quint8 sa9, bool valid = true)
{
    Command c;
    c.speedRaw = 1000;
    c.d1 = (1u << CmdHealth) | (1u << CmdSpeedValid);
    c.d2 = valid ? quint8((1u << CmdA9Valid) | (1u << CmdSa9Valid)) : quint8(0);
    c.a9Raw = a9;
    c.sa9Raw = sa9;
    return toDatagram({ kCobCommand, encodeCommand(c) });
}

void clearSaved418()
{
    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    settings.remove(QStringLiteral("locosim"));
    settings.remove(QStringLiteral("brakebench"));
}

}  // namespace

TEST_SUITE(session418)
{
    using LocoSim::BrakeType;
    clearSaved418();

    // ---- the brake a command asks for ------------------------------------------------
    {
        const LocoSim::BrakeRates rates;
        CHECK(LocoSim::commandedBrake(rates, 4.4, 1.2) == BrakeType::Normal, "A9 4.4, SA9 1.2: NB");
        CHECK(LocoSim::commandedBrake(rates, 3.5, 2.4) == BrakeType::FullService, "A9 3.5, SA9 2.4: FSB");
        CHECK(LocoSim::commandedBrake(rates, 0.0, 2.4) == BrakeType::Emergency, "A9 0, SA9 2.4: EB");
        CHECK(LocoSim::commandedBrake(rates, 0.0, 0.0) == BrakeType::Emergency, "A9 0 with SA9 0: still EB, BP decides");
        CHECK(LocoSim::commandedBrake(rates, 5.0, 3.5) == BrakeType::LightEngine, "A9 5.0, SA9 3.5: the light engine brake");
        CHECK(LocoSim::commandedBrake(rates, 5.0, 0.0) == BrakeType::Released, "A9 5.0, SA9 0: released");
        CHECK(LocoSim::commandedBrake(rates, 5.0, 0.1) == BrakeType::Released, "SA9 0.1, inside the released tolerance: released");
        CHECK(LocoSim::commandedBrake(rates, 1.70, 2.4) == BrakeType::Emergency
                  && LocoSim::commandedBrake(rates, 1.80, 2.4) == BrakeType::FullService,
              "EB / FSB split half way, at 1.75");
        CHECK(LocoSim::commandedBrake(rates, 3.90, 1.2) == BrakeType::FullService
                  && LocoSim::commandedBrake(rates, 4.00, 1.2) == BrakeType::Normal,
              "FSB / NB split half way, at 3.95");
        CHECK(LocoSim::commandedBrake(rates, 4.65, 1.2) == BrakeType::Normal
                  && LocoSim::commandedBrake(rates, 4.75, 0.0) == BrakeType::Released,
              "NB / released split half way, at 4.7");
        CHECK(LocoSim::brakeTypeRate(rates, BrakeType::Normal) == 0.3 && LocoSim::brakeTypeRate(rates, BrakeType::FullService) == 0.6
                  && LocoSim::brakeTypeRate(rates, BrakeType::Emergency) == 0.9
                  && LocoSim::brakeTypeRate(rates, BrakeType::LightEngine) == 0.6
                  && LocoSim::brakeTypeRate(rates, BrakeType::Released) == 0,
              "the rates: NB 0.3, FSB 0.6, EB 0.9, LE full service, released 0");
        CHECK(LocoSim::brakeTypeName(BrakeType::Emergency) == QLatin1String("EB")
                  && LocoSim::brakeTypeName(BrakeType::Released) == QLatin1String("released"),
              "named EB, ..., released");
    }

    // ---- the build-up ----------------------------------------------------------------
    {
        LocoSim::Motion motion;
        motion.setSpeed(80);
        motion.setBrakeBuildUp(2.0);
        motion.setBrake(0.6);
        CHECK(motion.braking() && motion.brakeEffort() == 0, "the brake on, not yet built up");
        motion.step(0.1);
        CHECK(near418(motion.brakeEffort(), 0.05) && near418(motion.brakeNow(), 0.03),
              "0.1 s of a 2 s build-up: 5 %, 0.03 m/s² (the effort at the step's end, as the simulator)");
        for (int i = 1; i < 20; ++i) motion.step(0.1);
        // sum over k = 1..20 of 0.6 * k/20 * 3.6 * 0.1 = 0.216 * 10.5
        CHECK(near418(motion.brakeEffort(), 1.0) && near418(motion.speed(), 80 - 0.216 * 10.5, 1e-9),
              "2 s on: full, and the loco 2.268 km/h slower");
        motion.setBrake(0.9);
        CHECK(near418(motion.brakeEffort(), 1.0), "FSB to EB while braking: the effort stays built up");
        motion.step(1.0);
        CHECK(near418(motion.speed(), 80 - 2.268 - 0.9 * 3.6, 1e-9), "then 1 s at 0.9");
        motion.setBrake(0);
        motion.setBrake(0.6);
        CHECK(motion.brakeEffort() == 0, "released and on again: it builds up from 0 again");
        motion.setBrakeBuildUp(0);
        CHECK(motion.brakeEffort() == 1.0, "no build-up: full at once");
    }

    // ---- the whole way: the VCC's command into the bench's BIU, the loco braking ------
    {
        qint64 t = 1782558147000LL;
        auto bench = std::make_unique<BrakeBenchWindow>(nullptr);
        bench->setAttribute(Qt::WA_DeleteOnClose, false);
        bench->setClock([&t]() { return t; });
        bench->biu()->setSendSink([](const QByteArray &) {});
        bench->setAnalogSink([](const QByteArray &) {});
        for (int ch = 0; ch < BrakePlant::kChannels; ++ch) bench->setRates(ch, 1.0, 1.0);

        LocoSimWindow window(nullptr);
        window.setAttribute(Qt::WA_DeleteOnClose, false);
        window.setClock([&t]() { return t; });
        window.setSendSink([](const QByteArray &) {});
        auto *state = window.findChild<QLabel *>(QStringLiteral("locoSimBrakeState"));
        auto *source = window.findChild<QComboBox *>(QStringLiteral("locoSimBrakeSource"));
        CHECK(source && window.brakeSource() == LocoSim::BrakeSource::Command && window.brakeFollows(),
              "by default the loco brakes by the BIU's command");
        CHECK(state && state->text().contains(QLatin1String("Brake Test Bench")), "no bench open: the Drive tab says to open it");

        window.followBench(bench.get());
        CHECK(state && state->text().startsWith(QLatin1String("released")) && !window.motion().braking(),
              qPrintable(QStringLiteral("the bench open, nothing commanded: released (%1)").arg(state ? state->text() : QString())));

        bench->biu()->setRunning(true, t);
        const QString vcc = QStringLiteral("192.168.25.126");
        bench->biu()->receive(toDatagram({ kCobHeartbeatKavach, QByteArray(1, char(Operational)) }), vcc, 50001, t);
        window.motion().setSpeed(90);
        window.motion().setDrive(LocoSim::Motion::Drive::Accelerate);
        CHECK(window.start(), "starts");
        const auto run = [&](int ticks, quint8 a9, quint8 sa9, bool valid = true) {
            for (int i = 0; i < ticks; ++i) {
                if (i % 5 == 0) bench->biu()->receive(command418(a9, sa9, valid), vcc, 50001, t);
                t += 50;
                bench->advance(0.05);
                window.tick();
            }
        };

        // EB: the full emergency rate at once, while the air has hardly moved.
        run(1, 0, 48);
        CHECK(window.motion().braking() && near418(window.motion().brake(), 0.9)
                  && window.motion().drive() == LocoSim::Motion::Drive::Hold,
              "EB commanded: 0.9 m/s² at once, the drive at Hold");
        CHECK(near418(bench->pressure(BrakePlant::Bc), 0.05, 1e-6), "though the bench's BC is only at 0.05");
        CHECK(near418(window.motion().speed(), 90 - 0.9 * 3.6 * 0.05, 1e-9), "50 ms of it: 0.162 km/h slower");
        CHECK(state && state->text().startsWith(QStringLiteral("braking EB 0.90 m/s\u00B2"))
                  && state->text().contains(QStringLiteral("A9 0.00 \u00B7 SA9 2.40")),
              qPrintable(QStringLiteral("the Drive tab says the brake, its rate and the command: %1").arg(state ? state->text() : QString())));
        double before = window.motion().speed();
        run(20, 0, 48);
        CHECK(near418(window.motion().speed(), before - 0.9 * 3.6, 1e-6), "1 s of EB: 3.24 km/h slower, a steady rate");

        // FSB, then NB: each its own rate, at once.
        run(1, 70, 48);
        CHECK(near418(window.motion().brake(), 0.6) && state && state->text().startsWith(QLatin1String("braking FSB 0.60")),
              "FSB commanded (A9 3.5): 0.6");
        before = window.motion().speed();
        run(20, 70, 48);
        CHECK(near418(window.motion().speed(), before - 0.6 * 3.6, 1e-6), "1 s of FSB: 2.16 km/h slower");
        run(1, 88, 24);
        CHECK(near418(window.motion().brake(), 0.3) && state && state->text().startsWith(QLatin1String("braking NB 0.30")),
              "NB commanded (A9 4.4, SA9 1.2): 0.3");
        run(1, 100, 70);
        CHECK(near418(window.motion().brake(), 0.6) && state && state->text().startsWith(QLatin1String("braking LE 0.60")),
              "the light engine brake (A9 5.0, SA9 3.5): full service");

        // Released: at once, though the air is still coming back; the speed held.
        run(1, 100, 0);
        CHECK(!window.motion().braking() && window.motion().drive() == LocoSim::Motion::Drive::Hold
                  && bench->pressure(BrakePlant::Bc) > 0.1,
              "released commanded: no brake at once, though BC is still up; the drive at Hold");
        const double held = window.motion().speed();
        run(20, 100, 0);
        CHECK(held > 0 && held < 90 && window.motion().speed() == held, "and the loco holds the speed it slowed to");

        // A command whose A9 and SA9 are not valid: the BIU does not follow it, nor does the loco.
        run(5, 0, 48, false);
        CHECK(!window.motion().braking(), "EB values, A9 and SA9 not valid: no brake");

        // The build-up time.
        LocoSim::BrakeRates rates = window.brakeRates();
        rates.buildUpSeconds = 1.0;
        window.setBrakeRates(rates);
        run(4, 70, 48);
        CHECK(window.motion().braking() && near418(window.motion().brakeEffort(), 0.2)
                  && state && state->text().contains(QLatin1String("(building up)")),
              qPrintable(QStringLiteral("FSB with a 1 s build-up, 0.2 s in: 20 %, and says so (%1)").arg(state ? state->text() : QString())));
        run(20, 70, 48);
        CHECK(near418(window.motion().brakeNow(), 0.6) && state && state->text().startsWith(QLatin1String("braking FSB 0.60")),
              "1 s on: the full 0.6");
        run(1, 100, 0);
        rates.buildUpSeconds = 0;
        window.setBrakeRates(rates);

        // Following off: the command does not touch the loco.
        run(1, 0, 48);
        window.setBrakeFollows(false);
        CHECK(!window.motion().braking() && state && state->text().startsWith(QLatin1String("off")), "following off: no brake");
        window.setBrakeFollows(true);
        CHECK(window.motion().braking() && near418(window.motion().brake(), 0.9), "back on: EB again");

        // The air way is still there: the rate follows BC as it moves.
        window.setBrakeSource(LocoSim::BrakeSource::Air);
        CHECK(window.motion().braking() && state && state->text().contains(QLatin1String("BC ")),
              qPrintable(QStringLiteral("by the bench's air: the Drive tab says the air (%1)").arg(state ? state->text() : QString())));
        CHECK(window.motion().brake() == LocoSim::brakeDeceleration(window.brakeRates(), bench->pressure(BrakePlant::Bp),
                                                                     bench->pressure(BrakePlant::Bc)),
              "and brakes at what the air gives");

        // The bench closes: nothing brakes the loco.
        window.setBrakeSource(LocoSim::BrakeSource::Command);
        bench.reset();
        CHECK(!window.motion().braking() && state && state->text().contains(QLatin1String("Brake Test Bench")),
              "the bench gone: no brake, and the Drive tab says to open it");
        window.stop();

        // Kept: the source and the build-up are settings.
        window.setBrakeSource(LocoSim::BrakeSource::Air);
        rates.buildUpSeconds = 2.5;
        window.setBrakeRates(rates);
    }
    {
        LocoSimWindow again(nullptr);
        again.setAttribute(Qt::WA_DeleteOnClose, false);
        CHECK(again.brakeSource() == LocoSim::BrakeSource::Air && again.brakeRates().buildUpSeconds == 2.5,
              "a new window: by the air, a 2.5 s build-up, as left");
    }
    clearSaved418();
}
