#include "stationlayoutwindow.h"

#include "settings.h"
#include "statusline.h"
#include "uicolors.h"
#include "undolog.h"
#include "windowgeometry.h"

#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHelpEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kLeft = 120;      // lane names
constexpr int kRight = 30;
constexpr int kTop = 46;        // station markers, texts above
constexpr int kLaneGap = 96;
constexpr int kBottom = 96;     // texts below, the legend, the scale

QString km(qint64 m) { return QString::number(m / 1000.0, 'f', 3) + QStringLiteral(" km"); }

bool isDuplicateName(const QString &n) { return n.endsWith(QLatin1Char('D')); }

// Labels of one band, up to `rows` rows deep, so neighbours do not collide.
// Returns the row used, or -1: no room (the mark's tooltip still has it).
// Marks must come in left-to-right order within a band for this to pack well;
// out-of-order ones just find less room.
int placeLabel(QHash<int, QVector<double>> &ends, int band, double x, int w, int rows)
{
    for (int row = 0; row < rows; ++row) {
        QVector<double> &e = ends[band * 8 + row];
        bool clear = true;
        for (int i = 0; i + 1 < e.size(); i += 2)
            if (x + w / 2.0 + 3 > e.at(i) && x - w / 2.0 - 3 < e.at(i + 1)) { clear = false; break; }
        if (!clear) continue;
        e << x - w / 2.0 << x + w / 2.0;
        return row;
    }
    return -1;
}

}  // namespace

QMenu *stationLibraryMenu(QWidget *parent, const std::function<void(const QString &)> &pick)
{
    auto *menu = new QMenu(QObject::tr("Library"), parent);
    QHash<QString, QMenu *> groups;
    for (const QString &path : StationLayout::library()) {
        const QString g = StationLayout::libraryGroup(path);
        QMenu *m = menu;
        if (!g.isEmpty()) {
            if (!groups.contains(g)) groups.insert(g, menu->addMenu(g));
            m = groups.value(g);
        }
        QString name = QFileInfo(path).completeBaseName();
        if (path == StationLayout::defaultFile()) name += QObject::tr(" (default)");
        QAction *a = m->addAction(name);
        a->setData(path);
        QObject::connect(a, &QAction::triggered, menu, [pick, path]() { pick(path); });
    }
    return menu;
}

// =============================================================================
//  StationLayoutCanvas
// =============================================================================

StationLayoutCanvas::StationLayoutCanvas(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    UiColor::onThemeChange(this, [this]() { update(); });
}

void StationLayoutCanvas::setStation(const StationLayout::Layout &l)
{
    m_l = l;
    layoutLanes();
    updateGeometry();
    update();
}

void StationLayoutCanvas::layoutLanes()
{
    m_loc = StationLayout::tagLocations(m_l);
    m_lane.clear();
    m_laneNames.clear();
    m_laneOfLine.clear();
    for (const StationLayout::Line &ln : m_l.lines) {
        if (m_laneOfLine.contains(ln.line)) continue;
        m_laneOfLine.insert(ln.line, m_laneNames.size());
        m_laneNames << (ln.name.isEmpty() || ln.name == ln.line ? ln.line : ln.name + QStringLiteral("  ") + ln.line);
    }
    const QHash<QString, QString> lineOf = StationLayout::lineOfTag(m_l);
    bool orphan = false;
    for (const StationLayout::Tag &t : m_l.tags) {
        // A duplicate tag rides its main tag's line.
        QString line = lineOf.value(t.name);
        if (line.isEmpty() && isDuplicateName(t.name)) line = lineOf.value(t.name.chopped(1));
        if (m_laneOfLine.contains(line)) m_lane.insert(t.name, m_laneOfLine.value(line));
        else { m_lane.insert(t.name, -1); orphan = true; }
    }
    if (orphan) {
        const int lane = m_laneNames.size();
        m_laneNames << tr("(no line)");
        for (auto it = m_lane.begin(); it != m_lane.end(); ++it) if (it.value() < 0) it.value() = lane;
    }
    if (m_laneNames.isEmpty()) m_laneNames << tr("(no line)");

    m_flagged.clear();
    for (const QString &c : StationLayout::checks(m_l)) {
        const QString tag = c.section(QLatin1Char(':'), 0, 0);
        if (tag.startsWith(QLatin1String("tag "))) m_flagged.insert(tag.mid(4));
    }

    bool have = false;
    auto widen = [&](qint64 v) {
        if (v <= 0) return;
        if (!have) { m_min = m_max = v; have = true; return; }
        m_min = qMin(m_min, v);
        m_max = qMax(m_max, v);
    };
    for (qint64 v : m_loc) widen(v);
    for (const StationLayout::Point &p : m_l.points) { widen(p.loc1); widen(p.loc2); }
    for (const StationLayout::Station &s : m_l.stations) widen(s.location);
    for (const StationLayout::Text &t : m_l.texts) widen(t.location);
    if (!have) { m_min = 0; m_max = 1000; }
    const qint64 pad = qMax<qint64>(50, (m_max - m_min) / 50);
    m_min -= pad;
    m_max += pad;
}

