#include "testutil.h"

#include "comparewindow.h"
#include "bookmarks.h"
#include "fieldinspector.h"
#include "rawbytespanel.h"
#include "messagedispatcher.h"
#include "settings.h"
#include "namemap.h"
#include "findbar.h"
#include "gototimestampdialog.h"
#include "markerscrollbar.h"
#include "logmodel.h"

#include "bookmarks.h"
#include "fieldinspector.h"
#include "rawbytespanel.h"
#include "messagedispatcher.h"
#include "settings.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QMenuBar>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QTableView>

// =============================================================================
//  Tools in the compare window.
//
//  A compare window is where a difference between two sources is actually
//  studied, and it had no way to search, no way to jump to a time and no way
//  to open a row — so that work moved back to a single tab, which is the thing
//  the window exists to avoid.
//
//  The panes bind their LogModel DIRECTLY: half a dozen places cast
//  view->model() to LogModel and would not survive a filter proxy being
//  slipped underneath them. So rather than give this window tools of its own,
//  the shared ones learned to accept either shape — which is what most of
//  these checks are really about.
// =============================================================================

namespace {

QMenu *menuNamed(QMenuBar *bar, const QString &name)
{
    if (!bar) { return nullptr; }
    for (QAction *a : bar->actions()) {
        if (a->menu() && a->text().remove(QLatin1Char('&')) == name) { return a->menu(); }
    }
    return nullptr;
}

QAction *actionContaining(QMenu *m, const QString &needle)
{
    if (!m) { return nullptr; }
    for (QAction *a : m->actions()) {
        if (a->isSeparator()) { continue; }
        if (a->text().remove(QLatin1Char('&')).contains(needle, Qt::CaseInsensitive)) {
            return a;
        }
    }
    return nullptr;
}

LogEntryPtr mk(qint64 ms, Severity s = Severity::Info)
{
    auto e = QSharedPointer<LogEntry>::create();
    e->header.source_id = 21;
    e->header.kvchId    = 1;
    e->epochMs  = ms;
    e->severity = s;
    e->text     = QStringLiteral("@lsrp_21_1 row");
    e->cacheDerived();
    return e;
}

}  // namespace

TEST_SUITE(comparetools)
{
    NameMap names;
    MessageDispatcher dispatcher;
    CompareWindow win(&dispatcher, &names);

    QMenuBar *bar = win.findChild<QMenuBar *>();
    CHECK(bar != nullptr, "the compare window has a menu bar");

    QMenu *edit  = menuNamed(bar, QStringLiteral("Edit"));
    QMenu *tools = menuNamed(bar, QStringLiteral("Tools"));
    CHECK(edit != nullptr,  "with an Edit menu");
    CHECK(tools != nullptr, "and a Tools menu");

    // ---- the same keys as everywhere else ------------------------------------
    {
        QAction *find = actionContaining(edit, QStringLiteral("Find"));
        QAction *goTo = actionContaining(edit, QStringLiteral("Go to timestamp"));
        QAction *next = actionContaining(edit, QStringLiteral("Next problem"));
        QAction *prev = actionContaining(edit, QStringLiteral("Previous problem"));
        CHECK(find && find->shortcut() == QKeySequence::Find, "Ctrl+F finds");
        CHECK(goTo && goTo->shortcut() == QKeySequence("Ctrl+G"), "Ctrl+G goes to a time");
        CHECK(next && next->shortcut() == QKeySequence("F4"), "F4 steps to a problem");
        CHECK(prev && prev->shortcut() == QKeySequence("Shift+F4"), "Shift+F4 back");

        QAction *wb = actionContaining(tools, QStringLiteral("Decode Workbench"));
        CHECK(wb && wb->shortcut() == QKeySequence("Ctrl+Shift+B"),
              "and the row opens in the workbench on the same key as the log");
    }

    // ---- the one tool specific to this window --------------------------------
    {
        CHECK(actionContaining(tools, QStringLiteral("across panes")),
              "the panes already hold two sources, so comparing what is "
              "selected in each is the question this window was opened to ask");
    }

    // ---- a find bar per pane -------------------------------------------------
    {
        const QList<FindBar *> bars = win.findChildren<FindBar *>();
        CHECK(bars.size() >= 2, "two panes, two find bars");
    }

    // ---- the shared helpers accept a model with no proxy ---------------------
    // This is what let the compare window reuse them rather than grow copies.
    {
        LogModel m(nullptr, 100);
        QVector<LogEntryPtr> v;
        for (int i = 0; i < 8; ++i) {
            v << mk(1000 + i * 1000, i == 5 ? Severity::Error : Severity::Info);
        }
        m.appendEntries(v);

        CHECK(nextMarkedRow(&m, &m, -1, +1) == 5,
              "stepping to a problem works on a directly-bound model");

        const GotoTimestamp::Span span = GotoTimestamp::spanOf(&m, &m);
        CHECK(span.ok && span.minMs == 1000 && span.maxMs == 8000,
              "and so does reading the span");
        CHECK(GotoTimestamp::resolveRow(&m, &m, 4400,
                  int(GotoTimestampDialog::Mode::AtOrAfter)) == 4,
              "and resolving a time to a row");
        CHECK(GotoTimestamp::epochAtProxyRow(&m, &m, 3) == 4000,
              "and reading a row's time");
    }
}

