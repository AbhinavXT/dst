#include "cabiosimwindow.h"

#include "serialmanager.h"
#include "seriallink.h"
#include "settings.h"
#include "pressuregauge.h"
#include "statusline.h"
#include "udpsender.h"
#include "theme.h"
#include "uicolors.h"
#include "windowgeometry.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGuiApplication>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadialGradient>
#include <QScrollArea>
#include <QScrollBar>
#include <QSerialPortInfo>
#include <QSet>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTime>
#include <QTimer>
#include <QToolButton>
#include <QValidator>
#include <QVBoxLayout>

using namespace CabIo;

namespace {

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

QPoint eventPos(const QMouseEvent *e)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return e->position().toPoint();
#else
    return e->pos();
#endif
}

QFont scaled(const QFont &base, double factor, int weight = -1)
{
    QFont f = base;
    if (f.pointSizeF() > 0) f.setPointSizeF(f.pointSizeF() * factor);
    if (weight >= 0) f.setWeight(QFont::Weight(weight));
    return f;
}

QString hexN(quint32 w, int digits)
{
    return QStringLiteral("0x") + QString::number(w, 16).toUpper().rightJustified(digits, QLatin1Char('0'));
}
QString hex32(quint32 w) { return hexN(w, 8); }
QString hex16(quint16 w) { return hexN(w, 4); }
QString hex8(quint8 w)   { return hexN(w, 2); }

// "1 reply", "3 replies"
QString count(int n, const char *one, const char *many)
{
    return QStringLiteral("%1 %2").arg(n).arg(QLatin1String(n == 1 ? one : many));
}

QString ago(qint64 atMs, qint64 now)
{
    if (atMs <= 0) return QStringLiteral("never");
    const double s = (now - atMs) / 1000.0;
    if (s < 60) return QStringLiteral("%1 s ago").arg(s, 0, 'f', 1);
    return QStringLiteral("%1 min ago").arg(int(s / 60));
}

QString idRange(const QVector<quint8> &ids)
{
    QStringList s;
    for (quint8 id : ids) s << QString::number(id);
    return s.isEmpty() ? QStringLiteral("none") : s.join(QStringLiteral(", "));
}

// vic_health and the like: "0x03", not "0x3".
class HexSpin : public QSpinBox
{
public:
    using QSpinBox::QSpinBox;
protected:
    QString textFromValue(int v) const override
    {
        return QString::number(v, 16).toUpper().rightJustified(2, QLatin1Char('0'));
    }
    int valueFromText(const QString &text) const override { return text.toInt(nullptr, 16); }
    QValidator::State validate(QString &text, int &) const override
    {
        QString t = text;
        if (t.startsWith(prefix())) t = t.mid(prefix().size());
        bool ok = false;
        const int v = t.toInt(&ok, 16);
        if (t.isEmpty()) return QValidator::Intermediate;
        return ok && v >= minimum() && v <= maximum() ? QValidator::Acceptable : QValidator::Invalid;
    }
};

// A row of buttons that read as one control: [ A | B | C ].
QWidget *segmented(const QStringList &labels, QButtonGroup **group, QWidget *parent)
{
    auto *w = new QWidget(parent);
    w->setObjectName(QStringLiteral("segmented"));
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    auto *g = new QButtonGroup(w);
    g->setExclusive(true);
    for (int i = 0; i < labels.size(); ++i) {
        auto *b = new QPushButton(labels.at(i), w);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setProperty("seg", i == 0 ? "first" : (i == labels.size() - 1 ? "last" : "mid"));
        g->addButton(b, i);
        h->addWidget(b);
    }
    g->button(0)->setChecked(true);
    *group = g;
    return w;
}

QColor blend(const QColor &a, const QColor &b, double t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

}  // namespace

// =============================================================================
//  Colours
// =============================================================================
namespace CabIoColors {

static bool highContrast() { return UiColor::activeTheme() == int(Theme::HighContrast); }

double textFloor()    { return highContrast() ? 7.0 : 4.5; }
double graphicFloor() { return highContrast() ? 7.0 : 3.0; }

QColor tileFill(Tile t)
{
    const QColor base = QGuiApplication::palette().color(QPalette::Base);
    switch (t) {
    case Tile::On:    return blend(base, UiColor::accent(), 0.08);
    case Tile::Fault: return blend(base, UiColor::warning(), 0.10);
    case Tile::Plain: break;
    }
    return base;
}

QColor tileInk(const QColor &ink, Tile t)
{
    return UiColor::withContrast(ink, tileFill(t), textFloor());
}

QColor lampRim(const QString &lamp)
{
    const QColor base = QGuiApplication::palette().color(QPalette::Base);
    return UiColor::withContrast(UiColor::signalLamp(lamp).darker(140), base, graphicFloor());
}

}  // namespace CabIoColors

using CabIoColors::Tile;

// =============================================================================
//  The painted pieces
// =============================================================================
namespace CabIoUi {

// Headings and tiles laid out in as many columns as fit, re-flowed on resize.
class TileGrid : public QWidget
{
public:
    explicit TileGrid(int minTileWidth, QWidget *parent = nullptr)
        : QWidget(parent), m_minTile(minTileWidth)
    {
        setMinimumWidth(minTileWidth);
    }
    void addHeading(const QString &text)
    {
        auto *l = new QLabel(text.toUpper(), this);
        l->setObjectName(QStringLiteral("groupHeading"));
        m_items.append({ l, true });
        relayout();
    }
    void addTile(QWidget *w)
    {
        w->setParent(this);
        m_items.append({ w, false });
        relayout();
    }
    void clear()
    {
        for (const Item &i : m_items) delete i.w;
        m_items.clear();
        relayout();
    }

protected:
    void resizeEvent(QResizeEvent *) override { relayout(); }

private:
    struct Item { QWidget *w; bool heading; };

    void relayout()
    {
        const int gap = 8;
        const int width = qMax(m_minTile, this->width());
        const int cols = qMax(1, (width + gap) / (m_minTile + gap));
        const int tileW = (width - (cols - 1) * gap) / cols;
        int x = 0, col = 0, y = 0, rowH = 0;
        for (const Item &i : m_items) {
            if (i.heading) {
                if (col) { y += rowH + gap; col = 0; rowH = 0; }
                const int h = i.w->sizeHint().height();
                y += (y ? 10 : 0);
                i.w->setGeometry(2, y, width - 2, h);
                y += h + 6;
                continue;
            }
            x = col * (tileW + gap);
            const int h = i.w->sizeHint().height();
            i.w->setGeometry(x, y, tileW, h);
            i.w->show();
            rowH = qMax(rowH, h);
            if (++col == cols) { col = 0; y += rowH + gap; rowH = 0; }
        }
        if (col) y += rowH;
        setMinimumHeight(y + 4);
    }

    int m_minTile;
    QVector<Item> m_items;
};

// One input: an Off/On switch and a flicker button. An input with NO and NC
// feedbacks switches the same way -- Off is the contact at rest (NO 0, NC 1),
// On is operated (NO 1, NC 0) -- and its two abnormal states (both 0, both 1)
// are a fault set from the warning button, not two more positions to read.
class InputTile : public QWidget
{
public:
    InputTile(const Input &in, QWidget *parent = nullptr) : QWidget(parent), m_in(in)
    {
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        setAccessibleName(displayName(in.name));
        QString tip = in.isPair()
            ? QStringLiteral("%1_no (bit %2) and %1_nc (bit %3)\n"
                             "Off: NO 0, NC 1 (at rest).  On: NO 1, NC 0 (operated).\n"
                             "Click or press Space to switch. The warning button (or E) sets a fault: "
                             "both 0, or both 1.").arg(in.name).arg(in.noBit).arg(in.ncBit)
            : QStringLiteral("%1 (bit %2)\nClick or press Space to switch.").arg(in.name).arg(in.bit);
        tip += QStringLiteral("\nF or the wave button: flicker.");
        setToolTip(tip);
    }
    QSize sizeHint() const override { return QSize(340, 60); }

    void setValues(int live, bool flicker)
    {
        if (live == m_live && flicker == m_flicker) return;
        m_live = live;
        m_flicker = flicker;
        setAccessibleDescription(stateWords());
        update();
    }

    std::function<void()>    onToggle;
    std::function<void(int)> onSetState;
    std::function<void()>    onFlicker;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPalette pal = palette();
        const QColor accent = UiColor::accent(), base = pal.color(QPalette::Base);
        const bool fault = isFault();
        const bool on = isOn();

        // The card: tinted when on, outlined in the warning colour on a fault.
        QRectF card = QRectF(rect()).adjusted(1, 1, -1, -1);
        QColor border = UiColor::frame();
        if (fault) border = UiColor::warning();
        if (hasFocus()) border = accent;
        p.setPen(QPen(border, (hasFocus() || fault) ? 2 : 1));
        const Tile tile = fault ? Tile::Fault : (on ? Tile::On : Tile::Plain);
        p.setBrush(CabIoColors::tileFill(tile));
        p.drawRoundedRect(card, 9, 9);
        if (m_flicker) {
            p.setPen(Qt::NoPen);
            p.setBrush(accent);
            p.drawRoundedRect(QRectF(1, 10, 4, height() - 20), 2, 2);
        }

        layoutRects();

        // The name, then the state in words with its bits.
        const int textLeft = 16;
        const int textRight = (m_in.isPair() ? m_faultRect.left() : m_control.left()) - 10;
        p.setPen(CabIoColors::tileInk(pal.color(QPalette::Text), tile));
        p.setFont(scaled(font(), 1.0, QFont::DemiBold));
        const QString name = p.fontMetrics().elidedText(displayName(m_in.name), Qt::ElideRight, textRight - textLeft);
        p.drawText(QRect(textLeft, 10, textRight - textLeft, 22), Qt::AlignLeft | Qt::AlignVCenter, name);
        p.setFont(scaled(font(), 0.85, fault ? int(QFont::DemiBold) : -1));
        p.setPen(CabIoColors::tileInk(fault ? UiColor::warning() : UiColor::muted(), tile));
        p.drawText(QRect(textLeft, 32, textRight - textLeft, 18), Qt::AlignLeft | Qt::AlignVCenter,
                   p.fontMetrics().elidedText(stateWords(), Qt::ElideRight, textRight - textLeft));

        paintSwitch(p, on, fault, base, accent);
        if (m_in.isPair()) paintFaultButton(p, fault, base, tile);
        paintFlicker(p, base, accent, tile);
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton) return;
        setFocus(Qt::MouseFocusReason);
        layoutRects();
        const QPoint pt = eventPos(e);
        if (m_flickerRect.adjusted(-4, -4, 4, 4).contains(pt)) { if (onFlicker) onFlicker(); return; }
        if (m_in.isPair() && m_faultRect.adjusted(-4, -4, 4, 4).contains(pt)) { showFaultMenu(); return; }
        if (onToggle) onToggle();
    }

    void keyPressEvent(QKeyEvent *e) override
    {
        if (e->key() == Qt::Key_Space || e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            if (onToggle) onToggle();
        } else if (e->key() == Qt::Key_F) {
            if (onFlicker) onFlicker();
        } else if (m_in.isPair() && e->key() == Qt::Key_E) {
            showFaultMenu();
        } else {
            QWidget::keyPressEvent(e);
        }
    }
    void focusInEvent(QFocusEvent *e) override { update(); QWidget::focusInEvent(e); }
    void focusOutEvent(QFocusEvent *e) override { update(); QWidget::focusOutEvent(e); }