int StationLayoutCanvas::laneY(int lane) const { return kTop + 30 + lane * kLaneGap; }

QSize StationLayoutCanvas::sizeHint() const
{
    return QSize(int(1000 * m_zoom), laneY(m_laneNames.size() - 1) + kBottom + 20);
}

QSize StationLayoutCanvas::minimumSizeHint() const
{
    return QSize(int(qMax(400.0, 600 * m_zoom)), laneY(m_laneNames.size() - 1) + kBottom + 20);
}

void StationLayoutCanvas::setZoom(double z)
{
    m_zoom = qBound(1.0, z, 40.0);
    setMinimumWidth(int(qMax(400.0, 600 * m_zoom)));
    updateGeometry();
    update();
}

void StationLayoutCanvas::setSelectedTag(const QString &name)
{
    m_selected = name;
    update();
}

double StationLayoutCanvas::xOf(qint64 locM) const
{
    const double w = qMax(1, width() - kLeft - kRight);
    return kLeft + double(locM - m_min) / double(qMax<qint64>(1, m_max - m_min)) * w;
}

qint64 StationLayoutCanvas::locAt(double x) const
{
    const double w = qMax(1, width() - kLeft - kRight);
    return m_min + qint64(std::llround((x - kLeft) / w * double(m_max - m_min)));
}

QRectF StationLayoutCanvas::tagRect(const QString &name) const
{
    if (!m_loc.contains(name)) return QRectF();
    const double x = xOf(m_loc.value(name));
    const int y = laneY(m_lane.value(name, 0));
    return QRectF(x - 6, y - 6, 12, 12);
}

bool StationLayoutCanvas::event(QEvent *e)
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

void StationLayoutCanvas::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
    for (const Hit &h : m_hits) {
        if (h.tag.isEmpty() || !h.rect.contains(e->pos())) continue;
        m_dragTag = h.tag;
        m_dragX = -1;
        emit tagClicked(h.tag);
        return;
    }
}

void StationLayoutCanvas::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragTag.isEmpty() || !(e->buttons() & Qt::LeftButton)) return;
    m_dragX = qBound(double(kLeft), double(e->pos().x()), double(width() - kRight));
    setCursor(Qt::SizeHorCursor);
    update();
}

void StationLayoutCanvas::mouseReleaseEvent(QMouseEvent *)
{
    const QString tag = m_dragTag;
    const double x = m_dragX;
    m_dragTag.clear();
    m_dragX = -1;
    unsetCursor();
    if (!tag.isEmpty() && x >= 0 && std::fabs(x - xOf(m_loc.value(tag))) >= 2.0) emit tagDragged(tag, locAt(x));
    update();
}

void StationLayoutCanvas::wheelEvent(QWheelEvent *e)
{
    if (!(e->modifiers() & Qt::ControlModifier)) { QWidget::wheelEvent(e); return; }
    setZoom(m_zoom * (e->angleDelta().y() > 0 ? 1.25 : 0.8));
    e->accept();
}

void StationLayoutCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), palette().base());
    const QColor text = palette().color(QPalette::Text), ink = UiColor::muted();
    QFont small = font();
    small.setPointSizeF(qMax(6.0, small.pointSizeF() - 1.0));
    p.setFont(small);
    const QFontMetrics fm(small);
    const int lh = fm.height();
    m_hits.clear();

    if (m_l.tags.isEmpty() && m_l.points.isEmpty()) {
        p.setPen(ink);
        p.drawText(rect(), Qt::AlignCenter, tr("No station layout: Open a .json or Import the Python tool's .xlsx"));
        return;
    }

    // ---- lanes ------------------------------------------------------------------------
    for (int lane = 0; lane < m_laneNames.size(); ++lane) {
        const int y = laneY(lane);
        qint64 lo = -1, hi = -1;
        for (auto it = m_lane.constBegin(); it != m_lane.constEnd(); ++it) {
            if (it.value() != lane || !m_loc.contains(it.key())) continue;
            const qint64 v = m_loc.value(it.key());
            lo = lo < 0 ? v : qMin(lo, v);
            hi = hi < 0 ? v : qMax(hi, v);
        }
        p.setPen(text);
        p.drawText(QRect(4, y - lh, kLeft - 12, 2 * lh), Qt::AlignRight | Qt::AlignVCenter,
                   fm.elidedText(m_laneNames.at(lane), Qt::ElideRight, kLeft - 12));
        if (lo < 0) continue;
        p.setPen(QPen(UiColor::frame(), 3));
        p.drawLine(QPointF(xOf(lo), y), QPointF(xOf(hi), y));
    }

    // ---- points: a link between two lanes -------------------------------------------------
    QHash<int, QVector<double>> ptEnds;
    for (const StationLayout::Point &pt : m_l.points) {
        if (!m_laneOfLine.contains(pt.line1) || !m_laneOfLine.contains(pt.line2)) continue;
        const QPointF a(xOf(pt.loc1), laneY(m_laneOfLine.value(pt.line1)));
        const QPointF b(xOf(pt.loc2), laneY(m_laneOfLine.value(pt.line2)));
        p.setPen(QPen(UiColor::frame(), 2));
        p.drawLine(a, b);
        const QPointF mid = (a + b) / 2.0;
        const int w = fm.horizontalAdvance(pt.name) + 4;
        if (placeLabel(ptEnds, 0, mid.x() + w / 2.0 + 3, w, 1) >= 0) {
            p.setPen(ink);
            p.drawText(QRectF(mid.x() + 3, mid.y() - lh / 2.0, w, lh), Qt::AlignLeft | Qt::AlignVCenter, pt.name);
        }
        m_hits << Hit{ QRectF(qMin(a.x(), b.x()) - 3, qMin(a.y(), b.y()) - 3, std::fabs(b.x() - a.x()) + 6, std::fabs(b.y() - a.y()) + 6),
                       QString(), tr("Point %1: %2 on %3 to %4 on %5").arg(pt.name, km(pt.loc1), pt.line1, km(pt.loc2), pt.line2) };
    }

    // ---- stations -----------------------------------------------------------------------------
    const int lanesBottom = laneY(m_laneNames.size() - 1) + 24;
    for (const StationLayout::Station &s : m_l.stations) {
        const double x = xOf(s.location);
        p.setPen(QPen(UiColor::accent(), 1, Qt::DashLine));
        p.drawLine(QPointF(x, kTop), QPointF(x, lanesBottom));
        p.setPen(UiColor::accent());
        p.drawText(QRectF(x - 60, kTop - lh - 2, 120, lh), Qt::AlignCenter, tr("Station %1").arg(s.id));
        m_hits << Hit{ QRectF(x - 3, kTop - lh, 6, lanesBottom - kTop + lh), QString(),
                       tr("Station %1 at %2").arg(s.id).arg(km(s.location)) };
    }

    // ---- texts: above the lanes when posy < 0, else below ----------------------------------------
    for (const StationLayout::Text &t : m_l.texts) {
        const double x = xOf(t.location);
        const int w = fm.horizontalAdvance(t.text) + 4;
        const QRectF r(x - w / 2.0, t.posY < 0 ? 4 : lanesBottom + 4, w, lh);
        p.setPen(ink);
        p.drawText(r, Qt::AlignCenter, t.text);
        m_hits << Hit{ r, QString(), tr("Text at %1: %2").arg(km(t.location), t.text) };
    }

    // ---- signals at their foot tags ---------------------------------------------------------------
    QHash<int, QVector<double>> sigEnds;
    for (const StationLayout::Signal &s : m_l.signalList) {
        if (!m_loc.contains(s.footTag)) continue;
        const double x = xOf(m_loc.value(s.footTag));
        const int y = laneY(m_lane.value(s.footTag, 0));
        p.setPen(QPen(UiColor::signalLamp(QStringLiteral("red")), 2));
        p.drawLine(QPointF(x, y + 7), QPointF(x, y + 18));
        p.setBrush(UiColor::signalLamp(QStringLiteral("red")));
        p.drawEllipse(QPointF(x, y + 20), 3, 3);
        const int w = fm.horizontalAdvance(s.name) + 4;
        const int row = placeLabel(sigEnds, m_lane.value(s.footTag, 0), x, w, 2);
        if (row >= 0) {
            p.setPen(text);
            p.drawText(QRectF(x - w / 2.0, y + 24 + row * lh, w, lh), Qt::AlignCenter, s.name);
        }
        m_hits << Hit{ QRectF(x - 6, y + 6, 12, 18 + lh), QString(),
                       tr("Signal %1 (id %2, station %3) at foot tag %4").arg(s.name).arg(s.sigId).arg(s.stationId).arg(s.footTag) };
    }

    // ---- tags: labels above, staggered over two rows so neighbours do not collide ---------------
    QHash<int, QVector<double>> tagEnds;
    for (const StationLayout::Tag &t : m_l.tags) {
        if (!m_loc.contains(t.name)) continue;
        const bool dragging = t.name == m_dragTag && m_dragX >= 0;
        const double x = dragging ? m_dragX : xOf(m_loc.value(t.name));
        const int lane = m_lane.value(t.name, 0);
        const int y = laneY(lane);
        const bool dup = isDuplicateName(t.name);
        const bool flagged = m_flagged.contains(t.name);
        QColor c = flagged ? UiColor::warning() : UiColor::series(0);
        const double r = dup ? 3.5 : 5.5;
        p.setPen(QPen(c, 1.5));
        p.setBrush(dup ? Qt::NoBrush : QBrush(c));
        QPolygonF dm;
        dm << QPointF(x, y - r) << QPointF(x + r, y) << QPointF(x, y + r) << QPointF(x - r, y);
        p.drawPolygon(dm);
        if (t.name == m_selected) {
            p.setPen(QPen(UiColor::selectedMark(), 2));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QPointF(x, y), r + 4, r + 4);
        }
        if (!dup) {
            const int w = fm.horizontalAdvance(t.name) + 4;
            const int row = placeLabel(tagEnds, lane, x, w, 2);
            if (row >= 0) {
                p.setPen(t.name == m_selected ? text : ink);
                p.drawText(QRectF(x - w / 2.0, y - 8 - (row + 1) * lh, w, lh), Qt::AlignCenter, t.name);
            }
        }
        QString tip = tr("Tag %1 at %2, line %3").arg(t.name, km(dragging ? locAt(x) : m_loc.value(t.name)),
                                                    m_laneNames.value(lane));
        if (dragging) tip += QLatin1Char('\n') + tr("Release to move it here (re-encoded, CRC-30 recomputed)");
        else tip += QLatin1Char('\n') + tr("Drag along the line to move it");
        m_hits << Hit{ QRectF(x - 7, y - 7, 14, 14), t.name, tip };
    }

    // ---- legend -----------------------------------------------------------------------------------
    {
        const int ly = height() - 2 * lh - 20;
        double x = kLeft;
        auto key = [&](const QColor &c, bool filled, const QString &label) {
            p.setPen(QPen(c, 1.5));
            p.setBrush(filled ? QBrush(c) : Qt::NoBrush);
            QPolygonF dm;
            dm << QPointF(x + 5, ly) << QPointF(x + 10, ly + 5) << QPointF(x + 5, ly + 10) << QPointF(x, ly + 5);
            p.drawPolygon(dm);
            p.setPen(ink);
            const int w = fm.horizontalAdvance(label);
            p.drawText(QRectF(x + 14, ly - 2, w + 2, lh), Qt::AlignLeft | Qt::AlignVCenter, label);
            x += 14 + w + 18;
        };
        key(UiColor::series(0), true, tr("tag"));
        key(UiColor::series(0), false, tr("duplicate tag"));
        key(UiColor::warning(), true, tr("named in Checks"));
        p.setPen(ink);
        p.drawText(QRectF(x, ly - 2, width() - x - kRight, lh), Qt::AlignLeft | Qt::AlignVCenter,
                   fm.elidedText(tr("drag a tag along its line to move it"), Qt::ElideRight, int(width() - x - kRight)));
    }

    // ---- scale ----------------------------------------------------------------------------------
    const int sy = height() - lh - 12;
    p.setPen(QPen(UiColor::frame(), 1));
    p.drawLine(kLeft, sy, width() - kRight, sy);
    const double perPx = double(m_max - m_min) / qMax(1, width() - kLeft - kRight);
    double step = 1;
    while (step / perPx < 90) step *= (QString::number(step).startsWith(QLatin1Char('2')) ? 2.5 : 2.0);
    for (double m = std::ceil(m_min / step) * step; m <= m_max; m += step) {
        const double x = xOf(qint64(m));
        p.drawLine(QPointF(x, sy), QPointF(x, sy + 4));
        p.setPen(ink);
        p.drawText(QRectF(x - 50, sy + 5, 100, lh), Qt::AlignCenter, km(qint64(m)));
        p.setPen(QPen(UiColor::frame(), 1));
    }
}

