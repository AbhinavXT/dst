#include "brakechainwindow.h"

#include "statusline.h"
#include "uicolors.h"
#include "windowgeometry.h"

#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHelpEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

using namespace BrakeChain;

namespace {

constexpr double kKgMax = 6.0;          // BP 5, BC 3.5: the chart's pressure axis
constexpr qint64 kTailMs = 3000;        // after the last link seen
constexpr qint64 kMinSpanMs = 10000;

QString shortName(int step)
{
    switch (step) {
    case Decision:       return QStringLiteral("decides");
    case Output:         return QStringLiteral("output");
    case RelayFeedback:  return QStringLiteral("relay");
    case BiuCommand:     return QStringLiteral("BIU cmd");
    case BiuMoves:       return QStringLiteral("BIU moves");
    case BiuAtTarget:    return QStringLiteral("BIU at target");
    case AnalogMoves:    return QStringLiteral("analog moves");
    case AnalogAtTarget: return QStringLiteral("analog at target");
    case SpeedFalls:     return QStringLiteral("speed falls");
    case Standstill:     return QStringLiteral("standstill");
    default:             return QString();
    }
}

bool isAir(int id) { return id != Speed; }

}  // namespace

// ---- the chart ------------------------------------------------------------------------

BrakeChainChart::BrakeChainChart(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void BrakeChainChart::setApplication(const Chain *chain, int app)
{
    m_chain = chain;
    m_app = chain && app >= 0 && app < chain->apps.size() ? app : -1;
    m_from = m_to = 0;
    if (m_app >= 0) {
        const Application &a = chain->apps.at(m_app);
        qint64 last = a.toMs;
        for (int s = 0; s < StepCount; ++s) if (a.links[s].seen()) last = qMax(last, a.links[s].ms);
        m_from = a.fromMs - kLeadMs;
        m_to = qMin(qMax(last + kTailMs, m_from + kMinSpanMs), a.fromMs + kFollowMs);
        m_speedMax = 10;
        for (const Point &p : chain->series[Speed])
            if (p.ms >= m_from && p.ms <= m_to) m_speedMax = qMax(m_speedMax, p.v);
        m_speedMax = std::ceil(m_speedMax / 10.0) * 10.0;
    }
    update();
}

QRect BrakeChainChart::plotRect() const
{
    const int fh = fontMetrics().height();
    return QRect(48, fh + 10, qMax(10, width() - 48 - 52), qMax(10, height() - (fh + 10) - (4 * fh + 12)));
}

int BrakeChainChart::xAtMs(qint64 ms) const
{
    const QRect r = plotRect();
    if (m_to <= m_from) return r.left();
    return r.left() + int(double(ms - m_from) * r.width() / double(m_to - m_from));
}

qint64 BrakeChainChart::msAtX(int x) const
{
    const QRect r = plotRect();
    if (m_to <= m_from || r.width() <= 0) return m_from;
    return m_from + qint64(double(x - r.left()) * double(m_to - m_from) / r.width());
}

void BrakeChainChart::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Base));
    QFont small = font();
    small.setPointSizeF(qMax(7.5, font().pointSizeF() - 1.0));
    p.setFont(small);
    const QFontMetrics fm(small);
    if (m_app < 0 || !m_chain) {
        p.setPen(UiColor::muted());
        p.drawText(rect(), Qt::AlignCenter, tr("Pick a brake application above."));
        return;
    }
    const Application &a = m_chain->apps.at(m_app);
    const QRect r = plotRect();
    p.setRenderHint(QPainter::Antialiasing, true);

    // Grid and axes: kg/cm² on the left, km/h on the right, seconds from the decision below.
    p.setPen(QPen(UiColor::grid(), 1));
    for (int k = 0; k <= int(kKgMax); ++k) {
        const int y = r.bottom() - int(k * r.height() / kKgMax);
        p.drawLine(r.left(), y, r.right(), y);
    }
    p.setPen(QPen(UiColor::frame(), 1));
    p.drawRect(r);
    p.setPen(UiColor::muted());
    for (int k = 0; k <= int(kKgMax); ++k) {
        const int y = r.bottom() - int(k * r.height() / kKgMax);
        p.drawText(QRect(0, y - fm.height() / 2, r.left() - 6, fm.height()), Qt::AlignRight | Qt::AlignVCenter, QString::number(k));
        const double kmh = m_speedMax * k / kKgMax;
        p.drawText(QRect(r.right() + 6, y - fm.height() / 2, 46, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, QString::number(kmh, 'f', 0));
    }
    p.drawText(QRect(0, 0, r.left() + 40, fm.height() + 6), Qt::AlignLeft | Qt::AlignVCenter, tr("kg/cm²"));
    p.drawText(QRect(r.right() - 40, 0, width() - r.right() + 40, fm.height() + 6), Qt::AlignRight | Qt::AlignVCenter, tr("km/h"));
    const qint64 spanS = (m_to - m_from) / 1000;
    const qint64 step = spanS > 40 ? 10 : spanS > 16 ? 5 : spanS > 8 ? 2 : 1;
    for (qint64 s = -kLeadMs / 1000; a.fromMs + s * 1000 <= m_to; s += step) {
        const int x = xAtMs(a.fromMs + s * 1000);
        if (x < r.left()) continue;
        p.setPen(QPen(UiColor::frame(), 1));
        p.drawLine(x, r.bottom(), x, r.bottom() + 3);
        p.setPen(UiColor::muted());
        p.drawText(QRect(x - 30, r.bottom() + 3, 60, fm.height()), Qt::AlignHCenter | Qt::AlignTop,
                   s == 0 ? QStringLiteral("0") : QStringLiteral("%1%2 s").arg(s > 0 ? QStringLiteral("+") : QString()).arg(s));
    }

    // Series: held from one sample to the next, as the logs report them.
    QStringList legend;
    QVector<QColor> legendInk;
    for (int id = 0; id < SeriesCount; ++id) {
        const QVector<Point> &pts = m_chain->series[id];
        QPainterPath path;
        bool started = false;
        double lastY = 0;
        for (int k = 0; k < pts.size(); ++k) {
            const Point &pt = pts.at(k);
            if (pt.ms > m_to) break;
            if (pt.ms < m_from && !(k + 1 < pts.size() && pts.at(k + 1).ms >= m_from)) continue;
            const double v = isAir(id) ? pt.v / kKgMax : pt.v / m_speedMax;
            const double y = r.bottom() - qBound(0.0, v, 1.05) * r.height();
            const double x = qMax(r.left(), xAtMs(pt.ms));
            if (!started) { path.moveTo(x, y); started = true; }
            else { path.lineTo(x, lastY); path.lineTo(x, y); }
            lastY = y;
        }
        if (!started) continue;
        path.lineTo(qMin(r.right(), xAtMs(qMin(m_to, m_chain->toMs))), lastY);
        const QColor c = UiColor::series(id);
        QPen pen(c, id == Speed ? 2.0 : 1.6);
        if (id == A9 || id == Sa9) pen.setStyle(Qt::DashLine);
        if (id >= AnalogTopBp) pen.setStyle(Qt::DotLine);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        legend << seriesName(id);
        legendInk << c;
    }
    p.setRenderHint(QPainter::Antialiasing, false);
    int lx = r.left() + 60;
    for (int k = 0; k < legend.size(); ++k) {
        p.fillRect(QRect(lx, fm.height() / 2 + 2, 12, 3), legendInk.at(k));
        p.setPen(palette().color(QPalette::Text));
        p.drawText(QPoint(lx + 16, fm.ascent() + 2), legend.at(k));
        lx += 16 + fm.horizontalAdvance(legend.at(k)) + 14;
    }

    // A mark at each link seen; links within a few pixels share one label,
    // and a label that would touch the one before drops a row (three rows).
    struct Mark { int x; QStringList names; bool decision; };
    QVector<Mark> marks;
    for (int s = 0; s < StepCount; ++s) {
        const Link &l = a.links[s];
        if (!l.seen()) continue;
        const int x = xAtMs(l.ms);
        if (x < r.left() || x > r.right()) continue;
        bool joined = false;
        for (Mark &m : marks)
            if (qAbs(m.x - x) <= 3) { m.names << shortName(s); joined = true; break; }
        if (!joined) marks << Mark{ x, { shortName(s) }, s == Decision };
    }
    std::sort(marks.begin(), marks.end(), [](const Mark &u, const Mark &v) { return u.x < v.x; });
    int lastRight[3] = { -100000, -100000, -100000 };
    for (const Mark &m : marks) {
        const QColor ink = m.decision ? UiColor::error() : UiColor::accent();
        p.setPen(QPen(ink, m.decision ? 2 : 1, m.decision ? Qt::SolidLine : Qt::DashLine));
        p.drawLine(m.x, r.top(), m.x, r.bottom());
        const QString text = m.names.join(QStringLiteral(" · "));
        const int w = fm.horizontalAdvance(text) + 8;
        int left = qBound(0, m.x - w / 2, qMax(0, width() - w));
        int row = 0;
        while (row < 3 && left <= lastRight[row]) ++row;
        if (row == 3) {   // every row taken here: after the least-filled one's label
            row = int(std::min_element(lastRight, lastRight + 3) - lastRight);
            left = lastRight[row] + 4;
        }
        lastRight[row] = left + w;
        p.setPen(UiColor::withContrast(ink, palette().color(QPalette::Base), 4.5));
        p.drawText(QRect(left, r.bottom() + fm.height() + 4 + row * fm.height(), w, fm.height()), Qt::AlignCenter, text);
    }
}

