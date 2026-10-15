#ifndef RECENTFILES_H
#define RECENTFILES_H

// =============================================================================
//  recentfiles.h -- File ▸ Open Recent and drag-and-drop (session 287)
//  ---------------------------------------------------------------------------
//  The files the operator opened last, newest first, kept in the INI
//  (`recent/files`): recorded sessions (.dlr), captures for replay (.cap)
//  and Tag Builder routes (.tagroute.xml). kindOf() says which a path is, so
//  a dropped file opens where it belongs; anything else is not opened.
// =============================================================================

#include <QString>
#include <QStringList>

namespace RecentFiles {

enum class Kind { Unknown, Session, Capture, TagRoute };

constexpr int kMax = 10;

// By extension, case-insensitive. A route.xml or Configuration1.xml is
// Unknown: an .xml could as well be kavach.xml.
Kind kindOf(const QString &path);
// "Recorded session", "Capture (replay)", "Tag route".
QString kindName(Kind k);

// `list` with `path` moved (or put) in front, at most `max` long. Paths are
// compared after cleaning; on Windows, case-insensitively.
QStringList added(const QStringList &list, const QString &path, int max = kMax);

// The list in the INI.
QStringList list();
void add(const QString &path);
void addAll(const QStringList &paths);
void clear();

}  // namespace RecentFiles

#endif  // RECENTFILES_H
