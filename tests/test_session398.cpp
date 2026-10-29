#include "testutil.h"

#include "biuprotocol.h"
#include "biusim.h"
#include "brakebenchwindow.h"
#include "locosim.h"
#include "locosimwindow.h"
#include "settings.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QLabel>
#include <QSettings>

#include <cmath>
#include <memory>

// =============================================================================
//  Session 398 — Loco Simulator, step 3: braking from the BIU's command.
//  The VCC commands the BIU; the Brake Test Bench's air moves toward it; the
//  loco slows in proportion to BC, BP picking the ceiling between the
//  full-service and the emergency rate. The brake wins over the drive and
//  leaves it at Hold. No BIU frame has been captured yet: the command frames
//  are session 360's, as the reviewed firmware sends them.
// =============================================================================

using namespace Biu;

namespace {

bool near398(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }

constexpr quint8 kHealthSpeed398 = (1u << CmdHealth) | (1u << CmdSpeedValid);
constexpr quint8 kBothValid398 = (1u << CmdA9Valid) | (1u << CmdSa9Valid);

QByteArray command398(quint8 a9, quint8 sa9)
{
    Command c;
    c.speedRaw = 1000;
    c.d1 = kHealthSpeed398;
    c.d2 = kBothValid398;
    c.a9Raw = a9;
    c.sa9Raw = sa9;
    return toDatagram({ kCobCommand, encodeCommand(c) });
}

void clearSaved398()
{
    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    settings.remove(QStringLiteral("locosim"));
    settings.remove(QStringLiteral("brakebench"));
}

}  // namespace

