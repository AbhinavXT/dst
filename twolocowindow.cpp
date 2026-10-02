#include "twolocowindow.h"

#include "messagedispatcher.h"
#include "settings.h"
#include "statusline.h"
#include "uicolors.h"
#include "windowgeometry.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDateTime>
#include <QFileDialog>
#include <QHelpEvent>
#include <QToolTip>
#include <cmath>
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

    // Known locations only (session 132): a 0 m sample is "not localised",
    // and counting it stretched the scale to 0..161 km.
    m_minLocM = pair.minLocM;
    m_maxLocM = pair.maxLocM;
    if (m_maxLocM - m_minLocM < 100.0) {   // parked, or one point: give it room
        const double mid = (m_minLocM + m_maxLocM) / 2.0;
        m_minLocM = mid - 50.0;
        m_maxLocM = mid + 50.0;
    }

    // A round top for the speed scale, so its labels are round numbers.
    const double top = qMax(1.0, qMax(pair.a.maxSpeedKmh, pair.b.maxSpeedKmh));
    const double step = top <= 30 ? 10.0 : (top <= 90 ? 30.0 : 60.0);
    m_maxSpeedKmh = std::ceil(top / step) * step;
    m_cursorMs = -1;
    update();
}

int TwoLocoCanvas::leftMargin() const
{
    const QFontMetrics fm(smallFont());
    return qMax(fm.horizontalAdvance(QStringLiteral("888.888 km")),
                fm.horizontalAdvance(QStringLiteral("888 km/h"))) + 14;
}

QFont TwoLocoCanvas::smallFont() const
{
    QFont f = font();
    f.setPointSizeF(qMax(6.0, f.pointSizeF() - 1.0));
    return f;
}

int TwoLocoCanvas::headerHeight() const
{
    // The legend/readout row, then the warning row when there is one.
    const int row = fontMetrics().height() + 10;
    return m_pair.plausible ? row : 2 * row;
}

QRect TwoLocoCanvas::locationLane() const
{
    const int lh = QFontMetrics(smallFont()).height();
    const int top = headerHeight() + lh + 4;           // + the lane's title
    const int bottomReserve = lh + 4 + lh + 10;         // speed title + time axis
    const int avail = qMax(2, height() - top - bottomReserve - 12);
    const int left = leftMargin();
    return QRect(left, top, qMax(1, width() - left - 16), qMax(1, int(avail * 0.56)));
}

QRect TwoLocoCanvas::speedLane() const
{
    const QRect loc = locationLane();
    const int lh = QFontMetrics(smallFont()).height();
    const int top = loc.bottom() + 12 + lh + 4;
    return QRect(loc.left(), top, loc.width(), qMax(1, height() - top - lh - 10));
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

namespace {

// Last sample at or before ms, or -1.
int atOrBefore(const QVector<SpeedDistance::Sample> &v, qint64 ms)
{
    int lo = 0, hi = v.size();
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (v.at(mid).epochMs <= ms) lo = mid + 1; else hi = mid;
    }
    return lo - 1;
}

QString km(double m, int decimals = 3) { return QString::number(m / 1000.0, 'f', decimals) + QStringLiteral(" km"); }
QString hms(qint64 ms) { return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss")); }

// A time step for the axis: the first of these at least minMs long.
qint64 niceTimeStep(qint64 minMs)
{
    static const qint64 steps[] = { 1000, 2000, 5000, 10000, 15000, 30000, 60000, 120000, 300000,
                                    600000, 900000, 1800000, 3600000, 7200000, 21600000 };
    for (qint64 s : steps) if (s >= minMs) return s;
    return 86400000;
}

}  // namespace

QString TwoLocoCanvas::readoutAt(qint64 ms) const
{
    auto one = [ms](const QString &name, const SpeedDistance::Trace &t) {
        const int i = atOrBefore(t.samples, ms);
        if (i < 0) return QObject::tr("%1 no data yet").arg(name);
        const SpeedDistance::Sample &s = t.samples.at(i);
        const QString where = SpeedDistance::locationKnown(s) ? km(s.locM) : QObject::tr("not localised (0 m)");
        return QObject::tr("%1 %2, %3 km/h").arg(name, where).arg(s.speedKmh, 0, 'f', 0);
    };
    QString out = hms(ms) + QStringLiteral("   ") + one(QStringLiteral("A"), m_pair.a)
                + QStringLiteral("   ") + one(QStringLiteral("B"), m_pair.b);
    // The gap sample nearest the cursor, within 2 s; none while either is not known.
    int best = -1;
    qint64 bestD = 0;
    for (int i = 0; i < m_pair.gap.size(); ++i) {
        const qint64 d = qAbs(m_pair.gap.at(i).ms - ms);
        if (best < 0 || d < bestD) { best = i; bestD = d; }
    }
    if (best >= 0 && bestD <= 2000)
        out += QStringLiteral("   ") + tr("gap %1 m").arg(m_pair.gap.at(best).gapM, 0, 'f', 0);
    else
        out += QStringLiteral("   ") + tr("no gap");
    return out;
}

bool TwoLocoCanvas::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        // The events near the pointer, by name; elsewhere the header has the readout.
        const auto *he = static_cast<QHelpEvent *>(e);
        const QRect both = locationLane().united(speedLane());
        QStringList lines;
        if (both.contains(he->pos())) {
            auto add = [&](const QString &who, const QVector<RunReport::Episode> &eps, const QString &kind) {
                for (const RunReport::Episode &ep : eps)
                    if (qAbs(xOf(ep.fromMs) - he->pos().x()) <= 4)
                        lines << tr("%1 %2 at %3: %4").arg(who, kind, hms(ep.fromMs), ep.what);
            };
            add(m_pair.keyA, m_pair.eventsA.sos, tr("SoS"));
            add(m_pair.keyB, m_pair.eventsB.sos, tr("SoS"));
            add(m_pair.keyA, m_pair.eventsA.headOn, tr("head-on"));
            add(m_pair.keyB, m_pair.eventsB.headOn, tr("head-on"));
            add(m_pair.keyA, m_pair.eventsA.rearEnd, tr("rear-end"));
            add(m_pair.keyB, m_pair.eventsB.rearEnd, tr("rear-end"));
        }
        if (lines.isEmpty()) { QToolTip::hideText(); e->ignore(); }
        else QToolTip::showText(he->globalPos(), lines.join(QLatin1Char('\n')), this);
        return true;
    }
    return QWidget::event(e);
}

