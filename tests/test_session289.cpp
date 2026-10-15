#include "testutil.h"

#include "fieldcatalog.h"
#include "mainwindow.h"
#include "messagedispatcher.h"
#include "recentfiles.h"
#include "replaywindow.h"
#include "settings.h"

#include <QApplication>
#include <QEvent>
#include <QFile>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>

// =============================================================================
//  Session 289 — the start page: over the tab area while no tab is on show,
//  the listening state, a button for each way in, and the recent files.
//  Real file: replay/loco_1_1_27062026_140226.cap, its lines as traffic.
// =============================================================================

namespace {
void settle()
{
    for (int i = 0; i < 20; ++i) QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

QList<QPushButton *> buttonsIn(QWidget *w)
{
    return w ? w->findChildren<QPushButton *>(QString(), Qt::FindDirectChildrenOnly) : QList<QPushButton *>();
}
}  // namespace

TEST_SUITE(session289)
{
    const QByteArray savedPkg = qgetenv("DLCONSOLE_PACKAGE");
    QTemporaryDir where;
    const QString exe = where.filePath(QStringLiteral("DLConsole.exe"));
    { QFile f(exe); CHECK(f.open(QIODevice::WriteOnly), "a stand-in for the .exe"); }
    qputenv("DLCONSOLE_PACKAGE", exe.toLocal8Bit());

    const QString cap = QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap");
    const QString gone = where.filePath(QStringLiteral("gone.dlr"));
    RecentFiles::clear();
    RecentFiles::addAll({ cap, gone });

    const bool catalogWasLoaded = FieldCatalog::instance().isLoaded();
    {
        MainWindow w;
        w.resize(1200, 800);
        w.show();
        settle();
        QTabWidget *tabs = w.findChild<QTabWidget *>(QStringLiteral("tabWidget"));
        QWidget *page = w.startPage();
        CHECK(tabs && tabs->count() == 0, "fixture: a fresh INI, no tabs");
        CHECK(page && page->isVisible(), "no tabs: the start page shows");
        CHECK(page && page->geometry() == QRect(QPoint(0, 0), tabs->size()), "over the whole tab area");

        QLabel *state = nullptr;
        for (QLabel *l : page->findChildren<QLabel *>())
            if (l->text().contains(QStringLiteral("<h3>"))) state = l;
        CHECK(state && (state->text().contains(QStringLiteral("Listening on UDP port"))
                        || state->text().contains(QStringLiteral("Not listening"))),
              "the listening state, as before");

        // The ways in: the menu actions themselves.
        QStringList names;
        for (QPushButton *b : buttonsIn(page->findChild<QWidget *>(QStringLiteral("startButtons")))) names << b->text();
        CHECK(names.contains(QStringLiteral("Open recorded session…")) && names.contains(QStringLiteral("Replay capture (.cap)…"))
                  && names.contains(QStringLiteral("Live Loco Console…")) && names.contains(QStringLiteral("RFID Tag Builder…"))
                  && names.contains(QStringLiteral("Serial Port Terminal…")),
              QByteArray("five ways in: ") + names.join(QStringLiteral(" | ")).toUtf8());

        // Recent files: one row each, a missing one greyed out.
        QList<QPushButton *> rows = buttonsIn(page->findChild<QWidget *>(QStringLiteral("startRecent")));
        CHECK(rows.size() == 2, "two recent files listed");
        CHECK(rows.size() == 2 && rows.at(0)->isEnabled() && rows.at(0)->text().contains(QStringLiteral("Capture (replay)")),
              "the capture: enabled, its kind named");
        CHECK(rows.size() == 2 && !rows.at(1)->isEnabled() && rows.at(1)->text().contains(QStringLiteral("missing")),
              "a file gone since: \"missing\", greyed out");

        // Clicking a row opens it; the list is rebuilt behind the click.
        int replays = 0;
        for (QWidget *t : QApplication::topLevelWidgets()) replays += qobject_cast<ReplayWindow *>(t) ? 1 : 0;
        if (rows.size() == 2) rows.at(0)->click();
        settle();
        int after = 0;
        for (QWidget *t : QApplication::topLevelWidgets()) after += qobject_cast<ReplayWindow *>(t) ? 1 : 0;
        CHECK(after == replays + 1, "clicking the capture opens a replay window");
        CHECK(buttonsIn(page->findChild<QWidget *>(QStringLiteral("startRecent"))).size() == 2, "and the list is rebuilt, still two");

        // Traffic: a tab appears and the page goes.
        MessageDispatcher *disp = w.findChild<MessageDispatcher *>();
        QFile f(cap);
        CHECK(disp && f.open(QIODevice::ReadOnly), "fixture: the dispatcher and the capture");
        if (disp && f.isOpen()) {
            for (int i = 0; i < 20 && !f.atEnd(); ++i)
                disp->ingestLocal(21, 1, f.readLine().trimmed(), 1782558147000LL + i * 100, QString());
            disp->drainNow();
        }
        settle();
        CHECK(tabs && tabs->count() == 1 && page && !page->isVisible(), "first traffic: a tab, no start page");

        // Closing the last tab brings it back, saying the source still runs.
        if (tabs && tabs->count() == 1) emit tabs->tabCloseRequested(0);
        settle();
        CHECK(tabs && tabs->count() == 0 && page && page->isVisible(), "the last tab closed: the start page again");
        CHECK(state && state->text().contains(QStringLiteral("Show all tabs")), "naming Window > Show all tabs");

        // Collected first: deleting one top-level can take others with it.
        QList<QPointer<ReplayWindow>> opened;
        for (QWidget *t : QApplication::topLevelWidgets())
            if (auto *r = qobject_cast<ReplayWindow *>(t)) opened << r;
        for (const QPointer<ReplayWindow> &r : opened) delete r.data();
    }
    if (!catalogWasLoaded) FieldCatalog::instance().clear();

    if (savedPkg.isNull()) qunsetenv("DLCONSOLE_PACKAGE");
    else qputenv("DLCONSOLE_PACKAGE", savedPkg);
}
