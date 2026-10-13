#include "testutil.h"

#include "programdir.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

// =============================================================================
//  Session 269 — single-file packages: where "beside the program" is when the
//  program runs from a temporary folder (programdir.cpp).
// =============================================================================

TEST_SUITE(session269)
{
    QTemporaryDir where;     // where the operator keeps the package
    CHECK(where.isValid(), "a temporary folder for the package");
    const QString exe = where.filePath(QStringLiteral("DLConsole.exe"));
    {
        QFile f(exe);
        CHECK(f.open(QIODevice::WriteOnly), "a stand-in for the single .exe");
    }
    const QString tempRun = QStringLiteral("/tmp/7zS1A2B3C4D");
    const QString packageDir = QDir(where.path()).absolutePath();

    // Plain build: the program's own folder.
    CHECK(programDirFor(tempRun, QString(), QString(), QString()) == tempRun,
          "no package: the folder the program runs from");

    // Windows single .exe: the folder of the .exe the operator started.
    CHECK(programDirFor(tempRun, exe, QString(), QString()) == packageDir,
          "Windows package: beside the single .exe, not the unpack folder");
    CHECK(programDirFor(tempRun, where.filePath(QStringLiteral("gone.exe")), QString(), QString()) == tempRun,
          "Windows package that no longer exists: ignored");

    // Linux AppImage, running from inside its mount.
    const QString appImage = where.filePath(QStringLiteral("DLConsole-x86_64.AppImage"));
    const QString mount = QStringLiteral("/tmp/.mount_DLConsXYZ");
    CHECK(programDirFor(mount + QStringLiteral("/usr/bin"), QString(), appImage, mount) == packageDir,
          "AppImage: beside the .AppImage file");
    CHECK(programDirFor(mount + QStringLiteral("/usr/bin/"), QString(), appImage, mount + QStringLiteral("/")) == packageDir,
          "AppImage: trailing slashes do not matter");

    // Another AppImage's variables, inherited: not ours.
    const QString devBuild = QStringLiteral("/home/op/build/DLConsole");
    CHECK(programDirFor(devBuild, QString(), appImage, mount) == devBuild,
          "inherited APPIMAGE/APPDIR of another program: ignored");
    CHECK(programDirFor(mount + QStringLiteral("x/usr/bin"), QString(), appImage, mount) == mount + QStringLiteral("x/usr/bin"),
          "a folder that only starts with the mount's name is not inside it");
    CHECK(programDirFor(mount + QStringLiteral("/usr/bin"), QString(), appImage, QString()) == mount + QStringLiteral("/usr/bin"),
          "APPIMAGE without APPDIR: ignored");

    // The live function, in this test run (no package variables set by CI).
    if (qEnvironmentVariableIsEmpty("DLCONSOLE_PACKAGE") && qEnvironmentVariableIsEmpty("APPIMAGE"))
        CHECK(programDir() == QCoreApplication::applicationDirPath(),
              "outside a package: the program's own folder, as before");
}
