#include "speeddistance.h"
#include <QCoreApplication>

#include "brakingcurves.h"
#include "fieldplot.h"
#include "logmodel.h"
#include "statusline.h"
#include "uicolors.h"
#include "windowgeometry.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSaveFile>
#include <QSet>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cmath>

namespace SpeedDistance {
namespace {

const double kResetJumpM = 500.0;     // a location step this big is a reset, not travel
const int    kSpeedUnidentified = 511; // 9-bit speed: "unidentified"

}  // namespace

int travelDirection(const QVector<Sample> &samples)
{
    double net = 0.0;
    for (int i = 1; i < samples.size(); ++i) {
        const double step = samples.at(i).locM - samples.at(i - 1).locM;
        if (std::fabs(step) <= kResetJumpM) net += step;
    }
    if (net > 1.0) return 1;
    if (net < -1.0) return -1;
    return 0;
}

double targetLocation(double locM, double targetDistM, int direction)
{
    return direction < 0 ? locM - targetDistM : locM + targetDistM;
}

namespace {

// Direction, location span, top speed, overspeed count and distinct
// targets, from t.samples.
void summarise(Trace &t)
{
    t.direction = 0;
    t.overspeedSamples = 0;
    t.maxSpeedKmh = 0.0;
    t.minLocM = t.maxLocM = 0.0;
    t.targets.clear();
    t.direction = travelDirection(t.samples);
    QSet<QPair<qint64, qint64>> seenTargets;
    bool first = true;
    for (const Sample &s : t.samples) {
        if (first) { t.minLocM = t.maxLocM = s.locM; first = false; }
        t.minLocM = qMin(t.minLocM, s.locM);
        t.maxLocM = qMax(t.maxLocM, s.locM);
        t.maxSpeedKmh = qMax(t.maxSpeedKmh, s.speedKmh);
        if (s.overspeed()) ++t.overspeedSamples;
        if (s.hasTarget) {
            const double at = targetLocation(s.locM, s.targetDistM, t.direction);
            // Distinct to 10 m and 1 km/h: the target does not move, the
            // train's estimate of the distance to it does, a little.
            const QPair<qint64, qint64> key(qint64(std::llround(at / 10.0)), qint64(std::llround(s.targetSpeedKmh)));
            if (!seenTargets.contains(key)) {
                seenTargets.insert(key);
                t.targets << Target{ at, s.targetSpeedKmh, s.epochMs, s.row };
            }
        }
    }
}

}  // namespace

Trace extract(const LogModel *model, int maxRows, qint64 fromMs, qint64 toMs)
{
    Trace t;
    if (!model) return t;
    bool capped = false;
    const QStringList dmiFields{ QStringLiteral("abs_loco_loc"), QStringLiteral("train_speed"),
                                 QStringLiteral("speed_limit_permissible"), QStringLiteral("target_distance"),
                                 QStringLiteral("target_speed"), QStringLiteral("loco_mode") };
    QVector<RowFields> rows = collectRowFields(model, QStringLiteral("dmi"), dmiFields, maxRows, fromMs, toMs, &capped);
    bool dmi = false;
    for (const RowFields &r : rows) dmi = dmi || (r.has(QStringLiteral("abs_loco_loc")) && r.has(QStringLiteral("train_speed")));
    QString locKey = QStringLiteral("abs_loco_loc"), speedKey = QStringLiteral("train_speed"),
            modeKey = QStringLiteral("loco_mode");
    if (dmi) {
        t.source = QStringLiteral("dmi");
    } else {
        rows = collectRowFields(model, QStringLiteral("lsrp"),
                                { QStringLiteral("ABS_LOCO_LOC"), QStringLiteral("TRAIN_SPEED"), QStringLiteral("LOCO_MODE") },
                                maxRows, fromMs, toMs, &capped);
        locKey = QStringLiteral("ABS_LOCO_LOC");
        speedKey = QStringLiteral("TRAIN_SPEED");
        modeKey = QStringLiteral("LOCO_MODE");
        if (!rows.isEmpty()) t.source = QStringLiteral("lsrp");
    }
    t.hitCap = capped;

    for (const RowFields &r : rows) {
        if (!r.has(locKey) || !r.has(speedKey)) { ++t.rowsSkipped; continue; }
        const qint64 speed = r.raw.value(speedKey);
        if (speed >= kSpeedUnidentified || speed > 400) { ++t.rowsSkipped; continue; }
        Sample s;
        s.row = r.row;
        s.epochMs = r.epochMs;
        s.locM = double(r.raw.value(locKey));
        s.speedKmh = double(speed);
        s.mode = r.display.value(modeKey);
        if (t.source == QLatin1String("dmi")) {
            if (r.has(QStringLiteral("speed_limit_permissible"))) {
                const qint64 p = r.raw.value(QStringLiteral("speed_limit_permissible"));
                if (p > 0 && p < kSpeedUnidentified) { s.hasPermitted = true; s.permittedKmh = double(p); }
            }
            if (r.has(QStringLiteral("target_distance")) && r.has(QStringLiteral("target_speed"))) {
                const qint64 d = r.raw.value(QStringLiteral("target_distance"));
                if (d > 0) {
                    s.hasTarget = true;
                    s.targetDistM = double(d);
                    s.targetSpeedKmh = double(r.raw.value(QStringLiteral("target_speed")));
                }
            }
        }
        t.samples << s;
    }
    summarise(t);
    return t;
}

Trace knownOnly(const Trace &trace, int *unknown)
{
    Trace t = trace;
    t.samples.clear();
    int dropped = 0;
    for (const Sample &s : trace.samples) {
        if (locationKnown(s)) t.samples << s;
        else ++dropped;
    }
    if (unknown) *unknown = dropped;
    summarise(t);
    return t;
}

QString toCsv(const Trace &trace, double fromLocM, double toLocM)
{
    const double lo = qMin(fromLocM, toLocM), hi = qMax(fromLocM, toLocM);
    QString out = QStringLiteral("time_local,epoch_ms,location_m,speed_kmh,permitted_kmh,"
                                 "target_distance_m,target_speed_kmh,overspeed,loco_mode\n");
    for (const Sample &s : trace.samples) {
        if (s.locM < lo || s.locM > hi) continue;
        QString mode = s.mode;
        if (mode.contains(QLatin1Char(','))) mode = QStringLiteral("\"%1\"").arg(mode);
        out += QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8,%9\n")
                   .arg(QDateTime::fromMSecsSinceEpoch(s.epochMs).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")))
                   .arg(s.epochMs)
                   .arg(s.locM, 0, 'f', 0)
                   .arg(s.speedKmh, 0, 'f', 0)
                   .arg(s.hasPermitted ? QString::number(s.permittedKmh, 'f', 0) : QString())
                   .arg(s.hasTarget ? QString::number(s.targetDistM, 'f', 0) : QString())
                   .arg(s.hasTarget ? QString::number(s.targetSpeedKmh, 'f', 0) : QString())
                   .arg(s.overspeed() ? QStringLiteral("1") : QStringLiteral("0"))
                   .arg(mode);
    }
    return out;
}

}  // namespace SpeedDistance

// =============================================================================
//  Canvas
// =============================================================================

using namespace SpeedDistance;

namespace {
const int    kDrag = 6;
const double kWheel = 1.25;
const double kMinSpanM = 20.0;
// A braking curve is drawn only if it lies within this distance of the
// stretch of track the train reported.
const double kCurveNearM = 5000.0;

// "1 sample" / "826 samples".
QString countOf(int n, const char *one, const char *many)
{
    return QStringLiteral("%1 %2").arg(n).arg(QCoreApplication::translate("SpeedDistanceWindow", n == 1 ? one : many));
}
}  // namespace

SpeedDistanceCanvas::SpeedDistanceCanvas(QWidget *parent)
    : QWidget(parent)
    , m_snaps(new QVector<Braking::Snapshot>())
{
    setMouseTracking(true);
    setMinimumHeight(260);
    setFocusPolicy(Qt::WheelFocus);
    UiColor::onThemeChange(this, [this]() { update(); });
}

SpeedDistanceCanvas::~SpeedDistanceCanvas()
{
    delete m_snaps;
}

void SpeedDistanceCanvas::setTrace(const Trace &trace)
{
    m_trace = trace;
    m_cursor = -1;
    resetZoom();
}

void SpeedDistanceCanvas::setBrakingSnapshots(const QVector<Braking::Snapshot> &snaps)
{
    *m_snaps = snaps;
    update();
}

bool SpeedDistanceCanvas::isZoomed() const
{
    if (m_trace.isEmpty()) return false;
    return m_yManual || m_x0 > m_trace.minLocM + 0.5 || m_x1 < m_trace.maxLocM - 0.5;
}

void SpeedDistanceCanvas::fitY()
{
    if (m_yManual) return;
    double hi = 10.0;
    for (const Sample &s : m_trace.samples) {
        if (s.locM < m_x0 || s.locM > m_x1) continue;
        hi = qMax(hi, s.speedKmh);
        if (m_showPermitted && s.hasPermitted) hi = qMax(hi, s.permittedKmh);
    }
    m_y0 = 0.0;
    m_y1 = std::ceil(hi * 1.08 / 10.0) * 10.0;
}

void SpeedDistanceCanvas::setView(double x0, double x1)
{
    if (m_trace.isEmpty()) return;
    const double lo = m_trace.minLocM, hi = qMax(m_trace.maxLocM, m_trace.minLocM + 100.0);
    double span = qBound(kMinSpanM, x1 - x0, (hi - lo) * 1.04 + 100.0);
    double c = qBound(lo, (x0 + x1) / 2.0, hi);
    m_x0 = c - span / 2.0;
    m_x1 = c + span / 2.0;
    fitY();
    update();
}

void SpeedDistanceCanvas::setDistanceView(double fromM, double toM)
{
    m_yManual = false;
    setView(qMin(fromM, toM), qMax(fromM, toM));
}

void SpeedDistanceCanvas::zoomIn()
{
    const double c = (m_x0 + m_x1) / 2.0, h = (m_x1 - m_x0) / 4.0;
    setView(c - h, c + h);
}

void SpeedDistanceCanvas::zoomOut()
{
    m_yManual = false;
    const double c = (m_x0 + m_x1) / 2.0, h = (m_x1 - m_x0);
    setView(c - h, c + h);
}

void SpeedDistanceCanvas::resetZoom()
{
    m_yManual = false;
    if (m_trace.isEmpty()) { m_x0 = 0; m_x1 = 1; m_y0 = 0; m_y1 = 1; update(); return; }
    const double pad = qMax(50.0, (m_trace.maxLocM - m_trace.minLocM) * 0.02);
    m_x0 = m_trace.minLocM - pad;
    m_x1 = m_trace.maxLocM + pad;
    fitY();
    update();
}

QRect SpeedDistanceCanvas::plotRect() const
{
    const int line = fontMetrics().height();
    const int left = fontMetrics().horizontalAdvance(QStringLiteral("0000")) + line + 20;
    return rect().adjusted(left, line * 2 + 10, -16, -(line * 2 + 14));
}

double SpeedDistanceCanvas::xOf(double locM) const
{
    const QRect r = plotRect();
    double f = (locM - m_x0) / qMax(1e-9, m_x1 - m_x0);
    // Travelling towards lower locations: the axis is reversed so the
    // train still runs left to right.
    if (m_trace.direction < 0) f = 1.0 - f;
    return r.left() + f * r.width();
}

double SpeedDistanceCanvas::yOf(double kmh) const
{
    const QRect r = plotRect();
    return r.bottom() - (kmh - m_y0) / qMax(1e-9, m_y1 - m_y0) * r.height();
}

double SpeedDistanceCanvas::locAt(int px) const
{
    const QRect r = plotRect();
    double f = double(px - r.left()) / qMax(1, r.width());
    if (m_trace.direction < 0) f = 1.0 - f;
    return m_x0 + f * (m_x1 - m_x0);
}

double SpeedDistanceCanvas::kmhAt(int py) const
{
    const QRect r = plotRect();
    return m_y0 + double(r.bottom() - py) / qMax(1, r.height()) * (m_y1 - m_y0);
}

int SpeedDistanceCanvas::nearestSample(int px, int py) const
{
    int best = -1;
    double bestD = 1e18;
    for (int i = 0; i < m_trace.samples.size(); ++i) {
        const Sample &s = m_trace.samples.at(i);
        if (s.locM < m_x0 || s.locM > m_x1) continue;
        const double dx = xOf(s.locM) - px, dy = yOf(s.speedKmh) - py;
        const double d = dx * dx * 4.0 + dy * dy;
        if (d < bestD) { bestD = d; best = i; }
    }
    return bestD <= 40.0 * 40.0 * 4.0 ? best : -1;
}

int SpeedDistanceCanvas::brakingIndexFor(int sample) const
{
    if (m_snaps->isEmpty()) return -1;
    const qint64 at = (sample >= 0 && sample < m_trace.samples.size())
                          ? m_trace.samples.at(sample).epochMs
                          : (m_trace.samples.isEmpty() ? 0 : m_trace.samples.last().epochMs);
    return Braking::indexAtOrBefore(*m_snaps, at);
}

void SpeedDistanceCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().base());
    const QColor ink = UiColor::muted(), text = palette().color(QPalette::Text);
    const QColor frame = UiColor::frame(), grid = UiColor::grid();
    const QColor actual = UiColor::series(0), permitted = UiColor::warning(), target = UiColor::series(2);
    const QRect r = plotRect();

    if (m_trace.isEmpty()) {
        p.setPen(frame);
        p.drawRect(r);
        p.setPen(ink);
        p.drawText(rect(), Qt::AlignCenter,
                   tr("No location and speed in this tab: it needs @dmi or @lsrp frames"));
        return;
    }

    QFont small = p.font();
    small.setPointSizeF(qMax(6.0, small.pointSizeF() - 1.0));
    QFont bold = p.font();
    bold.setBold(true);

    // ---- axes ------------------------------------------------------------------------
    p.setFont(small);
    for (double v : niceTicks(m_y0, m_y1, 6, true)) {
        const int y = int(std::lround(yOf(v)));
        p.setPen(grid);
        p.drawLine(r.left() + 1, y, r.right() - 1, y);
        p.setPen(ink);
        p.drawText(QRect(0, y - 9, r.left() - 6, 18), Qt::AlignRight | Qt::AlignVCenter, QString::number(v, 'f', 0));
    }
    const QVector<double> xTicks = niceTicks(m_x0, m_x1, qMax(3, r.width() / 110), false);
    const double xStep = xTicks.size() > 1 ? xTicks.at(1) - xTicks.at(0) : 1000.0;
    const int kmDecimals = xStep >= 1000.0 ? 0 : (xStep >= 100.0 ? 1 : (xStep >= 10.0 ? 2 : 3));
    for (double v : xTicks) {
        const int x = int(std::lround(xOf(v)));
        if (x < r.left() || x > r.right()) continue;
        p.setPen(grid);
        p.drawLine(x, r.top() + 1, x, r.bottom() - 1);
        p.setPen(ink);
        const QString label = QString::number(v / 1000.0, 'f', kmDecimals);
        const int w = p.fontMetrics().horizontalAdvance(label);
        p.drawText(x - w / 2, r.bottom() + 4 + p.fontMetrics().ascent(), label);
    }
    p.setPen(frame);
    p.drawRect(r);

    p.setFont(bold);
    p.setPen(text);
    const QString xTitle = m_trace.direction < 0
        ? tr("Track location (km) — counting down, train travels left to right")
        : tr("Track location (km)");
    p.drawText(QRect(r.left(), height() - fontMetrics().height() - 6, r.width(), fontMetrics().height() + 4),
               Qt::AlignCenter, xTitle);
    p.save();
    p.translate(4, r.center().y());
    p.rotate(-90);
    p.drawText(QRect(-r.height() / 2, 0, r.height(), fontMetrics().height() + 2), Qt::AlignCenter, tr("Speed (km/h)"));
    p.restore();

    // ---- traces --------------------------------------------------------------------------
    p.setClipRect(r.adjusted(1, 1, -1, -1));
    p.setRenderHint(QPainter::Antialiasing, true);
    auto pathOf = [this](bool permittedLine) {
        QPainterPath path;
        bool open = false;
        const Sample *prev = nullptr;
        for (const Sample &s : m_trace.samples) {
            if (permittedLine && !s.hasPermitted) { open = false; prev = nullptr; continue; }
            const double v = permittedLine ? s.permittedKmh : s.speedKmh;
            const QPointF at(xOf(s.locM), yOf(v));
            const bool jump = prev && std::fabs(s.locM - prev->locM) > 500.0;
            if (!open || jump) { path.moveTo(at); open = true; }
            else if (permittedLine) { path.lineTo(QPointF(at.x(), path.currentPosition().y())); path.lineTo(at); }
            else { path.lineTo(at); }
            prev = &s;
        }
        return path;
    };
    if (m_showPermitted && m_trace.source == QLatin1String("dmi")) {
        p.setPen(QPen(permitted, 2.0, Qt::DashLine));
        p.drawPath(pathOf(true));
    }

    // Braking curve in force at the cursor (or at the end of the run).
    m_brakingNote.clear();
    if (m_showBraking) {
        const int bi = brakingIndexFor(m_cursor);
        if (m_snaps->isEmpty()) {
            m_brakingNote = tr("no @uba braking curves in this tab");
        } else if (bi < 0) {
            m_brakingNote = tr("no braking curve yet at this time");
        } else {
            const Braking::Snapshot &snap = m_snaps->at(bi);
            double minLoc = 0, maxLoc = 0, minSp = 0, maxSp = 0;
            if (!snap.span(&minLoc, &maxLoc, &minSp, &maxSp)) {
                m_brakingNote = tr("the braking curve at this time has no active segment");
            } else if (maxLoc < m_trace.minLocM - kCurveNearM || minLoc > m_trace.maxLocM + kCurveNearM) {
                m_brakingNote = tr("the @uba curve (%1–%2 km) is nowhere near the train's locations — "
                                   "not drawn, it is not on the same scale")
                                    .arg(minLoc / 1000.0, 0, 'f', 2).arg(maxLoc / 1000.0, 0, 'f', 2);
            } else {
                for (int ci = 0; ci < snap.curves.size(); ++ci) {
                    const QColor c = ci == 0 ? UiColor::error() : UiColor::series(3);
                    p.setPen(QPen(c, 1.8, ci == 0 ? Qt::SolidLine : Qt::DashDotLine));
                    for (const QVector<QPointF> &line : Braking::polylines(snap.curves.at(ci))) {
                        QPainterPath path;
                        for (int k = 0; k < line.size(); ++k) {
                            const QPointF at(xOf(line.at(k).x()), yOf(line.at(k).y() * 3.6));
                            if (k == 0) path.moveTo(at); else path.lineTo(at);
                        }
                        p.drawPath(path);
                    }
                }
                m_brakingNote = tr("braking curve from @uba at %1 (target: %2)")
                                    .arg(QDateTime::fromMSecsSinceEpoch(snap.epochMs).toString(QStringLiteral("HH:mm:ss.zzz")),
                                         snap.targetTypeName);
            }
        }
    }

    p.setPen(QPen(actual, 2.0));
    p.drawPath(pathOf(false));

    // Overspeed samples, marked where they are.
    p.setPen(Qt::NoPen);
    p.setBrush(UiColor::error());
    for (const Sample &s : m_trace.samples) {
        if (s.overspeed() && s.locM >= m_x0 && s.locM <= m_x1) {
            p.drawEllipse(QPointF(xOf(s.locM), yOf(s.speedKmh)), 3.0, 3.0);
        }
    }

    if (m_showTargets) {
        p.setBrush(target);
        for (const Target &t : m_trace.targets) {
            if (t.locM < m_x0 || t.locM > m_x1) continue;
            const QPointF at(xOf(t.locM), yOf(t.speedKmh));
            QPolygonF tri;
            tri << QPointF(at.x(), at.y()) << QPointF(at.x() - 6, at.y() - 10) << QPointF(at.x() + 6, at.y() - 10);
            p.setPen(QPen(target.darker(130), 1));
            p.drawPolygon(tri);
        }
    }
    p.setClipping(false);

    // ---- rubber band ---------------------------------------------------------------------------
    if (m_drag == Drag::Box) {
        QRect band = QRect(m_dragStart, m_dragNow).normalized().intersected(r);
        band.setTop(r.top());
        band.setBottom(r.bottom());
        QColor fill = actual;
        fill.setAlpha(40);
        p.fillRect(band, fill);
    }

    // ---- legend -------------------------------------------------------------------------------------
    p.setFont(font());
    int lx = r.left();
    const int ly = 4, lh = fontMetrics().height() + 4;
    auto legendItem = [&](const QColor &c, Qt::PenStyle style, const QString &label) {
        p.setPen(QPen(c, 2.2, style));
        p.drawLine(lx, ly + lh / 2, lx + 20, ly + lh / 2);
        p.setPen(text);
        const int w = fontMetrics().horizontalAdvance(label);
        p.drawText(lx + 26, ly, w + 4, lh, Qt::AlignVCenter, label);
        lx += w + 44;
    };
    legendItem(actual, Qt::SolidLine, tr("actual speed (%1)").arg(m_trace.source));
    if (m_showPermitted && m_trace.source == QLatin1String("dmi")) legendItem(permitted, Qt::DashLine, tr("permitted"));
    if (m_showBraking && !m_snaps->isEmpty()) legendItem(UiColor::error(), Qt::SolidLine, tr("braking curve"));
    if (m_showTargets && !m_trace.targets.isEmpty()) {
        p.setBrush(target);
        p.setPen(target.darker(130));
        QPolygonF tri;
        tri << QPointF(lx + 10, ly + lh - 3) << QPointF(lx + 4, ly + 3) << QPointF(lx + 16, ly + 3);
        p.drawPolygon(tri);
        p.setPen(text);
        p.drawText(lx + 24, ly, 200, lh, Qt::AlignVCenter, tr("target"));
        lx += 90;
    }
    if (m_trace.overspeedSamples > 0) {
        p.setBrush(UiColor::error());
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(lx + 8, ly + lh / 2), 3.5, 3.5);
        p.setPen(text);
        p.drawText(lx + 18, ly, 260, lh, Qt::AlignVCenter,
                   tr("above permitted (%1 samples)").arg(m_trace.overspeedSamples));
    }
    if (isZoomed()) {
        p.setPen(ink);
        p.drawText(QRect(r.left(), ly + lh, r.width(), lh), Qt::AlignRight | Qt::AlignVCenter,
                   tr("zoomed · double-click to fit"));
    }

    // ---- cursor readout --------------------------------------------------------------------------------
    if (m_cursor >= 0 && m_cursor < m_trace.samples.size()) {
        const Sample &s = m_trace.samples.at(m_cursor);
        const QPointF at(xOf(s.locM), yOf(s.speedKmh));
        p.setPen(QPen(grid.darker(140), 1, Qt::DotLine));
        p.drawLine(QPointF(at.x(), r.top() + 1), QPointF(at.x(), r.bottom() - 1));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(UiColor::error(), 2));
        p.drawEllipse(at, 4.5, 4.5);
        QStringList lines;
        lines << QDateTime::fromMSecsSinceEpoch(s.epochMs).toString(QStringLiteral("HH:mm:ss.zzz"))
              << tr("location %1 km").arg(s.locM / 1000.0, 0, 'f', 3)
              << tr("speed %1 km/h").arg(s.speedKmh, 0, 'f', 0);
        if (s.hasPermitted) lines << tr("permitted %1 km/h%2").arg(s.permittedKmh, 0, 'f', 0)
                                        .arg(s.overspeed() ? tr("  (above)") : QString());
        if (s.hasTarget) lines << tr("target %1 km/h in %2 m").arg(s.targetSpeedKmh, 0, 'f', 0).arg(s.targetDistM, 0, 'f', 0);
        if (!s.mode.isEmpty()) lines << tr("mode %1").arg(s.mode);
        const QString txt = lines.join(QLatin1Char('\n'));
        const QRect tb = fontMetrics().boundingRect(QRect(0, 0, 500, 500), Qt::AlignLeft, txt);
        QRect box(0, 0, tb.width() + 14, tb.height() + 8);
        box.moveTo(int(at.x()) + 12, r.top() + 6);
        if (box.right() > r.right()) box.moveRight(int(at.x()) - 12);
        QColor fill = palette().color(QPalette::Base);
        fill.setAlpha(235);
        p.setBrush(fill);
        p.setPen(frame);
        p.drawRect(box);
        p.setPen(text);
        p.drawText(box.adjusted(7, 4, -7, -4), Qt::AlignLeft | Qt::AlignTop, txt);
    }
}

