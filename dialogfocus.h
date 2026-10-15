#ifndef DIALOGFOCUS_H
#define DIALOGFOCUS_H
// =============================================================================
//  DialogFocus -- a dialog gives focus back to the window that opened it
//  (session 283).
//
//  Abhinav: "doing actions like exporting a route, exporting config xml etc
//  or cancelling them in between changes my window from tag builder to dl
//  console main window". The dialogs were parented to the Tag Builder, but
//  when a modal dialog (a file dialog above all, the native ones most)
//  closes, the platform activates the application's main window, not the
//  tool window under the dialog.
//
//  install() puts one event filter on the application. When a dialog that
//  is its own window is shown, it remembers the window to return to: its
//  parent's window, or the window that was active. When it hides -- OK or
//  Cancel alike -- that window is raised and activated again, once on the
//  next turn of the event loop and once more a moment later if the
//  platform has handed activation elsewhere in between. Nothing is done
//  while another modal dialog is up (a second file dialog in a row), or if
//  the window has gone, been hidden or minimised.
// =============================================================================
#include <QWidget>

namespace DialogFocus {

// Once, after the QApplication exists. Repeated calls do nothing.
void install();

// The window a dialog, shown now, would give focus back to (null if none).
// Exposed for the tests.
QWidget *returnTarget(const QWidget *dialog);

}  // namespace DialogFocus

#endif  // DIALOGFOCUS_H
