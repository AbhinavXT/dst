#include "testutil.h"

#include "settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

// =============================================================================
//  Session 270 — what DLConsole keeps (the ini, the LOGS folder, the tag
//  library; bookmarks, workspace, flasher and loco-config files sit beside
//  the ini) goes beside the single-file package the operator started, not in
//  the temporary folder it runs from (deleted on close on Windows, read-only
//  in an AppImage).
// =============================================================================

TEST_SUITE(session270)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QByteArray savedPkg = qgetenv("DLCONSOLE_PACKAGE");

    // An ordinary build: unchanged.
    qunsetenv("DLCONSOLE_PACKAGE");
    CHECK(QFileInfo(Settings::iniPath()).absolutePath() == appDir,
          "no package: the ini beside the program, as before");

    // A Windows single .exe, kept in a folder of the operator's.
    QTemporaryDir where;
    CHECK(where.isValid(), "a folder for the package");
    const QString exe = where.filePath(QStringLiteral("DLConsole.exe"));
    { QFile f(exe); CHECK(f.open(QIODevice::WriteOnly), "a stand-in for the single .exe"); }
    const QString dir = QFileInfo(exe).absolutePath();
    qputenv("DLCONSOLE_PACKAGE", exe.toLocal8Bit());

    CHECK(Settings::iniPath() == dir + QStringLiteral("/dlconsole.ini"),
          "package: dlconsole.ini beside the .exe");
    CHECK(Settings::diskLogRoot() == dir + QStringLiteral("/LOGS"),
          "package: the LOGS folder beside the .exe (it was in the deleted temporary folder)");
    CHECK(Settings::tagLibraryPath() == dir + QStringLiteral("/tag_scenarios"),
          "package: the tag library beside the .exe");

    // Written there, read back from there: it survives the program.
    Settings::setUdpPort(50123);
    CHECK(QFile::exists(dir + QStringLiteral("/dlconsole.ini")), "a setting is saved beside the .exe");
    CHECK(Settings::udpPort() == 50123, "and read back from there");

    if (savedPkg.isNull()) qunsetenv("DLCONSOLE_PACKAGE");
    else qputenv("DLCONSOLE_PACKAGE", savedPkg);
    CHECK(QFileInfo(Settings::iniPath()).absolutePath() == appDir || !savedPkg.isNull(),
          "the test leaves the environment as it found it");
}
