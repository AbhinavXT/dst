#ifndef NOTESSIDECAR_H
#define NOTESSIDECAR_H

// =============================================================================
//  NotesSidecar (session 365) — the notes on a capture, in a file beside it.
//  -----------------------------------------------------------------------------
//  Bookmarks and their notes live in bookmarks.json beside the INI: on the
//  machine where they were made. A recording sent from the test site arrived
//  without them, so "EB felt late here" stayed on the site's laptop.
//
//  Beside `Loco1_25102026_101010.dlr` this writes
//  `Loco1_25102026_101010.notes.json`: the bookmarks of the capture's tab
//  inside its span, each with its note. Reading a .dlr reads its sidecar and
//  adds the marks to the reader's own store:
//    - a mark it does not have is added, with its note;
//    - a mark it has without a note takes the file's note;
//    - a mark whose note differs keeps the reader's note, and is counted, so
//      the window can say so: nobody's words are overwritten silently.
//
//  JSON, indented, readable without the program:
//    { "format": "dlconsole-notes", "version": 1, "capture": "x.dlr",
//      "notes": [ { "tab", "epoch_ms", "time", "note", "message",
//                   "created_ms" } ] }
//  "time" is for a person reading the file; "epoch_ms" is what is read.
// =============================================================================

#include <QString>
#include <QVector>

#include "bookmarks.h"

namespace NotesSidecar {

// `x.dlr` → `x.notes.json` (any suffix: `x.cap` → `x.notes.json`).
QString pathFor(const QString &capturePath);

// The marks of tab `tabKey` from `fromMs` to `toMs` (both included), in time
// order.
QVector<Bookmark> marksIn(const QVector<Bookmark> &all, const QString &tabKey,
                          qint64 fromMs, qint64 toMs);

// Write the sidecar of `capturePath`. No marks: the sidecar is removed (a
// stale one would bring back notes taken off). False, with `err`, when the
// file cannot be written.
bool write(const QString &capturePath, const QVector<Bookmark> &marks, QString *err = nullptr);

// The sidecar of `capturePath`; empty when there is none. `err` is set only
// when there is a file and it cannot be read as one.
QVector<Bookmark> read(const QString &capturePath, QString *err = nullptr);

struct Merge {
    int added = 0;    // marks the store did not have
    int filled = 0;   // marks it had without a note, given the file's
    int differ = 0;   // marks with another note: the store's kept
};
Merge mergeInto(BookmarkStore *store, const QVector<Bookmark> &marks);

}  // namespace NotesSidecar

#endif // NOTESSIDECAR_H
