#include "pressuregauge.h"

#include "uicolors.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QWheelEvent>

#include <cmath>

namespace {
constexpr double kStartDeg = 225;   // 0 at lower left, clockwise to lower right
constexpr double kSpanDeg  = 270;
constexpr double kPi       = 3.14159265358979323846;
double snap(double v) { return std::round(v * 10) / 10; }
QPointF eventPos(const QMouseEvent *e)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return e->position();
#else
    return e->localPos();
#endif
}
}  // namespace

PressureGauge::PressureGauge(const QString &title, QWidget *parent)
    : QWidget(parent)
    , m_title(title)
{
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setAccessibleName(tr("%1 pressure").arg(title));
    setToolTip(tr("Click or drag to set %1. Wheel or arrow keys: 0.1; Page Up/Down: 1.").arg(title));
}

void PressureGauge::setMaximum(double kgcm2)
{
    m_max = kgcm2 > 0 ? kgcm2 : 1;
    m_value = qBound(0.0, m_value, m_max);
    update();
}

void PressureGauge::setValue(double kgcm2)
{
    m_value = qBound(0.0, kgcm2, m_max);
    update();
}

void PressureGauge::setFault(const QString &why)
{
    m_fault = why;
    update();
}

void PressureGauge::setTarget(double kgcm2)
{
    if (qFuzzyCompare(kgcm2 + 2, m_target + 2)) return;
    m_target = kgcm2;
    update();
}

QSize PressureGauge::sizeHint() const { return { 170, 170 }; }
QSize PressureGauge::minimumSizeHint() const { return { 80, 80 }; }

QRectF PressureGauge::dialRect() const
{
    const double side = qMin(width(), height()) - 8;
    return { (width() - side) / 2.0, (height() - side) / 2.0, side, side };
}

double PressureGauge::valueAt(const QPointF &p) const
{
    const QRectF r = dialRect();
    const QPointF d = p - r.center();
    if (std::hypot(d.x(), d.y()) > r.width() / 2) return -1;
    // Qt's y grows down; angle in degrees, counter-clockwise from 3 o'clock.
    double deg = std::atan2(-d.y(), d.x()) * 180 / kPi;
    double along = kStartDeg - deg;                 // clockwise from the 0 mark
    while (along < 0) along += 360;
    while (along >= 360) along -= 360;
    if (along > kSpanDeg) along = along - kSpanDeg < (360 - kSpanDeg) / 2 ? kSpanDeg : 0;   // the gap: nearer end
    return snap(qBound(0.0, along / kSpanDeg * m_max, m_max));
}

void PressureGauge::edit(double kgcm2)
{
    const double v = qBound(0.0, snap(kgcm2), m_max);
    if (!m_fault.isEmpty() || v != m_value) {
        m_value = v;
        update();
        emit valueEdited(v);
    }
}

void PressureGauge::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return QWidget::mousePressEvent(e);
    const double v = valueAt(eventPos(e));
    if (v >= 0) edit(v);
}

void PressureGauge::mouseMoveEvent(QMouseEvent *e)
{
    if (!(e->buttons() & Qt::LeftButton)) return;
    const double v = valueAt(eventPos(e));
    if (v >= 0) edit(v);
}

void PressureGauge::wheelEvent(QWheelEvent *e)
{
    const int steps = e->angleDelta().y() / 120;
    if (steps != 0) edit(m_value + steps * 0.1);
    e->accept();
}

void PressureGauge::keyPressEvent(QKeyEvent *e)
{
    switch (e->key()) {
    case Qt::Key_Up: case Qt::Key_Right: edit(m_value + 0.1); break;
    case Qt::Key_Down: case Qt::Key_Left: edit(m_value - 0.1); break;
    case Qt::Key_PageUp: edit(m_value + 1); break;
    case Qt::Key_PageDown: edit(m_value - 1); break;
    case Qt::Key_Home: edit(0); break;
    case Qt::Key_End: edit(m_max); break;
    default: QWidget::keyPressEvent(e); return;
    }
}

