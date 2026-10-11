#include "trackdiagramwindow.h"

#include "dmipanel.h"
#include "logmodel.h"
#include "settings.h"
#include "statusline.h"
#include "uicolors.h"
#include "windowgeometry.h"

#include <QApplication>
#include <QSet>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QHelpEvent>
#include <QMenu>
#include <QToolTip>
#include <cmath>
#include <algorithm>
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

void TrackDiagramCanvas::setOverlay(const TrackDiagram::LayoutOverlay &o)
{
    m_overlay = o;
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
    // The rail's horizontal extent; the vertical layout is paintEvent's.
    return QRect(56, 0, qMax(1, width() - 112), height());
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

namespace {

QString km(double m) { return QString::number(m / 1000.0, 'f', 3) + QStringLiteral(" km"); }
QString hms(qint64 ms) { return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss")); }
QString modeText(const QString &mode) { QString m = mode; return m.replace(QLatin1Char('_'), QLatin1Char(' ')); }

// Labels in one lane, stacked into up to `rows` rows so none overlaps;
// a label with no free row is left out (its mark's tooltip still has it).
// Returns the row per label, -1 = not drawn. Input must be sorted by x.
QVector<int> stackLabels(const QVector<QPair<double, int>> &xw, int rows)
{
    QVector<double> rightEdge(rows, -1e9);
    QVector<int> out;
    for (const auto &l : xw) {
        const double left = l.first - l.second / 2.0;
        int row = -1;
        for (int r = 0; r < rows; ++r) if (rightEdge.at(r) + 6 <= left) { row = r; break; }
        if (row >= 0) rightEdge[row] = l.first + l.second / 2.0;
        out << row;
    }
    return out;
}

// A label's box centred on x, slid back inside [0, width] where it would
// run off an end.
QRectF labelBox(double x, int w, int y, int h, int width)
{
    double left = x - w / 2.0;
    left = qBound(2.0, left, double(width - w - 2));
    return QRectF(left, y, w, h);
}

// A metre step for the scale: 1, 2 or 5 x 10^k, at least `minM`.
double niceStep(double minM)
{
    double p = 1.0;
    while (p * 10.0 <= minM) p *= 10.0;
    for (double f : { 1.0, 2.0, 5.0, 10.0 }) if (p * f >= minM) return p * f;
    return p * 10.0;
}

}  // namespace

bool TrackDiagramCanvas::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        const QPoint pos = static_cast<QHelpEvent *>(e)->pos();
        QStringList lines;
        for (const Hit &h : m_hits) if (h.rect.contains(pos)) lines << h.text;
        if (lines.isEmpty()) { QToolTip::hideText(); e->ignore(); }
        else QToolTip::showText(static_cast<QHelpEvent *>(e)->globalPos(), lines.join(QLatin1Char('\n')), this);
        return true;
    }
    return QWidget::event(e);
}

void TrackDiagramCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), palette().base());
    const QColor text = palette().color(QPalette::Text);
    const QColor ink = UiColor::muted(), frame = UiColor::frame(), grid = UiColor::grid();
    m_hits.clear();
    m_labels.clear();

    if (m_d.isEmpty()) {
        p.setPen(ink);
        p.drawText(rect(), Qt::AlignCenter, tr("No location in this tab: it needs @dmi or @lsrp frames"));
        return;
    }
    if (!m_d.hasLocation()) {
        p.setPen(ink);
        p.drawText(rect(), Qt::AlignCenter,
                   tr("No location to draw: every frame in this tab reports 0 m\n"
                      "(the loco had not localised on an RFID tag)"));
        return;
    }

    const QRect track = trackRect();
    const qint64 cursorMs = cursorMsOrLast();
    const QFontMetrics fm(p.font());
    QFont small = p.font();
    small.setPointSizeF(qMax(6.0, small.pointSizeF() - 1.0));
    const QFontMetrics sfm(small);
    const int lh = sfm.height();

    // ---- vertical layout: one block, centred in whatever height there is -------
    //   readout | event lane | tag labels (2 rows) | tags | RAIL | signals
    //   | signal labels (2 rows) | scale | legend
    const int readoutH = fm.height() + 10;
    const int eventH = 26, tagLabH = 3 * lh, tagH = 14, signalH = 44, sigLabH = 2 * lh, scaleH = lh + 12, legendH = lh + 8;
    const int profH = profileLanesHeight(lh);
    const bool layoutRow = !m_overlay.tags.isEmpty() || !m_overlay.signalMarks.isEmpty();   // session 209: its own legend row
    const int blockH = readoutH + eventH + tagLabH + tagH + 22 + signalH + sigLabH + profH + scaleH + legendH * (layoutRow ? 2 : 1);
    int y = qMax(0, (height() - blockH) / 2);
    const int readoutY = y;              y += readoutH;
    const int eventTop = y;              y += eventH;
    const int tagLabTop = y;             y += tagLabH;
    const int tagY = y + tagH / 2;       y += tagH;
    const int midY = y + 11;             y += 22;
    const int signalTop = y;             y += signalH;
    const int sigLabTop = y;             y += sigLabH;
    const int profTop = y;               y += profH;
    const int scaleY = y;                y += scaleH;
    const int legendY = y;

    // ---- the loco at the cursor: readout first, drawn last ------------------------
    const bool haveCursor = m_cursorIndex >= 0 && m_cursorIndex < m_d.trace.samples.size();
    const SpeedDistance::Sample &smp = haveCursor ? m_d.trace.samples.at(m_cursorIndex) : m_d.trace.samples.last();
    const bool locoKnown = TrackDiagram::locationKnown(smp);
    {
        QFont bold = p.font();
        bold.setWeight(QFont::DemiBold);
        p.setFont(bold);
        p.setPen(text);
        const QString when = QDateTime::fromMSecsSinceEpoch(cursorMs).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        const int ww = QFontMetrics(bold).horizontalAdvance(when);
        p.drawText(QRect(track.left(), readoutY, ww + 4, readoutH), Qt::AlignLeft | Qt::AlignVCenter, when);
        p.setFont(font());
        QStringList parts;
        if (locoKnown) parts << tr("loco at %1").arg(km(smp.locM));
        else parts << tr("location not known (0 m: not localised)");
        parts << tr("%1 km/h").arg(smp.speedKmh, 0, 'f', 0);
        const QString line = TrackDiagram::lineAt(m_d, m_overlay, cursorMs);
        if (!line.isEmpty()) parts << tr("line %1").arg(line);
        if (!smp.mode.isEmpty()) parts << modeText(smp.mode);
        p.setPen(locoKnown ? text : UiColor::warning());
        p.drawText(QRect(track.left() + ww + 16, readoutY, track.width() - ww - 16, readoutH),
                   Qt::AlignLeft | Qt::AlignVCenter, parts.join(QStringLiteral("  ·  ")));
    }
    p.setFont(small);

    // ---- the rail and the scale -------------------------------------------------------
    p.setPen(QPen(frame, 2));
    p.drawLine(track.left(), midY, track.right(), midY);
    p.setPen(QPen(grid, 1));
    for (int x = track.left(); x <= track.right(); x += 14) p.drawLine(x, midY - 5, x, midY + 5);
    {
        const double span = m_d.maxLocM - m_d.minLocM;
        const double perPx = span / qMax(1, track.width());
        const double step = niceStep(perPx * qMax(90, sfm.horizontalAdvance(QStringLiteral("888.888 km")) + 24));
        const int decimals = step >= 1000.0 ? 0 : (step >= 100.0 ? 1 : (step >= 10.0 ? 2 : 3));
        p.setPen(QPen(frame, 1));
        p.drawLine(track.left(), scaleY, track.right(), scaleY);
        for (double m = std::ceil(m_d.minLocM / step) * step; m <= m_d.maxLocM + 1e-6; m += step) {
            const double x = xOf(m);
            p.setPen(QPen(frame, 1));
            p.drawLine(QPointF(x, scaleY), QPointF(x, scaleY + 4));
            p.setPen(ink);
            p.drawText(QRectF(x - 50, scaleY + 5, 100, lh), Qt::AlignCenter,
                       QString::number(m / 1000.0, 'f', decimals) + QStringLiteral(" km"));
        }
    }

    // ---- RFID tags, and the layout's tags this run did not read (session 209) ---------------
    {
        struct Mark { double x; QString label; int read; int layout; };   // index into m_d.tags / m_overlay.tags, or -1
        QVector<Mark> marks;
        for (int i = 0; i < m_d.tags.size(); ++i)
            marks << Mark{ xOf(m_d.tags.at(i).locM), QString::number(m_d.tags.at(i).uniqueId), i, -1 };
        for (int i = 0; i < m_overlay.tags.size(); ++i) {
            const TrackDiagram::LayoutOverlay::Tag &t = m_overlay.tags.at(i);
            if (t.read || t.locM < m_d.minLocM || t.locM > m_d.maxLocM) continue;
            marks << Mark{ xOf(t.locM), t.name, -1, i };
        }
        std::sort(marks.begin(), marks.end(), [](const Mark &a, const Mark &b) { return a.x < b.x; });
        QVector<QPair<double, int>> sorted;
        for (const Mark &m : marks) sorted << qMakePair(m.x, sfm.horizontalAdvance(m.label));
        const QVector<int> rows = stackLabels(sorted, 3);
        for (int k = 0; k < marks.size(); ++k) {
            const Mark &m = marks.at(k);
            const double x = m.x;
            QPolygonF diamond;
            diamond << QPointF(x, tagY - 5) << QPointF(x + 5, tagY) << QPointF(x, tagY + 5) << QPointF(x - 5, tagY);
            bool past = false;
            if (m.read >= 0) {
                const TrackDiagram::RfidMark &t = m_d.tags.at(m.read);
                past = t.epochMs <= cursorMs;
                const QColor c = past ? UiColor::series(0) : faded(ink);
                p.setPen(c);
                p.setBrush(c);
                p.drawPolygon(diamond);
                QString tip = tr("RFID tag %1 at %2, read %3").arg(t.uniqueId).arg(km(t.locM), hms(t.epochMs));
                const QString line = m_overlay.lineOfUnique.value(t.uniqueId);
                if (!line.isEmpty()) tip += tr(", line %1").arg(line);
                m_hits << Hit{ QRectF(x - 7, tagLabTop, 14, tagY + 7 - tagLabTop).toAlignedRect(), tip };
            } else {
                const TrackDiagram::LayoutOverlay::Tag &t = m_overlay.tags.at(m.layout);
                p.setPen(QPen(UiColor::warning(), 1.5));
                p.setBrush(Qt::NoBrush);
                p.drawPolygon(diamond);
                m_hits << Hit{ QRectF(x - 7, tagLabTop, 14, tagY + 7 - tagLabTop).toAlignedRect(),
                               tr("Layout tag %1 at %2, line %3: not read in this run").arg(t.name, km(t.locM), t.line) };
            }
            if (rows.at(k) >= 0) {
                p.setPen(m.read >= 0 ? (past ? text : ink) : UiColor::warning());
                const int ly = tagLabTop + (2 - rows.at(k)) * lh;   // row 0 nearest the tag
                const QRectF box = labelBox(x, sorted.at(k).second + 4, ly, lh, width());
                p.drawText(box, Qt::AlignCenter, m.label);
                m_labels << box.toAlignedRect();
            }
        }
    }

    // ---- signals and their MA end ----------------------------------------------------------
    {
        // Session 209: the layout's signals the DMI never named, as grey posts.
        QSet<QString> named;
        for (const TrackDiagram::SignalMark &s : m_d.signalMarks) named.insert(s.name);
        QVector<const TrackDiagram::LayoutOverlay::Sig *> extra;
        for (const TrackDiagram::LayoutOverlay::Sig &s : m_overlay.signalMarks)
            if (!named.contains(s.name) && s.locM >= m_d.minLocM && s.locM <= m_d.maxLocM) extra << &s;
        QVector<QPair<double, int>> xw;
        for (const TrackDiagram::SignalMark &s : m_d.signalMarks)   // sorted by location in build()
            xw << qMakePair(xOf(s.locM), sfm.horizontalAdvance(s.name));
        for (const auto *s : extra) xw << qMakePair(xOf(s->locM), sfm.horizontalAdvance(s->name));
        QVector<int> order(xw.size());
        for (int i = 0; i < order.size(); ++i) order[i] = i;
        // Stacked from the direction of travel's end, as before.
        std::sort(order.begin(), order.end(), [&xw, this](int a, int b) {
            return m_d.trace.direction < 0 ? xw.at(a).first > xw.at(b).first : xw.at(a).first < xw.at(b).first; });
        QVector<QPair<double, int>> sortedXw;
        for (int i : order) sortedXw << xw.at(i);
        const QVector<int> sortedRows = stackLabels(sortedXw, 2);
        QVector<int> rows(xw.size());
        for (int k = 0; k < order.size(); ++k) rows[order.at(k)] = sortedRows.at(k);
        for (int k = 0; k < extra.size(); ++k) {
            const TrackDiagram::LayoutOverlay::Sig &s = *extra.at(k);
            const double x = xOf(s.locM);
            p.setPen(QPen(ink, 2));
            p.drawLine(QPointF(x, signalTop + 2), QPointF(x, signalTop + signalH - 6));
            const int row = rows.at(m_d.signalMarks.size() + k);
            if (row >= 0) {
                const QRectF box = labelBox(x, sfm.horizontalAdvance(s.name) + 4, sigLabTop + row * lh, lh, width());
                p.drawText(box, Qt::AlignCenter, s.name);
                m_labels << box.toAlignedRect();
            }
            m_hits << Hit{ QRectF(x - 7, signalTop - 4, 14, signalH + sigLabH).toAlignedRect(),
                           tr("Layout signal %1 at foot tag %2 (%3): not named on the DMI in this run")
                               .arg(s.name, s.footTag, km(s.locM)) };
        }
        for (int i = 0; i < m_d.signalMarks.size(); ++i) {
            const TrackDiagram::SignalMark &s = m_d.signalMarks.at(i);
            const double x = xOf(s.locM);
            const bool past = s.epochMs <= cursorMs;
            const QColor c = past ? aspectColor(s.aspect) : faded(aspectColor(s.aspect));
            p.setPen(QPen(c, 3));
            p.drawLine(QPointF(x, signalTop + 2), QPointF(x, signalTop + signalH - 6));
            p.setBrush(c);
            p.drawEllipse(QPointF(x, signalTop + 2), 5, 5);
            if (rows.at(i) >= 0) {
                p.setPen(past ? text : ink);
                const QRectF box = labelBox(x, sfm.horizontalAdvance(s.name) + 4, sigLabTop + rows.at(i) * lh, lh, width());
                p.drawText(box, Qt::AlignCenter, s.name);
                m_labels << box.toAlignedRect();
            }
            QString tip = tr("Signal %1 at %2 (reading of %3)").arg(s.name, km(s.locM), hms(s.epochMs));
            if (s.hasMa) {
                const double xma = xOf(s.maEndLocM);
                const QColor mc = past ? UiColor::accent() : faded(UiColor::accent());
                const int my = signalTop + signalH / 2;
                p.setPen(QPen(mc, 1, Qt::DashLine));
                p.drawLine(QPointF(x, my), QPointF(xma, my));
                p.setBrush(mc);
                p.setPen(QPen(mc, 1));
                QPolygonF flag;
                const double dir = m_d.trace.direction < 0 ? -1.0 : 1.0;
                flag << QPointF(xma, my - 7) << QPointF(xma + 8 * dir, my - 3) << QPointF(xma, my + 1);
                p.drawPolygon(flag);
                p.drawLine(QPointF(xma, my - 7), QPointF(xma, my + 5));
                tip += QLatin1Char('\n') + tr("Movement authority ends at %1").arg(km(s.maEndLocM));
                m_hits << Hit{ QRectF(xma - 6, my - 9, 16, 16).toAlignedRect(),
                               tr("Movement authority end of %1 at %2").arg(s.name, km(s.maEndLocM)) };
            }
            m_hits << Hit{ QRectF(x - 7, signalTop - 4, 14, signalH + sigLabH).toAlignedRect(), tip };
        }
    }

    if (profH > 0) paintProfile(p, profTop, lh, cursorMs);

    // ---- events ---------------------------------------------------------------------------------
    for (const TrackDiagram::EventMark &e : m_d.events) {
        const double x = xOf(e.locM);
        const bool past = e.epochMs <= cursorMs;
        const QColor c = past ? eventColor(e.kind) : faded(eventColor(e.kind));
        p.setPen(QPen(c, 2));
        p.drawLine(QPointF(x, eventTop + 4), QPointF(x, eventTop + eventH - 2));
        m_hits << Hit{ QRectF(x - 4, eventTop, 8, eventH).toAlignedRect(),
                       tr("%1 at %2, %3").arg(e.label, km(e.locM), hms(e.epochMs)) };
    }

    // ---- the loco, at the cursor (only where its location is known) ------------------------------
    if (locoKnown) {
        const double x = xOf(smp.locM);
        const QColor lc = UiColor::accent();
        p.setPen(QPen(lc, 2));
        p.setBrush(lc);
        p.drawRect(QRectF(x - 9, midY - 9, 18, 18));
        if (m_d.trace.direction != 0) {
            const double d = m_d.trace.direction > 0 ? 1.0 : -1.0;
            QPolygonF nose;
            nose << QPointF(x + 11 * d, midY - 6) << QPointF(x + 18 * d, midY) << QPointF(x + 11 * d, midY + 6);
            p.drawPolygon(nose);
        }
        m_hits << Hit{ QRectF(x - 12, midY - 12, 24, 24).toAlignedRect(),
                       tr("Loco at %1, %2 km/h, %3").arg(km(smp.locM)).arg(smp.speedKmh, 0, 'f', 0).arg(hms(smp.epochMs)) };
    }

    // ---- legend ------------------------------------------------------------------------------------
    {
        int x = track.left();
        const int cy = legendY + legendH / 2;
        auto label = [&](const QString &t) {
            p.setPen(ink);
            const int w = sfm.horizontalAdvance(t);
            p.drawText(QRect(x, legendY, w + 2, legendH), Qt::AlignLeft | Qt::AlignVCenter, t);
            x += w + 18;
        };
        p.setPen(UiColor::accent()); p.setBrush(UiColor::accent());
        p.drawRect(QRectF(x, cy - 5, 10, 10)); x += 15; label(tr("loco"));
        p.setPen(UiColor::series(0)); p.setBrush(UiColor::series(0));
        QPolygonF dm; dm << QPointF(x + 5, cy - 5) << QPointF(x + 10, cy) << QPointF(x + 5, cy + 5) << QPointF(x, cy);
        p.drawPolygon(dm); x += 15; label(tr("RFID tag"));

        p.setPen(QPen(UiColor::signalLamp(QStringLiteral("green")), 3));
        p.drawLine(x + 4, cy - 4, x + 4, cy + 6); x += 12; label(tr("signal (aspect colour)"));
        p.setPen(QPen(UiColor::accent(), 1, Qt::DashLine));
        p.drawLine(x, cy, x + 14, cy); x += 19; label(tr("movement authority end"));
        for (const auto &k : { qMakePair(QStringLiteral("mode"), tr("mode change")),
                               qMakePair(QStringLiteral("overspeed"), tr("overspeed / SoS")),
                               qMakePair(QStringLiteral("emergency"), tr("emergency / fault / collision")) }) {
            p.setPen(QPen(eventColor(k.first), 2));
            p.drawLine(x + 3, cy - 6, x + 3, cy + 6); x += 10; label(k.second);
        }
    }
    if (layoutRow) {
        const int ry = legendY + legendH, cy = ry + legendH / 2;
        int x = track.left();
        auto label = [&](const QString &t) {
            p.setPen(ink);
            const int w = sfm.horizontalAdvance(t);
            p.drawText(QRect(x, ry, w + 2, legendH), Qt::AlignLeft | Qt::AlignVCenter, t);
            x += w + 18;
        };
        label(tr("Layout %1:").arg(m_overlay.source));
        p.setPen(QPen(UiColor::warning(), 1.5)); p.setBrush(Qt::NoBrush);
        QPolygonF lt; lt << QPointF(x + 5, cy - 5) << QPointF(x + 10, cy) << QPointF(x + 5, cy + 5) << QPointF(x, cy);
        p.drawPolygon(lt); x += 15; label(tr("tag not read in this run"));
        p.setPen(QPen(ink, 2));
        p.drawLine(x + 4, cy - 4, x + 4, cy + 6); x += 12; label(tr("signal the DMI did not name"));
    }
}

