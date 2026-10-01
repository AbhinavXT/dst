#include "trackdiagramwindow.h"

#include "dmipanel.h"
#include "logmodel.h"
#include "statusline.h"
#include "uicolors.h"
#include "windowgeometry.h"

#include <QApplication>
#include <QDateTime>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

namespace {

// The lamp an aspect lights (dmiLitLamps, dmipanel.h): 3 = red, 1 = green,
// 0/2 = yellow, none = unidentified.
QColor aspectColor(int aspect)
{
    const QVector<int> lamps = dmiLitLamps(aspect);
    if (lamps.contains(3)) return UiColor::signalLamp(QStringLiteral("red"));
    if (lamps.contains(1)) return UiColor::signalLamp(QStringLiteral("green"));
    if (lamps.contains(0) || lamps.contains(2)) return UiColor::signalLamp(QStringLiteral("yellow"));
    return UiColor::muted();
}

QColor eventColor(const QString &kind)
{
    if (kind == QLatin1String("emergency") || kind == QLatin1String("fault")
        || kind == QLatin1String("head-on") || kind == QLatin1String("rear-end")) return UiColor::error();
    if (kind == QLatin1String("overspeed") || kind == QLatin1String("sos")) return UiColor::warning();
    return UiColor::accent();   // "mode"
}

QColor faded(QColor c) { c.setAlpha(100); return c; }

}  // namespace

// =============================================================================
//  TrackDiagramCanvas
// =============================================================================

TrackDiagramCanvas::TrackDiagramCanvas(QWidget *parent)
    : QWidget(parent)
{
    UiColor::onThemeChange(this, [this]() { update(); });
}

void TrackDiagramCanvas::setDiagram(const TrackDiagram::Diagram &diagram)
{
    m_d = diagram;
    m_cursorIndex = m_d.trace.samples.isEmpty() ? -1 : m_d.trace.samples.size() - 1;
    update();
}

void TrackDiagramCanvas::setCursorIndex(int index)
{
    index = qBound(-1, index, m_d.trace.samples.size() - 1);
    if (index == m_cursorIndex) return;
    m_cursorIndex = index;
    update();
    emit cursorIndexChanged(index);
}

QRect TrackDiagramCanvas::trackRect() const
{
    return QRect(50, 40, qMax(1, width() - 100), qMax(1, height() - 140));
}

double TrackDiagramCanvas::xOf(double locM) const
{
    const QRect t = trackRect();
    const double span = m_d.maxLocM - m_d.minLocM;
    if (span <= 0.0) return t.left();
    return t.left() + (locM - m_d.minLocM) / span * t.width();
}

qint64 TrackDiagramCanvas::cursorMsOrLast() const
{
    if (m_d.trace.samples.isEmpty()) return 0;
    if (m_cursorIndex >= 0 && m_cursorIndex < m_d.trace.samples.size()) return m_d.trace.samples.at(m_cursorIndex).epochMs;
    return m_d.trace.samples.last().epochMs;
}

void TrackDiagramCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), palette().base());
    const QColor text = palette().color(QPalette::Text);
    const QColor ink = UiColor::muted(), frame = UiColor::frame(), grid = UiColor::grid();

    if (m_d.isEmpty()) {
        p.setPen(ink);
        p.drawText(rect(), Qt::AlignCenter, tr("No location in this tab: it needs @dmi or @lsrp frames"));
        return;
    }

    const QRect track = trackRect();
    const int midY = track.center().y();
    const qint64 cursorMs = cursorMsOrLast();

    QFont small = p.font();
    small.setPointSizeF(qMax(6.0, small.pointSizeF() - 1.0));
    p.setFont(small);

    // ---- the rail --------------------------------------------------------------------
    p.setPen(QPen(frame, 2));
    p.drawLine(track.left(), midY, track.right(), midY);
    p.setPen(QPen(grid, 1));
    for (int x = track.left(); x <= track.right(); x += 14) p.drawLine(x, midY - 6, x, midY + 6);

    // ---- RFID tags ---------------------------------------------------------------------
    for (const TrackDiagram::RfidMark &t : m_d.tags) {
        const double x = xOf(t.locM);
        const bool past = t.epochMs <= cursorMs;
        const QColor c = past ? UiColor::series(0) : faded(ink);
        p.setPen(c);
        p.setBrush(c);
        const double y = midY - 20;
        QPolygonF diamond;
        diamond << QPointF(x, y - 5) << QPointF(x + 5, y) << QPointF(x, y + 5) << QPointF(x - 5, y);
        p.drawPolygon(diamond);
        p.setPen(past ? text : ink);
        p.drawText(QRectF(x - 30, y - 20, 60, 14), Qt::AlignCenter, QString::number(t.uniqueId));
    }

    // ---- signals and their MA end ------------------------------------------------------
    for (const TrackDiagram::SignalMark &s : m_d.signalMarks) {
        const double x = xOf(s.locM);
        const bool past = s.epochMs <= cursorMs;
        const QColor c = past ? aspectColor(s.aspect) : faded(aspectColor(s.aspect));
        p.setPen(QPen(c, 3));
        p.drawLine(QPointF(x, midY + 14), QPointF(x, midY + 44));
        p.setBrush(c);
        p.drawEllipse(QPointF(x, midY + 14), 5, 5);
        p.setPen(past ? text : ink);
        p.drawText(QRectF(x - 60, midY + 46, 120, 14), Qt::AlignCenter, s.name);
        if (s.hasMa) {
            const double xma = xOf(s.maEndLocM);
            const QColor mc = past ? UiColor::accent() : faded(UiColor::accent());
            p.setPen(QPen(mc, 1, Qt::DashLine));
            p.drawLine(QPointF(x, midY + 29), QPointF(xma, midY + 29));
            p.setBrush(mc);
            QPolygonF flag;
            flag << QPointF(xma, midY + 22) << QPointF(xma + 8, midY + 26) << QPointF(xma, midY + 30);
            p.drawPolygon(flag);
        }
    }

    // ---- events ----------------------------------------------------------------------------
    for (const TrackDiagram::EventMark &e : m_d.events) {
        const double x = xOf(e.locM);
        const bool past = e.epochMs <= cursorMs;
        const QColor c = past ? eventColor(e.kind) : faded(eventColor(e.kind));
        p.setPen(QPen(c, 2));
        p.drawLine(QPointF(x, midY - 44), QPointF(x, midY - 16));
    }

    // ---- the loco, at the cursor -------------------------------------------------------------
    {
        const bool have = m_cursorIndex >= 0 && m_cursorIndex < m_d.trace.samples.size();
        const SpeedDistance::Sample &smp = have ? m_d.trace.samples.at(m_cursorIndex) : m_d.trace.samples.last();
        const double x = xOf(smp.locM);
        const QColor lc = UiColor::accent();
        p.setPen(QPen(lc, 2));
        p.setBrush(lc);
        p.drawRect(QRectF(x - 10, midY - 10, 20, 20));
        if (m_d.trace.direction != 0) {
            const double nx = x + (m_d.trace.direction > 0 ? 14 : -14);
            p.drawLine(QPointF(x + (m_d.trace.direction > 0 ? 10 : -10), midY), QPointF(nx, midY));
        }
    }

    // ---- axis + time readout ---------------------------------------------------------------------
    p.setPen(ink);
    p.drawText(QRect(track.left(), track.bottom() + 40, 160, 16), Qt::AlignLeft,
              QString::number(m_d.minLocM, 'f', 0) + QStringLiteral(" m"));
    p.drawText(QRect(track.right() - 160, track.bottom() + 40, 160, 16), Qt::AlignRight,
              QString::number(m_d.maxLocM, 'f', 0) + QStringLiteral(" m"));
    p.setPen(text);
    p.drawText(QRect(0, 4, width(), 16), Qt::AlignCenter,
              QDateTime::fromMSecsSinceEpoch(cursorMs).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
}

