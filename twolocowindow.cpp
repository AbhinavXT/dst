#include "twolocowindow.h"

#include "messagedispatcher.h"
#include "statusline.h"
#include "uicolors.h"
#include "windowgeometry.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSaveFile>
#include <QVBoxLayout>

// =============================================================================
//  TwoLocoCanvas
// =============================================================================

TwoLocoCanvas::TwoLocoCanvas(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    UiColor::onThemeChange(this, [this]() { update(); });
}

void TwoLocoCanvas::setPair(const TwoLocoView::Pair &pair)
{
    m_pair = pair;
    m_minMs = 0; m_maxMs = 1;
    bool first = true;
    auto consider = [&](qint64 ms) {
        if (first) { m_minMs = m_maxMs = ms; first = false; return; }
        m_minMs = qMin(m_minMs, ms); m_maxMs = qMax(m_maxMs, ms);
    };
    for (const SpeedDistance::Sample &s : pair.a.samples) consider(s.epochMs);
    for (const SpeedDistance::Sample &s : pair.b.samples) consider(s.epochMs);
    if (m_maxMs <= m_minMs) m_maxMs = m_minMs + 1;

    m_minLocM = 0; m_maxLocM = 1;
    bool firstLoc = true;
    if (!pair.a.isEmpty()) { m_minLocM = pair.a.minLocM; m_maxLocM = pair.a.maxLocM; firstLoc = false; }
    if (!pair.b.isEmpty()) {
        if (firstLoc) { m_minLocM = pair.b.minLocM; m_maxLocM = pair.b.maxLocM; firstLoc = false; }
        else { m_minLocM = qMin(m_minLocM, pair.b.minLocM); m_maxLocM = qMax(m_maxLocM, pair.b.maxLocM); }
    }
    if (m_maxLocM <= m_minLocM) m_maxLocM = m_minLocM + 1;

    m_maxSpeedKmh = qMax(1.0, qMax(pair.a.maxSpeedKmh, pair.b.maxSpeedKmh));
    m_cursorMs = -1;
    update();
}

QRect TwoLocoCanvas::locationLane() const
{
    const int top = m_pair.plausible ? 10 : 26;
    const int h = height() - top - 36;
    return QRect(64, top, qMax(1, width() - 84), qMax(1, int(h * 0.58)));
}

QRect TwoLocoCanvas::speedLane() const
{
    const QRect loc = locationLane();
    const int top = loc.bottom() + 28;
    return QRect(64, top, loc.width(), qMax(1, height() - top - 20));
}

double TwoLocoCanvas::xOf(qint64 ms) const
{
    const QRect r = locationLane();   // both lanes share the same x range
    return r.left() + double(ms - m_minMs) / double(m_maxMs - m_minMs) * r.width();
}

qint64 TwoLocoCanvas::msAt(int px) const
{
    const QRect r = locationLane();
    const double f = r.width() > 0 ? double(px - r.left()) / r.width() : 0.0;
    return m_minMs + qint64(f * double(m_maxMs - m_minMs));
}

double TwoLocoCanvas::yOfLoc(const QRect &r, double locM) const
{
    return r.bottom() - (locM - m_minLocM) / (m_maxLocM - m_minLocM) * r.height();
}

double TwoLocoCanvas::yOfSpeed(const QRect &r, double kmh) const
{
    return r.bottom() - qBound(0.0, kmh, m_maxSpeedKmh) / m_maxSpeedKmh * r.height();
}

static QPainterPath locationPath(const QRect &lane, const SpeedDistance::Trace &t,
                                 qint64 minMs, qint64 maxMs, double minLocM, double maxLocM)
{
    QPainterPath path;
    bool started = false;
    for (const SpeedDistance::Sample &s : t.samples) {
        const double x = lane.left() + double(s.epochMs - minMs) / double(maxMs - minMs) * lane.width();
        const double y = lane.bottom() - (s.locM - minLocM) / (maxLocM - minLocM) * lane.height();
        if (!started) { path.moveTo(x, y); started = true; } else path.lineTo(x, y);
    }
    return path;
}

