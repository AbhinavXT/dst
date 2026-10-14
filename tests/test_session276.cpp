#include "testutil.h"

#include "fieldcatalog.h"
#include "mainwindow.h"
#include "settings.h"

#include <QFile>
#include <QTabWidget>
#include <QTemporaryDir>

// =============================================================================
//  Session 276 — a tab restored at start-up takes its name from the names
//  file in force, not the one saved last session. Abhinav: on the exe, tabs
//  showed "21_1", "51_1" instead of L1_V1, Fault_L1V1. A package that once ran
//  without friendly_names.csv saved those keys as the tab names, and every
//  start restored them; traffic for an existing tab never renames it.
// =============================================================================

namespace {
QStringList tabTitles(MainWindow &w)
{
    QStringList out;
    if (QTabWidget *t = w.findChild<QTabWidget *>(QStringLiteral("tabWidget")))
        for (int i = 0; i < t->count(); ++i) out << t->tabText(i);
    return out;
}
}  // namespace

TEST_SUITE(session276)
{
    const QByteArray savedPkg = qgetenv("DLCONSOLE_PACKAGE");
    QTemporaryDir where;
    CHECK(where.isValid(), "a folder for a fresh dlconsole.ini");
    const QString exe = where.filePath(QStringLiteral("DLConsole.exe"));
    { QFile f(exe); CHECK(f.open(QIODevice::WriteOnly), "a stand-in for the .exe"); }
    qputenv("DLCONSOLE_PACKAGE", exe.toLocal8Bit());

    // The workspace an exe without names saved, plus a source the names file
    // does not know, saved under a name of its own.
    const QChar sep(0x1F);
    Settings::setWorkspaceTabs({ QStringList{ "21_1", "21_1", "1" }.join(sep),
                                 QStringList{ "51_1", "51_1", "1" }.join(sep),
                                 QStringList{ "99_3", "Bench rig", "1" }.join(sep) });
    CHECK(Settings::restoreWorkspace(), "fixture: the workspace is restored at start");

    const bool catalogWasLoaded = FieldCatalog::instance().isLoaded();
    {
        MainWindow w;
        const QStringList titles = tabTitles(w);
        const QByteArray seen = titles.join(QStringLiteral(" | ")).toUtf8();
        CHECK(titles.contains(QStringLiteral("L1_V1")) && !titles.contains(QStringLiteral("21_1")),
              QByteArray("21_1 restored as L1_V1 (") + seen + ")");
        CHECK(titles.contains(QStringLiteral("Fault_L1V1")) && !titles.contains(QStringLiteral("51_1")),
              QByteArray("51_1 restored as Fault_L1V1 (") + seen + ")");
        CHECK(titles.contains(QStringLiteral("Bench rig")),
              "a key the names file does not know keeps its saved name");
    }
    if (!catalogWasLoaded) FieldCatalog::instance().clear();

    if (savedPkg.isNull()) qunsetenv("DLCONSOLE_PACKAGE");
    else qputenv("DLCONSOLE_PACKAGE", savedPkg);
}
