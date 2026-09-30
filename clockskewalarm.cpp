#include "clockskewalarm.h"

#include "fieldplot.h"
#include "uicolors.h"
#include "windowgeometry.h"

#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>

ClockSkewAlarm::ClockSkewAlarm(int holdMs, QObject *parent)
    : QObject(parent)
    , m_holdMs(holdMs)
{
}

void ClockSkewAlarm::clear()
{
    m_raised = false;
    m_outsideSince = m_insideSince = -1;
    m_worstPending = 0;
    m_episodes.clear();
}

void ClockSkewAlarm::sample(qint64 nowMs, bool valid, int gapSeconds)
{
    if (!valid) {
        return;   // unheard is not in step, and not out of step either
    }
    const FrameClock::Accept a = FrameClock::classify(gapSeconds);
    const bool outside = (a == FrameClock::Accept::Stale || a == FrameClock::Accept::Ahead);

    if (outside) {
        m_insideSince = -1;
        if (m_outsideSince < 0) {
            m_outsideSince = nowMs;
            m_worstPending = gapSeconds;
            m_kindPending = a;
        } else if (qAbs(gapSeconds) > qAbs(m_worstPending)) {
            m_worstPending = gapSeconds;
            m_kindPending = a;
        }
        if (m_raised) {
            Episode &e = m_episodes.last();
            if (qAbs(gapSeconds) > qAbs(e.worstGap)) { e.worstGap = gapSeconds; e.kind = a; }
        } else if (nowMs - m_outsideSince >= m_holdMs) {
            Episode e;
            e.startMs = m_outsideSince;
            e.worstGap = m_worstPending;
            e.kind = m_kindPending;
            m_episodes.append(e);
            m_raised = true;
            emit raised(e);
        }
        return;
    }

    m_outsideSince = -1;
    if (!m_raised) {
        return;
    }
    if (m_insideSince < 0) {
        m_insideSince = nowMs;
    }
    if (nowMs - m_insideSince >= m_holdMs) {
        Episode &e = m_episodes.last();
        e.endMs = m_insideSince;
        m_raised = false;
        m_insideSince = -1;
        emit cleared(e);
    }
}

QString ClockSkewAlarm::gapText(int gap)
{
    if (gap == 0) return tr("loco and station in step");
    return gap > 0 ? tr("loco %1 s ahead of the station").arg(gap)
                   : tr("loco %1 s behind the station").arg(-gap);
}

QString ClockSkewAlarm::durationText(qint64 ms)
{
    const qint64 s = ms / 1000;
    if (s < 60) return tr("%1 s").arg(s);
    if (s < 3600) return tr("%1 min %2 s").arg(s / 60).arg(s % 60);
    return tr("%1 h %2 min").arg(s / 3600).arg((s % 3600) / 60);
}

// =============================================================================
//  ClockHistory
// =============================================================================

bool ClockHistory::add(const Sample &s)
{
    if (!m_samples.isEmpty() && s.ms - m_samples.last().ms < kMinStepMs) return false;
    m_samples.append(s);
    while (!m_samples.isEmpty() && s.ms - m_samples.first().ms > kKeepMs) m_samples.removeFirst();
    return true;
}

QVector<FieldSeries> ClockHistory::toSeries() const
{
    FieldSeries gap, loco, stn;
    gap.fieldName  = QObject::tr("loco − station");
    loco.fieldName = QObject::tr("loco − this PC");
    stn.fieldName  = QObject::tr("station − this PC");
    for (FieldSeries *f : { &gap, &loco, &stn }) f->unit = QStringLiteral("s");
    auto add = [](FieldSeries &f, qint64 ms, double v) {
        FieldSeries::Point p;
        p.epochMs = ms;
        p.value = v;
        if (f.points.isEmpty()) { f.minMs = f.maxMs = ms; f.minValue = f.maxValue = v; }
        f.minMs = qMin(f.minMs, ms); f.maxMs = qMax(f.maxMs, ms);
        f.minValue = qMin(f.minValue, v); f.maxValue = qMax(f.maxValue, v);
        f.points << p;
    };
    for (const Sample &s : m_samples) {
        if (s.hasGap)  add(gap, s.ms, s.gap);
        if (s.hasLoco) add(loco, s.ms, s.locoSkew);
        if (s.hasStn)  add(stn, s.ms, s.stnSkew);
    }
    QVector<FieldSeries> out;
    for (const FieldSeries &f : { gap, loco, stn }) if (!f.isEmpty()) out << f;
    return out;
}

// =============================================================================
//  ClockHistoryWindow
// =============================================================================

ClockHistoryWindow::ClockHistoryWindow(const ClockHistory *history, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_history(history)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Clocks — last 30 minutes"));
    WindowGeometry::makeResizableWindow(this);
    resize(820, 420);
    m_canvas = new FieldPlotCanvas(this);
    m_note = new QLabel(tr("From FRAME_NUM (seconds since midnight + 1). The loco and the station accept each "
                           "other's packets while loco − station stays between −2 s (exclusive) and +4 s. "
                           "Against this PC matters only for what DLConsole sends."), this);
    m_note->setWordWrap(true);
    m_note->setStyleSheet(UiColor::mutedStyle());
    auto *root = new QVBoxLayout(this);
    root->addWidget(m_canvas, 1);
    root->addWidget(m_note);
    m_timer = new QTimer(this);
    m_timer->setInterval(2000);
    connect(m_timer, &QTimer::timeout, this, &ClockHistoryWindow::refresh);
    m_timer->start();
    m_canvas->setSeriesList(m_history ? m_history->toSeries() : QVector<FieldSeries>());
    m_canvas->setOverlay(true);
}

void ClockHistoryWindow::refresh()
{
    if (!m_history) return;
    // Keep a zoom the operator made; otherwise follow the new data.
    m_canvas->setSeriesList(m_history->toSeries(), m_canvas->isZoomed());
}
