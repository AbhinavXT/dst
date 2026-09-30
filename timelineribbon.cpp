#include "timelineribbon.h"

#include "logmodel.h"

#include <QDateTime>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QToolTip>

#include <cmath>

// ============================== binning ====================================

int TimelineBins::binForTime(qint64 ms) const
{
    if (bins.isEmpty()) return -1;
    if (ms < minMs || ms > maxMs) return -1;

    const qint64 span = maxMs - minMs;
    if (span <= 0) return 0;      // every entry shares one timestamp

    // Clamped rather than trusted: floating-point rounding at the exact
    // upper bound can produce bins.size(), which would index off the end.
    const double f = double(ms - minMs) / double(span);
    int idx = int(f * bins.size());
    if (idx < 0)            idx = 0;
    if (idx >= bins.size()) idx = bins.size() - 1;
    return idx;
}

qint64 TimelineBins::timeForBin(int index) const
{
    if (bins.isEmpty()) return minMs;
    if (index < 0)            index = 0;
    if (index >= bins.size()) index = bins.size() - 1;

    const qint64 span = maxMs - minMs;
    if (span <= 0) return minMs;

    // Midpoint of the bucket: clicking a bar should land inside the slice
    // it represents, not on its leading edge.
    const double f = (double(index) + 0.5) / double(bins.size());
    return minMs + qint64(f * double(span));
}

TimelineBins binLogModel(const LogModel *model, int binCount)
{
    TimelineBins out;
    if (!model || binCount <= 0) return out;

    const int n = model->count();
    if (n <= 0) return out;

    // Entries are appended in arrival order and arrival order is time
    // order, so the span is the first and last rows — no scan needed.
    // Guard anyway: a reloaded session or a clock step could break that,
    // and a reversed span would make every bin index negative.
    LogEntryPtr first = model->entryAt(0);
    LogEntryPtr last  = model->entryAt(n - 1);
    if (!first || !last) return out;

    out.minMs = qMin(first->epochMs, last->epochMs);
    out.maxMs = qMax(first->epochMs, last->epochMs);
    out.bins.resize(binCount);

    for (int i = 0; i < n; ++i) {
        LogEntryPtr e = model->entryAt(i);
        if (!e) continue;

        int idx = out.binForTime(e->epochMs);
        if (idx < 0) {
            // Outside the [first,last] span — only possible if ordering
            // was violated. Attribute it to the nearest edge rather than
            // dropping it; a message that exists should be visible.
            idx = (e->epochMs < out.minMs) ? 0 : binCount - 1;
        }

        TimelineBins::Bin &b = out.bins[idx];
        ++b.total;
        if (e->severity == Severity::Error)     ++b.error;
        else if (e->severity == Severity::Warn) ++b.warn;
        if (e->bookmarked)                      ++b.bookmarks;

        ++out.entries;
        if (b.total > out.maxTotal) out.maxTotal = b.total;
    }
    return out;
}

// ============================== widget =====================================

TimelineRibbon::TimelineRibbon(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(kHeight);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setToolTip(tr("Message density over time. Click to seek, "
                  "drag to filter a time range."));

    // 400ms: fast enough to feel live, slow enough that a burst of traffic
    // does not turn into a rebuild per batch.
    m_timer = new QTimer(this);
    m_timer->setInterval(400);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        if (!m_dirty) return;
        rebuild();
        update();
    });
    m_timer->start();
}

void TimelineRibbon::setModel(LogModel *model)
{
    m_model = model;
    m_dirty = true;
    rebuild();
    update();
}

void TimelineRibbon::refreshLater()
{
    m_dirty = true;
}

void TimelineRibbon::setViewportRange(qint64 fromMs, qint64 toMs)
{
    if (m_viewFrom == fromMs && m_viewTo == toMs) return;
    m_viewFrom = fromMs;
    m_viewTo   = toMs;
    update();
}

void TimelineRibbon::rebuild()
{
    // One bucket per pixel. Finer cannot be drawn, and the bin walk is the
    // expensive part.
    m_bins  = binLogModel(m_model, qMax(1, width()));
    m_dirty = false;
}

void TimelineRibbon::resizeEvent(QResizeEvent *)
{
    m_dirty = true;
    rebuild();
}

QSize TimelineRibbon::sizeHint() const
{
    return QSize(200, kHeight);
}

qint64 TimelineRibbon::timeAtX(int x) const
{
    if (m_bins.isEmpty() || width() <= 0) return 0;
    const double f = double(qBound(0, x, width() - 1)) / double(width());
    return m_bins.minMs + qint64(f * double(m_bins.maxMs - m_bins.minMs));
}

