#include "stationlayoutwindow.h"
#include "stationinterlock.h"
#include "stationkavach.h"
#include "stationkavacheditor.h"

#include "logmodel.h"
#include "messagedispatcher.h"
#include "locosimwindow.h"
#include "routestrip.h"
#include "settings.h"
#include "statusline.h"
#include "tagbuilderwindow.h"
#include "uicolors.h"
#include "undolog.h"
#include "theme.h"
#include "tinlayout.h"
#include "uistyle.h"
#include "windowgeometry.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDate>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHelpEvent>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QTextDocument>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <climits>
#include <cmath>

namespace {

constexpr int kLeft = 120;      // lane names
constexpr int kRight = 30;
constexpr int kTop = 100;       // title, directions, texts above, the centre line's km
constexpr int kAbove = 78;      // above a lane: two rows of tag labels (km over "R-…")
constexpr int kLaneGap = 156;
constexpr int kBelow = 84;      // below the last lane: signals, gap dimensions

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
    placeTexts();
    updateGeometry();
    update();
}

void StationLayoutCanvas::layoutLanes()
{
    // Session 429: by key (StationLayout::tagKeys): every tag of a name the
    // layout lists more than once is drawn where it is.
    m_keys = StationLayout::tagKeys(m_l);
    m_keysOf.clear();
    m_loc.clear();
    m_info.clear();
    for (int i = 0; i < m_l.tags.size(); ++i) {
        const StationLayout::TagInfo in = StationLayout::info(m_l.tags.at(i));
        m_info.insert(m_keys.at(i), in);
        if (in.ok) m_loc.insert(m_keys.at(i), in.absLoc);
        m_keysOf[m_l.tags.at(i).name] << m_keys.at(i);
    }
    m_lane.clear();
    m_laneNames.clear();
    m_laneOfLine.clear();
    // Session 218: as on the RDSO layouts, DN and UP main in the middle, the
    // DN loops above DN main (DL1 nearest), the UP loops below UP main (UL1
    // nearest); any other line after them, in file order.
    QVector<const StationLayout::Line *> order;
    for (const StationLayout::Line &ln : m_l.lines) order << &ln;
    // Session 233: a line a point joins but the lines sheet does not list
    // (22.20.xlsx: lines has DM only, its points join DM to DL1, DL2) still
    // gets its lane, so the point is drawn rather than left in the table.
    m_circuits = StationLayout::circuits(m_l);   // session 436
    m_extraLines.clear();
    QSet<QString> listed;
    for (const StationLayout::Line &ln : m_l.lines) listed.insert(ln.line);
    for (const StationLayout::Point &pt : m_l.points)
        for (const QString &id : { pt.line1, pt.line2 })
            if (!id.trimmed().isEmpty() && !listed.contains(id)) {
                listed.insert(id);
                StationLayout::Line extra;
                extra.line = id;
                extra.name = id;     // Checks says it is not in the lines sheet
                m_extraLines << extra;
            }
    for (const StationLayout::Line &ln : m_extraLines) order << &ln;
    auto rank = [](const QString &line) {
        const QString u = line.toUpper();
        if (u == QLatin1String("DM")) return 1000;
        if (u == QLatin1String("UM")) return 2000;
        bool ok = false;
        const int n = u.mid(2).toInt(&ok);
        if (ok && n > 0 && n < 100 && u.startsWith(QLatin1String("DL"))) return 1000 - n;
        if (ok && n > 0 && n < 100 && u.startsWith(QLatin1String("UL"))) return 2000 + n;
        return 3000;
    };
    std::stable_sort(order.begin(), order.end(), [&](const StationLayout::Line *a, const StationLayout::Line *b) {
        return rank(a->line) < rank(b->line);
    });
    for (const StationLayout::Line *ln : order) {
        if (m_laneOfLine.contains(ln->line)) continue;
        m_laneOfLine.insert(ln->line, m_laneNames.size());
        m_laneNames << (ln->name.isEmpty() || ln->name == ln->line ? ln->line : ln->name + QStringLiteral("  ") + ln->line);
    }
    // A turnout tag (named in a point's tags) is drawn on the point's diagonal.
    m_pointOfTag.clear();
    for (int i = 0; i < m_l.points.size(); ++i) {
        const StationLayout::Point &pt = m_l.points.at(i);
        if (!m_laneOfLine.contains(pt.line1) || !m_laneOfLine.contains(pt.line2)) continue;
        for (QString n : pt.tags.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            n = n.trimmed();
            if (n.endsWith(QLatin1String(".0"))) n.chop(2);
            if (!n.isEmpty() && !m_pointOfTag.contains(n)) m_pointOfTag.insert(n, i);
        }
    }
    const QHash<QString, QString> lineOf = StationLayout::lineOfTag(m_l);
    // A tag on a line keeps its lane, even when a point names it too.
    for (auto it = m_pointOfTag.begin(); it != m_pointOfTag.end();) {
        if (!lineOf.value(it.key()).isEmpty()) it = m_pointOfTag.erase(it);
        else ++it;
    }
    bool orphan = false;
    for (int i = 0; i < m_l.tags.size(); ++i) {
        const StationLayout::Tag &t = m_l.tags.at(i);
        const QString key = m_keys.at(i);
        // A duplicate tag rides its main tag's line.
        QString line = lineOf.value(t.name);
        if (line.isEmpty() && isDuplicateName(t.name)) line = lineOf.value(t.name.chopped(1));
        const QString main = isDuplicateName(t.name) ? t.name.chopped(1) : t.name;
        if (line.isEmpty() && (m_pointOfTag.contains(t.name) || m_pointOfTag.contains(main))) {
            const int pi = m_pointOfTag.value(t.name, m_pointOfTag.value(main));
            m_pointOfTag.insert(key, pi);
            line = m_l.points.at(pi).line1;
        }
        if (m_laneOfLine.contains(line)) m_lane.insert(key, m_laneOfLine.value(line));
        else { m_lane.insert(key, -1); orphan = true; }
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

    // The yard: from the first point to the last, plus room for the signals
    // beyond them. When it is under kYard of the span, it gets kYard of the
    // width and the block sections on either side share the rest.
    constexpr double kYard = 0.6;
    qint64 ya = 0, yb = 0;
    bool yard = false;
    for (const StationLayout::Point &pt : m_l.points) {
        if (!m_laneOfLine.contains(pt.line1) || !m_laneOfLine.contains(pt.line2)) continue;
        for (const qint64 v : { pt.loc1, pt.loc2 }) {
            if (v <= 0) continue;
            if (!yard) { ya = yb = v; yard = true; }
            ya = qMin(ya, v);
            yb = qMax(yb, v);
        }
    }
    m_knots = { { m_min, 0.0 }, { m_max, 1.0 } };
    if (yard) {
        const qint64 room = qMax<qint64>(200, (yb - ya) / 4);
        ya = qMax(m_min, ya - room);
        yb = qMin(m_max, yb + room);
        const double span = double(m_max - m_min), share = (yb - ya) / span;
        if (share < kYard) {
            const double out = span - (yb - ya);
            const double fa = out > 0 ? (ya - m_min) / out * (1 - kYard) : 0;
            m_knots = { { m_min, 0.0 }, { ya, fa }, { yb, fa + kYard }, { m_max, 1.0 } };
        }
    }
}

QPointF StationLayoutCanvas::tagPos(const QString &name, double x) const
{
    if (m_pointOfTag.contains(name)) {
        const StationLayout::Point &pt = m_l.points.at(m_pointOfTag.value(name));
        const double xa = xOf(pt.loc1), xb = xOf(pt.loc2);
        const double ya = laneY(m_laneOfLine.value(pt.line1)), yb = laneY(m_laneOfLine.value(pt.line2));
        const double f = std::fabs(xb - xa) < 1 ? 0.5 : qBound(0.0, (x - xa) / (xb - xa), 1.0);
        return QPointF(x, ya + f * (yb - ya));
    }
    return QPointF(x, laneY(m_lane.value(name, 0)));
}

int StationLayoutCanvas::laneY(int lane) const { return topMargin() + kAbove + lane * kLaneGap; }

int StationLayoutCanvas::topMargin() const
{
    return kTop + (m_textRowsAbove - 1) * QFontMetrics(smallFont()).height();
}

void StationLayoutCanvas::placeTexts()
{
    const QFontMetrics fm(smallFont());
    QHash<int, QVector<double>> ends;
    m_textRow.clear();
    int above = 1, below = 1;
    for (const StationLayout::Text &t : m_l.texts) {
        // Bands 100 / 101: their own, apart from the lanes' label bands.
        const int band = t.posY < 0 ? 100 : 101;
        int row = placeLabel(ends, band, xOf(t.location), fm.horizontalAdvance(t.text) + 4, 8);
        if (row < 0) row = 7;   // shortcut: a ninth text on one spot overprints the eighth row; no library file needs that many (session230)
        m_textRow << row;
        (band == 100 ? above : below) = qMax(band == 100 ? above : below, row + 1);
    }
    if (above != m_textRowsAbove || below != m_textRowsBelow) {
        m_textRowsAbove = above;
        m_textRowsBelow = below;
        setMinimumHeight(sheetHeight(width()));
        updateGeometry();
    }
}

QFont StationLayoutCanvas::smallFont() const
{
    QFont f = font();
    f.setPointSizeF(qMax(6.0, f.pointSizeF() - 1.0));
    return f;
}

int StationLayoutCanvas::sheetHeight(int w) const
{
    const int lh = QFontMetrics(smallFont()).height();
    return laneY(m_laneNames.size() - 1) + kBelow + m_textRowsBelow * lh + 12 + legend(nullptr, 0, w) + lh + 28;
}

QSize StationLayoutCanvas::sizeHint() const
{
    const int w = int(1000 * m_zoom);
    return QSize(w, sheetHeight(qMax(width(), w)));
}

QSize StationLayoutCanvas::minimumSizeHint() const
{
    const int w = int(qMax(400.0, 600 * m_zoom));
    return QSize(w, sheetHeight(qMax(width(), w)));
}

void StationLayoutCanvas::resizeEvent(QResizeEvent *e)
{
    // The legend wraps: a new width can need a new height. The texts' rows follow the width too.
    QWidget::resizeEvent(e);
    placeTexts();
    if (sheetHeight(width()) != minimumHeight()) {
        setMinimumHeight(sheetHeight(width()));
        updateGeometry();
    }
}

// The legend (H2.25), flowed to the width; its height. Measures only when
// `p` is null.
int StationLayoutCanvas::legend(QPainter *p, int top, int w) const
{
    const QFont small = smallFont();
    const QFontMetrics fm(small);
    const int lh = fm.height(), rowH = lh + 8;
    enum Kind { Letter, Same, Ahead, Band, NonKavach, Flagged, Centre, IdBox, MainSignal, ShuntSignal, CallingOn, Note };
    struct Item { Kind kind; QChar letter; QString label; };
    QVector<Item> items;
    for (const char c : { 'N', 'S', 'T', 'X', 'D', 'G', 'L', 'A' })
        items.append({ Letter, QLatin1Char(c), TinLayout::notationName(QLatin1Char(c)) });
    items.append({ Same, QLatin1Char('N'), tr("main and duplicate at one location") });
    items.append({ Ahead, QLatin1Char('N'), tr("duplicate ahead, towards the tip") });
    items.append({ IdBox, QChar(), tr("tag id, its km above") });
    items.append({ Band, QChar(), tr("a TIN section, one colour per TIN") });
    items.append({ NonKavach, QChar(), tr("TIN 0: non-Kavach") });
    items.append({ Flagged, QLatin1Char('N'), tr("named in Checks") });
    items.append({ Centre, QChar(), tr("station centre line") });
    // Session 432: the signals, as the SIP draws them.
    items.append({ MainSignal, QChar(), tr("signal: Red, Yellow, Green (a lit lamp: its KAVACH aspect)") });
    items.append({ ShuntSignal, QChar(), tr("shunt signal") });
    items.append({ CallingOn, QChar(), tr("calling-on") });
    items.append({ Note, QChar(), m_knots.size() > 2 ? tr("To scale by absolute location, the yard wider than the block sections (see the km scale). Drag a tag along its line to move it")
                                         : tr("To scale by absolute location. Drag a tag along its line to move it") });

    const QString head = tr("LEGEND");
    QFont bold = small;
    bold.setBold(true);
    const int headW = QFontMetrics(bold).horizontalAdvance(head) + 12;
    const int x0 = 8 + headW, right = w - 8;
    double x = x0;
    int row = 0;
    if (p) {
        p->setFont(bold);
        p->setPen(palette().color(QPalette::Text));
        p->drawText(QRectF(8, top, headW, rowH), Qt::AlignLeft | Qt::AlignVCenter, head);
        p->setFont(small);
    }
    for (const Item &it : items) {
        const int sw = it.kind == IdBox ? fm.horizontalAdvance(QStringLiteral("R-981")) + 8 : it.kind == Note ? 0
                     : it.kind == MainSignal ? 36 : it.kind == ShuntSignal || it.kind == CallingOn ? 26 : 22;
        const int iw = sw + (sw ? 6 : 0) + fm.horizontalAdvance(it.label) + 18;
        if (x > x0 && x + iw > right) { ++row; x = x0; }
        if (p) {
            const double cy = top + row * rowH + rowH / 2.0;
            const QPointF c(x + sw / 2.0, cy);
            switch (it.kind) {
            case Letter: case Same: TinLayout::drawTag(*p, c, it.letter, TinLayout::Box, UiColor::frame()); break;
            case Ahead: TinLayout::drawTag(*p, c, it.letter, TinLayout::TipRight, UiColor::frame()); break;
            case Flagged: TinLayout::drawTag(*p, c, it.letter, TinLayout::Box, UiColor::warning(), 2); break;
            case Band: p->fillRect(QRectF(x, cy - 3.5, sw, 7), TinLayout::tinColor(1)); break;
            case NonKavach:
                p->setPen(QPen(UiColor::frame(), 1));
                p->setBrush(palette().base());
                p->drawRect(QRectF(x, cy - 3.5, sw, 7));
                break;
            case Centre:
                p->setPen(QPen(UiColor::ok(), 1.5));
                p->drawLine(QPointF(c.x(), cy - lh / 2.0), QPointF(c.x(), cy + lh / 2.0));
                break;
            case IdBox: TinLayout::drawIdBox(*p, QPointF(c.x(), cy + lh / 2.0), QStringLiteral("R-981")); break;
            case MainSignal: case ShuntSignal: case CallingOn: {
                TinLayout::SignalHead h;
                h.aspects = it.kind == MainSignal ? 3 : it.kind == ShuntSignal ? 0 : 2;
                h.callingOn = it.kind == CallingOn;
                const QColor ink = palette().color(QPalette::Text);
                TinLayout::drawSignal(*p, QPointF(x + (it.kind == CallingOn ? 10 : 3), cy - 13), h, 1, 0, ink);
                break;
            }
            case Note: break;
            }
            p->setPen(UiColor::muted());
            p->drawText(QRectF(x + sw + (sw ? 6 : 0), cy - lh / 2.0, iw, lh), Qt::AlignLeft | Qt::AlignVCenter, it.label);
        }
        x += iw;
    }
    return (row + 1) * rowH;
}

QScrollArea *StationLayoutCanvas::scrollArea() const
{
    return parentWidget() ? qobject_cast<QScrollArea *>(parentWidget()->parentWidget()) : nullptr;
}

void StationLayoutCanvas::setZoom(double z, double anchorX)
{
    QScrollArea *sa = scrollArea();
    const int view = sa ? sa->viewport()->width() : width();
    QScrollBar *h = sa ? sa->horizontalScrollBar() : nullptr;
    // Where the anchor sits in the viewport, and which fraction of the sheet it is.
    const double inView = anchorX >= 0 && h ? anchorX - h->value() : view / 2.0;
    const double f = width() > 0 ? ((h ? h->value() : 0) + inView) / width() : 0.5;
    m_zoom = qBound(1.0, z, 40.0);
    // At 1 the scroll area's own width (the minimum only stops it getting unreadable).
    const int w = m_zoom > 1.0 ? int(view * m_zoom) : 400;
    setMinimumWidth(w);
    if (sa) resize(qMax(w, view), height());   // now, so the scroll bar can be placed below
    updateGeometry();
    if (h) h->setValue(int(f * width() - inView));
    update();
}

bool StationLayoutCanvas::eventFilter(QObject *o, QEvent *e)
{
    // The window resized: keep the same zoom against the new width.
    if (e->type() == QEvent::Resize && m_zoom > 1.0 && scrollArea() && o == scrollArea()->viewport())
        setZoom(m_zoom);
    return QWidget::eventFilter(o, e);
}

void StationLayoutCanvas::setRunStates(const QHash<QString, int> &states)
{
    m_runStates = states;
    update();
}

void StationLayoutCanvas::setSimLoco(const QString &line, double locM, int sign)
{
    m_simLine = line;
    m_simLoc = locM;
    m_simSign = sign < 0 ? -1 : 1;
    if (line.isEmpty()) m_simTags.clear();
    update();
}

void StationLayoutCanvas::setSimTags(const QHash<QString, int> &states)
{
    m_simTags = states;
    update();
}

QRectF StationLayoutCanvas::simLocoRect() const
{
    if (m_simLine.isEmpty() || !m_laneOfLine.contains(m_simLine) || !std::isfinite(m_simLoc)) return QRectF();
    const double x = xOf(qint64(std::llround(m_simLoc)));
    const double y = laneY(m_laneOfLine.value(m_simLine));
    return QRectF(x - 15, y - 7, 30, 14);
}

void StationLayoutCanvas::setDiffTags(const QHash<QString, int> &kinds)
{
    setDiffTags(kinds, false);
}

void StationLayoutCanvas::setDiffTags(const QHash<QString, int> &kinds, bool byKey)
{
    m_diffKinds = kinds;
    m_diffByKey = byKey;
    update();
}

void StationLayoutCanvas::setKavachView(const KavachView &v)
{
    m_kavachView = v;
    if (v.sections.isEmpty()) m_kavachMaEnd = QRectF();
    update();
}

void StationLayoutCanvas::setLiveView(const LiveView &v)
{
    m_liveView = v;
    update();
}

QRectF StationLayoutCanvas::liveLocoRect(int i) const
{
    if (i < 0 || i >= m_liveView.locos.size()) return QRectF();
    const LiveLoco &k = m_liveView.locos.at(i);
    if (!m_loc.contains(k.laneTag)) return QRectF();
    const double y = tagPos(k.laneTag, xOf(m_loc.value(k.laneTag))).y();
    return QRectF(xOf(k.absLoc) - 15, y - 7, 30, 14);
}

QPolygonF StationLayoutCanvas::liveMaPath(int i) const
{
    QPolygonF out;
    const QRectF r = liveLocoRect(i);
    if (r.isNull() || m_liveView.locos.at(i).maEnd < 0) return out;
    const LiveLoco &k = m_liveView.locos.at(i);
    out << r.center();
    for (const QString &n : k.maTags)
        if (m_loc.contains(n)) out << tagPos(n, xOf(m_loc.value(n)));
    out << QPointF(xOf(k.maEnd), out.last().y());
    return out;
}

void StationLayoutCanvas::setPicking(bool on)
{
    m_picking = on;
    if (on) {
        setCursor(Qt::CrossCursor);
        setFocus();         // so Esc reaches it
    } else {
        unsetCursor();
    }
}

QPolygonF StationLayoutCanvas::kavachPath(int i) const
{
    QPolygonF out;
    if (i < 0 || i >= m_kavachView.sections.size()) return out;
    for (const QString &n : m_kavachView.sections.at(i).tags)
        if (m_loc.contains(n)) out << tagPos(n, xOf(m_loc.value(n)));
    return out;
}

void StationLayoutCanvas::setMarkedTags(const QSet<QString> &names)
{
    m_marked = names;
    update();
}

QStringList StationLayoutCanvas::tagsIn(const QRectF &r) const
{
    QStringList out;
    for (const Hit &h : m_hits)
        if (!h.tag.isEmpty() && r.contains(h.rect.center()) && !out.contains(h.tag)) out << h.tag;
    return out;
}

void StationLayoutCanvas::setSelectedTag(const QString &name)
{
    m_selected = name;
    update();
}

double StationLayoutCanvas::xOf(qint64 locM) const
{
    const double w = qMax(1, width() - kLeft - kRight);
    if (m_knots.size() < 2) return kLeft;
    int i = 0;
    while (i + 2 < m_knots.size() && locM > m_knots.at(i + 1).first) ++i;
    const auto &a = m_knots.at(i), &b = m_knots.at(i + 1);
    const double f = a.second + double(locM - a.first) / double(qMax<qint64>(1, b.first - a.first)) * (b.second - a.second);
    return kLeft + f * w;
}

qint64 StationLayoutCanvas::locAt(double x) const
{
    const double w = qMax(1, width() - kLeft - kRight);
    if (m_knots.size() < 2) return m_min;
    const double f = (x - kLeft) / w;
    int i = 0;
    while (i + 2 < m_knots.size() && f > m_knots.at(i + 1).second) ++i;
    const auto &a = m_knots.at(i), &b = m_knots.at(i + 1);
    const double span = qMax(1e-9, b.second - a.second);
    return a.first + qint64(std::llround((f - a.second) / span * double(b.first - a.first)));
}

QRectF StationLayoutCanvas::tagRect(const QString &name) const
{
    if (!m_loc.contains(name)) return QRectF();
    const QPointF c = tagPos(name, xOf(m_loc.value(name)));
    return QRectF(c.x() - 8, c.y() - 7, 16, 14);
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
    if (m_measuring) {
        measureAt(e->pos());
        return;
    }
    // Session 425: picking a KAVACH route's tags: a tag is picked, not selected or dragged.
    if (m_picking)
        for (const Hit &h : m_hits)
            if (!h.tag.isEmpty() && h.rect.contains(e->pos())) { emit tagPicked(h.tag); return; }
    // Session 223: Shift + drag boxes tags; Ctrl + click adds or removes one.
    if (e->modifiers() & Qt::ShiftModifier) {
        m_bandFrom = e->pos();
        m_band = QRect(m_bandFrom, QSize(1, 1));
        return;
    }
    for (const Hit &h : m_hits) {
        if (h.tag.isEmpty() || !h.rect.contains(e->pos())) continue;
        if (e->modifiers() & Qt::ControlModifier) { emit tagToggled(h.tag); return; }
        m_dragTag = h.tag;
        m_dragX = -1;
        emit tagClicked(h.tag);
        return;
    }
    // Session 233: a signal or a point selects its row, as a tag does.
    for (const Hit &h : m_hits) {
        if (!h.rect.contains(e->pos())) continue;
        if (h.signal >= 0) { setSelectedSignal(h.signal); emit signalClicked(h.signal); return; }
        if (h.point >= 0) { setSelectedPoint(h.point); emit pointClicked(h.point); return; }
    }
    // Session 220: anywhere else, grab the sheet and move it.
    if (QScrollArea *sa = scrollArea()) {
        m_panning = true;
        m_panFrom = mapToGlobal(e->pos());
        m_panH = sa->horizontalScrollBar()->value();
        m_panV = sa->verticalScrollBar()->value();
        setCursor(Qt::ClosedHandCursor);
    }
}

void StationLayoutCanvas::mouseMoveEvent(QMouseEvent *e)
{
    if (m_measuring) {
        if (m_measureA.set && !m_measureB.set) {
            m_hover = e->pos();
            update();
        }
        return;
    }
    if (!m_band.isNull() && (e->buttons() & Qt::LeftButton)) {
        m_band = QRect(m_bandFrom, e->pos()).normalized();
        update();
        return;
    }
    if (m_panning && (e->buttons() & Qt::LeftButton)) {
        const QPoint d = mapToGlobal(e->pos()) - m_panFrom;
        scrollArea()->horizontalScrollBar()->setValue(m_panH - d.x());
        scrollArea()->verticalScrollBar()->setValue(m_panV - d.y());
        return;
    }
    if (m_dragTag.isEmpty() || !(e->buttons() & Qt::LeftButton)) return;
    m_dragX = qBound(double(kLeft), double(e->pos().x()), double(width() - kRight));
    setCursor(Qt::SizeHorCursor);
    update();
}

void StationLayoutCanvas::mouseReleaseEvent(QMouseEvent *)
{
    if (!m_band.isNull()) {
        const QRect band = m_band;
        m_band = QRect();
        update();
        emit tagsBoxed(tagsIn(band));
        return;
    }
    const QString tag = m_dragTag;
    const double x = m_dragX;
    m_dragTag.clear();
    m_dragX = -1;
    m_panning = false;
    if (m_picking) setCursor(Qt::CrossCursor); else unsetCursor();
    if (!tag.isEmpty() && x >= 0 && std::fabs(x - xOf(m_loc.value(tag))) >= 2.0) emit tagDragged(tag, locAt(x));
    update();
}

void StationLayoutCanvas::mouseDoubleClickEvent(QMouseEvent *e)
{
    for (const Hit &h : m_hits)
        if (!h.tag.isEmpty() && h.rect.contains(e->pos())) { emit tagDoubleClicked(h.tag); return; }
    for (const Hit &h : m_hits)   // session 435
        if (h.point >= 0 && h.rect.contains(e->pos())) { emit pointThrowRequested(h.point); return; }
    for (const Hit &h : m_hits)   // session 436
        if (!h.circuit.isEmpty() && h.rect.contains(e->pos())) { emit circuitToggleRequested(h.circuit); return; }
}

QString StationLayoutCanvas::lineAt(double y) const
{
    for (auto it = m_laneOfLine.cbegin(); it != m_laneOfLine.cend(); ++it)
        if (std::fabs(y - laneY(it.value())) <= kLaneGap / 2.0) return it.key();
    return QString();
}

void StationLayoutCanvas::contextMenuEvent(QContextMenuEvent *e)
{
    QString tag;
    for (const Hit &h : m_hits)
        if (!h.tag.isEmpty() && h.rect.contains(e->pos())) { tag = h.tag; break; }
    // Session 435: a point: its own menu.
    if (tag.isEmpty())
        for (const Hit &h : m_hits)
            if (h.point >= 0 && h.rect.contains(e->pos())) {
                setSelectedPoint(h.point);
                emit pointClicked(h.point);
                emit pointContextRequested(e->globalPos(), h.point);
                return;
            }
    // Session 433: a signal of the sheet: its own menu.
    if (tag.isEmpty())
        for (const Hit &h : m_hits)
            if (h.signal >= 0 && h.rect.contains(e->pos())) {
                setSelectedSignal(h.signal);
                emit signalClicked(h.signal);
                emit signalContextRequested(e->globalPos(), h.signal);
                return;
            }
    const qint64 loc = locAt(qBound(double(kLeft), double(e->pos().x()), double(width() - kRight)));
    emit contextRequested(e->globalPos(), tag, lineAt(e->pos().y()), loc, e->pos().y() < laneY(0) - kLaneGap / 2.0);
}

void StationLayoutCanvas::wheelEvent(QWheelEvent *e)
{
    // Session 220: the wheel zooms about the pointer; Shift + wheel scrolls.
    const int dy = e->angleDelta().y();
    if ((e->modifiers() & Qt::ShiftModifier) || dy == 0) { QWidget::wheelEvent(e); return; }
    setZoom(m_zoom * (dy > 0 ? 1.25 : 0.8), e->position().x());
    e->accept();
}

void StationLayoutCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), palette().base());
    const QColor text = palette().color(QPalette::Text), ink = UiColor::muted();
    const QFont small = smallFont();
    p.setFont(small);
    const QFontMetrics fm(small);
    const int lh = fm.height();
    const int labelRow = 2 * lh + 4;           // a tag's km over its "R-…" box
    m_hits.clear();

    if (m_l.tags.isEmpty() && m_l.points.isEmpty()) {
        p.setPen(ink);
        p.drawText(rect(), Qt::AlignCenter, tr("No station layout: Open a .json or Import the Python tool's .xlsx"));
        return;
    }

    // ---- title (H2.2) and the directions ------------------------------------------------------
    {
        QStringList ids;
        for (const StationLayout::Station &s : m_l.stations)
            ids << tr("Station ID: %1 (km : %2)").arg(s.id).arg(QString::number(s.location / 1000.0, 'f', 3));
        QFont big = font();
        big.setBold(true);
        big.setPointSizeF(font().pointSizeF() + 3);
        const int bh = QFontMetrics(big).height();
        p.setFont(big);
        p.setPen(text);
        p.drawText(QRect(0, 6, width(), bh), Qt::AlignHCenter | Qt::AlignVCenter,
                   ids.isEmpty() ? tr("Station layout") : ids.join(QStringLiteral("    ")));
        p.setFont(small);
        // Nominal is the direction of increasing absolute location.
        const double ay = 6 + bh / 2.0;
        const QString rev = tr("Reverse"), nom = tr("Nominal");
        double x = 8;
        p.drawText(QRectF(x, ay - lh / 2.0, fm.horizontalAdvance(rev) + 2, lh), Qt::AlignVCenter, rev);
        x += fm.horizontalAdvance(rev) + 8;
        p.setPen(QPen(text, 1.2));
        p.drawLine(QPointF(x, ay), QPointF(x + 60, ay));
        for (const double s : { 1.0, -1.0 }) {
            const double tip = s > 0 ? x : x + 60;
            p.drawLine(QPointF(tip, ay), QPointF(tip + 6 * s, ay - 4));
            p.drawLine(QPointF(tip, ay), QPointF(tip + 6 * s, ay + 4));
        }
        p.setPen(text);
        p.drawText(QRectF(x + 68, ay - lh / 2.0, fm.horizontalAdvance(nom) + 2, lh), Qt::AlignVCenter, nom);
    }

    // ---- texts: above the lanes when posy < 0, else below ----------------------------------------
    const int lanesBottom = laneY(m_laneNames.size() - 1) + kBelow;
    for (int i = 0; i < m_l.texts.size(); ++i) {
        const StationLayout::Text &t = m_l.texts.at(i);
        const double x = xOf(t.location);
        const int w = fm.horizontalAdvance(t.text) + 4;
        const int row = m_textRow.value(i);
        const QRectF r(x - w / 2.0, t.posY < 0 ? kTop - 2 * lh - 6 + row * lh : lanesBottom + 4 + row * lh, w, lh);
        p.setPen(ink);
        p.drawText(r, Qt::AlignCenter, t.text);
        m_hits << Hit{ r, QString(), tr("Text at %1: %2").arg(km(t.location), t.text) };
    }

    // ---- the station centre line and its km (H2.4, H2.28) ------------------------------------------
    for (const StationLayout::Station &s : m_l.stations) {
        const double x = xOf(s.location);
        p.setPen(QPen(UiColor::ok(), 1.5));
        p.drawLine(QPointF(x, topMargin()), QPointF(x, lanesBottom));
        const QString k = QString::number(s.location / 1000.0, 'f', 3);
        const int w = fm.horizontalAdvance(k) + 4;
        p.setPen(UiColor::ok());
        p.drawText(QRectF(x - w / 2.0, topMargin() - lh - 2, w, lh), Qt::AlignCenter, k);
        m_hits << Hit{ QRectF(x - 3, topMargin() - lh, 6, lanesBottom - topMargin() + lh), QString(),
                       tr("Station %1 centre line at %2").arg(s.id).arg(km(s.location)) };
    }

    // ---- points: a link between two lanes -------------------------------------------------
    QHash<int, QVector<double>> ptEnds;
    for (int pi = 0; pi < m_l.points.size(); ++pi) {
        const StationLayout::Point &pt = m_l.points.at(pi);
        if (!m_laneOfLine.contains(pt.line1) || !m_laneOfLine.contains(pt.line2)) continue;
        const QPointF a(xOf(pt.loc1), laneY(m_laneOfLine.value(pt.line1)));
        QPointF b(xOf(pt.loc2), laneY(m_laneOfLine.value(pt.line2)));
        // Session 233: a point on one line (a crossover's end: P21 DL2 to DL2)
        // is a short stroke across its lane, so it can be seen and clicked.
        const bool oneLine = pt.line1 == pt.line2 && std::fabs(b.x() - a.x()) < 4;
        // Session 218: in the TIN of the point's tag, as a band like the lines.
        QColor c;
        for (auto it = m_pointOfTag.constBegin(); it != m_pointOfTag.constEnd() && !c.isValid(); ++it)
            if (m_l.points.at(it.value()).name == pt.name) c = TinLayout::tinColor(m_info.value(it.key()).tinNom);
        if (pi == m_selPoint) {
            p.setPen(QPen(UiColor::accent(), 11, Qt::SolidLine, Qt::RoundCap));
            if (oneLine) p.drawLine(QPointF(a.x(), a.y() - 9), QPointF(a.x(), a.y() + 9));
            else p.drawLine(a, b);
        }
        p.setPen(c.isValid() ? QPen(c, 7, Qt::SolidLine, Qt::FlatCap) : QPen(UiColor::frame(), oneLine ? 3 : 2));
        // Session 435: with the station's state, a Normal point's turnout drawn
        // faint and dashed, a Reverse one bold: the way a train would go.
        const bool reverse = m_liveView.reversePoints.contains(pt.name);
        if (m_liveView.pointsShown && !reverse) {
            QColor faint = c.isValid() ? c : UiColor::frame();
            faint.setAlpha(110);
            p.setPen(QPen(faint, 3, Qt::DashLine, Qt::FlatCap));
        } else if (m_liveView.pointsShown) {
            p.setPen(QPen(c.isValid() ? c : UiColor::accent(), 8, Qt::SolidLine, Qt::FlatCap));
        }
        if (oneLine) {
            p.drawLine(QPointF(a.x(), a.y() - 8), QPointF(a.x(), a.y() + 8));
            b = QPointF(a.x(), a.y() + 8);
        } else {
            p.drawLine(a, b);
        }
        const QPointF mid = oneLine ? QPointF(a.x(), a.y() - 8) : (a + b) / 2.0;
        const QString ptLabel = m_liveView.pointsShown ? pt.name + (reverse ? QStringLiteral(" R") : QStringLiteral(" N")) : pt.name;
        const int w = fm.horizontalAdvance(ptLabel) + 4;
        if (placeLabel(ptEnds, 0, mid.x() + w / 2.0 + 3, w, 1) >= 0) {
            p.setPen(pi == m_selPoint ? text : ink);
            p.drawText(QRectF(mid.x() + 3, mid.y() - lh / 2.0, w, lh), Qt::AlignLeft | Qt::AlignVCenter, ptLabel);
        }
        QRectF r(qMin(a.x(), b.x()) - 3, qMin(a.y(), b.y()) - 3, std::fabs(b.x() - a.x()) + 6, std::fabs(b.y() - a.y()) + 6);
        if (oneLine) r = QRectF(a.x() - 6, a.y() - 10, 12, 20);
        QString ptTip = tr("Point %1: %2 on %3 to %4 on %5").arg(pt.name, km(pt.loc1), pt.line1, km(pt.loc2), pt.line2);
        if (m_liveView.pointsShown)
            ptTip += QLatin1Char('\n') + (reverse ? tr("Reverse") : tr("Normal")) + tr(": double-click to throw it, right-click for its menu");
        Hit h{ r, QString(), ptTip };
        h.point = pi;
        m_hits << h;
    }

    // Main and duplicate make one set (H2.15): the duplicate is the main's
    // name with "D" after it, on the same line.
    // Session 429: by key; the second "12D" ("12D#2") pairs with the second "12" ("12#2").
    auto mainKeyOf = [](const QString &key) {
        const QString name = StationLayout::nameOfKey(key);
        return name.chopped(1) + key.mid(name.size());
    };
    QHash<QString, QString> dupOf;
    for (const QString &key : m_keys)
        if (isDuplicateName(StationLayout::nameOfKey(key)) && m_loc.contains(key) && m_loc.contains(mainKeyOf(key))
            && m_lane.value(key) == m_lane.value(mainKeyOf(key)))
            dupOf.insert(mainKeyOf(key), key);
    auto inSet = [&](const QString &k) { return isDuplicateName(StationLayout::nameOfKey(k)) && dupOf.value(mainKeyOf(k)) == k; };
    auto xAt = [&](const QString &n) { return n == m_dragTag && m_dragX >= 0 ? m_dragX : xOf(m_loc.value(n)); };

    // ---- session 435: the routes set, under the lanes --------------------------------------------
    for (const KavachSection &k : m_liveView.routesSet) {
        QPolygonF path;
        for (const QString &n : k.tags)
            if (m_loc.contains(n)) path << tagPos(n, xOf(m_loc.value(n)));
        if (path.isEmpty()) continue;
        QColor c = UiColor::ok();
        c.setAlpha(palette().color(QPalette::Base).lightness() < 128 ? 150 : 90);
        p.setPen(QPen(c, 13, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        if (path.size() == 1) p.drawPoint(path.first());
        else p.drawPolyline(path);
        m_hits << Hit{ path.boundingRect().adjusted(-4, -7, 4, 7), QString(), tr("Route %1 set").arg(k.route) };
    }

    // ---- session 424: the KAVACH route and the MA's sections on, under the lanes ---------------
    for (int i = 0; i < m_kavachView.sections.size(); ++i) {
        const QPolygonF path = kavachPath(i);
        const KavachSection &k = m_kavachView.sections.at(i);
        QColor c = UiColor::accent();
        const bool dark = palette().color(QPalette::Base).lightness() < 128;   // a faint halo vanishes on a dark sheet
        c.setAlpha(i == 0 ? (dark ? 200 : 150) : (dark ? 130 : 70));
        p.setPen(QPen(c, 17, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        if (path.size() == 1) p.drawPoint(path.first());
        else p.drawPolyline(path);
        QString tip = i == 0 ? tr("KAVACH route %1 (selected): %2 to %3 m").arg(k.route).arg(k.from).arg(k.to)
                             : tr("KAVACH route %1: MA section %2 of %3, %4 to %5 m")
                                   .arg(k.route).arg(i + 1).arg(m_kavachView.sections.size()).arg(k.from).arg(k.to);
        if (!k.repeated.isEmpty())
            tip += QLatin1Char('\n') + tr("tag(s) %1 listed more than once in the layout: the ones its linking distances lead to")
                                           .arg(k.repeated.join(QStringLiteral(", ")));
        for (int j = 0; j + 1 < path.size(); ++j)
            m_hits << Hit{ QRectF(path.at(j), path.at(j + 1)).normalized().adjusted(-2, -9, 2, 9), QString(), tip };
    }

    // ---- session 428: each live loco's MA, from it to where the MA ends, under the lanes ------
    for (int i = 0; i < m_liveView.locos.size(); ++i) {
        const QPolygonF path = liveMaPath(i);
        if (path.size() < 2) continue;
        QColor c = UiColor::ok();
        c.setAlpha(palette().color(QPalette::Base).lightness() < 128 ? 190 : 120);
        p.setPen(QPen(c, 11, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(path);
        const LiveLoco &k = m_liveView.locos.at(i);
        const QString tip = tr("Loco %1: %2, to %3").arg(k.id).arg(k.maText, km(k.maEnd));
        for (int j = 0; j + 1 < path.size(); ++j)
            m_hits << Hit{ QRectF(path.at(j), path.at(j + 1)).normalized().adjusted(-2, -6, 2, 6), QString(), tip };
    }

    // ---- lanes: one band colour per TIN section (H2.17, H2.18) --------------------------------
    struct Run { int lane, tin; double from, to; };
    QVector<Run> runs;
    QHash<int, QVector<QPair<qint64, QString>>> setsOfLane;   // set locations, for the gap dimensions
    for (int lane = 0; lane < m_laneNames.size(); ++lane) {
        const int y = laneY(lane);
        p.setPen(text);
        p.drawText(QRect(4, y - lh, kLeft - 12, 2 * lh), Qt::AlignRight | Qt::AlignVCenter,
                   fm.elidedText(m_laneNames.at(lane), Qt::ElideRight, kLeft - 12));
        QVector<QPair<qint64, QString>> along;
        for (auto it = m_lane.constBegin(); it != m_lane.constEnd(); ++it)
            if (it.value() == lane && m_loc.contains(it.key()) && !m_pointOfTag.contains(it.key()))
                along.append({ m_loc.value(it.key()), it.key() });
        std::sort(along.begin(), along.end());
        // Session 218: a loop runs on to the points at its ends, in the TIN
        // a loco leaving its end tag goes into.
        if (!along.isEmpty()) {
            qint64 lo = along.first().first, hi = along.last().first;
            for (const StationLayout::Point &pt : m_l.points) {
                if (m_laneOfLine.value(pt.line1, -1) == lane && pt.loc1 > 0) { lo = qMin(lo, pt.loc1); hi = qMax(hi, pt.loc1); }
                if (m_laneOfLine.value(pt.line2, -1) == lane && pt.loc2 > 0) { lo = qMin(lo, pt.loc2); hi = qMax(hi, pt.loc2); }
            }
            const QColor l = TinLayout::tinColor(m_info.value(along.first().second).tinRev);
            const QColor r = TinLayout::tinColor(m_info.value(along.last().second).tinNom);
            const QPen thin(UiColor::frame(), 1);
            if (lo < along.first().first) {
                const QRectF band(xOf(lo), y - 3.5, xOf(along.first().first) - xOf(lo), 7);
                if (l.isValid()) p.fillRect(band, l); else { p.setPen(thin); p.setBrush(palette().base()); p.drawRect(band); }
            }
            if (hi > along.last().first) {
                const QRectF band(xOf(along.last().first), y - 3.5, xOf(hi) - xOf(along.last().first), 7);
                if (r.isValid()) p.fillRect(band, r); else { p.setPen(thin); p.setBrush(palette().base()); p.drawRect(band); }
            }
        }
        for (const auto &a : along) if (!inSet(a.second)) setsOfLane[lane].append(a);
        // Between two tags: the TIN a loco reading the left one goes into
        // travelling Nominal.
        for (int i = 0; i + 1 < along.size(); ++i) {
            const StationLayout::TagInfo a = m_info.value(along.at(i).second), b = m_info.value(along.at(i + 1).second);
            const double x1 = xOf(along.at(i).first), x2 = xOf(along.at(i + 1).first);
            const QRectF band(x1, y - 3.5, x2 - x1, 7);
            const QColor c = TinLayout::tinColor(a.tinNom);
            if (c.isValid()) p.fillRect(band, c);
            else {
                p.setPen(QPen(UiColor::frame(), 1));
                p.setBrush(palette().base());
                p.drawRect(band);
            }
            QString tin = a.tinNom == b.tinRev ? QString::number(a.tinNom)
                                                : tr("%1 travelling Nominal, %2 Reverse").arg(a.tinNom).arg(b.tinRev);
            if (a.tinNom == 0) tin += tr(" (non-Kavach)");
            m_hits << Hit{ band.adjusted(0, -2, 0, 2), QString(),
                           tr("TIN %1 between tags %2 and %3").arg(tin, along.at(i).second, along.at(i + 1).second) };
            if (runs.isEmpty() || runs.last().lane != lane || runs.last().tin != a.tinNom) runs.append({ lane, a.tinNom, x1, x2 });
            else runs.last().to = x2;
        }
        if (along.size() == 1) {
            p.setPen(QPen(UiColor::frame(), 3));
            p.drawLine(QPointF(xOf(along.first().first) - 10, y), QPointF(xOf(along.first().first) + 10, y));
        }
    }

    // ---- session 436: the track circuits: their joints across the line, red when occupied ---------
    for (const StationLayout::Circuit &tc : m_circuits) {
        if (!m_laneOfLine.contains(tc.line)) continue;
        const double y = laneY(m_laneOfLine.value(tc.line));
        // A circuit at a location only: a short mark there.
        const double x1 = xOf(tc.from) - (tc.from == tc.to ? 4 : 0), x2 = xOf(tc.to) + (tc.from == tc.to ? 4 : 0);
        const bool occ = m_liveView.occupied.contains(tc.name) || m_liveView.occupiedBy.contains(tc.name);
        if (occ) {
            QColor c = UiColor::error();
            c.setAlpha(200);
            p.setPen(QPen(c, 6, Qt::SolidLine, Qt::FlatCap));
            p.drawLine(QPointF(x1, y), QPointF(x2, y));
        }
        p.setPen(QPen(text, 1.2));
        for (const double x : { x1, x2 }) p.drawLine(QPointF(x, y - 6), QPointF(x, y + 6));
        QString tip = tc.from == tc.to ? tr("Track circuit %1 (cid %2) on %3 at %4 (no ends in the file)").arg(tc.name).arg(tc.cid).arg(tc.line, km(tc.from))
                                       : tr("Track circuit %1 (cid %2) on %3: %4 to %5").arg(tc.name).arg(tc.cid).arg(tc.line, km(tc.from), km(tc.to));
        if (m_liveView.occupiedBy.contains(tc.name))
            tip += QLatin1Char('\n') + tr("Occupied by loco %1 (where its last ARP / LSRP puts its train)").arg(m_liveView.occupiedBy.value(tc.name));
        if (m_liveView.pointsShown) tip += QLatin1Char('\n') + (m_liveView.occupied.contains(tc.name) ? tr("Occupied") : tr("Clear"))
                                           + tr(" by hand: double-click to change");
        Hit h{ QRectF(qMin(x1, x2), y - 4, std::fabs(x2 - x1), 8), QString(), tip };
        h.circuit = tc.name;
        m_hits << h;
    }

    // ---- signals at their foot tags ---------------------------------------------------------------
    // Session 424: with the KAVACH tab's view, a KAVACH signal is lit by its
    // aspect, and one the signals sheet lacks is drawn at its foot tag too.
    QHash<QString, int> kAspect;
    for (const KavachSignal &k : m_kavachView.signalList) kAspect.insert(k.name, k.aspect);
    QVector<KavachSignal> kSignals = m_kavachView.signalList;
    for (const KavachSignal &k : m_liveView.signalList) {   // session 428: the live aspects over the file's
        if (!kAspect.contains(k.name)) kSignals << k;
        kAspect.insert(k.name, k.aspect);
    }
    // Session 432: drawn as the SIP and the tag-TIN layouts draw them (TinLayout::drawSignal).
    struct Sig { QString name, foot, tip; int index; StationLayout::SignalLook look; QString heads; };   // heads: said last in the tooltip
    QVector<Sig> sigs;
    QSet<QString> sheetNames;
    auto lookText = [](const StationLayout::SignalLook &k) {
        QString t = k.aspects > 0 ? tr("%1-aspect main signal").arg(k.aspects) : tr("shunt signal");
        if (k.aspects > 0 && k.shunt) t += tr(", a shunt signal below it");
        if (k.callingOn) t += tr(", calling-on");
        if (k.indicator > 0) t += tr(", route indicator at position %1").arg(k.indicator);
        return t;
    };
    // A route's signal at a foot tag a station signal stands at (the station XML
    // has both) is that signal: not drawn again, named in its tooltip.
    QHash<QString, QStringList> routeNames;
    for (const StationLayout::Signal &s : m_l.signalList)
        if (s.route.isEmpty()) routeNames[s.footTag];
    for (const StationLayout::Signal &s : m_l.signalList)
        if (!s.route.isEmpty() && routeNames.contains(s.footTag)) routeNames[s.footTag] << tr("%1 on route %2").arg(s.name, s.route);
    for (int si = 0; si < m_l.signalList.size(); ++si) {
        const StationLayout::Signal &s = m_l.signalList.at(si);
        sheetNames.insert(s.name);
        if (!s.route.isEmpty() && routeNames.contains(s.footTag)) continue;
        const StationLayout::SignalLook k = StationLayout::lookOf(s);
        const QString tip = s.route.isEmpty() ? tr("Signal %1 (id %2, station %3) at foot tag %4").arg(s.name).arg(s.sigId).arg(s.stationId).arg(s.footTag)
                                              : tr("Signal %1 (id %2) of route %3 at foot tag %4").arg(s.name).arg(s.sigId).arg(s.route, s.footTag);
        QString heads = lookText(k) + (s.aspects < 0 ? tr(" (from its name; right-click to set it)") : QString());
        if (s.route.isEmpty() && !routeNames.value(s.footTag).isEmpty())
            heads += QLatin1Char('\n') + tr("Also: %1").arg(routeNames.value(s.footTag).join(QStringLiteral(", ")));
        sigs << Sig{ s.name, s.footTag, tip, si, k, heads };
    }
    for (const KavachSignal &k : kSignals)
        if (!sheetNames.contains(k.name)) {
            StationLayout::Signal s;
            s.name = k.name;
            sigs << Sig{ k.name, k.footTag, tr("Signal %1 (KAVACH data; not in the signals sheet) at foot tag %2").arg(k.name, k.footTag),
                         -1, StationLayout::lookOf(s), QString() };
        }
    // The lamps an aspect lights (TinLayout::Lamp flags).
    auto lampsFor = [](int a) -> int {
        if (a == 12 || a == 13) return TinLayout::LampYellow | TinLayout::LampYellow2 | TinLayout::LampIndicator;
        if (a == 10) return TinLayout::LampYellow | TinLayout::LampYellow2;
        if (a >= 3 && a <= 8) return TinLayout::LampYellow | TinLayout::LampIndicator;
        if (a == 2 || a >= 32) return TinLayout::LampYellow;
        if (a == 11 || a == 14) return TinLayout::LampGreen;
        if (a == 15) return TinLayout::LampRed | TinLayout::LampCallingOn;
        return TinLayout::LampRed;
    };
    // The way a signal faces: its foot tag's placement (1 nominal, 2 reverse,
    // 6 both), else the way its tag set's triangle points (H2.15), else nominal.
    auto facing = [this](const QString &key) {
        const StationLayout::TagInfo in = m_info.value(key);
        if (in.ok && in.placement == 2) return -1;
        if (in.ok && (in.placement == 1 || in.placement == 6)) return 1;
        const QStringList dups = m_keysOf.value(StationLayout::nameOfKey(key) + QLatin1Char('D'));
        if (!dups.isEmpty() && m_loc.contains(dups.first()) && m_loc.contains(key) && m_loc.value(dups.first()) != m_loc.value(key))
            return m_loc.value(dups.first()) > m_loc.value(key) ? 1 : -1;
        return 1;
    };
    QHash<int, QVector<double>> sigEnds;
    // Session 429: a foot tag the layout lists more than once: the k-th sheet
    // row naming it at the k-th tag of that name when the rows and the tags
    // are as many (Section_30: one row per block); else at every one.
    QHash<QString, QVector<int>> rowsAt;
    for (const Sig &g : sigs) if (g.index >= 0) rowsAt[g.foot] << g.index;
    for (const Sig &g : sigs) for (const QString &foot : m_keysOf.value(g.foot)) {
        if (!m_loc.contains(foot)) continue;
        const QStringList &all = m_keysOf.value(g.foot);
        const QVector<int> &rows = rowsAt.value(g.foot);
        if (g.index >= 0 && all.size() > 1 && rows.size() == all.size() && all.indexOf(foot) != rows.indexOf(g.index)) continue;
        const int si = g.index;
        const double x = xOf(m_loc.value(foot));
        const double y = tagPos(foot, x).y();
        QString tip = g.tip;
        TinLayout::SignalHead head;
        head.aspects = g.look.aspects;
        head.callingOn = g.look.callingOn;
        head.shunt = g.look.shunt;
        head.indicator = g.look.indicator;
        int lit = 0;
        if (kAspect.contains(g.name)) {
            const int a = kAspect.value(g.name);
            lit = lampsFor(a);
            if (head.aspects > 0 && head.aspects < 4 && (lit & TinLayout::LampYellow2)) head.aspects = 4;   // Double Yellow needs the fourth lamp
            // Session 435: the aspect names the route indicator's position (sigAspect 3-8: 1-6; 12: 1; 13: 4).
            if (lit & TinLayout::LampIndicator) head.indicator = a >= 3 && a <= 8 ? a - 2 : a == 12 ? 1 : 4;
            tip += QLatin1Char('\n') + (m_liveView.signalList.isEmpty() ? tr("KAVACH: %1")
                                         : m_liveView.whatIf ? tr("KAVACH, what-if: %1") : tr("KAVACH, live: %1"))
                                            .arg(StationKavach::aspectName(a));
        }
        if (!g.heads.isEmpty()) tip += QLatin1Char('\n') + g.heads;
        const QRectF sym = TinLayout::drawSignal(p, QPointF(x, y + 8), head, facing(foot), lit, text);
        if (si >= 0 && si == m_selSignal) {
            p.setPen(QPen(UiColor::accent(), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(sym.adjusted(-3, -2, 3, 2), 3, 3);
        }
        p.setBrush(Qt::NoBrush);
        const int w = fm.horizontalAdvance(g.name) + 4;
        const double lx = sym.center().x();
        const int row = placeLabel(sigEnds, m_lane.value(foot, 0), lx, w, 2);
        if (row >= 0) {
            QFont f = p.font();
            if (si >= 0 && si == m_selSignal) { f.setBold(true); p.setFont(f); }
            p.setPen(text);
            p.drawText(QRectF(lx - w / 2.0 - 4, sym.bottom() + 1 + row * lh, w + 8, lh), Qt::AlignCenter, g.name);
            if (si >= 0 && si == m_selSignal) { f.setBold(false); p.setFont(f); }
        }
        // Centred on the mast (where the foot tag is), wide enough for the heads either side.
        const double half = qMax(x - sym.left(), sym.right() - x) + 2;
        Hit h{ QRectF(x - half, sym.top(), 2 * half, sym.height() + lh), QString(), tip };
        h.symbol = sym;
        h.signal = si;
        m_hits << h;
    }

    // ---- gap dimensions between tag sets (as on the RDSO layouts) ----------------------------------
    for (auto it = setsOfLane.constBegin(); it != setsOfLane.constEnd(); ++it) {
        const double dy = laneY(it.key()) + 28 + 2 * lh;
        for (int i = 0; i + 1 < it.value().size(); ++i) {
            const double x1 = xOf(it.value().at(i).first), x2 = xOf(it.value().at(i + 1).first);
            const QString d = QStringLiteral("%1m").arg(it.value().at(i + 1).first - it.value().at(i).first);
            const int w = fm.horizontalAdvance(d) + 4;
            if (x2 - x1 < w + 16) continue;
            p.setPen(QPen(UiColor::frame(), 1));
            p.drawLine(QPointF(x1 + 2, dy), QPointF(x2 - 2, dy));
            for (const double s : { 1.0, -1.0 }) {
                const double tip = s > 0 ? x1 + 2 : x2 - 2;
                p.drawLine(QPointF(tip, dy), QPointF(tip + 5 * s, dy - 3));
                p.drawLine(QPointF(tip, dy), QPointF(tip + 5 * s, dy + 3));
            }
            const QRectF r((x1 + x2 - w) / 2.0, dy - lh / 2.0, w, lh);
            p.fillRect(r, palette().base());
            p.setPen(ink);
            p.drawText(r, Qt::AlignCenter, d);
        }
    }

    QHash<int, QVector<double>> labelEnds;   // the label rows above each lane
    // ---- tags: one symbol per set (H2.15), its letter (H2.21), "R-…" and km above ------------------
    for (int ti = 0; ti < m_l.tags.size(); ++ti) {
        const QString &name = m_l.tags.at(ti).name, &key = m_keys.at(ti);   // session 429: drawn by key
        if (!m_loc.contains(key) || inSet(key)) continue;
        const QString dup = dupOf.value(key);
        const double x = xAt(key);
        const int lane = m_lane.value(key, 0);
        const double y = tagPos(key, x).y();
        const StationLayout::TagInfo in = m_info.value(key);
        const bool flagged = m_flagged.contains(name) || (!dup.isEmpty() && m_flagged.contains(StationLayout::nameOfKey(dup)));
        const TinLayout::Shape shape = TinLayout::shapeOf(m_loc.value(key), m_loc.value(dup), !dup.isEmpty());
        const QChar letter = TinLayout::notation(in.type, in.placement);
        TinLayout::drawTag(p, QPointF(x, y), letter, shape, flagged ? UiColor::warning() : text, flagged ? 2 : 1.2);
        // Session 227: the run: read (ok), read but different (warning), not read (error).
        if (m_runStates.contains(name)) {
            const int st = m_runStates.value(name);
            p.setPen(QPen(st == int(PlanRun::State::NotRead) ? UiColor::error()
                          : st == int(PlanRun::State::Different) ? UiColor::warning() : UiColor::ok(), 2));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QPointF(x, y), 20, 20);
        }
        // Session 411: the Loco Simulator's tags: sent (ok), a fault on it
        // (warning), skipped (muted, dashed); a duplicate's state on its set.
        if (const int st = m_simTags.value(name) | (dup.isEmpty() ? 0 : m_simTags.value(StationLayout::nameOfKey(dup)))) {
            const bool sent = st & (RouteStrip::SentOn1 | RouteStrip::SentOn2);
            const QColor c = st & RouteStrip::Faulted ? UiColor::warning() : sent ? UiColor::ok() : ink;
            p.setPen(QPen(c, 2, sent || (st & RouteStrip::Faulted) ? Qt::SolidLine : Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QPointF(x, y), 24, 24);
        }
        // Session 226: a tag that differs from the compared file (added: ok; moved / changed: warning).
        // Session 430: by key, only that tag of a repeated name.
        const QString diffOwn = m_diffByKey ? key : name;
        const QString diffDup = dup.isEmpty() ? QString() : m_diffByKey ? dup : StationLayout::nameOfKey(dup);
        if (m_diffKinds.contains(diffOwn) || (!diffDup.isEmpty() && m_diffKinds.contains(diffDup))) {
            const int k = m_diffKinds.value(diffOwn, m_diffKinds.value(diffDup));
            p.setPen(QPen(k == StationLayout::TagChange::Added ? UiColor::ok() : UiColor::warning(), 2, Qt::DotLine));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QPointF(x, y), 16, 16);
        }
        if ((!m_selected.isEmpty() && (key == m_selected || dup == m_selected)) || m_marked.contains(key)) {
            p.setPen(QPen(UiColor::selectedMark(), 2));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QPointF(x, y), 12, 12);
        }
        const QString id = TinLayout::idText(name), k = QString::number(m_loc.value(key) / 1000.0, 'f', 3);
        const int w = qMax(fm.horizontalAdvance(id) + 6, fm.horizontalAdvance(k) + 4);
        if (m_pointOfTag.contains(key)) {
            // On a diagonal: "R-…" and km beside the tag, as on the RDSO layouts.
            const double lx = x + 12 + w / 2.0;
            TinLayout::drawIdBox(p, QPointF(lx, y + lh / 2.0 + 2), id);
            p.setPen(key == m_selected ? text : ink);
            p.drawText(QRectF(lx - w / 2.0, y - lh * 1.5 - 4, w, lh), Qt::AlignCenter, k);
        } else if (const int row = placeLabel(labelEnds, lane, x, w, 2); row >= 0) {
            const double bottom = y - 9 - row * labelRow;
            TinLayout::drawIdBox(p, QPointF(x, bottom), id);
            p.setPen(key == m_selected ? text : ink);
            p.drawText(QRectF(x - w / 2.0, bottom - 2 * lh - 2, w, lh), Qt::AlignCenter, k);
        }
        QString tip = tr("Tag %1 (%2) at %3, line %4").arg(name, TinLayout::notationName(letter),
                                                          km(key == m_dragTag && m_dragX >= 0 ? locAt(x) : m_loc.value(key)),
                                                          m_pointOfTag.contains(key) ? tr("point %1").arg(m_l.points.at(m_pointOfTag.value(key)).name)
                                                                                        : m_laneNames.value(lane));
        tip += QLatin1Char('\n') + tr("TIN %1 Nominal / %2 Reverse").arg(in.tinNom).arg(in.tinRev);
        if (!dup.isEmpty())
            tip += QLatin1Char('\n') + (shape == TinLayout::Box ? tr("Duplicate %1 at the same location").arg(StationLayout::nameOfKey(dup))
                                                                : tr("Duplicate %1 at %2").arg(StationLayout::nameOfKey(dup), km(m_loc.value(dup))));
        tip += QLatin1Char('\n') + (key == m_dragTag && m_dragX >= 0 ? tr("Release to move it here (re-encoded, CRC-30 recomputed)")
                                                                        : tr("Drag along the line to move it"));
        m_hits << Hit{ QRectF(x - 9, y - 8, 18, 16), key, tip };
    }
    // A duplicate away from its main: a small mark of its own, to drag.
    for (auto it = dupOf.constBegin(); it != dupOf.constEnd(); ++it) {
        if (m_loc.value(it.value()) == m_loc.value(it.key())) continue;
        const double x = xAt(it.value());
        const double y = tagPos(it.value(), x).y();
        p.setPen(QPen(m_flagged.contains(StationLayout::nameOfKey(it.value())) ? UiColor::warning() : text, 1.2));
        p.setBrush(palette().base());
        p.drawEllipse(QPointF(x, y), 3, 3);
        if (it.value() == m_selected || m_marked.contains(it.value())) {
            p.setPen(QPen(UiColor::selectedMark(), 2));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QPointF(x, y), 8, 8);
        }
        m_hits << Hit{ QRectF(x - 5, y - 5, 10, 10), it.value(),
                       tr("Duplicate tag %1 at %2 (main %3 at %4)").arg(StationLayout::nameOfKey(it.value()), km(m_loc.value(it.value())),
                                                                        StationLayout::nameOfKey(it.key()),
                                                                        km(m_loc.value(it.key()))) };
    }

    // ---- session 424: where the MA ends: a bar across the lane, its length in the dimensions' row ------
    m_kavachMaEnd = QRectF();
    if (!m_kavachView.sections.isEmpty()) {
        const QPolygonF path = kavachPath(m_kavachView.sections.size() - 1);
        const KavachSection &k = m_kavachView.sections.last();
        if (!path.isEmpty() && !k.tags.isEmpty() && m_loc.contains(k.tags.last())) {
            const QPointF e = path.last();
            p.setPen(QPen(UiColor::accent(), 3, Qt::SolidLine, Qt::FlatCap));
            p.drawLine(QPointF(e.x(), e.y() - 14), QPointF(e.x(), e.y() + 14));
            const int w = fm.horizontalAdvance(m_kavachView.maText) + 8;
            const QRectF box(e.x() - w / 2.0, e.y() + 28 + 3 * lh - lh / 2.0, w, lh);   // under the gap dimensions' row
            p.setPen(Qt::NoPen);
            p.setBrush(palette().color(QPalette::Base));
            p.drawRect(box);
            p.setBrush(Qt::NoBrush);
            p.setPen(UiColor::accent());
            p.drawText(box, Qt::AlignCenter, m_kavachView.maText);
            m_kavachMaEnd = QRectF(e.x() - 4, e.y() - 14, 8, 28).united(box);
            m_hits << Hit{ m_kavachMaEnd, QString(),
                           tr("End of the MA: %1, at the foot of the exit signal of %2").arg(m_kavachView.maText, k.route) };
        }
    }

    // ---- TIN labels "(N-65)" in the label room the tags left -----------------------------------
    for (const Run &r : runs) {
        if (r.tin <= 0) continue;
        const QString label = TinLayout::tinLabel(r.tin);
        const int w = fm.horizontalAdvance(label) + 4;
        const double mid = (r.from + r.to) / 2.0;
        const int row = placeLabel(labelEnds, r.lane, mid, w, 2);
        if (row < 0) continue;
        p.setPen(UiColor::withContrast(TinLayout::tinColor(r.tin), palette().color(QPalette::Base), 4.5));
        p.drawText(QRectF(mid - w / 2.0, laneY(r.lane) - 9 - row * labelRow - lh, w, lh), Qt::AlignCenter, label);
    }

    // ---- legend (H2.25) and the scale ------------------------------------------------------------
    legend(&p, lanesBottom + lh + 12, width());
    const int sy = height() - lh - 12;
    p.setPen(QPen(UiColor::frame(), 1));
    p.drawLine(kLeft, sy, width() - kRight, sy);
    // One tick step per scale (session 218: the yard has its own).
    double lastX = -1e9;
    for (int k = 0; k + 1 < m_knots.size(); ++k) {
        const qint64 a = m_knots.at(k).first, b = m_knots.at(k + 1).first;
        const double perPx = double(b - a) / qMax(1.0, xOf(b) - xOf(a));
        double step = 1;
        while (step / perPx < 90) step *= (QString::number(step).startsWith(QLatin1Char('2')) ? 2.5 : 2.0);
        if (k > 0) {   // where the scale changes
            p.drawLine(QPointF(xOf(a), sy - 5), QPointF(xOf(a), sy + 5));
        }
        for (double m = std::ceil(a / step) * step; m <= b; m += step) {
            const double x = xOf(qint64(m));
            if (x - lastX < 100) continue;
            lastX = x;
            p.drawLine(QPointF(x, sy), QPointF(x, sy + 4));
            p.setPen(ink);
            p.drawText(QRectF(x - 50, sy + 5, 100, lh), Qt::AlignCenter, km(qint64(m)));
            p.setPen(QPen(UiColor::frame(), 1));
        }
    }
    // Session 411: the Loco Simulator's loco, on its line, pointing the way it runs.
    if (const QRectF r = simLocoRect(); !r.isNull()) {
        const double y = r.center().y(), nose = m_simSign * 7;
        const double back = m_simSign > 0 ? r.left() : r.right(), front = m_simSign > 0 ? r.right() : r.left();
        QPolygonF body;
        body << QPointF(back, r.top()) << QPointF(front - nose, r.top()) << QPointF(front, y)
             << QPointF(front - nose, r.bottom()) << QPointF(back, r.bottom());
        p.setPen(QPen(text, 1.2));
        p.setBrush(UiColor::accent());
        p.drawPolygon(body);
        p.setBrush(Qt::NoBrush);
        const QString label = tr("Loco %1").arg(km(qint64(std::llround(m_simLoc))));
        const int w = fm.horizontalAdvance(label) + 6;
        const QRectF box(r.center().x() - w / 2.0, r.bottom() + 3, w, lh);
        p.setPen(Qt::NoPen);
        p.setBrush(palette().color(QPalette::Base));
        p.drawRect(box);
        p.setBrush(Qt::NoBrush);
        p.setPen(UiColor::accent());
        p.drawText(box, Qt::AlignCenter, label);
        m_hits << Hit{ r, QString(), tr("Loco Simulator: line %1 at %2").arg(m_simLine, km(qint64(std::llround(m_simLoc)))) };
    }
    // Session 428: the locos the live station has heard: the train its length
    // back from the loco, the MA's end, the SoS sent. Labels of locos close
    // together stack, not overprint.
    QVector<QRectF> liveLabels;
    for (int i = 0; i < m_liveView.locos.size(); ++i) {
        const QRectF r = liveLocoRect(i);
        if (r.isNull()) continue;
        const LiveLoco &k = m_liveView.locos.at(i);
        const double y = r.center().y(), nose = k.sign * 7;
        const double back = k.sign > 0 ? r.left() : r.right(), front = k.sign > 0 ? r.right() : r.left();
        if (k.trainLength > 0) {
            const double rear = xOf(k.absLoc - k.sign * k.trainLength);
            p.setPen(QPen(text, 1));
            p.setBrush(palette().color(QPalette::Base));
            p.drawRect(QRectF(QPointF(qMin(rear, r.center().x()), y - 4), QPointF(qMax(rear, r.center().x()), y + 4)));
        }
        QPolygonF body;
        body << QPointF(back, r.top()) << QPointF(front - nose, r.top()) << QPointF(front, y)
             << QPointF(front - nose, r.bottom()) << QPointF(back, r.bottom());
        p.setPen(QPen(text, 1.2));
        p.setBrush(k.sos.isEmpty() ? UiColor::accent() : UiColor::error());
        p.drawPolygon(body);
        p.setBrush(Qt::NoBrush);
        const QString label = k.sos.isEmpty() ? tr("Loco %1 · %2").arg(k.id).arg(k.maText)
                                              : tr("Loco %1 · %2 · %3").arg(k.id).arg(k.maText, k.sos);
        const int w = fm.horizontalAdvance(label) + 6;
        QRectF box(r.center().x() - w / 2.0, r.top() - 3 - lh, w, lh);
        for (int up = 0; up < 6 && std::any_of(liveLabels.cbegin(), liveLabels.cend(),
                                               [&box](const QRectF &o) { return o.intersects(box); }); ++up)
            box.translate(0, -lh);
        liveLabels << box;
        p.setPen(Qt::NoPen);
        p.setBrush(palette().color(QPalette::Base));
        p.drawRect(box);
        p.setBrush(Qt::NoBrush);
        p.setPen(k.sos.isEmpty() ? UiColor::accent() : UiColor::error());
        p.drawText(box, Qt::AlignCenter, label);
        QString tip = tr("Loco %1 at %2, %3 m long").arg(k.id).arg(km(k.absLoc)).arg(k.trainLength);
        tip += QLatin1Char('\n') + (k.maEnd >= 0 ? tr("%1, to %2").arg(k.maText, km(k.maEnd)) : k.maText);
        if (!k.sos.isEmpty()) tip += QLatin1Char('\n') + k.sos;
        m_hits << Hit{ r.united(box), QString(), tip };
        const QPolygonF ma = liveMaPath(i);
        if (ma.size() >= 2) {
            const QPointF e = ma.last();
            p.setPen(QPen(UiColor::ok(), 3, Qt::SolidLine, Qt::FlatCap));
            p.drawLine(QPointF(e.x(), e.y() - 12), QPointF(e.x(), e.y() + 12));
            m_hits << Hit{ QRectF(e.x() - 4, e.y() - 12, 8, 24), QString(),
                           tr("End of loco %1's MA: %2, at %3").arg(k.id).arg(k.maText, km(k.maEnd)) };
        }
    }
    // Session 223: the box being drawn (Shift + drag).
    if (!m_band.isNull()) {
        p.setPen(QPen(UiColor::selectedMark(), 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(m_band);
    }
    // Session 233: the measurement — its two ends, and between them a
    // dimension line with the distance along the track.
    if (m_measureA.set) {
        const double top = topMargin() - 4, bottom = lanesBottom + 6;
        const double xa = xOf(m_measureA.loc);
        const bool live = !m_measureB.set && m_measuring && !m_hover.isNull();
        const bool have = m_measureB.set || live;
        const qint64 locB = m_measureB.set ? m_measureB.loc : locAt(m_hover.x());
        const double xb = have ? xOf(locB) : xa;
        p.setPen(QPen(UiColor::accent(), 1, Qt::DashLine));
        p.drawLine(QPointF(xa, top), QPointF(xa, bottom));
        if (have) p.drawLine(QPointF(xb, top), QPointF(xb, bottom));
        if (have && std::fabs(xb - xa) >= 1) {
            const double y = top + 2;
            p.setPen(QPen(UiColor::accent(), 2));
            p.drawLine(QPointF(xa, y), QPointF(xb, y));
            const double dir = xb > xa ? 1 : -1;
            for (const QPointF &tip : { QPointF(xa, y), QPointF(xb, y) }) {
                const double d = tip.x() == xa ? dir : -dir;
                p.drawLine(tip, QPointF(tip.x() + 7 * d, y - 4));
                p.drawLine(tip, QPointF(tip.x() + 7 * d, y + 4));
            }
            const QString label = QStringLiteral("%1 m").arg(std::llabs(locB - m_measureA.loc));
            const int w = fm.horizontalAdvance(label) + 10;
            const QRectF box((xa + xb) / 2.0 - w / 2.0, y - lh - 2, w, lh + 2);
            p.setPen(Qt::NoPen);
            p.setBrush(palette().color(QPalette::Base));
            p.drawRoundedRect(box, 3, 3);
            p.setBrush(Qt::NoBrush);
            p.setPen(UiColor::accent());
            p.drawText(box, Qt::AlignCenter, label);
        }
    }
}

void StationLayoutCanvas::setSelectedSignal(int i)
{
    m_selSignal = i;
    update();
}

void StationLayoutCanvas::setSelectedPoint(int i)
{
    m_selPoint = i;
    update();
}

void StationLayoutCanvas::setMeasuring(bool on)
{
    m_measuring = on;
    m_hover = QPoint();
    if (on) {
        setCursor(Qt::CrossCursor);
        setFocus();         // so Esc reaches it
    } else {
        unsetCursor();
    }
    update();
}

void StationLayoutCanvas::keyPressEvent(QKeyEvent *e)
{
    if (m_measuring && e->key() == Qt::Key_Escape) {
        emit measureCancelled();
        return;
    }
    if (m_picking && e->key() == Qt::Key_Escape) {
        emit pickCancelled();
        return;
    }
    QWidget::keyPressEvent(e);
}

void StationLayoutCanvas::clearMeasure()
{
    m_measureA = MeasureEnd();
    m_measureB = MeasureEnd();
    update();
}

void StationLayoutCanvas::measureAt(const QPointF &pos)
{
    MeasureEnd end;
    end.set = true;
    end.y = pos.y();
    bool found = false;
    // The nearest mark under the click: on a crowded sheet (narrower fonts,
    // a smaller window) neighbouring tags' boxes overlap.
    QVector<const Hit *> under;
    for (const Hit &h : m_hits)
        if (h.rect.contains(pos) && (!h.tag.isEmpty() || h.signal >= 0 || h.point >= 0)) under << &h;
    std::stable_sort(under.begin(), under.end(), [&pos](const Hit *a, const Hit *b) {
        const QPointF da = a->rect.center() - pos, db = b->rect.center() - pos;
        return QPointF::dotProduct(da, da) < QPointF::dotProduct(db, db);
    });
    for (const Hit *hp : under) {
        const Hit &h = *hp;
        if (!h.tag.isEmpty() && m_loc.contains(h.tag)) {
            end.loc = m_loc.value(h.tag);
            end.what = tr("tag %1").arg(StationLayout::nameOfKey(h.tag));
        } else if (h.signal >= 0 && m_loc.contains(m_l.signalList.at(h.signal).footTag)) {
            end.loc = m_loc.value(m_l.signalList.at(h.signal).footTag);
            end.what = tr("signal %1").arg(m_l.signalList.at(h.signal).name);
        } else if (h.point >= 0) {
            const StationLayout::Point &pt = m_l.points.at(h.point);
            end.loc = std::fabs(pos.x() - xOf(pt.loc1)) <= std::fabs(pos.x() - xOf(pt.loc2)) ? pt.loc1 : pt.loc2;
            end.what = tr("point %1").arg(pt.name);
        } else {
            continue;
        }
        found = true;
        break;
    }
    if (!found) {
        end.loc = locAt(pos.x());
        end.what = km(end.loc);
    } else {
        end.what += QStringLiteral(" (%1)").arg(km(end.loc));
    }
    if (!m_measureA.set || m_measureB.set) {
        m_measureA = end;
        m_measureB = MeasureEnd();
        emit measured(tr("From %1: click the end").arg(end.what));
    } else {
        m_measureB = end;
        emit measured(tr("From %1 to %2: %3 m").arg(m_measureA.what, m_measureB.what)
                          .arg(std::llabs(m_measureB.loc - m_measureA.loc)));
    }
    update();
}

// =============================================================================
//  StationLayoutWindow
// =============================================================================

namespace {

// The generic sheet tables (1..5) show the tool's sheet with the same index
// in StationLayout::toSheets(): signals, points, lines, station, texts.
const char *kTabNames[] = { "Tags", "Signals", "Points", "Lines", "Station", "Texts" };

// Session 433: the Signals tab's aspects, calling_on and shunt columns, picked
// from a list (the value written; "" = not said, the drawing decides).
class SignalLookDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    static QVector<QPair<QString, QString>> choices(const QString &column)
    {
        if (column == QLatin1String("aspects"))
            return { { QObject::tr("(from its name)"), QString() }, { QObject::tr("2: Red, Yellow"), QStringLiteral("2") },
                     { QObject::tr("3: Red, Yellow, Green"), QStringLiteral("3") },
                     { QObject::tr("4: Red, Yellow, Green, Yellow"), QStringLiteral("4") },
                     { QObject::tr("0: a shunt signal only"), QStringLiteral("0") } };
        if (column == QLatin1String("calling_on") || column == QLatin1String("shunt"))
            return { { QObject::tr("no"), QString() }, { QObject::tr("yes"), QStringLiteral("1") } };
        return {};
    }
    static QString column(const QModelIndex &i) { return i.model()->headerData(i.column(), Qt::Horizontal).toString(); }
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &o, const QModelIndex &i) const override
    {
        const auto c = choices(column(i));
        if (c.isEmpty()) return QStyledItemDelegate::createEditor(parent, o, i);
        auto *box = new QComboBox(parent);
        for (const auto &x : c) box->addItem(x.first, x.second);
        return box;
    }
    void setEditorData(QWidget *w, const QModelIndex &i) const override
    {
        if (auto *box = qobject_cast<QComboBox *>(w)) {
            const int at = box->findData(i.data().toString());
            box->setCurrentIndex(qMax(0, at));
            return;
        }
        QStyledItemDelegate::setEditorData(w, i);
    }
    void setModelData(QWidget *w, QAbstractItemModel *m, const QModelIndex &i) const override
    {
        if (auto *box = qobject_cast<QComboBox *>(w)) { m->setData(i, box->currentData().toString()); return; }
        QStyledItemDelegate::setModelData(w, m, i);
    }
};

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

// Session 429: a tag number's tags in the layout, each by its drawing key
// (StationLayout::tagKeys) and location: its main tags, or its duplicate
// ("…D") where it has no main.
struct TagCopy { QString key; qint64 loc = 0; };
QHash<int, QVector<TagCopy>> tagCopies(const StationLayout::Layout &l)
{
    const QStringList keys = StationLayout::tagKeys(l);
    QHash<int, QVector<TagCopy>> mains, dups;
    for (int i = 0; i < l.tags.size(); ++i) {
        const QString &n = l.tags.at(i).name;
        const bool dup = isDuplicateName(n);
        bool ok = false;
        const int id = (dup ? n.chopped(1) : n).toInt(&ok);
        const StationLayout::TagInfo in = StationLayout::info(l.tags.at(i));
        if (ok && in.ok) (dup ? dups : mains)[id] << TagCopy{ keys.at(i), in.absLoc };
    }
    for (auto it = dups.cbegin(); it != dups.cend(); ++it) if (!mains.contains(it.key())) mains.insert(it.key(), it.value());
    return mains;
}

// The tag of number `id` nearest `want` (-1: none of that number); *at its location.
QString nearestCopy(const QHash<int, QVector<TagCopy>> &copies, int id, qint64 want, qint64 *at = nullptr)
{
    const auto it = copies.constFind(id);
    if (it == copies.cend()) return QString();
    const TagCopy *best = nullptr;
    for (const TagCopy &c : it.value())
        if (!best || std::llabs(c.loc - want) < std::llabs(best->loc - want)) best = &c;
    if (at && best) *at = best->loc;
    return best ? best->key : QString();
}

}  // namespace

StationLayoutCanvas::KavachView kavachViewOf(const StationLayout::Layout &l, const StationKavach::Station &s,
                                             int route, QStringList *missing)
{
    StationLayoutCanvas::KavachView v;
    const QHash<int, QVector<TagCopy>> copies = tagCopies(l);
    auto note = [&](int id) {
        if (missing && !missing->contains(QString::number(id))) *missing << QString::number(id);
    };
    // The signals: at their foot tag's name (drawn at every tag of it).
    for (const StationKavach::Signal &g : s.signalList) {
        const QString key = nearestCopy(copies, g.footTag, 0);
        if (key.isEmpty()) { note(g.footTag); continue; }
        v.signalList << StationLayoutCanvas::KavachSignal{ g.name, StationLayout::nameOfKey(key), g.aspect };
    }
    if (route < 0 || route >= s.routes.size()) return v;
    const StationKavach::Sections sx = StationKavach::sectionsFrom(s, StationKavach::State::initial(s), route);
    // Session 429: of a number the layout lists more than once, the tags the
    // linking distances lead to: the route starts at the entry tag whose
    // route agrees best, each next tag the one nearest where its distance says.
    auto walk = [&](const StationKavach::Route &r, qint64 start, QStringList *keys) {
        const int sign = r.pktDir == 2 ? -1 : 1;
        qint64 at = start, off = 0;
        for (int i = 0; i < r.tags.size(); ++i) {
            const qint64 want = at + (i ? sign * r.tags[i].dist : 0);
            qint64 got = want;
            const QString key = nearestCopy(copies, r.tags[i].tag, want, &got);
            off += std::llabs(got - want);
            at = key.isEmpty() ? want : got;
            if (keys && !key.isEmpty()) *keys << key;
        }
        return qMakePair(off, at);
    };
    // The entry tag whose whole MA (every section) agrees best; of equals, the first.
    qint64 start = 0;
    if (!sx.routes.isEmpty()) {
        const StationKavach::Route &r0 = s.routes.at(sx.routes.first());
        qint64 best = -1;
        for (const TagCopy &c : copies.value(r0.tags.isEmpty() ? -1 : r0.tags.first().tag)) {
            qint64 off = 0, at = c.loc;
            for (int i : sx.routes) {
                const QPair<qint64, qint64> w = walk(s.routes.at(i), at, nullptr);
                off += w.first;
                at = w.second;
            }
            if (best < 0 || off < best) { best = off; start = c.loc; }
        }
    }
    for (int i = 0; i < sx.routes.size(); ++i) {
        const StationKavach::Route &r = s.routes.at(sx.routes.at(i));
        StationLayoutCanvas::KavachSection k;
        k.route = r.name;
        k.from = sx.offsets.at(i);
        k.to = k.from + r.length();
        start = walk(r, start, &k.tags).second;   // the next section starts at this one's exit
        for (const StationKavach::RouteTag &t : r.tags) {
            if (!copies.contains(t.tag)) { note(t.tag); continue; }
            const QString name = StationLayout::nameOfKey(copies.value(t.tag).first().key);
            if (copies.value(t.tag).size() > 1 && !k.repeated.contains(name)) k.repeated << name;
        }
        v.sections << k;
    }
    v.maText = QObject::tr("MA %1 m").arg(sx.maEnd);
    return v;
}

StationLayoutCanvas::LiveView liveViewOf(const StationLayout::Layout &l, const StationKavach::Station &s,
                                         const StationKavach::State &st, const QList<StationKavach::Loco> &locos)
{
    StationLayoutCanvas::LiveView v;
    for (StationLayoutCanvas::KavachSignal k : kavachViewOf(l, s, -1).signalList) {
        k.aspect = st.aspectOf(s, k.name);
        v.signalList << k;
    }
    const QHash<int, QVector<TagCopy>> copies = tagCopies(l);
    for (const StationKavach::Loco &o : locos) {
        if (o.absLoc < 0) continue;
        StationLayoutCanvas::LiveLoco k;
        k.id = o.id;
        k.absLoc = o.absLoc;
        k.trainLength = o.trainLength;
        const StationKavach::Route *r0 = !o.maRoutes.isEmpty() && o.maRoutes.first() >= 0 && o.maRoutes.first() < s.routes.size()
                                             ? &s.routes.at(o.maRoutes.first()) : nullptr;
        k.sign = (r0 ? r0->pktDir : o.dir) == 2 ? -1 : 1;
        qint64 refLoc = -1;
        k.laneTag = nearestCopy(copies, o.refTag, o.absLoc, &refLoc);   // the tag it last read, where it is
        if (k.laneTag.isEmpty()) {   // no tag on a route read yet: the layout's tag nearest it
            qint64 best = -1;
            for (const QVector<TagCopy> &cs : copies)
                for (const TagCopy &c : cs)
                    if (best < 0 || std::llabs(c.loc - o.absLoc) < best) { best = std::llabs(c.loc - o.absLoc); k.laneTag = c.key; }
        }
        k.maText = o.maSent < 0 ? QObject::tr("no MA sent yet") : QObject::tr("MA %1 m").arg(o.maSent);
        if (o.maSent == 0) k.maText = QObject::tr("MA 0");
        if (o.sos == 4) k.sos = QObject::tr("SoS SPAD");
        else if (o.sos == 5) k.sos = QObject::tr("SoS Rear-end");
        // Where the MA ends: from the start signal (the ref tag's place nearest the loco, less its distance).
        if (r0 && o.maSent > 0 && refLoc >= 0) {
            qint64 refDist = -1;
            for (int i = 0, d = 0; i < r0->tags.size(); ++i) {
                d += i ? r0->tags[i].dist : 0;
                if (r0->tags[i].tag == o.refTag) { refDist = d; break; }
            }
            if (refDist >= 0) {
                const qint64 start = refLoc - k.sign * refDist;
                const qint64 self = (o.absLoc - start) * k.sign;
                k.maEnd = start + k.sign * o.maSent;
                qint64 d = 0;
                for (int i = 0; i < o.maRoutes.size(); ++i) {
                    if (o.maRoutes[i] < 0 || o.maRoutes[i] >= s.routes.size()) break;
                    const QVector<StationKavach::RouteTag> &ts = s.routes[o.maRoutes[i]].tags;
                    for (int j = (i ? 1 : 0); j < ts.size(); ++j) {
                        d += j ? ts[j].dist : 0;
                        // Session 429: of a number listed more than once, the tag where its distance says.
                        const QString key = d > self && d < o.maSent ? nearestCopy(copies, ts[j].tag, start + k.sign * d) : QString();
                        if (!key.isEmpty()) k.maTags << key;
                    }
                }
            }
        }
        v.locos << k;
    }
    return v;
}

void StationLayoutWindow::showLiveStation(const StationKavach::Station &s, const StationKavach::State &st,
                                          const QList<StationKavach::Loco> &locos, const QString &from)
{
    // Session 435: kept, so the drawing can edit it and send it back.
    m_lvStation = s;
    m_lvState = st;
    m_lvLocos = locos;
    m_lvFrom = from;
    m_lvLive = m_lvActive = true;
    refreshLive();
}

void StationLayoutWindow::clearLiveStation()
{
    m_lvLive = m_lvActive = false;
    m_lvLocos.clear();
    m_canvas->setLiveView(StationLayoutCanvas::LiveView());
    m_liveState->clear();
    m_liveState->hide();
}

// ---- session 435: the station's state edited on the drawing -----------------------------------

bool StationLayoutWindow::ensureLive()
{
    if (m_lvActive) return true;
    // Not live: a what-if from the layout's KAVACH data (or none: its signals by name).
    StationKavach::Station s;
    if (!m_layout.kavach.isEmpty()) StationKavach::fromJson(m_layout.kavach, &s);
    m_lvStation = s;
    m_lvState = StationKavach::State::initial(s);
    m_lvLocos.clear();
    m_lvFrom = tr("this drawing (what-if: not sent to a station)");
    m_lvActive = true;
    return true;
}

QString StationLayoutWindow::kavachPointName(const QString &layoutPoint) const
{
    auto bare = [](QString n) {
        n = n.trimmed();
        if (n.startsWith(QLatin1Char('P'), Qt::CaseInsensitive)) n.remove(0, 1);
        return n;
    };
    for (const QString &n : m_lvStation.pointNames())
        if (n == layoutPoint || bare(n) == bare(layoutPoint)) return n;
    return layoutPoint;
}

void StationLayoutWindow::refreshLive()
{
    if (!m_lvActive) return;
    StationLayoutCanvas::LiveView v = liveViewOf(m_layout, m_lvStation, m_lvState, m_lvLocos);
    // A layout signal the KAVACH data lacks, given an aspect here, is drawn with it too.
    QSet<QString> shown;
    for (const StationLayoutCanvas::KavachSignal &k : v.signalList) shown.insert(k.name);
    for (const StationLayout::Signal &sg : m_layout.signalList)
        if (!shown.contains(sg.name) && m_lvState.aspect.contains(sg.name)) {
            v.signalList << StationLayoutCanvas::KavachSignal{ sg.name, sg.footTag, m_lvState.aspect.value(sg.name) };
            shown.insert(sg.name);
        }
    v.pointsShown = true;
    v.whatIf = !m_lvLive;
    for (const StationLayout::Point &pt : m_layout.points)
        if (m_lvState.reverse.contains(kavachPointName(pt.name))) v.reversePoints.insert(pt.name);
    v.occupied = m_lvState.occupied;   // session 436
    v.occupiedBy = m_lvState.occupiedBy;   // session 441
    QStringList set;
    for (auto it = m_lvState.routes.cbegin(); it != m_lvState.routes.cend(); ++it) {
        const StationLayoutCanvas::KavachView kv = kavachViewOf(m_layout, m_lvStation, it.value());
        if (!kv.sections.isEmpty()) v.routesSet << kv.sections.first();
        // Session 442: a route a train is in, said so.
        set << m_lvStation.routes.value(it.value()).name + (m_lvState.entered.contains(it.key()) ? tr(" (a train in it)") : QString());
    }
    m_canvas->setLiveView(v);
    QStringList parts;
    for (const StationLayoutCanvas::LiveLoco &k : v.locos) {
        QString t = tr("loco %1 at %2, %3").arg(k.id).arg(km(k.absLoc), k.maText);
        if (!k.sos.isEmpty()) t += QStringLiteral(", ") + k.sos;
        parts << t;
    }
    QString line = m_lvLive ? tr("Live from %1, station %2: %3").arg(m_lvFrom).arg(m_lvStation.stationId)
                                  .arg(parts.isEmpty() ? tr("no loco heard yet") : parts.join(QStringLiteral("; ")))
                            : tr("Signals and points from %1").arg(m_lvFrom);
    if (!set.isEmpty()) { set.sort(); line += tr(" · routes set: %1").arg(set.join(QStringLiteral(", "))); }
    m_liveState->setText(line);
    if (m_formKind == 3) showForm(m_formKind, m_formIndex);   // session 437: a point's form says how it lies
    m_liveState->setToolTip(tr("Right-click a signal for its aspect or a route from it; double-click a point to throw it"));
    m_liveState->show();
}

void StationLayoutWindow::liveEdited(const QString &what)
{
    if (m_lvLive) emit liveStateEdited(m_lvState);
    refreshLive();
    m_status->ok(m_lvLive ? tr("%1: sent to %2").arg(what, m_lvFrom) : tr("%1 (what-if: not sent to a station)").arg(what));
}

bool StationLayoutWindow::setLiveAspect(const QString &signal, int aspect)
{
    if (signal.isEmpty() || aspect < 0 || aspect > 63) return false;
    ensureLive();
    m_lvState.aspect.insert(signal, aspect);
    liveEdited(tr("%1 %2").arg(signal, StationKavach::aspectName(aspect)));
    return true;
}

bool StationLayoutWindow::setLivePoint(const QString &point, bool reverse)
{
    if (point.isEmpty()) return false;
    ensureLive();
    const QString name = kavachPointName(point);
    // Session 442: the station working its signals: a point a set route holds is not thrown.
    if (m_lvState.interlocking && reverse != m_lvState.reverse.contains(name)) {
        const QString why = StationInterlock::pointLocked(m_lvStation, m_lvState, name);
        if (!why.isEmpty()) { m_status->fail(tr("Not thrown: %1").arg(why)); return false; }
    }
    if (reverse) m_lvState.reverse.insert(name); else m_lvState.reverse.remove(name);
    // A route set through it the other way is no longer set.
    for (auto it = m_lvState.routes.begin(); it != m_lvState.routes.end();) {
        const StationKavach::Route &r = m_lvStation.routes.at(it.value());
        if ((reverse ? r.pointsNormal : r.pointsReverse).contains(name)) {
            m_lvState.entered.remove(it.key());
            it = m_lvState.routes.erase(it);
        } else {
            ++it;
        }
    }
    liveEdited(tr("point %1 %2").arg(point, reverse ? tr("Reverse") : tr("Normal")));
    return true;
}

bool StationLayoutWindow::setLiveRoute(int route)
{
    ensureLive();
    if (route < 0 || route >= m_lvStation.routes.size()) return false;
    const StationKavach::Route &r = m_lvStation.routes.at(route);
    // The entry signal off: a diverging route with its route indicator, else Yellow (a
    // proceed aspect it already shows is kept). Session 442: StationInterlock sets it.
    int indicator = 0;
    for (const StationLayout::Signal &sg : m_layout.signalList) if (sg.name == r.entry && sg.indicator > 0) indicator = sg.indicator;
    const QString why = StationInterlock::setRoute(m_lvStation, &m_lvState, route, indicator);
    if (!why.isEmpty()) { m_status->fail(tr("Not set: %1").arg(why)); return false; }
    const int aspect = m_lvState.aspectOf(m_lvStation, r.entry);
    liveEdited(tr("route %1 set, %2 %3").arg(r.name, r.entry, StationKavach::aspectName(aspect)));
    return true;
}

bool StationLayoutWindow::setLiveOccupied(const QString &circuit, bool occupied)
{
    if (circuit.isEmpty()) return false;
    ensureLive();
    if (occupied) m_lvState.occupied.insert(circuit); else m_lvState.occupied.remove(circuit);
    liveEdited(tr("track circuit %1 %2").arg(circuit, occupied ? tr("occupied") : tr("clear")));
    return true;
}

bool StationLayoutWindow::cancelLiveRoute(const QString &entrySignal)
{
    if (!m_lvState.routes.contains(entrySignal)) return false;
    const QString name = m_lvStation.routes.value(m_lvState.routes.value(entrySignal)).name;
    const QString why = StationInterlock::cancelRoute(m_lvStation, &m_lvState, entrySignal);
    if (!why.isEmpty()) { m_status->fail(tr("Not cancelled: %1").arg(why)); return false; }
    liveEdited(tr("route %1 cancelled, %2 Red").arg(name, entrySignal));
    return true;
}

bool StationLayoutWindow::setSignalIndicator(int index, int position)
{
    if (index < 0 || index >= m_layout.signalList.size() || position < 0 || position > 6) return false;
    if (m_layout.signalList.at(index).indicator == position) return true;
    const StationLayout::Layout before = m_layout;
    m_layout.signalList[index].indicator = position;
    changed(tr("signal %1's route indicator").arg(m_layout.signalList.at(index).name), before);
    m_canvas->setSelectedSignal(index);
    return true;
}

QMenu *StationLayoutWindow::pointMenuFor(int index)
{
    if (index < 0 || index >= m_layout.points.size()) return nullptr;
    const QString name = m_layout.points.at(index).name;
    if (m_lvActive == false) ensureLive();
    const bool reverse = m_lvState.reverse.contains(kavachPointName(name));
    auto *menu = new QMenu(this);
    menu->addSection(tr("Point %1").arg(name));
    QAction *n = menu->addAction(tr("Normal"));
    QAction *r = menu->addAction(tr("Reverse"));
    n->setCheckable(true);
    r->setCheckable(true);
    n->setChecked(!reverse);
    r->setChecked(reverse);
    connect(n, &QAction::triggered, this, [this, name]() { setLivePoint(name, false); });
    connect(r, &QAction::triggered, this, [this, name]() { setLivePoint(name, true); });
    menu->addSeparator();
    menu->addAction(tr("Show its row"), this, [this, index]() {
        setTablesShown(true);
        selectSheetRow(2, index);
    });
    return menu;
}

void StationLayoutWindow::updateKavachView()
{
    const bool on = m_tabs->currentWidget() == m_kavach && !m_layout.kavach.isEmpty();
    m_canvas->setKavachView(on ? kavachViewOf(m_layout, m_kavach->station(), m_kavach->currentRoute())
                               : StationLayoutCanvas::KavachView());
}

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
    openMenu->setToolTipsVisible(true);
    QAction *openFileAct = openMenu->addAction(tr("Open file…"));
    openFileAct->setToolTip(tr("A station XML (station_<id>.xml) or a DLConsole station layout (.json)"));
    QAction *defaultAct = openMenu->addAction(tr("Default layout (station.xlsx)"));
    openMenu->addMenu(stationLibraryMenu(openMenu, [this](const QString &path) {
        if (confirmDiscard()) openFile(path);
    }));
    QAction *newAct = openMenu->addAction(tr("New, empty"));
    // Session 440: in this menu (it was a button: the bar had no room for wider fonts once View came).
    QAction *importAct = openMenu->addAction(tr("Import .xlsx…"));
    importAct->setToolTip(tr("A station file of the old Python tool (config/station/*.xlsx): tags, signals, points, "
                             "lines, station, texts; other sheets (relaymap, …) are kept for export"));
    // Session 226: compare the open layout with another station file.
    openMenu->addSeparator();
    QMenu *cmpMenu = openMenu->addMenu(tr("Compare with"));
    cmpMenu->addAction(tr("A file (.xml, .json or .xlsx)…"), this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Compare with"), QFileInfo(m_path).path(),
                                                          tr("Station layout (*.xml *.json *.xlsx)"));
        if (!path.isEmpty()) compareWith(path);
    });
    cmpMenu->addAction(tr("Default layout (station.xlsx)"), this, [this]() { compareWith(StationLayout::defaultFile()); });
    cmpMenu->addMenu(stationLibraryMenu(cmpMenu, [this](const QString &path) { compareWith(path); }));
    // Session 430: a station's tag-TIN layout drawing, read off by hand.
    QMenu *drawMenu = cmpMenu->addMenu(tr("A tag-layout drawing"));
    for (const QString &path : LayoutDrawing::builtIn()) {
        LayoutDrawing::Drawing d;
        if (!LayoutDrawing::load(path, &d)) continue;
        QAction *a = drawMenu->addAction(d.title, this, [this, path]() { compareWithDrawing(path); });
        a->setToolTip(d.source);
    }
    drawMenu->addSeparator();
    drawMenu->addAction(tr("A drawing file (.json)…"), this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Compare with a tag-layout drawing"), QFileInfo(m_path).path(),
                                                          tr("Tag-layout drawing (*.json)"));
        if (!path.isEmpty()) compareWithDrawing(path);
    });
    drawMenu->setToolTipsVisible(true);
    cmpMenu->addSeparator();
    cmpMenu->addAction(tr("Stop comparing"), this, [this]() { clearCompare(); });
    // Session 227: a loco's log over the layout.
    QMenu *runMenu = openMenu->addMenu(tr("Show a loco's run"));
    connect(runMenu, &QMenu::aboutToShow, this, [this, runMenu]() {
        runMenu->clear();
        QStringList keys = m_dispatcher ? m_dispatcher->knownKeys() : QStringList();
        keys.sort();
        for (const QString &k : keys)
            runMenu->addAction(k, this, [this, k]() { showRun(m_dispatcher->modelForKey(k), k); });
        if (keys.isEmpty()) runMenu->addAction(tr("(no loco logs open)"))->setEnabled(false);
        runMenu->addSeparator();
        runMenu->addAction(tr("Hide the run"), this, [this]() { clearRun(); });
    });
    openBtn->setMenu(openMenu);
    auto *saveBtn = new QPushButton(tr("Save"), this);
    auto *saveAsBtn = new QPushButton(tr("Save as…"), this);
    auto *exportBtn = new QPushButton(tr("Export"), this);
    auto *exportMenu = new QMenu(exportBtn);
    exportMenu->setToolTipsVisible(true);
    QAction *xlsxAct = exportMenu->addAction(tr("Python tool .xlsx…"));
    xlsxAct->setToolTip(tr("The Python tool's station file, every sheet it reads"));
    QAction *drawingAct = exportMenu->addAction(tr("Drawing as PDF or PNG…"));
    // Session 229.
    QAction *tableAct = exportMenu->addAction(tr("Tag table as PDF or CSV…"));
    tableAct->setToolTip(tr("Every tag by line and location: id, km, kind, TIN nominal / reverse, signals, CRC-30"));
    drawingAct->setToolTip(tr("The drawing as a sheet: A3 landscape PDF, or a PNG image"));
    exportBtn->setMenu(exportMenu);
    // Session 411: a line in the Loco Simulator, one menu entry per line and direction.
    auto *simBtn = new QPushButton(tr("Simulator"), this);
    simBtn->setObjectName(QStringLiteral("stationLayoutSimulator"));
    simBtn->setToolTip(tr("Run a line in the Loco Simulator: its tags in the order a loco meets them, the loco "
                          "drawn here as it goes, each edit here followed there"));
    auto *simMenu = new QMenu(simBtn);
    connect(simMenu, &QMenu::aboutToShow, this, [this, simMenu]() {
        simMenu->clear();
        for (const StationLayout::Line &ln : m_layout.lines) {
            const QString label = ln.name.isEmpty() || ln.name == ln.line ? ln.line : tr("%1 (%2)").arg(ln.line, ln.name);
            QMenu *sub = simMenu->addMenu(tr("Line %1").arg(label));
            sub->addAction(tr("Dir 1, Nominal (locations rising)"), this, [this, ln]() { runLineInSimulator(ln.line, RfidTag::DirNominal); });
            sub->addAction(tr("Dir 2, Reverse (locations falling)"), this, [this, ln]() { runLineInSimulator(ln.line, RfidTag::DirReverse); });
        }
        if (m_layout.lines.isEmpty()) simMenu->addAction(tr("(no lines in this layout)"))->setEnabled(false);
        simMenu->addSeparator();
        simMenu->addAction(tr("Stop: the simulator keeps the route, no longer this layout"), this, [this]() {
            if (m_sim) m_sim->unfollowLayout();
            clearSimulator();
        })->setEnabled(!m_canvas->simLine().isEmpty());
    });
    simBtn->setMenu(simMenu);
    auto *zoomIn = new QPushButton(QStringLiteral("+"), this);
    auto *zoomOut = new QPushButton(QStringLiteral("\u2212"), this);
    auto *fit = new QPushButton(tr("Fit"), this);
    zoomIn->setToolTip(tr("Zoom in (the mouse wheel on the drawing zooms too; drag the sheet to move it)"));
    zoomOut->setToolTip(tr("Zoom out"));
    for (QPushButton *b : { zoomIn, zoomOut }) b->setMaximumWidth(b->fontMetrics().horizontalAdvance(QStringLiteral("MM")) + 24);
    m_undo = new UndoLog(this);
    QAction *undoAct = m_undo->createAction(this);
    addAction(undoAct);
    auto *undoBtn = new QToolButton(this);
    undoBtn->setDefaultAction(undoAct);
    undoBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);

    auto *bar = new QHBoxLayout;
    for (QWidget *w : std::initializer_list<QWidget *>{ openBtn, saveBtn, saveAsBtn, exportBtn })
        bar->addWidget(w);
    bar->addStretch(1);
    // Session 233: Measure, from one place on the drawing to another.
    m_measureBtn = new QPushButton(tr("Measure"), this);
    m_measureBtn->setCheckable(true);
    m_measureBtn->setToolTip(tr("Click a start and an end on the drawing: the distance between them along the track. "
                                "A tag, signal or point under the click gives its location. Esc clears"));
    // Sessions 433, 437: what is shown with the drawing, one button (the bar fits wider fonts).
    auto *viewBtn = new QPushButton(tr("View"), this);
    viewBtn->setObjectName(QStringLiteral("stationLayoutView"));
    auto *viewMenu = new QMenu(viewBtn);
    viewMenu->setToolTipsVisible(true);
    m_formBtn = viewMenu->addAction(tr("Form beside the drawing"));
    m_formBtn->setObjectName(QStringLiteral("stationLayoutFormButton"));
    m_tablesBtn = viewMenu->addAction(tr("Tables under it"));
    m_tablesBtn->setObjectName(QStringLiteral("stationLayoutTables"));
    viewBtn->setMenu(viewMenu);
    for (QWidget *w : std::initializer_list<QWidget *>{ m_measureBtn, zoomOut, zoomIn, fit, viewBtn, undoBtn }) bar->addWidget(w);

    m_canvas = new StationLayoutCanvas(this);
    m_scroll = new QScrollArea(this);
    m_scroll->setWidget(m_canvas);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->viewport()->installEventFilter(m_canvas);

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
    m_run = makeTable(this);
    m_run->setColumnCount(5);
    m_run->setHorizontalHeaderLabels({ tr("Tag"), tr("Line"), tr("Run"), tr("First read"), tr("Detail") });
    m_run->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_run->setToolTip(tr("A loco's run over the layout (Open ▸ Show a loco's run). Double-click a row: the tag, and the read in its log"));
    connect(m_run, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row < 0 || row >= m_runTags.size()) return;
        const RunTag &t = m_runTags.at(row);
        if (tagIndexOf(t.name) >= 0) { selectTags({ t.name }); m_canvas->setSelectedTag(t.name); }
        if (t.firstMs) emit jumpRequested(m_runKey, t.firstMs);
    });
    m_compare = new QListWidget(this);
    m_compare->setToolTip(tr("What differs from the file compared with (Open ▸ Compare with). Double-click a tag's row to select it"));
    m_tabs->addTab(m_compare, tr("Compare"));
    m_tabs->addTab(m_run, tr("Run"));
    // Session 423: the Stationary KAVACH data, edited here; each edit one Undo step.
    m_kavach = new StationKavachEditor(this);
    m_kavach->setSeedSource(&m_layout);
    m_tabs->addTab(m_kavach, tr("KAVACH"));
    m_tabs->setTabToolTip(m_tabs->count() - 1, tr("The station's signals and routes (Table-of-Control rows) for "
                                                  "Packet Maker ▸ Send log to answer a loco as this station would"));
    connect(m_kavach, &StationKavachEditor::edited, this, [this](const QJsonObject &k, const QString &label) {
        const StationLayout::Layout before = m_layout;
        m_layout.kavach = k;
        changed(label, before);
    });
    connect(m_kavach, &StationKavachEditor::refused, this, [this](const QString &why) { m_status->fail(why); });
    // Session 424: the route selected, and the MA's sections on from it, drawn while the tab is shown.
    connect(m_kavach, &StationKavachEditor::routeSelected, this, [this]() { updateKavachView(); });
    // Session 425: the selected route's tags picked on the drawing.
    m_kavach->setTagLocations([this](int id) { return StationLayout::tagLocationsByNumber(m_layout).value(id); });
    connect(m_kavach, &StationKavachEditor::pickingChanged, this, [this](bool on) {
        m_canvas->setPicking(on);
        if (on) {
            m_measureBtn->setChecked(false);
            m_status->state(tr("Pick: click the route's tags on the drawing in the order the loco reads them; Esc ends"));
        }
    });
    connect(m_kavach, &StationKavachEditor::said, this, [this](const QString &what) { m_status->ok(what); });
    connect(m_canvas, &StationLayoutCanvas::pickCancelled, this, [this]() {
        m_kavach->setPicking(false);
        m_status->state(tr("Picking ended"));
    });
    connect(m_compare, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
        const QString name = it->data(Qt::UserRole).toString();
        if (tagIndexOf(name) >= 0) { selectTags({ name }); m_canvas->setSelectedTag(name); }
    });

    auto *addBtn = new QPushButton(tr("Add row"), this);
    addBtn->setToolTip(tr("After the selected row. On Tags: a copy of the selected tag, to move or edit"));
    auto *delBtn = new QPushButton(tr("Delete rows"), this);
    // Session 221: the RFID Tag Builder on a tag of the layout.
    // Session 416: short names, the row fits with wider (Linux) fonts; the tooltips name the Tag Builder.
    auto *editTagBtn = new QPushButton(tr("Edit tag…"), this);
    editTagBtn->setToolTip(tr("Open the selected tag in the RFID Tag Builder: every field by name, CRC-30 computed. "
                              "Its Apply button puts it back here (double-click a tag in the drawing does the same)"));
    auto *newTagBtn = new QPushButton(tr("New tag…"), this);
    newTagBtn->setToolTip(tr("Make a new tag in the RFID Tag Builder, starting from the selected tag; "
                             "its Apply button adds it to the layout"));
    auto *rowBar = new QHBoxLayout;
    rowBar->addWidget(addBtn);
    rowBar->addWidget(delBtn);
    rowBar->addWidget(editTagBtn);
    rowBar->addWidget(newTagBtn);
    // Session 228.
    auto *fixCrcBtn = new QPushButton(tr("Fix CRCs"), this);
    fixCrcBtn->setToolTip(tr("Every tag whose CRC-30 does not match its contents gets the one they give "
                             "(only the CRC bits change; one Undo step)"));
    rowBar->addWidget(fixCrcBtn);
    rowBar->addWidget(simBtn);   // session 411 (here: the top bar has no room for wider fonts)
    connect(fixCrcBtn, &QPushButton::clicked, this, [this]() { fixAllCrcs(); });
    rowBar->addStretch(1);
    auto *bottom = new QWidget(this);
    m_bottom = bottom;
    auto *bl = new QVBoxLayout(bottom);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->addWidget(m_tabs, 1);
    bl->addLayout(rowBar);
    // Session 433: the drawing alone, the whole window (Ctrl+T, or the button).
    m_tablesBtn->setCheckable(true);
    m_tablesBtn->setChecked(true);
    m_tablesBtn->setToolTip(tr("Show or hide the tables under the drawing (%1): hidden, the drawing has the whole window")
                                .arg(QKeySequence(Qt::CTRL | Qt::Key_T).toString(QKeySequence::NativeText)));
    connect(m_tablesBtn, &QAction::toggled, this, [this](bool on) { setTablesShown(on); });
    m_tablesBtn->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    m_tablesBtn->setShortcutContext(Qt::WindowShortcut);
    addAction(m_tablesBtn);
    // Session 433: the signals' look picked from a list, not typed.
    m_tables.at(1)->setItemDelegate(new SignalLookDelegate(m_tables.at(1)));

    // Session 437: the form for what is selected, beside the drawing.
    m_form = new QWidget(this);
    m_form->setObjectName(QStringLiteral("stationLayoutForm"));
    auto *fl = new QVBoxLayout(m_form);
    fl->setContentsMargins(8, 4, 4, 4);
    m_formTitle = new QLabel(m_form);
    QFont tf = m_formTitle->font();
    tf.setBold(true);
    m_formTitle->setFont(tf);
    fl->addWidget(m_formTitle);
    auto *fields = new QWidget(m_form);
    m_formLayout = new QFormLayout(fields);
    m_formLayout->setContentsMargins(0, 0, 0, 0);
    fl->addWidget(fields);
    m_formApply = new QPushButton(tr("Apply"), m_form);
    m_formApply->setObjectName(QStringLiteral("formApply"));
    m_formApply->setToolTip(tr("Make the change (one Undo step)"));
    connect(m_formApply, &QPushButton::clicked, this, [this]() { applyForm(); });
    fl->addWidget(m_formApply);
    fl->addStretch(1);
    m_form->setMinimumWidth(240);
    m_form->setMaximumWidth(380);
    auto *across = new QSplitter(Qt::Horizontal, this);
    across->addWidget(m_scroll);
    across->addWidget(m_form);
    across->setStretchFactor(0, 1);
    across->setStretchFactor(1, 0);
    across->setCollapsible(0, false);
    m_formBtn->setCheckable(true);
    m_formBtn->setChecked(false);   // hidden at first: the drawing keeps its width
    m_form->hide();
    m_formBtn->setToolTip(tr("Show or hide the form beside the drawing (the selected tag, signal or point)"));
    connect(m_formBtn, &QAction::toggled, this, [this](bool on) { setFormShown(on); });
    auto *split = new QSplitter(Qt::Vertical, this);
    split->addWidget(across);
    split->addWidget(bottom);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    // Session 433: the tables' own minimum (the KAVACH tab's) left the drawing a
    // third of the window; a smaller one lets it have the 430 it is given.
    bottom->setMinimumHeight(140);
    split->setSizes({ 430, 230 });
    // Session 423: the KAVACH tab has its own Add / Delete buttons, on the table each
    // names; its tables want the height: the drawing gets a third while it is shown,
    // its own size back after.
    connect(m_tabs, &QTabWidget::currentChanged, this, [this, addBtn, delBtn, split, drawingSizes = QList<int>()]() mutable {
        const bool kavach = m_tabs->currentWidget() == m_kavach;
        updateKavachView();
        if (!kavach) m_kavach->setPicking(false);
        addBtn->setVisible(!kavach);
        delBtn->setVisible(!kavach);
        const QList<int> now = split->sizes();
        const int total = now.value(0) + now.value(1);
        if (kavach && now.value(1) < total * 2 / 3) {
            drawingSizes = now;
            split->setSizes({ total / 3, total - total / 3 });
        } else if (!kavach && !drawingSizes.isEmpty()) {
            split->setSizes(drawingSizes);
            drawingSizes.clear();
        }
    });

    m_status = new StatusLine;
    m_simState = new QLabel(this);   // session 411: where the simulator's loco is
    m_simState->setWordWrap(true);
    m_simState->hide();
    m_liveState = new QLabel(this);   // session 428: the station live, from Packet Maker
    m_liveState->setObjectName(QStringLiteral("slLiveState"));
    m_liveState->setWordWrap(true);
    m_liveState->hide();
    auto *root = new QVBoxLayout(this);
    root->addLayout(bar);
    root->addWidget(m_simState);
    root->addWidget(m_liveState);
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
                                                          tr("Station layout (*.xml *.json)"));
        if (!path.isEmpty()) openFile(path);
    });
    connect(importAct, &QAction::triggered, this, [this]() {
        if (!confirmDiscard()) return;
        const QString path = QFileDialog::getOpenFileName(this, tr("Import station spreadsheet"), QString(),
                                                          tr("Station spreadsheet (*.xlsx)"));
        if (!path.isEmpty()) openFile(path);
    });
    // Session 431: the station XML first (station_<id>.xml); the JSON as before.
    auto saveAs = [this]() {
        QString filter;
        QString path = QFileDialog::getSaveFileName(this, tr("Save station layout"),
                                                    m_path.isEmpty() ? suggestedXmlName() : m_path,
                                                    tr("Station XML (*.xml);;DLConsole station layout (*.json)"), &filter);
        if (path.isEmpty()) return;
        if (QFileInfo(path).suffix().isEmpty()) path += filter.contains(QLatin1String("json")) ? QStringLiteral(".json") : QStringLiteral(".xml");
        saveFile(path);
    };
    connect(saveAsBtn, &QPushButton::clicked, this, saveAs);
    connect(saveBtn, &QPushButton::clicked, this, [this, saveAs]() {
        if (m_path.isEmpty()) saveAs(); else saveFile(m_path);
    });
    connect(tableAct, &QAction::triggered, this, [this]() {
        QString path = QFileDialog::getSaveFileName(this, tr("Export the tag table"),
                                                    QFileInfo(m_path).completeBaseName() + QStringLiteral("_tags.pdf"),
                                                    tr("PDF (*.pdf);;CSV (*.csv)"));
        if (path.isEmpty()) return;
        const QString suffix = QFileInfo(path).suffix().toLower();
        if (suffix != QLatin1String("pdf") && suffix != QLatin1String("csv")) path += QStringLiteral(".pdf");
        exportTagTable(path);
    });
    connect(drawingAct, &QAction::triggered, this, [this]() {
        QString path = QFileDialog::getSaveFileName(this, tr("Export the drawing"),
                                                    QFileInfo(m_path).completeBaseName() + QStringLiteral(".pdf"),
                                                    tr("PDF (*.pdf);;PNG image (*.png)"));
        if (path.isEmpty()) return;
        const QString suffix = QFileInfo(path).suffix().toLower();
        if (suffix != QLatin1String("pdf") && suffix != QLatin1String("png")) path += QStringLiteral(".pdf");
        exportDrawing(path);
    });
    connect(xlsxAct, &QAction::triggered, this, [this]() {
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
        m_canvas->setSelectedTag(r >= 0 && r < m_layout.tags.size() ? StationLayout::tagKeys(m_layout).at(r) : QString());
        const QStringList sel = selectedTags();
        m_canvas->setMarkedTags(QSet<QString>(sel.cbegin(), sel.cend()));
        if (r >= 0 && r < m_layout.tags.size() && !m_updating && !m_selecting) emit tagSelected(m_layout.tags.at(r).name);
        if (!m_updating && r >= 0) showForm(1, r);   // session 437
    });
    // Session 437: a signal's or a point's row selected (a click on the drawing selects it): its form.
    for (int sheet : { 1, 2 })
        connect(m_tables.at(sheet), &QTableWidget::itemSelectionChanged, this, [this, sheet]() {
            const int r = m_tables.at(sheet)->currentRow();
            if (!m_updating && r >= 0) showForm(sheet == 1 ? 2 : 3, r);
        });
    connect(m_canvas, &StationLayoutCanvas::tagsBoxed, this, [this](const QStringList &names) {
        selectTags(names);
        m_status->state(tr("%n tag(s) selected: right-click for Shift, Set a field, Delete", nullptr, names.size()));
    });
    connect(m_canvas, &StationLayoutCanvas::tagToggled, this, [this](const QString &name) {
        QStringList sel = selectedTags();
        if (!sel.removeOne(name)) sel << name;
        selectTags(sel);
    });
    // Session 233: signals and points select as tags do, both ways.
    connect(m_canvas, &StationLayoutCanvas::signalClicked, this, [this](int i) { selectSheetRow(1, i); });
    connect(m_canvas, &StationLayoutCanvas::pointClicked, this, [this](int i) { selectSheetRow(2, i); });
    connect(m_tables.at(1), &QTableWidget::itemSelectionChanged, this, [this]() {
        if (!m_updating) m_canvas->setSelectedSignal(m_tables.at(1)->currentRow());
    });
    connect(m_tables.at(2), &QTableWidget::itemSelectionChanged, this, [this]() {
        if (!m_updating) m_canvas->setSelectedPoint(m_tables.at(2)->currentRow());
    });
    connect(m_measureBtn, &QPushButton::toggled, this, [this](bool on) {
        if (on) m_kavach->setPicking(false);   // session 425: one click mode at a time
        m_canvas->setMeasuring(on);
        if (on) m_status->state(tr("Measure: click the start on the drawing"));
        else { m_canvas->clearMeasure(); m_status->clear(); }
    });
    connect(m_canvas, &StationLayoutCanvas::measured, this, [this](const QString &t) { m_status->state(t); });
    connect(m_canvas, &StationLayoutCanvas::measureCancelled, this, [this]() { m_measureBtn->setChecked(false); });
    // Session 425: a tag picked for the KAVACH route: by its number, a duplicate ("…D") as its main tag.
    // Session 429: at the place of the tag clicked (of a name listed more than once, that one).
    connect(m_canvas, &StationLayoutCanvas::tagPicked, this, [this](const QString &key) {
        const QString name = StationLayout::nameOfKey(key);
        const QString suffix = key.mid(name.size());
        const QString main = isDuplicateName(name) ? name.chopped(1) : name;
        bool ok = false;
        const int id = main.toInt(&ok);
        const int at = tagIndexOf(main + suffix);
        const StationLayout::TagInfo in = at < 0 ? StationLayout::TagInfo() : StationLayout::info(m_layout.tags.at(at));
        if (!ok || !in.ok) { m_status->fail(tr("Tag %1 has no number and location to pick").arg(name)); return; }
        m_kavach->pickTag(id, in.absLoc);
    });
    connect(m_canvas, &StationLayoutCanvas::tagClicked, this, [this](const QString &key) {
        const int i = tagIndexOf(key);   // session 429: the row of the tag clicked
        if (i < 0) return;
        m_tabs->setCurrentIndex(0);
        m_tables.at(0)->selectRow(i);
        m_tables.at(0)->scrollToItem(m_tables.at(0)->item(i, 0));
    });
    auto selectedTag = [this]() {
        const int r = m_tables.at(0)->currentRow();
        return r >= 0 && r < m_layout.tags.size() ? m_layout.tags.at(r).name : QString();
    };
    connect(editTagBtn, &QPushButton::clicked, this, [this, selectedTag]() {
        if (selectedTag().isEmpty()) { m_status->warn(tr("Select a tag first (in the Tags tab or the drawing)")); return; }
        openInTagBuilder(selectedTag());
    });
    connect(newTagBtn, &QPushButton::clicked, this, [this]() { openInTagBuilder(QString()); });
    connect(m_canvas, &StationLayoutCanvas::contextRequested, this,
            [this](const QPoint &at, const QString &tag, const QString &line, qint64 loc, bool above) {
                QMenu *menu = contextMenuFor(tag, line, loc, above);
                menu->setAttribute(Qt::WA_DeleteOnClose);
                menu->popup(at);
            });
    connect(m_canvas, &StationLayoutCanvas::signalContextRequested, this, [this](const QPoint &at, int index) {
        if (QMenu *menu = signalMenuFor(index)) {
            menu->setAttribute(Qt::WA_DeleteOnClose);
            menu->popup(at);
        }
    });
    // Session 435: points thrown on the drawing.
    connect(m_canvas, &StationLayoutCanvas::pointThrowRequested, this, [this](int i) {
        if (i < 0 || i >= m_layout.points.size()) return;
        ensureLive();
        const QString name = m_layout.points.at(i).name;
        setLivePoint(name, !m_lvState.reverse.contains(kavachPointName(name)));
    });
    connect(m_canvas, &StationLayoutCanvas::circuitToggleRequested, this, [this](const QString &name) {   // session 436
        ensureLive();
        setLiveOccupied(name, !m_lvState.occupied.contains(name));
    });
    connect(m_canvas, &StationLayoutCanvas::pointContextRequested, this, [this](const QPoint &at, int index) {
        if (QMenu *menu = pointMenuFor(index)) {
            menu->setAttribute(Qt::WA_DeleteOnClose);
            menu->popup(at);
        }
    });
    connect(m_canvas, &StationLayoutCanvas::tagDoubleClicked, this, [this](const QString &name) { openInTagBuilder(name); });
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
    if (!m_lvLive && m_lvActive) clearLiveStation();   // session 435: a what-if was the other layout's
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
    QStringList found = StationLayout::checks(m_layout);
    // Session 423: what the KAVACH data would not do, and the tags it names the layout lacks.
    m_kavach->setKavach(m_layout.kavach);
    if (!m_layout.kavach.isEmpty()) {
        StationKavach::Station ks;
        QString err;
        QStringList notes;
        if (!StationKavach::fromJson(m_layout.kavach, &ks, &err, &notes)) found << tr("KAVACH: %1").arg(err);
        QSet<int> ids;
        for (const StationLayout::Tag &t : m_layout.tags)
            ids.insert((t.name.endsWith(QLatin1Char('D')) ? t.name.chopped(1) : t.name).toInt());
        // Session 424: and where the layout's tags do not lie as the routes' linking distances say.
        const QHash<int, QVector<qint64>> locs = StationLayout::tagLocationsByNumber(m_layout);
        for (const QString &c : notes + StationKavach::checks(ks) + StationKavach::tagChecks(ks, ids)
                                    + StationKavach::locationChecks(ks, locs))
            found << tr("KAVACH: %1").arg(c);
        m_tabs->setTabText(m_tabs->indexOf(m_kavach), ks.routes.size() == 1 ? tr("KAVACH (1 route)")
                                                                     : tr("KAVACH (%1 routes)").arg(ks.routes.size()));
    } else {
        m_tabs->setTabText(m_tabs->indexOf(m_kavach), tr("KAVACH"));
    }
    m_checks->clear();
    m_checks->addItems(found.isEmpty() ? QStringList{ tr("Nothing to report") } : found);
    m_tabs->setTabText(6, found.isEmpty() ? tr("Checks") : tr("Checks (%1)").arg(found.size()));
    fillCompare();
    fillRun();
    QStringList other;
    for (const XlsxBook::Sheet &s : m_layout.otherSheets)
        other << tr("%1: %2 rows").arg(s.name).arg(qMax(0, s.rows.size() - 1));
    m_other->setText(other.isEmpty() ? tr("No other sheets.")
                                     : tr("Kept as they are and written back on export (DLConsole does not use "
                                          "them; relaymap is NMS field-input data):\n") + other.join(QLatin1Char('\n')));
    m_status->state(tr("%1 tags · %2 signals · %3 lines · %4 points").arg(m_layout.tags.size())
                        .arg(m_layout.signalList.size()).arg(m_layout.lines.size()).arg(m_layout.points.size()));
    showForm(m_formKind, m_formIndex);   // session 437: the form follows edits and Undo
    emit stationChanged();
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
    const QString key = name;   // session 429: a key: "906#2" moves the second tag 906
    if (const int i = tagIndexOf(key); i >= 0) {
        const QString name = m_layout.tags.at(i).name;
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

TagBuilderWindow *StationLayoutWindow::tagBuilder() const { return m_builder; }

TagBuilderWindow *StationLayoutWindow::openInTagBuilder(const QString &target)
{
    QString name = target;
    if (!m_builder) {
        m_builder = new TagBuilderWindow(nullptr, this);
        m_builder->setWindowFlag(Qt::Window);
        m_builder->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_builder, &TagBuilderWindow::applyRequested, this,
                [this](const QByteArray &tag) { applyTag(m_builderTarget, tag); });
    }
    // A new tag starts from the selected one (same TINs and line, a location to change).
    const int r = m_tables.at(0)->currentRow();
    const int fromRow = !name.isEmpty() ? tagIndexOf(name) : r >= 0 && r < m_layout.tags.size() ? r : -1;
    if (fromRow >= 0) m_builder->setPages(m_layout.tags.at(fromRow).pageX, m_layout.tags.at(fromRow).pageY);
    m_builderTarget = name;   // a key (session 429): the one opened
    const QString key = name;
    name = StationLayout::nameOfKey(key);
    m_builderLine.clear();
    m_builder->offerApply(name.isEmpty() ? tr("Add this tag to the station layout") : tr("Apply to tag %1 in the station layout").arg(name),
                          name.isEmpty() ? tr("Adds the tag above to the layout, named by its bits")
                                         : tr("Replaces tag %1 in the layout with the tag above").arg(name));
    m_builder->setWindowTitle(name.isEmpty() ? tr("RFID Tag Builder — new tag for the station layout")
                                             : tr("RFID Tag Builder — tag %1 of the station layout").arg(name));
    m_builder->show();
    m_builder->raise();
    m_builder->activateWindow();
    return m_builder;
}

