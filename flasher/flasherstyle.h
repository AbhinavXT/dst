#ifndef FLASHERSTYLE_H
#define FLASHERSTYLE_H
// =============================================================================
//  flasherstyle.{h,cpp} -- how the Firmware Flasher's screens look, derived
//  from DLConsole's theme.
//
//  THE HANDOFF'S COLOURS, MAPPED
//    The design canvas carries its own token table (#1F4E79 primary, #D98A3D
//    repair, #A61B1B danger, a near-black app bar). The handoff says: if
//    DLConsole already has a theme, keep its fonts and map these colours
//    onto its palette. It does -- Ayu Light / Ayu Dark with measured
//    contrast, enforced by the contrastaudit suite, which fails the build on
//    any colour literal outside uicolors/theme/uistyle. So every colour here
//    is one of UiColor's meanings or a palette role:
//
//      design token              ->  here
//      primary / held blocks     ->  UiColor::accent()
//      accepted                  ->  UiColor::ok()
//      repair / active card      ->  UiColor::warning()
//      warning (pre-flight)      ->  UiColor::warning()
//      danger / failed / abort   ->  UiColor::error()
//      muted captions            ->  UiColor::muted()
//      borders                   ->  UiColor::frame()
//      "keep powered" amber bar  ->  UiColor::banner*()  (the caution strip
//                                    Packet Maker already uses for "this
//                                    transmits to live equipment")
//      tints (badge fills)       ->  the ink blended into palette Base
//
//    What this gives up: the canvas's exact look. What it keeps: the flasher
//    follows the theme toggle, passes the contrast floor in both themes, and
//    does not look like a second application bolted onto the first.
//
//  All functions read the palette in force when they are called, so callers
//  re-apply them from UiColor::onThemeChange().
// =============================================================================
#include <QColor>
#include <QFont>
#include <QString>

class QLabel;
class QWidget;

namespace FlasherStyle {

// Meanings, so the pages never reach for UiColor directly and the mapping
// above lives in one place.
QColor primary();       // held blocks, primary actions, the active step
QColor accepted();      // a card the board accepted
QColor active();        // the card being flashed, repaired blocks
QColor attention();     // pre-flight issues, name mismatches
QColor danger();        // failures, abort
QColor muted();         // captions and labels
QColor border();        // card outlines
QColor notSent();       // block-map cells beyond the send cursor

// `ink` mixed into the palette's Base at `strength` (0..1). Used for badge
// fills and highlighted rows: light enough that `ink` itself stays readable
// on top of it.
QColor tint(const QColor &ink, double strength = 0.14);

// Stylesheets. Each returns a complete rule for the given object.
QString badgeSheet(const QColor &ink);            // rounded pill: ink on tint(ink)
QString cardSheet(const QString &objectName);     // a panel: Base fill, frame border, 10 px radius
QString tintedPanelSheet(const QString &objectName, const QColor &ink);

// A small upper-case section label ("TARGET", "PRE-FLIGHT").
void makeSectionLabel(QLabel *label);

// A big monospace number for the stats cards.
QFont statValueFont();

// Relative font sizes, so the pages scale with DLConsole's own font choice
// rather than fixing pixel sizes.
QFont scaledFont(const QFont &base, double factor, bool bold = false);

}  // namespace FlasherStyle

#endif  // FLASHERSTYLE_H