void PressureGauge::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = dialRect();
    const QPointF c = r.center();
    const double rad = r.width() / 2;
    const QColor ink = palette().color(QPalette::Text);
    const bool faulty = !m_fault.isEmpty();

    p.setPen(QPen(UiColor::frame(), 1.5));
    p.setBrush(palette().color(QPalette::Base));
    p.drawEllipse(r);

    // The value arc, then the ticks: every 1 kg/cm^2 long and numbered, every 0.5 short.
    const QRectF arc = r.adjusted(rad * 0.12, rad * 0.12, -rad * 0.12, -rad * 0.12);
    p.setPen(QPen(UiColor::grid(), rad * 0.06, Qt::SolidLine, Qt::FlatCap));
    p.drawArc(arc, int(kStartDeg * 16), int(-kSpanDeg * 16));
    if (!faulty && m_value > 0) {
        p.setPen(QPen(UiColor::accent(), rad * 0.06, Qt::SolidLine, Qt::FlatCap));
        p.drawArc(arc, int(kStartDeg * 16), int(-kSpanDeg * m_value / m_max * 16));
    }
    QFont small = font();
    small.setPointSizeF(qMax(6.0, rad * 0.09));
    p.setFont(small);
    const auto at = [&](double v, double k) {
        const double a = (kStartDeg - kSpanDeg * v / m_max) * kPi / 180;
        return QPointF(c.x() + std::cos(a) * rad * k, c.y() - std::sin(a) * rad * k);
    };
    for (int half = 0; half <= int(m_max * 2 + 1e-9); ++half) {
        const double v = half / 2.0;
        const bool major = half % 2 == 0;
        p.setPen(QPen(major ? ink : UiColor::muted(), major ? 1.5 : 1));
        p.drawLine(at(v, major ? 0.70 : 0.75), at(v, 0.80));
        if (major && (int(v) % (m_max > 12 ? 2 : 1) == 0)) {
            const QPointF t = at(v, 0.57);
            p.drawText(QRectF(t.x() - 12, t.y() - 8, 24, 16), Qt::AlignCenter, QString::number(int(v)));
        }
    }

    // The reading (or why there is none) under the hub, the unit, then the
    // title in the scale's gap at the bottom, clear of the numbers.
    QFont big = font();
    big.setPointSizeF(qMax(7.0, rad * (faulty ? 0.11 : 0.15)));
    big.setBold(!faulty);
    p.setFont(big);
    p.setPen(faulty ? UiColor::warning() : ink);
    p.drawText(QRectF(r.left(), c.y() + rad * 0.14, r.width(), rad * 0.26), Qt::AlignCenter,
               faulty ? m_fault : QString::number(m_value, 'f', 2));
    p.setFont(small);
    p.setPen(UiColor::muted());
    p.drawText(QRectF(r.left(), c.y() + rad * 0.38, r.width(), rad * 0.18), Qt::AlignCenter,
               QStringLiteral("kg/cm\u00B2"));
    QFont title = font();
    title.setBold(true);
    title.setPointSizeF(qMax(7.0, rad * 0.12));
    p.setFont(title);
    p.setPen(ink);
    p.drawText(QRectF(c.x() - rad * 0.6, c.y() + rad * 0.58, rad * 1.2, rad * 0.3), Qt::AlignCenter, m_title);

    // The target: a wedge on the rim, pointing in.
    if (!faulty && m_target >= 0) {
        const double v = qMin(m_target, m_max);
        const double a = (kStartDeg - kSpanDeg * v / m_max) * kPi / 180;
        const QPointF tip = at(v, 0.82);
        const QPointF side(-std::sin(a) * rad * 0.05, -std::cos(a) * rad * 0.05);
        const QPointF outer = at(v, 0.97);
        QPolygonF wedge;
        wedge << tip << outer + side << outer - side;
        p.setPen(Qt::NoPen);
        p.setBrush(UiColor::ok());
        p.drawPolygon(wedge);
    }

    // The needle.
    const QColor needle = faulty ? UiColor::muted() : UiColor::error();
    p.setPen(QPen(needle, qMax(2.0, rad * 0.03), Qt::SolidLine, Qt::RoundCap));
    p.drawLine(c, at(faulty ? 0 : m_value, 0.78));
    p.setPen(Qt::NoPen);
    p.setBrush(needle);
    p.drawEllipse(c, rad * 0.06, rad * 0.06);

    if (hasFocus()) {
        p.setPen(QPen(UiColor::accent(), 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(r.adjusted(2, 2, -2, -2));
    }
}
