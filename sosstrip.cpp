#include "sosstrip.h"

#include "capturedecoder.h"
#include "fieldplot.h"
#include "logmodel.h"
#include "uicolors.h"

#include <QHelpEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kLabelW  = 112;   // lane names
constexpr int kTopH    = 18;    // station pins
constexpr int kBottomH = 46;    // scale + legend
constexpr int kPad     = 6;
constexpr int kBarH    = 10;

QString metres(double m) { return QStringLiteral("%1 m").arg(m, 0, 'f', 0); }

QColor withAlpha(QColor c, int a) { c.setAlpha(a); return c; }

QColor threatColor(quint8 threats)
{
    if (threats & (SosLog::BitHeadOn | SosLog::BitRearEnd)) return UiColor::error();
    if (threats) return UiColor::warning();
    return UiColor::muted();
}

}  // namespace

SosStrip::Config SosStrip::configFromLog(const LogModel *model)
{
    // The newest @linfo only, found from the end: decoding all of them (about
    // 12,000 in a day's tab) took 2.9 s, to use the last (session 193).
    Config c;
    if (!model) return c;
    for (int i = model->count() - 1; i >= 0; --i) {
        const LogEntryPtr e = model->entryAt(i);
        if (!e || captureTypeOf(e->text) != QLatin1String("linfo")) continue;
        const CaptureLine cap = CaptureDecoder::parseLine(e->text);
        if (!cap.valid || cap.bytes.isEmpty()) continue;
        QHash<QString, qint64> raw;
        CaptureDecoder::describe(cap, nullptr, 0, &raw);
        c.sosTriggerM = int(raw.value(QStringLiteral("sos_trigger_distance"), -1));
        c.collisionTriggerM = int(raw.value(QStringLiteral("collision_trigger_distance"), -1));
        c.cancelM = int(raw.value(QStringLiteral("sos_cancellation_distance"), -1));
        break;
    }
    return c;
}