// =============================================================================
//  StationLayoutWindow
// =============================================================================

namespace {

// The generic sheet tables (1..5) show the tool's sheet with the same index
// in StationLayout::toSheets(): signals, points, lines, station, texts.
const char *kTabNames[] = { "Tags", "Signals", "Points", "Lines", "Station", "Texts" };

QTableWidget *makeTable(QWidget *parent)
{
    auto *t = new QTableWidget(parent);
    t->verticalHeader()->hide();
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::ExtendedSelection);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    t->horizontalHeader()->setStretchLastSection(true);
    t->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
    return t;
}

QTableWidgetItem *readOnly(const QString &s)
{
    auto *it = new QTableWidgetItem(s);
    it->setFlags(it->flags() & ~Qt::ItemIsEditable);
    return it;
}

}  // namespace

StationLayoutWindow::StationLayoutWindow(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setObjectName(QStringLiteral("StationLayoutWindow"));
    WindowGeometry::makeResizableWindow(this);
    resize(1180, 720);

    // Open, the built-in default, or an empty layout: one button, so the bar
    // fits a laptop with wider (Linux) fonts.
    auto *openBtn = new QPushButton(tr("Open"), this);
    auto *openMenu = new QMenu(openBtn);
    QAction *openFileAct = openMenu->addAction(tr("Open file…"));
    openFileAct->setToolTip(tr("A DLConsole station layout (.json)"));
    QAction *defaultAct = openMenu->addAction(tr("Default layout (station.xlsx)"));
    openMenu->addMenu(stationLibraryMenu(openMenu, [this](const QString &path) {
        if (confirmDiscard()) openFile(path);
    }));
    QAction *newAct = openMenu->addAction(tr("New, empty"));
    openBtn->setMenu(openMenu);
    auto *saveBtn = new QPushButton(tr("Save"), this);
    auto *saveAsBtn = new QPushButton(tr("Save as…"), this);
    auto *importBtn = new QPushButton(tr("Import .xlsx…"), this);
    importBtn->setToolTip(tr("A station file of the old Python tool (config/station/*.xlsx): tags, signals, points, "
                             "lines, station, texts; other sheets (relaymap, …) are kept for export"));
    auto *exportBtn = new QPushButton(tr("Export .xlsx…"), this);
    exportBtn->setToolTip(tr("The Python tool's station file, every sheet it reads"));
    auto *zoomIn = new QPushButton(QStringLiteral("+"), this);
    auto *zoomOut = new QPushButton(QStringLiteral("\u2212"), this);
    auto *fit = new QPushButton(tr("Fit"), this);
    zoomIn->setToolTip(tr("Zoom in (Ctrl + mouse wheel on the drawing zooms too)"));
    zoomOut->setToolTip(tr("Zoom out"));
    for (QPushButton *b : { zoomIn, zoomOut }) b->setMaximumWidth(b->fontMetrics().horizontalAdvance(QStringLiteral("MM")) + 24);
    m_undo = new UndoLog(this);
    QAction *undoAct = m_undo->createAction(this);
    addAction(undoAct);
    auto *undoBtn = new QToolButton(this);
    undoBtn->setDefaultAction(undoAct);
    undoBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);

    auto *bar = new QHBoxLayout;
    for (QWidget *w : std::initializer_list<QWidget *>{ openBtn, saveBtn, saveAsBtn, importBtn, exportBtn })
        bar->addWidget(w);
    bar->addStretch(1);
    for (QWidget *w : std::initializer_list<QWidget *>{ zoomOut, zoomIn, fit, undoBtn }) bar->addWidget(w);

    m_canvas = new StationLayoutCanvas(this);
    m_scroll = new QScrollArea(this);
    m_scroll->setWidget(m_canvas);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);

    m_tabs = new QTabWidget(this);
    m_tabs->setUsesScrollButtons(true);   // eight tabs with counts: scroll rather than widen the window
    for (int i = 0; i < 6; ++i) {
        QTableWidget *t = makeTable(this);
        m_tables << t;
        m_tabs->addTab(t, tr(kTabNames[i]));
    }
    m_tables.at(0)->setColumnCount(9);
    m_tables.at(0)->setHorizontalHeaderLabels({ tr("Tag"), tr("page_x"), tr("page_y"), tr("Abs loc (m)"), tr("Type"),
                                                tr("Unique"), tr("TIN nom / rev"), tr("CRC-30"), tr("Line") });
    m_tables.at(0)->horizontalHeaderItem(3)->setToolTip(tr("The location inside the tag's bits (abs_loc_1 on an "
                                                           "adjustment tag). Changing it re-encodes the tag: new "
                                                           "page_x / page_y, CRC-30 recomputed"));
    const QVector<XlsxBook::Sheet> empty = StationLayout::toSheets(StationLayout::Layout());
    for (int i = 1; i < 6; ++i) {
        QStringList header;
        for (const QVariant &v : empty.at(i).rows.first()) header << v.toString();
        m_tables.at(i)->setColumnCount(header.size());
        m_tables.at(i)->setHorizontalHeaderLabels(header);
    }
    m_tables.at(3)->horizontalHeaderItem(2)->setToolTip(tr("The line's tags in order, separated by commas"));
    m_checks = new QListWidget(this);
    m_tabs->addTab(m_checks, tr("Checks"));
    m_other = new QLabel(this);
    m_other->setWordWrap(true);
    m_other->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_other->setMargin(8);
    m_tabs->addTab(m_other, tr("Other sheets"));

    auto *addBtn = new QPushButton(tr("Add row"), this);
    addBtn->setToolTip(tr("After the selected row. On Tags: a copy of the selected tag, to move or edit"));
    auto *delBtn = new QPushButton(tr("Delete rows"), this);
    auto *rowBar = new QHBoxLayout;
    rowBar->addWidget(addBtn);
    rowBar->addWidget(delBtn);
    rowBar->addStretch(1);
    auto *bottom = new QWidget(this);
    auto *bl = new QVBoxLayout(bottom);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->addWidget(m_tabs, 1);
    bl->addLayout(rowBar);

    auto *split = new QSplitter(Qt::Vertical, this);
    split->addWidget(m_scroll);
    split->addWidget(bottom);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    split->setSizes({ 430, 230 });

    m_status = new StatusLine;
    auto *root = new QVBoxLayout(this);
    root->addLayout(bar);
    root->addWidget(split, 1);
    root->addWidget(m_status);

    connect(newAct, &QAction::triggered, this, [this]() {
        if (!confirmDiscard()) return;
        m_path.clear();
        setStation(StationLayout::Layout());
    });
    connect(defaultAct, &QAction::triggered, this, [this]() {
        if (confirmDiscard()) openFile(StationLayout::defaultFile());
    });
    connect(openFileAct, &QAction::triggered, this, [this]() {
        if (!confirmDiscard()) return;
        const QString path = QFileDialog::getOpenFileName(this, tr("Open station layout"), QFileInfo(m_path).path(),
                                                          tr("Station layout (*.json)"));
        if (!path.isEmpty()) openFile(path);
    });
    connect(importBtn, &QPushButton::clicked, this, [this]() {
        if (!confirmDiscard()) return;
        const QString path = QFileDialog::getOpenFileName(this, tr("Import station spreadsheet"), QString(),
                                                          tr("Station spreadsheet (*.xlsx)"));
        if (!path.isEmpty()) openFile(path);
    });
    auto saveAs = [this]() {
        QString path = QFileDialog::getSaveFileName(this, tr("Save station layout"),
                                                    m_path.isEmpty() ? QStringLiteral("station_layout.json") : m_path,
                                                    tr("Station layout (*.json)"));
        if (path.isEmpty()) return;
        if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".json");
        saveFile(path);
    };
    connect(saveAsBtn, &QPushButton::clicked, this, saveAs);
    connect(saveBtn, &QPushButton::clicked, this, [this, saveAs]() {
        if (m_path.isEmpty()) saveAs(); else saveFile(m_path);
    });
    connect(exportBtn, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getSaveFileName(this, tr("Export station spreadsheet"),
                                                    QFileInfo(m_path).completeBaseName() + QStringLiteral(".xlsx"),
                                                    tr("Station spreadsheet (*.xlsx)"));
        if (path.isEmpty()) return;
        if (QFileInfo(path).suffix().compare(QLatin1String("xlsx"), Qt::CaseInsensitive) != 0) path += QStringLiteral(".xlsx");
        exportXlsx(path);
    });
    connect(zoomIn, &QPushButton::clicked, this, [this]() { m_canvas->setZoom(m_canvas->zoom() * 1.5); });
    connect(zoomOut, &QPushButton::clicked, this, [this]() { m_canvas->setZoom(m_canvas->zoom() / 1.5); });
    connect(fit, &QPushButton::clicked, this, [this]() { m_canvas->setZoom(1.0); });
    connect(addBtn, &QPushButton::clicked, this, &StationLayoutWindow::addRow);
    connect(delBtn, &QPushButton::clicked, this, &StationLayoutWindow::deleteRows);
    connect(m_tables.at(0), &QTableWidget::cellChanged, this, &StationLayoutWindow::onTagCell);
    for (int i = 1; i < 6; ++i)
        connect(m_tables.at(i), &QTableWidget::cellChanged, this, [this, i](int, int) { onSheetCell(i); });
    connect(m_tables.at(0), &QTableWidget::itemSelectionChanged, this, [this]() {
        const int r = m_tables.at(0)->currentRow();
        m_canvas->setSelectedTag(r >= 0 && r < m_layout.tags.size() ? m_layout.tags.at(r).name : QString());
    });
    connect(m_canvas, &StationLayoutCanvas::tagClicked, this, [this](const QString &name) {
        for (int i = 0; i < m_layout.tags.size(); ++i)
            if (m_layout.tags.at(i).name == name) {
                m_tabs->setCurrentIndex(0);
                m_tables.at(0)->selectRow(i);
                m_tables.at(0)->scrollToItem(m_tables.at(0)->item(i, 0));
                break;
            }
    });
    connect(m_canvas, &StationLayoutCanvas::tagDragged, this, [this](const QString &name, qint64 loc) { moveTag(name, loc); });
    connect(m_undo, &UndoLog::undone, this, [this](const QString &label, bool restored) {
        if (restored) m_status->ok(tr("Undone: %1").arg(label));
    });

    setStation(StationLayout::Layout());
}

