#include "laneband.h"

#include "fieldplot.h"
#include "logmodel.h"
#include "capturedecoder.h"
#include "uicolors.h"
#include "uistyle.h"

#include <QDateTime>
#include <QContextMenuEvent>
#include <QHelpEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSet>
#include <QtMath>
#include <QTimer>
#include <QToolTip>

namespace {
const int kLaneH   = 16;
const int kLaneGap = 4;
const int kLabelW  = 64;      // the five fixed lanes' names fit in this
const int kLabelMaxW = 150;   // a custom lane's field name, up to this
const int kPad     = 6;
const char *kLanes[] = { "Mode", "Safety", "RFID", "Link", "Faults" };

// Session 184: a firmware SoS spell that is a collision (SosLog::Spell::what()).
bool isCollisionSpell(const QString &what)
{
    return what.startsWith(QLatin1String("Head-on")) || what.startsWith(QLatin1String("Rear-end"))
        || what.startsWith(QLatin1String("Station head-on")) || what.startsWith(QLatin1String("Station rear-end"));
}

QString hm(qint64 ms) { return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss")); }

const QRegularExpression &numberRx()
{
    static const QRegularExpression rx(QStringLiteral("^(-?\\d+(?:\\.\\d+)?)\\s*([A-Za-z/%\u00B0]*)$"));
    return rx;
}
}  // namespace

LaneBand::LaneBand(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("laneBand"));
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(3000);
    connect(m_debounce, &QTimer::timeout, this, &LaneBand::rebuildNow);
    hide();                                  // until there is something to draw
}

void LaneBand::setModel(LogModel *model, const QString &tabKey, const QString &tabName)
{
    m_model = model;
    m_key = tabKey;
    m_name = tabName;
    m_dirty = true;
    rebuildNow();
}

void LaneBand::refreshLater()
{
    m_dirty = true;
    if (!m_debounce->isActive()) m_debounce->start();
}

void LaneBand::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    if (m_dirty) refreshLater();
}

void LaneBand::rebuildNow()
{
    if (!m_model || !m_enabled) { setVisible(false); return; }
    // A band on a tab that is not showing costs nothing: it stays dirty and
    // is rebuilt when its tab is shown (MainWindow refreshes the current
    // tab's band on a tab change). It hides itself while it has nothing to
    // draw, so ITS visibility is not the test — its tab's is.
    if (parentWidget() && !parentWidget()->isVisible()) { m_dirty = true; return; }
    m_dirty = false;
    const int n = m_model->count();
    if (n == 0) { m_sum = RunReport::Summary(); m_from = m_to = 0; setVisible(false); return; }
    const qint64 newest = m_model->newestMs();
    qint64 from = newest - kWindowMs;
    if (n > kMaxRows) {
        if (LogEntryPtr e = m_model->entryAt(n - kMaxRows)) from = qMax(from, e->epochMs);
    }
    RunReport::Options o;
    o.hasWindow = true;
    o.fromMs = from;
    o.toMs = newest;
    m_sum = RunReport::summarise(m_model, m_key, m_name, o);
    m_from = qMax(from, m_sum.firstMs > 0 ? m_sum.firstMs : from);
    m_to = newest;
    buildCustom();
    setVisible(hasContent());
    updateGeometry();
    update();
}

void LaneBand::setCustomLanes(const QVector<CustomLane> &lanes)
{
    if (lanes == m_customDefs) return;
    m_customDefs = lanes;
    m_dirty = true;
    rebuildNow();
}

