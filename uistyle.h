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
#include <QFont>
#include <QString>

class QTabWidget;

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

}  // namespace UiStyle

#endif  // UISTYLE_H