private:
    // A pair's live state is (NO << 1) | NC: 1 at rest, 2 operated, 0 and 3 faults.
    bool isFault() const { return m_in.isPair() && (m_live == 0 || m_live == 3); }
    bool isOn() const { return m_in.isPair() ? m_live == 2 : (m_live & 1); }

    QString stateWords() const
    {
        if (!m_in.isPair())
            return QStringLiteral("%1 \u00B7 bit %2").arg(isOn() ? QStringLiteral("On") : QStringLiteral("Off")).arg(m_in.bit);
        const int no = (m_live >> 1) & 1, nc = m_live & 1;
        QString words;
        if (m_live == 0)      words = QStringLiteral("Fault: both 0");
        else if (m_live == 3) words = QStringLiteral("Fault: both 1");
        else                  words = isOn() ? QStringLiteral("On") : QStringLiteral("Off");
        return QStringLiteral("%1 \u00B7 NO %2 \u00B7 NC %3").arg(words).arg(no).arg(nc);
    }

    void showFaultMenu()
    {
        QMenu menu(this);
        const int states[] = { 1, 2, 0, 3 };
        const QString labels[] = {
            QStringLiteral("Off \u2014 at rest (NO 0, NC 1)"),
            QStringLiteral("On \u2014 operated (NO 1, NC 0)"),
            QStringLiteral("Fault: both 0 (NO 0, NC 0)"),
            QStringLiteral("Fault: both 1 (NO 1, NC 1)") };
        for (int k = 0; k < 4; ++k) {
            if (k == 2) menu.addSeparator();
            QAction *a = menu.addAction(labels[k]);
            a->setCheckable(true);
            a->setChecked(m_live == states[k]);
            a->setData(states[k]);
        }
        layoutRects();
        if (QAction *chosen = menu.exec(mapToGlobal(m_faultRect.bottomLeft() + QPoint(0, 4))))
            if (onSetState) onSetState(chosen->data().toInt());
    }

    void layoutRects()
    {
        const int fl = 30;
        m_flickerRect = QRect(width() - 14 - fl, (height() - fl) / 2, fl, fl);
        m_control = QRect(m_flickerRect.left() - 12 - 46, (height() - 26) / 2, 46, 26);
        m_faultRect = QRect(m_control.left() - 10 - 28, (height() - 28) / 2, 28, 28);
    }

    void paintSwitch(QPainter &p, bool on, bool fault, const QColor &base, const QColor &accent)
    {
        const QRectF track = m_control;
        const QColor ink = fault ? UiColor::warning() : (on ? accent : UiColor::muted());
        p.setPen(QPen(ink, 1.5));
        p.setBrush(on ? accent : palette().color(QPalette::Window));
        p.drawRoundedRect(track, track.height() / 2, track.height() / 2);
        const double d = track.height() - 8;
        // A fault puts the knob in the middle: neither off nor on.
        double x = on ? track.right() - 4 - d : track.left() + 4;
        if (fault) x = track.center().x() - d / 2;
        p.setPen(Qt::NoPen);
        p.setBrush(on ? base : ink);
        p.drawEllipse(QRectF(x, track.top() + 4, d, d));
    }

    void paintFaultButton(QPainter &p, bool fault, const QColor &base, Tile tile)
    {
        const QRectF r = QRectF(m_faultRect).adjusted(1, 1, -1, -1);
        const QColor warn = CabIoColors::tileInk(UiColor::warning(), tile);
        // A triangle with "!" in it: filled while a fault is set.
        QPainterPath tri;
        tri.moveTo(r.center().x(), r.top() + 3);
        tri.lineTo(r.right() - 2, r.bottom() - 4);
        tri.lineTo(r.left() + 2, r.bottom() - 4);
        tri.closeSubpath();
        p.setPen(QPen(fault ? warn : CabIoColors::tileInk(UiColor::muted(), tile), 1.5, Qt::SolidLine,
                      Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(fault ? QBrush(warn) : QBrush(Qt::NoBrush));
        p.drawPath(tri);
        p.setPen(QPen(fault ? base : CabIoColors::tileInk(UiColor::muted(), tile), 1.8, Qt::SolidLine, Qt::RoundCap));
        const double cx = r.center().x();
        p.drawLine(QPointF(cx, r.top() + 10), QPointF(cx, r.bottom() - 11));
        p.drawPoint(QPointF(cx, r.bottom() - 7.5));
    }

    void paintFlicker(QPainter &p, const QColor &base, const QColor &accent, Tile tile)
    {
        const QRectF r = m_flickerRect;
        p.setPen(QPen(m_flicker ? accent : UiColor::frame(), 1.5));
        p.setBrush(m_flicker ? accent : QBrush(Qt::NoBrush));
        p.drawEllipse(r.adjusted(1, 1, -1, -1));
        const QPointF c = r.center();
        QPainterPath wave;
        wave.moveTo(c.x() - 8, c.y());
        wave.cubicTo(c.x() - 5, c.y() - 8, c.x() - 1, c.y() - 8, c.x(), c.y());
        wave.cubicTo(c.x() + 1, c.y() + 8, c.x() + 5, c.y() + 8, c.x() + 8, c.y());
        p.setPen(QPen(m_flicker ? base : CabIoColors::tileInk(UiColor::muted(), tile), 2, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawPath(wave);
    }

    Input m_in;
    int   m_live = 0;
    bool  m_flicker = false;
    QRect m_control, m_faultRect, m_flickerRect;
};

// One output: a lamp, its name, its state and how often it changed.
class LampTile : public QWidget
{
public:
    LampTile(const OutputBit &o, QWidget *parent = nullptr) : QWidget(parent), m_o(o)
    {
        setToolTip(QStringLiteral("%1: bit %2 (data_byte%3 bit %4)\n%5 when the bit is %6, %7 when it is %8")
                       .arg(o.name).arg(o.bit).arg(o.bit < 8 ? 1 : 2).arg(o.bit % 8)
                       .arg(o.activeWord).arg(o.activeLow ? 0 : 1).arg(o.idleWord.toLower()).arg(o.activeLow ? 1 : 0));
        setAccessibleName(displayName(o.name));
    }
    QSize sizeHint() const override { return QSize(250, 60); }
    const OutputBit &bit() const { return m_o; }

    // `readbackByte`: the pin's byte in the last readback heard, -1 for none.
    void setValues(bool known, bool on, int changes, bool recent, int readbackByte = -1)
    {
        if (known == m_known && on == m_on && changes == m_changes && recent == m_recent
            && readbackByte == m_readback) return;
        m_known = known; m_on = on; m_changes = changes; m_recent = recent; m_readback = readbackByte;
        setAccessibleDescription(on ? QStringLiteral("on") : QStringLiteral("off"));
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPalette pal = palette();
        p.setPen(QPen(m_recent ? UiColor::accent() : UiColor::frame(), m_recent ? 2 : 1));
        p.setBrush(pal.color(QPalette::Base));
        p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 9, 9);

        const QRectF lamp(16, (height() - 26) / 2.0, 26, 26);
        const QColor c = UiColor::signalLamp(m_o.lamp);
        if (m_known && m_on) {
            QRadialGradient glow(lamp.center(), 20);
            QColor g = c; g.setAlpha(90);
            QColor t = c; t.setAlpha(0);
            glow.setColorAt(0.55, g);
            glow.setColorAt(1.0, t);
            p.setPen(Qt::NoPen);
            p.setBrush(glow);
            p.drawEllipse(lamp.center(), 20, 20);
            QRadialGradient body(lamp.center() - QPointF(4, 4), 16);
            body.setColorAt(0, c.lighter(150));
            body.setColorAt(1, c);
            p.setBrush(body);
            // A rim the card can see: a yellow or white lamp on a light
            // theme is otherwise a faint disc (session 328).
            p.setPen(QPen(CabIoColors::lampRim(m_o.lamp), 1.5));
            p.drawEllipse(lamp);
        } else {
            p.setBrush(pal.color(QPalette::Window));
            QPen pen(UiColor::muted(), 1.5);
            if (!m_known) pen.setStyle(Qt::DotLine);
            p.setPen(pen);
            p.drawEllipse(lamp);
        }

        const int left = 56, w = width() - left - 12;
        p.setPen(pal.color(QPalette::Text));
        p.setFont(scaled(font(), 1.0, QFont::DemiBold));
        p.drawText(QRect(left, 10, w, 22), Qt::AlignLeft | Qt::AlignVCenter,
                   p.fontMetrics().elidedText(displayName(m_o.name), Qt::ElideRight, w));
        p.setFont(scaled(font(), 0.85));
        p.setPen(UiColor::muted());
        // "Applied \u00B7 bit 13 = 0": what the output does, then the bit as sent.
        QString sub;
        if (!m_known) {
            sub = QStringLiteral("no data yet \u00B7 bit %1").arg(m_o.bit);
        } else {
            const int bitValue = (m_on != m_o.activeLow) ? 1 : 0;
            sub = QStringLiteral("%1 \u00B7 bit %2 = %3").arg(m_on ? m_o.activeWord : m_o.idleWord)
                      .arg(m_o.bit).arg(bitValue);
        }
        if (m_changes) sub += QStringLiteral(" \u00B7 %1 change%2").arg(m_changes).arg(QLatin1String(m_changes == 1 ? "" : "s"));
        if (m_readback >= 0)
            sub += QStringLiteral(" \u00B7 readback ")
                   + QString::number(m_readback, 16).toUpper().rightJustified(2, QLatin1Char('0'));
        p.drawText(QRect(left, 32, w, 18), Qt::AlignLeft | Qt::AlignVCenter,
                   p.fontMetrics().elidedText(sub, Qt::ElideRight, w));
    }

private:
    OutputBit m_o;
    bool m_known = false, m_on = false, m_recent = false;
    int  m_changes = 0;
    int  m_readback = -1;
};

}  // namespace CabIoUi

using CabIoUi::InputTile;
using CabIoUi::LampTile;
using CabIoUi::TileGrid;

// =============================================================================
//  The window
// =============================================================================

CabIoSimWindow::CabIoSimWindow(SerialManager *serial, QWidget *parent)
    : QWidget(parent, Qt::Window), m_serial(serial)
{
    setObjectName(QStringLiteral("cabIoSimWindow"));
    setWindowTitle(tr("Cab I/O Simulator"));
    setAttribute(Qt::WA_DeleteOnClose);
    WindowGeometry::makeResizableWindow(this);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 10);
    root->setSpacing(10);
    root->addWidget(buildPortBar());

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    m_tabs->addTab(buildInputsTab(),  tr("Inputs \u2192 VCC"));
    m_tabs->addTab(buildOutputsTab(), tr("Outputs \u2190 VCC"));
    m_tabs->addTab(buildReadbackTab(), tr("Readback \u2192 VCC"));
    m_tabs->addTab(buildAnalogTab(),  tr("Analog \u2192 VCC"));
    m_tabs->addTab(buildFramesTab(),  tr("Frames"));
    root->addWidget(m_tabs, 1);

    m_status = new StatusLine(this);
    root->addWidget(m_status);

    m_flickerTimer = new QTimer(this);
    m_flickerTimer->setTimerType(Qt::PreciseTimer);
    m_flickerTimer->setInterval(250);
    connect(m_flickerTimer, &QTimer::timeout, this, &CabIoSimWindow::tickFlicker);
    m_flickerTimer->start();

    m_periodTimer = new QTimer(this);
    m_periodTimer->setTimerType(Qt::PreciseTimer);
    connect(m_periodTimer, &QTimer::timeout, this, &CabIoSimWindow::onPeriodic);

    m_anTimer = new QTimer(this);
    m_anTimer->setTimerType(Qt::PreciseTimer);
    m_anTimer->setInterval(100);
    connect(m_anTimer, &QTimer::timeout, this, &CabIoSimWindow::sendAnalogNow);
    m_anUdp = new UdpSender(this);
    connect(m_anUdp, &UdpSender::error, this, [this](const QString &what) {
        m_status->fail(tr("Analog over UDP: %1").arg(what));
    });

    m_uiTimer = new QTimer(this);
    m_uiTimer->setInterval(250);
    connect(m_uiTimer, &QTimer::timeout, this, &CabIoSimWindow::onUiTick);
    m_uiTimer->start();

    loadSettings();
    rebuildInputs();
    rebuildOutputs();
    rebuildReadback();
    restyle();
    UiColor::onThemeChange(this, [this]() { restyle(); });
    refreshPort();

    resize(1180, 820);
    WindowGeometry::restore(this, QStringLiteral("cabIoSim"));

    if (!m_serial)
        m_status->state(tr("Not connected to the console's serial ports: the simulator can be looked at, not opened."));
    else
        m_status->state(tr("Pick the port wired to the VCC's IOA link and press Open."));
}

