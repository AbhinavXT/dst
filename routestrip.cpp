#include "routestrip.h"

#include "rfidcheck.h"
#include "tinlayout.h"
#include "uicolors.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace {
constexpr int kMargin = 24;
constexpr int kAxisY = 64;
constexpr double kNone = std::numeric_limits<double>::quiet_NaN();
// The overlay's rows under the line (session 397).
constexpr int kStateY = kAxisY + 30;
constexpr int kLocoY = kAxisY + 60;
}  // namespace

RouteStrip::RouteStrip(QWidget *parent)
    : QWidget(parent), m_loco(kNone), m_pulsesLoco(kNone), m_window2From(kNone), m_window2To(kNone)
{
    setToolTip(tr("The route by absolute location. Click a tag to select it"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void RouteStrip::setRoute(const RfidTag::Route &r)
{
    m_marks.clear();
    m_signals.clear();
    m_dir = r.dir;
    QHash<int, qint64> mainLoc;
    for (int i = 0; i < r.tags.size(); ++i) {
        const RfidTag::Summary s = RfidTag::summary(r.tags.at(i).bytes);
        if (s.absLoc == RfidCheck::kNotApplicable) continue;
        m_marks.append({ i, s.absLoc, s.duplicate, s.crcOk, RfidTag::nameOf(r.tags.at(i).bytes), s.unique, s.tinNom,
                         TinLayout::notation(s.type, s.placement) });
        if (!s.duplicate && !mainLoc.contains(s.unique)) mainLoc.insert(s.unique, s.absLoc);
    }
    for (const RfidTag::Signal &sg : r.signalList) {
        bool ok = false;
        const int id = sg.footTag.trimmed().toInt(&ok);
        if (ok && mainLoc.contains(id)) m_signals.append({ mainLoc.value(id), sg.name });
    }
    // Left to right, whatever the direction: names are skipped by overlap.
    std::sort(m_marks.begin(), m_marks.end(), [](const Mark &a, const Mark &b) { return a.loc < b.loc; });
    std::sort(m_signals.begin(), m_signals.end(), [](const QPair<qint64, QString> &a, const QPair<qint64, QString> &b) {
        return a.first < b.first;
    });
    m_lo = m_hi = 0;
    for (int i = 0; i < m_marks.size(); ++i) {
        if (i == 0 || m_marks.at(i).loc < m_lo) m_lo = m_marks.at(i).loc;
        if (i == 0 || m_marks.at(i).loc > m_hi) m_hi = m_marks.at(i).loc;
    }
    update();
}

void RouteStrip::setSelectedRow(int row)
{
    m_selected = row;
    update();
}

int RouteStrip::drawnTags() const { return m_marks.size(); }

double RouteStrip::xOf(qint64 loc) const
{
    return xAt(double(loc));
}

double RouteStrip::xAt(double loc) const
{
    const double w = qMax(1, width() - 2 * kMargin);
    const double lo = viewLow(), hi = viewHigh();
    if (hi == lo) return kMargin + w / 2;
    return kMargin + w * (loc - lo) / (hi - lo);
}

double RouteStrip::locAt(double x) const
{
    const double w = qMax(1, width() - 2 * kMargin);
    const double lo = viewLow(), hi = viewHigh();
    return lo + (x - kMargin) / w * (hi - lo);
}

void RouteStrip::useOverlay()
{
    if (m_overlay) return;
    m_overlay = true;
    setFixedHeight(sizeHint().height());
    updateGeometry();
}

void RouteStrip::setView(double lo, double hi)
{
    m_viewSet = hi > lo;
    m_viewLo = lo;
    m_viewHi = hi;
    update();
}

void RouteStrip::clearView()
{
    m_viewSet = false;
    update();
}

void RouteStrip::setLoco(double loc, double pulsesLoc)
{
    useOverlay();
    m_loco = loc;
    m_pulsesLoco = pulsesLoc;
    update();
}

void RouteStrip::setTagStates(const QHash<int, int> &states)
{
    useOverlay();
    m_states = states;
    update();
}

void RouteStrip::setTagLabels(const QHash<int, QString> &labels)
{
    m_labels = labels;
    update();
}

void RouteStrip::setReader2Window(double from, double to)
{
    m_window2From = from;
    m_window2To = to;
    update();
}

int RouteStrip::rowAt(const QPoint &p) const
{
    int best = -1;
    double bestD = 6;
    for (const Mark &m : m_marks) {
        const double d = qAbs(xOf(m.loc) - p.x());
        if (d < bestD) { bestD = d; best = m.row; }
    }
    return best;
}

void RouteStrip::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;   // the right button is contextMenuEvent's
    const int row = rowAt(e->pos());
    if (row >= 0) emit rowClicked(row);
}

void RouteStrip::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
    const int row = rowAt(e->pos());
    if (row >= 0) emit rowDoubleClicked(row);
}

void RouteStrip::contextMenuEvent(QContextMenuEvent *e)
{
    emit contextRequested(rowAt(e->pos()), locAt(e->pos().x()), e->globalPos());
}

void RouteStrip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor ink = palette().color(QPalette::WindowText);
    const QColor muted = UiColor::muted();
    if (m_marks.isEmpty()) {
        p.setPen(muted);
        p.drawText(rect(), Qt::AlignCenter, tr("No tag with a location to draw"));
        return;
    }
    const QFontMetrics fm(font());

    // The line, and the two ends' locations.
    p.setPen(QPen(UiColor::frame(), 2));
    p.drawLine(kMargin, kAxisY, width() - kMargin, kAxisY);
    p.setPen(muted);
    const QString loText = QStringLiteral("%1 m").arg(qRound64(viewLow()));
    const QString hiText = QStringLiteral("%1 m").arg(qRound64(viewHigh()));
    p.drawText(QRect(0, kAxisY + 6, width(), fm.height()), Qt::AlignLeft, loText);
    p.drawText(QRect(0, kAxisY + 6, width(), fm.height()), Qt::AlignRight, hiText);

    // Direction of travel: an arrow in the middle under the line.
    if (m_dir == RfidTag::DirNominal || m_dir == RfidTag::DirReverse) {
        const int cx = width() / 2, y = kAxisY + 6 + fm.height() / 2;
        const int sgn = m_dir == RfidTag::DirNominal ? 1 : -1;
        p.setPen(QPen(UiColor::accent(), 2));
        p.drawLine(cx - 18 * sgn, y, cx + 18 * sgn, y);
        QPainterPath head;
        head.moveTo(cx + 18 * sgn, y);
        head.lineTo(cx + 12 * sgn, y - 4);
        head.lineTo(cx + 12 * sgn, y + 4);
        head.closeSubpath();
        p.fillPath(head, UiColor::accent());
    }

    // Signals: a short post with its name, above the line. A name that
    // would overlap the one before is left off (the post stays; the
    // Signals tab has them all); the last one keeps inside the widget.
    p.setPen(ink);
    int lastSignalEnd = -1000;
    for (const auto &sg : m_signals) {
        const int x = int(xOf(sg.first));
        if (x < 0 || x > width()) continue;
        p.drawLine(x, kAxisY - 32, x, kAxisY - 46);
        p.drawEllipse(QPoint(x, kAxisY - 49), 3, 3);
        const int w = fm.horizontalAdvance(sg.second);
        const int tx = qMin(x + 6, width() - w - 2);
        if (tx > lastSignalEnd + 6 && tx > x - w) {
            p.drawText(tx, kAxisY - 44, sg.second);
            lastSignalEnd = tx + w;
        }
    }

    // The line between the tags in its TIN colours (H2.17); TIN 0 hollow.
    for (int i = 0; i + 1 < m_marks.size(); ++i) {
        const double x1 = xOf(m_marks.at(i).loc), x2 = xOf(m_marks.at(i + 1).loc);
        const QColor c = TinLayout::tinColor(m_marks.at(i).tinNom);
        const QRectF band(x1, kAxisY - 3, x2 - x1, 6);
        if (c.isValid()) p.fillRect(band, c);
        else {
            p.setPen(QPen(UiColor::frame(), 1));
            p.setBrush(palette().color(QPalette::Base));
            p.drawRect(band);
        }
    }
    // "(N-…)" under each run of one TIN, where it fits between the end labels.
    int lastTinEnd = fm.horizontalAdvance(loText) + 4;
    const int tinRight = width() - fm.horizontalAdvance(hiText) - 4;
    for (int i = 0; i + 1 < m_marks.size();) {
        int j = i + 1;
        while (j + 1 < m_marks.size() && m_marks.at(j).tinNom == m_marks.at(i).tinNom) ++j;
        const int tin = m_marks.at(i).tinNom;
        if (tin > 0) {
            const QString label = TinLayout::tinLabel(tin);
            const int w = fm.horizontalAdvance(label);
            const int x = int((xOf(m_marks.at(i).loc) + xOf(m_marks.at(j).loc)) / 2) - w / 2;
            const bool arrow = m_dir == RfidTag::DirNominal || m_dir == RfidTag::DirReverse;
            const bool onArrow = arrow && x < width() / 2 + 24 && x + w > width() / 2 - 24;
            if (x > lastTinEnd + 6 && x + w < tinRight && !onArrow) {
                p.setPen(UiColor::withContrast(TinLayout::tinColor(tin), palette().color(QPalette::Base), 4.5));
                p.drawText(QRect(x, kAxisY + 6, w + 2, fm.height()), Qt::AlignLeft, label);
                lastTinEnd = x + w;
            }
        }
        i = j;
    }

    // Tags: one symbol per main + duplicate set (H2.15), its letter (H2.21);
    // a duplicate away from its main gets a small mark of its own. A tag
    // whose CRC-30 fails is edged in the error colour.
    QHash<int, qint64> mainLoc, dupLoc;
    for (const Mark &m : m_marks) (m.dup ? dupLoc : mainLoc).insert(m.unique, m.loc);
    int lastLabelEnd = -1000;
    for (const Mark &m : m_marks) {
        const int x = int(xOf(m.loc));
        if (x < -12 || x > width() + 12) continue;
        const QColor edge = !m.crcOk ? UiColor::error() : ink;
        const bool selected = m.row == m_selected;
        if (m.dup && mainLoc.contains(m.unique)) {
            if (mainLoc.value(m.unique) != m.loc) {
                p.setPen(QPen(edge, 1.2));
                p.setBrush(palette().color(QPalette::Base));
                p.drawEllipse(QPointF(x, kAxisY), 3, 3);
            } else if (!m.crcOk) {
                // Hidden under its main's symbol: its CRC failure must still show.
                TinLayout::drawTag(p, QPointF(x, kAxisY), m.letter, TinLayout::Box, edge, 2);
            }
        } else {
            const TinLayout::Shape shape = TinLayout::shapeOf(m.loc, dupLoc.value(m.unique), !m.dup && dupLoc.contains(m.unique));
            TinLayout::drawTag(p, QPointF(x, kAxisY), m.letter, shape, edge, !m.crcOk ? 2 : 1.2);
            const QString id = TinLayout::idText(m.name);
            const int w = fm.horizontalAdvance(id) + 6;
            if (x - w / 2 > lastLabelEnd + 4) {
                TinLayout::drawIdBox(p, QPointF(x, kAxisY - 10), id);
                lastLabelEnd = x + w / 2;
            }
        }
        if (selected) {
            p.setPen(QPen(UiColor::selectedMark(), 2));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QPoint(x, kAxisY), 12, 12);
        }
    }
    if (m_overlay) paintOverlay(p);
}

