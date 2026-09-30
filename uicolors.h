#ifndef UICOLORS_H
#define UICOLORS_H
// =====================================================================
//  uicolors.{h,cpp} -- the handful of colours this program uses to mean
//  something, resolved against whichever palette is active.
//
//  WHY THIS EXISTS
//    Before it, twenty-six hex literals were spread across nine files,
//    nearly all picked by eye against the light palette. Some of them
//    fail a contrast check outright: #b9770e on the light window is
//    3.2:1, #e05a52 on the dark window is 3.8:1, #80868e is 3.2:1 —
//    below the 4.5:1 that normal text needs to stay legible. On a laptop
//    in a bright cab, "the warning text is a bit faint" is not a
//    cosmetic problem.
//
//    FaultPanelWindow had already solved this properly for itself, with
//    a dark/light pair per meaning and a changeEvent to re-apply them.
//    This is that approach extracted so the other windows can share it,
//    rather than a new scheme invented alongside it.
//
//  THE RULE
//    A colour here means something -- ok, warning, error, secondary,
//    accent. It is never chosen for looks. Anything that is decoration
//    should come from the palette directly and is not this module's
//    business.
//
//  WHAT IS DELIBERATELY NOT HERE
//    The exported HTML in testassertions.cpp and faultpanelwindow.cpp
//    writes standalone documents with their own white background. They
//    are read outside this program, in a browser or on paper, and have
//    no palette to follow. ReplayWindow paints a fixed dark canvas for
//    the same kind of reason: it is a plot, not a surface of the app.
// =====================================================================
#include <QColor>
#include <QString>
#include <QWidget>

#include <functional>

namespace UiColor {

// Is the active palette a dark one? Read from the palette rather than
// from Settings, so anything that sets a palette gets the right answer —
// including the tests, which set one directly.
bool dark();

// Colour-blind-safe verdict colours: ok / warning / error on a blue / amber /
// raspberry (sky / yellow / vermillion on dark themes) axis instead of green / orange / red,
// and accent moved off blue. Off by default; View ▸ Theme ▸ Colour-blind-safe
// status colours, stored as ui/colorBlindSafe. Call notifyThemeChanged()
// after changing it so painted surfaces pick it up.
void setColorBlindSafe(bool on);

// A lit signal lamp as drawn on the cab view: "red", "yellow", "green",
// "white". The railway's own colours, the same in every theme (and in the
// colour-blind mode: lamp position carries the meaning on a real post too).
QColor signalLamp(const QString &name);
bool colorBlindSafe();

QColor ok();        // it worked / reproduced / verified
QColor warning();   // it worked, with something the operator should read
QColor error();     // it did not work / was refused
QColor muted();     // secondary text: still has to be readable
QColor accent();    // neutral emphasis, not a verdict

// Structural lines, NOT text.
//
// These exist because the obvious thing to reach for is palette Mid, and
// Mid is a FRAME role: #DCDEE0 on the light theme, about 1.2:1 against a
// white surface. Three separate bugs came from drawing labels in it — the
// startup panel's third paragraph, the field plot's axis numbers and the
// timeline's end labels were all invisible for exactly this reason. Text
// goes in muted(); lines go here.
QColor frame();   // a border or an axis: present, not loud
QColor grid();    // a gridline: findable when looked for, otherwise quiet

// Categorical colours for plot series (curve segments, targets). Lines and
// markers only, never text, so the floor is WCAG's 3:1 for graphical objects
// rather than 4.5:1 — against the Base the plot is drawn on, in both themes.
// Cycles through seriesCount() entries; any int is accepted.
QColor series(int i);

// `ink`, moved lighter or darker (same hue) until it reaches `floor`
// contrast against `background`. Unchanged if it already does.
QColor withContrast(const QColor &ink, const QColor &background, double floor);

// The fill of a "this one is selected" mark (the flash queue's selector):
// a vivid green, not ok()'s darker text green, held to the 3:1 WCAG asks of
// a graphical object against the current theme's base.
QColor selectedMark();

// Colour tags for tabs / locos (TabTags). kTagCount of them, in the order
// the menu lists them. Each is a clear hue moved, per theme, to 3:1 against
// both the window and the base (a tag is a dot: a graphical object).
const int kTagCount = 8;
QColor  tagColor(int index);
QString tagName(int index);

// Which theme's semantic set ok()/warning()/... return (the Theme enum's
// value; theme.h calls this from ThemeUtil::apply()). An int so this header
// does not need theme.h.
void setActiveTheme(int themeId);
int  activeTheme();   // -1 until ThemeUtil::apply() has run
int    seriesCount();

// Caution banner (the "this transmits to live equipment" strips).
QColor bannerBg();
QColor bannerFg();
QColor bannerBorder();

// Stylesheet fragments, since most call sites set a QLabel's colour.
QString style(const QColor &c);   // "color:#rrggbb;"
QString okStyle();
QString warningStyle();
QString errorStyle();
QString mutedStyle();
QString accentStyle();
QString bannerStyle();            // full "QLabel { ... }" rule

// WCAG relative-luminance contrast ratio, 1.0 (identical) to 21.0
// (black on white). Public because the test suite asserts on it: colour
// choices are exactly the kind of thing that regresses silently when
// someone tunes one value by eye.
double contrastRatio(const QColor &a, const QColor &b);

// Announce that the palette has changed. ThemeUtil::apply() calls this;
// nothing else should need to.
void notifyThemeChanged();

// Call `repaint` whenever the palette under `w` changes.
//
// Without this, a window that colours itself once keeps its old colours
// after a theme toggle until it is closed and reopened — which is what
// every window except the fault panel used to do. The watcher is
// parented to the widget, so it dies with it.
void onThemeChange(QWidget *w, std::function<void()> repaint);

}  // namespace UiColor

#endif  // UICOLORS_H