SosStrip::SosStrip(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void SosStrip::setSnapshot(const SosLog::Snapshot *s)
{
    m_has = s != nullptr;
    if (s) m_s = *s;
    // The span: every source and station in view, and the SoS trigger band.
    double far = 1500.0;
    if (m_has) {
        for (const SosLog::Source &src : m_s.sources) {
            far = std::max(far, std::abs(double(src.locM) - m_s.ownLocM) + src.lengthM);
            far = std::max(far, std::abs(double(src.rawLocM) - m_s.ownLocM) + src.lengthM);
        }
        for (const SosLog::Station &st : m_s.stations)
            if (st.inUse) far = std::max(far, std::abs(double(st.absLocM) - m_s.ownLocM));
        far = std::max(far, double(m_s.ownLengthM) + 200.0);
    }
    if (m_cfg.sosTriggerM > 0) far = std::max(far, double(m_cfg.sosTriggerM));
    m_halfSpanM = std::min(15000.0, far * 1.12 + 100.0);
    update();
}

QVector<int> SosStrip::laneTins() const
{
    // The own line in the middle; the others, sorted, half above and half below.
    QVector<int> others;
    if (m_has)
        for (const SosLog::Source &src : m_s.sources)
            if (src.tin != m_s.ownTin && !others.contains(src.tin)) others << src.tin;
    std::sort(others.begin(), others.end());
    QVector<int> lanes;
    const int above = others.size() / 2;
    for (int i = 0; i < above; ++i) lanes << others.at(i);
    lanes << (m_has ? m_s.ownTin : 0);
    for (int i = above; i < others.size(); ++i) lanes << others.at(i);
    return lanes;
}

QStringList SosStrip::laneNames() const
{
    QStringList out;
    for (int tin : laneTins())
        out << (m_has && tin == m_s.ownTin ? tr("TIN %1 (own)").arg(tin) : tr("TIN %1").arg(tin));
    return out;
}

QString SosStrip::legend() const
{
    QString bands;
    if (!m_cfg.known())
        bands = tr("No @linfo in this log: trigger distances not drawn.");
    else
        bands = tr("Shaded: SoS trigger %1, collision trigger %2 (@linfo).")
                    .arg(m_cfg.sosTriggerM >= 0 ? metres(m_cfg.sosTriggerM) : tr("not logged"),
                         m_cfg.collisionTriggerM >= 0 ? metres(m_cfg.collisionTriggerM) : tr("not logged"));
    return bands + QLatin1Char(' ') + tr("Dashed: the ARP's own position.");
}

double SosStrip::xOf(double locM, const QRect &r) const
{
    const double lo = m_s.ownLocM - m_halfSpanM;
    return r.left() + (locM - lo) / (2 * m_halfSpanM) * r.width();
}

void SosStrip::paintEvent(QPaintEvent *)
{
    m_hits.clear();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), palette().color(QPalette::Base));
    const QColor text = palette().color(QPalette::Text);
    QFont small = font();
    small.setPointSizeF(qMax(7.0, small.pointSizeF() * 0.9));
    p.setFont(small);

    if (!m_has) {
        p.setPen(UiColor::muted());
        p.drawText(rect(), Qt::AlignCenter, tr("No SoS snapshot here."));
        return;
    }

    const QRect area(kLabelW, kTopH, width() - kLabelW - kPad, height() - kTopH - kBottomH);
    const QVector<int> lanes = laneTins();
    const int laneH = std::max(20, std::min(44, area.height() / std::max(1, int(lanes.size()))));
    const int lanesTop = area.top() + (area.height() - laneH * int(lanes.size())) / 2;
    auto laneY = [&](int tin) {
        const int i = std::max(0, int(lanes.indexOf(tin)));
        return lanesTop + i * laneH + laneH / 2;
    };

    // Bands around the own loco's front, across every lane.
    const double own = m_s.ownLocM;
    const QRect bandRect(area.left(), lanesTop, area.width(), laneH * int(lanes.size()));
    auto band = [&](int m, const QColor &c) {
        if (m <= 0) return;
        const double x1 = std::max<double>(area.left(), xOf(own - m, area));
        const double x2 = std::min<double>(area.right(), xOf(own + m, area));
        p.fillRect(QRectF(x1, bandRect.top(), x2 - x1, bandRect.height()), withAlpha(c, 28));
        p.setPen(QPen(withAlpha(c, 140), 1, Qt::DashLine));
        p.drawLine(QPointF(x1, bandRect.top()), QPointF(x1, bandRect.bottom()));
        p.drawLine(QPointF(x2, bandRect.top()), QPointF(x2, bandRect.bottom()));
    };
    band(m_cfg.sosTriggerM, UiColor::warning());
    band(m_cfg.collisionTriggerM, UiColor::error());

    // The lanes: a line each, named.
    for (int tin : lanes) {
        const int y = laneY(tin);
        p.setPen(QPen(UiColor::frame(), tin == m_s.ownTin ? 2 : 1));
        p.drawLine(area.left(), y, area.right(), y);
        p.setPen(tin == m_s.ownTin ? text : UiColor::muted());
        p.drawText(QRect(kPad, y - laneH / 2, kLabelW - 2 * kPad, laneH), Qt::AlignVCenter | Qt::AlignLeft,
                   tin == m_s.ownTin ? tr("TIN %1 (own)").arg(tin) : tr("TIN %1").arg(tin));
    }

    // A train: a bar of its length behind its front, an arrow at the front.
    auto train = [&](double front, int dir, int lengthM, int y, const QColor &fill, bool dashed,
                     bool outline, const QString &label, const QString &tip) {
        const double back = dir == 2 ? front + lengthM : front - lengthM;
        double x1 = xOf(std::min(front, back), area), x2 = xOf(std::max(front, back), area);
        const bool offLeft = x2 < area.left(), offRight = x1 > area.right();
        x1 = std::max<double>(x1, area.left());
        x2 = std::min<double>(x2, area.right());
        QRectF bar(x1, y - kBarH / 2.0, std::max(3.0, x2 - x1), kBarH);
        if (offLeft) bar.moveLeft(area.left());
        if (offRight) bar.moveRight(area.right());
        if (dashed) {
            p.setPen(QPen(fill, 1, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
        } else {
            p.setPen(outline ? QPen(text, 2) : Qt::NoPen);
            p.setBrush(fill);
        }
        p.drawRect(bar);
        if (!dashed && dir != 0 && !offLeft && !offRight) {
            const double fx = xOf(front, area);
            QPainterPath arrow;
            const double s = dir == 2 ? -1.0 : 1.0;
            arrow.moveTo(fx + s * 7, y);
            arrow.lineTo(fx, y - kBarH / 2.0 - 2);
            arrow.lineTo(fx, y + kBarH / 2.0 + 2);
            arrow.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawPath(arrow);
        }
        if (!label.isEmpty()) {
            p.setPen(outline ? text : fill.darker(110));
            // Centred on the bar, kept inside the drawing area.
            const int w = p.fontMetrics().horizontalAdvance(label) + 8;
            int lx = int(bar.center().x()) - w / 2;
            lx = std::max(area.left(), std::min(lx, area.right() - w));
            p.drawText(QRect(lx, y - kBarH / 2 - 15, w, 13), Qt::AlignHCenter | Qt::AlignBottom, label);
        }
        m_hits << Hit{ bar.toAlignedRect().adjusted(-3, -4, 3, 4), tip };
    };

    // The others first, so the own loco draws on top.
    for (const SosLog::Source &src : m_s.sources) {
        const int y = laneY(src.tin);
        const double gap = std::abs(double(src.locM) - own);
        const bool target = !m_s.aggStationWon && m_s.aggThreat != SosLog::ThreatNone && src.locoId == m_s.collisionLoco;
        if (src.check(SosLog::EvalAdjusted))
            train(src.rawLocM, src.rawDir, src.lengthM, y, threatColor(src.threats), true, false, QString(),
                  tr("Loco %1 as its ARP put it: %2 %3 (before SOSWithAdjustment)")
                      .arg(src.locoId).arg(metres(src.rawLocM), SosLog::dirName(src.rawDir)));
        QString label = tr("Loco %1").arg(src.locoId);
        if (src.threats) label += QStringLiteral(" · ") + SosLog::threatBitsText(src.threats);
        label += QStringLiteral(" · ") + metres(gap);
        if (target) label = QStringLiteral("◎ ") + label;
        train(src.locM, src.dir, src.lengthM, y, threatColor(src.threats), false, target, label,
              tr("Loco %1%2: %3 %4, TIN %5, %6 long; %7 from the own loco. Threats: %8. ARP status %9")
                  .arg(src.locoId).arg(target ? tr(" (the target)") : QString())
                  .arg(metres(src.locM), SosLog::dirName(src.dir)).arg(src.tin).arg(metres(src.lengthM))
                  .arg(metres(gap), SosLog::threatBitsText(src.threats), SosLog::emergencyName(src.emergency)));
    }
    train(own, m_s.ownDir, m_s.ownLengthM, laneY(m_s.ownTin), UiColor::accent(), false, false, tr("own loco"),
          tr("Own loco: %1 %2, TIN %3, %4 long").arg(metres(own), SosLog::dirName(m_s.ownDir)).arg(m_s.ownTin)
              .arg(metres(m_s.ownLengthM)));

    // Stations: a pin on the top edge.
    for (const SosLog::Station &st : m_s.stations) {
        if (!st.inUse) continue;
        const double x = xOf(st.absLocM, area);
        if (x < area.left() || x > area.right()) continue;
        const QColor c = st.addEmSos() ? UiColor::warning() : UiColor::muted();
        const bool target = m_s.aggStationWon && st.id == m_s.aggStation;
        p.setPen(QPen(c, target ? 2 : 1));
        p.drawLine(QPointF(x, 4), QPointF(x, bandRect.bottom()));
        p.setBrush(c);
        p.drawEllipse(QPointF(x, 6), 4, 4);
        p.setPen(target ? text : c);
        p.drawText(QRect(int(x) + 6, 0, 220, kTopH), Qt::AlignVCenter | Qt::AlignLeft,
                   (target ? QStringLiteral("◎ ") : QString()) + tr("Station %1").arg(st.id)
                       + (st.addEmSos() ? tr(" · general SoS") : QString()));
        m_hits << Hit{ QRect(int(x) - 5, 0, 10, bandRect.bottom()),
                       tr("Station %1 at %2%3, %4 from the own loco").arg(st.id).arg(metres(st.absLocM),
                           st.addEmSos() ? tr(", general SoS on") : QString(), metres(std::abs(double(st.absLocM) - own))) };
    }

    // The scale: absolute location in km, and the own front marked.
    const int sy = area.bottom() + 4;
    p.setPen(UiColor::frame());
    p.drawLine(area.left(), sy, area.right(), sy);
    const QVector<double> ticks = niceTicks(own - m_halfSpanM, own + m_halfSpanM, 7, true);
    p.setPen(UiColor::muted());
    for (double t : ticks) {
        const double x = xOf(t, area);
        if (x < area.left() || x > area.right()) continue;
        p.drawLine(QPointF(x, sy), QPointF(x, sy + 3));
        p.drawText(QRectF(x - 40, sy + 3, 80, 13), Qt::AlignHCenter | Qt::AlignTop,
                   QStringLiteral("%1 km").arg(t / 1000.0, 0, 'f', t == std::floor(t / 1000.0) * 1000.0 ? 0 : 1));
    }
    p.drawText(QRect(kPad, height() - 15, width() - 2 * kPad, 14), Qt::AlignLeft | Qt::AlignVCenter, legend());
}

bool SosStrip::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        const auto *he = static_cast<QHelpEvent *>(e);
        for (int i = m_hits.size() - 1; i >= 0; --i) {
            if (m_hits.at(i).rect.contains(he->pos())) {
                QToolTip::showText(he->globalPos(), m_hits.at(i).text, this);
                return true;
            }
        }
        QToolTip::hideText();
        e->ignore();
        return true;
    }
    return QWidget::event(e);
}