bool StationLayoutWindow::applyTag(const QString &target, const QByteArray &bytes)
{
    const QString name = RfidTag::nameOf(bytes);
    if (bytes.size() != RfidTag::TagBytes || name.isEmpty()) {
        m_status->fail(tr("Not applied: the Tag Builder's tag does not decode"));
        return false;
    }
    // Session 429: `target` a key: the one tag it names; the others of its name stay.
    const int at0 = target.isEmpty() ? -1 : tagIndexOf(target);
    const QString targetName = StationLayout::nameOfKey(target);
    const int targetK = targetName == target ? 1 : target.mid(targetName.size() + 1).toInt();
    int at = at0;
    for (int i = 0; i < m_layout.tags.size(); ++i) {
        const QString n = m_layout.tags.at(i).name;
        if (i != at0 && n == name && n != targetName) {
            m_status->fail(tr("Not applied: the layout already has a tag %1. Open that one to change it").arg(name));
            return false;
        }
    }
    if (!target.isEmpty() && at < 0) {
        m_status->fail(tr("Not applied: tag %1 is no longer in the layout").arg(target));
        return false;
    }
    const StationLayout::Layout before = m_layout;
    const StationLayout::Tag t{ name, RfidTag::pageX(bytes), RfidTag::pageY(bytes) };
    if (at < 0) {
        const int r = m_tables.at(0)->currentRow();
        at = r >= 0 && r < m_layout.tags.size() ? r + 1 : m_layout.tags.size();
        m_layout.tags.insert(at, t);
        // Session 222: a tag made at a place on a line goes into that line, in location order.
        for (StationLayout::Line &ln : m_layout.lines) {
            if (m_builderLine.isEmpty() || ln.line != m_builderLine) continue;
            const QHash<QString, qint64> loc = StationLayout::tagLocations(m_layout);
            // Lines run either way: keep the order the line already has.
            const bool down = ln.tags.size() > 1 && loc.value(ln.tags.first()) > loc.value(ln.tags.last());
            int k = 0;
            while (k < ln.tags.size() && loc.contains(ln.tags.at(k))
                   && (down ? loc.value(ln.tags.at(k)) >= loc.value(name) : loc.value(ln.tags.at(k)) <= loc.value(name))) ++k;
            ln.tags.insert(k, name);
            break;
        }
    } else {
        m_layout.tags[at] = t;
        // shortcut: a point's "tags" cell is free text and is not renamed; rename it by hand.
        if (name != targetName) {
            int others = 0;
            for (const StationLayout::Tag &o : m_layout.tags) others += o.name == targetName;
            for (StationLayout::Line &ln : m_layout.lines) {
                int seen = 0;
                for (QString &n : ln.tags)
                    if (n == targetName && (others == 0 || ++seen == targetK)) n = name;   // listed more than once: its own place
            }
            for (StationLayout::Signal &sg : m_layout.signalList)
                if (sg.footTag == targetName && others == 0) sg.footTag = name;
        }
    }
    changed(target.isEmpty() ? tr("add tag %1").arg(name) : tr("edit tag %1").arg(targetName), before);
    m_tables.at(0)->selectRow(at);
    const QString atKey = StationLayout::tagKeys(m_layout).value(at);
    m_canvas->setSelectedTag(atKey);
    if (!target.isEmpty()) m_builderTarget = atKey;   // a second Apply edits the same tag
    m_status->ok(target.isEmpty() ? tr("Tag %1 added from the Tag Builder").arg(name)
                 : name == targetName ? tr("Tag %1 updated from the Tag Builder").arg(name)
                                      : tr("Tag %1 is now %2 (its lines and signals follow)").arg(targetName, name));
    return true;
}

