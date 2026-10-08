#include "faulttimelinewindow.h"

#include "missionreport.h"
#include "statusline.h"
#include "uicolors.h"
#include "windowgeometry.h"

#include <QApplication>
#include <QDateTime>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTextBrowser>
#include <QToolTip>
#include <QVBoxLayout>

namespace {
const int kRight = 12, kTop = 6, kModeH = 22, kRowH = 18, kRowGap = 3, kAxisH = 22;
QString hms(qint64 ms) { return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss")); }
}  // namespace

// The label column fits the longest row name, within 170..260 px.
int FaultTimelineCanvas::labelW() const
{
    QFont small = font();
    small.setPointSizeF(qMax(7.5, font().pointSizeF() - 1.0));
    const QFontMetrics fm(small);
    int w = 0;
    for (const QString &r : m_t.rows) w = qMax(w, fm.horizontalAdvance(r));
    return qBound(170, w + 16, 260);
}

FaultTimelineCanvas::FaultTimelineCanvas(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("faultTimelineCanvas"));
}

void FaultTimelineCanvas::setTimeline(const FaultTimeline::Timeline &t)
{
    m_t = t;
    updateGeometry();
    update();
}

QSize FaultTimelineCanvas::sizeHint() const
{
    return QSize(900, kTop + kModeH + 8 + m_t.rows.size() * (kRowH + kRowGap) + kAxisH + 4);
}

QRect FaultTimelineCanvas::rowRect(int row) const
{
    const int w = width() - labelW() - kRight;
    if (row < 0) return QRect(labelW(), kTop, w, kModeH);
    return QRect(labelW(), kTop + kModeH + 8 + row * (kRowH + kRowGap), w, kRowH);
}

int FaultTimelineCanvas::xAtMs(qint64 ms) const
{
    const QRect r = rowRect(-1);
    if (m_t.toMs <= m_t.fromMs) return r.left();
    return r.left() + int(double(ms - m_t.fromMs) / double(m_t.toMs - m_t.fromMs) * r.width());
}

qint64 FaultTimelineCanvas::msAtX(int x) const
{
    const QRect r = rowRect(-1);
    if (r.width() <= 0) return m_t.fromMs;
    return m_t.fromMs + qint64(double(qBound(r.left(), x, r.right()) - r.left()) / r.width() * double(m_t.toMs - m_t.fromMs));
}

QString FaultTimelineCanvas::describeAt(const QPoint &pos) const
{
    if (pos.x() < labelW() || m_t.toMs <= m_t.fromMs) return QString();
    const qint64 ms = msAtX(pos.x());
    const qint64 slack = (m_t.toMs - m_t.fromMs) * 3 / qMax(1, rowRect(-1).width());
    if (rowRect(-1).adjusted(0, -2, 0, 2).contains(pos)) {
        for (qint64 f : m_t.failures)
            if (qAbs(f - ms) <= slack) return tr("System_Failure at %1").arg(hms(f));
        for (const FaultTimeline::ModeSpan &m : m_t.modes)
            if (ms >= m.fromMs && ms <= m.toMs) return tr("%1, %2 to %3").arg(m.mode, hms(m.fromMs), hms(m.toMs));
        return QString();
    }
    for (int r = 0; r < m_t.rows.size(); ++r) {
        if (!rowRect(r).adjusted(0, -1, 0, 1).contains(pos)) continue;
        QStringList lines;
        for (const FaultTimeline::Bar &b : m_t.bars)
            if (b.row == m_t.rows.at(r) && ms >= b.fromMs - slack && ms <= b.toMs + slack)
                lines << tr("%1: %2 (%3), %4 to %5").arg(b.row, b.fault, b.source, hms(b.fromMs),
                                                       b.open ? tr("the end of the log") : hms(b.toMs));
        return lines.join(QLatin1Char('\n'));
    }
    return QString();
}

void FaultTimelineCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Base));
    const QColor ink = palette().color(QPalette::Text);
    QFont small = font();
    small.setPointSizeF(qMax(7.5, font().pointSizeF() - 1.0));
    p.setFont(small);
    const QFontMetrics fm(small);
    if (m_t.toMs <= m_t.fromMs) {
        p.setPen(UiColor::muted());
        p.drawText(rect(), Qt::AlignCenter, tr("No traffic in this tab."));
        return;
    }
    auto colourFor = [](const QString &s) { return UiColor::series(int(qHash(s) % uint(qMax(1, UiColor::seriesCount())))); };

    // Mode band.
    const QRect band = rowRect(-1);
    p.setPen(UiColor::muted());
    p.drawText(QRect(4, band.top(), labelW() - 10, band.height()), Qt::AlignRight | Qt::AlignVCenter, tr("MODE"));
    for (const FaultTimeline::ModeSpan &m : m_t.modes) {
        const int x1 = xAtMs(m.fromMs), x2 = qMax(xAtMs(m.toMs), x1 + 1);
        const QRect r(x1, band.top(), x2 - x1, band.height());
        const QColor c = colourFor(m.mode);
        p.fillRect(r, c);
        const QString name = Missions::modeName(m.mode);
        if (fm.horizontalAdvance(name) + 6 < r.width()) {
            p.setPen(UiColor::withContrast(ink, c, 4.5));
            p.drawText(r, Qt::AlignCenter, name);
        }
    }
    // Rows.
    for (int i = 0; i < m_t.rows.size(); ++i) {
        const QRect r = rowRect(i);
        p.setPen(ink);
        p.drawText(QRect(4, r.top(), labelW() - 10, r.height()), Qt::AlignRight | Qt::AlignVCenter,
                   fm.elidedText(m_t.rows.at(i), Qt::ElideRight, labelW() - 10));
        QColor track = UiColor::muted();
        track.setAlpha(30);
        p.fillRect(r, track);
    }
    for (const FaultTimeline::Bar &b : m_t.bars) {
        const int row = m_t.rows.indexOf(b.row);
        if (row < 0) continue;
        const QRect r = rowRect(row);
        const int x1 = xAtMs(b.fromMs), x2 = qMax(xAtMs(b.toMs), x1 + 2);
        p.fillRect(QRect(x1, r.top() + 2, x2 - x1, r.height() - 4), b.source == QLatin1String("CCSYS") ? UiColor::warning() : UiColor::error());
    }
    // System_Failure lines across everything.
    const int bottom = rowRect(m_t.rows.size() - 1).bottom();
    p.setPen(QPen(ink, 1, Qt::DashLine));
    for (qint64 f : m_t.failures) {
        const int x = xAtMs(f);
        p.drawLine(x, band.top(), x, qMax(bottom, band.bottom()));
    }
    // Time axis.
    p.setPen(UiColor::muted());
    const int axisY = qMax(bottom, band.bottom()) + 4;
    for (int k = 0; k <= 4; ++k) {
        const qint64 ms = m_t.fromMs + (m_t.toMs - m_t.fromMs) * k / 4;
        const int left = qBound(0, xAtMs(ms) - 40, width() - 80);
        p.drawText(QRect(left, axisY, 80, 16), Qt::AlignCenter, hms(ms));
    }
}

void FaultTimelineCanvas::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && e->pos().x() >= labelW() && m_t.toMs > m_t.fromMs) emit timeClicked(msAtX(e->pos().x()));
}

bool FaultTimelineCanvas::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        auto *he = static_cast<QHelpEvent *>(e);
        const QString t = describeAt(he->pos());
        if (t.isEmpty()) QToolTip::hideText(); else QToolTip::showText(he->globalPos(), t, this);
        return true;
    }
    return QWidget::event(e);
}

// =============================================================================

FaultTimelineWindow::FaultTimelineWindow(LogModel *model, const QString &tabKey, const QString &tabName, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_model(model)
    , m_tabKey(tabKey)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Fault timeline — %1").arg(tabName.isEmpty() ? tabKey : tabName));
    WindowGeometry::makeResizableWindow(this);
    resize(1000, 640);

    m_canvas = new FaultTimelineCanvas;
    auto *scroll = new QScrollArea;
    scroll->setWidget(m_canvas);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_failures = new QTextBrowser;
    m_failures->setObjectName(QStringLiteral("faultTimelineFailures"));
    auto *split = new QSplitter(Qt::Vertical, this);
    split->addWidget(scroll);
    split->addWidget(m_failures);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    m_status = new StatusLine;
    auto *rebuildBtn = new QPushButton(tr("Rebuild"), this);
    auto *save = new QPushButton(tr("Save image…"), this);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_status, 1);
    buttons->addWidget(rebuildBtn);
    buttons->addWidget(save);
    auto *root = new QVBoxLayout(this);
    root->addWidget(split, 1);
    root->addLayout(buttons);
    connect(rebuildBtn, &QPushButton::clicked, this, &FaultTimelineWindow::rebuild);
    connect(save, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, tr("Save fault timeline"),
                                                          QStringLiteral("fault_timeline_%1.png").arg(m_tabKey), tr("PNG (*.png)"));
        if (path.isEmpty()) return;
        if (m_canvas->grab().save(path, "PNG")) m_status->ok(tr("Saved %1").arg(path));
        else m_status->fail(tr("Could not write %1").arg(path));
    });
    connect(m_canvas, &FaultTimelineCanvas::timeClicked, this, [this](qint64 ms) { emit jumpRequested(m_tabKey, ms); });
    rebuild();
}

QString FaultTimelineWindow::failuresText() const { return m_failures->toPlainText(); }

void FaultTimelineWindow::rebuild()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_t = FaultTimeline::build(m_model);
    QApplication::restoreOverrideCursor();
    m_canvas->setTimeline(m_t);
    m_failures->setHtml(FaultTimeline::failuresHtml(m_t));
    m_status->state(tr("%1 fault bar(s) on %2 row(s) · %3 System_Failure(s)")
                        .arg(m_t.bars.size()).arg(m_t.rows.size()).arg(m_t.failures.size()));
}