TEST_SUITE(session398)
{
    // ---- the deceleration the air gives ------------------------------------------------
    {
        const LocoSim::BrakeRates rates;   // 0.6 full service, 0.9 emergency, BC 2.4 full, BP 3.5 FSB
        CHECK(LocoSim::brakeDeceleration(rates, 5.0, 0.0) == 0, "released (BP 5.0, BC 0): no brake");
        CHECK(LocoSim::brakeDeceleration(rates, 5.0, 0.1) == 0, "BC 0.1, inside the released tolerance: no brake");
        CHECK(near398(LocoSim::brakeDeceleration(rates, 4.4, 1.2), 0.3), "NB (BP 4.4, BC 1.2): half full service, 0.3");
        CHECK(near398(LocoSim::brakeDeceleration(rates, 3.5, 2.4), 0.6), "FSB (BP 3.5, BC 2.4): full service, 0.6");
        CHECK(near398(LocoSim::brakeDeceleration(rates, 0.0, 2.4), 0.9), "EB (BP 0, BC 2.4): emergency, 0.9");
        CHECK(near398(LocoSim::brakeDeceleration(rates, 1.75, 2.4), 0.75), "BP half way under FSB: half way to emergency");
        CHECK(near398(LocoSim::brakeDeceleration(rates, 5.0, 3.5), 0.6), "LE (BC 3.5, BP charged): held to full service");
        CHECK(near398(LocoSim::brakeDeceleration(rates, 0.0, 1.2), 0.45), "EB with BC still at 1.2: half the emergency rate");
    }

    // ---- the motion: the brake wins, then holds ----------------------------------------
    {
        LocoSim::Motion motion;
        motion.setSpeed(72);                       // 20 m/s
        motion.setDrive(LocoSim::Motion::Drive::Accelerate);
        motion.setBrake(0.6);
        CHECK(motion.braking() && motion.drive() == LocoSim::Motion::Drive::Hold, "a brake coming on sets the drive to Hold");
        motion.setDrive(LocoSim::Motion::Drive::Accelerate);   // the operator clicks Accelerate anyway
        const LocoSim::Step step = motion.step(1.0);
        CHECK(near398(motion.speed(), 72 - 0.6 * 3.6), "1 s at 0.6 m/s²: 72 km/h down to 69.84, Accelerate ignored");
        CHECK(near398(step.metres, (20 + 19.4) / 2), "the metres at the mean speed of the step: 19.7");
        motion.setBrake(0);
        CHECK(!motion.braking() && motion.drive() == LocoSim::Motion::Drive::Hold, "released: Hold, not the Accelerate clicked");
        const double held = motion.speed();
        motion.step(1.0);
        CHECK(motion.speed() == held, "and the loco keeps the speed it slowed to");
        motion.setBrake(0.9);
        motion.step(60);
        CHECK(motion.speed() == 0, "a long brake stops the loco at 0, not below");
    }

    // ---- the window, with the Brake Test Bench --------------------------------------------
    clearSaved398();
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
        CHECK(window.brakeFollows() && state && state->text().contains(QLatin1String("Brake Test Bench")),
              "follows the bench by default; with no bench open, the Drive tab says to open it");

        window.followBench(bench.get());
        CHECK(state && state->text().startsWith(QLatin1String("released")), "the bench open, its air released: released");

        // The VCC commands EB (A9 0, SA9 2.4) and keeps commanding it, as the firmware does every 250 ms.
        bench->biu()->setRunning(true, t);
        const QString vcc = QStringLiteral("192.168.25.126");
        bench->biu()->receive(toDatagram({ kCobHeartbeatKavach, QByteArray(1, char(Operational)) }), vcc, 50001, t);
        window.motion().setSpeed(90);
        window.motion().setDrive(LocoSim::Motion::Drive::Accelerate);
        CHECK(window.start(), "starts");
        const auto run = [&](int ticks, quint8 a9, quint8 sa9) {
            for (int i = 0; i < ticks; ++i) {
                if (i % 5 == 0) bench->biu()->receive(command398(a9, sa9), vcc, 50001, t);
                t += 50;
                bench->advance(0.05);
                window.tick();
            }
        };
        run(1, 0, 48);
        CHECK(!window.motion().braking() && near398(bench->pressure(BrakePlant::Bc), 0.05, 1e-6),
              "50 ms in, BC 0.05: inside the released tolerance, no brake");
        run(2, 0, 48);
        CHECK(window.motion().braking() && window.motion().drive() == LocoSim::Motion::Drive::Hold,
              "BC past 0.1: the loco brakes, the drive at Hold");
        bool buttonsOff = true;
        for (QAbstractButton *button : window.findChildren<QAbstractButton *>())
            if (button->text().contains(QLatin1String("Accelerate"))) buttonsOff = buttonsOff && !button->isEnabled();
        CHECK(buttonsOff, "while it brakes the drive buttons are off: the brake wins");
        run(57, 0, 48);
        const double bc = bench->pressure(BrakePlant::Bc), bp = bench->pressure(BrakePlant::Bp);
        CHECK(near398(bc, 2.4, 0.01) && near398(bp, 2.0, 0.06), "3 s into EB at 1 kg/cm²/s: BC 2.4, BP down to 2.0");
        const double speedBefore = window.motion().speed();
        run(20, 0, 48);
        const double expected = LocoSim::brakeDeceleration(window.brakeRates(), bench->pressure(BrakePlant::Bp), 2.4);
        CHECK(window.motion().speed() < speedBefore - (0.6 * 3.6) * 0.99,
              "with BC full and BP falling, the loco slows faster than full service");
        CHECK(state && state->text().startsWith(QLatin1String("braking"))
                  && state->text().contains(QString::number(expected, 'f', 2)),
              qPrintable(QStringLiteral("the Drive tab says the rate and the air: %1").arg(state ? state->text() : QString())));

        // Released: no brake command, the air goes back; the loco keeps what it slowed to.
        run(100, 100, 0);   // A9 5.0, SA9 0
        if (window.motion().braking()) run(100, 100, 0);
        CHECK(!window.motion().braking() && window.motion().drive() == LocoSim::Motion::Drive::Hold,
              qPrintable(QStringLiteral("released (BC %1): no brake, the drive at Hold")
                             .arg(bench->pressure(BrakePlant::Bc))));
        const double held = window.motion().speed();
        CHECK(held > 0 && held < 90, "the loco slowed, and did not stop");
        run(20, 100, 0);
        CHECK(window.motion().speed() == held, "and holds that speed");

        // A hand on the bench's dial brakes the loco too: it follows the air, whoever moved it.
        bench->setSource(BrakeBenchWindow::Source::Manual);
        bench->plant().setValue(BrakePlant::Bc, 2.4);
        bench->plant().setTarget(BrakePlant::Bc, 2.4);
        bench->plant().setValue(BrakePlant::Bp, 3.5);
        bench->plant().setTarget(BrakePlant::Bp, 3.5);
        t += 50;
        bench->advance(0.05);
        const double before = window.motion().speed();
        t += 1000;
        window.tick();
        CHECK(near398(window.motion().speed(), before - 0.6 * 3.6, 1e-6), "BC 2.4 / BP 3.5 by hand: full service, 2.16 km/h in 1 s");

        // Following off: the air does not touch the loco.
        window.setBrakeFollows(false);
        CHECK(!window.motion().braking() && state && state->text().startsWith(QLatin1String("off")), "following off: no brake");
        const double free = window.motion().speed();
        t += 1000;
        window.tick();
        CHECK(window.motion().speed() == free, "and the speed holds");
        window.setBrakeFollows(true);
        CHECK(window.motion().braking(), "back on: brakes again");

        // The bench closes: nothing brakes the loco, and the Drive tab says so.
        bench.reset();
        CHECK(!window.motion().braking() && state && state->text().contains(QLatin1String("Brake Test Bench")),
              "the bench gone: no brake, and the Drive tab says to open it");
        window.stop();

        // The rates are settings, kept.
        LocoSim::BrakeRates rates;
        rates.fullServiceMps2 = 0.5;
        rates.emergencyMps2 = 1.1;
        rates.fullBcKgcm2 = 3.5;
        window.setBrakeRates(rates);
    }
    {
        LocoSimWindow again(nullptr);
        again.setAttribute(Qt::WA_DeleteOnClose, false);
        const LocoSim::BrakeRates rates = again.brakeRates();
        CHECK(rates.fullServiceMps2 == 0.5 && rates.emergencyMps2 == 1.1 && rates.fullBcKgcm2 == 3.5,
              "the brake rates come back the next time the window opens");

        again.show();
        for (int round = 0; round < 3; ++round) QCoreApplication::processEvents();
        CHECK(again.minimumSizeHint().width() <= 1366 && again.minimumSizeHint().height() <= 700,
              QByteArray("still fits a laptop (") + QByteArray::number(again.minimumSizeHint().width()) + "x"
                  + QByteArray::number(again.minimumSizeHint().height()) + ")");
        again.close();
    }
    clearSaved398();
}