// ---- SLRP profile lanes (session 207) -------------------------------------------------------

namespace {
constexpr int kProfileLanes = 5;   // SSP, gradient, TSR, track conditions, tag links
}

QSize TrackDiagramCanvas::minimumSizeHint() const
{
    QFont small = font();
    small.setPointSizeF(qMax(6.0, small.pointSizeF() - 1.0));
    return QSize(480, 330 + profileLanesHeight(QFontMetrics(small).height()));
}

int TrackDiagramCanvas::profileLanesHeight(int lh) const
{
    return m_d.profiles.isEmpty() ? 0 : (lh + 6) + kProfileLanes * (lh + 4) + 6;
}

void TrackDiagramCanvas::paintProfile(QPainter &p, int top, int lh, qint64 cursorMs)
{
    const QRect track = trackRect();
    const QColor text = palette().color(QPalette::Text), ink = UiColor::muted();
    const QFontMetrics sfm(p.font());
    const TrackDiagram::Profile *pr = m_d.profileAt(cursorMs);

    // caption: which profile, or why none is drawn
    QString caption;
    if (!pr) caption = tr("SLRP profile: none received yet at this moment");
    else if (!pr->placed) caption = tr("SLRP profile: reference tag %1 was not read in this tab, so it cannot be placed").arg(pr->refRfid);
    else caption = tr("SLRP profile in force: reference tag %1, profile id %2, from %3")
                       .arg(pr->refRfid).arg(pr->refProfId).arg(hms(pr->epochMs));
    p.setPen(pr && pr->placed ? text : ink);
    p.drawText(QRect(track.left(), top, track.width(), lh + 4), Qt::AlignLeft | Qt::AlignVCenter,
               sfm.elidedText(caption, Qt::ElideRight, track.width()));

    const int laneTop = top + lh + 6, laneH = lh + 4;
    const QStringList names{ tr("SSP"), tr("Grad"), tr("TSR"), tr("Cond"), tr("Tags") };
    for (int l = 0; l < kProfileLanes; ++l) {
        const int ly = laneTop + l * laneH;
        p.setPen(ink);
        p.drawText(QRect(2, ly, track.left() - 6, laneH - 2), Qt::AlignRight | Qt::AlignVCenter, names.at(l));
        p.setPen(QPen(UiColor::grid(), 1));
        p.drawLine(track.left(), ly + laneH - 2, track.right(), ly + laneH - 2);
    }
    if (!pr || !pr->placed) return;

    // A span, clipped to the rail; its label only where it fits.
    auto span = [&](int lane, const TrackDiagram::ProfileSpan &s, const QColor &c, bool acted) {
        double x0 = xOf(qMin(s.fromM, s.toM)), x1 = xOf(qMax(s.fromM, s.toM));
        if (x1 < track.left() || x0 > track.right()) return;
        x0 = qMax(x0, double(track.left()));
        x1 = qMin(x1, double(track.right()));
        const QRectF r(x0, laneTop + lane * laneH + 1, qMax(2.0, x1 - x0), laneH - 4);
        QColor fill = c;
        fill.setAlpha(acted ? 70 : 25);
        p.setBrush(fill);
        p.setPen(QPen(c, 1, acted ? Qt::SolidLine : Qt::DashLine));
        p.drawRect(r);
        if (sfm.horizontalAdvance(s.label) + 4 <= r.width()) {
            p.setPen(acted ? text : ink);
            p.drawText(r, Qt::AlignCenter, s.label);
        }
        QString tip = tr("%1, %2 to %3").arg(s.tip, km(s.fromM), km(s.toM));
        if (!acted) tip += QLatin1Char('\n') + tr("Not acted on: the loco acts on TSR entries only when TSR_STATUS is 2");
        m_hits << Hit{ r.toAlignedRect(), tip };
    };
    for (const auto &s : pr->ssp)  span(0, s, UiColor::series(1), true);
    for (const auto &s : pr->grad) span(1, s, UiColor::series(2), true);
    for (const auto &s : pr->tsr)  span(2, s, UiColor::series(3), pr->tsrStatus == 2);
    for (const auto &s : pr->cond) span(3, s, UiColor::series(4), true);

    // tag links: ticks, labels stacked in one row
    {
        const int ly = laneTop + 4 * laneH;
        QVector<QPair<double, int>> xw;
        QVector<const TrackDiagram::ProfileMark *> marks;
        for (const auto &m : pr->tagLinks) {
            const double x = xOf(m.locM);
            if (x < track.left() || x > track.right()) continue;
            xw << qMakePair(x, sfm.horizontalAdvance(m.label) + 10);
            marks << &m;
        }
        QVector<int> order(xw.size());
        for (int i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&xw](int a, int b) { return xw.at(a).first < xw.at(b).first; });
        QVector<QPair<double, int>> sorted;
        for (int i : order) sorted << xw.at(i);
        const QVector<int> rows = stackLabels(sorted, 1);
        for (int k = 0; k < order.size(); ++k) {
            const TrackDiagram::ProfileMark &m = *marks.at(order.at(k));
            const double x = sorted.at(k).first;
            p.setPen(QPen(UiColor::series(0), 2));
            p.drawLine(QPointF(x, ly + 2), QPointF(x, ly + laneH - 4));
            if (rows.at(k) >= 0) {
                p.setPen(text);
                p.drawText(QRectF(x + 3, ly, sorted.at(k).second, laneH - 2), Qt::AlignLeft | Qt::AlignVCenter, m.label);
            }
            m_hits << Hit{ QRectF(x - 4, ly, 8, laneH).toAlignedRect(),
                           tr("Linked tag %1 expected at %2").arg(m.label, km(m.locM)) };
        }
    }

    // the start signal the lanes are measured from, and the MA end
    auto marker = [&](double locM, const QColor &c, const QString &tip) {
        const double x = xOf(locM);
        if (x < track.left() || x > track.right()) return;
        p.setPen(QPen(c, 1, Qt::DashLine));
        p.drawLine(QPointF(x, laneTop), QPointF(x, laneTop + kProfileLanes * laneH - 2));
        m_hits << Hit{ QRectF(x - 3, laneTop, 6, kProfileLanes * laneH).toAlignedRect(), tip };
    };
    marker(pr->startSignalM, text, tr("Profile start signal at %1 (from reference tag %2, PKT_DIR %3)")
           .arg(km(pr->startSignalM)).arg(pr->refRfid).arg(pr->pktDir));
    if (pr->haveMa) marker(pr->maEndM, UiColor::accent(), tr("Movement authority end (SLRP) at %1").arg(km(pr->maEndM)));
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
    resize(1100, 560);

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
    auto *layoutBtn = new QPushButton(tr("Station layout…"), this);
    layoutBtn->setToolTip(tr("Lay a station layout (Tools \u25B8 Station Layout, or the Python tool's .xlsx) over the "
                             "run: the tags it expects on the lines this run used, signal names, the line the loco is on"));
    m_hideLayout = new QPushButton(tr("Hide layout"), this);
    m_hideLayout->setEnabled(false);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_status, 1);
    buttons->addWidget(layoutBtn);
    buttons->addWidget(m_hideLayout);
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

    // Session 210: the built-in default, or a file.
    auto *layoutMenu = new QMenu(layoutBtn);
    layoutMenu->addAction(tr("Default layout (station.xlsx)"), this, [this]() { loadLayout(StationLayout::defaultFile()); });
    layoutMenu->addAction(tr("Open file…"), this, [this]() {
        const QString last = Settings::stationLayoutLastFile();
        const QString path = QFileDialog::getOpenFileName(this, tr("Station layout"),
                                                          last.startsWith(QLatin1Char(':')) ? QString() : last,
                                                          tr("Station layout (*.json *.xlsx)"));
        if (!path.isEmpty()) loadLayout(path);
    });
    layoutBtn->setMenu(layoutMenu);
    connect(m_hideLayout, &QPushButton::clicked, this, &TrackDiagramWindow::clearLayout);

    rebuild();
}