QString BrakeChainChart::describeAt(const QPoint &pos) const
{
    if (m_app < 0 || !m_chain) return QString();
    const Application &a = m_chain->apps.at(m_app);
    QStringList lines;
    for (int s = 0; s < StepCount; ++s) {
        const Link &l = a.links[s];
        if (l.seen() && qAbs(xAtMs(l.ms) - pos.x()) <= 4)
            lines << QStringLiteral("%1 %2: %3").arg(stepName(s), delayText(l.ms - a.fromMs), l.what);
    }
    const qint64 ms = msAtX(pos.x());
    if (ms < m_from || ms > m_to) return lines.join(QLatin1Char('\n'));
    lines << tr("%1 from the decision").arg(delayText(ms - a.fromMs));
    for (int id = 0; id < SeriesCount; ++id) {
        const Point *held = nullptr;
        for (const Point &pt : m_chain->series[id]) {
            if (pt.ms > ms) break;
            held = &pt;
        }
        if (held && held->ms >= m_from - kFollowMs)
            lines << QStringLiteral("%1: %2").arg(seriesName(id), QString::number(held->v, 'f', id == Speed ? 0 : 2));
    }
    return lines.join(QLatin1Char('\n'));
}

void BrakeChainChart::mousePressEvent(QMouseEvent *e)
{
    if (m_app >= 0 && plotRect().contains(e->pos())) emit timeClicked(msAtX(e->pos().x()));
}