void SpeedDistanceCanvas::mouseMoveEvent(QMouseEvent *ev)
{
    if (m_drag == Drag::Box) { m_dragNow = ev->pos(); update(); return; }
    if (m_drag == Drag::Pan) {
        const QRect r = plotRect();
        double dM = double(ev->pos().x() - m_dragStart.x()) / qMax(1, r.width()) * (m_panX1 - m_panX0);
        if (m_trace.direction < 0) dM = -dM;
        setView(m_panX0 - dM, m_panX1 - dM);
        return;
    }
    if ((ev->buttons() & (Qt::LeftButton | Qt::RightButton)) && !m_dragStart.isNull()
        && (ev->pos() - m_dragStart).manhattanLength() > kDrag) {
        if (ev->buttons() & Qt::LeftButton) { m_drag = Drag::Box; m_dragNow = ev->pos(); }
        else { m_drag = Drag::Pan; m_panX0 = m_x0; m_panX1 = m_x1; setCursor(Qt::ClosedHandCursor); }
        update();
        return;
    }
    const int idx = nearestSample(ev->pos().x(), ev->pos().y());
    if (idx != m_cursor) { m_cursor = idx; update(); emit cursorChanged(idx); }
}

void SpeedDistanceCanvas::leaveEvent(QEvent *) {}