static QPainterPath speedPath(const QRect &lane, const SpeedDistance::Trace &t,
                              qint64 minMs, qint64 maxMs, double maxKmh)
{
    QPainterPath path;
    bool started = false;
    for (const SpeedDistance::Sample &s : t.samples) {
        const double x = lane.left() + double(s.epochMs - minMs) / double(maxMs - minMs) * lane.width();
        const double y = lane.bottom() - qBound(0.0, s.speedKmh, maxKmh) / maxKmh * lane.height();
        if (!started) { path.moveTo(x, y); started = true; } else path.lineTo(x, y);
    }
    return path;
}

void TwoLocoCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), palette().base());

    const QColor text = palette().color(QPalette::Text);
    const QColor ink = UiColor::muted(), frame = UiColor::frame(), grid = UiColor::grid();
    const QColor colA = UiColor::series(0), colB = UiColor::series(1);

    if (m_pair.a.isEmpty() && m_pair.b.isEmpty()) {
        p.setPen(ink);
        p.drawText(rect(), Qt::AlignCenter, tr("Pick two tabs with @dmi or @lsrp frames."));
        return;
    }

    if (!m_pair.plausible) {
        p.setPen(UiColor::error());
        QFont bold = p.font(); bold.setBold(true);
        p.setFont(bold);
        p.drawText(QRect(0, 2, width(), 20), Qt::AlignCenter, tr("⚠ %1 and %2 do not overlap in location")
                                                                   .arg(m_pair.keyA, m_pair.keyB));
    }

    const QRect loc = locationLane(), spd = speedLane();
    p.setPen(frame);
    p.drawRect(loc);
    p.drawRect(spd);

    QFont small = p.font();
    small.setPointSizeF(qMax(6.0, small.pointSizeF() - 1.0));
    p.setFont(small);

    // ---- location lane: gridlines, gap shading, both traces ------------------
    p.setPen(ink);
    p.drawText(QRect(0, int(loc.top() - 6), 60, 16), Qt::AlignRight, QString::number(m_maxLocM, 'f', 0));
    p.drawText(QRect(0, int(loc.bottom() - 10), 60, 16), Qt::AlignRight, QString::number(m_minLocM, 'f', 0));
    p.setPen(QPen(grid, 1, Qt::DotLine));
    for (int i = 1; i < 4; ++i) { const int y = loc.top() + loc.height() * i / 4; p.drawLine(loc.left(), y, loc.right(), y); }

    // The gap: a faint connector between A and B at each time both have a sample.
    {
        int ia = 0, ib = 0;
        p.setPen(QPen(QColor(ink.red(), ink.green(), ink.blue(), 90), 1));
        for (const TwoLocoView::GapSample &g : m_pair.gap) {
            while (ia < m_pair.a.samples.size() - 1 && m_pair.a.samples.at(ia).epochMs < g.ms) ++ia;
            while (ib < m_pair.b.samples.size() - 1 && m_pair.b.samples.at(ib).epochMs < g.ms) ++ib;
            if (ia >= m_pair.a.samples.size() || ib >= m_pair.b.samples.size()) continue;
            const double x = xOf(g.ms);
            const double ya = yOfLoc(loc, m_pair.a.samples.at(ia).locM);
            const double yb = yOfLoc(loc, m_pair.b.samples.at(ib).locM);
            p.drawLine(QPointF(x, ya), QPointF(x, yb));
        }
    }

    p.setPen(QPen(colA, 2));
    p.drawPath(locationPath(loc, m_pair.a, m_minMs, m_maxMs, m_minLocM, m_maxLocM));
    p.setPen(QPen(colB, 2));
    p.drawPath(locationPath(loc, m_pair.b, m_minMs, m_maxMs, m_minLocM, m_maxLocM));

    // ---- speed lane -------------------------------------------------------------
    p.setPen(ink);
    p.drawText(QRect(0, int(spd.top() - 6), 60, 16), Qt::AlignRight, QString::number(m_maxSpeedKmh, 'f', 0) + QStringLiteral(" km/h"));
    p.setPen(QPen(grid, 1, Qt::DotLine));
    for (int i = 1; i < 3; ++i) { const int y = spd.top() + spd.height() * i / 3; p.drawLine(spd.left(), y, spd.right(), y); }
    p.setPen(QPen(colA, 2));
    p.drawPath(speedPath(spd, m_pair.a, m_minMs, m_maxMs, m_maxSpeedKmh));
    p.setPen(QPen(colB, 2));
    p.drawPath(speedPath(spd, m_pair.b, m_minMs, m_maxMs, m_maxSpeedKmh));

    // ---- legend -------------------------------------------------------------------
    p.setPen(colA); p.drawText(QRect(loc.left(), loc.top() - 2, 120, 14), Qt::AlignLeft, m_pair.keyA);
    p.setPen(colB); p.drawText(QRect(loc.left() + 80, loc.top() - 2, 120, 14), Qt::AlignLeft, m_pair.keyB);

    // ---- events: a tick across both lanes, warning() for SoS, error() for collisions ---
    auto drawTicks = [&](const QVector<RunReport::Episode> &eps, const QColor &c) {
        p.setPen(QPen(c, 1, Qt::DashLine));
        for (const RunReport::Episode &e : eps) {
            const double x = xOf(e.fromMs);
            p.drawLine(QPointF(x, loc.top()), QPointF(x, spd.bottom()));
        }
    };
    drawTicks(m_pair.eventsA.sos, UiColor::warning());
    drawTicks(m_pair.eventsB.sos, UiColor::warning());
    drawTicks(m_pair.eventsA.headOn, UiColor::error());
    drawTicks(m_pair.eventsB.headOn, UiColor::error());
    drawTicks(m_pair.eventsA.rearEnd, UiColor::error());
    drawTicks(m_pair.eventsB.rearEnd, UiColor::error());

    // ---- time axis ------------------------------------------------------------------
    p.setPen(ink);
    p.drawText(QRect(loc.left(), spd.bottom() + 2, 160, 16), Qt::AlignLeft,
              QDateTime::fromMSecsSinceEpoch(m_minMs).toString(QStringLiteral("HH:mm:ss")));
    p.drawText(QRect(spd.right() - 160, spd.bottom() + 2, 160, 16), Qt::AlignRight,
              QDateTime::fromMSecsSinceEpoch(m_maxMs).toString(QStringLiteral("HH:mm:ss")));

    // ---- cursor ---------------------------------------------------------------------
    if (m_cursorMs >= m_minMs && m_cursorMs <= m_maxMs) {
        p.setPen(QPen(text, 1));
        const double x = xOf(m_cursorMs);
        p.drawLine(QPointF(x, loc.top()), QPointF(x, spd.bottom()));
    }
}

