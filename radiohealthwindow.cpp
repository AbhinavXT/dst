#include "radiohealthwindow.h"

#include "statusline.h"
#include "uicolors.h"
#include "windowgeometry.h"

#include <QApplication>
#include <QDateTime>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <cmath>

namespace {

const int kLeft = 112, kRight = 12, kTop = 8, kBottom = 26, kGap = 10;

// Strip heights, as shares of the plot height.
const double kShare[5] = { 0.26, 0.10, 0.24, 0.16, 0.24 };
const char *kTitle[5] = { "DMI signal", "Radio not OK", "Temperature", "Fwd power", "GSM RSSI" };

QString hms(qint64 ms) { return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss")); }

// The last value at or before `ms` in a series ("" if none).
QString valueAt(const RadioHealth::Series &s, qint64 ms)
{
    int lo = 0, hi = s.ms.size();
    while (lo < hi) { const int mid = (lo + hi) / 2; if (s.ms.at(mid) <= ms) lo = mid + 1; else hi = mid; }
    if (lo == 0) return QString();
    const double v = s.v.at(lo - 1);
    return std::isnan(v) ? QStringLiteral("not known") : QStringLiteral("%1 %2").arg(v, 0, 'g', 4).arg(s.unit);
}

}  // namespace

RadioHealthCanvas::RadioHealthCanvas(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setObjectName(QStringLiteral("radioHealthCanvas"));
}

void RadioHealthCanvas::setReport(const RadioHealth::Report &r)
{
    m_r = r;
    update();
}

QVector<QRect> RadioHealthCanvas::strips() const
{
    QVector<QRect> out;
    const int h = height() - kTop - kBottom - 4 * kGap;
    int y = kTop;
    for (double share : kShare) {
        const int sh = qMax(16, int(h * share));
        out << QRect(kLeft, y, width() - kLeft - kRight, sh);
        y += sh + kGap;
    }
    return out;
}

int RadioHealthCanvas::xAtMs(qint64 ms) const
{
    const QRect r = strips().first();
    if (m_r.toMs <= m_r.fromMs) return r.left();
    return r.left() + int(double(ms - m_r.fromMs) / double(m_r.toMs - m_r.fromMs) * r.width());
}

qint64 RadioHealthCanvas::msAtX(int x) const
{
    const QRect r = strips().first();
    if (r.width() <= 0) return m_r.fromMs;
    const double t = double(qBound(r.left(), x, r.right()) - r.left()) / double(r.width());
    return m_r.fromMs + qint64(t * double(m_r.toMs - m_r.fromMs));
}

QString RadioHealthCanvas::describeAt(qint64 ms) const
{
    QStringList lines{ hms(ms) };
    const QString bars = valueAt(m_r.signal, ms);
    if (!bars.isEmpty()) lines << tr("DMI signal: %1").arg(bars);
    for (const RadioHealth::Span &s : m_r.noRadio)
        if (ms >= s.fromMs && ms <= s.toMs)
            lines << tr("No radio %1 to %2%3").arg(hms(s.fromMs), hms(s.toMs),
                                                   s.announced ? tr(", after a radio-hole announcement") : tr(", no radio hole announced"));
    for (const RadioHealth::Span &s : m_r.radioHole)
        if (ms >= s.fromMs && ms <= s.toMs) lines << tr("Approaching Radio Hole (DMI) %1 to %2").arg(hms(s.fromMs), hms(s.toMs));
    for (const RadioHealth::Span &s : m_r.radioFail)
        if (ms >= s.fromMs && ms <= s.toMs) lines << tr("%1, %2 to %3").arg(s.what, hms(s.fromMs), hms(s.toMs));
    auto series = [&](const QVector<RadioHealth::Series> &v, const QString &head) {
        QStringList parts;
        for (const RadioHealth::Series &s : v) {
            const QString t = valueAt(s, ms);
            if (!t.isEmpty()) parts << QStringLiteral("%1 %2").arg(s.name, t);
        }
        if (!parts.isEmpty()) lines << head + QStringLiteral(": ") + parts.join(QStringLiteral(", "));
    };
    series(m_r.temperatures, tr("Temperature"));
    series(m_r.power, tr("Forward power"));
    series(m_r.gsm, tr("GSM"));
    return lines.join(QLatin1Char('\n'));
}

void RadioHealthCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPalette pal = palette();
    p.fillRect(rect(), pal.color(QPalette::Base));
    const QColor ink = pal.color(QPalette::Text);
    const QVector<QRect> st = strips();
    QFont small = font();
    small.setPointSizeF(qMax(7.5, font().pointSizeF() - 1.0));
    p.setFont(small);