void SpeedDistanceCanvas::mousePressEvent(QMouseEvent *ev)
{
    setFocus(Qt::MouseFocusReason);
    if (ev->button() == Qt::LeftButton || ev->button() == Qt::RightButton) m_dragStart = ev->pos();
}

void SpeedDistanceCanvas::mouseReleaseEvent(QMouseEvent *ev)
{
    const Drag was = m_drag;
    m_drag = Drag::None;
    unsetCursor();
    if (was == Drag::Box) {
        const int a = m_dragStart.x(), b = ev->pos().x();
        m_dragStart = QPoint();
        if (std::abs(b - a) >= kDrag) {
            const double l1 = locAt(a), l2 = locAt(b);
            setView(qMin(l1, l2), qMax(l1, l2));
        }
        update();
        return;
    }
    if (was == Drag::Pan) { m_dragStart = QPoint(); return; }
    m_dragStart = QPoint();
    if (ev->button() == Qt::LeftButton) {
        const int idx = nearestSample(ev->pos().x(), ev->pos().y());
        if (idx >= 0) emit sampleActivated(m_trace.samples.at(idx).row, m_trace.samples.at(idx).epochMs);
    } else if (ev->button() == Qt::RightButton) {
        QMenu menu(this);
        QAction *in = menu.addAction(tr("Zoom in"));
        QAction *out = menu.addAction(tr("Zoom out"));
        QAction *fit = menu.addAction(tr("Fit all (double-click)"));
        QAction *chosen = menu.exec(mapToGlobal(ev->pos()));
        if (chosen == in) zoomIn(); else if (chosen == out) zoomOut(); else if (chosen == fit) resetZoom();
    }
}