void TwoLocoCanvas::mouseMoveEvent(QMouseEvent *event)
{
    const QRect loc = locationLane(), spd = speedLane();
    const QRect both = loc.united(spd);
    if (!both.contains(event->pos())) { setCursor(-1); return; }
    setCursor(qBound(m_minMs, msAt(event->pos().x()), m_maxMs));
}

void TwoLocoCanvas::leaveEvent(QEvent *)
{
    setCursor(-1);
}

void TwoLocoCanvas::setCursor(qint64 ms)
{
    if (ms == m_cursorMs) return;
    m_cursorMs = ms;
    update();
    emit cursorChanged(ms);
}

// =============================================================================
//  TwoLocoWindow
// =============================================================================

TwoLocoWindow::TwoLocoWindow(MessageDispatcher *dispatcher, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_dispatcher(dispatcher)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Two-loco view"));
    WindowGeometry::makeResizableWindow(this);
    resize(1000, 760);

    m_boxA = new QComboBox(this);
    m_boxB = new QComboBox(this);
    m_canvas = new TwoLocoCanvas(this);
    m_status = new StatusLine;

    auto *picker = new QHBoxLayout;
    picker->addWidget(new QLabel(tr("Loco A:")));
    picker->addWidget(m_boxA, 1);
    picker->addWidget(new QLabel(tr("Loco B:")));
    picker->addWidget(m_boxB, 1);

    auto *saveImg = new QPushButton(tr("Save image…"), this);
    auto *saveCsvBtn = new QPushButton(tr("Save gap CSV…"), this);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_status, 1);
    buttons->addWidget(saveCsvBtn);
    buttons->addWidget(saveImg);

    auto *root = new QVBoxLayout(this);
    root->addLayout(picker);
    root->addWidget(m_canvas, 1);
    root->addLayout(buttons);

    connect(m_boxA, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TwoLocoWindow::rebuild);
    connect(m_boxB, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TwoLocoWindow::rebuild);
    connect(m_canvas, &TwoLocoCanvas::cursorChanged, this, &TwoLocoWindow::onCursorChanged);
    connect(saveImg, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, tr("Save two-loco view"),
            QStringLiteral("two_loco_%1_%2.png").arg(m_pair.keyA, m_pair.keyB), tr("PNG (*.png)"));
        if (path.isEmpty()) return;
        if (saveImage(path)) m_status->ok(tr("Saved %1").arg(path)); else m_status->fail(tr("Could not write %1").arg(path));
    });
    connect(saveCsvBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, tr("Save gap as CSV"),
            QStringLiteral("two_loco_gap_%1_%2.csv").arg(m_pair.keyA, m_pair.keyB), tr("CSV (*.csv)"));
        if (path.isEmpty()) return;
        if (saveCsv(path)) m_status->ok(tr("Saved %1").arg(path)); else m_status->fail(tr("Could not write %1").arg(path));
    });

    if (m_dispatcher) {
        connect(m_dispatcher, &MessageDispatcher::tabRequested, this, [this](QString, QString) { refreshPickers(); });
    }
    refreshPickers();

    QStringList keys = m_dispatcher ? m_dispatcher->knownKeys() : QStringList();
    keys.sort();
    if (keys.size() >= 2) setSources(keys.at(0), keys.at(1));
    else rebuild();
}