QTableWidget *StationLayoutWindow::table(int sheet) const { return m_tables.value(sheet); }

void StationLayoutWindow::setModified(bool on)
{
    m_modified = on;
    const QString name = m_path.isEmpty() ? tr("untitled") : QFileInfo(m_path).fileName();
    setWindowTitle(tr("Station Layout — %1").arg(name) + (on ? QStringLiteral(" *") : QString()));
}

void StationLayoutWindow::setStation(const StationLayout::Layout &l)
{
    m_layout = l;
    m_undo->clear();
    refreshAll();
    setModified(false);
}

void StationLayoutWindow::refreshAll()
{
    m_canvas->setStation(m_layout);
    fillTags();
    for (int i = 1; i < 6; ++i) fillSheet(i);
    const QStringList found = StationLayout::checks(m_layout);
    m_checks->clear();
    m_checks->addItems(found.isEmpty() ? QStringList{ tr("Nothing to report") } : found);
    m_tabs->setTabText(6, found.isEmpty() ? tr("Checks") : tr("Checks (%1)").arg(found.size()));
    QStringList other;
    for (const XlsxBook::Sheet &s : m_layout.otherSheets)
        other << tr("%1: %2 rows").arg(s.name).arg(qMax(0, s.rows.size() - 1));
    m_other->setText(other.isEmpty() ? tr("No other sheets.")
                                     : tr("Kept as they are and written back on export (DLConsole does not use "
                                          "them; relaymap is NMS field-input data):\n") + other.join(QLatin1Char('\n')));
    m_status->state(tr("%1 tags · %2 signals · %3 lines · %4 points").arg(m_layout.tags.size())
                        .arg(m_layout.signalList.size()).arg(m_layout.lines.size()).arg(m_layout.points.size()));
}