// Session 397: what the Loco Simulator adds under the line.
void RouteStrip::paintOverlay(QPainter &p)
{
    const QColor ink = palette().color(QPalette::WindowText);
    const QColor base = palette().color(QPalette::Base);
    const QColor theme = UiColor::withContrast(palette().color(QPalette::Highlight), palette().color(QPalette::Window), 3.0);
    QFont small = font();
    small.setPointSizeF(qMax(6.5, small.pointSizeF() - 1.5));
    p.setFont(small);
    const QFontMetricsF fm(small);
    const int sgn = m_dir == RfidTag::DirReverse ? -1 : 1;

    // Reader 2's window: where it sends the main tag again, 20-24 m past it.
    if (!std::isnan(m_window2From) && !std::isnan(m_window2To)) {
        const double x1 = xAt(qMin(m_window2From, m_window2To)), x2 = xAt(qMax(m_window2From, m_window2To));
        QColor band = theme;
        band.setAlpha(70);
        p.fillRect(QRectF(x1, kAxisY - 12, qMax(2.0, x2 - x1), 24), band);
    }

    // Each tag's state: which readers sent it, or skipped. Mains on one row,
    // duplicates on the next; a chip that would overlap the one before is left off.
    double lastEnd[2] = { -1000, -1000 };
    for (const Mark &m : m_marks) {
        const int state = m_states.value(m.row);
        if (!state) continue;
        const double x = xOf(m.loc);
        if (x < 0 || x > width()) continue;
        QString text;
        if (state & Skipped) text = tr("skip");
        else text = QString((state & SentOn1) ? QStringLiteral("1") : QString())
                    + ((state & SentOn2) ? QStringLiteral("2") : QString());
        // Session 405: a fault set on the tag, before what was sent.
        if (!(state & Skipped) && (state & Faulted))
            text = (m_labels.value(m.row, tr("fault")) + QLatin1Char(' ') + text).trimmed();
        const bool warn = state & (Skipped | Faulted);
        const int line = m.dup ? 1 : 0;
        const double w = fm.horizontalAdvance(text) + 8, h = fm.height() + 2;
        const QRectF chip(x - w / 2, kStateY + line * (h + 2) - h / 2, w, h);
        if (chip.left() < lastEnd[line] + 2) continue;
        lastEnd[line] = chip.right();
        const QColor fill = warn ? UiColor::warning() : UiColor::ok();
        p.setPen(QPen(fill, 1.2));
        p.setBrush(warn ? base : fill);
        p.drawRoundedRect(chip, 3, 3);
        p.setPen(warn ? UiColor::withContrast(fill, base, 4.5)
                      : UiColor::withContrast(base, fill, 4.5));
        p.drawText(chip, Qt::AlignCenter, text);
    }

    // The loco: its nose at its location, pointing the way it runs; where the
    // pulses put it as an outline.
    const auto drawLoco = [&](double loc, bool filled) {
        if (std::isnan(loc)) return;
        const double nose = xAt(loc);
        if (nose < -60 || nose > width() + 60) return;
        const double len = 40, h = 14;
        QPainterPath body;
        body.moveTo(nose, kLocoY);
        body.lineTo(nose - sgn * 8, kLocoY - h / 2);
        body.lineTo(nose - sgn * len, kLocoY - h / 2);
        body.lineTo(nose - sgn * len, kLocoY + h / 2);
        body.lineTo(nose - sgn * 8, kLocoY + h / 2);
        body.closeSubpath();
        if (filled) {
            p.setPen(QPen(theme, 1.5));
            p.drawLine(QPointF(nose, kAxisY + 4), QPointF(nose, kLocoY - h / 2));
            p.fillPath(body, theme);
            p.setPen(QPen(ink, 1));
            p.setBrush(Qt::NoBrush);
            p.drawPath(body);
        } else {
            p.setPen(QPen(ink, 1.4, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawPath(body);
        }
    };
    drawLoco(m_loco, true);
    drawLoco(m_pulsesLoco, false);
}
