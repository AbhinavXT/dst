#include "testutil.h"

#include "colorrules.h"
#include "fieldcatalog.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "namemap.h"
#include "sessionfile.h"
#include "sessionwindow.h"
#include "testassertions.h"
#include "testcaseboard.h"
#include "testcasepanel.h"
#include "theme.h"
#include "uistyle.h"

#include <QAction>
#include <QCoreApplication>
#include <QDateTime>
#include <QDockWidget>
#include <QFile>
#include <QMenu>
#include <QMenuBar>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>

// =============================================================================
//  Session 370 — the test cases over a recording. The engine that marks
//  cases OBSERVED / NOT ANSWERED on live traffic (sessions 305, 306) runs
//  over every tab of a recording, in time order, and its results show in a
//  Test cases dock in the recorded-session window. Real frames: the
//  2026-10-08 runs session 305 and 306 check (loco_1_1_08102026_101300 and
//  _105200), written to .dlr files the way Save writes them.
// =============================================================================

namespace {

QString writeDlr370(const QString &path, LogModel *m)
{
    QFile f(path);
    if (!m || !f.open(QIODevice::WriteOnly)) return QString();
    SessionFile::FileHeader h;
    h.createdMs = 1791453180000LL;
    h.sourceId = 1;
    h.kvchId = 1;
    f.write(SessionFile::encodeHeader(h));
    for (int r = 0; r < m->rowCount(); ++r) {
        const LogEntryPtr e = m->entryAt(r);
        if (e && !e->rawBytes.isEmpty()) f.write(SessionFile::encodeRecord(e->epochMs, e->rawBytes));
    }
    return path;
}

// The capture as the console takes it: one dispatcher, tab 1_1.
void ingest370(MessageDispatcher &disp, const char *path)
{
    QFile f(QString::fromUtf8(path));
    if (!f.open(QIODevice::ReadOnly)) return;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3 || !l.startsWith('@')) continue;
        QDateTime t = QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate);
        disp.ingestLocal(1, 1, l, t.toMSecsSinceEpoch(), QString());
    }
    disp.drainNow();
}

QByteArray fileBytes370(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

const TestAssertion *find370(const TestAssertionEngine &en, const QString &id)
{
    for (const TestAssertion &a : en.assertions())
        if (a.id == id) return &a;
    return nullptr;
}

}  // namespace

