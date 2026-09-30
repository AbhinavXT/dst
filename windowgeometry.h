#ifndef WINDOWGEOMETRY_H
#define WINDOWGEOMETRY_H

// =============================================================================
//  WindowGeometry
//  -----------------------------------------------------------------------------
//  Remembers each secondary window's size and position across openings.
//
//  WHY THIS AND NOT DOCKING
//    The nine secondary windows are free-floating, and the tempting fix is
//    to make them dockable panes of the main window. That is a large
//    refactor of ten working, tested windows, and it trades away something
//    real: on a two-monitor desk — which is how this tool is actually used
//    — a floating Merged view on the second screen beside the live tabs is
//    better than a tab inside the main window, not worse.
//
//    The genuine daily annoyance is smaller and entirely fixable: every one
//    of these windows hard-codes its size and opens in whatever position
//    the window manager picks, discarding whatever you set last time.
//    Open the field plot, size it, close it, open it again — back to
//    860x400 in the middle of the screen. Every time.
//
//    So: keep them floating, make them remember. Additive, no refactor,
//    and it removes most of the friction that made docking attractive.
//
//  USAGE
//    In the constructor, after the default resize():
//        WindowGeometry::restore(this, "fieldPlot");
//    and in the destructor (or closeEvent):
//        WindowGeometry::save(this, "fieldPlot");
//
//    The key must be stable across versions; it is what identifies the
//    window in the INI.
//
//  OFF-SCREEN GUARD
//    A window restored to coordinates that no longer exist — the second
//    monitor was unplugged, or the laptop is now docked differently — is
//    invisible and unreachable, with no indication anything opened. That is
//    a far worse failure than forgetting the position, so a restored
//    geometry is only applied if it lands on a screen that currently
//    exists.
// =============================================================================

#include <QString>

class QWidget;

namespace WindowGeometry {

// Apply the stored geometry for `key`, if there is one and it is still
// on-screen. Returns true if anything was applied — callers that want a
// sensible default should resize() BEFORE calling this.
// Give a tool window the frame an operator expects: minimise, maximise and
// close, and a system menu.
//
// A QDialog does not get maximise on most window managers, and several of
// this program's real work surfaces are QDialogs — the Packet Maker with a
// hundred and seventy header fields, the Decode Workbench, the fault panel.
// Setting Qt::Window alone (which is what they did) makes them independent
// windows but does not add the buttons: the hint set is separate. Anything
// an operator will resize should call this.
//
// Must be called BEFORE the window is first shown: changing window flags on
// a visible widget hides it.
void makeResizableWindow(QWidget *w);

bool restore(QWidget *w, const QString &key);

void save(const QWidget *w, const QString &key);

}  // namespace WindowGeometry

#endif // WINDOWGEOMETRY_H
