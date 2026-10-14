#include "testutil.h"

#include "fieldcatalog.h"
#include "mainwindow.h"
#include "settings.h"

#include <QCoreApplication>
#include <QDockWidget>
#include <QFile>
#include <QTemporaryDir>

// =============================================================================
//  Session 275 — first-run defaults: UDP port 51234, the Sage theme, no panel
//  open, and only the Time and Message columns. A fresh dlconsole.ini comes
//  from pointing DLCONSOLE_PACKAGE at an empty folder (session 270).
// =============================================================================

TEST_SUITE(session275)
{
    const QByteArray savedPkg = qgetenv("DLCONSOLE_PACKAGE");
    QTemporaryDir where;
    CHECK(where.isValid(), "a folder for a fresh dlconsole.ini");
    const QString exe = where.filePath(QStringLiteral("DLConsole.exe"));
    { QFile f(exe); CHECK(f.open(QIODevice::WriteOnly), "a stand-in for the .exe"); }
    qputenv("DLCONSOLE_PACKAGE", exe.toLocal8Bit());
    CHECK(!QFile::exists(Settings::iniPath()), "fixture: no dlconsole.ini yet");

    CHECK(Settings::udpPort() == 51234, "the UDP port is 51234");
    CHECK(Settings::theme() == QLatin1String("sage"), "the theme is Sage");
    const QList<int> hidden = Settings::hiddenColumns();
    CHECK(hidden.size() == 4 && hidden.contains(1) && hidden.contains(2) && hidden.contains(3)
              && hidden.contains(4) && !hidden.contains(0) && !hidden.contains(5),
          "Source, Name, Dir and Sev hidden: Time and Message shown");

    // The operator's choice still wins, even "show every column".
    Settings::setHiddenColumns({});
    CHECK(Settings::hiddenColumns().isEmpty(), "an empty saved list stays empty");
    Settings::setUdpPort(50002);
    Settings::setTheme(QStringLiteral("dark"));
    CHECK(Settings::udpPort() == 50002 && Settings::theme() == QLatin1String("dark"),
          "a saved port and theme are kept");
    QFile::remove(Settings::iniPath());

    // MainWindow loads the field catalogue, a process-wide singleton, which
    // later suites (assertions) expect to find as the runner left it.
    const bool catalogWasLoaded = FieldCatalog::instance().isLoaded();
    {
        MainWindow w;
        int open = 0;
        QStringList names;
        for (QDockWidget *d : w.findChildren<QDockWidget *>()) {
            if (d->isVisibleTo(&w) || !d->isHidden()) { ++open; names << d->objectName(); }
        }
        CHECK(open == 0, QByteArray("no panel open on a first start (open: ")
                             + names.join(QStringLiteral(", ")).toUtf8() + ")");
    }
    if (!catalogWasLoaded) FieldCatalog::instance().clear();

    if (savedPkg.isNull()) qunsetenv("DLCONSOLE_PACKAGE");
    else qputenv("DLCONSOLE_PACKAGE", savedPkg);
}
