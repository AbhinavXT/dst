#include "testutil.h"

#include "settings.h"
#include "workspacesnapshot.h"
#include "tabpopoutwindow.h"

#include <QFile>
#include <QJsonDocument>
#include <QSettings>
#include <QTemporaryDir>

// =============================================================================
//  Window layouts and crash recovery share one value: WorkspaceSnapshot
//  (session 79). This covers the value, the layouts file, and the crash /
//  clean-exit bookkeeping. The real main window taking a layout and
//  recovering after a crash is in the menu audit.
// =============================================================================

namespace {

WorkspaceSnapshot sample()
{
    WorkspaceSnapshot s;
    s.mainGeometry = QByteArray("\x01\x02geom", 6);
    s.mainState    = QByteArray("state\x00\xff", 7);
    s.tabs = { { QStringLiteral("21_1"), QStringLiteral("L1_V1"), true },
               { QStringLiteral("81_1"), QStringLiteral("Station, 81"), true },
               { QStringLiteral("7_0"),  QString(), false } };
    s.activeTab = QStringLiteral("81_1");
    s.popouts   = QStringList{ QStringLiteral("21_1") };
    s.popoutGeometry.insert(QStringLiteral("21_1"), QByteArray("popgeom"));
    s.savedAt = QDateTime::currentDateTimeUtc();
    return s;
}

}  // namespace