CabIoSimWindow::~CabIoSimWindow()
{
    if (m_link) disconnect(m_link, nullptr, this, nullptr);
}

void CabIoSimWindow::closeEvent(QCloseEvent *e)
{
    saveSettings();
    WindowGeometry::save(this, QStringLiteral("cabIoSim"));
    if (m_serial && m_link && m_weOpened && m_link->isOpen())
        m_serial->close(m_link->config().portName);
    QWidget::closeEvent(e);
}

// ---- building ----------------------------------------------------------------------

QWidget *CabIoSimWindow::buildPortBar()
{
    auto *bar = new QWidget(this);
    auto *h = new QHBoxLayout(bar);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(8);

    h->addWidget(new QLabel(tr("Port"), bar));
    m_port = new QComboBox(bar);
    m_port->setEditable(true);
    m_port->setMinimumWidth(170);
    m_port->setToolTip(tr("The serial port wired to the VCC's IOA link"));
    h->addWidget(m_port);
    auto *rescan = new QToolButton(bar);
    rescan->setText(QStringLiteral("\u21BB"));
    rescan->setToolTip(tr("Look for ports again"));
    rescan->setAutoRaise(true);
    connect(rescan, &QToolButton::clicked, this, &CabIoSimWindow::refreshPorts);
    h->addWidget(rescan);

    h->addSpacing(6);
    h->addWidget(new QLabel(tr("Baud"), bar));
    m_baud = new QComboBox(bar);
    m_baud->setEditable(true);
    for (qint32 b : serialStandardBauds()) m_baud->addItem(QString::number(b));
    m_baud->setCurrentText(QStringLiteral("115200"));
    h->addWidget(m_baud);

    m_open = new QPushButton(tr("Open"), bar);
    m_open->setObjectName(QStringLiteral("primary"));
    m_open->setCursor(Qt::PointingHandCursor);
    m_open->setEnabled(m_serial != nullptr);
    connect(m_open, &QPushButton::clicked, this, &CabIoSimWindow::openOrClose);
    h->addWidget(m_open);

    m_portState = new QLabel(bar);
    h->addWidget(m_portState);
    m_traffic = new QLabel(bar);
    m_traffic->setObjectName(QStringLiteral("muted"));
    h->addWidget(m_traffic);
    h->addStretch(1);

    h->addWidget(new QLabel(tr("Cab"), bar));
    h->addWidget(segmented({ tr("SIFA extended"), tr("Old cab") }, &m_cabGroup, bar));
    m_cabGroup->button(0)->setToolTip(tr("STRUCT_CAB_INPUTS_SIFA_EXTENDED_SET1 / _SET2 and the SIFA STRUCT_LOCO_OUTPUT"));
    m_cabGroup->button(1)->setToolTip(tr("STRUCT_CAB_INPUT and the old STRUCT_LOCO_OUTPUT"));
    connect(m_cabGroup, &QButtonGroup::idClicked, this, [this](int id) {
        setCab(id == 1 ? Cab::Old : Cab::Sifa);
    });

    refreshPorts();
    return bar;
}

QWidget *CabIoSimWindow::buildInputsTab()
{
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 10, 0, 0);
    v->setSpacing(8);

    auto *row = new QHBoxLayout;
    row->setSpacing(8);
    row->addWidget(segmented({ tr("Answer VCC requests"), tr("Send every") }, &m_respondGroup, page));
    m_respondGroup->button(0)->setToolTip(tr("Reply to each input request (0x15) addressed to a ticked id, as the card does"));
    m_respondGroup->button(1)->setToolTip(tr("Send each ticked id's data on a period, unasked"));
    connect(m_respondGroup, &QButtonGroup::idClicked, this, [this](int id) {
        setRespond(id == 1 ? Respond::Periodic : Respond::Answer);
    });
    m_period = new QSpinBox(page);
    m_period->setRange(10, 10000);
    m_period->setSingleStep(10);
    m_period->setValue(100);
    m_period->setSuffix(tr(" ms"));
    connect(m_period, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int ms) { setPeriodMs(ms); });
    row->addWidget(m_period);
    auto *sendNowBtn = new QPushButton(tr("Send now"), page);
    sendNowBtn->setToolTip(tr("Send each ticked id's data once, now"));
    connect(sendNowBtn, &QPushButton::clicked, this, &CabIoSimWindow::sendNow);
    row->addWidget(sendNowBtn);
    row->addStretch(1);
    v->addLayout(row);

    // Session 355: the inputs' own controls on a second row. On one row the
    // tab was 1133 px wide on the Mac, about 1440 with Linux's fonts: wider
    // than a 1366 px laptop.
    row = new QHBoxLayout;
    row->setSpacing(8);
    row->addWidget(new QLabel(tr("Flicker"), page));
    m_flicker = new QSpinBox(page);
    m_flicker->setRange(20, 5000);
    m_flicker->setSingleStep(10);
    m_flicker->setValue(250);
    m_flicker->setSuffix(tr(" ms"));
    m_flicker->setToolTip(tr("How long a flickering input holds each state"));
    connect(m_flicker, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int ms) { setFlickerPeriodMs(ms); });
    row->addWidget(m_flicker);

    row->addSpacing(16);
    row->addWidget(new QLabel(tr("vic_health"), page));
    m_healthSpin = new HexSpin(page);
    m_healthSpin->setRange(0, 255);
    m_healthSpin->setPrefix(QStringLiteral("0x"));
    m_healthSpin->setValue(kHealthy);
    m_healthSpin->setToolTip(tr("The health byte after the input data (0x13 in every real reply)"));
    connect(m_healthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int h) { setHealth(quint8(h)); });
    row->addWidget(m_healthSpin);
    row->addStretch(1);

    auto *stopFl = new QPushButton(tr("Stop flicker"), page);
    connect(stopFl, &QPushButton::clicked, this, [this]() {
        for (Bank &b : m_banks) b.bank.stopFlicker();
        refreshInputs();
        m_status->say(tr("Every input holds its state."));
    });
    row->addWidget(stopFl);
    // Session 329: the operator's own default for every input. Set the
    // inputs as wanted, "Save as default"; Reset returns to it, and the
    // window opens with it.
    auto *defaults = new QPushButton(tr("Default"), page);
    defaults->setToolTip(tr("Save the inputs as they are now as the default, or forget the saved default"));
    auto *defaultsMenu = new QMenu(defaults);
    defaultsMenu->addAction(tr("Save these inputs as the default"), this, [this]() {
        saveInputDefault();
        m_status->ok(tr("Saved: the window opens with these inputs, and Reset returns to them."));
    });
    QAction *forget = defaultsMenu->addAction(tr("Forget the saved default (every input Off)"), this, [this]() {
        forgetInputDefault();
        m_status->say(tr("No saved default: Reset puts every input Off."));
    });
    connect(defaultsMenu, &QMenu::aboutToShow, this, [this, forget]() { forget->setEnabled(hasInputDefault()); });
    defaults->setMenu(defaultsMenu);
    row->addWidget(defaults);
    m_resetDefault = new QPushButton(tr("Reset"), page);
    connect(m_resetDefault, &QPushButton::clicked, this, [this]() {
        resetInputsToDefault();
        m_status->say(hasInputDefault() ? tr("Inputs back to the saved default, no flicker.")
                                        : tr("Every input Off, no faults, no flicker."));
    });
    row->addWidget(m_resetDefault);
    v->addLayout(row);

    auto *hint = new QLabel(tr("Click an input to switch it On or Off. Inputs with NO and NC feedbacks switch the same way "
                               "(Off = NO 0 NC 1, On = NO 1 NC 0); their \u26A0 button sets a fault (both 0 or both 1). "
                               "The wave button flickers an input."), page);
    hint->setObjectName(QStringLiteral("muted"));
    hint->setWordWrap(true);
    v->addWidget(hint);

    m_inputsSummary = new QLabel(page);
    m_inputsSummary->setObjectName(QStringLiteral("muted"));
    m_inputsSummary->setWordWrap(true);
    v->addWidget(m_inputsSummary);

    auto *scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_banksHost = new QWidget(scroll);
    m_banksBox = new QVBoxLayout(m_banksHost);
    m_banksBox->setContentsMargins(0, 0, 8, 0);
    m_banksBox->setSpacing(14);
    scroll->setWidget(m_banksHost);
    v->addWidget(scroll, 1);

    m_preview = new QLabel(page);
    m_preview->setObjectName(QStringLiteral("mono"));
    m_preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_preview->setWordWrap(true);
    v->addWidget(m_preview);
    return page;
}

QWidget *CabIoSimWindow::buildOutputsTab()
{
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 10, 0, 0);
    v->setSpacing(6);

    m_outHeadline = new QLabel(page);
    m_outHeadline->setObjectName(QStringLiteral("headline"));
    v->addWidget(m_outHeadline);
    m_outDetail = new QLabel(page);
    m_outDetail->setObjectName(QStringLiteral("muted"));
    m_outDetail->setWordWrap(true);
    v->addWidget(m_outDetail);
    m_outLegend = new QLabel(page);
    m_outLegend->setObjectName(QStringLiteral("muted"));
    m_outLegend->setWordWrap(true);
    v->addWidget(m_outLegend);
    m_outBytes = new QLabel(page);
    m_outBytes->setObjectName(QStringLiteral("mono"));
    m_outBytes->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(m_outBytes);
    v->addSpacing(4);

    auto *scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_lampGrid = new TileGrid(250, scroll);
    scroll->setWidget(m_lampGrid);
    v->addWidget(scroll, 1);

    m_outFooter = new QLabel(page);
    m_outFooter->setObjectName(QStringLiteral("muted"));
    m_outFooter->setWordWrap(true);
    v->addWidget(m_outFooter);
    return page;
}

QWidget *CabIoSimWindow::buildReadbackTab()
{
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 10, 0, 0);
    v->setSpacing(8);

    auto *row = new QHBoxLayout;
    row->setSpacing(8);
    row->addWidget(segmented({ tr("Off"), tr("Answer readback requests") }, &m_rbAnswerGroup, page));
    m_rbAnswerGroup->button(1)->setToolTip(tr("Reply to each readback request (0x1D to 17 / 18) as output card 13 / 14. "
                                              "Leave Off while a real output card is on the line: two would answer."));
    connect(m_rbAnswerGroup, &QButtonGroup::idClicked, this, [this](int id) { setAnswerReadback(id == 1); });
    row->addSpacing(12);
    auto *as = new QLabel(tr("as cards"), page);
    as->setObjectName(QStringLiteral("muted"));
    row->addWidget(as);
    for (quint8 id : { quint8(13), quint8(14) }) {
        auto *chip = new QPushButton(QString::number(id), page);
        chip->setObjectName(QStringLiteral("idChip"));
        chip->setCheckable(true);
        chip->setChecked(true);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setProperty("cardId", int(id));
        chip->setToolTip(tr("Answer the readback requests to %1 as output card %2").arg(id + 4).arg(id));
        connect(chip, &QPushButton::toggled, this, [this]() { refreshReadback(); });
        m_rbCardChips << chip;
        row->addWidget(chip);
    }
    row->addSpacing(16);
    row->addWidget(new QLabel(tr("voc_health"), page));
    m_rbHealthSpin = new HexSpin(page);
    m_rbHealthSpin->setRange(0, 255);
    m_rbHealthSpin->setPrefix(QStringLiteral("0x"));
    m_rbHealthSpin->setValue(kHealthy);
    m_rbHealthSpin->setToolTip(tr("The health byte after the 16 pins (0x13 in every real readback)"));
    connect(m_rbHealthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int h) { setReadbackHealth(quint8(h)); });
    row->addWidget(m_rbHealthSpin);
    row->addStretch(1);
    auto *allAuto = new QPushButton(tr("All pins Auto"), page);
    connect(allAuto, &QPushButton::clicked, this, [this]() {
        m_rb.allAuto();
        refreshReadback();
        m_status->say(tr("Every pin follows its output bit again."));
    });
    row->addWidget(allAuto);
    v->addLayout(row);

    auto *hint = new QLabel(tr("Auto: a pin follows its output bit as the card does \u2014 1 reads back a=0 b=0 fault=1 trans=1 output=0 "
                               "(0x0C), 0 reads back a=1 b=1 fault=1 trans=0 output=1 (0x17). Switch a pin to Manual and click "
                               "its bits to send something else."), page);
    hint->setObjectName(QStringLiteral("muted"));
    hint->setWordWrap(true);
    v->addWidget(hint);
    m_rbSummary = new QLabel(page);
    m_rbSummary->setWordWrap(true);
    v->addWidget(m_rbSummary);

    auto *scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_rbTable = new QWidget(scroll);
    scroll->setWidget(m_rbTable);
    v->addWidget(scroll, 1);
    return page;
}

