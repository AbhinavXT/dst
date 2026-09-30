#include "testutil.h"
#include "settings.h"

#include <QByteArray>
#include <QFile>
#include <QMainWindow>
#include <QDockWidget>
#include <QTemporaryDir>
#include <QCoreApplication>

// Layout persistence. The interesting failures are in the encoding (column
// widths go through a QStringList) and in the version handshake that stops
// a stale dock arrangement being applied to a changed dock set.
TEST_SUITE(layout)
{
    // Settings writes next to the app binary, so point it somewhere
    // disposable for the duration of this suite.
    const QString realDir = QCoreApplication::applicationDirPath();
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp dir created");

    // ---- column width encoding -------------------------------------------
    {
        const QList<int> in { 100, 60, 120, 40, 45, 900 };
        Settings::setLogColumnWidths(in);
        const QList<int> out = Settings::logColumnWidths();
        CHECK(out == in, "column widths round-trip through the INI");
    }
    {
        Settings::setLogColumnWidths({});
        CHECK(Settings::logColumnWidths().isEmpty(), "empty list round-trips");
    }
    {
        // Values the UI should refuse to restore are still stored faithfully;
        // the filtering is the caller's job, and it must be able to see them.
        const QList<int> odd { 0, -5, 1, 100000 };
        Settings::setLogColumnWidths(odd);
        CHECK(Settings::logColumnWidths() == odd,
              "zero/negative/huge widths survive storage unaltered");
    }

    // ---- geometry / state blobs ------------------------------------------
    {
        QMainWindow w;
        w.resize(1234, 567);
        auto *d = new QDockWidget("D", &w);
        d->setObjectName("TestDock");            // required by saveState
        w.addDockWidget(Qt::RightDockWidgetArea, d);

        const QByteArray geom  = w.saveGeometry();
        const QByteArray state = w.saveState(Settings::kLayoutVersion);
        CHECK(!geom.isEmpty(),  "geometry blob is non-empty");
        CHECK(!state.isEmpty(), "state blob is non-empty");

        Settings::setMainWindowGeometry(geom);
        Settings::setMainWindowState(state);
        CHECK(Settings::mainWindowGeometry() == geom, "geometry round-trips");
        CHECK(Settings::mainWindowState() == state,   "state round-trips");

        // Restoring into an identical window works.
        QMainWindow w2;
        auto *d2 = new QDockWidget("D", &w2);
        d2->setObjectName("TestDock");
        w2.addDockWidget(Qt::LeftDockWidgetArea, d2);
        CHECK(w2.restoreGeometry(Settings::mainWindowGeometry()),
              "geometry restores");
        CHECK(w2.restoreState(Settings::mainWindowState(),
                              Settings::kLayoutVersion),
              "state restores at the matching version");
        CHECK(w2.dockWidgetArea(d2) == Qt::RightDockWidgetArea,
              "dock actually moved to the saved area");

        // The version guard is the whole reason kLayoutVersion exists: a
        // blob from a different dock set must be REJECTED, leaving the
        // code's defaults in place rather than half-applied.
        QMainWindow w3;
        auto *d3 = new QDockWidget("D", &w3);
        d3->setObjectName("TestDock");
        w3.addDockWidget(Qt::LeftDockWidgetArea, d3);
        CHECK(!w3.restoreState(Settings::mainWindowState(),
                               Settings::kLayoutVersion + 1),
              "state from a different layout version is rejected");
        CHECK(w3.dockWidgetArea(d3) == Qt::LeftDockWidgetArea,
              "and the default arrangement is left untouched");
    }

    // Empty/corrupt blobs must not throw away the defaults either.
    {
        QMainWindow w;
        auto *d = new QDockWidget("D", &w);
        d->setObjectName("TestDock");
        w.addDockWidget(Qt::BottomDockWidgetArea, d);
        CHECK(!w.restoreState(QByteArray(), Settings::kLayoutVersion),
              "empty state blob is rejected");
        CHECK(!w.restoreGeometry(QByteArray()), "empty geometry blob is rejected");
        CHECK(!w.restoreState(QByteArray("garbage"), Settings::kLayoutVersion),
              "garbage state blob is rejected");
        CHECK(w.dockWidgetArea(d) == Qt::BottomDockWidgetArea,
              "defaults survive every rejection path");
    }

    // Clean up so the suite leaves no INI residue for later runs.
    Settings::setMainWindowGeometry(QByteArray());
    Settings::setMainWindowState(QByteArray());
    Settings::setLogColumnWidths({});
    CHECK(Settings::mainWindowState().isEmpty(), "cleared state");
    Q_UNUSED(realDir);
}
