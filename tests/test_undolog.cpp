#include "testutil.h"

#include "undolog.h"
#include "logmodel.h"
#include "bookmarks.h"

#include <QAction>
#include <QMainWindow>
#include <QTemporaryDir>

// =============================================================================
//  Undo for destructive clicks (session 79): the log itself, and the two
//  model operations the main window's undos are built from.
// =============================================================================

namespace {

LogEntryPtr entry(qint64 ms, const QString &text)
{
    LogEntryPtr e(new LogEntry);
    e->epochMs = ms;
    e->text    = text;
    return e;
}

}  // namespace

TEST_SUITE(undolog)
{
    // ---- the log ------------------------------------------------------------
    {
        UndoLog log;
        CHECK(!log.canUndo(), "an empty log has nothing to undo");
        QString label = QStringLiteral("x");
        CHECK(!log.undo(&label) && label.isEmpty(), "undo on an empty log does nothing and names nothing");

        int value = 0;
        log.push(QStringLiteral("set 1"), [&value]() { value = 0; return true; });
        value = 1;
        log.push(QStringLiteral("set 2"), [&value]() { value = 1; return true; });
        value = 2;
        CHECK(log.depth() == 2 && log.nextLabel() == QLatin1String("set 2"), "the newest step is next");
        CHECK(log.undo(&label) && value == 1 && label == QLatin1String("set 2"), "undo runs the newest step");
        CHECK(log.undo(&label) && value == 0, "then the one before it");
        CHECK(!log.canUndo(), "and the log is empty");

        log.push(QStringLiteral("gone"), []() { return false; });
        CHECK(!log.undo(&label) && label == QLatin1String("gone") && !log.canUndo(),
              "a step whose target is gone reports false and is still consumed");
    }
    {
        UndoLog log;
        for (int i = 0; i < UndoLog::kMaxSteps + 5; ++i) {
            log.push(QStringLiteral("step %1").arg(i), []() { return true; });
        }
        CHECK(log.depth() == UndoLog::kMaxSteps, "at most kMaxSteps are kept");
        CHECK(log.nextLabel() == QStringLiteral("step %1").arg(UndoLog::kMaxSteps + 4), "the newest survive");

        UndoLog heavy;
        heavy.push(QStringLiteral("a"), []() { return true; }, UndoLog::kMaxHeldRows / 2 + 1);
        heavy.push(QStringLiteral("b"), []() { return true; }, UndoLog::kMaxHeldRows / 2 + 1);
        CHECK(heavy.depth() == 1 && heavy.nextLabel() == QLatin1String("b"),
              "rows held across steps are capped: the older big step is dropped");
        heavy.push(QStringLiteral("huge"), []() { return true; }, UndoLog::kMaxHeldRows * 3);
        CHECK(heavy.depth() == 1 && heavy.nextLabel() == QLatin1String("huge"),
              "the newest step survives however large it is");
    }

    // ---- the menu action --------------------------------------------------
    {
        QMainWindow window;
        UndoLog log;
        QAction *act = log.createAction(&window);
        CHECK(!act->isEnabled() && act->shortcut() == QKeySequence(QKeySequence::Undo),
              "Undo starts disabled, on Ctrl+Z");
        bool undone = false;
        QString reported;
        QObject::connect(&log, &UndoLog::undone, [&](const QString &l, bool ok) { reported = l; undone = ok; });
        log.push(QStringLiteral("Clear tab 21_1"), []() { return true; });
        CHECK(act->isEnabled() && act->text().remove('&') == QLatin1String("Undo Clear tab 21_1"),
              "the action names what it will undo");
        act->trigger();
        CHECK(undone && reported == QLatin1String("Clear tab 21_1"), "triggering it undoes and reports");
        CHECK(!act->isEnabled(), "and it goes back to disabled");
    }

    // ---- LogModel::takeAll / restoreOlder ----------------------------------
    {
        LogModel model(nullptr, 10, 0.1);
        for (int i = 0; i < 6; ++i) model.appendEntry(entry(i, QStringLiteral("old %1").arg(i)));
        const QVector<LogEntryPtr> taken = model.takeAll();
        CHECK(taken.size() == 6 && model.count() == 0, "takeAll empties the model and hands back every row");
        CHECK(taken.first()->epochMs == 0 && taken.last()->epochMs == 5, "oldest first");

        model.appendEntry(entry(100, QStringLiteral("new A")));
        model.appendEntry(entry(101, QStringLiteral("new B")));
        const int back = model.restoreOlder(taken);
        CHECK(back == 6 && model.count() == 8, "restoreOlder puts them all back when there is room");
        CHECK(model.entryAt(0)->epochMs == 0 && model.entryAt(5)->epochMs == 5
                  && model.entryAt(6)->text == QLatin1String("new A"),
              "in front of the rows that arrived since, in their order");

        LogModel small(nullptr, 10, 0.1);
        for (int i = 0; i < 8; ++i) small.appendEntry(entry(i, QString()));
        const QVector<LogEntryPtr> t2 = small.takeAll();
        for (int i = 0; i < 5; ++i) small.appendEntry(entry(200 + i, QString()));
        CHECK(small.restoreOlder(t2) == 5 && small.count() == 10,
              "over capacity, only as many as fit come back");
        CHECK(small.entryAt(0)->epochMs == 3 && small.entryAt(4)->epochMs == 7,
              "and they are the newest of the old rows");
        CHECK(small.entryAt(5)->epochMs == 200, "rows that arrived since are never pushed out");
    }

    // ---- BookmarkStore::replaceAll -----------------------------------------
    {
        QTemporaryDir dir;
        BookmarkStore store;
        store.load(dir.filePath(QStringLiteral("bookmarks.json")));
        store.toggle(QStringLiteral("21_1"), 1000, QStringLiteral("a"));
        store.toggle(QStringLiteral("21_1"), 2000, QStringLiteral("b"));
        const QVector<Bookmark> before = store.all();
        store.clear();
        CHECK(store.count() == 0, "Remove all empties the store");
        store.replaceAll(before);
        CHECK(store.count() == 2 && store.has(QStringLiteral("21_1"), 2000), "replaceAll brings them back");
        BookmarkStore reread;
        reread.load(dir.filePath(QStringLiteral("bookmarks.json")));
        CHECK(reread.count() == 2, "and saves them");
    }
}