QWidget *CabIoSimWindow::buildAnalogTab()
{
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 10, 0, 0);
    v->setSpacing(8);

    auto *row = new QHBoxLayout;
    row->setSpacing(8);
    row->addWidget(segmented({ tr("Off"), tr("Send every") }, &m_anSendGroup, page));
    m_anSendGroup->button(1)->setToolTip(tr("Send the six channels over UDP as each ticked controller, unasked, every period. "
                                            "Leave Off while a real analog card is on the line."));
    connect(m_anSendGroup, &QButtonGroup::idClicked, this, [this](int id) { setAnalogSending(id == 1); });
    m_anPeriod = new QSpinBox(page);
    m_anPeriod->setRange(10, 10000);
    m_anPeriod->setSingleStep(10);
    m_anPeriod->setValue(100);
    m_anPeriod->setSuffix(tr(" ms"));
    connect(m_anPeriod, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int ms) { setAnalogPeriodMs(ms); });
    row->addWidget(m_anPeriod);
    auto *once = new QPushButton(tr("Send now"), page);
    once->setToolTip(tr("Send the six channels once, as each ticked controller"));
    connect(once, &QPushButton::clicked, this, &CabIoSimWindow::sendAnalogNow);
    row->addWidget(once);
    row->addSpacing(12);
    auto *as = new QLabel(tr("as"), page);
    as->setObjectName(QStringLiteral("muted"));
    row->addWidget(as);
    for (quint8 id : { kAnalogTopId, kAnalogBottomId }) {
        auto *chip = new QPushButton(id == kAnalogTopId ? tr("15 top") : tr("16 bottom"), page);
        chip->setObjectName(QStringLiteral("idChip"));
        chip->setCheckable(true);
        chip->setChecked(id == kAnalogTopId);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setProperty("cardId", int(id));
        chip->setToolTip(tr("Send as analog controller %1 (src_id %1)").arg(id));
        connect(chip, &QPushButton::toggled, this, [this]() { refreshAnalog(); });
        m_anCardChips << chip;
        row->addWidget(chip);
    }
    row->addStretch(1);
    v->addLayout(row);

    // Session 350: the analog card's frames go over UDP, one datagram each.
    auto *to = new QHBoxLayout;
    to->setSpacing(8);
    to->addWidget(new QLabel(tr("To (UDP)"), page));
    m_anHost = new QLineEdit(QStringLiteral("127.0.0.1"), page);
    m_anHost->setMaximumWidth(160);
    m_anHost->setToolTip(tr("The VCC's address: a dotted quad or a host name"));
    to->addWidget(m_anHost);
    to->addWidget(new QLabel(tr("port"), page));
    m_anPort = new QSpinBox(page);
    m_anPort->setRange(1, 65535);
    m_anPort->setValue(20000);
    to->addWidget(m_anPort);
    connect(m_anHost, &QLineEdit::textChanged, this, [this]() { refreshAnalog(); });
    connect(m_anPort, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { refreshAnalog(); });
    auto *udpNote = new QLabel(tr("one 34-byte frame per datagram; the input and output cards stay on the serial port"), page);
    udpNote->setObjectName(QStringLiteral("muted"));
    to->addWidget(udpNote);
    to->addStretch(1);
    v->addLayout(to);

    auto *hint = new QLabel(tr("Set a channel's loop current, or the pressure it should read as. The VCC reads "
                               "raw / 2\u00B3\u00B9 \u00D7 75 mA (the firmware's 2500/33 is integer division), then "
                               "4\u201320 mA across the sensor's full scale \u00D7 1.02 in kg/cm\u00B2. Below 3.5 mA it is an "
                               "open loop, above 20.5 mA a short: no pressure."), page);
    hint->setObjectName(QStringLiteral("muted"));
    hint->setWordWrap(true);
    v->addWidget(hint);

    // Session 351: a dial per channel, its pressure box under it. Drag the
    // dial or type the pressure: either sets the current that reads as it.
    auto *dials = new QHBoxLayout;
    dials->setSpacing(8);
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(6);
    const QStringList heads{ tr("Channel"), tr("Current"), tr("Sensor"), tr("The VCC reads") };
    for (int c = 0; c < heads.size(); ++c) {
        auto *h = new QLabel(heads.at(c).toUpper(), page);
        h->setObjectName(QStringLiteral("groupHeading"));
        grid->addWidget(h, 0, c);
    }
    for (int ch = 0; ch < kAnalogChannels; ++ch) {
        AnRow r;
        grid->addWidget(new QLabel(QStringLiteral("%1 <span style=\"%2\">\u00B7 channel%3_data</span>")
                                       .arg(analogChannelName(ch), UiColor::mutedStyle()).arg(ch + 1), page),
                        ch + 1, 0);
        r.mA = new QDoubleSpinBox(page);
        r.mA->setRange(0, 25);
        r.mA->setDecimals(3);
        r.mA->setSingleStep(0.1);
        r.mA->setSuffix(tr(" mA"));
        r.mA->setValue(4);
        connect(r.mA, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]() { refreshAnalog(); });
        grid->addWidget(r.mA, ch + 1, 1);
        r.kg = new QDoubleSpinBox(page);
        r.kg->setRange(0, 16.32);
        r.kg->setDecimals(2);
        r.kg->setSingleStep(0.1);
        r.kg->setSuffix(tr(" kg/cm\u00B2"));
        r.kg->setToolTip(tr("Sets the current that reads as this pressure"));
        connect(r.kg, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this, ch](double kg) { setAnalogPressure(ch, kg); });
        // As narrow as its dial: six boxes sized to "16.32 kg/cm\u00B2" made the
        // window 1440 px wide with Linux's fonts (session 351, CI).
        r.kg->setMinimumWidth(80);
        r.gauge = new PressureGauge(analogChannelName(ch), page);
        connect(r.gauge, &PressureGauge::valueEdited, this, [this, ch](double kg) { setAnalogPressure(ch, kg); });
        auto *dial = new QVBoxLayout;
        dial->setSpacing(4);
        dial->addWidget(r.gauge, 1);
        dial->addWidget(r.kg);
        dials->addLayout(dial, 1);
        r.scale = new QComboBox(page);
        r.scale->addItem(tr("10 bar"), 10);
        r.scale->addItem(tr("16 bar"), 16);
        connect(r.scale, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { refreshAnalog(); });
        grid->addWidget(r.scale, ch + 1, 2);
        r.reads = new QLabel(page);
        r.reads->setTextInteractionFlags(Qt::TextSelectableByMouse);
        grid->addWidget(r.reads, ch + 1, 3);
        m_anRows << r;
    }
    grid->setColumnStretch(3, 1);
    v->addLayout(dials, 1);
    v->addLayout(grid);
    m_anSummary = new QLabel(page);
    m_anSummary->setWordWrap(true);
    v->addWidget(m_anSummary);
    return page;
}

void CabIoSimWindow::refreshAnalog()
{
    if (m_anRows.isEmpty()) return;
    for (AnRow &r : m_anRows) {
        const quint32 raw = analogRawFor(r.mA->value());
        const double ma = CabIo::analogMa(raw);
        const int bar = r.scale->currentData().toInt();
        const PressureReading p = currentToPressure(ma, bar);
        { const QSignalBlocker block(r.kg); r.kg->setMaximum(bar * 1.02); }
        QString t = QStringLiteral("%1 \u2192 %2 mA \u2192 ").arg(hex32(raw)).arg(ma, 0, 'f', 3);
        r.gauge->setMaximum(bar * 1.02);
        if (p.status == PressureReading::Ok) {
            t += tr("%1 kg/cm\u00B2 (%2 kPa)").arg(p.kgcm2, 0, 'f', 2).arg(p.kgcm2 * 98.0665, 0, 'f', 0);
            const QSignalBlocker block(r.kg);
            r.kg->setValue(p.kgcm2);
            r.gauge->setValue(p.kgcm2);
            r.gauge->setFault(QString());
        } else {
            t += p.status == PressureReading::Open ? tr("open loop (< 3.5 mA): no pressure")
                                                   : tr("short (> 20.5 mA): no pressure");
            r.gauge->setFault(p.status == PressureReading::Open ? tr("open loop") : tr("short"));
        }
        r.kg->setEnabled(p.status == PressureReading::Ok);
        r.reads->setText(t);
        r.reads->setStyleSheet(p.status == PressureReading::Ok ? QString() : UiColor::warningStyle());
    }
    const QVector<quint8> cards = analogCards();
    QString sum = analogSending() ? tr("Sending every %1 ms as %2 to %3:%4 (UDP).").arg(m_anTimer->interval())
                                        .arg(cards.isEmpty() ? tr("no controller (tick 15 or 16)") : idRange(cards))
                                        .arg(analogHost()).arg(analogPort())
                                  : tr("Off: nothing sent unasked.");
    sum += tr(" %1 sent.").arg(count(m_analogSent, "analog frame", "analog frames"));
    m_anSummary->setText(sum);
    m_anSummary->setStyleSheet(analogSending() ? QString() : UiColor::mutedStyle());
}

// One row per pin, named by the cab's STRUCT_LOCO_OUTPUT.
void CabIoSimWindow::rebuildReadback()
{
    if (!m_rbTable) return;
    delete m_rbTable->layout();
    for (QWidget *w : m_rbTable->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
        w->hide();
        w->deleteLater();
    }
    m_rbRows.clear();

    auto *g = new QGridLayout(m_rbTable);
    g->setContentsMargins(0, 0, 8, 0);
    g->setHorizontalSpacing(10);
    g->setVerticalSpacing(6);
    const QStringList heads{ tr("Output pin"), tr("Mode"), QStringLiteral("a"), QStringLiteral("b"),
                             QStringLiteral("fault"), QStringLiteral("trans"), QStringLiteral("output"),
                             tr("Byte"), tr("Output bit") };
    for (int c = 0; c < heads.size(); ++c) {
        auto *h = new QLabel(heads.at(c).toUpper(), m_rbTable);
        h->setObjectName(QStringLiteral("groupHeading"));
        g->addWidget(h, 0, c);
    }
    QVector<OutputBit> byPin(kPins);
    for (const OutputBit &o : outputBits(m_cab)) byPin[o.bit] = o;
    static const char *const bitNames[5] = { "a", "b", "fault", "trans", "output" };
    for (int pin = 0; pin < kPins; ++pin) {
        RbRow r;
        r.name = new QLabel(QStringLiteral("%1 <span style=\"%2\">\u00B7 pin %3</span>")
                                .arg(displayName(byPin[pin].name).toHtmlEscaped(), UiColor::mutedStyle()).arg(pin),
                            m_rbTable);
        r.name->setToolTip(byPin[pin].name);
        g->addWidget(r.name, pin + 1, 0);
        r.mode = new QPushButton(tr("Auto"), m_rbTable);
        r.mode->setObjectName(QStringLiteral("idChip"));
        r.mode->setCheckable(true);
        r.mode->setCursor(Qt::PointingHandCursor);
        r.mode->setToolTip(tr("Auto follows the output bit; Manual sends the bits you set"));
        connect(r.mode, &QPushButton::clicked, this, [this, pin](bool manual) {
            bool known = false;
            const QVector<quint8> cards = readbackCards();
            const quint16 w = readbackWord(cards.isEmpty() ? quint8(13) : cards.first(), &known);
            if (manual) m_rb.setManual(pin, m_rb.pinFor(pin, w));   // start from what Auto sends
            else m_rb.setAuto(pin);
            refreshReadback();
        });
        g->addWidget(r.mode, pin + 1, 1);
        for (int k = 0; k < 5; ++k) {
            auto *b = new QPushButton(QString::fromLatin1(bitNames[k]), m_rbTable);
            b->setObjectName(QStringLiteral("idChip"));
            b->setCheckable(true);
            b->setCursor(Qt::PointingHandCursor);
            connect(b, &QPushButton::clicked, this, [this, pin, k]() {
                PinReadback p = pinFromByte(m_rb.manualByte(pin));
                bool *fields[5] = { &p.a, &p.b, &p.fault, &p.trans, &p.output };
                *fields[k] = !*fields[k];
                m_rb.setManual(pin, quint8((m_rb.manualByte(pin) & 0xE0) | pinByte(p)));
                refreshReadback();
            });
            r.bits[k] = b;
            g->addWidget(b, pin + 1, 2 + k);
        }
        r.byte = new QLabel(m_rbTable);
        r.byte->setObjectName(QStringLiteral("bankWord"));
        g->addWidget(r.byte, pin + 1, 7);
        r.commanded = new QLabel(m_rbTable);
        r.commanded->setObjectName(QStringLiteral("muted"));
        g->addWidget(r.commanded, pin + 1, 8);
        m_rbRows << r;
    }
    g->setColumnStretch(9, 1);
    g->setRowStretch(kPins + 1, 1);
    refreshReadback();
}

