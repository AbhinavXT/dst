#ifndef PROFILEIODIALOGS_H
#define PROFILEIODIALOGS_H
// =============================================================================
//  profileiodialogs.{h,cpp} -- the two small dialogs profile export/import
//  needs, shared by the Loco Configuration window and the Firmware Flasher
//  (session 94).
// =============================================================================
#include "profileio.h"

#include <QStringList>

class QWidget;

namespace ProfileIo {

// Which saved items to export: a checkable list, `preselected` ticked.
// Empty when cancelled or nothing is ticked.
QStringList pickNames(QWidget *parent, const QString &title, const QString &what,
                      const QStringList &all, const QStringList &preselected);

// What to do with an imported `name` that is already used: Replace, Keep
// both, or Skip, with "Do the same for the rest". *cancelled is set when the
// dialog is closed without an answer (the import then stops).
ImportClash askClash(QWidget *parent, const QString &what, const QString &name,
                     bool *sameForRest, bool *cancelled);

}  // namespace ProfileIo

#endif // PROFILEIODIALOGS_H