    for (int i = 0; i < st.size(); ++i) {
        p.setPen(UiColor::muted());
        p.drawText(QRect(4, st[i].top(), kLeft - 10, st[i].height() / 2), Qt::AlignRight | Qt::AlignBottom | Qt::TextWordWrap,
                   tr(kTitle[i]));
        p.setPen(QPen(UiColor::muted(), 1, Qt::DotLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(st[i]);
    }
    if (!m_r.any() || m_r.toMs <= m_r.fromMs) {
        p.setPen(UiColor::muted());
        p.drawText(rect(), Qt::AlignCenter, tr("Nothing to draw: this tab has no @dmi, @ccsys or @dlsys."));
        return;
    }
    auto spanRect = [&](const QRect &strip, qint64 a, qint64 b) {
        const int x1 = xAtMs(a), x2 = qMax(xAtMs(b), x1 + 2);
        return QRect(x1, strip.top(), x2 - x1, strip.height());
    };
    // A line per series, on its own scale (lo..hi over the series' known values).
    auto drawSeries = [&](const QRect &strip, const QVector<RadioHealth::Series> &v, double lo, double hi) {
        for (int k = 0; k < v.size(); ++k) {
            const RadioHealth::Series &s = v.at(k);
            QPainterPath path;
            bool pen = false;
            for (int j = 0; j < s.ms.size(); ++j) {
                if (std::isnan(s.v.at(j))) { pen = false; continue; }
                const double t = hi > lo ? (s.v.at(j) - lo) / (hi - lo) : 0.5;
                const QPointF pt(xAtMs(s.ms.at(j)), strip.bottom() - 2 - t * (strip.height() - 4));
                if (!pen) path.moveTo(pt); else path.lineTo(pt);
                pen = true;
            }
            p.setPen(QPen(UiColor::series(k), 1.5));
            p.drawPath(path);
        }
        // The scale under the strip's title; the legend inside the strip.
        p.setPen(UiColor::muted());
        p.drawText(QRect(4, strip.top() + strip.height() / 2 + 2, kLeft - 10, 16), Qt::AlignRight | Qt::AlignTop,
                   QStringLiteral("%1–%2 %3").arg(lo, 0, 'g', 4).arg(hi, 0, 'g', 4).arg(v.isEmpty() ? QString() : v.first().unit));
        int lx = strip.left() + 6;
        for (int k = 0; k < v.size(); ++k) {
            p.setPen(UiColor::series(k));
            const QString name = v.at(k).name;
            p.drawText(QPoint(lx, strip.top() + 13), name);
            lx += p.fontMetrics().horizontalAdvance(name) + 12;
        }
    };
    auto rangeOf = [](const QVector<RadioHealth::Series> &v, double *lo, double *hi) {
        bool any = false;
        for (const RadioHealth::Series &s : v)
            for (double x : s.v) {
                if (std::isnan(x)) continue;
                if (!any) { *lo = *hi = x; any = true; } else { *lo = qMin(*lo, x); *hi = qMax(*hi, x); }
            }
        return any;
    };

    // 0: signal bars, no-radio spells behind them, radio-hole announcements above.
    {
        const QRect s = st[0];
        QColor none = UiColor::error();
        none.setAlpha(50);
        for (const RadioHealth::Span &sp : m_r.noRadio) p.fillRect(spanRect(s, sp.fromMs, sp.toMs), none);
        QColor hole = UiColor::accent();
        hole.setAlpha(70);
        for (const RadioHealth::Span &sp : m_r.radioHole) {
            const QRect r = spanRect(s, sp.fromMs, sp.toMs);
            p.fillRect(QRect(r.left(), s.top(), r.width(), 6), hole);
        }
        QPainterPath path;
        for (int j = 0; j < m_r.signal.ms.size(); ++j) {
            const QPointF pt(xAtMs(m_r.signal.ms.at(j)), s.bottom() - 2 - m_r.signal.v.at(j) / 5.0 * (s.height() - 10));
            if (j == 0) path.moveTo(pt); else path.lineTo(pt);
        }
        p.setPen(QPen(ink, 1.5));
        p.drawPath(path);
        p.setPen(UiColor::muted());
        p.drawText(QRect(4, s.top() + s.height() / 2 + 2, kLeft - 10, 16), Qt::AlignRight | Qt::AlignTop, tr("0–5 bars"));
        int lx = s.left() + 6;
        auto legend = [&](const QColor &c, const QString &t) {
            p.fillRect(QRect(lx, s.top() + 6, 10, 8), c);
            p.setPen(ink);
            p.drawText(QPoint(lx + 14, s.top() + 14), t);
            lx += 14 + p.fontMetrics().horizontalAdvance(t) + 14;
        };
        legend(none, tr("no radio"));
        legend(hole, tr("radio hole announced"));
    }
    // 1: radio not OK.
    for (const RadioHealth::Span &sp : m_r.radioFail) p.fillRect(spanRect(st[1], sp.fromMs, sp.toMs), UiColor::error());
    // 2-4: series.
    double lo = 0, hi = 0;
    if (rangeOf(m_r.temperatures, &lo, &hi)) drawSeries(st[2], m_r.temperatures, lo, hi);
    if (rangeOf(m_r.power, &lo, &hi)) drawSeries(st[3], m_r.power, lo, hi);
    if (rangeOf(m_r.gsm, &lo, &hi)) drawSeries(st[4], m_r.gsm, lo, hi);

    // Time axis.
    p.setPen(UiColor::muted());
    const QRect last = st.last();
    for (int k = 0; k <= 4; ++k) {
        const qint64 ms = m_r.fromMs + (m_r.toMs - m_r.fromMs) * k / 4;
        const int x = xAtMs(ms);
        const int left = qBound(0, x - 40, width() - 80);
        p.drawText(QRect(left, last.bottom() + 4, 80, 16), Qt::AlignCenter, hms(ms));
    }
    if (m_hoverMs >= 0) {
        p.setPen(QPen(ink, 1, Qt::DashLine));
        const int x = xAtMs(m_hoverMs);
        p.drawLine(x, st.first().top(), x, last.bottom());
    }
}

void RadioHealthCanvas::mouseMoveEvent(QMouseEvent *e)
{
    if (m_r.toMs <= m_r.fromMs || e->pos().x() < kLeft) { m_hoverMs = -1; update(); return; }
    m_hoverMs = msAtX(e->pos().x());
    update();
    QToolTip::showText(mapToGlobal(e->pos()), describeAt(m_hoverMs), this);
}

void RadioHealthCanvas::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && e->pos().x() >= kLeft && m_r.toMs > m_r.fromMs) emit timeClicked(msAtX(e->pos().x()));
}