void LaneBand::buildCustom()
{
    m_custom.clear();
    if (!m_model || m_customDefs.isEmpty()) return;
    for (const CustomLane &def : m_customDefs) {
        bool capped = false;
        const QVector<RowFields> rows = collectRowFields(m_model, def.type, { def.field }, kMaxRows,
                                                         m_from, m_to, &capped);
        LaneData d;
        d.def = def;
        int numeric = 0, withUnit = 0;
        QSet<QString> distinct;
        for (const RowFields &r : rows) {
            if (!r.has(def.field)) continue;
            const QString shown = r.display.value(def.field).trimmed();
            // A number, with a unit or not ("57 km/h", "1200 m", "3"), is a
            // number. A name ("7 (Trip)", "unidentified") is not.
            const QRegularExpressionMatch m = numberRx().match(shown);
            const bool isNumber = m.hasMatch();
            d.ms << r.epochMs;
            d.value << (isNumber ? m.captured(1).toDouble() : qQNaN());
            d.label << shown;
            if (isNumber) {
                ++numeric;
                if (!m.captured(2).isEmpty()) ++withUnit;
                distinct.insert(m.captured(1));
            }
        }
        if (d.ms.isEmpty()) continue;           // this tab does not carry it
        // A graph when nearly all values are numbers AND they are a measured
        // quantity (a unit: km/h, m) or take many values; the odd special
        // value ("unidentified") is a gap in the line. A speed that stays at
        // 0 and 15 km/h is still a speed. Bare numbers with few values
        // (flags, counters, statuses) are spans.
        const bool mostlyNumbers = numeric * 10 >= d.ms.size() * 9;
        d.graph = mostlyNumbers && (withUnit * 10 >= numeric * 9 || distinct.size() > kMaxSpanValues);
        bool first = true;
        for (double v : d.value) {
            if (qIsNaN(v)) continue;
            if (first) { d.lo = d.hi = v; first = false; }
            d.lo = qMin(d.lo, v);
            d.hi = qMax(d.hi, v);
        }
        m_custom << d;
    }
}

int LaneBand::labelWidth() const
{
    // Wide enough for the longest custom lane's name (up to a limit), so
    // "TRAIN_SPEED" is not "TRAIN…". Every lane shares it: one time axis.
    QFont small = font();
    small.setPointSizeF(qMax(7.5, font().pointSizeF() - 1.5));
    const QFontMetrics fm(small);
    int w = kLabelW;
    for (const LaneData &d : m_custom) w = qMax(w, fm.horizontalAdvance(d.def.field) + 10);
    return qMin(w, kLabelMaxW);
}

QVector<CustomLane> LaneBand::shownCustomLanes() const
{
    QVector<CustomLane> out;
    for (const LaneData &d : m_custom) out << d.def;
    return out;
}

bool LaneBand::customLaneIsGraph(int i) const
{
    return i >= 0 && i < m_custom.size() && m_custom.at(i).graph;
}

bool LaneBand::hasContent() const
{
    return !m_sum.firstMode.isEmpty() || !m_sum.modeChanges.isEmpty() || !m_sum.missionStarts.isEmpty()
        || !m_sum.brakes.isEmpty() || !m_sum.selfSos.isEmpty() || !m_sum.collisionDetections.isEmpty()
        || !m_sum.sosThreats.isEmpty() || !m_sum.sosDecisions.isEmpty()
        || !m_sum.noKeys.isEmpty() || !m_sum.keyLoads.isEmpty()
        || !m_sum.emergencies.isEmpty()
        || !m_sum.overspeed.isEmpty() || !m_sum.tagReads.isEmpty() || !m_sum.faults.isEmpty()
        || !m_custom.isEmpty();
}

QStringList LaneBand::laneNames() const
{
    QStringList out;
    for (const char *l : kLanes) out << QString::fromLatin1(l);
    for (const LaneData &d : m_custom) out << d.def.field;
    return out;
}

QSize LaneBand::sizeHint() const
{
    const int n = laneCount();
    return QSize(400, kPad * 2 + n * kLaneH + (n - 1) * kLaneGap);
}

QSize LaneBand::minimumSizeHint() const { return QSize(120, sizeHint().height()); }

QRect LaneBand::track(int lane) const
{
    const int x = kPad + labelWidth();
    return QRect(x, kPad + lane * (kLaneH + kLaneGap), width() - x - kPad, kLaneH);
}

int LaneBand::xFor(qint64 ms, const QRect &r) const
{
    if (m_to <= m_from) return r.left();
    const double t = double(qBound(m_from, ms, m_to) - m_from) / double(m_to - m_from);
    return r.left() + int(t * r.width());
}

qint64 LaneBand::msFor(int x, const QRect &r) const
{
    if (r.width() <= 0 || m_to <= m_from) return m_from;
    const double t = double(qBound(r.left(), x, r.right()) - r.left()) / double(r.width());
    return m_from + qint64(t * double(m_to - m_from));
}