bool BrakeChainChart::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        const auto *he = static_cast<QHelpEvent *>(e);
        const QString text = describeAt(he->pos());
        if (text.isEmpty()) QToolTip::hideText();
        else QToolTip::showText(he->globalPos(), text, this);
        return true;
    }
    return QWidget::event(e);
}

// ---- the window -----------------------------------------------------------------------

BrakeChainWindow::BrakeChainWindow(LogModel *model, const QString &tabKey, const QString &tabName, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_model(model)
    , m_tabKey(tabKey)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Brake chain — %1").arg(tabName.isEmpty() ? tabKey : tabName));
    WindowGeometry::makeResizableWindow(this);
    resize(1100, 680);

    auto *note = new QLabel(tr("Each brake KAVACH applied (DMI brake_type NB / FSB / EB / LE), and the time from that "
                               "decision to each link the capture shows: the brake output, the relay feedback, the BIU's "
                               "command and pressures, the analog card's pressures, the speed. “—”: not in the capture. "
                               "A .cap is stamped to the second, so links in the same second read +0 s."), this);
    note->setWordWrap(true);
    note->setStyleSheet(UiColor::mutedStyle());

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("brakeChainTable"));
    QStringList head{ tr("Time"), tr("Brake"), tr("Speed") };
    for (int s = Output; s < StepCount; ++s) head << stepName(s);
    head << tr("Reasons");
    m_table->setColumnCount(head.size());
    m_table->setHorizontalHeaderLabels(head);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);

    m_chart = new BrakeChainChart;
    m_chart->setObjectName(QStringLiteral("brakeChainChart"));
    m_detail = new QLabel(this);
    m_detail->setWordWrap(true);
    m_detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *lower = new QWidget(this);
    auto *lv = new QVBoxLayout(lower);
    lv->setContentsMargins(0, 0, 0, 0);
    lv->addWidget(m_chart, 1);
    lv->addWidget(m_detail);
    auto *split = new QSplitter(Qt::Vertical, this);
    split->addWidget(m_table);
    split->addWidget(lower);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);

    m_status = new StatusLine;
    auto *rebuildBtn = new QPushButton(tr("Rebuild"), this);
    auto *csv = new QPushButton(tr("Export CSV…"), this);
    auto *save = new QPushButton(tr("Save image…"), this);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_status, 1);
    buttons->addWidget(rebuildBtn);
    buttons->addWidget(csv);
    buttons->addWidget(save);
    auto *root = new QVBoxLayout(this);
    root->addWidget(note);
    root->addWidget(split, 1);
    root->addLayout(buttons);

    connect(rebuildBtn, &QPushButton::clicked, this, &BrakeChainWindow::rebuild);
    connect(csv, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, tr("Export the brake chain"),
                                                          QStringLiteral("brake_chain_%1.csv").arg(m_tabKey), tr("CSV (*.csv)"));
        if (path.isEmpty()) return;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text) && f.write(toCsv(m_chain).toUtf8()) >= 0) m_status->ok(tr("Saved %1").arg(path));
        else m_status->fail(tr("Could not write %1").arg(path));
    });
    connect(save, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, tr("Save the brake chain"),
                                                          QStringLiteral("brake_chain_%1.png").arg(m_tabKey), tr("PNG (*.png)"));
        if (path.isEmpty()) return;
        if (m_chart->grab().save(path, "PNG")) m_status->ok(tr("Saved %1").arg(path));
        else m_status->fail(tr("Could not write %1").arg(path));
    });
    connect(m_table, &QTableWidget::currentCellChanged, this, [this](int row) { showApplication(row); });
    // Double-click a cell: the log at that link (the decision for the first columns).
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
        if (row < 0 || row >= m_chain.apps.size()) return;
        const Application &a = m_chain.apps.at(row);
        const int step = col >= 3 && col < 3 + (StepCount - Output) ? Output + (col - 3) : int(Decision);
        emit jumpRequested(m_tabKey, a.links[step].seen() ? a.links[step].ms : a.fromMs);
    });
    connect(m_chart, &BrakeChainChart::timeClicked, this, [this](qint64 ms) { emit jumpRequested(m_tabKey, ms); });
    rebuild();
}

