#include "testutil.h"
#include "layoutaudit.h"
#include "locosimwindow.h"
#include "settings.h"
#include "speedgauge.h"
#include "theme.h"
#include "uistyle.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSettings>
#include <QSignalSpy>
#include <QTableWidget>

#include <cmath>

// =============================================================================
//  Session 393 — the Loco Simulator laid out like the other simulators: a
//  speedometer to drive with, a gauge per speed sensor channel, the frames
//  sent, the settings on their own tab.
// =============================================================================

TEST_SUITE(session393)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    // ---- the gauge -------------------------------------------------------------
    {
        SpeedGauge gauge(QStringLiteral("Loco"), true);
        gauge.resize(300, 300);
        gauge.setScaleMaximum(140);
        QSignalSpy edited(&gauge, &SpeedGauge::valueEdited);
        const QRectF dial(4, 4, 292, 292);
        // 0 at 225 degrees (lower left), the top of the scale at -45 (lower right).
        const QPointF centre = dial.center();
        const auto pointAt = [&](double degrees) {
            const double radians = degrees * 3.14159265358979 / 180;
            return QPointF(centre.x() + std::cos(radians) * 100, centre.y() - std::sin(radians) * 100);
        };
        CHECK(gauge.valueAt(pointAt(225)) == 0, "lower left is 0 km/h");
        CHECK(gauge.valueAt(pointAt(90)) == 70, "the top is half the scale");
        CHECK(gauge.valueAt(pointAt(-45)) == 140, "lower right is the top of the scale");
        CHECK(gauge.valueAt(QPointF(1, 1)) < 0, "outside the dial is no value");

        QMouseEvent press(QEvent::MouseButtonPress, pointAt(90), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&gauge, &press);
        CHECK(edited.size() == 1 && gauge.value() == 70, "a click sets the speed and says so");
        QKeyEvent pageUp(QEvent::KeyPress, Qt::Key_PageUp, Qt::NoModifier);
        QCoreApplication::sendEvent(&gauge, &pageUp);
        CHECK(gauge.value() == 80, "Page Up: 10 km/h");

        SpeedGauge readout(QStringLiteral("Ch 1"), false);
        readout.resize(150, 150);
        QSignalSpy untouched(&readout, &SpeedGauge::valueEdited);
        QCoreApplication::sendEvent(&readout, &press);
        CHECK(untouched.isEmpty() && readout.value() == 0, "a sensor's gauge only shows; a click does nothing");
    }

    // ---- the window --------------------------------------------------------------
    {
        {
            QSettings settings(Settings::iniPath(), QSettings::IniFormat);
            settings.remove(QStringLiteral("locosim"));
        }
        LocoSimWindow window(nullptr);
        window.setAttribute(Qt::WA_DeleteOnClose, false);
        qint64 nowMs = 1782558147000LL;
        window.setClock([&nowMs]() { return nowMs; });
        QVector<QByteArray> sent;
        window.setSendSink([&sent](const QByteArray &message) { sent.append(message); });

        auto *speedometer = window.findChild<SpeedGauge *>(QStringLiteral("locoSimSpeedometer"));
        CHECK(speedometer, "the loco has a speedometer");
        if (speedometer) {
            speedometer->resize(300, 300);
            emit speedometer->valueEdited(60);
            CHECK(window.motion().speed() == 60, "setting the speedometer sets the loco's speed");
            CHECK(speedometer->limit() == 120 && speedometer->scaleMaximum() == 140,
                  "max speed 120: red above it, on a 0-140 scale");
        }

        for (QCheckBox *box : window.findChildren<QCheckBox *>()) box->setChecked(true);
        window.findChild<QDoubleSpinBox *>(QStringLiteral("locoSimChannel22"))->setValue(90);
        CHECK(window.start(), "starts");
        window.tick();
        CHECK(sent.isEmpty(), "a tick with no time passed sends nothing (0 pulses would say the loco stopped)");
        nowMs += 100;
        window.tick();
        CHECK(sent.size() == 2, "100 ms later: one message from each sensor");

        QVector<SpeedGauge *> channels;
        for (SpeedGauge *gauge : window.findChildren<SpeedGauge *>())
            if (gauge != speedometer) channels.append(gauge);
        CHECK(channels.size() == 4, "a gauge for each sensor's two channels");
        bool sixty = false, ninety = false;
        for (SpeedGauge *gauge : channels) {
            if (std::fabs(gauge->value() - 60) < 0.2) sixty = true;
            if (std::fabs(gauge->value() - 90) < 0.2) ninety = true;
        }
        CHECK(sixty && ninety, "the gauges read the speed the pulses say: 60, and 90 on the slipping channel");

        auto *frames = window.findChild<QTableWidget *>(QStringLiteral("locoSimFrames"));
        CHECK(frames && frames->rowCount() == 2 && frames->item(1, 4)
                  && frames->item(1, 4)->text() == QString::number(25129),
              "Frames: each message, with sensor 2's channel 2 count");
        window.stop();

        window.show();
        for (int round = 0; round < 3; ++round) QCoreApplication::processEvents();
        CHECK(window.minimumSizeHint().width() <= 1366 && window.minimumSizeHint().height() <= 700,
              QByteArray("fits a laptop (") + QByteArray::number(window.minimumSizeHint().width()) + "x"
                  + QByteArray::number(window.minimumSizeHint().height()) + ")");
        const QStringList loose = LayoutAudit::orphans(&window);
        CHECK(loose.isEmpty(), QByteArray("no visible widget outside every layout (")
                                   + loose.join(QLatin1String(", ")).toUtf8() + ")");
        window.close();

        QSettings settings(Settings::iniPath(), QSettings::IniFormat);
        settings.remove(QStringLiteral("locosim"));
    }
}