void LaneBand::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPalette pal = palette();
    const QColor window = pal.color(QPalette::Window);
    const QColor text = pal.color(QPalette::WindowText);
    QColor trackBg = window;
    trackBg = QColor((window.red() * 85 + text.red() * 15) / 100, (window.green() * 85 + text.green() * 15) / 100,
                     (window.blue() * 85 + text.blue() * 15) / 100);
    QFont small = font();
    small.setPointSizeF(qMax(7.5, font().pointSizeF() - 1.5));
    p.setFont(small);
    const QFontMetrics fm(small);

    for (int lane = 0; lane < laneCount(); ++lane) {
        const QRect r = track(lane);
        p.setPen(UiColor::muted());
        const QString name = lane < kFixedLanes ? QString::fromLatin1(kLanes[lane]).toUpper()
                                                : m_custom.at(lane - kFixedLanes).def.field;
        p.drawText(QRect(kPad, r.top(), labelWidth() - 6, r.height()), Qt::AlignLeft | Qt::AlignVCenter,
                   fm.elidedText(name, Qt::ElideRight, labelWidth() - 6));
        p.setPen(Qt::NoPen);
        p.setBrush(trackBg);
        p.drawRoundedRect(r, 4, 4);
    }

    auto bar = [&](int lane, qint64 a, qint64 b, const QColor &c, const QString &label) {
        const QRect r = track(lane);
        int x1 = xFor(a, r), x2 = xFor(b, r);
        if (x2 - x1 < 3) x2 = x1 + 3;
        const QRect br(x1, r.top(), x2 - x1, r.height());
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRoundedRect(br, 4, 4);
        if (!label.isEmpty() && fm.horizontalAdvance(label) + 8 < br.width()) {
            p.setPen(UiColor::withContrast(text, c, UiStyle::contrastFloor()));
            p.drawText(br, Qt::AlignCenter, label);
        }
    };
    auto tick = [&](int lane, qint64 ms, const QColor &c, const QString &label) {
        const QRect r = track(lane);
        const int x = xFor(ms, r);
        p.setPen(QPen(c, 2));
        p.drawLine(x, r.top() + 2, x, r.bottom() - 2);
        if (!label.isEmpty()) {
            p.setPen(text);
            p.drawText(QRect(x + 3, r.top(), 60, r.height()), Qt::AlignLeft | Qt::AlignVCenter, label);
        }
    };

    // A colour per name (stable), for Mode, RFID and custom span lanes.
    auto colourFor = [](const QString &m) {
        uint h = qHash(m);
        QColor c = UiColor::series(int(h % uint(qMax(1, UiColor::seriesCount()))));
        return c;
    };
    // Mode: one segment per mode, named.
    {
        QString mode = m_sum.firstMode;
        qint64 at = m_from;
        // "7 (Trip)" reads as "Trip", as on the inspector's tile.
        auto named = [](const QString &m) {
            const int open = m.indexOf(QLatin1Char('(')), close = m.lastIndexOf(QLatin1Char(')'));
            return (open > 0 && close > open) ? m.mid(open + 1, close - open - 1).trimmed() : m;
        };
        for (const RunReport::Change &c : m_sum.modeChanges) {
            if (!mode.isEmpty()) bar(0, at, c.ms, colourFor(mode), named(mode));
            mode = c.to;
            at = c.ms;
        }
        if (!mode.isEmpty()) bar(0, at, m_to, colourFor(mode), named(mode));
    }
    // Session 168: start of mission (ARP), over the mode it precedes: the
    // spell in the accent colour, at least 6 px so a few seconds still show.
    for (const RunReport::Episode &m : m_sum.missionStarts) {
        const QRect r = track(0);
        const int x1 = xFor(m.fromMs, r);
        const qint64 minMs = m_to > m_from ? (m_to - m_from) * 6 / qMax(1, r.width()) : 1;
        bar(0, m.fromMs, qMax(m.toMs, qMin(m.fromMs + minMs, m_to)), UiColor::accent(), tr("Start of mission"));
        p.setPen(QPen(UiColor::accent(), 2));
        p.drawLine(x1, r.top() - 2, x1, r.bottom() + 2);
    }
    for (const RunReport::Episode &e : m_sum.emergencies) bar(1, e.fromMs, qMax(e.toMs, e.fromMs + 1), UiColor::error(), tr("EMERGENCY"));
    for (const RunReport::Episode &e : m_sum.overspeed) bar(1, e.fromMs, qMax(e.toMs, e.fromMs + 1), UiColor::warning(), tr("overspeed"));
    // Session 178: the loco's own SoS (accent, top half) and collision detections (ticks).
    for (const RunReport::Episode &e : m_sum.selfSos) {
        const QRect r = track(1);
        const int x1 = xFor(e.fromMs, r), x2 = qMax(xFor(e.toMs, r), x1 + 3);
        p.fillRect(QRect(x1, r.top(), x2 - x1, r.height() / 2), UiColor::accent());
    }
    for (const RunReport::Change &c : m_sum.collisionDetections) tick(1, c.ms, UiColor::error(), QString());
    // Session 184: the firmware's SoS threats (@sos), between the own-SoS half
    // and the brake strip: collisions in the error colour, SoS in the warning
    // colour; its brake decisions as ticks (applied: error, not applied: muted).
    for (const RunReport::Episode &e : m_sum.sosThreats) {
        const QRect r = track(1);
        const int x1 = xFor(e.fromMs, r), x2 = qMax(xFor(e.toMs, r), x1 + 3);
        const int y = r.top() + r.height() / 2 + 1;
        p.fillRect(QRect(x1, y, x2 - x1, qMax(2, r.bottom() - 5 - y)),
                   isCollisionSpell(e.what) ? UiColor::error() : UiColor::warning());
    }
    for (const RunReport::Change &c : m_sum.sosDecisions)
        tick(1, c.ms, c.from == QLatin1String("applied") ? UiColor::error() : UiColor::muted(), QString());
    // Session 175: EB / FSB applications, along the bottom of the Safety lane.
    for (const RunReport::BrakeEvent &b : m_sum.brakes) {
        const QRect r = track(1);
        const int x1 = xFor(b.fromMs, r), x2 = qMax(xFor(b.toMs, r), x1 + 3);
        p.fillRect(QRect(x1, r.bottom() - 4, x2 - x1, 4), b.type.startsWith(QLatin1String("4")) ? UiColor::error() : UiColor::warning());
    }
    // RFID (session 164): the last tag read holds until the next read, as
    // the mode holds until the next change -- a span per tag, named.
    for (int i = 0; i < m_sum.tagReads.size(); ++i) {
        const RunReport::Change &t = m_sum.tagReads.at(i);
        const qint64 until = i + 1 < m_sum.tagReads.size() ? m_sum.tagReads.at(i + 1).ms : m_to;
        bar(2, t.ms, qMax(until, t.ms + 1), colourFor(QStringLiteral("tag ") + t.to), t.to);
    }
    // Session 180: NMS reported a tag's duplicate missing: a tick on the RFID lane.
    for (const TagCheck::Tag &tg : m_sum.tagCheck.tags)
        if (tg.duplicateMissingMs) tick(2, tg.duplicateMissingMs, UiColor::warning(), QString());
    for (const RunReport::Gap &g : m_sum.gaps) bar(3, g.fromMs, g.toMs, UiColor::warning(), tr("gap"));
    // Session 179: no session keys (top half of the Link lane), key loads (ticks).
    for (const RunReport::Episode &k : m_sum.noKeys) {
        const QRect r = track(3);
        const int x1 = xFor(k.fromMs, r), x2 = qMax(xFor(k.toMs, r), x1 + 3);
        p.fillRect(QRect(x1, r.top(), x2 - x1, r.height() / 2), UiColor::accent());
    }
    for (qint64 ms : m_sum.keyLoads) tick(3, ms, UiColor::ok(), QString());
    for (const RunReport::FaultEvent &f : m_sum.faults) tick(4, f.ms, f.raised ? UiColor::error() : UiColor::ok(), QString());

    // Custom lanes (session 164).
    for (int i = 0; i < m_custom.size(); ++i) {
        const LaneData &d = m_custom.at(i);
        const int lane = kFixedLanes + i;
        if (!d.graph) {
            int j = 0;
            while (j < d.ms.size()) {
                int k = j;
                while (k + 1 < d.ms.size() && d.label.at(k + 1) == d.label.at(j)) ++k;
                const qint64 until = k + 1 < d.ms.size() ? d.ms.at(k + 1) : m_to;
                bar(lane, d.ms.at(j), qMax(until, d.ms.at(j) + 1),
                    colourFor(d.def.field + d.label.at(j)), d.label.at(j));
                j = k + 1;
            }
            continue;
        }
        const QRect r = track(lane).adjusted(0, 2, 0, -2);
        const double span = d.hi > d.lo ? d.hi - d.lo : 1.0;
        QPainterPath path;
        bool pen = false;                        // a special value lifts the pen
        for (int j = 0; j < d.ms.size(); ++j) {
            if (qIsNaN(d.value.at(j))) { pen = false; continue; }
            const QPointF pt(xFor(d.ms.at(j), r), r.bottom() - (d.value.at(j) - d.lo) / span * r.height());
            if (!pen) path.moveTo(pt); else path.lineTo(pt);
            pen = true;
        }
        p.setPen(QPen(UiColor::accent(), 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }

}

QString LaneBand::describeAt(const QPoint &pos) const
{
    for (int lane = 0; lane < laneCount(); ++lane) {
        const QRect r = track(lane);
        if (!r.adjusted(0, -kLaneGap / 2, 0, kLaneGap / 2).contains(pos)) continue;
        const qint64 ms = msFor(pos.x(), r);
        const qint64 slack = r.width() > 0 ? (m_to - m_from) * 4 / r.width() : 0;   // ±4 px
        switch (lane) {
        case 0: {
            for (const RunReport::Episode &m : m_sum.missionStarts) {
                if (ms < m.fromMs - slack || ms > m.toMs + slack * 2) continue;
                return m.what.isEmpty()
                    ? tr("Start of mission from %1 (ARP: Stand_By, no direction, tag or location), still so at %2")
                          .arg(hm(m.fromMs), hm(m.toMs))
                    : tr("Start of mission from %1 (ARP: Stand_By, no direction, tag or location); "
                         "then %2 after %3").arg(hm(m.fromMs), m.what, hm(m.toMs));
            }
            QString mode = m_sum.firstMode;
            for (const RunReport::Change &c : m_sum.modeChanges) { if (c.ms > ms) break; mode = c.to; }
            return mode.isEmpty() ? QString() : tr("Mode %1 at %2").arg(mode, hm(ms));
        }
        case 1:
            for (const RunReport::Change &c : m_sum.collisionDetections)
                if (qAbs(c.ms - ms) <= slack) return tr("Collision detection (NMS) at %1: %2").arg(hm(c.ms), c.to);
            for (const RunReport::Change &c : m_sum.sosDecisions)
                if (qAbs(c.ms - ms) <= slack) return tr("SoS decision at %1: %2").arg(hm(c.ms), c.to);
            for (const RunReport::Episode &e : m_sum.sosThreats)
                if (ms >= e.fromMs - slack && ms <= e.toMs + slack)
                    return tr("SoS (firmware): %1, %2 – %3").arg(e.what, hm(e.fromMs), hm(e.toMs));
            for (const RunReport::Episode &e : m_sum.selfSos)
                if (ms >= e.fromMs - slack && ms <= e.toMs + slack)
                    return tr("Own SoS (NMS): %1, %2 – %3").arg(e.what, hm(e.fromMs), hm(e.toMs));
            for (const RunReport::BrakeEvent &b : m_sum.brakes)
                if (ms >= b.fromMs - slack && ms <= b.toMs + slack)
                    return tr("%1 %2 – %3: %4").arg(b.type, hm(b.fromMs), hm(b.toMs), b.reasonsText());
            for (const RunReport::Episode &e : m_sum.emergencies)
                if (ms >= e.fromMs - slack && ms <= e.toMs + slack) return tr("Emergency %1 – %2: %3").arg(hm(e.fromMs), hm(e.toMs), e.what);
            for (const RunReport::Episode &e : m_sum.overspeed)
                if (ms >= e.fromMs - slack && ms <= e.toMs + slack)
                    return tr("Overspeed %1 – %2, worst %3 km/h over").arg(hm(e.fromMs), hm(e.toMs)).arg(e.worst, 0, 'f', 0);
            return QString();
        case 2: {
            for (const TagCheck::Tag &tg : m_sum.tagCheck.tags)
                if (tg.duplicateMissingMs && qAbs(tg.duplicateMissingMs - ms) <= slack)
                    return tr("NMS: duplicate of tag %1 missing, at %2").arg(tg.tag, hm(tg.duplicateMissingMs));
            // The span under the point: the last tag read at or before it.
            const RunReport::Change *at = nullptr;
            int i = 0;
            for (; i < m_sum.tagReads.size(); ++i) {
                if (m_sum.tagReads.at(i).ms > ms + slack) break;
                at = &m_sum.tagReads.at(i);
            }
            if (!at) return QString();
            const qint64 until = i < m_sum.tagReads.size() ? m_sum.tagReads.at(i).ms : m_to;
            return tr("RFID tag %1, read at %2; the last tag read until %3")
                .arg(at->to, hm(at->ms), i < m_sum.tagReads.size() ? hm(until) : tr("now"));
        }
        case 3:
            for (qint64 k : m_sum.keyLoads)
                if (qAbs(k - ms) <= slack) return tr("Session keys loaded (@auth_keys) at %1").arg(hm(k));
            for (const RunReport::Episode &k : m_sum.noKeys)
                if (ms >= k.fromMs - slack && ms <= k.toMs + slack)
                    return tr("No session keys (NMS), %1 – %2%3").arg(hm(k.fromMs), hm(k.toMs),
                        k.what.endsWith(QLatin1String("(at a start of mission)")) ? tr(", at a start of mission") : QString());
            for (const RunReport::Gap &g : m_sum.gaps)
                if (ms >= g.fromMs - slack && ms <= g.toMs + slack)
                    return tr("No traffic %1 – %2 (%3 s)").arg(hm(g.fromMs), hm(g.toMs)).arg((g.toMs - g.fromMs) / 1000);
            return QString();
        case 4:
            for (const RunReport::FaultEvent &f : m_sum.faults)
                if (qAbs(f.ms - ms) <= slack) return tr("%1 at %2: %3").arg(f.raised ? tr("Fault") : tr("Cleared"), hm(f.ms), f.text);
            return QString();
        default: {
            const LaneData &d = m_custom.at(lane - kFixedLanes);
            int j = -1;                          // the last sample at or before
            for (int k = 0; k < d.ms.size() && d.ms.at(k) <= ms + slack; ++k) j = k;
            if (j < 0) return tr("@%1 %2: no value yet at %3").arg(d.def.type, d.def.field, hm(ms));
            return tr("@%1 %2 = %3 at %4").arg(d.def.type, d.def.field, d.label.at(j), hm(d.ms.at(j)));
        }
        }
    }
    return QString();
}

bool LaneBand::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        auto *he = static_cast<QHelpEvent *>(e);
        const QString t = describeAt(he->pos());
        if (t.isEmpty()) QToolTip::hideText(); else QToolTip::showText(he->globalPos(), t, this);
        return true;
    }
    return QWidget::event(e);
}