void StationLayoutWindow::fillTags()
{
    m_updating = true;
    QTableWidget *t = m_tables.at(0);
    t->setRowCount(m_layout.tags.size());
    const QHash<QString, QString> lineOf = StationLayout::lineOfTag(m_layout);
    for (int i = 0; i < m_layout.tags.size(); ++i) {
        const StationLayout::Tag &tag = m_layout.tags.at(i);
        const StationLayout::TagInfo in = StationLayout::info(tag);
        t->setItem(i, 0, new QTableWidgetItem(tag.name));
        t->setItem(i, 1, new QTableWidgetItem(tag.pageX));
        t->setItem(i, 2, new QTableWidgetItem(tag.pageY));
        t->setItem(i, 3, new QTableWidgetItem(in.ok ? QString::number(in.absLoc) : QString()));
        t->setItem(i, 4, readOnly(in.ok ? QString::number(in.type) : tr("does not decode")));
        t->setItem(i, 5, readOnly(in.ok ? QString::number(in.unique) + (in.duplicate ? QStringLiteral("D") : QString()) : QString()));
        t->setItem(i, 6, readOnly(in.ok ? QStringLiteral("%1 / %2").arg(in.tinNom).arg(in.tinRev) : QString()));
        auto *crc = readOnly(in.ok ? (in.crcOk ? tr("good") : tr("does not match")) : QString());
        if (in.ok && !in.crcOk) crc->setForeground(UiColor::warning());
        t->setItem(i, 7, crc);
        t->setItem(i, 8, readOnly(lineOf.value(tag.name)));
    }
    t->resizeColumnsToContents();
    m_tabs->setTabText(0, tr("Tags (%1)").arg(m_layout.tags.size()));
    m_updating = false;
}