void SpeedDistanceCanvas::mouseDoubleClickEvent(QMouseEvent *ev)
{
    if (ev->button() == Qt::LeftButton) resetZoom();
}

void SpeedDistanceCanvas::wheelEvent(QWheelEvent *ev)
{
    const int delta = ev->angleDelta().y();
    if (delta == 0 || m_trace.isEmpty()) return;
    const double f = std::pow(kWheel, -double(delta) / 120.0);
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    const QPoint pos = ev->position().toPoint();
#else
    const QPoint pos = ev->pos();
#endif
    if (ev->modifiers() & Qt::ShiftModifier) {
        const double at = kmhAt(pos.y());
        m_y0 = qMax(0.0, at - (at - m_y0) * f);
        m_y1 = at + (m_y1 - at) * f;
        m_yManual = true;
        update();
    } else {
        const double at = locAt(pos.x());
        setView(at - (at - m_x0) * f, at + (m_x1 - at) * f);
    }
    ev->accept();
}

void SpeedDistanceCanvas::keyPressEvent(QKeyEvent *ev)
{
    const double span = m_x1 - m_x0;
    const double dir = m_trace.direction < 0 ? -1.0 : 1.0;
    switch (ev->key()) {
    case Qt::Key_Plus: case Qt::Key_Equal: zoomIn(); break;
    case Qt::Key_Minus: zoomOut(); break;
    case Qt::Key_0: case Qt::Key_Home: resetZoom(); break;
    case Qt::Key_Left:  setView(m_x0 - dir * span * 0.2, m_x1 - dir * span * 0.2); break;
    case Qt::Key_Right: setView(m_x0 + dir * span * 0.2, m_x1 + dir * span * 0.2); break;
    default: QWidget::keyPressEvent(ev); return;
    }
    ev->accept();
}

