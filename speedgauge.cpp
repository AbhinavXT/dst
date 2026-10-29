#include "speedgauge.h"

#include "simwidgets.h"
#include "uicolors.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <cmath>

namespace {
constexpr double kStartDeg = 225;   // 0 at lower left, clockwise to lower right
constexpr double kSpanDeg  = 270;
constexpr double kPi       = 3.14159265358979323846;
QPointF eventPos(const QMouseEvent *e)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return e->position();
#else
    return e->localPos();
#endif
}
}  // namespace

SpeedGauge::SpeedGauge(const QString &title, bool editable, QWidget *parent)
    : QWidget(parent)
    , m_title(title)
    , m_editable(editable)
{
    setAccessibleName(tr("%1 speed").arg(title));
    if (m_editable) {
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        setToolTip(tr("Click or drag to set the speed. Wheel or arrow keys: 1 km/h; Page Up/Down: 10."));
    }
}

void SpeedGauge::setScaleMaximum(double kmh)
{
    m_scaleMax = kmh > 0 ? kmh : 1;
    update();
}

void SpeedGauge::setValue(double kmh)
{
    if (qFuzzyCompare(kmh + 1, m_value + 1)) return;
    m_value = qMax(0.0, kmh);
    setAccessibleDescription(tr("%1 km/h").arg(m_value, 0, 'f', 1));
    update();
}

void SpeedGauge::setLimit(double kmh)
{
    m_limit = kmh;
    update();
}

void SpeedGauge::setTitle(const QString &title)
{
    if (title == m_title) return;
    m_title = title;
    update();
}

void SpeedGauge::setCaption(const QString &caption)
{
    if (caption == m_caption) return;
    m_caption = caption;
    update();
}

QSize SpeedGauge::sizeHint() const { return m_editable ? QSize(300, 300) : QSize(150, 150); }
QSize SpeedGauge::minimumSizeHint() const { return m_editable ? QSize(220, 220) : QSize(110, 110); }

QRectF SpeedGauge::dialRect() const
{
    const double side = qMin(width(), height()) - 8;
    return { (width() - side) / 2.0, (height() - side) / 2.0, side, side };
}

double SpeedGauge::valueAt(const QPointF &p) const
{
    const QRectF r = dialRect();
    const QPointF d = p - r.center();
    if (std::hypot(d.x(), d.y()) > r.width() / 2) return -1;
    const double deg = std::atan2(-d.y(), d.x()) * 180 / kPi;
    double along = kStartDeg - deg;                 // clockwise from the 0 mark
    while (along < 0) along += 360;
    while (along >= 360) along -= 360;
    if (along > kSpanDeg) along = along - kSpanDeg < (360 - kSpanDeg) / 2 ? kSpanDeg : 0;   // the gap: nearer end
    return std::round(qBound(0.0, along / kSpanDeg * m_scaleMax, m_scaleMax));
}

void SpeedGauge::edit(double kmh)
{
    if (!m_editable) return;
    const double speed = qBound(0.0, std::round(kmh), m_scaleMax);
    if (speed == m_value) return;
    setValue(speed);
    emit valueEdited(speed);
}

void SpeedGauge::mousePressEvent(QMouseEvent *e)
{
    if (!m_editable || e->button() != Qt::LeftButton) return QWidget::mousePressEvent(e);
    const double speed = valueAt(eventPos(e));
    if (speed >= 0) edit(speed);
}

void SpeedGauge::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_editable || !(e->buttons() & Qt::LeftButton)) return;
    const double speed = valueAt(eventPos(e));
    if (speed >= 0) edit(speed);
}

void SpeedGauge::wheelEvent(QWheelEvent *e)
{
    if (!m_editable) return QWidget::wheelEvent(e);
    const int steps = e->angleDelta().y() / 120;
    if (steps != 0) edit(m_value + steps);
    e->accept();
}

void SpeedGauge::keyPressEvent(QKeyEvent *e)
{
    if (!m_editable) return QWidget::keyPressEvent(e);
    switch (e->key()) {
    case Qt::Key_Up: case Qt::Key_Right: edit(m_value + 1); break;
    case Qt::Key_Down: case Qt::Key_Left: edit(m_value - 1); break;
    case Qt::Key_PageUp: edit(m_value + 10); break;
    case Qt::Key_PageDown: edit(m_value - 10); break;
    case Qt::Key_Home: edit(0); break;
    default: QWidget::keyPressEvent(e); return;
    }
}