void TwoLocoWindow::refreshPickers()
{
    if (!m_dispatcher) return;
    QStringList keys = m_dispatcher->knownKeys();
    keys.sort();
    for (QComboBox *box : { m_boxA, m_boxB }) {
        const QString keep = box->currentData().toString();
        box->blockSignals(true);
        box->clear();
        for (const QString &k : keys) {
            const QString friendly = m_dispatcher->friendlyNameFor(k);
            box->addItem(friendly == k ? k : QStringLiteral("%1   (%2)").arg(friendly, k), k);
        }
        if (!keep.isEmpty()) { const int idx = box->findData(keep); if (idx >= 0) box->setCurrentIndex(idx); }
        box->blockSignals(false);
    }
}

void TwoLocoWindow::setSources(const QString &keyA, const QString &keyB)
{
    const int ia = m_boxA->findData(keyA), ib = m_boxB->findData(keyB);
    if (ia >= 0) m_boxA->setCurrentIndex(ia);
    if (ib >= 0) m_boxB->setCurrentIndex(ib);
    rebuild();
}

void TwoLocoWindow::rebuild()
{
    const QString keyA = m_boxA->currentData().toString();
    const QString keyB = m_boxB->currentData().toString();
    LogModel *modelA = (m_dispatcher && !keyA.isEmpty()) ? m_dispatcher->modelForKey(keyA) : nullptr;
    LogModel *modelB = (m_dispatcher && !keyB.isEmpty()) ? m_dispatcher->modelForKey(keyB) : nullptr;
    m_pair = TwoLocoView::build(modelA, keyA, modelB, keyB);
    m_canvas->setPair(m_pair);
    if (!m_pair.plausible) m_status->warn(m_pair.warning);
    else m_status->state(tr("%1 gap sample(s)").arg(m_pair.gap.size()));
}

void TwoLocoWindow::onCursorChanged(qint64 ms)
{
    if (ms < 0 || !m_pair.plausible) {
        if (m_pair.plausible) m_status->state(tr("%1 gap sample(s)").arg(m_pair.gap.size()));
        return;
    }
    // Nearest gap sample to the cursor, for a one-line readout.
    int best = -1; qint64 bestD = -1;
    for (int i = 0; i < m_pair.gap.size(); ++i) {
        const qint64 d = qAbs(m_pair.gap.at(i).ms - ms);
        if (best < 0 || d < bestD) { best = i; bestD = d; }
    }
    if (best < 0) return;
    m_status->state(tr("%1  gap %2 m").arg(QDateTime::fromMSecsSinceEpoch(m_pair.gap.at(best).ms).toString("HH:mm:ss"))
                        .arg(m_pair.gap.at(best).gapM, 0, 'f', 0));
}

bool TwoLocoWindow::saveImage(const QString &path) const
{
    return m_canvas->grab().save(path, "PNG");
}

bool TwoLocoWindow::saveCsv(const QString &path) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(TwoLocoView::gapToCsv(m_pair).toUtf8());
    return file.commit();
}