bool TrackDiagramWindow::loadLayout(const QString &path)
{
    StationLayout::Layout l;
    QString err;
    if (!StationLayout::load(path, &l, &err)) {
        m_status->fail(tr("Could not read %1: %2").arg(QFileInfo(path).fileName(), err));
        return false;
    }
    m_layout = l;
    m_layoutFile = QFileInfo(path).fileName();
    Settings::setStationLayoutLastFile(path);
    rebuild();
    return true;
}

void TrackDiagramWindow::clearLayout()
{
    m_layout = StationLayout::Layout();
    m_layoutFile.clear();
    rebuild();
}

void TrackDiagramWindow::rebuild()
{
    m_diagram = TrackDiagram::build(m_model, m_tabKey, m_tabName);
    m_canvas->setDiagram(m_diagram);
    const TrackDiagram::LayoutOverlay ov = m_layoutFile.isEmpty() ? TrackDiagram::LayoutOverlay()
                                                                  : TrackDiagram::overlay(m_diagram, m_layout, m_layoutFile);
    m_canvas->setOverlay(ov);
    m_hideLayout->setEnabled(!m_layoutFile.isEmpty());
    const int last = m_diagram.trace.samples.size() - 1;
    m_slider->blockSignals(true);
    m_slider->setRange(0, qMax(0, last));
    m_slider->setValue(qMax(0, last));
    m_slider->blockSignals(false);
    m_slider->setEnabled(last > 0);
    m_play->setEnabled(last > 0);
    auto count = [](int n, const QString &one, const QString &many) {
        return QStringLiteral("%1 %2").arg(n).arg(n == 1 ? one : many);
    };
    if (m_diagram.isEmpty() || !m_diagram.hasLocation()) {
        m_status->state(tr("No location in this tab"));
    } else {
        QStringList parts{ count(m_diagram.tags.size(), tr("RFID tag"), tr("RFID tags")),
                           count(m_diagram.signalMarks.size(), tr("signal"), tr("signals")),
                           count(m_diagram.events.size(), tr("event"), tr("events")) };
        if (!m_diagram.profiles.isEmpty()) {
            int placed = 0;
            for (const TrackDiagram::Profile &pr : m_diagram.profiles) placed += pr.placed ? 1 : 0;
            parts << tr("SLRP: %1 of %2 placed").arg(placed).arg(m_diagram.profiles.size());
        }
        // Said, so a frame left off the rail is not mistaken for a missing
        // one; the why is in the tooltip, to keep the line to one line.
        if (m_diagram.unknownSamples > 0) {
            QString lost = tr("not localised (0 m): %1 of %2 frames").arg(m_diagram.unknownSamples)
                                                                  .arg(m_diagram.trace.samples.size());
            if (m_diagram.unpinnedEvents > 0)
                lost += QStringLiteral(", ") + count(m_diagram.unpinnedEvents, tr("event"), tr("events"));
            parts << lost;
        }
        if (!m_layoutFile.isEmpty()) {
            if (ov.lines.isEmpty())
                parts << tr("layout %1: none of this run's tags are in it").arg(m_layoutFile);
            else
                parts << tr("layout %1, line %2: %3 of its %4 tags here read").arg(m_layoutFile, ov.lines.join(QStringLiteral(", ")))
                             .arg(ov.tags.size() - ov.unreadTags()).arg(ov.tags.size());
        }
        m_status->state(parts.join(QStringLiteral(" · ")));
    }
    m_status->setToolTip(m_diagram.unknownSamples > 0
        ? tr("A loco that has not localised on an RFID tag reports 0 m.\n"
             "Those frames are left off the rail, and the loco is not drawn\n"
             "while the time cursor is on one. Events at such a moment are\n"
             "not pinned: the last known location may be from before a\n"
             "Stand_By, somewhere the loco no longer was.")
        : QString());
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
