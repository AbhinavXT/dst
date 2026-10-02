#ifndef UISTYLE_H
#define UISTYLE_H
// =====================================================================
//  uistyle.{h,cpp} -- the shape of the window, as opposed to uicolors,
//  which is what a colour means.
//
//  Fusion out of the box is functional and cramped: controls sit tight
//  against their frames, group boxes are square-cornered boxes with a
//  hard 1px border, table headers look like buttons, and every dialog
//  picks its own margins. None of that is wrong, exactly. It just reads
//  as a form somebody assembled rather than a tool somebody designed,
//  and this program is looked at for hours at a stretch.
//
//  THE RULE HERE IS THE SAME AS IN UICOLORS
//    Every value in the stylesheet is derived from the ACTIVE PALETTE.
//    There is not one hex literal below. A stylesheet with baked-in
//    colours is how Qt applications end up with a dark mode that works
//    everywhere except the four widgets somebody styled by hand — and
//    unlike a wrong palette colour, a stylesheet silently wins over the
//    palette, so the breakage cannot be fixed from the theme.
//
//  Call apply() after ThemeUtil::apply(), both at startup and on every
//  toggle: the sheet is rebuilt from the new palette.
// =====================================================================
#include <QColor>
#include <QFont>
#include <QIcon>
#include <QString>
#include <QStringList>

class QAbstractButton;
class QButtonGroup;
class QLabel;
class QTabWidget;
class QWidget;

namespace UiStyle {

// Give a tab widget a tab bar that puts each tab's full name on its tooltip,
// so a tab can be identified when the bar is scrolled or a label does not fit.
//
// Tab WIDTH is deliberately left to Qt. It was not, briefly: the selected tab
// carried font-weight:600 and the bar reserved the difference per tab, which
// fixed a clipped label and then put scroll arrows in the middle of a
// half-empty bar, because widening tabs does not widen the bar Qt has already
// sized. The stylesheet now draws every tab at one weight, so measured and
// drawn agree and there is nothing to reserve.
//
// Must be called before any tabs are added: QTabWidget::setTabBar() replaces
// the bar wholesale.
void useTabTooltips(::QTabWidget *tabs);

// The fixed-pitch face for bytes, hex and field values. One definition,
// because "monospace" as a family name does not resolve on Windows and
// silently falls back to whatever the system decides — which is how the
// Packet Maker's buffer box ended up in a different face from every
// other hex view in the program.
QFont monoFont();

// Layout metrics. Small enough set that using them everywhere is easy.
int gap();      // between related controls
int margin();   // dialog edge
int radius();   // corner rounding

// The stylesheet, built from the palette in force when it is called.
QString sheet();

// Set the application font and the stylesheet. Safe to call repeatedly.
void apply();

// =====================================================================
//  Components (session 117: the start of the UI revamp)
//  ---------------------------------------------------------------------
//  The look of a widget is chosen by ROLE, a dynamic property the sheet
//  selects on ([dlRole="chip"]), so any window opts in with one call and
//  every theme restyles it — the same rule as above: no colour is chosen
//  here, all of it comes from the palette and UiColor. A chip's text is
//  pushed to 4.5:1 against its tinted fill (7:1 in High Contrast) by
//  UiColor::withContrast, so a role cannot ship an unreadable label.
// =====================================================================

enum class Tone { Neutral, Accent, Ok, Warn, Fail };

// A pill-shaped status chip: "● UDP 50002 · 312/s". On a QLabel or a
// button (clickable chip). setTone() changes its meaning in place.
void makeChip(QWidget *w, Tone tone = Tone::Neutral);
void setTone(QWidget *w, Tone tone);
Tone toneOf(const QWidget *w);

// The colours a chip of `tone` is drawn with in the active theme: fill and
// text (text already lifted to the contrast floor). For custom-painted
// widgets that want to match, and for the tests.
QColor chipFill(Tone tone);
QColor chipText(Tone tone);
double contrastFloor();   // 4.5, or 7.0 in High Contrast

// A segmented control: a row of exclusive, checkable buttons drawn as one
// rounded group ("All | Errors | Warnings | In | Out"). The group is
// returned through `group`, ids 0..n-1 in label order; the first is checked.
QWidget *segmented(const QStringList &labels,
                           QButtonGroup **group,
                           QWidget *parent = nullptr);

// The left icon rail's buttons: square, icon-only, a filled background when
// checked. `tip` is the tooltip AND the accessible name (an icon alone is
// not a label).
void makeRailButton(QAbstractButton *b, const QString &tip);

// The one primary action of a window or strip ("Open", "Send"): filled in
// the accent, its text lifted to the contrast floor against it.
void makePrimary(QAbstractButton *b);

// A small upper-case caption over a panel's content ("SOURCES · 6").
void makeSectionLabel(QLabel *l);

// Surfaces: a panel (raised a step from the window), and a toolbar strip
// (the band a panel's header controls sit in).
void makePanel(QWidget *w);
void makeStrip(QWidget *w);

// The fixed-pitch face for a widget's TEXT (timestamps, values, counters):
// monoFont() at the widget's own size.
void makeMono(QWidget *w);

// Spacing scale, in px: step 1 = 4, 2 = 8, 3 = 12, 4 = 16, 6 = 24.
int space(int step);

}  // namespace UiStyle

// Icons drawn from paths in the active theme's colours (no image files, no
// Qt SVG module). Names: "log", "dmi", "track", "serial", "send", "report",
// "settings", "search", "twoloco", "more", "pin", "filter".
namespace UiIcons {
QIcon icon(const QString &name, const QColor &color, int px = 18);
QIcon icon(const QString &name);            // in the palette's text colour
QStringList names();
}

#endif  // UISTYLE_H
