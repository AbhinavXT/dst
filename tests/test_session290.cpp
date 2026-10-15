#include "testutil.h"

#include "fieldcatalog.h"
#include "mainwindow.h"
#include "shortcutsdialog.h"
#include "uicolors.h"

#include <QAction>
#include <QFile>
#include <QMenu>
#include <QMenuBar>
#include <QSet>
#include <QTableWidget>
#include <QTemporaryDir>

// =============================================================================
//  Session 290 — Help ▸ Keyboard Shortcuts (F1): every shortcut of the
//  window, from its real menus, with a search box.
// =============================================================================

namespace {
int countShortcuts(QMenu *m)
{
    int n = 0;
    emit m->aboutToShow();
    for (QAction *a : m->actions()) {
        if (a->isSeparator()) continue;
        if (a->menu()) n += countShortcuts(a->menu());
        else if (!a->shortcut().isEmpty() && !a->text().remove(QLatin1Char('&')).isEmpty()) ++n;
    }
    return n;
}
}  // namespace

TEST_SUITE(session290)
{
    // ---- the harvest, on a menu bar built here ------------------------
    {
        QWidget window;
        QMenuBar bar(&window);
        QMenu *file = bar.addMenu(QStringLiteral("&File"));
        QAction *open = file->addAction(QStringLiteral("&Open…"));
        open->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
        file->addSeparator();
        file->addAction(QStringLiteral("No key"));
        QMenu *sub = file->addMenu(QStringLiteral("&Export"));
        QAction *csv = sub->addAction(QStringLiteral("CSV"));
        csv->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
        csv->setEnabled(false);
        window.addAction(open);   // also on the window: listed once
        auto *hidden = new QAction(QStringLiteral("Leave presentation"), &window);
        hidden->setShortcut(QKeySequence(Qt::Key_Escape));
        window.addAction(hidden);

        const QVector<ShortcutRow> rows = harvestShortcuts(&bar, &window);
        CHECK(rows.size() == 3, "three shortcuts: Open, CSV, and the window's own; no key, no row");
        CHECK(rows.size() == 3 && rows.at(0).command == QStringLiteral("Open…")
                  && rows.at(0).where == QStringLiteral("File › Open…") && rows.at(0).enabled,
              "menu order, & stripped, the menu path");
        CHECK(rows.size() == 3 && rows.at(1).where == QStringLiteral("File › Export › CSV") && !rows.at(1).enabled,
              "a submenu followed; a disabled action listed, as disabled");
        CHECK(rows.size() == 3 && rows.at(2).command == QLatin1String("Leave presentation")
                  && rows.at(2).where == QLatin1String("(window)"),
              "a shortcut on the window that no menu holds: \"(window)\"");

        ShortcutsDialog d(rows);
        CHECK(d.table()->rowCount() == 3 && d.visibleRows() == 3, "the dialog: one row each, all shown");
        CHECK(d.table()->item(1, 0)->foreground().color() == UiColor::muted(), "the disabled one greyed");
        d.setFilter(QStringLiteral("export"));
        CHECK(d.visibleRows() == 1 && !d.table()->isRowHidden(1), "search by menu: \"export\" finds CSV");
        d.setFilter(open->shortcut().toString(QKeySequence::NativeText));
        CHECK(d.visibleRows() == 1 && !d.table()->isRowHidden(0), "search by keys finds Open");
        d.setFilter(QStringLiteral("ctrl+o"));
        CHECK(d.visibleRows() == 1 && !d.table()->isRowHidden(0), "\"ctrl+o\" finds Open on every platform (a Mac shows \u2318O)");
        d.setFilter(QStringLiteral("zzzz"));
        CHECK(d.visibleRows() == 0, "no match: nothing shown");
        d.setFilter(QString());
        CHECK(d.visibleRows() == 3, "cleared: all again");
    }

    // ---- the real window --------------------------------------------
    const QByteArray savedPkg = qgetenv("DLCONSOLE_PACKAGE");
    QTemporaryDir where;
    const QString exe = where.filePath(QStringLiteral("DLConsole.exe"));
    { QFile f(exe); CHECK(f.open(QIODevice::WriteOnly), "a stand-in for the .exe"); }
    qputenv("DLCONSOLE_PACKAGE", exe.toLocal8Bit());
    const bool catalogWasLoaded = FieldCatalog::instance().isLoaded();
    {
        MainWindow w;
        QAction *f1 = w.findChild<QAction *>(QStringLiteral("actKeyboardShortcuts"));
        CHECK(f1 && f1->shortcut() == QKeySequence(Qt::Key_F1), "Help > Keyboard Shortcuts… on F1");

        const QVector<ShortcutRow> rows = harvestShortcuts(w.menuBar(), &w);
        int inMenus = 0;
        for (QAction *top : w.menuBar()->actions())
            if (top->menu()) inMenus += countShortcuts(top->menu());
        int fromMenus = 0;
        QSet<QString> commands;
        bool keysSet = true;
        for (const ShortcutRow &r : rows) {
            fromMenus += r.where == QLatin1String("(window)") ? 0 : 1;
            commands.insert(r.command);
            keysSet = keysSet && !r.keys.isEmpty();
        }
        CHECK(fromMenus == inMenus && inMenus > 20,
              QByteArray("every shortcut in the menus, counted independently: ") + QByteArray::number(inMenus));
        CHECK(keysSet, "no row without keys");
        CHECK(commands.contains(QStringLiteral("Keyboard Shortcuts…")) && commands.contains(QStringLiteral("Run command…"))
                  && commands.contains(QStringLiteral("Find…")) && commands.contains(QStringLiteral("Open recorded session…")),
              "F1 itself, Ctrl+P, Ctrl+F and Ctrl+O among them");
    }
    if (!catalogWasLoaded) FieldCatalog::instance().clear();
    if (savedPkg.isNull()) qunsetenv("DLCONSOLE_PACKAGE");
    else qputenv("DLCONSOLE_PACKAGE", savedPkg);
}