// =============================================================================
//  Window
// =============================================================================

SpeedDistanceWindow::SpeedDistanceWindow(LogModel *model, const QString &tabKey, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_model(model)
    , m_tabKey(tabKey)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Speed vs distance — %1").arg(tabKey));
    WindowGeometry::makeResizableWindow(this);
    resize(1100, 620);
    WindowGeometry::restore(this, QStringLiteral("speedDistance"));

    m_canvas = new SpeedDistanceCanvas(this);
    m_status = new StatusLine;
    m_status->setWordWrap(true);

    m_permitted = new QCheckBox(tr("Permitted speed"), this);
    m_targets = new QCheckBox(tr("Targets"), this);
    m_braking = new QCheckBox(tr("Braking curve (@uba)"), this);
    for (QCheckBox *b : { m_permitted, m_targets, m_braking }) b->setChecked(true);
    m_braking->setToolTip(tr("The firmware's own braking curves, from the @uba frame in force at the "
                             "cursor (or at the end of the run)."));

    auto button = [this](const QString &text, const QString &tip) {
        auto *b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setMinimumWidth(30);
        return b;
    };
    QToolButton *reloadButton = button(tr("Reload"), tr("Read the tab again (new traffic)"));
    QToolButton *out = button(QStringLiteral("\u2212"), tr("Zoom out"));
    QToolButton *in = button(QStringLiteral("+"), tr("Zoom in (or the mouse wheel)"));
    QToolButton *fit = button(tr("Fit"), tr("Show the whole run (0, or double-click)"));
    auto *exportButton = new QToolButton(this);
    exportButton->setObjectName(QStringLiteral("speedDistanceExport"));
    exportButton->setText(tr("Export"));
    exportButton->setPopupMode(QToolButton::InstantPopup);
    auto *exportMenu = new QMenu(exportButton);
    QAction *png = exportMenu->addAction(tr("Save image (PNG)…"));
    QAction *csv = exportMenu->addAction(tr("Save data in view (CSV)…"));
    QAction *copy = exportMenu->addAction(tr("Copy image"));
    exportButton->setMenu(exportMenu);

    auto *top = new QHBoxLayout;
    top->addWidget(m_permitted);
    top->addWidget(m_targets);
    top->addWidget(m_braking);
    top->addStretch(1);
    top->addWidget(reloadButton);
    top->addSpacing(8);
    top->addWidget(out);
    top->addWidget(in);
    top->addWidget(fit);
    top->addWidget(exportButton);

    auto *hint = new QLabel(tr("Wheel: zoom distance  ·  Shift+wheel: zoom speed  ·  Drag: zoom to a stretch  ·  "
                               "Right-drag: pan  ·  Double-click: fit  ·  Click: open the message"));
    hint->setStyleSheet(UiColor::mutedStyle());
    hint->setWordWrap(true);

    auto *root = new QVBoxLayout(this);
    root->addLayout(top);
    root->addWidget(m_canvas, 1);
    root->addWidget(hint);
    root->addWidget(m_status);

    connect(m_permitted, &QCheckBox::toggled, m_canvas, &SpeedDistanceCanvas::setShowPermitted);
    connect(m_targets, &QCheckBox::toggled, m_canvas, &SpeedDistanceCanvas::setShowTargets);
    connect(m_braking, &QCheckBox::toggled, m_canvas, &SpeedDistanceCanvas::setShowBraking);
    connect(reloadButton, &QToolButton::clicked, this, &SpeedDistanceWindow::reload);
    connect(in, &QToolButton::clicked, m_canvas, &SpeedDistanceCanvas::zoomIn);
    connect(out, &QToolButton::clicked, m_canvas, &SpeedDistanceCanvas::zoomOut);
    connect(fit, &QToolButton::clicked, m_canvas, &SpeedDistanceCanvas::resetZoom);
    connect(m_canvas, &SpeedDistanceCanvas::sampleActivated, this,
            [this](int, qint64 ms) { emit jumpRequested(m_tabKey, ms); });
    connect(m_canvas, &SpeedDistanceCanvas::cursorChanged, this, [this](int) {
        if (!m_canvas->brakingNote().isEmpty()) describeTrace();
    });
    connect(png, &QAction::triggered, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, tr("Save image"),
            QStringLiteral("%1_speed_distance.png").arg(m_tabKey), tr("PNG image (*.png)"));
        if (path.isEmpty()) return;
        if (saveImage(path)) m_status->ok(tr("Saved %1").arg(path)); else m_status->fail(tr("Could not write %1").arg(path));
    });
    connect(csv, &QAction::triggered, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, tr("Save data"),
            QStringLiteral("%1_speed_distance.csv").arg(m_tabKey), tr("CSV (*.csv)"));
        if (path.isEmpty()) return;
        if (saveCsv(path)) m_status->ok(tr("Saved %1").arg(path)); else m_status->fail(tr("Could not write %1").arg(path));
    });
    connect(copy, &QAction::triggered, this, [this]() {
        QApplication::clipboard()->setPixmap(m_canvas->grab());
        m_status->ok(tr("Image copied"));
    });

    reload();
}