// =============================================================================
//  TrackDiagramWindow
// =============================================================================

TrackDiagramWindow::TrackDiagramWindow(LogModel *model, const QString &tabKey, const QString &tabName, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_model(model)
    , m_tabKey(tabKey)
    , m_tabName(tabName)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Track diagram — %1").arg(tabName.isEmpty() ? tabKey : tabName));
    WindowGeometry::makeResizableWindow(this);
    resize(1040, 520);

    m_canvas = new TrackDiagramCanvas(this);
    m_slider = new QSlider(Qt::Horizontal, this);
    m_play = new QPushButton(tr("Play"), this);
    m_play->setCheckable(true);
    m_timer = new QTimer(this);
    m_timer->setInterval(150);
    m_status = new StatusLine;

    auto *controls = new QHBoxLayout;
    controls->addWidget(m_play);
    controls->addWidget(m_slider, 1);

    auto *saveImg = new QPushButton(tr("Save image…"), this);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_status, 1);
    buttons->addWidget(saveImg);

    auto *root = new QVBoxLayout(this);
    root->addWidget(m_canvas, 1);
    root->addLayout(controls);
    root->addLayout(buttons);

    connect(m_slider, &QSlider::valueChanged, this, &TrackDiagramWindow::onSliderMoved);
    connect(m_play, &QPushButton::toggled, this, &TrackDiagramWindow::onPlayToggled);
    connect(m_timer, &QTimer::timeout, this, &TrackDiagramWindow::onTick);
    connect(saveImg, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, tr("Save track diagram"),
            QStringLiteral("track_diagram_%1.png").arg(m_tabKey), tr("PNG (*.png)"));
        if (path.isEmpty()) return;
        if (saveImage(path)) m_status->ok(tr("Saved %1").arg(path)); else m_status->fail(tr("Could not write %1").arg(path));
    });

    rebuild();
}

void TrackDiagramWindow::rebuild()
{
    m_diagram = TrackDiagram::build(m_model, m_tabKey, m_tabName);
    m_canvas->setDiagram(m_diagram);
    const int last = m_diagram.trace.samples.size() - 1;
    m_slider->blockSignals(true);
    m_slider->setRange(0, qMax(0, last));
    m_slider->setValue(qMax(0, last));
    m_slider->blockSignals(false);
    m_slider->setEnabled(last > 0);
    m_play->setEnabled(last > 0);
    if (m_diagram.isEmpty()) m_status->state(tr("No location in this tab"));
    else m_status->state(tr("%1 tag(s), %2 signal(s), %3 event(s)")
                              .arg(m_diagram.tags.size()).arg(m_diagram.signalMarks.size()).arg(m_diagram.events.size()));
}

void TrackDiagramWindow::onSliderMoved(int value)
{
    m_canvas->setCursorIndex(value);
}

void TrackDiagramWindow::onPlayToggled(bool on)
{
    m_play->setText(on ? tr("Pause") : tr("Play"));
    if (on) m_timer->start(); else m_timer->stop();
}

void TrackDiagramWindow::onTick()
{
    if (m_slider->value() >= m_slider->maximum()) {
        m_play->setChecked(false);
        return;
    }
    m_slider->setValue(m_slider->value() + 1);
}

bool TrackDiagramWindow::saveImage(const QString &path) const
{
    return m_canvas->grab().save(path, "PNG");
}