void TimelineRibbon::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().base());

    if (m_bins.isEmpty()) {
        p.setPen(UiColor::muted());
        p.drawText(rect(), Qt::AlignCenter, tr("No messages yet"));
        return;
    }

    const int h = height();
    const int w = width();
    const int barTop = 2;
    const int barBot = h - 14;            // leave a strip for time labels
    const int barH   = qMax(1, barBot - barTop);

    // Viewport shading first, so bars and ticks draw over it.
    if (m_viewTo > m_viewFrom) {
        const int x0 = m_bins.binForTime(m_viewFrom);
        const int x1 = m_bins.binForTime(m_viewTo);
        if (x0 >= 0 && x1 >= 0) {
            QColor hl = palette().highlight().color();
            hl.setAlpha(40);
            p.fillRect(QRect(x0, barTop, qMax(1, x1 - x0), barH), hl);
        }
    }

    // Density is a structural fill; the three mark colours are the same
    // meanings the minimap and the log rows use, so they come from the same
    // place rather than from three literals that were tuned for the dark
    // theme and used in both.
    const QColor density = UiColor::frame();
    const QColor warnCol = UiColor::warning();
    const QColor errCol  = UiColor::error();
    const QColor bmCol   = UiColor::accent();

    // Log scale: raw counts across a session span orders of magnitude, and
    // a linear scale flattens every quiet period into an invisible line
    // against one busy burst.
    const double denom = std::log1p(double(m_bins.maxTotal));

    for (int i = 0; i < m_bins.bins.size() && i < w; ++i) {
        const TimelineBins::Bin &b = m_bins.bins.at(i);
        if (b.total == 0) continue;

        const double f = denom > 0.0 ? std::log1p(double(b.total)) / denom : 1.0;
        const int barPx = qMax(1, int(f * barH));
        p.setPen(density);
        p.drawLine(i, barBot, i, barBot - barPx);

        // Full-height severity ticks. Deliberately not proportional — see
        // the header. Errors last so they win where both are present.
        if (b.warn > 0) {
            p.setPen(warnCol);
            p.drawLine(i, barTop, i, barBot);
        }
        if (b.error > 0) {
            p.setPen(errCol);
            p.drawLine(i, barTop, i, barBot);
        }
        if (b.bookmarks > 0) {
            p.setPen(bmCol);
            p.drawLine(i, barTop, i, barTop + 4);
        }
    }

    // Drag selection.
    if (m_dragStartX >= 0 && m_dragCurX >= 0) {
        const int x0 = qMin(m_dragStartX, m_dragCurX);
        const int x1 = qMax(m_dragStartX, m_dragCurX);
        QColor sel = palette().highlight().color();
        sel.setAlpha(70);
        p.fillRect(QRect(x0, barTop, qMax(1, x1 - x0), barH), sel);
        p.setPen(palette().highlight().color());
        p.drawRect(QRect(x0, barTop, qMax(1, x1 - x0), barH - 1));
    }

    // End labels, so the strip is readable without hovering.
    p.setPen(UiColor::muted());
    const QFont f = p.font();
    QFont small = f;
    small.setPointSizeF(qMax(6.0, f.pointSizeF() - 2.0));
    p.setFont(small);
    p.drawText(2, h - 2,
               QDateTime::fromMSecsSinceEpoch(m_bins.minMs).toString("HH:mm:ss"));
    const QString endLabel =
        QDateTime::fromMSecsSinceEpoch(m_bins.maxMs).toString("HH:mm:ss");
    p.drawText(w - p.fontMetrics().horizontalAdvance(endLabel) - 2, h - 2, endLabel);
}

namespace {
// globalPos() is deprecated in Qt 6, globalPosition() absent in Qt 5.
inline QPoint eventGlobalPos(const QMouseEvent *ev)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return ev->globalPosition().toPoint();
#else
    return ev->globalPos();
#endif
}
}  // namespace

void TimelineRibbon::mousePressEvent(QMouseEvent *ev)
{
    if (ev->button() != Qt::LeftButton || m_bins.isEmpty()) return;
    m_dragStartX = ev->pos().x();
    m_dragCurX   = m_dragStartX;
    update();
}

void TimelineRibbon::mouseMoveEvent(QMouseEvent *ev)
{
    const int x = ev->pos().x();
    m_hoverX = x;

    if (m_dragStartX >= 0) {
        m_dragCurX = x;
        update();
        return;
    }

    if (m_bins.isEmpty()) return;
    const int idx = qBound(0, x, m_bins.bins.size() - 1);
    const TimelineBins::Bin &b = m_bins.bins.at(idx);
    const QString when =
        QDateTime::fromMSecsSinceEpoch(m_bins.timeForBin(idx))
            .toString("yyyy-MM-dd HH:mm:ss.zzz");

    QString tip = tr("%1\n%2 message(s)").arg(when).arg(b.total);
    if (b.error > 0)     tip += tr("\n%1 error(s)").arg(b.error);
    if (b.warn > 0)      tip += tr("\n%1 warning(s)").arg(b.warn);
    if (b.bookmarks > 0) tip += tr("\n%1 bookmark(s)").arg(b.bookmarks);
    QToolTip::showText(eventGlobalPos(ev), tip, this);
}

void TimelineRibbon::mouseReleaseEvent(QMouseEvent *ev)
{
    if (ev->button() != Qt::LeftButton || m_dragStartX < 0) return;

    const int x0 = m_dragStartX;
    const int x1 = ev->pos().x();
    m_dragStartX = -1;
    m_dragCurX   = -1;
    update();

    if (m_bins.isEmpty()) return;

    // A short drag is a click. Without this, the tiny cursor movement that
    // happens during an ordinary click would produce a millisecond-wide
    // range filter and appear to blank the table.
    if (qAbs(x1 - x0) < kMinDragPx) {
        emit timeClicked(timeAtX(x1));
        return;
    }
    emit rangeSelected(timeAtX(qMin(x0, x1)), timeAtX(qMax(x0, x1)));
}

void TimelineRibbon::leaveEvent(QEvent *)
{
    m_hoverX = -1;
    QToolTip::hideText();
}