// The output word card `card` was last sent; *known false when none yet.
quint16 CabIoSimWindow::readbackWord(quint8 card, bool *known) const
{
    *known = m_outDest.contains(card);
    return *known ? m_outDest.value(card).word : 0;
}

void CabIoSimWindow::refreshReadback()
{
    if (m_rbRows.isEmpty()) return;
    const QVector<quint8> cards = readbackCards();
    quint8 shown = cards.isEmpty() ? quint8(13) : cards.first();
    for (quint8 c : cards)
        if (m_outDest.contains(c)) { shown = c; break; }
    bool known = false;
    const quint16 word = readbackWord(shown, &known);
    for (int pin = 0; pin < m_rbRows.size(); ++pin) {
        RbRow &r = m_rbRows[pin];
        const bool manual = m_rb.isManual(pin);
        const quint8 byte = m_rb.pinFor(pin, word);
        const PinReadback p = pinFromByte(byte);
        const bool vals[5] = { p.a, p.b, p.fault, p.trans, p.output };
        { const QSignalBlocker block(r.mode); r.mode->setChecked(manual); }
        r.mode->setText(manual ? tr("Manual") : tr("Auto"));
        for (int k = 0; k < 5; ++k) {
            const QSignalBlocker block(r.bits[k]);
            r.bits[k]->setChecked(vals[k]);
            r.bits[k]->setEnabled(manual);
        }
        const bool sendable = manual || known;
        r.byte->setText(sendable ? hex8(byte) : QStringLiteral("\u2014"));
        r.byte->setStyleSheet(manual ? UiColor::accentStyle() : QString());   // what is overridden
        r.commanded->setText(known ? QString::number((word >> pin) & 1u) : QStringLiteral("\u2014"));
        r.commanded->setToolTip(known ? tr("Bit %1 of the output word last sent to card %2 (%3)")
                                            .arg(pin).arg(shown).arg(hex16(word))
                                      : tr("No output data sent to card %1 yet").arg(shown));
    }
    QString sum = m_rbAnswer ? tr("Answering readback requests as %1.").arg(
                                   cards.isEmpty() ? tr("no card (tick 13 or 14)")
                                                   : tr("card %1").arg(idRange(cards)))
                             : tr("Off: readback requests are not answered.");
    sum += tr(" %1 heard, %2 sent").arg(count(m_outRequests, "request", "requests"))
               .arg(count(m_readbacksSent, "readback", "readbacks"));
    if (m_readbackUnanswered)
        sum += tr(", %1 not answered (no output data to that card yet)").arg(m_readbackUnanswered);
    if (m_rb.manualCount()) sum += tr(" \u00B7 %1 Manual").arg(count(m_rb.manualCount(), "pin", "pins"));
    sum += known ? tr(" \u00B7 the table shows card %1's word %2.").arg(shown).arg(hex16(word))
                 : tr(" \u00B7 no output data yet: Auto pins have nothing to follow.");
    m_rbSummary->setText(sum);
    m_rbSummary->setStyleSheet(m_rbAnswer ? QString() : UiColor::mutedStyle());
}

QWidget *CabIoSimWindow::buildFramesTab()
{
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 10, 0, 0);
    v->setSpacing(8);

    auto *box = new QGroupBox(tr("Wire format \u2014 the defaults are what the real frames carry"), page);
    auto *form = new QFormLayout(box);
    m_crc = new QComboBox(box);
    for (Crc c : allCrcs()) m_crc->addItem(crcName(c), int(c));
    m_crc->setToolTip(tr("Over the header and the payload, stored little-endian"));
    connect(m_crc, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        m_wf.crc = Crc(m_crc->currentData().toInt());
        refreshInputs();
        onUiTick();
    });
    form->addRow(tr("CRC"), m_crc);
    m_length = new QComboBox(box);
    for (LengthRule r : { LengthRule::WholeFrame, LengthRule::HeaderAndPayload, LengthRule::Payload })
        m_length->addItem(lengthRuleName(r), int(r));
    connect(m_length, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        m_wf.length = LengthRule(m_length->currentData().toInt());
        refreshInputs();
    });
    form->addRow(tr("message_length counts"), m_length);
    m_echo = new QCheckBox(tr("A reply carries its request's seq_num"), box);
    m_echo->setChecked(true);
    connect(m_echo, &QCheckBox::toggled, this, [this](bool on) { m_wf.echoSeq = on; refreshInputs(); });
    form->addRow(tr("seq_num"), m_echo);
    auto *crcRow = new QHBoxLayout;
    m_crcNote = new QLabel(box);
    m_crcNote->setWordWrap(true);
    crcRow->addWidget(m_crcNote, 1);
    m_useCrc = new QPushButton(tr("Use it"), box);
    m_useCrc->setVisible(false);
    connect(m_useCrc, &QPushButton::clicked, this, [this]() {
        if (m_lastCrcMatches.isEmpty()) return;
        m_crc->setCurrentIndex(m_crc->findData(int(m_lastCrcMatches.first())));
        m_status->ok(tr("CRC set to %1, as the VCC's frames carry.").arg(crcName(m_wf.crc)));
    });
    crcRow->addWidget(m_useCrc);
    form->addRow(tr("Received"), crcRow);
    v->addWidget(box);

    auto *pasteRow = new QHBoxLayout;
    pasteRow->addWidget(new QLabel(tr("Decode a frame"), page));
    m_pasteHex = new QLineEdit(page);
    m_pasteHex->setPlaceholderText(tr("AF AF 13 05 01 51 07 00 95 95 F8 41 03 \u2026 FA FA"));
    m_pasteHex->setClearButtonEnabled(true);
    pasteRow->addWidget(m_pasteHex, 1);
    auto *decode = new QPushButton(tr("Decode"), page);
    auto doDecode = [this]() {
        bool ok = false;
        const QByteArray bytes = serialParseHex(m_pasteHex->text(), &ok);
        if (!ok || bytes.isEmpty()) { m_status->fail(tr("Not hex: nothing decoded.")); return; }
        Scanner s;
        const QVector<Frame> frames = s.feed(bytes);
        if (frames.isEmpty()) {
            m_status->fail(tr("No frame found in %1: no start pair (AA AA, AF AF, CC CC) with its end pair.")
                               .arg(count(bytes.size(), "byte", "bytes")));
            return;
        }
        for (const Frame &f : frames) logFrame(false, f);
        flushLog();
        const Frame &f = frames.first();
        m_lastCrcMatches = f.crcMatches;
        m_crcSeen = true;
        onUiTick();
        m_status->ok(tr("%1 decoded into the table, as received.").arg(count(frames.size(), "frame", "frames")));
    };
    connect(decode, &QPushButton::clicked, this, doDecode);
    connect(m_pasteHex, &QLineEdit::returnPressed, this, doDecode);
    pasteRow->addWidget(decode);
    v->addLayout(pasteRow);

    auto *logRow = new QHBoxLayout;
    m_pause = new QCheckBox(tr("Pause"), page);
    m_pause->setToolTip(tr("Stop adding rows; frames are still answered"));
    logRow->addWidget(m_pause);
    auto *clear = new QPushButton(tr("Clear"), page);
    connect(clear, &QPushButton::clicked, this, [this]() { m_log->setRowCount(0); m_pendingRows.clear(); m_pendingKinds.clear(); });
    logRow->addWidget(clear);
    m_logNote = new QLabel(page);
    m_logNote->setObjectName(QStringLiteral("muted"));
    logRow->addWidget(m_logNote, 1);
    v->addLayout(logRow);

    m_log = new QTableWidget(0, 8, page);
    m_log->setHorizontalHeaderLabels({ tr("Time"), tr("Dir"), tr("Frame"), tr("From \u2192 To"),
                                       tr("Seq"), tr("Data"), tr("CRC"), tr("Bytes") });
    m_log->verticalHeader()->setVisible(false);
    m_log->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_log->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_log->setAlternatingRowColors(true);
    m_log->horizontalHeader()->setStretchLastSection(true);
    m_log->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_log->setColumnWidth(0, 112);
    m_log->setColumnWidth(1, 40);
    m_log->setColumnWidth(2, 170);
    m_log->setColumnWidth(3, 150);
    m_log->setColumnWidth(4, 56);
    m_log->setColumnWidth(5, 210);
    m_log->setColumnWidth(6, 110);
    v->addWidget(m_log, 1);
    return page;
}

void CabIoSimWindow::restyle()
{
    const QPalette pal = palette();
    const QString frame  = UiColor::frame().name();
    const QString accent = UiColor::accent().name();
    const QString base   = pal.color(QPalette::Base).name();
    const QString text   = pal.color(QPalette::Text).name();
    const QString muted  = UiColor::muted().name();
    setStyleSheet(QStringLiteral(
        "QWidget#segmented QPushButton { border: 1px solid %1; padding: 5px 14px; background: %3; color: %4; border-radius: 0px; }"
        "QWidget#segmented QPushButton[seg=\"mid\"], QWidget#segmented QPushButton[seg=\"last\"] { border-left: none; }"
        "QWidget#segmented QPushButton[seg=\"first\"] { border-top-left-radius: 6px; border-bottom-left-radius: 6px; }"
        "QWidget#segmented QPushButton[seg=\"last\"] { border-top-right-radius: 6px; border-bottom-right-radius: 6px; }"
        "QWidget#segmented QPushButton:checked { background: %2; color: %3; border-color: %2; font-weight: 600; }"
        "QPushButton#primary { background: %2; color: %3; border: 1px solid %2; border-radius: 6px; padding: 5px 18px; font-weight: 600; }"
        "QPushButton#primary:disabled { background: %3; color: %5; border-color: %1; }"
        "QPushButton#idChip { border: 1px solid %1; border-radius: 11px; padding: 2px 10px; background: %3; color: %4; }"
        "QPushButton#idChip:checked { background: %2; color: %3; border-color: %2; font-weight: 600; }"
        "QPushButton#idChip:disabled { color: %5; }"
        "QPushButton#idChip:checked:disabled { background: %5; color: %3; border-color: %5; }"
        "QLabel#muted { color: %5; }"
        "QLabel#groupHeading { color: %5; font-weight: 600; letter-spacing: 1px; }"
        "QLabel#bankTitle { font-size: 15pt; font-weight: 600; }"
        "QLabel#headline { font-size: 14pt; font-weight: 600; }"
        "QLabel#mono, QLabel#bankWord { font-family: \"Consolas\", \"Menlo\", \"DejaVu Sans Mono\", monospace; color: %5; }"
        "QLabel#bankWord { color: %4; font-weight: 600; }"
        "QFrame#bankRule { color: %1; }")
        .arg(frame, accent, base, text, muted));
    for (Bank &b : m_banks)
        for (InputTile *t : b.tiles) t->update();
    for (LampTile *l : m_lamps) l->update();
    refreshPort();
}