QHash<QString, QStringList> LaneBand::addableFields() const
{
    QHash<QString, QStringList> out;
    if (!m_model) return out;
    const FieldCatalogue cat = discoverFieldCatalogue(m_model, kavachSchema());
    for (const QString &type : cat.types) out.insert(type, cat.fields.value(type));
    return out;
}

int LaneBand::customLaneAt(const QPoint &pos) const
{
    for (int i = 0; i < m_custom.size(); ++i) {
        QRect r = track(kFixedLanes + i);
        r.setLeft(0);                            // the label counts too
        if (r.adjusted(0, -kLaneGap / 2, 0, kLaneGap / 2).contains(pos)) return i;
    }
    return -1;
}

void LaneBand::contextMenuEvent(QContextMenuEvent *e)
{
    QMenu menu(this);
    QMenu *add = menu.addMenu(tr("Add lane"));
    add->setObjectName(QStringLiteral("laneAddMenu"));
    const QHash<QString, QStringList> fields = addableFields();
    QStringList types = fields.keys();
    types.sort();
    for (const QString &type : types) {
        QMenu *sub = add->addMenu(type);
        for (const QString &field : fields.value(type)) {
            QAction *a = sub->addAction(field);
            const CustomLane lane{ type, field };
            a->setData(lane.key());
            a->setCheckable(true);
            a->setChecked(m_customDefs.contains(lane));
            a->setEnabled(!m_customDefs.contains(lane));
        }
    }
    if (types.isEmpty()) add->addAction(tr("(no decodable packets in this tab)"))->setEnabled(false);
    QAction *remove = nullptr;
    const int under = customLaneAt(e->pos());
    if (under >= 0) {
        const CustomLane &d = m_custom.at(under).def;
        remove = menu.addAction(tr("Remove lane \u201C%1 \u25B8 %2\u201D").arg(d.type, d.field));
    }
    // Emitted after the menu has closed: the owner rebuilds every band, and
    // this menu's actions must not be freed while one is delivering.
    QAction *chosen = menu.exec(e->globalPos());
    if (!chosen) return;
    if (chosen == remove) { emit removeLaneRequested(m_custom.at(under).def); return; }
    const QStringList parts = chosen->data().toString().split(QLatin1Char('\t'));
    if (parts.size() == 2) emit addLaneRequested(CustomLane{ parts.at(0), parts.at(1) });
}

void LaneBand::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
    const QRect r = track(0);
    if (e->pos().x() < r.left()) return;
    emit timeClicked(msFor(e->pos().x(), r));
}