void SpeedDistanceWindow::reload()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    // 0 m is "location not known" here too (session 148, as the track
    // diagram, the two-loco view and the incident report read it): plotted
    // as a place, it stretched the axis from 0 km and squeezed the run
    // against its far end.
    const Trace trace = knownOnly(extract(m_model), &m_unknownLocation);
    bool capped = false;
    const QVector<Braking::Snapshot> snaps = Braking::collect(m_model, 200000, &capped);
    QApplication::restoreOverrideCursor();
    m_canvas->setTrace(trace);
    m_canvas->setBrakingSnapshots(snaps);
    m_permitted->setEnabled(trace.source == QLatin1String("dmi"));
    m_targets->setEnabled(!trace.targets.isEmpty());
    m_braking->setEnabled(!snaps.isEmpty());
    describeTrace();
}

void SpeedDistanceWindow::describeTrace()
{
    const Trace &t = m_canvas->trace();
    if (t.isEmpty()) {
        m_status->state(tr("No location and speed found. This view reads @dmi frames (location, speed, "
                           "permitted speed, target) or, failing that, @lsrp (location and speed only)."));
        return;
    }
    QStringList parts;
    parts << tr("%1 from @%2").arg(countOf(t.samples.size(), "sample", "samples"), t.source);
    parts << tr("%1–%2 km").arg(t.minLocM / 1000.0, 0, 'f', 3).arg(t.maxLocM / 1000.0, 0, 'f', 3);
    parts << tr("max %1 km/h").arg(t.maxSpeedKmh, 0, 'f', 0);
    if (t.source == QLatin1String("lsrp")) parts << tr("no permitted speed or targets (no @dmi in this tab)");
    if (t.overspeedSamples > 0) parts << tr("%1 above permitted").arg(countOf(t.overspeedSamples, "sample", "samples"));
    if (t.direction == 0) parts << tr("direction of travel unclear (the train barely moved)");
    if (m_unknownLocation > 0)
        parts << tr("%1 at 0 m (location not known) left off").arg(countOf(m_unknownLocation, "sample", "samples"));
    if (t.rowsSkipped > 0) parts << tr("%1 skipped (speed unidentified or no location)").arg(countOf(t.rowsSkipped, "row", "rows"));
    if (t.hitCap) parts << tr("sampled (very long tab)");
    if (!m_canvas->brakingNote().isEmpty()) parts << m_canvas->brakingNote();
    if (t.overspeedSamples > 0) m_status->warn(parts.join(QStringLiteral("  ·  ")));
    else m_status->state(parts.join(QStringLiteral("  ·  ")));
}

bool SpeedDistanceWindow::saveImage(const QString &path) const
{
    return m_canvas->grab().save(path, "PNG");
}

bool SpeedDistanceWindow::saveCsv(const QString &path) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    file.write(toCsv(m_canvas->trace(), m_canvas->viewFromM(), m_canvas->viewToM()).toUtf8());
    return file.commit();
}

void SpeedDistanceWindow::closeEvent(QCloseEvent *event)
{
    WindowGeometry::save(this, QStringLiteral("speedDistance"));
    QWidget::closeEvent(event);
}
