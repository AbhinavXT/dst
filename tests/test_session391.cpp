#include "testutil.h"

#include "locosim.h"
#include "locosimwindow.h"
#include "settings.h"
#include "theme.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QSettings>
#include <QtEndian>

#include <cmath>

// =============================================================================
//  Session 391 \u2014 Loco Simulator, step 1: speed and the two speed sensors.
//  The message is LocoTcasSimulator's LPIM_LKAVACH_INFO_MSG (19 B, packed LE).
//  No real one has been captured: the expected bytes are laid out by hand from
//  its C struct (header src/dest/msg id/length, reader, dir, tacho 1, tacho 2,
//  crc 0).
// =============================================================================

namespace {
quint32 tachometer(const QByteArray &message, int which)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(message.constData()) + (which == 1 ? 7 : 11));
}
}

TEST_SUITE(session391)
{
    using namespace LocoSim;

    // ---- the message ---------------------------------------------------------
    {
        const QByteArray message = speedMessage(1, Forward, 16753, 0x01020304);
        CHECK(message.toHex() == QByteArray("1402011300" "01" "01" "71410000" "04030201" "00000000"),
              "the simulator's message: 20, 2, id 1, length 19, sensor 1, forward, tachos LE, crc 0");
        CHECK(speedMessage(2, Reverse, 0, 0).at(5) == 2 && speedMessage(2, Reverse, 0, 0).at(6) == 2,
              "sensor 2, reverse");
    }

    // ---- the pulses ------------------------------------------------------------
    {
        const double perMetre = pulsesPerMetre(Wheel());
        CHECK(std::fabs(perMetre - 30000.0 / (3.14159265358979 * 0.95)) < 1e-6,
              "950 mm, 30 a turn, x1000, real pi: 10051.9 a metre");

        Motion motion;
        motion.setSpeed(60);
        const Step first = motion.step(0.1);
        CHECK(first.pulses[0][0] == 16753 && first.pulses[1][1] == 16753,
              "60 km/h for 100 ms: 16753 on every channel (the simulator's 3.14 gave 16761)");
        // Session 408: every message one interval's (100 ms) pulses, truncated, as
        // the simulator: the same count at a steady speed, whatever the clock did.
        bool steady = true;
        for (int tick = 1; tick < 100; ++tick) steady = steady && motion.step(0.1).pulses[0][0] == 16753;
        CHECK(steady, "100 messages at 60 km/h: all 16753, no fraction carried from one to the next");
        CHECK(std::fabs(motion.position() - 166.6667) < 0.001, "60 km/h for 10 s: 166.67 m");
        bool jitter = true;
        for (double ms : { 97.0, 103.0, 250.0, 100.0 }) jitter = jitter && motion.step(ms / 1000).pulses[0][0] == 16753;
        CHECK(jitter, "a tick 97, 103 or 250 ms after the last: still 16753 (the interval's, not the clock's)");
        Motion eighty;
        eighty.setSpeed(80);
        const quint32 at80 = eighty.step(0.1).pulses[0][0];
        CHECK(at80 == quint32(std::floor(80 / 3.6 * 0.1 * perMetre)) && eighty.step(0.1).pulses[1][0] == at80
                  && eighty.step(0.13).pulses[0][1] == at80,
              QByteArray("80 km/h: ") + QByteArray::number(at80) + " on every channel, every message");
        Motion slower;
        slower.setSpeed(60);
        slower.setPulseInterval(0.2);
        CHECK(slower.step(0.2).pulses[0][0] == quint32(std::floor(60 / 3.6 * 0.2 * perMetre)), "a 200 ms interval: 200 ms of pulses");
    }

    // ---- driving -----------------------------------------------------------------
    {
        Motion motion;
        motion.setMaxSpeed(10);
        motion.setRate(2);
        motion.setDrive(Motion::Drive::Accelerate);
        motion.step(1);
        CHECK(std::fabs(motion.speed() - 2) < 1e-9, "accelerate 2 km/h/s for 1 s: 0 to 2 km/h");
        CHECK(std::fabs(motion.position() - 1 / 3.6) < 1e-9, "and moves at the mean speed, 1 km/h");
        motion.step(10);
        CHECK(motion.speed() == 10, "held at max speed");
        motion.setDrive(Motion::Drive::Decelerate);
        motion.step(20);
        CHECK(motion.speed() == 0, "decelerates to 0, not below");
        motion.setSpeed(300);
        CHECK(motion.speed() == 10, "a set speed above max is held to it");

        motion.setMaxSpeed(120);
        motion.setSpeed(36);
        motion.setDrive(Motion::Drive::Hold);
        motion.setDirection(Reverse);
        motion.setPosition(1000);
        motion.step(1);
        CHECK(std::fabs(motion.position() - 990) < 1e-9, "reverse counts the position down");

        motion.setDirection(Forward);
        motion.setChannelSpeed(1, 1, 0);
        motion.setChannelSpeed(0, 1, 72);
        const Step slip = motion.step(0.1);
        CHECK(slip.pulses[1][1] == 0, "a channel at 0 km/h (skid, dead): no pulses");
        CHECK(slip.pulses[0][1] > slip.pulses[0][0] * 1.99 && slip.pulses[0][1] < slip.pulses[0][0] * 2.01,
              "a channel at 72 km/h on a 36 km/h loco: twice the pulses (slip)");
        CHECK(std::fabs(slip.metres - 1) < 1e-9, "the loco still moves at its own speed");
    }

    // ---- the window ------------------------------------------------------------------
    {
        {
            QSettings settings(Settings::iniPath(), QSettings::IniFormat);
            settings.remove(QStringLiteral("locosim"));
        }
        ThemeUtil::apply(Theme::Light);
        LocoSimWindow window(nullptr);
        window.setAttribute(Qt::WA_DeleteOnClose, false);

        QLineEdit *hosts = window.findChild<QLineEdit *>(QStringLiteral("locoSimHosts"));
        CHECK(hosts && hosts->text() == QStringLiteral("192.168.25.16, 192.168.25.102, 192.168.25.103, 192.168.25.104"),
              "sends to loco 1's four VCCs by default (the simulator's Ipconfig1.xml)");

        window.setHosts(QString());
        CHECK(!window.start() && !window.running(), "Start with no VCC listed is refused");

        QVector<QByteArray> sent;
        window.setSendSink([&sent](const QByteArray &message) { sent.append(message); });
        for (QCheckBox *box : window.findChildren<QCheckBox *>()) box->setChecked(true);
        window.findChild<QDoubleSpinBox *>(QStringLiteral("locoSimSpeed"))->setValue(60);
        CHECK(window.start() && window.running(), "starts");
        window.advance(0.1);
        CHECK(sent.size() == 2 && sent.at(0).at(5) == 1 && sent.at(1).at(5) == 2,
              "one tick: one message from each sensor");
        CHECK(sent.size() == 2 && tachometer(sent.at(0), 1) == 16753 && tachometer(sent.at(1), 2) == 16753,
              "the speed box drives the pulses");
        window.stop();
        CHECK(!window.running() && window.sentCount() == 2, "stops");

        QSettings settings(Settings::iniPath(), QSettings::IniFormat);
        settings.remove(QStringLiteral("locosim"));
    }
}
