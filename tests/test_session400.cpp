#include "testutil.h"

#include "locosimwindow.h"
#include "settings.h"

#include <QDoubleSpinBox>
#include <QLabel>
#include <QSettings>
#include <QToolButton>

// =============================================================================
//  Session 400 — the Loco Simulator's RFID tab shows the loco's speed, and
//  sets it: a readout, a box and -10 / -5 / +5 / +10, the same speed as the
//  Drive tab's; and "braking … m/s²" while the brake acts.
// =============================================================================

namespace {

QToolButton *stepButton400(LocoSimWindow &window, const QString &text)
{
    auto *rfidSpeed = window.findChild<QDoubleSpinBox *>(QStringLiteral("locoSimRfidSpeed"));
    if (!rfidSpeed) return nullptr;
    for (QToolButton *button : rfidSpeed->parentWidget()->findChildren<QToolButton *>())
        if (button->text() == text) return button;
    return nullptr;
}

}  // namespace

TEST_SUITE(session400)
{
    {
        QSettings settings(Settings::iniPath(), QSettings::IniFormat);
        settings.remove(QStringLiteral("locosim"));
    }
    {
        LocoSimWindow window(nullptr);
        window.setAttribute(Qt::WA_DeleteOnClose, false);
        auto *value = window.findChild<QLabel *>(QStringLiteral("locoSimRfidSpeedValue"));
        auto *rfidSpeed = window.findChild<QDoubleSpinBox *>(QStringLiteral("locoSimRfidSpeed"));
        auto *driveSpeed = window.findChild<QDoubleSpinBox *>(QStringLiteral("locoSimSpeed"));
        auto *brake = window.findChild<QLabel *>(QStringLiteral("locoSimRfidBrake"));
        CHECK(value && rfidSpeed && driveSpeed && brake, "the RFID tab has a speed readout, a speed box and a brake line");
        if (!value || !rfidSpeed || !driveSpeed || !brake) return;

        driveSpeed->setValue(60);
        CHECK(value->text() == QLatin1String("60.0 km/h") && rfidSpeed->value() == 60,
              "a speed set on the Drive tab shows on the RFID tab: 60.0 km/h");
        rfidSpeed->setValue(30);
        CHECK(window.motion().speed() == 30 && driveSpeed->value() == 30 && value->text() == QLatin1String("30.0 km/h"),
              "set on the RFID tab: the loco at 30, the Drive tab says so");

        QToolButton *plus5 = stepButton400(window, QStringLiteral("+5"));
        QToolButton *minus10 = stepButton400(window, QStringLiteral("-10"));
        QToolButton *plus10 = stepButton400(window, QStringLiteral("+10"));
        QToolButton *minus5 = stepButton400(window, QStringLiteral("-5"));
        CHECK(plus5 && minus10 && plus10 && minus5, "-10, -5, +5 and +10 beside it");
        if (plus5 && minus10 && minus5 && plus10) {
            plus5->click();
            CHECK(window.motion().speed() == 35 && rfidSpeed->value() == 35, "+5: 35");
            minus10->click();
            CHECK(window.motion().speed() == 25, "-10: 25");
            window.motion().setSpeed(3);
            minus5->click();
            CHECK(window.motion().speed() == 0 && value->text() == QLatin1String("0.0 km/h"), "-5 from 3: 0, not below");
            window.motion().setMaxSpeed(120);
            window.motion().setSpeed(115);
            plus10->click();
            CHECK(window.motion().speed() == 120, "+10 from 115: held to the max speed, 120");
        }

        CHECK(brake->text().isEmpty(), "no brake: the brake line is empty");
        window.setBrakeSource(LocoSim::BrakeSource::Air);   // session 418: the air way (the default is the command)
        window.observeAir(3.5, 2.4);
        CHECK(brake->text() == QStringLiteral("braking 0.60 m/s\u00B2"), "FSB air: braking 0.60 m/s² on the RFID tab");
        window.observeAir(5.0, 0.0);
        CHECK(brake->text().isEmpty(), "released: empty again");

        window.show();
        for (int round = 0; round < 3; ++round) QCoreApplication::processEvents();
        CHECK(window.minimumSizeHint().width() <= 1366 && window.minimumSizeHint().height() <= 700,
              QByteArray("still fits a laptop (") + QByteArray::number(window.minimumSizeHint().width()) + "x"
                  + QByteArray::number(window.minimumSizeHint().height()) + ")");
        window.close();
    }
    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    settings.remove(QStringLiteral("locosim"));
}
