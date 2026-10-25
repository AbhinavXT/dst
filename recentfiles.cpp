#include "recentfiles.h"

#include "settings.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace RecentFiles {

namespace {
const char *const kKey = "recent/files";

QString normalised(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool samePath(const QString &a, const QString &b)
{
#ifdef Q_OS_WIN
    return a.compare(b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}
}  // namespace

Kind kindOf(const QString &path)
{
    const QString lower = path.toLower();
    if (lower.endsWith(QLatin1String(".dlr"))) return Kind::Session;
    if (lower.endsWith(QLatin1String(".cap"))) return Kind::Capture;
    if (lower.endsWith(QLatin1String(".tagroute.xml"))) return Kind::TagRoute;
    if (lower.endsWith(QLatin1String(".zip"))) return Kind::ReviewPack;   // session 367
    return Kind::Unknown;
}

QString kindName(Kind k)
{
    switch (k) {
    case Kind::Session:  return QStringLiteral("Recorded session");
    case Kind::Capture:  return QStringLiteral("Capture (replay)");
    case Kind::TagRoute: return QStringLiteral("Tag route");
    case Kind::ReviewPack: return QStringLiteral("Review pack");
    case Kind::Unknown:  break;
    }
    return QStringLiteral("Unknown");
}

QStringList added(const QStringList &list, const QString &path, int max)
{
    if (path.trimmed().isEmpty()) return list.mid(0, max);
    const QString p = normalised(path);
    QStringList out{ p };
    for (const QString &old : list) {
        if (out.size() >= max) break;
        if (!samePath(normalised(old), p)) out << old;
    }
    return out;
}

QStringList list()
{
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    return s.value(kKey).toStringList().mid(0, kMax);
}

void addAll(const QStringList &paths)
{
    QStringList l = list();
    // The last added ends up first: add in reverse so paths[0] leads.
    for (int i = paths.size() - 1; i >= 0; --i) l = added(l, paths.at(i));
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    s.setValue(kKey, l);
}

void add(const QString &path) { addAll({ path }); }

void clear()
{
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    s.remove(kKey);
}

}  // namespace RecentFiles