void RadioHealthCanvas::leaveEvent(QEvent *)
{
    m_hoverMs = -1;
    update();
}

// =============================================================================

RadioHealthWindow::RadioHealthWindow(LogModel *model, const QString &tabKey, const QString &tabName, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_model(model)
    , m_tabKey(tabKey)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Radio health — %1").arg(tabName.isEmpty() ? tabKey : tabName));
    WindowGeometry::makeResizableWindow(this);
    resize(1000, 640);

    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("radioHealthSummary"));
    m_summary->setWordWrap(true);
    m_canvas = new RadioHealthCanvas(this);
    m_status = new StatusLine;
    auto *rebuildBtn = new QPushButton(tr("Rebuild"), this);
    rebuildBtn->setToolTip(tr("Read the tab again (more traffic has arrived)"));
    auto *save = new QPushButton(tr("Save image…"), this);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_status, 1);
    buttons->addWidget(rebuildBtn);
    buttons->addWidget(save);
    auto *root = new QVBoxLayout(this);
    root->addWidget(m_summary);
    root->addWidget(m_canvas, 1);
    root->addLayout(buttons);

    connect(rebuildBtn, &QPushButton::clicked, this, &RadioHealthWindow::rebuild);
    connect(save, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, tr("Save radio health"),
                                                          QStringLiteral("radio_health_%1.png").arg(m_tabKey), tr("PNG (*.png)"));
        if (path.isEmpty()) return;
        if (m_canvas->grab().save(path, "PNG")) m_status->ok(tr("Saved %1").arg(path));
        else m_status->fail(tr("Could not write %1").arg(path));
    });
    connect(m_canvas, &RadioHealthCanvas::timeClicked, this, [this](qint64 ms) { emit jumpRequested(m_tabKey, ms); });
    rebuild();
}

QString RadioHealthWindow::summary() const { return m_summary->text(); }

void RadioHealthWindow::rebuild()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_report = RadioHealth::build(m_model);
    QApplication::restoreOverrideCursor();
    m_canvas->setReport(m_report);
    m_summary->setText(RadioHealth::summaryText(m_report));
    m_status->state(tr("%1 @dmi · %2 radio-hole announcements · %3 radio-not-OK spells")
                        .arg(m_report.signal.ms.size()).arg(m_report.radioHole.size()).arg(m_report.radioFail.size()));
}