void StationLayoutWindow::fillSheet(int sheet)
{
    m_updating = true;
    const XlsxBook::Sheet sh = StationLayout::toSheets(m_layout).at(sheet);
    QTableWidget *t = m_tables.at(sheet);
    t->setRowCount(qMax(0, sh.rows.size() - 1));
    for (int r = 1; r < sh.rows.size(); ++r)
        for (int c = 0; c < t->columnCount(); ++c)
            t->setItem(r - 1, c, new QTableWidgetItem(sh.rows.at(r).value(c).toString()));
    t->resizeColumnsToContents();
    m_tabs->setTabText(sheet, tr("%1 (%2)").arg(tr(kTabNames[sheet])).arg(t->rowCount()));
    m_updating = false;
}

void StationLayoutWindow::changed(const QString &label, const StationLayout::Layout &before)
{
    QPointer<StationLayoutWindow> self(this);
    m_undo->push(label, [self, before]() {
        if (!self) return false;
        self->m_layout = before;
        self->refreshAll();
        self->setModified(true);
        return true;
    });
    refreshAll();
    setModified(true);
}

bool StationLayoutWindow::moveTag(const QString &name, qint64 locM)
{
    for (int i = 0; i < m_layout.tags.size(); ++i) {
        if (m_layout.tags.at(i).name != name) continue;
        const StationLayout::Layout before = m_layout;
        QString err;
        if (!StationLayout::moveTag(&m_layout.tags[i], locM, &err)) {
            m_status->fail(tr("Tag %1 not moved: %2").arg(name, err));
            fillTags();
            return false;
        }
        changed(tr("move tag %1").arg(name), before);
        m_tables.at(0)->selectRow(i);
        m_status->ok(tr("Tag %1 moved to %2 m, CRC-30 recomputed").arg(name).arg(locM));
        return true;
    }
    return false;
}

void StationLayoutWindow::onTagCell(int row, int col)
{
    if (m_updating || row < 0 || row >= m_layout.tags.size()) return;
    const QString v = m_tables.at(0)->item(row, col)->text().trimmed();
    if (col == 3) {
        bool ok = false;
        const qint64 loc = v.toLongLong(&ok);
        if (!ok) { m_status->fail(tr("\"%1\" is not a location in metres").arg(v)); fillTags(); return; }
        moveTag(m_layout.tags.at(row).name, loc);
        return;
    }
    const StationLayout::Layout before = m_layout;
    StationLayout::Tag &t = m_layout.tags[row];
    if (col == 0) t.name = v;
    else if (col == 1) t.pageX = v.toLower();
    else if (col == 2) t.pageY = v.toLower();
    else return;
    changed(tr("edit tag %1").arg(t.name), before);
}

