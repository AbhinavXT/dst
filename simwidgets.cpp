#include "simwidgets.h"

#include "theme.h"
#include "uicolors.h"

#include <QButtonGroup>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QRadialGradient>

namespace {

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
    case Tile::On:    return blend(base, SimUi::mark(), 0.08);
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

namespace SimUi {

QColor fill()
{
    return QGuiApplication::palette().color(QPalette::Highlight);
}

QColor fillText()
{
    return QGuiApplication::palette().color(QPalette::HighlightedText);
}

QColor mark()
{
    return UiColor::withContrast(fill(), QGuiApplication::palette().color(QPalette::Base),
                                 CabIoColors::graphicFloor());
}

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

void paintLamp(QPainter &p, const QRectF &lamp, const QString &colour, bool known, bool on)
{
    const QColor c = UiColor::signalLamp(colour);
    const double glowR = lamp.width() * 0.77;
    if (known && on) {
        QRadialGradient glow(lamp.center(), glowR);
        QColor g = c; g.setAlpha(90);
        QColor t = c; t.setAlpha(0);
        glow.setColorAt(0.55, g);
        glow.setColorAt(1.0, t);
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(lamp.center(), glowR, glowR);
        QRadialGradient body(lamp.center() - QPointF(lamp.width() * 0.15, lamp.width() * 0.15), lamp.width() * 0.62);
        body.setColorAt(0, c.lighter(150));
        body.setColorAt(1, c);
        p.setBrush(body);
        // A rim the card can see: a yellow or white lamp on a light
        // theme is otherwise a faint disc (session 328).
        p.setPen(QPen(CabIoColors::lampRim(colour), 1.5));
        p.drawEllipse(lamp);
    } else {
        p.setBrush(QGuiApplication::palette().color(QPalette::Window));
        QPen pen(UiColor::muted(), 1.5);
        if (!known) pen.setStyle(Qt::DotLine);
        p.setPen(pen);
        p.drawEllipse(lamp);
    }
}

QFont scaled(const QFont &base, double factor, int weight)
{
    QFont f = base;
    if (f.pointSizeF() > 0) f.setPointSizeF(f.pointSizeF() * factor);
    if (weight >= 0) f.setWeight(QFont::Weight(weight));
    return f;
}

QString baseStyleSheet()
{
    const QPalette pal = QGuiApplication::palette();
    const QString frame  = UiColor::frame().name();
    const QString fillColour = fill().name();
    const QString fillInk = fillText().name();
    const QString base   = pal.color(QPalette::Base).name();
    const QString text   = pal.color(QPalette::Text).name();
    const QString muted  = UiColor::muted().name();
    return QStringLiteral(
        "QWidget#segmented QPushButton { border: 1px solid %1; padding: 5px 14px; background: %3; color: %4; border-radius: 0px; }"
        "QWidget#segmented QPushButton[seg=\"mid\"], QWidget#segmented QPushButton[seg=\"last\"] { border-left: none; }"
        "QWidget#segmented QPushButton[seg=\"first\"] { border-top-left-radius: 6px; border-bottom-left-radius: 6px; }"
        "QWidget#segmented QPushButton[seg=\"last\"] { border-top-right-radius: 6px; border-bottom-right-radius: 6px; }"
        "QWidget#segmented QPushButton:checked { background: %2; color: %6; border-color: %2; font-weight: 600; }"
        "QWidget#segmented QPushButton:disabled { color: %5; }"
        "QPushButton#primary { background: %2; color: %6; border: 1px solid %2; border-radius: 6px; padding: 5px 18px; font-weight: 600; }"
        "QPushButton#primary:disabled { background: %3; color: %5; border-color: %1; }"
        "QPushButton#idChip { border: 1px solid %1; border-radius: 11px; padding: 2px 10px; background: %3; color: %4; }"
        "QPushButton#idChip:checked { background: %2; color: %6; border-color: %2; font-weight: 600; }"
        "QPushButton#idChip:disabled { color: %5; }"
        "QPushButton#idChip:checked:disabled { background: %5; color: %3; border-color: %5; }"
        "QLabel#muted { color: %5; }"
        "QLabel#groupHeading { color: %5; font-weight: 600; letter-spacing: 1px; }"
        "QLabel#headline { font-size: 14pt; font-weight: 600; }"
        "QLabel#mono { font-family: \"Consolas\", \"Menlo\", \"DejaVu Sans Mono\", monospace; color: %5; }"
        "QFrame#card { background: %3; border: 1px solid %1; border-radius: 10px; }"
        "QLabel#cardTitle { font-size: 13pt; font-weight: 600; }")
        .arg(frame, fillColour, base, text, muted, fillInk);
}

// ---- SignalLamp -----------------------------------------------------------------

SignalLamp::SignalLamp(const QString &name, const QString &colour, QWidget *parent)
    : QWidget(parent), m_name(name), m_colour(colour),
      m_onText(QStringLiteral("on")), m_offText(QStringLiteral("off")), m_unknownText(QStringLiteral("not known yet"))
{
    setAccessibleName(name);
    setMinimumSize(56, 58);
    describe();
}

void SignalLamp::setTexts(const QString &on, const QString &off, const QString &unknown)
{
    m_onText = on;
    m_offText = off;
    m_unknownText = unknown;
    describe();
}

QSize SignalLamp::sizeHint() const { return QSize(64, 62); }

void SignalLamp::setValues(bool known, bool on)
{
    if (known == m_known && on == m_on) return;
    m_known = known;
    m_on = on;
    describe();
    update();
}

void SignalLamp::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    paintLamp(p, QRectF((width() - 28) / 2.0, 6, 28, 28), m_colour, m_known, m_on);
    p.setPen(palette().color(QPalette::Text));
    p.setFont(scaled(font(), 0.95, m_on ? QFont::Bold : QFont::Normal));
    p.drawText(QRect(0, 38, width(), 20), Qt::AlignCenter, m_name);
}

void SignalLamp::describe()
{
    const QString state = !m_known ? m_unknownText : m_on ? m_onText : m_offText;
    setAccessibleDescription(state);
    setToolTip(QStringLiteral("%1: %2").arg(m_name, state));
}

}  // namespace SimUi
