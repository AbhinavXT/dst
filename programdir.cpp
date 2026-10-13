#include "programdir.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

QString programDirFor(const QString &appDirPath, const QString &windowsPackage,
                      const QString &appImage, const QString &appImageMount)
{
    if (!windowsPackage.isEmpty() && QFileInfo(windowsPackage).isFile())
        return QFileInfo(windowsPackage).absolutePath();

    if (!appImage.isEmpty() && !appImageMount.isEmpty()) {
        const QString mount = QDir::cleanPath(appImageMount);
        const QString app = QDir::cleanPath(appDirPath);
        if (app == mount || app.startsWith(mount + QLatin1Char('/')))
            return QFileInfo(appImage).absolutePath();
    }
    return appDirPath;
}

QString programDir()
{
    return programDirFor(QCoreApplication::applicationDirPath(),
                         qEnvironmentVariable("DLCONSOLE_PACKAGE"),
                         qEnvironmentVariable("APPIMAGE"),
                         qEnvironmentVariable("APPDIR"));
}