void SpeedGauge::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF dial = dialRect();
    const QPointF centre = dial.center();
    const double radius = dial.width() / 2;
    const QColor ink = palette().color(QPalette::Text);
    const auto at = [&](double kmh, double fraction) {
        const double angle = (kStartDeg - kSpanDeg * qMin(kmh, m_scaleMax) / m_scaleMax) * kPi / 180;
        return QPointF(centre.x() + std::cos(angle) * radius * fraction, centre.y() - std::sin(angle) * radius * fraction);
    };
    const auto arcSpan = [&](double from, double to) {
        return std::make_pair(int((kStartDeg - kSpanDeg * from / m_scaleMax) * 16),
                              int(-kSpanDeg * (to - from) / m_scaleMax * 16));
    };

    painter.setPen(QPen(UiColor::frame(), 1.5));
    painter.setBrush(palette().color(QPalette::Base));
    painter.drawEllipse(dial);

    // The track, the speed along it, and the red band above the limit.
    const QRectF arc = dial.adjusted(radius * 0.12, radius * 0.12, -radius * 0.12, -radius * 0.12);
    const double width = radius * 0.06;
    painter.setPen(QPen(UiColor::grid(), width, Qt::SolidLine, Qt::FlatCap));
    painter.drawArc(arc, int(kStartDeg * 16), int(-kSpanDeg * 16));
    const bool over = m_limit >= 0 && m_value > m_limit + 0.05;
    if (m_value > 0) {
        const auto span = arcSpan(0, qMin(m_value, m_scaleMax));
        painter.setPen(QPen(over ? UiColor::error() : SimUi::mark(), width, Qt::SolidLine, Qt::FlatCap));
        painter.drawArc(arc, span.first, span.second);
    }
    if (m_limit >= 0 && m_limit < m_scaleMax) {
        const QRectF band = dial.adjusted(radius * 0.035, radius * 0.035, -radius * 0.035, -radius * 0.035);
        const auto span = arcSpan(m_limit, m_scaleMax);
        painter.setPen(QPen(UiColor::error(), radius * 0.035, Qt::SolidLine, Qt::FlatCap));
        painter.drawArc(band, span.first, span.second);
    }

    QFont small = font();
    small.setPointSizeF(qMax(6.0, radius * 0.085));
    painter.setFont(small);
    const int labelEvery = m_scaleMax > 200 ? 40 : (m_scaleMax > 60 ? 20 : 10);
    for (int kmh = 0; kmh <= int(m_scaleMax + 1e-9); kmh += 5) {
        const bool major = kmh % 10 == 0;
        painter.setPen(QPen(major ? ink : UiColor::muted(), major ? 1.5 : 1));
        painter.drawLine(at(kmh, major ? 0.70 : 0.75), at(kmh, 0.80));
        if (kmh % labelEvery == 0) {
            const QPointF text = at(kmh, 0.57);
            const QString number = QString::number(kmh);
            painter.drawText(SimUi::labelBox(small, text, number), Qt::AlignCenter, number);
        }
    }

    // The reading, the unit, the caption, the title in the gap at the bottom.
    QFont big = font();
    big.setPointSizeF(qMax(8.0, radius * 0.19));
    big.setBold(true);
    painter.setFont(big);
    painter.setPen(over ? UiColor::error() : ink);
    painter.drawText(QRectF(dial.left(), centre.y() + radius * 0.10, dial.width(), radius * 0.30), Qt::AlignCenter,
                     QString::number(m_value, 'f', m_editable ? 1 : 0));
    painter.setFont(small);
    painter.setPen(UiColor::muted());
    painter.drawText(QRectF(dial.left(), centre.y() + radius * 0.38, dial.width(), radius * 0.16), Qt::AlignCenter,
                     m_caption.isEmpty() ? QStringLiteral("km/h") : m_caption);
    QFont title = font();
    title.setBold(true);
    title.setPointSizeF(qMax(7.0, radius * 0.11));
    painter.setFont(title);
    painter.setPen(ink);
    painter.drawText(QRectF(centre.x() - radius * 0.6, centre.y() + radius * 0.60, radius * 1.2, radius * 0.28),
                     Qt::AlignCenter, m_title);

    const QColor needle = UiColor::error();
    painter.setPen(QPen(needle, qMax(2.0, radius * 0.03), Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(centre, at(m_value, 0.78));
    painter.setPen(Qt::NoPen);
    painter.setBrush(needle);
    painter.drawEllipse(centre, radius * 0.06, radius * 0.06);

    if (hasFocus()) {
        painter.setPen(QPen(SimUi::mark(), 1, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(dial.adjusted(2, 2, -2, -2));
    }
}