TEST_SUITE(workspacesnapshot)
{
    // ---- the value -----------------------------------------------------------
    {
        const WorkspaceSnapshot s = sample();
        bool ok = false;
        const WorkspaceSnapshot back = WorkspaceSnapshot::fromJson(s.toJson(), &ok);
        CHECK(ok, "a snapshot reads back from its own JSON");
        CHECK(back.sameAs(s), "tabs, order, visibility, active tab, pop-outs and every geometry survive");
        CHECK(back.mainState == s.mainState, "binary state (with a zero byte) survives base64");
        CHECK(s.summary() == QLatin1String("2 tabs (1 hidden), 1 pop-out"), "the summary counts what an operator sees");

        WorkspaceSnapshot moved = s;
        moved.savedAt = moved.savedAt.addSecs(60);
        CHECK(moved.sameAs(s), "when it was taken is not part of what it is");
        moved.tabs[0].visible = false;
        CHECK(!moved.sameAs(s), "hiding a tab is a change");

        const QStringList records = s.workspaceRecords();
        CHECK(records.size() == 3 && records.at(0) == QStringList({ "21_1", "L1_V1", "1" }).join(QChar(0x1F)),
              "the workspace record format is the one saveWorkspace() writes");
        CHECK(WorkspaceSnapshot::tabsFromRecords(records) == s.tabs, "and reads back to the same tabs");

        QJsonObject damaged = s.toJson();
        damaged.remove(QStringLiteral("tabs"));
        WorkspaceSnapshot::fromJson(damaged, &ok);
        CHECK(!ok, "a snapshot with no tab list is refused");
    }

    // ---- named layouts ---------------------------------------------------------
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("window_layouts.json"));
        WindowLayoutStore store(path);
        CHECK(store.load() && store.names().isEmpty(), "no file yet: an empty store, not an error");

        WorkspaceSnapshot a = sample();
        WorkspaceSnapshot b = sample();
        b.tabs.removeLast();
        b.popouts.clear();
        b.popoutGeometry.clear();
        store.put(QStringLiteral("Bench"), a);
        store.put(QStringLiteral("Review"), b);
        store.put(QStringLiteral("  Bench  "), b);    // same name, trimmed: replaces in place
        CHECK(store.names() == QStringList({ "Bench", "Review" }), "a same-named save replaces, keeping its place");
        CHECK(store.snapshot(QStringLiteral("Bench")).sameAs(b), "with the new content");
        store.put(QString(QStringLiteral("x")).repeated(100), a);
        CHECK(store.names().last().size() == WindowLayoutStore::kMaxNameLength, "names are held to a sane length");
        store.remove(store.names().last());

        CHECK(store.save(), "saves");
        WindowLayoutStore reread(path);
        CHECK(reread.load() && reread.names() == store.names(), "and reads back in order");

        const int at = store.indexOf(QStringLiteral("Bench"));
        const WorkspaceSnapshot gone = store.snapshot(QStringLiteral("Bench"));
        store.remove(QStringLiteral("Bench"));
        store.insert(at, QStringLiteral("Bench"), gone);
        CHECK(store.names() == QStringList({ "Bench", "Review" }), "a removed layout goes back where it was (undo)");

        QFile junk(path);
        junk.open(QIODevice::WriteOnly);
        junk.write("{ \"format\": \"something-else\" }");
        junk.close();
        WindowLayoutStore wrong(path);
        CHECK(!wrong.load() && !wrong.lastError().isEmpty(), "another program's JSON is refused, with a reason");
    }

    // ---- crash recovery ---------------------------------------------------------
    {
        // The ini is the real one beside the test binary: borrow the keys and
        // put them back.
        QSettings ini(Settings::iniPath(), QSettings::IniFormat);
        const QVariant savedRunning = ini.value(QStringLiteral("session/running"));
        const QStringList savedTabs = Settings::workspaceTabs();
        const QString savedActive   = Settings::workspaceActiveTab();
        const QVariant savedPopouts = ini.value(QStringLiteral("ui/popouts"));
        const QByteArray savedGeom  = Settings::mainWindowGeometry();
        const QByteArray savedState = Settings::mainWindowState();

        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("session_recovery.json"));
        const WorkspaceSnapshot s = sample();
        CHECK(SessionRecovery::write(path, s), "the recovery snapshot writes");
        WorkspaceSnapshot back;
        CHECK(SessionRecovery::read(path, &back) && back.sameAs(s), "and reads back");

        // A clean previous run: the file is stale and is not used.
        Settings::setSessionRunning(false);
        Settings::setWorkspaceTabs({ QStringList({ "99_1", "clean", "1" }).join(QChar(0x1F)) });
        WorkspaceSnapshot recovered;
        CHECK(!SessionRecovery::adoptAfterCrash(path, &recovered), "after a clean exit nothing is recovered");
        CHECK(Settings::sessionRunning(), "but this run is marked running");
        CHECK(!QFile::exists(path), "and the stale file is removed");
        CHECK(Settings::workspaceTabs().first().startsWith(QLatin1String("99_1")), "the clean workspace is untouched");

        // The same start again, without a clean exit in between: a crash.
        SessionRecovery::write(path, s);
        CHECK(SessionRecovery::adoptAfterCrash(path, &recovered), "after an unclean exit the snapshot is recovered");
        CHECK(recovered.sameAs(s), "the one that was written");
        CHECK(Settings::workspaceTabs() == s.workspaceRecords() && Settings::workspaceActiveTab() == QLatin1String("81_1"),
              "its tabs are now what the startup restore will read");
        QSettings after(Settings::iniPath(), QSettings::IniFormat);
        CHECK(after.value(QStringLiteral("ui/popouts")).toStringList() == QStringList({ "21_1" }),
              "so are its pop-outs");
        CHECK(after.value(QStringLiteral("windows/%1_geometry").arg(TabPopoutWindow::geometryKey(QStringLiteral("21_1"))))
                  .toByteArray() == QByteArray("popgeom"),
              "and where they were");
        CHECK(Settings::mainWindowState() == s.mainState, "and the dock layout");

        SessionRecovery::write(path, s);
        SessionRecovery::markCleanExit(path);
        CHECK(!Settings::sessionRunning() && !QFile::exists(path), "a clean exit clears the flag and the file");

        // Unclean, but the file is damaged: nothing is adopted, nothing breaks.
        Settings::setSessionRunning(true);
        QFile bad(path);
        bad.open(QIODevice::WriteOnly);
        bad.write("{ not json");
        bad.close();
        CHECK(!SessionRecovery::adoptAfterCrash(path, &recovered), "a damaged recovery file is ignored");

        ini.setValue(QStringLiteral("session/running"), savedRunning);
        Settings::setWorkspaceTabs(savedTabs);
        Settings::setWorkspaceActiveTab(savedActive);
        ini.setValue(QStringLiteral("ui/popouts"), savedPopouts);
        Settings::setMainWindowGeometry(savedGeom);
        Settings::setMainWindowState(savedState);
        ini.sync();
    }
}