TagBuilderWindow *StationLayoutWindow::openNewTagAt(const QString &line, qint64 locM)
{
    // The nearest main tag of the line is the template: its TINs, type and placement.
    const QHash<QString, qint64> loc = StationLayout::tagLocations(m_layout);
    QString near;
    for (const StationLayout::Line &ln : m_layout.lines) {
        if (ln.line != line) continue;
        for (const QString &n : ln.tags)
            if (loc.contains(n) && !n.endsWith(QLatin1Char('D'))
                && (near.isEmpty() || std::llabs(loc.value(n) - locM) < std::llabs(loc.value(near) - locM)))
                near = n;
    }
    if (!near.isEmpty()) {
        for (int i = 0; i < m_layout.tags.size(); ++i)
            if (m_layout.tags.at(i).name == near) m_tables.at(0)->selectRow(i);
    }
    TagBuilderWindow *tb = openInTagBuilder(QString());
    m_builderLine = line;
    QSet<int> used;
    for (const StationLayout::Tag &t : m_layout.tags) used.insert(t.name.endsWith(QLatin1Char('D')) ? t.name.chopped(1).toInt() : t.name.toInt());
    int id = 1;
    while (used.contains(id) && id < 1023) ++id;   // unique is a 10-bit field
    tb->setField(QStringLiteral("unique"), id);
    if (!tb->setField(QStringLiteral("abs_loc"), locM)) tb->setField(QStringLiteral("abs_loc_1"), locM);
    tb->offerApply(tr("Add this tag to line %1 of the station layout").arg(line),
                   tr("Adds the tag above to the layout and to line %1, in location order").arg(line));
    m_builder->setWindowTitle(tr("RFID Tag Builder — new tag on line %1 at %2 m").arg(line).arg(locM));
    return tb;
}