TEST_SUITE(session370)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    FieldCatalog &cat = FieldCatalog::instance();
    // The catalog is process-wide: leave it as found, early returns too
    // (the assertions suite reads RADIO1_LINK_FAIL without it).
    struct Restore { FieldCatalog &c; bool had; ~Restore() { if (!had) c.clear(); } } restore{ cat, cat.isLoaded() };
    CHECK(cat.load(QStringLiteral(DL_SRC_DIR "/fieldmap.json")), "the repository's fieldmap.json loads");
    const QByteArray cases = fileBytes370(QStringLiteral(DL_SRC_DIR "/testcases.json"));
    CHECK(!cases.isEmpty(), "fixture: testcases.json");

    // 105200: OBSERVED and NOT ANSWERED cases both (session 306). One run:
    // the engine takes about a millisecond a row with 101 cases.
    const char *runs[] = { DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_105200.cap" };
    QTemporaryDir tmp;
    ColorRules rules;
    NameMap names;
    int n = 0;
    for (const char *run : runs) {
        ++n;
        MessageDispatcher disp;
        ingest370(disp, run);
        LogModel *live = disp.modelForKey(QStringLiteral("1_1"));
        CHECK(live && live->count() > 100, "fixture: the run's rows");
        if (!live) return;

        // What the live engine makes of the same rows, in the same order.
        TestAssertionEngine liveEngine;
        liveEngine.loadFromJson(cases);
        for (int r = 0; r < live->count(); ++r) liveEngine.observe(live->entryAt(r));
        liveEngine.expireArmed(live->entryAt(live->count() - 1)->epochMs);
        TestCaseBoard liveBoard(&liveEngine);

        const QString dlr = writeDlr370(tmp.filePath(QStringLiteral("run%1.dlr").arg(n)), live);
        SessionWindow w(&rules, &names, Theme::Light);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.setTestCases(cases);
        CHECK(w.loadFiles({ dlr }) == live->count(), "the recording loads");
        w.show();
        QCoreApplication::processEvents();

        const TestAssertionEngine *en = w.testEngine();
        CHECK(en && en->assertions().size() == 101, "the window's own engine, with the 101 cases");
        if (!en) return;
        TestCaseBoard *board = w.testBoard();
        CHECK(board && board->counts().observed == liveBoard.counts().observed
                  && board->counts().notAnswered == liveBoard.counts().notAnswered
                  && board->counts().observed > 0,
              QByteArray("the recording's results are the live engine's: ") + board->summary().toUtf8());
        CHECK(board->counts().notAnswered > 0, "NOT ANSWERED cases too, from the recording's own end");
        bool same = true;
        for (const TestAssertion &a : liveEngine.assertions()) {
            const TestAssertion *b = find370(*en, a.id);
            same = same && b && TestCaseBoard::stateOf(*b) == TestCaseBoard::stateOf(a) && b->firstMs == a.firstMs;
        }
        CHECK(same, "case by case: the same state, from the same frame");

        if (n == 1) {
            const TestAssertion *a2 = nullptr;
            for (const TestAssertion &a : en->assertions())
                if (a.observed() && !a2) a2 = &a;
            CHECK(a2 && a2->firstKey == QLatin1String("1_1") && a2->firstMs > 0,
                  "an OBSERVED case, its evidence on tab 1_1: " + (a2 ? a2->id.toUtf8() : QByteArray()));

            // The dock, in front of the decoded fields, and a jump to the evidence.
            QDockWidget *dock = w.findChild<QDockWidget *>(QStringLiteral("sessionTestCasesDock"));
            auto *panel = w.findChild<TestCasePanel *>();
            CHECK(dock && panel && panel->table()->rowCount() == 101, "a Test cases dock, one row per case");
            if (a2 && panel) {
                emit panel->jumpRequested(a2->firstKey, a2->firstMs);
                QTableView *view = nullptr;
                for (QTableView *tv : w.findChildren<QTableView *>())
                    if (tv->isVisible() && tv->model() && tv->model()->rowCount() == live->count()) view = tv;
                auto *proxy = view ? qobject_cast<QSortFilterProxyModel *>(view->model()) : nullptr;
                auto *model = proxy ? qobject_cast<LogModel *>(proxy->sourceModel()) : nullptr;
                CHECK(model && view->currentIndex().isValid()
                          && model->entryAt(proxy->mapToSource(view->currentIndex()).row())->epochMs == a2->firstMs,
                      "double-click on a case lands on its evidence frame in the recording");
            }

            // The report, as the live window saves it.
            const QString html = w.testReportHtml();
            CHECK(a2 && html.contains(a2->id) && html.contains(QLatin1String("recording")),
                  "the observation report over the recording names it");

            // Loading other cases runs them again over the same rows.
            w.setTestCases(QByteArray("[]"));
            CHECK(w.testEngine()->assertions().isEmpty() && w.testBoard()->counts().observed == 0,
                  "no cases: nothing counted");
            w.setTestCases(cases);
            CHECK(w.testBoard()->counts().observed == liveBoard.counts().observed,
                  "set again: run again over the loaded rows, the same result");
        }
    }

    // ---- without test cases: no dock, and the menu says how ---------------------------------
    {
        MessageDispatcher disp;
        ingest370(disp, DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_101300.cap");
        const QString dlr = writeDlr370(tmp.filePath(QStringLiteral("bare.dlr")), disp.modelForKey(QStringLiteral("1_1")));
        SessionWindow w(&rules, &names, Theme::Light);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.loadFiles({ dlr });
        QDockWidget *dock = w.findChild<QDockWidget *>(QStringLiteral("sessionTestCasesDock"));
        CHECK(!dock || dock->isHidden(), "no cases given: no Test cases dock on show");
        bool builtin = false;
        for (QAction *m : w.menuBar()->actions())
            if (m->menu())
                for (QAction *a : m->menu()->actions())
                    if (a->objectName() == QLatin1String("sessionTestCasesBuiltin")) builtin = true;
        CHECK(builtin, "Test cases ▸ Run the built-in test cases over this recording");
    }
}