// ---- the cab type -------------------------------------------------------------------

void CabIoSimWindow::setCab(Cab cab)
{
    if (m_cabGroup) m_cabGroup->button(cab == Cab::Old ? 1 : 0)->setChecked(true);
    if (cab == m_cab && !m_banks.isEmpty()) return;
    m_cab = cab;
    rebuildInputs();
    rebuildOutputs();
    rebuildReadback();
    saveSettings();
}

void CabIoSimWindow::rebuildInputs()
{
    // Clear the old banks' widgets.
    while (QLayoutItem *it = m_banksBox->takeAt(0)) {
        if (QWidget *w = it->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete it;
    }
    m_banks.clear();

    const QVector<const InputLayout *> layouts = inputLayouts(m_cab);
    m_banks.resize(layouts.size());
    for (int bi = 0; bi < layouts.size(); ++bi) {
        const InputLayout *l = layouts.at(bi);
        Bank &b = m_banks[bi];
        b.bank = InputBank(l);
        applyDefault(b);

        auto *section = new QWidget(m_banksHost);
        auto *sv = new QVBoxLayout(section);
        sv->setContentsMargins(0, 0, 0, 0);
        sv->setSpacing(8);

        auto *head = new QHBoxLayout;
        head->setSpacing(10);
        auto *title = new QLabel(l->title, section);
        title->setObjectName(QStringLiteral("bankTitle"));
        head->addWidget(title);
        auto *structLbl = new QLabel(l->structName, section);
        structLbl->setObjectName(QStringLiteral("muted"));
        head->addWidget(structLbl);
        head->addStretch(1);
        auto *answers = new QLabel(tr("Answers as"), section);
        answers->setObjectName(QStringLiteral("muted"));
        head->addWidget(answers);
        const QVector<quint8> chipIds = (m_cab == Cab::Old)
            ? QVector<quint8>{ 5, 6, 7, 8, 9, 10, 11, 12 } : l->ids;
        for (quint8 id : chipIds) {
            auto *chip = new QPushButton(QString::number(id), section);
            chip->setObjectName(QStringLiteral("idChip"));
            chip->setCheckable(true);
            chip->setChecked(l->ids.contains(id));
            chip->setCursor(Qt::PointingHandCursor);
            chip->setProperty("cardId", int(id));
            chip->setToolTip(tr("Answer requests to input controller %1").arg(id));
            connect(chip, &QPushButton::toggled, this, [this]() { refreshInputs(); saveSettings(); });
            b.idChips << chip;
            head->addWidget(chip);
        }
        head->addSpacing(10);
        b.word = new QLabel(section);
        b.word->setObjectName(QStringLiteral("bankWord"));
        b.word->setToolTip(tr("input_data as sent now (uint32, little-endian on the wire)"));
        b.word->setTextInteractionFlags(Qt::TextSelectableByMouse);
        head->addWidget(b.word);
        sv->addLayout(head);

        b.counts = new QLabel(section);
        b.counts->setObjectName(QStringLiteral("muted"));
        sv->addWidget(b.counts);

        auto *grid = new TileGrid(330, section);
        QString group;
        for (int i = 0; i < l->inputs.size(); ++i) {
            const Input &in = l->inputs.at(i);
            if (in.group != group) { group = in.group; grid->addHeading(group); }
            auto *tile = new InputTile(in);
            tile->onToggle = [this, bi, i]() {
                m_banks[bi].bank.toggle(i);
                refreshInputs();
            };
            tile->onSetState = [this, bi, i](int s) {
                m_banks[bi].bank.setState(i, s);
                refreshInputs();
            };
            tile->onFlicker = [this, bi, i]() {
                InputBank &bank = m_banks[bi].bank;
                bank.setFlicker(i, !bank.flickering(i));
                refreshInputs();
            };
            grid->addTile(tile);
            b.tiles << tile;
        }
        sv->addWidget(grid);
        m_banksBox->addWidget(section);
        if (bi + 1 < layouts.size()) {
            auto *rule = new QFrame(m_banksHost);
            rule->setObjectName(QStringLiteral("bankRule"));
            rule->setFrameShape(QFrame::HLine);
            m_banksBox->addWidget(rule);
        }
    }
    m_banksBox->addStretch(1);

    // Restore the ticked ids saved for this cab type.
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    for (int bi = 0; bi < m_banks.size(); ++bi) {
        const QString key = QStringLiteral("cabio/ids_") + m_banks[bi].bank.layout()->key;
        if (!s.contains(key)) continue;
        QVector<quint8> ids;
        for (const QString &t : s.value(key).toString().split(QLatin1Char(','), Qt::SkipEmptyParts))
            ids << quint8(t.toInt());
        setBankIds(bi, ids);
    }
    refreshInputs();
}

void CabIoSimWindow::rebuildOutputs()
{
    m_lampGrid->clear();
    m_lamps.clear();
    m_outChanges.clear();
    m_outChangedAtMs.clear();
    QString group;
    for (const OutputBit &o : outputBits(m_cab)) {
        if (o.group != group) { group = o.group; m_lampGrid->addHeading(group); }
        auto *lamp = new LampTile(o);
        m_lampGrid->addTile(lamp);
        m_lamps << lamp;
    }
    refreshOutputs();
}

QVector<quint8> CabIoSimWindow::bankIds(int i) const
{
    QVector<quint8> ids;
    for (QPushButton *c : m_banks.at(i).idChips)
        if (c->isChecked()) ids << quint8(c->property("cardId").toInt());
    return ids;
}

void CabIoSimWindow::setBankIds(int i, const QVector<quint8> &ids)
{
    for (QPushButton *c : m_banks[i].idChips) {
        const QSignalBlocker block(c);
        c->setChecked(ids.contains(quint8(c->property("cardId").toInt())));
    }
    refreshInputs();
}

int CabIoSimWindow::bankFor(quint8 id) const
{
    for (int i = 0; i < m_banks.size(); ++i)
        if (bankIds(i).contains(id)) return i;
    return -1;
}

// ---- settings --------------------------------------------------------------------------

void CabIoSimWindow::setRespond(Respond r)
{
    m_respond = r;
    if (m_respondGroup) m_respondGroup->button(r == Respond::Periodic ? 1 : 0)->setChecked(true);
    if (r == Respond::Periodic) m_periodTimer->start(m_period ? m_period->value() : 100);
    else m_periodTimer->stop();
    refreshInputs();
}

void CabIoSimWindow::setPeriodMs(int ms)
{
    if (m_period && m_period->value() != ms) m_period->setValue(ms);
    m_periodTimer->setInterval(ms);
}

void CabIoSimWindow::setFlickerPeriodMs(int ms)
{
    if (m_flicker && m_flicker->value() != ms) m_flicker->setValue(ms);
    m_flickerTimer->setInterval(ms);
    refreshInputs();
}

int CabIoSimWindow::flickerPeriodMs() const { return m_flickerTimer->interval(); }

void CabIoSimWindow::setHealth(quint8 h)
{
    m_health = h;
    if (m_healthSpin && m_healthSpin->value() != h) m_healthSpin->setValue(h);
    refreshInputs();
}

void CabIoSimWindow::setAnswerReadback(bool on)
{
    m_rbAnswer = on;
    if (m_rbAnswerGroup) m_rbAnswerGroup->button(on ? 1 : 0)->setChecked(true);
    refreshReadback();
}

void CabIoSimWindow::setReadbackCards(const QVector<quint8> &cards)
{
    for (QPushButton *c : m_rbCardChips) {
        const QSignalBlocker block(c);
        c->setChecked(cards.contains(quint8(c->property("cardId").toInt())));
    }
    refreshReadback();
}

QVector<quint8> CabIoSimWindow::readbackCards() const
{
    QVector<quint8> ids;
    for (QPushButton *c : m_rbCardChips)
        if (c->isChecked()) ids << quint8(c->property("cardId").toInt());
    return ids;
}

void CabIoSimWindow::setReadbackHealth(quint8 h)
{
    m_rbHealth = h;
    if (m_rbHealthSpin && m_rbHealthSpin->value() != h) m_rbHealthSpin->setValue(h);
    refreshReadback();
}

// ---- input defaults ----------------------------------------------------------------------

static QString defaultKey(const InputLayout *l) { return QStringLiteral("cabio/default_") + l->key; }

void CabIoSimWindow::applyDefault(Bank &b) const
{
    b.bank.reset();
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    const QString key = defaultKey(b.bank.layout());
    if (!s.contains(key)) return;
    bool ok = false;
    const quint32 w = s.value(key).toString().toUInt(&ok, 16);
    if (ok) b.bank.setBase(w & layoutMask(*b.bank.layout()));
}

void CabIoSimWindow::saveInputDefault()
{
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    for (const Bank &b : m_banks)
        s.setValue(defaultKey(b.bank.layout()),
                   QString::number(b.bank.base() & layoutMask(*b.bank.layout()), 16).toUpper());
    refreshInputs();
}

void CabIoSimWindow::resetInputsToDefault()
{
    for (Bank &b : m_banks) applyDefault(b);
    refreshInputs();
}

void CabIoSimWindow::forgetInputDefault()
{
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    for (const Bank &b : m_banks) s.remove(defaultKey(b.bank.layout()));
    refreshInputs();
}

bool CabIoSimWindow::hasInputDefault() const
{
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    for (const Bank &b : m_banks)
        if (s.contains(defaultKey(b.bank.layout()))) return true;
    return false;
}

void CabIoSimWindow::setWireFormat(const WireFormat &wf)
{
    m_wf = wf;
    { const QSignalBlocker b(m_crc);    m_crc->setCurrentIndex(m_crc->findData(int(wf.crc))); }
    { const QSignalBlocker b(m_length); m_length->setCurrentIndex(m_length->findData(int(wf.length))); }
    { const QSignalBlocker b(m_echo);   m_echo->setChecked(wf.echoSeq); }
    refreshInputs();
}

void CabIoSimWindow::loadSettings()
{
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("cabio"));
    const QString port = s.value(QStringLiteral("port")).toString();
    if (!port.isEmpty()) {
        const int i = m_port->findData(port);
        if (i >= 0) m_port->setCurrentIndex(i); else m_port->setEditText(port);
    }
    m_baud->setCurrentText(s.value(QStringLiteral("baud"), QStringLiteral("115200")).toString());
    m_cab = s.value(QStringLiteral("cab")).toString() == QLatin1String("old") ? Cab::Old : Cab::Sifa;
    m_cabGroup->button(m_cab == Cab::Old ? 1 : 0)->setChecked(true);
    m_period->setValue(s.value(QStringLiteral("periodMs"), 100).toInt());
    m_flicker->setValue(s.value(QStringLiteral("flickerMs"), 250).toInt());
    m_healthSpin->setValue(s.value(QStringLiteral("health"), int(kHealthy)).toInt());
    WireFormat wf;
    wf.crc = Crc(s.value(QStringLiteral("crc"), int(wf.crc)).toInt());
    wf.length = LengthRule(s.value(QStringLiteral("length"), int(wf.length)).toInt());
    wf.echoSeq = s.value(QStringLiteral("echoSeq"), true).toBool();
    // No brake by default: BP 5.0, BC 0, MR 9.0 kg/cm^2, the rest 4 mA.
    static const double defaultKg[kAnalogChannels] = { 5.0, 0, 9.0, 0, 0, 0 };
    for (int ch = 0; ch < kAnalogChannels; ++ch) {
        setAnalogScale(ch, s.value(QStringLiteral("analogBar_%1").arg(ch + 1), 10).toInt());
        const QVariant ma = s.value(QStringLiteral("analogMa_%1").arg(ch + 1));
        if (ma.isValid()) setAnalogMa(ch, ma.toDouble()); else setAnalogPressure(ch, defaultKg[ch]);
    }
    setAnalogPeriodMs(s.value(QStringLiteral("analogPeriodMs"), 100).toInt());
    setAnalogTarget(s.value(QStringLiteral("analogHost"), QStringLiteral("127.0.0.1")).toString(),
                    quint16(s.value(QStringLiteral("analogPort"), 20000).toUInt()));
    QVector<quint8> anIds;
    for (const QString &id : s.value(QStringLiteral("analogIds"), QStringLiteral("15")).toString().split(QLatin1Char(','), Qt::SkipEmptyParts))
        anIds << quint8(id.toInt());
    setAnalogCards(anIds);
    s.endGroup();
    setWireFormat(wf);
    // Always start answering: a window that opened sending unasked would
    // surprise whoever opens it next.
    setRespond(Respond::Answer);
}