bool StationLayoutWindow::addSignal(const StationLayout::Signal &sg)
{
    bool found = false;
    for (const StationLayout::Tag &t : m_layout.tags) found = found || t.name == sg.footTag;
    if (!found || sg.name.trimmed().isEmpty()) {
        m_status->fail(!found ? tr("Signal not added: tag %1 is not in the layout").arg(sg.footTag) : tr("Signal not added: give it a name"));
        return false;
    }
    const StationLayout::Layout before = m_layout;
    m_layout.signalList << sg;
    changed(tr("add signal %1").arg(sg.name), before);
    m_status->ok(tr("Signal %1 added at tag %2").arg(sg.name, sg.footTag));
    return true;
}

bool StationLayoutWindow::addLine(const StationLayout::Line &ln)
{
    bool used = false;
    for (const StationLayout::Line &o : m_layout.lines) used = used || o.line == ln.line.trimmed();
    if (ln.line.trimmed().isEmpty() || used) {
        m_status->fail(used ? tr("Line not added: the layout already has line %1").arg(ln.line) : tr("Line not added: give it a line id (DM, UL1, ...)"));
        return false;
    }
    const StationLayout::Layout before = m_layout;
    m_layout.lines << ln;
    changed(tr("add line %1").arg(ln.line), before);
    m_status->ok(tr("Line %1 added: right-click on it to put tags on it").arg(ln.line));
    return true;
}