// =============================================================================
//  The right-click menu in a compare pane.
//
//  The panes had no context menu at all, so the only way to get a row out of
//  this window was to go and find it again in its own tab — which is the
//  thing the window exists to avoid.
// =============================================================================

TEST_SUITE(comparerowmenu)
{
    NameMap names;
    MessageDispatcher dispatcher;

    // Two sources, created BEFORE the window: a pane binds nothing until a
    // source exists to pick, which is also the real order.
    LogModel *m21 = dispatcher.modelForKey(QStringLiteral("21_1"));
    LogModel *m22 = dispatcher.modelForKey(QStringLiteral("22_1"));
    CHECK(m21 && m22, "two sources exist to compare");
    m21->appendEntries({ mk(1000), mk(2000) });
    m22->appendEntries({ mk(1500), mk(2500) });

    CompareWindow win(&dispatcher, &names);

    CHECK(win.paneCount() == 2, "two panes");
    // Addressed by PANE INDEX, not by widget-tree order: the window also
    // contains the decoded-fields table, and findChildren does not return
    // the panes first or in pane order.
    const QList<QTableView *> views{ win.paneView(0), win.paneView(1) };
    CHECK(views.at(0) && views.at(1), "each has a table");

    // Without this the menu never appears and the whole path is unreachable.
    bool allAsk = !views.isEmpty();
    for (QTableView *v : views) {
        if (v->contextMenuPolicy() != Qt::CustomContextMenu) { allAsk = false; }
    }
    CHECK(allAsk, "every pane answers a right-click");

    // ---- what the menu offers ----------------------------------------------
    {
        QTableView *view = views.first();
        auto *model = qobject_cast<LogModel *>(view->model());
        CHECK(model != nullptr && model->count() > 0,
              "the first pane is bound to a source with rows in it");

        const QModelIndex idx = model->index(0, LogModel::ColMessage);
        QMenu *menu = win.buildRowMenu(0, idx, &win);
        CHECK(menu != nullptr, "a row has a menu");

        CHECK(actionContaining(menu, QStringLiteral("Copy message")),
              "which copies the message");
        CHECK(actionContaining(menu, QStringLiteral("tab-separated")),
              "or the whole row when a report wants the columns");
        CHECK(actionContaining(menu, QStringLiteral("with header")),
              "with a header line, for pasting into a sheet");
        CHECK(actionContaining(menu, QStringLiteral("Copy bytes")),
              "or the bytes, which is what the frame tools take as input");
        CHECK(actionContaining(menu, QStringLiteral("Decode Workbench")),
              "and opens the row where it can be decoded");
        CHECK(actionContaining(menu, QStringLiteral("across panes")),
              "and reaches the one verb this window exists for");

        // Ctrl+C on the menu item means the same thing as Ctrl+C in the
        // window. Two different copies under one key is how an operator
        // ends up pasting the wrong one into a report.
        QAction *msg = actionContaining(menu, QStringLiteral("Copy message"));
        QAction *row = actionContaining(menu, QStringLiteral("tab-separated"));
        CHECK(msg && msg->shortcut() == QKeySequence::Copy,
              "the message is on Ctrl+C");
        CHECK(row && row->shortcut() == QKeySequence("Ctrl+Alt+C"),
              "and the full row one shift away");

        // ---- and what it actually puts on the clipboard ---------------------
        QApplication::clipboard()->clear();
        if (msg) { msg->trigger(); }
        const QString copied = QApplication::clipboard()->text();
        CHECK(copied == QStringLiteral("@lsrp_21_1 row"),
              "copying the message gives the message and nothing else");
        CHECK(!copied.contains(QLatin1Char('\t')),
              "no time, no source, no severity — no columns to strip off "
              "before it can be pasted into a decoder");

        if (row) { row->trigger(); }
        const QString full = QApplication::clipboard()->text();
        CHECK(full.contains(QLatin1Char('\t')) && full.endsWith(QStringLiteral("@lsrp_21_1 row")),
              "and the other action still gives the columns, for the times a "
              "report wants them");

        delete menu;
    }

    // ---- a row that cannot be acted on says so ------------------------------
    {
        QTableView *view = views.first();
        auto *model = qobject_cast<LogModel *>(view->model());
        QMenu *menu = win.buildRowMenu(0, model->index(9999, 0), &win);
        CHECK(menu == nullptr,
              "a row that is not there has no menu, rather than a menu whose "
              "every action would act on nothing");
    }

    // ---- parity with a log tab ---------------------------------------------
    //
    // These panes used to hard-code 44 px rows and their own column set, so
    // an operator who set rows to Comfortable or hid the Source column
    // changed the log tabs and left the compare panes looking like a
    // different program.
    {
        QTableView *view = views.first();
        CHECK(view->selectionMode() == QAbstractItemView::ExtendedSelection,
              "a span of rows can be selected, as in a log tab — copying a "
              "block into a report was a row-at-a-time job before");
        CHECK(view->verticalHeader()->defaultSectionSize()
                  == Settings::rowHeightFor(Settings::rowDensity()),
              "row height follows the density setting rather than a "
              "hard-coded 44 px");
        CHECK(view->wordWrap() == Settings::wordWrapFor(Settings::rowDensity()),
              "and so does word wrap");

        // Hidden columns are the setting's business, not the pane's.
        const QList<int> hidden = Settings::hiddenColumns();
        bool honoured = true;
        for (int c = 0; c < LogModel::ColumnCount; ++c) {
            const bool shouldHide =
                (c != LogModel::ColMessage) && hidden.contains(c);
            if (view->isColumnHidden(c) != shouldHide) { honoured = false; }
        }
        CHECK(honoured, "hidden columns match the setting, and Message is "
                        "never hidden — a log table with no message in it is "
                        "not a log table");
    }

    // ---- copies act on the whole selection ---------------------------------
    {
        QTableView *view = views.first();
        auto *model = qobject_cast<LogModel *>(view->model());
        view->selectionModel()->select(
            QItemSelection(model->index(0, 0),
                           model->index(1, LogModel::ColumnCount - 1)),
            QItemSelectionModel::Select);

        QMenu *menu = win.buildRowMenu(0, model->index(0, 0), &win);
        QAction *msg = actionContaining(menu, QStringLiteral("Copy 2 messages"));
        CHECK(msg != nullptr,
              "right-clicking inside a selection offers the selection — "
              "silently copying one row would discard the span just made");
        if (msg) {
            msg->trigger();
            CHECK(QApplication::clipboard()->text().split('\n').size() == 2,
                  "and copies both");
        }
        delete menu;
        view->clearSelection();
    }

    // ---- decoded fields, not just bytes ------------------------------------
    //
    // The useful comparison here is of decoded VALUES across two sources,
    // not of hex. With bytes alone an operator had to carry the row back to
    // a log tab to read it.
    {
        CHECK(win.findChild<RawBytesPanel *>() != nullptr,
              "the window shows the bytes of the selected frame");
        CHECK(win.findChild<FieldInspector *>() != nullptr,
              "and its decoded fields beside them");
    }

    // ---- bookmarks are the shared store ------------------------------------
    {
        BookmarkStore store;
        win.setBookmarks(&store);

        QTableView *view = views.first();
        auto *model = qobject_cast<LogModel *>(view->model());
        view->setCurrentIndex(model->index(0, LogModel::ColTime));

        QMenu *menu = win.buildRowMenu(0, model->index(0, 0), &win);
        QAction *bm = actionContaining(menu, QStringLiteral("bookmark"));
        CHECK(bm != nullptr && bm->isEnabled(),
              "a row can be bookmarked");
        CHECK(bm && bm->shortcut() == QKeySequence("Ctrl+B"),
              "on the same key as in a log tab");
        if (bm) {
            bm->trigger();
            CHECK(store.all().size() == 1,
                  "and the mark lands in the store the log tabs read, not in "
                  "a second one only this window can see");
            CHECK(model->entryAt(0) && model->entryAt(0)->bookmarked,
                  "and on the entry, so the row draws its mark");
        }
        delete menu;
    }

    // ---- a store that was never supplied disables rather than misleads -----
    {
        MessageDispatcher d2;
        LogModel *m = d2.modelForKey(QStringLiteral("21_1"));
        m->appendEntries({ mk(1000) });
        CompareWindow bare(&d2, &names);   // no setBookmarks
        auto *bm2 = bare.paneView(0)
                        ? qobject_cast<LogModel *>(bare.paneView(0)->model())
                        : nullptr;
        if (bm2 && bm2->count() > 0) {
            QMenu *menu = bare.buildRowMenu(0, bm2->index(0, 0), &bare);
            QAction *bm = menu ? actionContaining(menu, QStringLiteral("bookmark"))
                               : nullptr;
            CHECK(bm && !bm->isEnabled(),
                  "with no store to write to, bookmarking is disabled rather "
                  "than a menu entry that quietly does nothing");
            delete menu;
        }
    }

    // ---- the window's own keys ---------------------------------------------
    {
        QMenuBar *bar = win.findChild<QMenuBar *>();
        QMenu *edit = menuNamed(bar, QStringLiteral("Edit"));
        QAction *msg = actionContaining(edit, QStringLiteral("Copy message"));
        QAction *row = actionContaining(edit, QStringLiteral("Copy row"));
        CHECK(msg && msg->shortcut() == QKeySequence::Copy,
              "Ctrl+C in the window copies the message");
        CHECK(row && row->shortcut() == QKeySequence("Ctrl+Alt+C"),
              "and Ctrl+Alt+C the row");

        QAction *bm = actionContaining(edit, QStringLiteral("bookmark"));
        CHECK(bm && bm->shortcut() == QKeySequence("Ctrl+B"),
              "Ctrl+B bookmarks, as in a log tab");
        CHECK(actionContaining(edit, QStringLiteral("Next bookmark")),
              "F2 and Shift+F2 step between them");
    }
}