void CabIoSimWindow::saveSettings() const
{
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("cabio"));
    s.setValue(QStringLiteral("port"), currentPortName());
    s.setValue(QStringLiteral("baud"), m_baud->currentText());
    s.setValue(QStringLiteral("cab"), m_cab == Cab::Old ? QStringLiteral("old") : QStringLiteral("sifa"));
    s.setValue(QStringLiteral("periodMs"), m_period->value());
    s.setValue(QStringLiteral("flickerMs"), m_flicker->value());
    s.setValue(QStringLiteral("health"), m_healthSpin->value());
    s.setValue(QStringLiteral("crc"), int(m_wf.crc));
    s.setValue(QStringLiteral("length"), int(m_wf.length));
    s.setValue(QStringLiteral("echoSeq"), m_wf.echoSeq);
    for (int ch = 0; ch < m_anRows.size(); ++ch) {
        s.setValue(QStringLiteral("analogMa_%1").arg(ch + 1), analogMa(ch));
        s.setValue(QStringLiteral("analogBar_%1").arg(ch + 1), analogScale(ch));
    }
    s.setValue(QStringLiteral("analogPeriodMs"), m_anPeriod->value());
    s.setValue(QStringLiteral("analogHost"), analogHost());
    s.setValue(QStringLiteral("analogPort"), analogPort());
    QStringList anIds;
    for (quint8 id : analogCards()) anIds << QString::number(id);
    s.setValue(QStringLiteral("analogIds"), anIds.join(QLatin1Char(',')));
    for (int bi = 0; bi < m_banks.size(); ++bi) {
        QStringList ids;
        for (quint8 id : bankIds(bi)) ids << QString::number(id);
        s.setValue(QStringLiteral("ids_") + m_banks[bi].bank.layout()->key, ids.join(QLatin1Char(',')));
    }
    s.endGroup();
}

// ---- the port ------------------------------------------------------------------------------

void CabIoSimWindow::refreshPorts()
{
    const QString current = currentPortName();
    const QSignalBlocker block(m_port);
    m_port->clear();
    for (const QSerialPortInfo &p : QSerialPortInfo::availablePorts()) {
        QString label = p.portName();
        if (!p.description().isEmpty()) label += QStringLiteral(" \u2014 ") + p.description();
        m_port->addItem(label, p.portName());
    }
    const int i = m_port->findData(current);
    if (i >= 0) m_port->setCurrentIndex(i);
    else if (!current.isEmpty()) m_port->setEditText(current);
}

// The listed port's name ("COM5", not "COM5 \u2014 USB Serial"), or what was typed.
QString CabIoSimWindow::currentPortName() const
{
    const int i = m_port->currentIndex();
    if (i >= 0 && m_port->currentText() == m_port->itemText(i)) return m_port->itemData(i).toString();
    return m_port->currentText().section(QStringLiteral(" \u2014 "), 0, 0).trimmed();
}

void CabIoSimWindow::attachLink(SerialLink *link)
{
    if (m_link == link) return;
    if (m_link) disconnect(m_link, nullptr, this, nullptr);
    m_link = link;
    if (!link) return;
    connect(link, &SerialLink::bytesReceived, this, [this](const QByteArray &b, qint64) { feedBytes(b); });
    connect(link, &SerialLink::opened, this, [this]() {
        refreshPort();
        m_status->ok(tr("%1 open at %2. Waiting for the VCC.").arg(m_link->config().portName, m_link->config().summary()));
    });
    connect(link, &SerialLink::openFailed, this, [this](const QString &why) {
        m_weOpened = false;
        refreshPort();
        m_status->fail(tr("Not opened: %1").arg(why));
    });
    connect(link, &SerialLink::closed, this, [this]() { refreshPort(); });
    connect(link, &SerialLink::lost, this, [this](const QString &why) {
        refreshPort();
        m_status->fail(tr("The port went away: %1").arg(why));
    });
}

void CabIoSimWindow::openOrClose()
{
    if (!m_serial) return;
    if (m_link && (m_link->isOpen() || m_link->isOpening())) {
        m_serial->close(m_link->config().portName);
        m_weOpened = false;
        refreshPort();
        m_status->say(tr("Port closed."));
        return;
    }
    const QString port = currentPortName();
    if (port.isEmpty()) { m_status->fail(tr("Pick a port first.")); return; }
    bool ok = false;
    const int baud = m_baud->currentText().toInt(&ok);
    if (!ok || baud <= 0) { m_status->fail(tr("Baud rate \u201C%1\u201D is not a number.").arg(m_baud->currentText())); return; }

    SerialConfig c;
    c.portName = port;
    c.baud = baud;
    m_serial->setFeed(port, false);     // binary frames: never into a console tab,
    m_serial->setBinary(port, true);    // nor judged as log lines
    attachLink(m_serial->linkFor(port));
    m_scanner.clear();
    m_weOpened = true;
    m_serial->openAsync(c);
    refreshPort();
    saveSettings();
}

bool CabIoSimWindow::openPort(const QString &portName, int baud)
{
    if (!m_serial || (m_link && m_link->isOpen())) return false;
    m_port->setEditText(portName);
    m_baud->setCurrentText(QString::number(baud));
    openOrClose();
    return m_link && (m_link->isOpen() || m_link->isOpening());
}

bool CabIoSimWindow::isPortOpen() const { return m_link && m_link->isOpen(); }

void CabIoSimWindow::refreshPort()
{
    if (!m_open) return;
    const bool open = m_link && m_link->isOpen();
    const bool opening = m_link && m_link->isOpening();
    m_open->setText(open || opening ? tr("Close") : tr("Open"));
    m_port->setEnabled(!open && !opening);
    m_baud->setEnabled(!open && !opening);
    if (open) {
        m_portState->setText(QStringLiteral("\u25CF ") + tr("Open \u00B7 %1").arg(m_link->config().summary()));
        m_portState->setStyleSheet(UiColor::okStyle());
    } else if (opening) {
        m_portState->setText(QStringLiteral("\u25CB ") + tr("Opening\u2026"));
        m_portState->setStyleSheet(UiColor::warningStyle());
    } else {
        m_portState->setText(QStringLiteral("\u25CB ") + tr("Closed"));
        m_portState->setStyleSheet(UiColor::mutedStyle());
    }
    m_traffic->setText(m_link && (open || m_link->rxBytes() || m_link->txBytes())
                           ? tr("rx %1 B \u00B7 tx %2 B").arg(m_link->rxBytes()).arg(m_link->txBytes())
                           : QString());
}

// ---- analog -------------------------------------------------------------------------------

void CabIoSimWindow::setAnalogMa(int ch, double mA) { if (ch >= 0 && ch < m_anRows.size()) m_anRows[ch].mA->setValue(mA); }
double CabIoSimWindow::analogMa(int ch) const { return m_anRows.value(ch).mA ? m_anRows.at(ch).mA->value() : 0; }

void CabIoSimWindow::setAnalogPressure(int ch, double kgcm2)
{
    if (ch < 0 || ch >= m_anRows.size()) return;
    setAnalogMa(ch, 4.0 + 16.0 * kgcm2 / (analogScale(ch) * 1.02));
}

void CabIoSimWindow::setAnalogScale(int ch, int bar)
{
    if (ch < 0 || ch >= m_anRows.size()) return;
    m_anRows[ch].scale->setCurrentIndex(qMax(0, m_anRows[ch].scale->findData(bar)));
}

int CabIoSimWindow::analogScale(int ch) const
{
    return m_anRows.value(ch).scale ? m_anRows.at(ch).scale->currentData().toInt() : 10;
}

void CabIoSimWindow::setAnalogCards(const QVector<quint8> &ids)
{
    for (QPushButton *c : m_anCardChips) {
        const QSignalBlocker b(c);
        c->setChecked(ids.contains(quint8(c->property("cardId").toInt())));
    }
    refreshAnalog();
}

QVector<quint8> CabIoSimWindow::analogCards() const
{
    QVector<quint8> ids;
    for (QPushButton *c : m_anCardChips)
        if (c->isChecked()) ids << quint8(c->property("cardId").toInt());
    return ids;
}

void CabIoSimWindow::setAnalogSending(bool on)
{
    { const QSignalBlocker b(m_anSendGroup); m_anSendGroup->button(on ? 1 : 0)->setChecked(true); }
    if (on) m_anTimer->start(); else m_anTimer->stop();
    refreshAnalog();
}

bool CabIoSimWindow::analogSending() const { return m_anTimer->isActive(); }

void CabIoSimWindow::setAnalogPeriodMs(int ms)
{
    { const QSignalBlocker b(m_anPeriod); m_anPeriod->setValue(ms); }
    m_anTimer->setInterval(ms);
    refreshAnalog();
}

void CabIoSimWindow::setAnalogTarget(const QString &host, quint16 port)
{
    m_anHost->setText(host);
    m_anPort->setValue(port);
}

QString CabIoSimWindow::analogHost() const { return m_anHost->text().trimmed(); }
quint16 CabIoSimWindow::analogPort() const { return quint16(m_anPort->value()); }

void CabIoSimWindow::sendAnalogNow()
{
    QVector<quint32> raw;
    for (int ch = 0; ch < kAnalogChannels; ++ch) raw << analogRawFor(analogMa(ch));
    for (quint8 id : analogCards()) {
        const QByteArray bytes = analogData(id, m_anSeq++, raw, m_wf);
        // Session 350: over UDP, never the serial port. A failure is said
        // by UdpSender::error and the frame is not counted or logged.
        if (!m_anUdp->sendOnce(bytes, analogHost(), analogPort())) continue;
        ++m_analogSent;
        Frame f;
        if (parseAnalog(bytes, &f)) logFrame(true, f);
    }
}

// ---- traffic -------------------------------------------------------------------------------

void CabIoSimWindow::write(const QByteArray &bytes)
{
    if (m_link && m_link->isOpen()) m_link->write(bytes);
    else if (m_sink) m_sink(bytes);
    Frame f;
    if (parse(bytes, &f) || parseAnalog(bytes, &f)) logFrame(true, f);
}

void CabIoSimWindow::feedBytes(const QByteArray &bytes)
{
    for (const Frame &f : m_scanner.feed(bytes)) onFrame(f);
}

