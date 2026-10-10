#include "routestrip.h"

#include "rfidcheck.h"
#include "uicolors.h"

#include <algorithm>

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace {
constexpr int kMargin = 24;
constexpr int kAxisY = 64;
}  // namespace

RouteStrip::RouteStrip(QWidget *parent)
    : QWidget(parent)
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
        m_marks.append({ i, s.absLoc, s.duplicate, s.crcOk, s.type == 12, RfidTag::nameOf(r.tags.at(i).bytes) });
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
    const double w = qMax(1, width() - 2 * kMargin);
    if (m_hi == m_lo) return kMargin + w / 2;
    return kMargin + w * double(loc - m_lo) / double(m_hi - m_lo);
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
    const int row = rowAt(e->pos());
    if (row >= 0) emit rowClicked(row);
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
    p.drawText(QRect(0, kAxisY + 6, width(), fm.height()), Qt::AlignLeft, QStringLiteral("%1 m").arg(m_lo));
    p.drawText(QRect(0, kAxisY + 6, width(), fm.height()), Qt::AlignRight, QStringLiteral("%1 m").arg(m_hi));

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
        p.drawLine(x, kAxisY - 32, x, kAxisY - 46);
        p.drawEllipse(QPoint(x, kAxisY - 49), 3, 3);
        const int w = fm.horizontalAdvance(sg.second);
        const int tx = qMin(x + 6, width() - w - 2);
        if (tx > lastSignalEnd + 6 && tx > x - w) {
            p.drawText(tx, kAxisY - 44, sg.second);
            lastSignalEnd = tx + w;
        }
    }

    // Tags. Names of main tags, skipped where they would overlap.
    int lastLabelEnd = -1000;
    for (const Mark &m : m_marks) {
        const int x = int(xOf(m.loc));
        const QColor c = !m.crcOk ? UiColor::error() : m.adjust ? UiColor::accent() : ink;
        p.setPen(QPen(c, m.row == m_selected ? 3 : 1.5));
        p.drawLine(x, kAxisY, x, kAxisY - (m.dup ? 7 : 14));
        if (!m.dup) {
            const int w = fm.horizontalAdvance(m.name);
            if (x - w / 2 > lastLabelEnd + 4) {
                p.drawText(x - w / 2, kAxisY - 17, m.name);
                lastLabelEnd = x + w / 2;
            }
        }
        if (m.row == m_selected) {
            p.setPen(QPen(UiColor::selectedMark(), 2));
            p.drawEllipse(QPoint(x, kAxisY - 7), 9, 9);
        }
    }
}
