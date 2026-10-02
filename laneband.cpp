#include "laneband.h"

#include "logmodel.h"
#include "uicolors.h"
#include "uistyle.h"

#include <QDateTime>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QToolTip>

namespace {
const int kLaneH   = 16;
const int kLaneGap = 4;
const int kLabelW  = 64;
const int kPad     = 6;
const char *kLanes[] = { "Mode", "Safety", "RFID", "Link", "Faults" };
const int kLaneCount = 5;

QString hm(qint64 ms) { return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss")); }
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
    setVisible(hasContent());
    updateGeometry();
    update();
}

bool LaneBand::hasContent() const
{
    return !m_sum.firstMode.isEmpty() || !m_sum.modeChanges.isEmpty() || !m_sum.emergencies.isEmpty()
        || !m_sum.overspeed.isEmpty() || !m_sum.tagReads.isEmpty() || !m_sum.faults.isEmpty();
}

QStringList LaneBand::laneNames() const
{
    QStringList out;
    for (const char *l : kLanes) out << QString::fromLatin1(l);
    return out;
}

QSize LaneBand::sizeHint() const
{
    return QSize(400, kPad * 2 + kLaneCount * kLaneH + (kLaneCount - 1) * kLaneGap);
}

QSize LaneBand::minimumSizeHint() const { return QSize(120, sizeHint().height()); }

QRect LaneBand::track(int lane) const
{
    const int x = kPad + kLabelW;
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

    for (int lane = 0; lane < kLaneCount; ++lane) {
        const QRect r = track(lane);
        p.setPen(UiColor::muted());
        p.drawText(QRect(kPad, r.top(), kLabelW - 6, r.height()), Qt::AlignLeft | Qt::AlignVCenter,
                   QString::fromLatin1(kLanes[lane]).toUpper());
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

    // Mode: one segment per mode, named; a colour per mode name (stable).
    {
        QString mode = m_sum.firstMode;
        qint64 at = m_from;
        auto colourFor = [](const QString &m) {
            uint h = qHash(m);
            QColor c = UiColor::series(int(h % uint(qMax(1, UiColor::seriesCount()))));
            return c;
        };
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
    for (const RunReport::Episode &e : m_sum.emergencies) bar(1, e.fromMs, qMax(e.toMs, e.fromMs + 1), UiColor::error(), tr("EMERGENCY"));
    for (const RunReport::Episode &e : m_sum.overspeed) bar(1, e.fromMs, qMax(e.toMs, e.fromMs + 1), UiColor::warning(), tr("overspeed"));
    for (const RunReport::Change &t : m_sum.tagReads) tick(2, t.ms, UiColor::ok(), t.to);
    for (const RunReport::Gap &g : m_sum.gaps) bar(3, g.fromMs, g.toMs, UiColor::warning(), tr("gap"));
    for (const RunReport::FaultEvent &f : m_sum.faults) tick(4, f.ms, f.raised ? UiColor::error() : UiColor::ok(), QString());

}

QString LaneBand::describeAt(const QPoint &pos) const
{
    for (int lane = 0; lane < kLaneCount; ++lane) {
        const QRect r = track(lane);
        if (!r.adjusted(0, -kLaneGap / 2, 0, kLaneGap / 2).contains(pos)) continue;
        const qint64 ms = msFor(pos.x(), r);
        const qint64 slack = r.width() > 0 ? (m_to - m_from) * 4 / r.width() : 0;   // ±4 px
        switch (lane) {
        case 0: {
            QString mode = m_sum.firstMode;
            for (const RunReport::Change &c : m_sum.modeChanges) { if (c.ms > ms) break; mode = c.to; }
            return mode.isEmpty() ? QString() : tr("Mode %1 at %2").arg(mode, hm(ms));
        }
        case 1:
            for (const RunReport::Episode &e : m_sum.emergencies)
                if (ms >= e.fromMs - slack && ms <= e.toMs + slack) return tr("Emergency %1 – %2: %3").arg(hm(e.fromMs), hm(e.toMs), e.what);
            for (const RunReport::Episode &e : m_sum.overspeed)
                if (ms >= e.fromMs - slack && ms <= e.toMs + slack)
                    return tr("Overspeed %1 – %2, worst %3 km/h over").arg(hm(e.fromMs), hm(e.toMs)).arg(e.worst, 0, 'f', 0);
            return QString();
        case 2:
            for (const RunReport::Change &t : m_sum.tagReads)
                if (qAbs(t.ms - ms) <= slack) return tr("RFID tag %1 at %2").arg(t.to, hm(t.ms));
            return QString();
        case 3:
            for (const RunReport::Gap &g : m_sum.gaps)
                if (ms >= g.fromMs - slack && ms <= g.toMs + slack)
                    return tr("No traffic %1 – %2 (%3 s)").arg(hm(g.fromMs), hm(g.toMs)).arg((g.toMs - g.fromMs) / 1000);
            return QString();
        case 4:
            for (const RunReport::FaultEvent &f : m_sum.faults)
                if (qAbs(f.ms - ms) <= slack) return tr("%1 at %2: %3").arg(f.raised ? tr("Fault") : tr("Cleared"), hm(f.ms), f.text);
            return QString();
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

void LaneBand::mousePressEvent(QMouseEvent *e)
{
    const QRect r = track(0);
    if (e->pos().x() < r.left()) return;
    emit timeClicked(msFor(e->pos().x(), r));
}