void CabIoSimWindow::onFrame(const Frame &f)
{
    logFrame(false, f);
    m_lastCrcMatches = f.crcMatches;
    m_crcSeen = true;
    const qint64 now = nowMs();

    if (f.isInputRequest()) {
        ++m_requests;
        const int bi = bankFor(f.h.dest);
        if (bi < 0) { ++m_requestsOther; return; }
        if (m_respond != Respond::Answer) return;
        Bank &b = m_banks[bi];
        write(replyTo(f, b.bank.word(), m_health, m_wf, m_ownSeq++));
        ++b.replies;
        ++m_replies;
        b.lastReplyMs = now;
        return;
    }
    if (f.isOutputData()) {
        const quint16 w = f.outputWord();
        OutDest &d = m_outDest[f.h.dest];
        d.word = w;
        d.seq = f.h.seq;
        d.atMs = now;
        ++d.frames;
        refreshReadback();
        // The lamps follow the lowest output id heard from: the two output
        // cards are sent the same word, and a difference is reported, not
        // drawn as changes.
        if (f.h.dest != m_outDest.firstKey()) return;
        if (m_outFrames > 0) {
            const quint16 changed = quint16(w ^ m_outWord);
            for (int bit = 0; bit < 16; ++bit) {
                if (!(changed & (1u << bit))) continue;
                ++m_outChanges[bit];
                m_outChangedAtMs[bit] = now;
            }
        }
        m_outWord = w;
        m_outAtMs = now;
        ++m_outFrames;
        refreshOutputs();
        return;
    }
    if (f.isOutputRequest()) {
        ++m_outRequests;
        const quint8 card = readbackCard(f.h.dest);
        if (!m_rbAnswer || !readbackCards().contains(card)) { refreshReadback(); return; }
        // An Auto pin follows what the card was last sent: never sent
        // anything, it has nothing to read back, and is not made to invent
        // it -- unless every pin is Manual.
        bool known = false;
        const quint16 word = readbackWord(card, &known);
        if (!known && m_rb.manualCount() < kPins) { ++m_readbackUnanswered; refreshReadback(); return; }
        write(readbackTo(f, m_rb.pins(word), m_rbHealth, m_wf));
        ++m_readbacksSent;
        refreshReadback();
        return;
    }
    if (f.isReadback()) {
        m_readbackPinsHeard = f.pins();
        ++m_readbacksHeard;
        refreshOutputs();
        return;
    }
    ++m_otherFrames;
}

void CabIoSimWindow::sendNow()
{
    for (int bi = 0; bi < m_banks.size(); ++bi) {
        Bank &b = m_banks[bi];
        for (quint8 id : bankIds(bi)) {
            write(inputReply(id, kVccId, m_ownSeq++, b.bank.word(), m_health, m_wf));
            ++b.replies;
            ++m_replies;
            b.lastReplyMs = nowMs();
        }
    }
}

void CabIoSimWindow::onPeriodic()
{
    if (m_respond == Respond::Periodic) sendNow();
}

void CabIoSimWindow::tickFlicker()
{
    bool any = false;
    for (Bank &b : m_banks) {
        if (!b.bank.flickerCount()) continue;
        b.bank.tick();
        any = true;
    }
    if (any) refreshInputs();
}

// ---- the views ---------------------------------------------------------------------------

void CabIoSimWindow::refreshInputs()
{
    if (m_banks.isEmpty()) return;
    const qint64 now = nowMs();
    QStringList answering;
    int flickering = 0;
    for (int bi = 0; bi < m_banks.size(); ++bi) {
        Bank &b = m_banks[bi];
        for (int i = 0; i < b.tiles.size(); ++i)
            b.tiles[i]->setValues(b.bank.liveState(i), b.bank.flickering(i));
        b.word->setText(hex32(b.bank.word()));
        const int fl = b.bank.flickerCount();
        flickering += fl;
        QString c = tr("%1 sent").arg(count(b.replies, "reply", "replies"));
        if (b.lastReplyMs) c += tr(", last %1").arg(ago(b.lastReplyMs, now));
        if (fl) c += tr(" \u00B7 %1 flickering").arg(fl);
        b.counts->setText(c);
        answering << tr("%1 for ids %2").arg(b.bank.layout()->title, idRange(bankIds(bi)));
    }
    QString sum = m_respond == Respond::Answer
        ? tr("Answering input requests \u2014 %1.").arg(answering.join(QStringLiteral("; ")))
        : tr("Sending every %1 ms, unasked \u2014 %2.").arg(m_period->value()).arg(answering.join(QStringLiteral("; ")));
    sum += tr(" %1 heard").arg(count(m_requests, "request", "requests"));
    if (m_requestsOther) sum += tr(" (%1 for ids not ticked)").arg(m_requestsOther);
    sum += tr(", %1 sent.").arg(count(m_replies, "reply", "replies"));
    if (flickering) sum += tr(" Flicker: every %1 ms.").arg(m_flickerTimer->interval());
    m_inputsSummary->setText(sum);
    m_preview->setText(nextReplyText());
}

QString CabIoSimWindow::nextReplyText() const
{
    for (int bi = 0; bi < m_banks.size(); ++bi) {
        const QVector<quint8> ids = bankIds(bi);
        if (ids.isEmpty()) continue;
        const QByteArray f = inputReply(ids.first(), kVccId, m_ownSeq, m_banks.at(bi).bank.word(), m_health, m_wf);
        return tr("Next reply from %1:  %2").arg(unitName(ids.first()), hex(f));
    }
    return tr("No id is ticked: nothing is sent.");
}

void CabIoSimWindow::refreshOutputs()
{
    const qint64 now = nowMs();
    const bool known = m_outFrames > 0;
    for (LampTile *l : m_lamps) {
        const int bit = l->bit().bit;
        const bool recent = m_outChangedAtMs.value(bit) && now - m_outChangedAtMs.value(bit) < 1000;
        l->setValues(known, known && outputActive(l->bit(), m_outWord), m_outChanges.value(bit), recent,
                     bit < m_readbackPinsHeard.size() ? int(quint8(m_readbackPinsHeard.at(bit))) : -1);
    }
    if (!known) {
        m_outHeadline->setText(tr("Waiting for output data from the VCC"));
        m_outDetail->setText(tr("%1 \u2014 the frames the VCC sends to output cards 13 and 14 "
                                "(CC CC \u2026 DD DD, message 0x2D).").arg(outputStructName(m_cab)));
        m_outBytes->clear();
    } else {
        const bool stale = now - m_outAtMs > 2000;
        m_outHeadline->setText(stale ? tr("Output data stopped %1").arg(ago(m_outAtMs, now))
                                     : tr("Receiving output data"));
        m_outHeadline->setStyleSheet(stale ? UiColor::warningStyle() : QString());
        QStringList per;
        QSet<quint16> words;
        for (auto it = m_outDest.cbegin(); it != m_outDest.cend(); ++it) {
            per << tr("\u2192 %1: %2, seq %3, %4").arg(unitName(it.key()), hex16(it.value().word))
                       .arg(it.value().seq).arg(ago(it.value().atMs, now));
            words.insert(it.value().word);
        }
        QString d = per.join(QStringLiteral("   \u00B7   "));
        if (words.size() > 1) d += tr("   \u2014 the output cards were sent different words; the lamps show %1.")
                                       .arg(unitName(m_outDest.firstKey()));
        m_outDetail->setText(d);
        m_outBytes->setText(tr("data_byte1 %1   data_byte2 %2   \u00B7   %3")
                                .arg(hex8(quint8(m_outWord & 0xFF)), hex8(quint8(m_outWord >> 8)),
                                     outputStructName(m_cab)));
    }
    m_outLegend->setText(m_cab == Cab::Old
        ? tr("A lamp is lit when its output acts: every output acts on 1 (relay energised).")
        : tr("A lamp is lit when its output acts: the buzzers, the brakes and the SIFA valve act on 0 "
             "(sounding, applied, open); every other output acts on 1 (relay energised)."));
    QString foot = count(m_outFrames, "output data frame", "output data frames");
    foot += tr(" \u00B7 %1 (0x1D) heard").arg(count(m_outRequests, "readback request", "readback requests"));
    if (m_readbacksSent) foot += tr(", %1").arg(count(m_readbacksSent, "readback sent", "readbacks sent"));
    if (m_readbackUnanswered)
        foot += tr(", %1 not answered (no output data sent to that card yet)").arg(m_readbackUnanswered);
    if (m_readbacksHeard) foot += tr(" \u00B7 %1 from a card on the line").arg(count(m_readbacksHeard, "readback", "readbacks"));
    if (m_otherFrames) foot += tr(" \u00B7 %1: see Frames").arg(count(m_otherFrames, "other frame", "other frames"));
    m_outFooter->setText(foot + QLatin1Char('.'));
}

QString CabIoSimWindow::crcHint() const
{
    if (!m_crcSeen) return QString();
    if (m_lastCrcMatches.isEmpty())
        return tr("The last frame's CRC matches none of the algorithms here.");
    QStringList names;
    for (Crc c : m_lastCrcMatches) names << crcName(c);
    return tr("The last frame's CRC matches %1.").arg(names.join(QStringLiteral(", ")));
}

void CabIoSimWindow::onUiTick()
{
    refreshPort();
    flushLog();
    // Ages and the "changed" highlight move on with time.
    refreshInputs();
    refreshOutputs();
    if (!m_crcSeen) {
        m_crcNote->setText(tr("No frame received yet. Each one is checked against every algorithm above."));
        m_crcNote->setStyleSheet(UiColor::mutedStyle());
        m_useCrc->setVisible(false);
    } else {
        const bool agrees = m_lastCrcMatches.contains(m_wf.crc);
        m_crcNote->setText(crcHint() + (agrees ? QString() : tr(" Not the one chosen.")));
        m_crcNote->setStyleSheet(agrees ? UiColor::okStyle() : UiColor::warningStyle());
        m_useCrc->setVisible(!agrees && !m_lastCrcMatches.isEmpty());
    }
    m_logNote->setText(m_scanner.skippedBytes()
                           ? tr("%1 outside any frame skipped").arg(count(m_scanner.skippedBytes(), "byte", "bytes"))
                           : QString());
}

void CabIoSimWindow::logFrame(bool tx, const Frame &f)
{
    if (m_pause && m_pause->isChecked()) return;
    QString data;
    if (f.isInputReply())
        data = tr("%1  health %2").arg(hex32(f.inputWord()), hex8(f.health()));
    else if (f.isOutputData())
        data = hex16(f.outputWord());
    else if (f.isReadback())
        data = tr("%1  health %2").arg(hex(f.pins()), hex8(f.health()));
    else if (f.isAnalog()) {
        QStringList mA;
        for (int ch = 0; ch < kAnalogChannels; ++ch) mA << QString::number(CabIo::analogMa(f.analogRaw(ch)), 'f', 3);
        data = mA.join(QLatin1Char(' ')) + tr(" mA");
    }
    else
        data = f.payload.isEmpty() ? QStringLiteral("\u2014") : hex(f.payload);
    QString crc;
    int kind = 0;
    if (f.crcMatches.contains(m_wf.crc)) {
        crc = QStringLiteral("\u2713 ") + crcShortName(m_wf.crc);
    } else if (!f.crcMatches.isEmpty()) {
        crc = tr("\u2717 is %1").arg(crcShortName(f.crcMatches.first()));
        kind = 1;
    } else {
        crc = tr("\u2717 matches none");
        kind = 1;
    }
    QString name = msgName(f.h.msgId);
    if (!f.known) name += tr(" \u00B7 by end bytes");
    m_pendingRows.append({ QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")),
                           tx ? QStringLiteral("TX") : QStringLiteral("RX"),
                           name,
                           tr("%1 \u2192 %2").arg(unitName(f.h.src), unitName(f.h.dest)),
                           QString::number(f.h.seq), data, crc, hex(f.raw) });
    m_pendingKinds.append(kind);
}

void CabIoSimWindow::flushLog()
{
    if (m_pendingRows.isEmpty()) return;
    const int kMaxRows = 2000;
    const bool atBottom = m_log->verticalScrollBar() == nullptr
                          || m_log->verticalScrollBar()->value() >= m_log->verticalScrollBar()->maximum() - 2;
    m_log->setUpdatesEnabled(false);
    for (int r = 0; r < m_pendingRows.size(); ++r) {
        const QStringList &cols = m_pendingRows.at(r);
        const int row = m_log->rowCount();
        m_log->insertRow(row);
        for (int c = 0; c < 8; ++c) {
            auto *it = new QTableWidgetItem(cols.at(c));
            if (c == 7) it->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
            if (c == 6 && m_pendingKinds.at(r)) it->setForeground(UiColor::warning());
            m_log->setItem(row, c, it);
        }
    }
    m_pendingRows.clear();
    m_pendingKinds.clear();
    const int extra = m_log->rowCount() - kMaxRows;
    for (int i = 0; i < extra; ++i) m_log->removeRow(0);
    m_log->setUpdatesEnabled(true);
    if (atBottom) m_log->scrollToBottom();
}

int CabIoSimWindow::logRows() const { return m_log->rowCount(); }
