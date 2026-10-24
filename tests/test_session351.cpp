#include "testutil.h"
#include "layoutaudit.h"

#include "cabio.h"
#include "pressuregauge.h"
#include "theme.h"
#include "uistyle.h"

#ifdef DL_HAVE_SERIAL
#include "cabiosimwindow.h"
#endif

#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTabWidget>

#include <cmath>

// =============================================================================
//  Session 351 — the analog card's pressure gauges: a dial per channel in
//  the Cab I/O Simulator's Analog tab. Drag the dial or type under it; either
//  sets the current the VCC reads as that pressure.
// =============================================================================

using namespace CabIo;

namespace {
bool near351(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }
// The point on the dial for a value, the way the dial draws it (270 degrees from lower left).
QPointF pointFor(const PressureGauge &g, double v)
{
    const double side = qMin(g.width(), g.height()) - 8;
    const QPointF c(g.width() / 2.0, g.height() / 2.0);
    const double a = (225 - 270 * v / g.maximum()) * 3.14159265358979323846 / 180;
    return { c.x() + std::cos(a) * side * 0.35, c.y() - std::sin(a) * side * 0.35 };
}
}  // namespace

TEST_SUITE(session351)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    // ---- the dial --------------------------------------------------------------
    {
        PressureGauge g(QStringLiteral("BP"));
        g.resize(200, 200);
        g.setMaximum(10.2);
        CHECK(near351(g.valueAt(pointFor(g, 0)), 0) && near351(g.valueAt(pointFor(g, 10.2)), 10.2),
              "0 at lower left, full scale at lower right");
        CHECK(near351(g.valueAt(pointFor(g, 5.1)), 5.1), "half scale at the top");
        CHECK(near351(g.valueAt(pointFor(g, 3.04)), 3.0), "snaps to 0.1");
        CHECK(near351(g.valueAt(QPointF(100 - 30, 100 + 80)), 0) && near351(g.valueAt(QPointF(100 + 30, 100 + 80)), 10.2),
              "the gap at the bottom: the nearer end");
        CHECK(g.valueAt(QPointF(0, 0)) < 0, "outside the dial: nothing");

        QSignalSpy edited(&g, &PressureGauge::valueEdited);
        g.setValue(5.0);
        CHECK(edited.isEmpty(), "setValue does not report an edit");
        QMouseEvent press(QEvent::MouseButtonPress, pointFor(g, 7.5), g.mapToGlobal(pointFor(g, 7.5).toPoint()),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&g, &press);
        CHECK(edited.size() == 1 && near351(edited.last().at(0).toDouble(), 7.5) && near351(g.value(), 7.5),
              "a click on the dial sets it");
        QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
        QCoreApplication::sendEvent(&g, &up);
        CHECK(near351(g.value(), 7.6), "Up: +0.1");
        QKeyEvent pgdn(QEvent::KeyPress, Qt::Key_PageDown, Qt::NoModifier);
        QCoreApplication::sendEvent(&g, &pgdn);
        CHECK(near351(g.value(), 6.6), "Page Down: -1");
        QKeyEvent end(QEvent::KeyPress, Qt::Key_End, Qt::NoModifier);
        QCoreApplication::sendEvent(&g, &end);
        CHECK(near351(g.value(), 10.2), "End: full scale");
        QCoreApplication::sendEvent(&g, &up);
        CHECK(near351(g.value(), 10.2) && edited.size() == 4, "not past full scale, and no edit for it");
        g.setMaximum(5.0);
        CHECK(near351(g.value(), 5.0), "a smaller scale clamps the value");
        CHECK(!g.accessibleName().isEmpty() && g.focusPolicy() == Qt::StrongFocus, "named and reachable by keyboard");
    }

#ifdef DL_HAVE_SERIAL
    // ---- in the window -----------------------------------------------------------
    {
        CabIoSimWindow w(nullptr);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        for (int ch = 0; ch < kAnalogChannels; ++ch) { w.setAnalogScale(ch, 10); w.setAnalogMa(ch, 4); }
        const QList<PressureGauge *> gauges = w.findChildren<PressureGauge *>();
        CHECK(gauges.size() == kAnalogChannels, "a gauge for each of the six channels");
        if (gauges.size() != kAnalogChannels) return;

        emit gauges[0]->valueEdited(5.0);
        CHECK(near351(w.analogMa(0), 4 + 16 * 5.0 / 10.2, 1e-3), "dragging BP to 5.0 sets 11.843 mA");
        CHECK(near351(gauges[0]->value(), 5.0, 0.01), "and the dial shows what the VCC reads");
        QDoubleSpinBox *kg0 = nullptr;
        for (QDoubleSpinBox *b : w.findChildren<QDoubleSpinBox *>())
            if (b->suffix().contains(QLatin1String("kg/cm")) && near351(b->value(), 5.0, 0.01)) kg0 = b;
        CHECK(kg0 != nullptr, "the box under it follows");

        w.setAnalogPressure(2, 7.0);
        CHECK(near351(gauges[2]->value(), 7.0, 0.01), "typing a pressure moves the dial");
        w.setAnalogScale(2, 16);
        CHECK(near351(gauges[2]->maximum(), 16.32), "a 16 bar sensor: the dial reads to 16.32");

        w.setAnalogMa(1, 2.0);
        CHECK(gauges[1]->fault() == QLatin1String("open loop"), "below 3.5 mA: the BC dial says open loop");
        w.setAnalogMa(1, 21.0);
        CHECK(gauges[1]->fault() == QLatin1String("short"), "above 20.5 mA: short");
        emit gauges[1]->valueEdited(3.0);
        CHECK(gauges[1]->fault().isEmpty() && near351(gauges[1]->value(), 3.0, 0.01),
              "setting a pressure on a faulty channel brings it back");

        auto *tabs = w.findChild<QTabWidget *>();
        if (tabs) tabs->setCurrentIndex(3);
        w.show();
        for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
        CHECK(gauges[0]->isVisible() && gauges[0]->height() >= gauges[0]->minimumSizeHint().height(), "the dials show");
        // The Analog page's own width: the window's is the Inputs tab's tile
        // grid (session 324), 1133 px on the Mac, 1440 with Linux's fonts.
        const QSize page = tabs ? tabs->widget(3)->minimumSizeHint() : QSize();
        CHECK(page.isValid() && page.width() <= 1366 && w.minimumSizeHint().height() <= 700,
              QByteArray("the Analog tab fits a laptop (page ") + QByteArray::number(page.width()) + " wide, window "
                  + QByteArray::number(w.minimumSizeHint().height()) + " tall)");
        const QStringList loose = LayoutAudit::orphans(&w);
        CHECK(loose.isEmpty(), QByteArray("no visible widget outside every layout (") + loose.join(QLatin1String(", ")).toUtf8() + ")");
    }
#endif
}