bool StationLayoutWindow::addPoint(const StationLayout::Point &p)
{
    QSet<QString> lines;
    for (const StationLayout::Line &o : m_layout.lines) lines.insert(o.line);
    bool used = false;
    for (const StationLayout::Point &o : m_layout.points) used = used || o.name == p.name.trimmed();
    QString why;
    if (p.name.trimmed().isEmpty()) why = tr("give it a name");
    else if (used) why = tr("the layout already has point %1").arg(p.name);
    else if (!lines.contains(p.line1) || !lines.contains(p.line2)) why = tr("both lines must be lines of the layout");
    else if (p.line1 == p.line2) why = tr("a point joins two different lines");
    if (!why.isEmpty()) { m_status->fail(tr("Point not added: %1").arg(why)); return false; }
    const StationLayout::Layout before = m_layout;
    m_layout.points << p;
    changed(tr("add point %1").arg(p.name), before);
    m_status->ok(tr("Point %1 added, %2 to %3").arg(p.name, p.line1, p.line2));
    return true;
}

bool StationLayoutWindow::addText(const StationLayout::Text &t)
{
    if (t.text.trimmed().isEmpty()) { m_status->fail(tr("Text not added: it is empty")); return false; }
    const StationLayout::Layout before = m_layout;
    m_layout.texts << t;
    changed(tr("add text"), before);
    m_status->ok(tr("Text added at %1 m").arg(t.location));
    return true;
}

