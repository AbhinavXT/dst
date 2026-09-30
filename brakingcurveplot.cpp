#include "brakingcurveplot.h"

#include "uicolors.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QtGlobal>
#include <cmath>

namespace {

// One colour per segment index. Deliberately a fixed cycle rather than a
// gradient: the operator's question is "which step am I looking at", and a
// gradient makes adjacent steps hard to tell apart at exactly the place they
// matter most — the junction.
QColor segmentColor(int i)
{
    // Theme-aware since session 65: the fixed cycle that lived here was
    // below 3:1 on Ayu Light for five of its six colours.
    return UiColor::series(i);
}

// A "nice" step for gridlines: 1, 2, 5 x 10^k covering the range in roughly
// `target` divisions.
double niceStep(double span, int target)
{
    if (span <= 0 || target <= 0) { return 1.0; }
    const double raw = span / target;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    const double norm = raw / mag;
    double step;
    if      (norm <= 1.0) { step = 1.0; }
    else if (norm <= 2.0) { step = 2.0; }
    else if (norm <= 5.0) { step = 5.0; }
    else                  { step = 10.0; }
    return step * mag;
}

} // namespace

BrakingCurvePlot::BrakingCurvePlot(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void BrakingCurvePlot::setSnapshot(const Braking::Snapshot &snap)
{
    Braking::Cycle c;
    c.epochMs = snap.epochMs;
    if (snap.valid) { c.frames.push_back(snap); }
    setCycle(c);
}

void BrakingCurvePlot::setCycle(const Braking::Cycle &cycle)
{
    m_cycle = cycle;
    m_snap  = cycle.frames.isEmpty() ? Braking::Snapshot() : cycle.frames.first();
    update();
}

void BrakingCurvePlot::setShowEnvelope(bool on)
{
    m_showEnvelope = on;
    update();
}

void BrakingCurvePlot::setGhosts(const QVector<Braking::Snapshot> &ghosts)
{
    m_ghosts = ghosts;
    update();
}

void BrakingCurvePlot::setCurveFilter(int curveIndex)
{
    m_curveFilter = curveIndex;
    update();
}

void BrakingCurvePlot::setQueryLocation(double metres, bool active)
{
    m_queryLoc    = metres;
    m_queryActive = active;
    update();
}

void BrakingCurvePlot::setLocoLocation(double metres, bool known)
{
    m_locoLoc   = metres;
    m_locoKnown = known;
    update();
}

void BrakingCurvePlot::setLockedRange(bool on, double minLoc, double maxLoc,
                                      double maxSpeedKmph)
{
    m_locked       = on;
    m_lockMinLoc   = minLoc;
    m_lockMaxLoc   = maxLoc;
    m_lockMaxSpeed = maxSpeedKmph;
    update();
}

BrakingCurvePlot::Range BrakingCurvePlot::computeRange() const
{
    Range r;

    if (m_locked && m_lockMaxLoc > m_lockMinLoc && m_lockMaxSpeed > 0) {
        r.x0 = m_lockMinLoc;
        r.x1 = m_lockMaxLoc;
        r.y0 = 0.0;
        r.y1 = m_lockMaxSpeed;
        return r;
    }

    double lo = 0, hi = 0, vlo = 0, vhi = 0;
    if (!m_cycle.span(&lo, &hi, &vlo, &vhi)) {
        r.x0 = 0; r.x1 = 1000; r.y0 = 0; r.y1 = 120;
        return r;
    }
    if (m_locoKnown) {
        lo = qMin(lo, m_locoLoc);
        hi = qMax(hi, m_locoLoc);
    }
    if (m_queryActive) {          // a searched location off the curve still
        lo = qMin(lo, m_queryLoc);  // has to be visible, or the search looks
        hi = qMax(hi, m_queryLoc);  // like it did nothing
    }

    const double padX = qMax(1.0, (hi - lo) * 0.04);
    r.x0 = lo - padX;
    r.x1 = hi + padX;

    // y always starts at zero: a braking curve that does not visibly reach the
    // axis reads as "it never stops", which is a dangerous thing to imply.
    r.y0 = 0.0;
    r.y1 = qMax(1.0, vhi * Braking::kMpsToKmph * 1.10);
    return r;
}

QPointF BrakingCurvePlot::toPx(const Range &r, const QRectF &plot,
                               double x, double y) const
{
    const double sx = (r.x1 > r.x0) ? (x - r.x0) / (r.x1 - r.x0) : 0.0;
    const double sy = (r.y1 > r.y0) ? (y - r.y0) / (r.y1 - r.y0) : 0.0;
    return QPointF(plot.left() + sx * plot.width(),
                   plot.bottom() - sy * plot.height());
}

void BrakingCurvePlot::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QColor fg   = palette().color(QPalette::WindowText);
    const QColor grid = UiColor::grid();
    const QColor axis = UiColor::frame();
    const QColor mute = UiColor::muted();

    const QFontMetrics fm(font());
    // The rotated y-axis title needs a gutter of its own. Sharing one margin
    // with the tick labels is what clipped it to "peed (km/h)".
    const int titleGutter  = fm.height() + 4;
    const int tickGutter   = fm.horizontalAdvance(QStringLiteral("0000")) + 8;
    const int leftMargin   = titleGutter + tickGutter;
    // Two text rows live below the plot — the tick labels and the axis
    // title — so the margin has to fit both, not one.
    const int bottomMargin = 2 * fm.height() + 16;
    const QRectF plot(leftMargin, 10,
                      qMax(10, width() - leftMargin - 14),
                      qMax(10, height() - 10 - bottomMargin));

    int cycleActive = 0;
    for (const Braking::Snapshot &f : m_cycle.frames) { cycleActive += f.activeCount(); }
    if (m_cycle.frames.isEmpty() || cycleActive == 0) {
        p.setPen(mute);
        p.drawText(rect(), Qt::AlignCenter,
                   m_snap.valid ? tr("curve is empty — no active segments")
                                : tr("no braking curve at this point in the session"));
        return;
    }

    const Range r = computeRange();

    // ---- grid + axis labels ---------------------------------------------
    const double xStep = niceStep(r.x1 - r.x0, 6);
    const double yStep = niceStep(r.y1 - r.y0, 5);

    p.setPen(QPen(grid, 1, Qt::DotLine));
    for (double x = std::ceil(r.x0 / xStep) * xStep; x <= r.x1; x += xStep) {
        const QPointF a = toPx(r, plot, x, r.y0), b = toPx(r, plot, x, r.y1);
        p.drawLine(a, b);
    }
    for (double y = std::ceil(r.y0 / yStep) * yStep; y <= r.y1; y += yStep) {
        const QPointF a = toPx(r, plot, r.x0, y), b = toPx(r, plot, r.x1, y);
        p.drawLine(a, b);
    }

    p.setPen(mute);
    for (double x = std::ceil(r.x0 / xStep) * xStep; x <= r.x1; x += xStep) {
        const QPointF a = toPx(r, plot, x, r.y0);
        p.drawText(QRectF(a.x() - 45, plot.bottom() + 4, 90, fm.height()),
                   Qt::AlignHCenter, QString::number(qint64(x)));
    }
    for (double y = std::ceil(r.y0 / yStep) * yStep; y <= r.y1; y += yStep) {
        const QPointF a = toPx(r, plot, r.x0, y);
        p.drawText(QRectF(titleGutter, a.y() - fm.height() / 2.0,
                          tickGutter - 6, fm.height()),
                   Qt::AlignRight | Qt::AlignVCenter, QString::number(y, 'f', 0));
    }

    p.setPen(QPen(axis, 1));
    p.drawLine(plot.bottomLeft(), plot.bottomRight());
    p.drawLine(plot.bottomLeft(), plot.topLeft());

    p.setPen(mute);
    p.drawText(QRectF(plot.left(), plot.bottom() + fm.height() + 4,
                      plot.width(), fm.height()),
               Qt::AlignHCenter, tr("track location (m)"));
    p.save();
    p.translate(fm.height() - 2, plot.center().y());
    p.rotate(-90);
    p.drawText(QRectF(-80, -fm.height(), 160, fm.height()),
               Qt::AlignHCenter, tr("speed (km/h)"));
    p.restore();

    // ---- ghosts (older curves, faint) -----------------------------------
    for (const Braking::Snapshot &g : m_ghosts) {
        QColor c = mute;
        c.setAlpha(70);
        p.setPen(QPen(c, 1));
        for (int ci = 0; ci < g.curves.size(); ++ci) {
            if (m_curveFilter >= 0 && ci != m_curveFilter) { continue; }
            const QVector<QVector<QPointF>> lines = Braking::polylines(g.curves.at(ci));
            for (const QVector<QPointF> &line : lines) {
                QPainterPath path;
                for (int i = 0; i < line.size(); ++i) {
                    const QPointF pt = toPx(r, plot, line.at(i).x(),
                                            line.at(i).y() * Braking::kMpsToKmph);
                    if (i == 0) { path.moveTo(pt); } else { path.lineTo(pt); }
                }
                p.drawPath(path);
            }
        }
    }

    // ---- the current curve ----------------------------------------------
    const bool multiTarget = m_cycle.frames.size() > 1;
    int legendY = plot.top() + 2;

    // ---- most-restrictive envelope (drawn UNDER the target curves) --------
    // The curve the loco is actually held to: at every location, the lowest
    // speed any target permits. With one target it IS that target's curve, so
    // drawing it would just thicken the line — hence multiTarget only.
    if (m_showEnvelope && multiTarget) {
        QPainterPath env;
        bool started = false;
        const int N = 400;
        for (int i = 0; i <= N; ++i) {
            const double x = r.x0 + (r.x1 - r.x0) * double(i) / N;
            double v = 0.0;
            if (!m_cycle.mostRestrictive(x, &v, nullptr, m_curveFilter)) {
                started = false;          // break the path, do not bridge a gap
                continue;
            }
            const QPointF pt = toPx(r, plot, x, v * Braking::kMpsToKmph);
            if (!started) { env.moveTo(pt); started = true; } else { env.lineTo(pt); }
        }
        QColor ec = palette().color(QPalette::WindowText);
        ec.setAlpha(110);
        p.setPen(QPen(ec, 5.0));
        p.drawPath(env);
    }


    for (int fi = 0; fi < m_cycle.frames.size(); ++fi) {
        const Braking::Snapshot &frame = m_cycle.frames.at(fi);

        for (int ci = 0; ci < frame.curves.size(); ++ci) {
            if (m_curveFilter >= 0 && ci != m_curveFilter) { continue; }
            const Braking::Curve &curve = frame.curves.at(ci);
            if (curve.activeCount() == 0) { continue; }

            // Curve 0 solid, curve 1 dashed. Carried by line STYLE so it
            // survives greyscale and colour-blindness.
            const Qt::PenStyle style = (ci == 0) ? Qt::SolidLine : Qt::DashLine;
            const QVector<QVector<QPointF>> lines = Braking::polylines(curve);

            for (int i = 0; i < lines.size(); ++i) {
                // With several targets on one chart, colour has to separate the
                // TARGETS — which target is restricting you is the question.
                // With one target it separates the deceleration steps instead.
                const QColor col = multiTarget ? segmentColor(fi) : segmentColor(i);
                const QVector<QPointF> &line = lines.at(i);
                p.setPen(QPen(col, 2.0, style));
                QPainterPath path;
                for (int k = 0; k < line.size(); ++k) {
                    const QPointF pt = toPx(r, plot, line.at(k).x(),
                                            line.at(k).y() * Braking::kMpsToKmph);
                    if (k == 0) { path.moveTo(pt); } else { path.lineTo(pt); }
                }
                p.drawPath(path);
            }

            const QVector<Braking::Segment> act = curve.activeSegments();
            for (int i = 0; i < act.size(); ++i) {
                const Braking::Segment &sg = act.at(i);
                double v = 0.0;
                if (!sg.speedAt(sg.startLoc, &v)) { continue; }
                const QPointF pt = toPx(r, plot, sg.startLoc, v * Braking::kMpsToKmph);
                p.setPen(QPen(multiTarget ? segmentColor(fi) : segmentColor(i), 1.0));
                p.setBrush(palette().color(QPalette::Base));
                p.drawEllipse(pt, 3.0, 3.0);
            }
            p.setBrush(Qt::NoBrush);
        }

        // Legend: name each target when there is more than one to tell apart.
        if (multiTarget) {
            p.setPen(QPen(segmentColor(fi), 2));
            p.drawLine(QPointF(plot.left() + 6, legendY + fm.height() / 2.0),
                       QPointF(plot.left() + 28, legendY + fm.height() / 2.0));
            p.setPen(mute);
            p.drawText(QRectF(plot.left() + 32, legendY, 220, fm.height()),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       tr("%1 @ %2 m").arg(frame.targetTypeName)
                                      .arg(frame.targetLocation, 0, 'f', 0));
            legendY += fm.height() + 2;
        } else if (frame.curves.size() > 1 && m_curveFilter < 0) {
            for (int ci = 0; ci < frame.curves.size(); ++ci) {
                p.setPen(QPen(fg, 1, ci == 0 ? Qt::SolidLine : Qt::DashLine));
                p.drawLine(QPointF(plot.left() + 6, legendY + fm.height() / 2.0),
                           QPointF(plot.left() + 28, legendY + fm.height() / 2.0));
                p.setPen(mute);
                p.drawText(QRectF(plot.left() + 32, legendY, 120, fm.height()),
                           Qt::AlignLeft | Qt::AlignVCenter, frame.curves.at(ci).name);
                legendY += fm.height() + 2;
            }
        }
    }

    // Envelope legend entry. The envelope PATH is drawn before the per-target
    // curves (see above) so the targets sit on top of it — an envelope painted
    // over them hides the very thing the operator is looking for, which is
    // WHICH target is imposing the limit.
    if (m_showEnvelope && multiTarget) {
        QColor ec = palette().color(QPalette::WindowText);
        ec.setAlpha(150);
        p.setPen(QPen(ec, 4));
        p.drawLine(QPointF(plot.left() + 6, legendY + fm.height() / 2.0),
                   QPointF(plot.left() + 28, legendY + fm.height() / 2.0));
        p.setPen(mute);
        p.drawText(QRectF(plot.left() + 32, legendY, 220, fm.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, tr("most restrictive"));
        legendY += fm.height() + 2;
    }

    // ---- targets ---------------------------------------------------------
    // One marker per target. Labels are stacked rather than all written at the
    // top, because several targets a few metres apart would otherwise print
    // over each other.
    for (int fi = 0; fi < m_cycle.frames.size(); ++fi) {
        const Braking::Snapshot &frame = m_cycle.frames.at(fi);
        const QPointF top = toPx(r, plot, frame.targetLocation, r.y1);
        const QPointF bot = toPx(r, plot, frame.targetLocation, r.y0);
        p.setPen(QPen(UiColor::error(), 1.5, Qt::DashLine));
        p.drawLine(top, bot);

        if (!multiTarget) {           // with many targets the legend names them
            p.setPen(UiColor::error());
            const QString lbl = tr("%1 @ %2 m").arg(frame.targetTypeName)
                                    .arg(frame.targetLocation, 0, 'f', 1);
            const int w = fm.horizontalAdvance(lbl) + 6;
            p.drawText(QRectF(qMin(double(top.x()) + 4, plot.right() - w),
                              plot.top() + 2, w, fm.height()),
                       Qt::AlignLeft, lbl);
        }
    }

    // ---- loco ------------------------------------------------------------
    if (m_locoKnown) {
        // Deliberately not a segment colour and deliberately labelled: an
        // unlabelled vertical line in the same blue as segment 0 reads as part
        // of the curve rather than as the train's position.
        const QPointF top = toPx(r, plot, m_locoLoc, r.y1);
        const QPointF bot = toPx(r, plot, m_locoLoc, r.y0);
        QColor loco = palette().color(QPalette::WindowText);
        p.setPen(QPen(loco, 1.5, Qt::DashDotLine));
        p.drawLine(top, bot);

        p.setPen(loco);
        const QString lbl = tr("loco %1 m").arg(m_locoLoc, 0, 'f', 0);
        const int w = fm.horizontalAdvance(lbl) + 6;
        p.drawText(QRectF(qMax(plot.left(), double(bot.x()) - w - 4),
                          plot.bottom() - fm.height() - 2, w, fm.height()),
                   Qt::AlignRight, lbl);
    }

    // ---- searched location -------------------------------------------------
    if (m_queryActive) {
        const QPointF top = toPx(r, plot, m_queryLoc, r.y1);
        const QPointF bot = toPx(r, plot, m_queryLoc, r.y0);
        p.setPen(QPen(UiColor::warning(), 1.5, Qt::DotLine));
        p.drawLine(top, bot);

        double v = 0.0;
        const bool on = m_cycle.mostRestrictive(m_queryLoc, &v, nullptr, m_curveFilter);
        const QString lbl = on
            ? tr("%1 m  →  %2 km/h").arg(m_queryLoc, 0, 'f', 1)
                                    .arg(v * Braking::kMpsToKmph, 0, 'f', 1)
            : tr("%1 m  (off curve)").arg(m_queryLoc, 0, 'f', 1);

        if (on) {
            const QPointF pt = toPx(r, plot, m_queryLoc, v * Braking::kMpsToKmph);
            p.setBrush(UiColor::warning());
            p.drawEllipse(pt, 3.5, 3.5);
            p.setBrush(Qt::NoBrush);
        }
        // One row ABOVE the loco label: both sit near the bottom axis and at a
        // shared baseline they overprint whenever the two markers are close.
        const int w = fm.horizontalAdvance(lbl) + 8;
        p.setPen(UiColor::warning());
        p.drawText(QRectF(qBound(plot.left(), double(bot.x()) + 6, plot.right() - w),
                          plot.bottom() - 2 * fm.height() - 5, w, fm.height()),
                   Qt::AlignLeft, lbl);
    }

    // ---- hover crosshair --------------------------------------------------
    if (m_hover && plot.contains(m_hoverPx)) {
        const double frac = (m_hoverPx.x() - plot.left()) / plot.width();
        const double loc  = r.x0 + frac * (r.x1 - r.x0);

        double v = 0.0;
        int whichTarget = -1;
        const bool on = m_cycle.mostRestrictive(loc, &v, &whichTarget, m_curveFilter);

        p.setPen(QPen(mute, 1, Qt::DashLine));
        p.drawLine(QPointF(m_hoverPx.x(), plot.top()),
                   QPointF(m_hoverPx.x(), plot.bottom()));

        QString txt = tr("%1 m").arg(loc, 0, 'f', 1);
        if (on) {
            const QPointF pt = toPx(r, plot, loc, v * Braking::kMpsToKmph);
            p.setPen(QPen(fg, 1));
            p.setBrush(fg);
            p.drawEllipse(pt, 3.0, 3.0);
            p.setBrush(Qt::NoBrush);
            txt += tr("   %1 km/h").arg(v * Braking::kMpsToKmph, 0, 'f', 1);
            // Name the target imposing it: "why am I braked here" is answered
            // by the target, not by the number.
            if (m_cycle.frames.size() > 1 && whichTarget >= 0) {
                txt += tr("   (%1)").arg(m_cycle.frames.at(whichTarget).targetTypeName);
            }
        } else {
            // Said out loud rather than shown as 0: off the curve is not the
            // same fact as a permitted speed of zero.
            txt += tr("   (off curve)");
        }

        const int w = fm.horizontalAdvance(txt) + 10;
        QRectF box(qMin(double(m_hoverPx.x()) + 8, plot.right() - w),
                   plot.top() + 2, w, fm.height() + 4);
        p.setPen(Qt::NoPen);
        QColor bg = palette().color(QPalette::Base);
        bg.setAlpha(220);
        p.setBrush(bg);
        p.drawRect(box);
        p.setPen(fg);
        p.setBrush(Qt::NoBrush);
        p.drawText(box, Qt::AlignCenter, txt);
    }
}

void BrakingCurvePlot::mouseMoveEvent(QMouseEvent *event)
{
    m_hover   = true;
    m_hoverPx = event->pos();
    update();
    QWidget::mouseMoveEvent(event);
}

void BrakingCurvePlot::leaveEvent(QEvent *event)
{
    m_hover = false;
    update();
    QWidget::leaveEvent(event);
}