void StationLayoutWindow::onSheetCell(int sheet)
{
    if (m_updating) return;
    QVector<XlsxBook::Sheet> sheets = StationLayout::toSheets(m_layout);
    QTableWidget *t = m_tables.at(sheet);
    XlsxBook::Sheet &sh = sheets[sheet];
    sh.rows.resize(1);   // keep the header
    for (int r = 0; r < t->rowCount(); ++r) {
        XlsxBook::Row row;
        for (int c = 0; c < t->columnCount(); ++c) row << (t->item(r, c) ? t->item(r, c)->text() : QString());
        sh.rows << row;
    }
    StationLayout::Layout next;
    QString err;
    QStringList notes;
    if (!StationLayout::fromSheets(sheets, &next, &err, &notes)) { m_status->fail(err); fillSheet(sheet); return; }
    const StationLayout::Layout before = m_layout;
    m_layout = next;
    changed(tr("edit %1").arg(tr(kTabNames[sheet]).toLower()), before);
    if (!notes.isEmpty()) m_status->warn(notes.join(QStringLiteral("; ")));
}

void StationLayoutWindow::addRow()
{
    const int sheet = m_tabs->currentIndex();
    if (sheet < 0 || sheet > 5) return;
    const StationLayout::Layout before = m_layout;
    const int at = m_tables.at(sheet)->currentRow() + 1;
    if (sheet == 0) {
        const int from = at - 1;
        StationLayout::Tag t = from >= 0 && from < m_layout.tags.size() ? m_layout.tags.at(from) : StationLayout::Tag();
        m_layout.tags.insert(qBound(0, at, m_layout.tags.size()), t);
        changed(tr("add tag"), before);
    } else {
        // An empty row: it becomes a record with the first cell typed into it.
        QTableWidget *t = m_tables.at(sheet);
        const int r = qBound(0, at, t->rowCount());
        m_updating = true;
        t->insertRow(r);
        m_updating = false;
        t->setCurrentCell(r, 0);
        t->edit(t->model()->index(r, 0));
        return;
    }
    m_tables.at(sheet)->selectRow(at);
}

void StationLayoutWindow::deleteRows()
{
    const int sheet = m_tabs->currentIndex();
    if (sheet < 0 || sheet > 5) return;
    QTableWidget *t = m_tables.at(sheet);
    QList<int> rows;
    for (const QModelIndex &i : t->selectionModel()->selectedRows()) rows << i.row();
    if (rows.isEmpty()) return;
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    if (sheet == 0) {
        const StationLayout::Layout before = m_layout;
        for (int r : rows) if (r < m_layout.tags.size()) m_layout.tags.remove(r);
        changed(tr("delete %n tag(s)", nullptr, rows.size()), before);
        return;
    }
    m_updating = true;
    for (int r : rows) t->removeRow(r);
    m_updating = false;
    onSheetCell(sheet);
}

bool StationLayoutWindow::openFile(const QString &path)
{
    StationLayout::Layout l;
    QString err;
    QStringList notes;
    if (!StationLayout::load(path, &l, &err, &notes)) {
        m_status->fail(tr("Could not read %1: %2").arg(QFileInfo(path).fileName(), err));
        return false;
    }
    const bool imported = QFileInfo(path).suffix().compare(QLatin1String("xlsx"), Qt::CaseInsensitive) == 0;
    m_path = imported ? QString() : path;
    setStation(l);
    Settings::setStationLayoutLastFile(path);
    const QString what = path == StationLayout::defaultFile()
        ? tr("The built-in default layout (station.xlsx): save it as a .json to keep your changes")
        : path.startsWith(QStringLiteral(":/"))
        ? tr("Built-in layout %1: save it as a .json to keep your changes").arg(QFileInfo(path).fileName())
        : imported ? tr("Imported %1: save it as a .json to keep editing it").arg(QFileInfo(path).fileName())
                   : tr("Opened %1").arg(QFileInfo(path).fileName());
    if (notes.isEmpty()) m_status->ok(what);
    else m_status->warn(what + QStringLiteral(" · ") + notes.join(QStringLiteral("; ")));
    return true;
}

bool StationLayoutWindow::saveFile(const QString &path)
{
    QString err;
    if (!StationLayout::save(path, m_layout, &err)) { m_status->fail(err); return false; }
    m_path = path;
    setModified(false);
    Settings::setStationLayoutLastFile(path);
    m_status->ok(tr("Saved %1").arg(path));
    return true;
}

bool StationLayoutWindow::exportXlsx(const QString &path)
{
    QString err;
    if (!XlsxBook::writeFile(path, StationLayout::toSheets(m_layout), &err)) { m_status->fail(err); return false; }
    m_status->ok(tr("Exported %1").arg(path));
    return true;
}

bool StationLayoutWindow::reopenLast()
{
    // The last file, else the built-in default (session 210).
    const QString last = Settings::stationLayoutLastFile();
    if (!last.isEmpty() && QFileInfo::exists(last) && openFile(last)) return true;
    return openFile(StationLayout::defaultFile());
}

bool StationLayoutWindow::undo() { return m_undo->undo(); }

bool StationLayoutWindow::confirmDiscard()
{
    if (!m_modified) return true;
    return QMessageBox::question(this, tr("Station Layout"),
                                 tr("The layout has changes that are not saved. Discard them?"),
                                 QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel)
        == QMessageBox::Discard;
}

void StationLayoutWindow::closeEvent(QCloseEvent *e)
{
    if (confirmDiscard()) e->accept();
    else e->ignore();
}