namespace {
// Session 437 / 438: a signal's fields in a form (the panel beside the drawing,
// the new-signal dialog), and the signal they make.
struct SignalFields {
    QLineEdit *name = nullptr, *route = nullptr;
    QComboBox *foot = nullptr, *aspects = nullptr, *indicator = nullptr;
    QSpinBox *sigId = nullptr, *station = nullptr;
    QCheckBox *callingOn = nullptr, *shunt = nullptr;
    StationLayout::Signal read(StationLayout::Signal s) const
    {
        s.name = name->text().trimmed();
        s.footTag = foot->currentText().trimmed();
        s.sigId = sigId->value();
        s.stationId = station->value();
        s.route = route->text().trimmed();
        s.aspects = aspects->currentData().toInt();
        s.callingOn = callingOn->isChecked();
        s.shunt = shunt->isChecked();
        s.indicator = indicator->currentData().toInt();
        return s;
    }
};
SignalFields addSignalFields(QFormLayout *f, const StationLayout::Signal &s, const QStringList &tags, QWidget *parent)
{
    SignalFields x;
    auto named = [](QWidget *w, const char *n) { w->setObjectName(QLatin1String(n)); return w; };
    x.name = new QLineEdit(s.name, parent);
    x.foot = new QComboBox(parent);
    x.foot->setEditable(true);
    x.foot->addItems(tags);
    x.foot->setCurrentText(s.footTag);
    x.sigId = new QSpinBox(parent);
    x.sigId->setRange(0, 999999);
    x.sigId->setValue(int(s.sigId));
    x.station = new QSpinBox(parent);
    x.station->setRange(0, 65535);
    x.station->setValue(int(s.stationId));
    x.route = new QLineEdit(s.route, parent);
    x.route->setPlaceholderText(QObject::tr("the station's (empty) or a route's name"));
    x.aspects = new QComboBox(parent);
    for (const auto &c : QVector<QPair<QString, int>>{ { QObject::tr("from its name"), -1 }, { QObject::tr("2: Red, Yellow"), 2 },
                                                        { QObject::tr("3: Red, Yellow, Green"), 3 },
                                                        { QObject::tr("4: Red, Yellow, Green, Yellow"), 4 },
                                                        { QObject::tr("none: a shunt signal"), 0 } })
        x.aspects->addItem(c.first, c.second);
    x.aspects->setCurrentIndex(qMax(0, x.aspects->findData(s.aspects)));
    x.callingOn = new QCheckBox(QObject::tr("calling-on"), parent);
    x.callingOn->setChecked(s.callingOn);
    x.shunt = new QCheckBox(QObject::tr("a shunt signal on its mast"), parent);
    x.shunt->setChecked(s.shunt);
    x.indicator = new QComboBox(parent);
    x.indicator->addItem(QObject::tr("none"), 0);
    for (int p = 1; p <= 6; ++p) x.indicator->addItem(p <= 3 ? QObject::tr("position %1 (left)").arg(p) : QObject::tr("position %1 (right)").arg(p), p);
    x.indicator->setCurrentIndex(qMax(0, x.indicator->findData(s.indicator)));
    f->addRow(QObject::tr("Name"), named(x.name, "formSignalName"));
    f->addRow(QObject::tr("Foot tag"), named(x.foot, "formSignalFoot"));
    f->addRow(QObject::tr("sig_id"), named(x.sigId, "formSignalId"));
    f->addRow(QObject::tr("Station"), named(x.station, "formSignalStation"));
    f->addRow(QObject::tr("Route"), named(x.route, "formSignalRoute"));
    f->addRow(QObject::tr("Main head"), named(x.aspects, "formSignalAspects"));
    f->addRow(QString(), named(x.callingOn, "formSignalCallingOn"));
    f->addRow(QString(), named(x.shunt, "formSignalShunt"));
    f->addRow(QObject::tr("Route indicator"), named(x.indicator, "formSignalIndicator"));
    return x;
}

// A small form: one line edit per field; false when cancelled.
bool askFields(QWidget *parent, const QString &title, const QStringList &labels, QStringList *values)
{
    QDialog d(parent);
    d.setWindowTitle(title);
    auto *form = new QFormLayout(&d);
    QVector<QLineEdit *> edits;
    for (int i = 0; i < labels.size(); ++i) {
        edits << new QLineEdit(values->value(i), &d);
        form->addRow(labels.at(i), edits.last());
    }
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    form->addRow(box);
    QObject::connect(box, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted) return false;
    values->clear();
    for (QLineEdit *e : edits) *values << e->text().trimmed();
    return true;
}
}  // namespace

// ---- session 437: the form beside the drawing --------------------------------------------------

void StationLayoutWindow::showForm(int kind, int index)
{
    if (!m_formLayout) return;
    while (m_formLayout->rowCount() > 0) m_formLayout->removeRow(0);
    m_formApplyFn = nullptr;
    const bool valid = (kind == 1 && index >= 0 && index < m_layout.tags.size())
                    || (kind == 2 && index >= 0 && index < m_layout.signalList.size())
                    || (kind == 3 && index >= 0 && index < m_layout.points.size());
    m_formKind = valid ? kind : 0;
    m_formIndex = valid ? index : -1;
    m_formApply->setEnabled(valid);
    QStringList tagNames;
    for (const StationLayout::Tag &t : m_layout.tags) if (!tagNames.contains(t.name)) tagNames << t.name;
    auto readOnlyRow = [this](const QString &label, const QString &value) {
        auto *v = new QLabel(value, m_form);
        v->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_formLayout->addRow(label, v);
    };
    if (m_formKind == 0) {
        m_formTitle->setText(tr("Nothing selected"));
        auto *hint = new QLabel(tr("Click a tag, a signal or a point on the drawing (or a row of its table): its fields here."), m_form);
        hint->setWordWrap(true);
        m_formLayout->addRow(hint);
        return;
    }
    if (m_formKind == 1) {
        const StationLayout::Tag t = m_layout.tags.at(index);
        const StationLayout::TagInfo in = StationLayout::info(t);
        const QString key = StationLayout::tagKeys(m_layout).value(index);
        m_formTitle->setText(tr("Tag %1").arg(t.name));
        auto *loc = new QLineEdit(in.ok ? QString::number(in.absLoc) : QString(), m_form);
        loc->setObjectName(QStringLiteral("formTagLocation"));
        loc->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("\\d{1,8}")), loc));
        loc->setToolTip(tr("Apply re-encodes the tag at this location (CRC-30 recomputed)"));
        m_formLayout->addRow(tr("Location (m)"), loc);
        if (in.ok) {
            readOnlyRow(tr("Type"), RfidTag::typeName(in.type));
            readOnlyRow(tr("Kind"), TinLayout::notationName(TinLayout::notation(in.type, in.placement)));
            readOnlyRow(tr("TIN nom / rev"), tr("%1 / %2").arg(in.tinNom).arg(in.tinRev));
            readOnlyRow(tr("CRC-30"), in.crcOk ? tr("good") : tr("does not match"));
        } else {
            readOnlyRow(tr("Bits"), tr("page_x / page_y do not make a tag"));
        }
        readOnlyRow(tr("Line"), StationLayout::lineOfTag(m_layout).value(t.name));
        auto *builder = new QPushButton(tr("Edit in Tag Builder…"), m_form);
        connect(builder, &QPushButton::clicked, this, [this, key]() { openInTagBuilder(key); });
        m_formLayout->addRow(builder);
        m_formApplyFn = [this, loc, key, was = in.absLoc]() {
            bool ok = false;
            const qint64 v = loc->text().toLongLong(&ok);
            if (!ok) { m_status->fail(tr("\"%1\" is not a location in metres").arg(loc->text())); return false; }
            return v == was || moveTag(key, v);
        };
        return;
    }
    if (m_formKind == 2) {
        const StationLayout::Signal sg = m_layout.signalList.at(index);
        m_formTitle->setText(tr("Signal %1").arg(sg.name));
        const SignalFields f = addSignalFields(m_formLayout, sg, tagNames, m_form);
        m_formApplyFn = [this, f, index]() {
            if (index >= m_layout.signalList.size()) return false;
            const StationLayout::Signal next = f.read(m_layout.signalList.at(index));
            if (next.name.isEmpty()) { m_status->fail(tr("A signal needs a name")); return false; }
            if (tagIndexOf(next.footTag) < 0) { m_status->fail(tr("Tag %1 is not in the layout").arg(next.footTag)); return false; }
            const StationLayout::Layout before = m_layout;
            m_layout.signalList[index] = next;
            changed(tr("edit signal %1").arg(next.name), before);
            return true;
        };
        return;
    }
    const StationLayout::Point pt = m_layout.points.at(index);
    m_formTitle->setText(tr("Point %1").arg(pt.name));
    QStringList lines;
    for (const StationLayout::Line &ln : m_layout.lines) if (!lines.contains(ln.line)) lines << ln.line;
    auto *name = new QLineEdit(pt.name, m_form);
    auto lineBox = [this, &lines](const QString &now) {
        auto *b = new QComboBox(m_form);
        b->setEditable(true);
        b->addItems(lines);
        b->setCurrentText(now);
        return b;
    };
    auto locEdit = [this](qint64 v) {
        auto *e = new QLineEdit(QString::number(v), m_form);
        e->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("\\d{1,8}")), e));
        return e;
    };
    QComboBox *l1 = lineBox(pt.line1), *l2 = lineBox(pt.line2);
    QLineEdit *loc1 = locEdit(pt.loc1), *loc2 = locEdit(pt.loc2);
    auto *tags = new QLineEdit(pt.tags, m_form);
    name->setObjectName(QStringLiteral("formPointName"));
    loc1->setObjectName(QStringLiteral("formPointLoc1"));
    loc2->setObjectName(QStringLiteral("formPointLoc2"));
    m_formLayout->addRow(tr("Name"), name);
    m_formLayout->addRow(tr("From line"), l1);
    m_formLayout->addRow(tr("at (m)"), loc1);
    m_formLayout->addRow(tr("To line"), l2);
    m_formLayout->addRow(tr("at (m)"), loc2);
    m_formLayout->addRow(tr("Its tags"), tags);
    if (m_lvActive) {   // session 435: as it lies
        const bool rev = m_lvState.reverse.contains(kavachPointName(pt.name));
        auto *thr = new QPushButton(rev ? tr("Reverse: throw to Normal") : tr("Normal: throw to Reverse"), m_form);
        connect(thr, &QPushButton::clicked, this, [this, n = pt.name, rev]() { setLivePoint(n, !rev); });
        m_formLayout->addRow(thr);
    }
    m_formApplyFn = [this, index, name, l1, l2, loc1, loc2, tags]() {
        if (index >= m_layout.points.size()) return false;
        StationLayout::Point p = m_layout.points.at(index);
        p.name = name->text().trimmed();
        p.line1 = l1->currentText().trimmed();
        p.line2 = l2->currentText().trimmed();
        p.loc1 = loc1->text().toLongLong();
        p.loc2 = loc2->text().toLongLong();
        p.tags = tags->text().trimmed();
        if (p.name.isEmpty()) { m_status->fail(tr("A point needs a name")); return false; }
        const StationLayout::Layout before = m_layout;
        m_layout.points[index] = p;
        changed(tr("edit point %1").arg(p.name), before);
        return true;
    };
}

bool StationLayoutWindow::applyForm()
{
    if (!m_formApplyFn) return false;
    const int kind = m_formKind, index = m_formIndex;
    const bool ok = m_formApplyFn();
    if (ok) showForm(kind, index);   // the values as they are now
    return ok;
}

void StationLayoutWindow::setFormShown(bool on)
{
    if (m_form) m_form->setVisible(on);
    if (m_formBtn && m_formBtn->isChecked() != on) {
        const QSignalBlocker block(m_formBtn);
        m_formBtn->setChecked(on);
    }
}

// ---- session 438: a new signal, its heads chosen at once ---------------------------------------

bool StationLayoutWindow::newSignalAt(const QString &tag)
{
    StationLayout::Signal sg;
    sg.footTag = StationLayout::nameOfKey(tag);
    sg.stationId = m_layout.stations.isEmpty() ? 0 : m_layout.stations.first().id;
    // The way it faces, said: from the foot tag's placement (1 nominal, 2 reverse).
    const int ti = tagIndexOf(tag);
    const StationLayout::TagInfo in = ti >= 0 ? StationLayout::info(m_layout.tags.at(ti)) : StationLayout::TagInfo();
    QDialog d(this);
    d.setObjectName(QStringLiteral("newSignalDialog"));
    d.setWindowTitle(tr("New signal at tag %1").arg(sg.footTag));
    auto *form = new QFormLayout(&d);
    if (in.ok)
        form->addRow(new QLabel(in.placement == 1 ? tr("Its foot tag says Signal foot, nominal: it faces rising km.")
                              : in.placement == 2 ? tr("Its foot tag says Signal foot, reverse: it faces falling km.")
                              : tr("Its foot tag is placement %1, not a signal foot tag: Checks will not say so; the tag may need it.").arg(in.placement), &d));
    QStringList tagNames;
    for (const StationLayout::Tag &t : m_layout.tags) if (!tagNames.contains(t.name)) tagNames << t.name;
    const SignalFields f = addSignalFields(form, sg, tagNames, &d);
    f.name->setFocus();
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    form->addRow(box);
    connect(box, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted) return false;
    return addSignal(f.read(sg));
}

