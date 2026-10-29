#ifndef SIMWIDGETS_H
#define SIMWIDGETS_H
// =============================================================================
//  The simulators' shared look (session 360): the Cab I/O Simulator and the
//  Brake Test Bench draw the same controls, lamps and cards.
//
//  CabIoColors moved here from cabiosimwindow.h (which includes this), so the
//  bench -- built with or without Qt Serial Port -- uses the same colours.
// =============================================================================
#include <QColor>
#include <QFont>
#include <QStringList>
#include <QWidget>

class QButtonGroup;
class QPainter;
class QRectF;

// The window's own colour decisions (session 328). Everything else it draws
// is a palette role or a UiColor, which the contrast audit already holds in
// every theme; these mix their own -- a tinted tile, a lamp's rim -- so they
// are public for tests/test_session328.cpp to hold to the same floors.
namespace CabIoColors {
enum class Tile { Plain, On, Fault };
double textFloor();                          // 7 : 1 in High Contrast, else 4.5 : 1
double graphicFloor();                       // 7 : 1 in High Contrast, else 3 : 1
QColor tileFill(Tile t);                     // Base, tinted for On and Fault
QColor tileInk(const QColor &ink, Tile t);   // `ink`, held to textFloor() on tileFill(t)
QColor lampRim(const QString &lamp);         // a lit lamp's outline, graphicFloor() on Base
}

namespace SimUi {

// The theme's own colour in the simulators (session 394): the palette's
// Highlight, as the rest of DLConsole selects and fills with, not the shared
// accent (sea-blue in Sage). fill / fillText: a filled button, a lit chip.
// mark(): a line or tint on Base (a dial's arc, a lit tile, a border), held to
// the graphical floor, since on the dark themes Highlight is itself dark.
QColor fill();
QColor fillText();
QColor mark();

// A row of buttons that read as one control: [ A | B | C ]. The first is checked.
QWidget *segmented(const QStringList &labels, QButtonGroup **group, QWidget *parent);

// A signal lamp in `lamp`: lit with a glow, or an empty ring (dotted when not known).
void paintLamp(QPainter &p, const QRectF &lamp, const QString &colour, bool known, bool on);

QFont scaled(const QFont &base, double factor, int weight = -1);

// A dial's scale number centred on `centre` (session 396): the box its text
// takes in `font`, not a fixed one. The dials' fonts grow with the dial, so a
// fixed 32 x 16 box cut the numbers off on a large window.
QRectF labelBox(const QFont &font, const QPointF &centre, const QString &text);
// Session 400: `font` made small enough that `widest`'s box, centred at
// `labelAt` of `radius` anywhere round the dial, keeps clear of the tick ring
// at `ringAt` (fonts differ: Linux's ran 0.004 of the radius into it).
QFont fitLabelFont(QFont font, double radius, double labelAt, double ringAt, const QString &widest);

// The rules both simulator windows start their style sheet with: segmented
// buttons, primary buttons, id chips, muted / heading / mono labels, cards.
QString baseStyleSheet();

// A lamp with its name under it (session 358's brake lamps, generalised).
// Says its state to a screen reader and in its tooltip; not "off" before
// anything is known.
class SignalLamp : public QWidget
{
public:
    SignalLamp(const QString &name, const QString &colour, QWidget *parent = nullptr);
    void setTexts(const QString &on, const QString &off, const QString &unknown);
    void setValues(bool known, bool on);
    bool isOn() const { return m_on; }
    bool isKnown() const { return m_known; }
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent *) override;
private:
    void describe();
    QString m_name, m_colour;
    QString m_onText, m_offText, m_unknownText;
    bool m_known = false, m_on = false;
};

}  // namespace SimUi

#endif  // SIMWIDGETS_H