// Location as a path that breaks wherever the location is not known, rather
// than diving to 0 m and back.
static QPainterPath locationPath(const QRect &lane, const SpeedDistance::Trace &t,
                                 qint64 minMs, qint64 maxMs, double minLocM, double maxLocM)
{
    QPainterPath path;
    bool pen = false;
    for (const SpeedDistance::Sample &s : t.samples) {
        if (!SpeedDistance::locationKnown(s)) { pen = false; continue; }
        const double x = lane.left() + double(s.epochMs - minMs) / double(maxMs - minMs) * lane.width();
        const double y = lane.bottom() - (s.locM - minLocM) / (maxLocM - minLocM) * lane.height();
        if (!pen) { path.moveTo(x, y); pen = true; } else path.lineTo(x, y);
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

    const QRect loc = locationLane(), spd = speedLane();
    const QFont small = smallFont();
    const QFontMetrics fm(font()), sfm(small);
    const int lh = sfm.height();
    const int rowH = fm.height() + 10;

    // ---- header: legend, then the readout at the cursor -------------------------
    {
        int x = loc.left();
        auto swatch = [&](const QColor &c, const QString &label) {
            p.setPen(QPen(c, 3));
            p.drawLine(x, rowH / 2, x + 16, rowH / 2);
            x += 22;
            p.setPen(text);
            const int w = fm.horizontalAdvance(label);
            p.drawText(QRect(x, 0, w + 2, rowH), Qt::AlignLeft | Qt::AlignVCenter, label);
            x += w + 18;
        };
        swatch(colA, QStringLiteral("A  ") + m_pair.keyA);
        swatch(colB, QStringLiteral("B  ") + m_pair.keyB);
        const QString read = m_cursorMs >= 0 ? readoutAt(m_cursorMs) : tr("Point at the plot to read both locos at that moment");
        p.setPen(m_cursorMs >= 0 ? text : ink);
        p.drawText(QRect(x, 0, qMax(0, loc.right() - x), rowH), Qt::AlignRight | Qt::AlignVCenter,
                   fm.elidedText(read, Qt::ElideRight, qMax(0, loc.right() - x)));
    }
    if (!m_pair.plausible) {
        p.setPen(UiColor::warning());
        QFont bold = font(); bold.setWeight(QFont::DemiBold);
        p.setFont(bold);
        const QString w = (m_pair.hasLocA && m_pair.hasLocB)
            ? tr("⚠ %1 and %2 are %3 km apart at their nearest: they may not be on the same section")
                  .arg(m_pair.keyA, m_pair.keyB).arg(m_pair.apartM / 1000.0, 0, 'f', 1)
            : tr("⚠ No gap: a loco never localised (0 m throughout)");
        p.drawText(QRect(loc.left(), rowH, loc.width(), rowH), Qt::AlignLeft | Qt::AlignVCenter, w);
        p.setFont(font());
    }

    p.setFont(small);

    // ---- lane titles and frames ---------------------------------------------------
    p.setPen(ink);
    p.drawText(QRect(loc.left(), loc.top() - lh - 3, loc.width(), lh), Qt::AlignLeft | Qt::AlignVCenter,
               tr("LOCATION"));
    p.drawText(QRect(spd.left(), spd.top() - lh - 3, spd.width(), lh), Qt::AlignLeft | Qt::AlignVCenter, tr("SPEED"));
    p.setPen(frame);
    p.drawRect(loc);
    p.drawRect(spd);

    // ---- location lane: km labels on the gridlines, gap shading, both traces ------
    const bool anyLoc = m_pair.hasLocA || m_pair.hasLocB;
    if (anyLoc) {
        // Round ticks: 1, 2 or 5 x 10^k metres, about four of them.
        const double span = m_maxLocM - m_minLocM;
        double step = 1.0;
        while (step * 10.0 <= span / 4.0) step *= 10.0;
        for (double f : { 1.0, 2.0, 5.0, 10.0 }) if (step * f >= span / 4.0) { step *= f; break; }
        const int dec = step >= 1000.0 ? 0 : (step >= 100.0 ? 1 : (step >= 10.0 ? 2 : 3));
        for (double m = std::ceil(m_minLocM / step) * step; m <= m_maxLocM + 1e-6; m += step) {
            const int y = int(yOfLoc(loc, m));
            if (y > loc.top() + 1 && y < loc.bottom() - 1) {
                p.setPen(QPen(grid, 1, Qt::DotLine));
                p.drawLine(loc.left(), y, loc.right(), y);
            }
            p.setPen(ink);
            p.drawText(QRect(0, y - lh / 2, loc.left() - 6, lh), Qt::AlignRight | Qt::AlignVCenter, km(m, dec));
        }
        // The gap: a faint connector between A and B at each matched instant.
        QColor gc = ink; gc.setAlpha(90);
        p.setPen(QPen(gc, 1));
        for (const TwoLocoView::GapSample &g : m_pair.gap) {
            const double x = xOf(g.ms);
            p.drawLine(QPointF(x, yOfLoc(loc, g.aLocM)), QPointF(x, yOfLoc(loc, g.bLocM)));
        }
        p.setPen(QPen(colA, 2));
        p.drawPath(locationPath(loc, m_pair.a, m_minMs, m_maxMs, m_minLocM, m_maxLocM));
        p.setPen(QPen(colB, 2));
        p.drawPath(locationPath(loc, m_pair.b, m_minMs, m_maxMs, m_minLocM, m_maxLocM));
    } else {
        p.setPen(ink);
        p.drawText(loc, Qt::AlignCenter, tr("Neither loco localised: every frame reports 0 m"));
    }

    // ---- speed lane --------------------------------------------------------------------
    for (int i = 0; i <= 3; ++i) {
        const int y = spd.bottom() - spd.height() * i / 3;
        if (i > 0 && i < 3) { p.setPen(QPen(grid, 1, Qt::DotLine)); p.drawLine(spd.left(), y, spd.right(), y); }
        p.setPen(ink);
        p.drawText(QRect(0, y - lh / 2, spd.left() - 6, lh), Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("%1 km/h").arg(m_maxSpeedKmh * i / 3.0, 0, 'f', 0));
    }
    p.setPen(QPen(colA, 2));
    p.drawPath(speedPath(spd, m_pair.a, m_minMs, m_maxMs, m_maxSpeedKmh));
    p.setPen(QPen(colB, 2));
    p.drawPath(speedPath(spd, m_pair.b, m_minMs, m_maxMs, m_maxSpeedKmh));

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

    // ---- time axis: round ticks, labelled ---------------------------------------------------
    {
        const int labelW = sfm.horizontalAdvance(QStringLiteral("88:88:88")) + 24;
        const double msPerPx = double(m_maxMs - m_minMs) / qMax(1, spd.width());
        const qint64 step = niceTimeStep(qint64(msPerPx * labelW));
        // Ticks on round local clock times.
        const qint64 offset = QDateTime::fromMSecsSinceEpoch(m_minMs).offsetFromUtc() * 1000LL;
        qint64 t = ((m_minMs + offset + step - 1) / step) * step - offset;
        double lastRight = -1e9;
        for (; t <= m_maxMs; t += step) {
            const double x = xOf(t);
            p.setPen(QPen(frame, 1));
            p.drawLine(QPointF(x, spd.bottom()), QPointF(x, spd.bottom() + 4));
            p.setPen(ink);
            const double left = qBound(0.0, x - labelW / 2.0, double(width() - labelW));
            if (left < lastRight) continue;   // slid in from the edge onto its neighbour
            p.drawText(QRectF(left, spd.bottom() + 5, labelW, lh), Qt::AlignCenter, hms(t));
            lastRight = left + labelW;
        }
    }

    // ---- cursor: a line, and a dot on each trace where it crosses -----------------------------
    if (m_cursorMs >= m_minMs && m_cursorMs <= m_maxMs) {
        const double x = xOf(m_cursorMs);
        p.setPen(QPen(text, 1));
        p.drawLine(QPointF(x, loc.top()), QPointF(x, spd.bottom()));
        auto dots = [&](const SpeedDistance::Trace &t, const QColor &c) {
            const int i = atOrBefore(t.samples, m_cursorMs);
            if (i < 0) return;
            const SpeedDistance::Sample &s = t.samples.at(i);
            p.setPen(QPen(base(), 1));
            p.setBrush(c);
            if (anyLoc && SpeedDistance::locationKnown(s)) p.drawEllipse(QPointF(x, yOfLoc(loc, s.locM)), 4, 4);
            p.drawEllipse(QPointF(x, yOfSpeed(spd, s.speedKmh)), 4, 4);
        };
        dots(m_pair.a, colA);
        dots(m_pair.b, colB);
    }
}

QColor TwoLocoCanvas::base() const { return palette().color(QPalette::Base); }

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
    resize(1100, 680);

    m_boxA = new QComboBox(this);
    m_boxB = new QComboBox(this);
    m_canvas = new TwoLocoCanvas(this);
    m_status = new StatusLine;

    auto *picker = new QHBoxLayout;
    picker->addWidget(new QLabel(tr("Loco A:")));
    picker->addWidget(m_boxA, 1);
    picker->addWidget(new QLabel(tr("Loco B:")));
    picker->addWidget(m_boxB, 1);
    picker->addSpacing(12);
    // How far apart the two locos' locations may be before the view warns
    // they may not be on the same section (session 132; configurable at
    // Abhinav's request). Remembered.
    m_warnApart = new QDoubleSpinBox(this);
    m_warnApart->setObjectName(QStringLiteral("warnApart"));
    m_warnApart->setRange(0.0, 1000.0);
    m_warnApart->setDecimals(1);
    m_warnApart->setSingleStep(1.0);
    m_warnApart->setSuffix(tr(" km"));
    m_warnApart->setSpecialValueText(tr("any gap"));
    m_warnApart->setValue(Settings::twoLocoWarnApartKm());
    m_warnApart->setToolTip(tr("Warn that the locos may not be on the same section when their\n"
                               "locations are further apart than this at their nearest.\n"
                               "\"any gap\" (0) warns whenever they do not overlap at all."));
    auto *warnLabel = new QLabel(tr("Warn if apart by more than"));
    warnLabel->setBuddy(m_warnApart);
    picker->addWidget(warnLabel);
    picker->addWidget(m_warnApart);

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
    connect(m_warnApart, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double km) {
        Settings::setTwoLocoWarnApartKm(km);
        rebuild();
    });
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
    m_pair = TwoLocoView::build(modelA, keyA, modelB, keyB, 0, 0, 2000, m_warnApart->value() * 1000.0);
    m_canvas->setPair(m_pair);

    QString tip;
    if (!m_pair.plausible) {
        // The plot's banner says it; the full sentence is the tooltip, so
        // the line stays one line.
        m_status->warn(m_pair.hasLocA && m_pair.hasLocB
                           ? tr("%1 gap samples \u00B7 the locos may not be on the same section").arg(m_pair.gap.size())
                           : tr("No gap: a loco never localised"));
        tip = m_pair.warning;
    } else {
        QStringList parts{ m_pair.gap.size() == 1 ? tr("1 gap sample") : tr("%1 gap samples").arg(m_pair.gap.size()) };
        // What was left out, said, so a broken line is not read as missing data.
        auto lost = [](const QString &who, int unknown, int total) {
            return tr("%1: %2 of %3 frames not localised (0 m)").arg(who).arg(unknown).arg(total);
        };
        if (m_pair.unknownA > 0) parts << lost(QStringLiteral("A"), m_pair.unknownA, m_pair.a.samples.size());
        if (m_pair.unknownB > 0) parts << lost(QStringLiteral("B"), m_pair.unknownB, m_pair.b.samples.size());
        m_status->state(parts.join(QStringLiteral(" \u00B7 ")));
    }
    if (m_pair.unknownA + m_pair.unknownB > 0) {
        if (!tip.isEmpty()) tip += QStringLiteral("\n\n");
        tip += tr("A loco that has not localised on an RFID tag reports 0 m.\n"
                  "Its location line breaks there, and no gap is computed\n"
                  "until both locos report a location. Speed is drawn as is.");
    }
    m_status->setToolTip(tip);
}

void TwoLocoWindow::onCursorChanged(qint64)
{
    // The readout is in the canvas's header now, next to what it describes;
    // the status line stays on the summary instead of flickering with it.
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