QMenu *StationLayoutWindow::contextMenuFor(const QString &tag, const QString &line, qint64 locM, bool aboveLanes)
{
    auto *menu = new QMenu(this);
    const qint64 station = m_layout.stations.isEmpty() ? 0 : m_layout.stations.first().id;
    // Session 223: the tags selected together.
    const QStringList sel = selectedTags();
    if (sel.size() > 1) {
        menu->addAction(tr("Shift the %1 selected tags…").arg(sel.size()), this, [this, sel]() {
            bool ok = false;
            const int m = QInputDialog::getInt(this, tr("Shift %1 tags").arg(sel.size()),
                                               tr("Metres (negative: towards lower locations):"), 0, -100000, 100000, 1, &ok);
            if (ok && m != 0) shiftTags(sel, m);
        });
        menu->addAction(tr("Set a field on the %1 selected tags…").arg(sel.size()), this, [this, sel]() {
            QStringList fields;
            for (const RfidTag::Field &f : RfidTag::fieldsOf(StationLayout::info(m_layout.tags.value(tagIndexOf(sel.first()))).type))
                fields << f.name;
            bool ok = false;
            const QString field = QInputDialog::getItem(this, tr("Set a field"), tr("Field:"), fields, 0, false, &ok);
            if (!ok) return;
            const QString v = QInputDialog::getText(this, tr("Set %1").arg(field), tr("Value (a number):"), QLineEdit::Normal, QString(), &ok);
            bool num = false;
            const qint64 value = v.trimmed().toLongLong(&num, 0);
            if (ok && num) setFieldOnTags(sel, field, value);
            else if (ok) m_status->fail(tr("Not set: \"%1\" is not a number").arg(v));
        });
        menu->addAction(tr("Delete the %1 selected tags").arg(sel.size()), this, [this, sel]() { deleteTags(sel); });
        menu->addSeparator();
    }
    if (!tag.isEmpty()) {
        menu->addAction(tr("Edit tag %1 in Tag Builder…").arg(StationLayout::nameOfKey(tag)), this, [this, tag]() { openInTagBuilder(tag); });
        menu->addAction(tr("New signal at tag %1…").arg(tag), this, [this, tag]() { newSignalAt(tag); });   // session 438: with its heads
        menu->addSeparator();
    }
    if (!line.isEmpty()) {
        menu->addAction(tr("New tag here (line %1, %2 m)…").arg(line).arg(locM), this, [this, line, locM]() { openNewTagAt(line, locM); });
        menu->addAction(tr("Line %1 as a route in Tag Builder…").arg(line), this, [this, line]() { openLineInTagBuilder(line); });
        QMenu *sim = menu->addMenu(tr("Line %1 in the Loco Simulator").arg(line));   // session 411
        sim->addAction(tr("Dir 1, Nominal (locations rising)"), this, [this, line]() { runLineInSimulator(line, RfidTag::DirNominal); });
        sim->addAction(tr("Dir 2, Reverse (locations falling)"), this, [this, line]() { runLineInSimulator(line, RfidTag::DirReverse); });
        menu->addAction(tr("New point here (from line %1, %2 m)…").arg(line).arg(locM), this, [this, line, locM, station]() {
            QStringList v{ QString(), QString(), QString::number(locM), QString() };
            if (!askFields(this, tr("New point on line %1 at %2 m").arg(line).arg(locM),
                           { tr("Name:"), tr("To line:"), tr("Location on that line (m):"), tr("Its tags (comma-separated):") }, &v)) return;
            StationLayout::Point p;
            p.name = v.at(0);
            p.loc1 = locM;
            p.line1 = line;
            p.line2 = v.at(1);
            p.loc2 = v.at(2).toLongLong();
            p.tags = v.at(3);
            p.stationId = station;
            addPoint(p);
        });
    }
    menu->addAction(tr("New text here (%1 m)…").arg(locM), this, [this, locM, aboveLanes]() {
        QStringList v{ QString() };
        if (!askFields(this, tr("New text at %1 m").arg(locM), { tr("Text:") }, &v)) return;
        addText({ v.at(0), locM, aboveLanes ? -1 : 1 });
    });
    menu->addAction(tr("New line from a route file…"), this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Route for a new line"), QString(),
                                                          tr("Routes (*.xml *.json);;All files (*)"));
        if (path.isEmpty()) return;
        QString err;
        const QVector<RfidTag::Route> routes = RfidTag::readFile(path, &err);
        if (routes.isEmpty()) { m_status->fail(tr("No route in %1: %2").arg(QFileInfo(path).fileName(), err)); return; }
        int pick = 0;
        if (routes.size() > 1) {
            QStringList names;
            for (const RfidTag::Route &r : routes) names << tr("%1 (%2 tags)").arg(r.name).arg(r.tags.size());
            bool ok = false;
            const QString got = QInputDialog::getItem(this, tr("Which route"), tr("Route:"), names, 0, false, &ok);
            if (!ok) return;
            pick = names.indexOf(got);
        }
        QStringList v{ QString(), routes.at(pick).name };
        if (!askFields(this, tr("New line from route %1").arg(routes.at(pick).name),
                       { tr("Line id (DM, UM, DL1, UL2, ...):"), tr("Name:") }, &v)) return;
        addLineFromRoute(routes.at(pick), v.at(0), v.at(1));
    });
    menu->addAction(tr("New line…"), this, [this]() {
        QStringList v{ QString(), QString() };
        if (!askFields(this, tr("New line"), { tr("Line id (DM, UM, DL1, UL2, ...):"), tr("Name:") }, &v)) return;
        addLine({ v.at(1), v.at(0), {} });
    });
    return menu;
}

int StationLayoutWindow::tagIndexOf(const QString &name) const
{
    return StationLayout::indexOfKey(m_layout, name);   // session 429: "906#2" is the second tag 906
}

QStringList StationLayoutWindow::selectedTags() const
{
    QList<int> rows;
    for (const QModelIndex &i : m_tables.at(0)->selectionModel()->selectedRows()) rows << i.row();
    std::sort(rows.begin(), rows.end());
    QStringList out;
    const QStringList keys = StationLayout::tagKeys(m_layout);   // session 429: each row its own tag
    for (int r : rows) if (r < keys.size()) out << keys.at(r);
    return out;
}

void StationLayoutWindow::selectSheetRow(int sheet, int row)
{
    QTableWidget *t = m_tables.value(sheet);
    if (!t || row < 0 || row >= t->rowCount()) return;
    m_tabs->setCurrentIndex(sheet);
    t->selectRow(row);
    t->scrollToItem(t->item(row, 0));
}

void StationLayoutWindow::selectTags(const QStringList &names)
{
    QTableWidget *t = m_tables.at(0);
    m_selecting = true;   // session 417: one tagSelected, at the end, for the current one
    t->clearSelection();
    QItemSelection sel;
    for (const QString &n : names) {
        const int r = tagIndexOf(n);
        if (r >= 0) sel.select(t->model()->index(r, 0), t->model()->index(r, t->columnCount() - 1));
    }
    t->selectionModel()->select(sel, QItemSelectionModel::Select);
    if (!names.isEmpty() && tagIndexOf(names.last()) >= 0) {
        t->selectionModel()->setCurrentIndex(t->model()->index(tagIndexOf(names.last()), 0), QItemSelectionModel::NoUpdate);
        m_tabs->setCurrentIndex(0);
    }
    m_selecting = false;
    if (!names.isEmpty() && tagIndexOf(names.last()) >= 0 && !m_updating) emit tagSelected(StationLayout::nameOfKey(names.last()));
}

bool StationLayoutWindow::shiftTags(const QStringList &names, qint64 metres)
{
    StationLayout::Layout next = m_layout;
    for (const QString &n : names) {
        const int i = tagIndexOf(n);
        const StationLayout::TagInfo in = StationLayout::info(m_layout.tags.value(i));
        QString err;
        if (i < 0 || !in.ok || !StationLayout::moveTag(&next.tags[i], in.absLoc + metres, &err)) {
            m_status->fail(tr("Nothing shifted: tag %1: %2").arg(n, i < 0 || !in.ok ? tr("does not decode") : err));
            return false;
        }
    }
    const StationLayout::Layout before = m_layout;
    m_layout = next;
    changed(tr("shift %n tag(s)", nullptr, names.size()), before);
    selectTags(names);
    m_status->ok(tr("%1 tags shifted %2 m, CRC-30 recomputed").arg(names.size()).arg(metres));
    return true;
}

bool StationLayoutWindow::setFieldOnTags(const QStringList &names, const QString &field, qint64 value)
{
    StationLayout::Layout next = m_layout;
    for (const QString &n : names) {
        const int i = tagIndexOf(n);
        const QByteArray bytes = i < 0 ? QByteArray() : RfidTag::fromPages(m_layout.tags.at(i).pageX, m_layout.tags.at(i).pageY);
        QHash<QString, qint64> v = RfidTag::values(bytes);
        QString err;
        if (!v.contains(field)) err = tr("has no field %1").arg(field);
        v.insert(field, value);
        const QByteArray b = err.isEmpty() ? RfidTag::build(v, &err) : QByteArray();
        if (err.isEmpty() && RfidTag::nameOf(b) != StationLayout::nameOfKey(n)) err = tr("would become tag %1: change ids one at a time in the Tag Builder").arg(RfidTag::nameOf(b));
        if (!err.isEmpty() || b.isEmpty()) {
            m_status->fail(tr("Nothing set: tag %1 %2").arg(n, err));
            return false;
        }
        next.tags[i].pageX = RfidTag::pageX(b);
        next.tags[i].pageY = RfidTag::pageY(b);
    }
    const StationLayout::Layout before = m_layout;
    m_layout = next;
    changed(tr("set %1 on %n tag(s)", nullptr, names.size()).arg(field), before);
    selectTags(names);
    m_status->ok(tr("%1 = %2 on %3 tags, CRC-30 recomputed").arg(field).arg(value).arg(names.size()));
    return true;
}

bool StationLayoutWindow::deleteTags(const QStringList &names)
{
    if (names.isEmpty()) return false;
    const StationLayout::Layout before = m_layout;
    // Session 429: a tag of a name listed more than once takes its own place
    // in the lines with it (the k-th "906" goes with the k-th 906 of a line).
    QVector<QPair<int, QString>> gone;
    for (const QString &n : names) if (tagIndexOf(n) >= 0) gone.append({ tagIndexOf(n), n });
    std::sort(gone.begin(), gone.end(), [](const QPair<int, QString> &a, const QPair<int, QString> &b) { return a.first > b.first; });
    for (const auto &g : gone) {
        const QString name = StationLayout::nameOfKey(g.second);
        const int k = name == g.second ? 1 : g.second.mid(name.size() + 1).toInt();
        int alone = 0;
        for (const StationLayout::Tag &t : m_layout.tags) alone += t.name == name;
        m_layout.tags.remove(g.first);
        for (StationLayout::Line &ln : m_layout.lines) {
            if (alone <= 1) { ln.tags.removeAll(name); continue; }
            for (int j = 0, seen = 0; j < ln.tags.size(); ++j)
                if (ln.tags.at(j) == name && ++seen == k) { ln.tags.removeAt(j); break; }
        }
    }
    changed(tr("delete %n tag(s)", nullptr, names.size()), before);
    m_status->ok(tr("%1 tags deleted (Undo brings them back)").arg(names.size()));
    return true;
}

RfidTag::Route StationLayoutWindow::lineAsRoute(const QString &line) const
{
    RfidTag::Route r;
    const StationLayout::Line *ln = nullptr;
    for (const StationLayout::Line &o : m_layout.lines) if (o.line == line) { ln = &o; break; }
    if (!ln) return r;
    r.name = ln->name.isEmpty() ? line : ln->name;
    auto add = [&](const QString &name) {
        const int i = tagIndexOf(name);
        if (i < 0) return;
        const QByteArray b = RfidTag::fromPages(m_layout.tags.at(i).pageX, m_layout.tags.at(i).pageY);
        if (b.size() == RfidTag::TagBytes) r.tags << RfidTag::Tag{ name, b, {} };
    };
    for (const QString &n : ln->tags) {
        add(n);
        if (!n.endsWith(QLatin1Char('D')) && !ln->tags.contains(n + QLatin1Char('D'))) add(n + QLatin1Char('D'));
    }
    QSet<QString> in;
    for (const RfidTag::Tag &t : r.tags) in.insert(t.name);
    for (const StationLayout::Signal &sg : m_layout.signalList)
        if (in.contains(sg.footTag)) r.signalList << RfidTag::Signal{ sg.footTag, sg.name, QString::number(sg.sigId) };
    return r;
}

TagBuilderWindow *StationLayoutWindow::openLineInTagBuilder(const QString &line)
{
    const RfidTag::Route r = lineAsRoute(line);
    if (r.tags.isEmpty()) { m_status->fail(tr("Line %1 has no tag that decodes").arg(line)); return nullptr; }
    TagBuilderWindow *tb = openInTagBuilder(QString());
    tb->setRoute(r);
    m_builder->applyButton()->hide();   // a route goes out through the builder's own Save / Export
    m_builder->setWindowTitle(tr("RFID Tag Builder — line %1 of the station layout").arg(line));
    m_status->ok(tr("Line %1 opened as a route (%2 tags). Set its direction in the builder, then Save or Export")
                     .arg(line).arg(r.tags.size()));
    return tb;
}

// ---- session 411: a line in the Loco Simulator ------------------------------------------

RfidTag::Route StationLayoutWindow::lineAsSimRoute(const QString &line, int dir) const
{
    if (dir != RfidTag::DirNominal && dir != RfidTag::DirReverse) return RfidTag::Route();
    RfidTag::Route r = lineAsRoute(line);
    if (r.tags.isEmpty()) return r;
    // The order a loco meets them: by each tag's own location (abs_loc_1 on
    // an adjustment tag); tags at one place keep the line's order.
    const QHash<QString, qint64> loc = StationLayout::tagLocations(m_layout);
    std::stable_sort(r.tags.begin(), r.tags.end(), [&](const RfidTag::Tag &a, const RfidTag::Tag &b) {
        return dir == RfidTag::DirNominal ? loc.value(a.name) < loc.value(b.name) : loc.value(a.name) > loc.value(b.name);
    });
    r.dir = dir;
    return r;
}

LocoSimWindow *StationLayoutWindow::runLineInSimulator(const QString &line, int dir)
{
    const RfidTag::Route r = lineAsSimRoute(line, dir);
    if (r.tags.size() < 2) {
        m_status->fail(tr("Line %1 is not run: the simulator needs two tags that decode, it has %2").arg(line).arg(r.tags.size()));
        return nullptr;
    }
    LocoSimWindow *sim = m_openSim ? m_openSim() : nullptr;
    if (!sim) {
        m_status->fail(tr("No Loco Simulator to run line %1 in").arg(line));
        return nullptr;
    }
    if (!sim->followLayout(this, line, dir)) return nullptr;
    m_sim = sim;
    m_status->ok(tr("Line %1 in the Loco Simulator, dir %2: the loco at its first tag. Press Start there to send.")
                     .arg(line).arg(dir));
    return sim;
}

void StationLayoutWindow::showSimulator(const QString &line, double locM, int sign, const QHash<QString, int> &tagStates,
                                        const QString &where)
{
    const bool fresh = m_canvas->simLine() != line;
    m_canvas->setSimLoco(line, locM, sign);
    m_canvas->setSimTags(tagStates);
    // Keep the loco in view: brought into it when the run starts, followed
    // while it was in view; left alone once the sheet is moved off it.
    const QRectF r = m_canvas->simLocoRect();
    if (!r.isNull()) {
        const QRect seen(m_scroll->horizontalScrollBar()->value(), m_scroll->verticalScrollBar()->value(),
                         m_scroll->viewport()->width(), m_scroll->viewport()->height());
        if (fresh || (m_simInView && !seen.contains(r.toRect())))
            m_scroll->ensureVisible(int(r.center().x()), int(r.center().y()), 160, 90);
        m_simInView = QRect(m_scroll->horizontalScrollBar()->value(), m_scroll->verticalScrollBar()->value(),
                            m_scroll->viewport()->width(), m_scroll->viewport()->height()).contains(r.toRect());
    }
    m_simState->setText(tr("Loco Simulator, line %1, dir %2: %3").arg(line).arg(sign < 0 ? 2 : 1).arg(where));
    m_simState->show();
}

void StationLayoutWindow::clearSimulator()
{
    m_canvas->setSimLoco(QString(), 0, 1);
    m_simState->clear();
    m_simState->hide();
}

bool StationLayoutWindow::addLineFromRoute(const RfidTag::Route &r, const QString &lineId, const QString &name)
{
    QString why;
    for (const StationLayout::Line &o : m_layout.lines) if (o.line == lineId.trimmed()) why = tr("the layout already has line %1").arg(lineId);
    if (lineId.trimmed().isEmpty()) why = tr("give it a line id");
    if (r.tags.isEmpty()) why = tr("the route has no tags");
    StationLayout::Layout next = m_layout;
    StationLayout::Line ln{ name, lineId.trimmed(), {} };
    QStringList added;
    for (const RfidTag::Tag &t : r.tags) {
        if (!why.isEmpty()) break;
        const QString n = RfidTag::nameOf(t.bytes);
        if (n.isEmpty()) { why = tr("route row %1 does not decode").arg(t.name); break; }
        const StationLayout::Tag st{ n, RfidTag::pageX(t.bytes), RfidTag::pageY(t.bytes) };
        const int i = tagIndexOf(n);
        if (i >= 0 && (m_layout.tags.at(i).pageX.compare(st.pageX, Qt::CaseInsensitive) != 0
                       || m_layout.tags.at(i).pageY.compare(st.pageY, Qt::CaseInsensitive) != 0)) {
            why = tr("tag %1 differs from the layout's tag %1").arg(n);
            break;
        }
        if (i < 0 && !added.contains(n)) { next.tags << st; added << n; }
        if (!n.endsWith(QLatin1Char('D')) && !ln.tags.contains(n)) ln.tags << n;
    }
    if (!why.isEmpty()) { m_status->fail(tr("Line not made: %1").arg(why)); return false; }
    next.lines << ln;
    for (const RfidTag::Signal &sg : r.signalList) {
        bool have = false;
        for (const StationLayout::Signal &o : next.signalList) have = have || (o.footTag == sg.footTag && o.name == sg.name);
        const qint64 station = next.stations.isEmpty() ? 0 : next.stations.first().id;
        if (!have) next.signalList << StationLayout::Signal{ sg.footTag, sg.name, sg.sigId.toLongLong(), station };
    }
    const StationLayout::Layout before = m_layout;
    m_layout = next;
    changed(tr("line %1 from a route").arg(lineId), before);
    m_status->ok(tr("Line %1 made from route %2: %3 tags in it, %4 new to the layout")
                     .arg(lineId, r.name).arg(ln.tags.size()).arg(added.size()));
    return true;
}

void StationLayoutWindow::showRun(const LogModel *model, const QString &key)
{
    m_runModel = const_cast<LogModel *>(model);   // read only here; QPointer wants a non-const QObject
    m_runKey = key;
    fillRun();
    m_tabs->setCurrentWidget(m_run);
    int counts[4] = { 0, 0, 0, 0 };
    for (const RunTag &t : m_runTags) ++counts[int(t.state)];
    m_status->state(m_runTags.isEmpty() && m_runExtra.isEmpty()
                        ? tr("%1: no RFID tag read").arg(key)
                        : tr("%1 over the layout: %2 read, %3 different, %4 not read; %5 read that the layout lacks")
                              .arg(key).arg(counts[int(PlanRun::State::Read)]).arg(counts[int(PlanRun::State::Different)])
                              .arg(counts[int(PlanRun::State::NotRead)]).arg(m_runExtra.size()));
}

void StationLayoutWindow::clearRun()
{
    m_runModel = nullptr;
    m_runKey.clear();
    fillRun();
}

void StationLayoutWindow::fillRun()
{
    m_runTags.clear();
    m_runExtra.clear();
    QHash<QString, int> states;
    if (m_runModel) {
        // The plan: every tag of the layout, by location (the order does not matter here:
        // out of order counts as read).
        const QHash<QString, qint64> loc = StationLayout::tagLocations(m_layout);
        RfidTag::Route all;
        for (const StationLayout::Tag &t : m_layout.tags) {
            const QByteArray b = RfidTag::fromPages(t.pageX, t.pageY);
            if (b.size() == RfidTag::TagBytes) all.tags << RfidTag::Tag{ t.name, b, {} };
        }
        const PlanRun::Result r = PlanRun::compare(all, PlanRun::readsOf(m_runModel));
        QHash<QString, PlanRun::Planned> byName;
        for (const PlanRun::Planned &p : r.planned) byName.insert(p.name, p);
        m_runExtra = r.notPlanned;
        // Where the run went: per line, the span of the tags it read.
        const QHash<QString, QString> lineOf = StationLayout::lineOfTag(m_layout);
        auto lineFor = [&](const QString &n) {
            const QString l = lineOf.value(n);
            return l.isEmpty() && n.endsWith(QLatin1Char('D')) ? lineOf.value(n.chopped(1)) : l;
        };
        QHash<QString, QPair<qint64, qint64>> span;
        for (const PlanRun::Planned &p : r.planned) {
            if (p.state == PlanRun::State::NotRead || lineFor(p.name).isEmpty()) continue;
            auto &s = span[lineFor(p.name)];
            const qint64 m = loc.value(p.name);
            s = s.first == 0 && s.second == 0 ? qMakePair(m, m) : qMakePair(qMin(s.first, m), qMax(s.second, m));
        }
        for (const StationLayout::Line &ln : m_layout.lines) {
            if (!span.contains(ln.line)) continue;
            QStringList names;
            for (const QString &n : ln.tags) { names << n; if (!n.endsWith(QLatin1Char('D'))) names << n + QLatin1Char('D'); }
            names.removeDuplicates();
            for (const QString &n : names) {
                if (!byName.contains(n)) continue;
                const PlanRun::Planned &p = byName.value(n);
                const qint64 m = loc.value(n);
                if (p.state == PlanRun::State::NotRead && (m < span.value(ln.line).first || m > span.value(ln.line).second)) continue;
                const PlanRun::State st = p.state == PlanRun::State::OutOfOrder ? PlanRun::State::Read : p.state;
                m_runTags << RunTag{ n, ln.line, st, p.firstMs, st == PlanRun::State::Different ? p.detail : QString() };
                states.insert(n, int(st));
            }
        }
    }
    m_canvas->setRunStates(states);
    m_run->setRowCount(m_runTags.size());
    for (int i = 0; i < m_runTags.size(); ++i) {
        const RunTag &t = m_runTags.at(i);
        auto *st = readOnly(PlanRun::stateText(t.state));
        st->setForeground(t.state == PlanRun::State::NotRead ? UiColor::error() : t.state == PlanRun::State::Different ? UiColor::warning() : UiColor::ok());
        m_run->setItem(i, 0, readOnly(t.name));
        m_run->setItem(i, 1, readOnly(t.line));
        m_run->setItem(i, 2, st);
        m_run->setItem(i, 3, readOnly(t.firstMs ? QDateTime::fromMSecsSinceEpoch(t.firstMs).toString(QStringLiteral("HH:mm:ss.zzz")) : QString()));
        m_run->setItem(i, 4, readOnly(t.detail));
    }
    if (!m_runExtra.isEmpty()) {
        const int r = m_run->rowCount();
        m_run->insertRow(r);
        m_run->setItem(r, 0, readOnly(tr("Read, not in the layout: %1").arg(m_runExtra.join(QStringLiteral(", ")))));
        m_run->setSpan(r, 0, 1, 5);
    }
    m_run->resizeColumnsToContents();
    m_tabs->setTabText(m_tabs->indexOf(m_run), m_runModel ? tr("Run (%1)").arg(m_runKey) : tr("Run"));
}

int StationLayoutWindow::fixAllCrcs(QStringList *notFixed)
{
    StationLayout::Layout next = m_layout;
    QStringList fixed, skipped;
    for (StationLayout::Tag &t : next.tags) {
        const QByteArray b = RfidTag::fromPages(t.pageX, t.pageY);
        if (b.size() != RfidTag::TagBytes) { skipped << t.name; continue; }
        if (RfidTag::summary(b).crcOk) continue;
        const QByteArray f = RfidTag::fixCrc(b);
        t.pageX = RfidTag::pageX(f);
        t.pageY = RfidTag::pageY(f);
        fixed << t.name;
    }
    if (notFixed) *notFixed = skipped;
    const QString left = skipped.isEmpty() ? QString()
                         : tr("; %n tag(s) do not decode and are left as they are: ", nullptr, skipped.size()) + skipped.join(QStringLiteral(", "));
    if (fixed.isEmpty()) {
        m_status->ok(tr("Every CRC-30 already matches") + left);
        return 0;
    }
    const StationLayout::Layout before = m_layout;
    m_layout = next;
    changed(tr("fix %n CRC(s)", nullptr, fixed.size()), before);
    m_status->ok(tr("CRC-30 fixed on %n tag(s): ", nullptr, fixed.size()) + fixed.join(QStringLiteral(", ")) + left);
    return fixed.size();
}