void BrakeChainWindow::rebuild()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_chain = build(m_model);
    QApplication::restoreOverrideCursor();
    fillTable();
    if (m_chain.apps.isEmpty()) {
        m_status->state(tr("No brake in this tab: no @dmi with brake_type NB / FSB / EB / LE."));
        showApplication(-1);
        return;
    }
    QStringList missing;
    if (!m_chain.dioFrames) missing << tr("no @dop / @dip (they come over the serial terminal)");
    if (!m_chain.biuCommands) missing << tr("no @biu_cmd (the VCC's biu_dlconsole_mirror)");
    if (!m_chain.analogFrames) missing << tr("no @analog_top / @analog_bottom");
    const QString head = tr("%1 brake application(s)").arg(m_chain.apps.size());
    if (missing.isEmpty()) m_status->state(head);
    else m_status->warn(head + QStringLiteral("; ") + missing.join(QStringLiteral("; ")));
    m_table->setCurrentCell(0, 0);
}

void BrakeChainWindow::fillTable()
{
    m_table->setRowCount(m_chain.apps.size());
    for (int i = 0; i < m_chain.apps.size(); ++i) {
        const Application &a = m_chain.apps.at(i);
        auto put = [this, i](int col, const QString &text, const QString &tip = QString()) {
            auto *item = new QTableWidgetItem(text);
            if (!tip.isEmpty()) item->setToolTip(tip);
            m_table->setItem(i, col, item);
            return item;
        };
        put(0, QDateTime::fromMSecsSinceEpoch(a.fromMs).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
        put(1, a.type);
        put(2, a.speedAtOnset < 0 ? QStringLiteral("—") : tr("%1 km/h").arg(a.speedAtOnset));
        for (int s = Output; s < StepCount; ++s) {
            QString text = QStringLiteral("—");
            if (a.links[s].seen()) text = delayText(a.delayMs(s));
            else if ((s == SpeedFalls || s == Standstill) && a.speedAtOnset == 0) text = tr("standing");
            QTableWidgetItem *item = put(3 + (s - Output), text, a.links[s].what);
            item->setTextAlignment(Qt::AlignCenter);
            if (!a.links[s].seen()) item->setForeground(UiColor::muted());
        }
        put(3 + (StepCount - Output), a.reasons.isEmpty() ? tr("no reason in the capture") : a.reasons.join(QStringLiteral("; ")));
    }
    m_table->resizeColumnsToContents();
}

void BrakeChainWindow::showApplication(int app)
{
    m_chart->setApplication(&m_chain, app);
    if (app < 0 || app >= m_chain.apps.size()) {
        m_detail->clear();
        return;
    }
    const Application &a = m_chain.apps.at(app);
    QStringList lines;
    for (int s = 0; s < StepCount; ++s) {
        const Link &l = a.links[s];
        const bool standing = !l.seen() && (s == SpeedFalls || s == Standstill) && a.speedAtOnset == 0;
        lines << (l.seen() ? QStringLiteral("<b>%1</b> %2 · %3").arg(stepName(s).toHtmlEscaped(), delayText(a.delayMs(s)), l.what.toHtmlEscaped())
                           : QStringLiteral("<span style=\"%1\">%2: %3</span>")
                                 .arg(UiColor::style(UiColor::muted()), stepName(s).toHtmlEscaped(),
                                      standing ? tr("standing at the decision (0 km/h)") : tr("not in the capture")));
    }
    m_detail->setText(lines.join(QStringLiteral("<br>")));
}