bool StationLayoutWindow::compareWith(const QString &path)
{
    StationLayout::Layout other;
    QString err;
    if (!StationLayout::load(path, &other, &err)) {
        m_status->fail(tr("Could not read %1: %2").arg(QFileInfo(path).fileName(), err));
        return false;
    }
    compareWith(other, QFileInfo(path).fileName());
    return true;
}

void StationLayoutWindow::compareWith(const StationLayout::Layout &other, const QString &label)
{
    m_comparing = true;
    m_againstDrawing = false;
    m_compareTo = other;
    m_compareLabel = label;
    fillCompare();
    m_tabs->setCurrentWidget(m_compare);
    m_status->state(m_diff.isEmpty() ? tr("Same as %1").arg(label)
                                     : tr("%1 tag(s) and %2 other row(s) differ from %3").arg(m_diff.tags.size()).arg(m_diff.other.size()).arg(label));
}

bool StationLayoutWindow::compareWithDrawing(const QString &path)
{
    LayoutDrawing::Drawing d;
    QString err;
    if (!LayoutDrawing::load(path, &d, &err)) {
        m_status->fail(tr("Could not read %1: %2").arg(QFileInfo(path).fileName(), err));
        return false;
    }
    if (d.title.isEmpty()) d.title = QFileInfo(path).fileName();
    compareWithDrawing(d);
    return true;
}

void StationLayoutWindow::compareWithDrawing(const LayoutDrawing::Drawing &d)
{
    m_comparing = true;
    m_againstDrawing = true;
    m_drawingRef = d;
    m_compareTo = StationLayout::Layout();
    m_compareLabel = d.title;
    fillCompare();
    m_tabs->setCurrentWidget(m_compare);
    m_status->state(m_findings.isEmpty() ? tr("The layout says what the drawing %1 shows").arg(d.title)
                                         : tr("%n line(s) where the layout and the drawing %1 differ", nullptr, m_findings.size()).arg(d.title));
}

void StationLayoutWindow::clearCompare()
{
    m_comparing = false;
    m_againstDrawing = false;
    m_compareTo = StationLayout::Layout();
    m_drawingRef = LayoutDrawing::Drawing();
    fillCompare();
}

void StationLayoutWindow::fillCompare()
{
    m_diff = m_comparing && !m_againstDrawing ? StationLayout::diff(m_compareTo, m_layout) : StationLayout::Diff();
    m_findings = m_comparing && m_againstDrawing ? LayoutDrawing::check(m_drawingRef, m_layout) : QVector<LayoutDrawing::Finding>();
    m_compare->clear();
    QHash<QString, int> kinds;
    if (m_againstDrawing) {
        // Session 430: what the drawing shows that the layout's tags do not say.
        m_compare->addItem(tr("Against the drawing %1 (%2):").arg(m_drawingRef.title, m_drawingRef.source));
        if (m_findings.isEmpty()) m_compare->addItem(tr("The layout says what the drawing shows"));
        for (const LayoutDrawing::Finding &f : m_findings) {
            auto *it = new QListWidgetItem(f.text, m_compare);
            it->setData(Qt::UserRole, f.key);
            using F = LayoutDrawing::Finding;
            it->setForeground(f.kind == F::NotInLayout ? UiColor::error() : f.kind == F::NotOnDrawing ? UiColor::ok() : UiColor::warning());
            if (f.key.isEmpty()) continue;
            kinds.insert(f.key, f.kind == F::NotOnDrawing ? StationLayout::TagChange::Added
                              : f.kind == F::Moved && !kinds.contains(f.key) ? StationLayout::TagChange::Moved
                                                                              : StationLayout::TagChange::Changed);
        }
        m_canvas->setDiffTags(kinds, true);
        m_tabs->setTabText(m_tabs->indexOf(m_compare), tr("Compare (%1)").arg(m_findings.size()));
        return;
    }
    if (!m_comparing) {
        m_compare->addItem(tr("Not comparing. Open ▸ Compare with: a file, the default or a library layout, or a tag-layout drawing"));
    } else if (m_diff.isEmpty()) {
        m_compare->addItem(tr("Same as %1").arg(m_compareLabel));
    } else {
        m_compare->addItem(tr("From %1 to this layout:").arg(m_compareLabel));
        const char *kindText[] = { QT_TR_NOOP("added"), QT_TR_NOOP("removed"), QT_TR_NOOP("moved"), QT_TR_NOOP("changed") };
        for (const StationLayout::TagChange &c : m_diff.tags) {
            QString t = tr("tag %1 %2").arg(c.name, tr(kindText[c.kind]));
            if (c.kind == StationLayout::TagChange::Added) t += tr(" (at %1 m)").arg(c.toLoc);
            if (c.kind == StationLayout::TagChange::Removed) t += tr(" (was at %1 m)").arg(c.fromLoc);
            if (c.kind == StationLayout::TagChange::Moved) t += tr(": %1 m \u2192 %2 m (%3%4 m)").arg(c.fromLoc).arg(c.toLoc)
                                                                   .arg(c.toLoc >= c.fromLoc ? QStringLiteral("+") : QString()).arg(c.toLoc - c.fromLoc);
            if (c.kind == StationLayout::TagChange::Changed) t += QStringLiteral(": ") + c.fields.join(QStringLiteral(", "));
            auto *it = new QListWidgetItem(t, m_compare);
            it->setData(Qt::UserRole, c.name);
            it->setForeground(c.kind == StationLayout::TagChange::Added ? UiColor::ok() : c.kind == StationLayout::TagChange::Removed ? UiColor::error() : UiColor::warning());
            if (c.kind != StationLayout::TagChange::Removed) kinds.insert(c.name, c.kind);
        }
        m_compare->addItems(m_diff.other);
    }
    m_canvas->setDiffTags(kinds);
    m_tabs->setTabText(m_tabs->indexOf(m_compare), !m_comparing ? tr("Compare")
                                                  : tr("Compare (%1)").arg(m_diff.tags.size() + m_diff.other.size()));
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
        ? tr("The built-in default layout (station.xlsx): Save as… a station XML to keep your changes")
        : path.startsWith(QStringLiteral(":/"))
        ? tr("Built-in layout %1: Save as… a station XML to keep your changes").arg(QFileInfo(path).fileName())
        : imported ? tr("Imported %1: Save as… a station XML to keep editing it").arg(QFileInfo(path).fileName())
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

QMenu *StationLayoutWindow::signalMenuFor(int index)
{
    if (index < 0 || index >= m_layout.signalList.size()) return nullptr;
    const StationLayout::Signal sg = m_layout.signalList.at(index);
    const StationLayout::SignalLook now = StationLayout::lookOf(sg);
    auto *menu = new QMenu(this);
    menu->addSection(tr("Signal %1").arg(sg.name));
    QMenu *heads = menu->addMenu(tr("Main head"));
    const QVector<QPair<QString, int>> kinds{ { tr("2 aspects: Red, Yellow"), 2 }, { tr("3 aspects: Red, Yellow, Green"), 3 },
                                               { tr("4 aspects: Red, Yellow, Green, Yellow"), 4 },
                                               { tr("None: a shunt signal"), 0 }, { tr("From its name"), -1 } };
    auto *group = new QActionGroup(heads);
    for (const auto &k : kinds) {
        QAction *a = heads->addAction(k.first);
        a->setCheckable(true);
        a->setChecked(sg.aspects == k.second);
        group->addAction(a);
        const int n = k.second;
        connect(a, &QAction::triggered, this, [this, index, n]() {
            const StationLayout::Signal s = m_layout.signalList.value(index);
            setSignalLook(index, n, s.callingOn, n == 0 ? false : s.shunt);
        });
    }
    QAction *callOn = menu->addAction(tr("Calling-on"));
    callOn->setCheckable(true);
    callOn->setChecked(sg.callingOn);
    connect(callOn, &QAction::toggled, this, [this, index](bool on) {
        const StationLayout::Signal s = m_layout.signalList.value(index);
        setSignalLook(index, s.aspects, on, s.shunt);
    });
    QAction *shunt = menu->addAction(tr("A shunt signal on its mast"));
    shunt->setCheckable(true);
    shunt->setChecked(now.shunt);
    shunt->setEnabled(now.aspects > 0);
    connect(shunt, &QAction::toggled, this, [this, index](bool on) {
        const StationLayout::Signal s = m_layout.signalList.value(index);
        setSignalLook(index, s.aspects, s.callingOn, on);
    });
    // Session 435: its route indicator, and the station's state: aspect, routes from it.
    QMenu *ind = menu->addMenu(tr("Route indicator"));
    for (int pos = 0; pos <= 6; ++pos) {
        QAction *a = ind->addAction(pos == 0 ? tr("None") : pos <= 3 ? tr("Position %1 (left)").arg(pos) : tr("Position %1 (right)").arg(pos));
        a->setCheckable(true);
        a->setChecked(sg.indicator == pos);
        connect(a, &QAction::triggered, this, [this, index, pos]() { setSignalIndicator(index, pos); });
    }
    ensureLive();
    const int shows = m_lvState.aspectOf(m_lvStation, sg.name);
    menu->addSection(m_lvLive ? tr("Station (live, %1)").arg(m_lvFrom) : tr("Station (what-if)"));
    QMenu *asp = menu->addMenu(tr("Aspect: %1").arg(StationKavach::aspectName(shows)));
    auto aspect = [this, asp, shows, name = sg.name](QMenu *in, const QString &label, int a) {
        QAction *x = in->addAction(label);
        x->setCheckable(true);
        x->setChecked(shows == a);
        connect(x, &QAction::triggered, this, [this, name, a]() { setLiveAspect(name, a); });
        Q_UNUSED(asp)
    };
    aspect(asp, tr("Red"), 1);
    aspect(asp, tr("Yellow"), 2);
    aspect(asp, tr("Double Yellow"), 10);
    aspect(asp, tr("Green"), 11);
    aspect(asp, tr("Red with calling-on OFF"), 15);
    QMenu *withInd = asp->addMenu(tr("With route indicator"));
    for (int pos = 1; pos <= 6; ++pos)
        aspect(withInd, tr("Yellow + position %1%2").arg(pos).arg(pos == sg.indicator ? tr(" (its indicator)") : QString()), 2 + pos);
    aspect(withInd, tr("Double Yellow + position 1"), 12);
    aspect(withInd, tr("Double Yellow + position 4"), 13);
    QMenu *routes = menu->addMenu(tr("Set route"));
    for (int i = 0; i < m_lvStation.routes.size(); ++i) {
        const StationKavach::Route &r = m_lvStation.routes.at(i);
        if (r.entry != sg.name) continue;
        QString label = r.name;
        if (!r.pointsReverse.isEmpty()) label += tr("  (points %1 Reverse)").arg(r.pointsReverse.join(QStringLiteral(", ")));
        QAction *x = routes->addAction(label);
        x->setCheckable(true);
        x->setChecked(m_lvState.routes.value(sg.name, -1) == i);
        connect(x, &QAction::triggered, this, [this, i]() { setLiveRoute(i); });
    }
    if (routes->isEmpty()) routes->addAction(tr("(no route from %1 in the KAVACH data)").arg(sg.name))->setEnabled(false);
    if (m_lvState.routes.contains(sg.name))
        menu->addAction(tr("Cancel route (%1 to Red)").arg(sg.name), this, [this, name = sg.name]() { cancelLiveRoute(name); });
    menu->addSeparator();
    menu->addAction(tr("Show its row"), this, [this, index]() {
        setTablesShown(true);
        selectSheetRow(1, index);
    });
    return menu;
}

bool StationLayoutWindow::setSignalLook(int index, int aspects, bool callingOn, bool shunt)
{
    if (index < 0 || index >= m_layout.signalList.size() || !(aspects == -1 || aspects == 0 || (aspects >= 2 && aspects <= 4))) return false;
    const StationLayout::Signal &was = m_layout.signalList.at(index);
    if (was.aspects == aspects && was.callingOn == callingOn && was.shunt == shunt) return true;
    // The copy first: a reference taken before it would write into the copy too (shared data).
    const StationLayout::Layout before = m_layout;
    StationLayout::Signal &s = m_layout.signalList[index];
    s.aspects = aspects;
    s.callingOn = callingOn;
    s.shunt = shunt;
    const QString name = s.name;
    changed(tr("signal %1's heads").arg(name), before);
    m_canvas->setSelectedSignal(index);
    return true;
}

void StationLayoutWindow::setTablesShown(bool on)
{
    if (m_bottom) m_bottom->setVisible(on);
    if (m_tablesBtn && m_tablesBtn->isChecked() != on) {
        const QSignalBlocker block(m_tablesBtn);
        m_tablesBtn->setChecked(on);
    }
}

bool StationLayoutWindow::tablesShown() const { return m_bottom && !m_bottom->isHidden(); }

QString StationLayoutWindow::suggestedXmlName() const
{
    return m_layout.stations.isEmpty() ? QStringLiteral("station.xml")
                                       : QStringLiteral("station_%1.xml").arg(m_layout.stations.first().id);
}

bool StationLayoutWindow::exportXlsx(const QString &path)
{
    QString err;
    if (!XlsxBook::writeFile(path, StationLayout::toToolSheets(m_layout), &err)) { m_status->fail(err); return false; }
    m_status->ok(tr("Exported %1").arg(path));
    return true;
}

QVector<StationLayoutWindow::TagRow> StationLayoutWindow::tagTable() const
{
    const QHash<QString, QString> lineOf = StationLayout::lineOfTag(m_layout);
    QHash<QString, int> lineRank;
    for (const StationLayout::Line &l : m_layout.lines) if (!lineRank.contains(l.line)) lineRank.insert(l.line, lineRank.size());
    QHash<QString, QStringList> sigs;
    for (const StationLayout::Signal &sg : m_layout.signalList) sigs[sg.footTag] << sg.name;
    QHash<QString, QString> pointOf;
    for (const StationLayout::Point &p : m_layout.points)
        for (QString n : p.tags.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            n = n.trimmed();
            if (n.endsWith(QLatin1String(".0"))) n.chop(2);
            if (!pointOf.contains(n)) pointOf.insert(n, p.name);
        }
    QVector<TagRow> rows;
    for (const StationLayout::Tag &t : m_layout.tags) {
        TagRow r;
        r.name = t.name;
        const bool dup = t.name.endsWith(QLatin1Char('D'));
        r.mainTag = dup ? t.name.chopped(1) : QString();
        r.line = lineOf.value(t.name, dup ? lineOf.value(r.mainTag) : QString());
        const StationLayout::TagInfo in = StationLayout::info(t);
        r.decodes = in.ok;
        if (in.ok) {
            r.locM = in.absLoc;
            r.tinNom = in.tinNom;
            r.tinRev = in.tinRev;
            r.kind = TinLayout::notationName(TinLayout::notation(in.type, in.placement));
            r.crc = in.crcOk ? tr("matches") : tr("does not match");
        } else {
            r.kind = tr("does not decode");
        }
        r.signalNames = sigs.value(t.name).join(QStringLiteral(", "));
        r.point = pointOf.value(t.name, pointOf.value(r.mainTag));
        rows << r;
    }
    std::stable_sort(rows.begin(), rows.end(), [&](const TagRow &a, const TagRow &b) {
        const int ra = lineRank.value(a.line, INT_MAX), rb = lineRank.value(b.line, INT_MAX);
        if (ra != rb) return ra < rb;
        return a.locM != b.locM ? a.locM < b.locM : a.name < b.name;
    });
    return rows;
}

bool StationLayoutWindow::exportTagTable(const QString &path)
{
    const QVector<TagRow> rows = tagTable();
    const QStringList heads{ tr("Tag"), tr("Line"), tr("Location (km)"), tr("Kind"), tr("TIN nom"), tr("TIN rev"),
                             tr("Duplicate of"), tr("Signals"), tr("Point"), tr("CRC-30") };
    auto cells = [](const TagRow &r) {
        return QStringList{ r.name, r.line, r.decodes ? QString::number(r.locM / 1000.0, 'f', 3) : QString(), r.kind,
                            r.decodes ? QString::number(r.tinNom) : QString(), r.decodes ? QString::number(r.tinRev) : QString(),
                            r.mainTag, r.signalNames, r.point, r.crc };
    };
    bool ok = false;
    if (QFileInfo(path).suffix().compare(QLatin1String("csv"), Qt::CaseInsensitive) == 0) {
        auto csv = [](QString v) {
            return v.contains(QLatin1Char(',')) || v.contains(QLatin1Char('"')) ? QLatin1Char('"') + v.replace(QLatin1Char('"'), QStringLiteral("\"\"")) + QLatin1Char('"') : v;
        };
        QStringList lines{ heads.join(QLatin1Char(',')) };
        for (const TagRow &r : rows) {
            QStringList c = cells(r);
            for (QString &v : c) v = csv(v);
            lines << c.join(QLatin1Char(','));
        }
        QSaveFile f(path);
        ok = f.open(QIODevice::WriteOnly) && f.write((lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8()) > 0 && f.commit();
    } else {
        QStringList stations;
        for (const StationLayout::Station &st : m_layout.stations) stations << tr("%1 at %2 km").arg(st.id).arg(st.location / 1000.0, 0, 'f', 3);
        QString h = QStringLiteral("<html><body><h3>%1</h3><p>%2</p><table border=\"1\" cellspacing=\"0\" cellpadding=\"3\"><tr>")
                        .arg(tr("RFID tags — %1").arg((m_path.isEmpty() ? tr("untitled") : QFileInfo(m_path).fileName()).toHtmlEscaped()),
                             tr("Station %1 · %2 tags · %3").arg(stations.isEmpty() ? tr("not given") : stations.join(QStringLiteral(", ")))
                                 .arg(rows.size()).arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))).toHtmlEscaped());
        for (const QString &c : heads) h += QStringLiteral("<th>%1</th>").arg(c.toHtmlEscaped());
        h += QStringLiteral("</tr>");
        for (const TagRow &r : rows) {
            h += QStringLiteral("<tr>");
            for (const QString &c : cells(r)) h += QStringLiteral("<td>%1</td>").arg(c.toHtmlEscaped());
            h += QStringLiteral("</tr>");
        }
        h += QStringLiteral("</table></body></html>");
        QTextDocument doc;
        doc.setHtml(h);
        QPdfWriter pdf(path);
        pdf.setCreator(QStringLiteral("DLConsole"));
        pdf.setTitle(tr("RFID tag table"));
        pdf.setPageSize(QPageSize(QPageSize::A4));
        pdf.setPageMargins(QMarginsF(12, 12, 12, 12), QPageLayout::Millimeter);
        doc.setPageSize(QSizeF(pdf.width(), pdf.height()));
        doc.print(&pdf);
        ok = QFileInfo(path).size() > 0;
    }
    if (!ok) { m_status->fail(tr("Could not write %1").arg(path)); return false; }
    m_status->ok(tr("Tag table (%1 tags) written to %2").arg(rows.size()).arg(QDir::toNativeSeparators(path)));
    return true;
}

bool StationLayoutWindow::exportDrawing(const QString &path)
{
    // Session 439: the sheet on paper, black on white as the SIP / tag-TIN
    // layouts are, whatever the theme (Light for the moment of the export).
    const int theme = UiColor::activeTheme();
    const bool switched = theme >= 0 && theme != int(Theme::Light);
    if (switched) { ThemeUtil::apply(Theme::Light); UiStyle::apply(); }
    struct Back { bool on; int t; ~Back() { if (on) { ThemeUtil::apply(Theme(t)); UiStyle::apply(); } } } back{ switched, theme };
    // A canvas of its own, wide enough for a sheet.
    StationLayoutCanvas c;
    c.setFont(m_canvas->font());
    c.setStation(m_layout);
    c.setKavachView(m_canvas->kavachView());   // session 424: the KAVACH route as shown
    c.setLiveView(m_canvas->liveView());       // session 428: and the station live
    c.resize(qMax(2400, m_canvas->width()), 100);
    c.resize(c.width(), c.minimumSizeHint().height());
    bool ok = false;
    if (QFileInfo(path).suffix().compare(QLatin1String("png"), Qt::CaseInsensitive) == 0) {
        QImage img(c.size() * 2, QImage::Format_ARGB32_Premultiplied);
        img.setDevicePixelRatio(2);
        c.render(&img);
        ok = img.save(path);
    } else {
        QPdfWriter pdf(path);
        pdf.setCreator(QStringLiteral("DLConsole"));
        pdf.setTitle(tr("RFID Tag / TIN layout"));
        pdf.setPageSize(QPageSize(QPageSize::A3));
        pdf.setPageOrientation(QPageLayout::Landscape);
        pdf.setPageMargins(QMarginsF(10, 10, 10, 10), QPageLayout::Millimeter);
        QPainter painter;
        if (painter.begin(&pdf)) {
            // Session 439: a title block under the drawing, as on the SIP sheets.
            const double mm = pdf.resolution() / 25.4;
            const QRectF block(pdf.width() - 190 * mm, pdf.height() - 34 * mm, 190 * mm, 34 * mm);
            const double room = block.top() - 3 * mm;
            const double s = qMin(pdf.width() / double(c.width()), room / double(c.height()));
            painter.save();
            painter.scale(s, s);
            c.render(&painter);
            painter.restore();
            drawTitleBlock(painter, block, mm);
            ok = painter.end();
        }
        ok = ok && QFileInfo(path).size() > 0;
    }
    if (!ok) { m_status->fail(tr("Could not write %1").arg(path)); return false; }
    m_status->ok(tr("Exported the drawing to %1").arg(path));
    return true;
}

void StationLayoutWindow::drawTitleBlock(QPainter &p, const QRectF &r, double mm) const
{
    QStringList ids, kms;
    for (const StationLayout::Station &s : m_layout.stations) {
        ids << QString::number(s.id);
        kms << km(s.location);
    }
    const QString file = m_path.isEmpty() ? tr("(not saved)") : QFileInfo(m_path).fileName();
    p.save();
    p.setPen(QPen(Qt::black, 0.35 * mm));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r);
    // Rows: the title; station and km; file and date; the signatories' boxes.
    const double rowH = r.height() / 4.0;
    for (int i = 1; i < 4; ++i) p.drawLine(QPointF(r.left(), r.top() + i * rowH), QPointF(r.right(), r.top() + i * rowH));
    const double mid = r.left() + r.width() / 2.0;
    for (int i = 1; i < 4; ++i) p.drawLine(QPointF(mid, r.top() + i * rowH), QPointF(mid, r.top() + (i + 1) * rowH));
    QFont f = p.font();
    f.setPixelSize(int(3.2 * mm));
    f.setBold(true);
    p.setFont(f);
    auto cell = [&](double x, double y, double w, const QString &t) {
        p.drawText(QRectF(x + 2 * mm, y, w - 4 * mm, rowH), Qt::AlignVCenter | Qt::AlignLeft, t);
    };
    cell(r.left(), r.top(), r.width(), tr("KAVACH — RFID TAG / TIN LAYOUT%1").arg(m_layout.stations.size() == 1 ? tr(", STATION %1").arg(ids.first()) : QString()));
    f.setBold(false);
    f.setPixelSize(int(2.6 * mm));
    p.setFont(f);
    cell(r.left(), r.top() + rowH, r.width() / 2.0, tr("Station ID: %1").arg(ids.isEmpty() ? tr("—") : ids.join(QStringLiteral(", "))));
    cell(mid, r.top() + rowH, r.width() / 2.0, tr("Centre km: %1").arg(kms.isEmpty() ? tr("—") : kms.join(QStringLiteral(", "))));
    cell(r.left(), r.top() + 2 * rowH, r.width() / 2.0, tr("File: %1").arg(file));
    cell(mid, r.top() + 2 * rowH, r.width() / 2.0, tr("Drawn by DLConsole, %1").arg(QDate::currentDate().toString(QStringLiteral("dd.MM.yyyy"))));
    cell(r.left(), r.top() + 3 * rowH, r.width() / 2.0, tr("Checked:"));
    cell(mid, r.top() + 3 * rowH, r.width() / 2.0, tr("Approved:"));
    p.restore();
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
