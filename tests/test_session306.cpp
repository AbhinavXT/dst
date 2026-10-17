#include "testutil.h"

#include "dmipanel.h"
#include "fieldcatalog.h"
#include "lococonsolewindow.h"
#include "logentry.h"
#include "messagedispatcher.h"
#include "settings.h"
#include "testassertions.h"
#include "testcaseboard.h"
#include "testcasepanel.h"
#include "uicolors.h"

#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>

// =============================================================================
//  Session 306 — the test cases' results, live: the Test cases dock and the
//  strip under the DMI window and the Live Loco Console.
//
//  - the engine keeps the tab of each case's evidence and a change counter;
//  - TestCaseBoard: counts per section, the latest change, the section
//    filter shared by every view;
//  - TestCasePanel / TestCaseStrip on the real 2026-10-08 captures (the
//    same runs session 305 checks), and the strip's place in both windows.
// =============================================================================

namespace {

const char *k101300 = DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_101300.cap";
const char *k105200 = DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_105200.cap";
const QString kTab306 = QStringLiteral("1_1");

// Every @ line of a capture into the engine, each entry carrying the tab key
// the console would give it.
void run306(TestAssertionEngine &en, const char *path)
{
    QFile f(QString::fromUtf8(path));
    if (!f.open(QIODevice::ReadOnly)) return;
    while (!f.atEnd()) {
        const QString line = QString::fromLatin1(f.readLine().trimmed());
        if (!line.startsWith(QLatin1Char('@'))) continue;
        QDateTime t = QDateTime::fromString(line.section(QLatin1Char(' '), 1, 1), Qt::ISODate);
        t.setTimeSpec(Qt::UTC);
        auto e = LogEntryPtr::create();
        e->text = line;
        e->epochMs = t.toMSecsSinceEpoch();
        e->cachedTabKey = kTab306;
        en.observe(e);
    }
}

const TestAssertion *find306(const TestAssertionEngine &en, const QString &id)
{
    for (const TestAssertion &a : en.assertions())
        if (a.id == id) return &a;
    return nullptr;
}

int rowOf306(QTableWidget *t, const QString &id)
{
    for (int r = 0; r < t->rowCount(); ++r)
        if (t->item(r, TestCasePanel::ColCase) && t->item(r, TestCasePanel::ColCase)->text() == id) return r;
    return -1;
}

QString cell306(QTableWidget *t, int row, int col)
{
    return row >= 0 && t->item(row, col) ? t->item(row, col)->text() : QString();
}

QString stamp306(qint64 ms)
{
    return QDateTime::fromMSecsSinceEpoch(ms, Settings::showUtc() ? Qt::UTC : Qt::LocalTime)
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

}  // namespace

TEST_SUITE(session306)
{
    const QString cases = QStringLiteral(DL_SRC_DIR "/testcases.json");
    // The cases use fieldmap.json's names (dmi_alarm, arp_loco_mode, ...).
    FieldCatalog &cat = FieldCatalog::instance();
    const bool hadCatalog = cat.isLoaded();
    CHECK(cat.load(QStringLiteral(DL_SRC_DIR "/fieldmap.json")), "the repository's fieldmap.json loads");

    // ---- sections -----------------------------------------------------------
    CHECK(TestAssertionEngine::sectionOf(QStringLiteral("1.14/FS")) == QLatin1String("1")
              && TestAssertionEngine::sectionOf(QStringLiteral("32.9.3")) == QLatin1String("32")
              && TestAssertionEngine::sectionOf(QStringLiteral("SEC/KEY-EXPIRY")) == QLatin1String("SEC"),
          "sectionOf: 1.14/FS -> 1, 32.9.3 -> 32, SEC/KEY-EXPIRY -> SEC");

    // ---- the engine: evidence tabs and the change counter --------------------
    {
        TestAssertionEngine en;
        const quint64 g0 = en.generation();
        CHECK(en.load(cases), "testcases.json loads");
        const quint64 g1 = en.generation();
        CHECK(g1 > g0, "a load moves the change counter");
        run306(en, k101300);
        const quint64 g2 = en.generation();
        CHECK(g2 > g1, "101300 moves it");
        const TestAssertion *a2 = find306(en, QStringLiteral("1.2"));
        CHECK(a2 && a2->observed() && a2->firstKey == kTab306 && a2->firstMs > 0,
              "101300: 1.2 OBSERVED, its evidence on tab 1_1");
        en.resetObservations();
        CHECK(en.generation() > g2 && a2->firstKey.isEmpty() && a2->firstMs == 0,
              "a reset clears the evidence tab and moves the counter");
    }

    // ---- one run of 105200 behind the board, the dock and a strip ----------
    {
        TestAssertionEngine en;
        TestCaseBoard board(&en);
        TestCasePanel panel(&board);
        QWidget host;
        auto *strip = new TestCaseStrip(&board, &host);
        CHECK(board.counts().total() == 0 && !board.lastEvent().valid(), "no cases: nothing counted, no event");
        CHECK(panel.table()->rowCount() == 0 && panel.summaryText().contains(QLatin1String("No test cases loaded")),
              "dock, nothing loaded: says how to load the built-in cases");
        CHECK(strip->isHidden(), "strip, nothing loaded: hidden");

        en.load(cases);
        QSignalSpy changed(&board, &TestCaseBoard::changed);
        board.poll();
        CHECK(changed.count() == 1, "poll after a load: changed");
        board.poll();
        CHECK(changed.count() == 1, "poll with nothing new: quiet");
        CHECK(board.sections() == (QStringList{ QStringLiteral("1"), QStringLiteral("32"), QStringLiteral("SEC") }),
              "sections: 1, 32, then SEC");
        CHECK(board.counts().total() == en.assertions().size() && board.counts().total() == 101,
              "All: the 101 loaded cases");

        run306(en, k105200);
        board.poll();
        CHECK(changed.count() == 2, "poll after a run: changed once, however many frames");

        const TestAssertion *c30 = find306(en, QStringLiteral("1.30"));
        CHECK(c30 && c30->hasViolation() && c30->worstKey == kTab306 && c30->worstMs > 0
                  && c30->lastViolKey == kTab306 && c30->lastViolMs >= c30->worstMs,
              "105200: 1.30 NOT ANSWERED, the unanswered precondition's tab and time kept (first and latest)");
        const TestAssertion *c29 = find306(en, QStringLiteral("1.29"));
        CHECK(c29 && c29->observed() && c29->firstKey == kTab306, "105200: 1.29 OBSERVED on tab 1_1");

        int obs = 0, na = 0, no = 0;
        for (const TestAssertion &a : en.assertions()) {
            if (a.hasViolation()) ++na; else if (a.observed()) ++obs; else ++no;
        }
        const CaseCounts all = board.counts();
        CHECK(all.observed == obs && all.notAnswered == na && all.notObserved == no && all.queryError == 0,
              "All: counts agree with the cases' own states");
        CHECK(panel.table()->rowCount() == 101 && panel.sectionBox()->count() == 4,
              "dock: 101 rows; the section box: All, §1, §32, SEC");

        // The dock picks §1: the board, and so every strip, follow.
        panel.sectionBox()->setCurrentIndex(panel.sectionBox()->findData(QStringLiteral("1")));
        CHECK(changed.count() == 3 && board.section() == QLatin1String("1") && panel.table()->rowCount() == 76,
              "picking §1 in the dock sets the board's section: changed, 76 rows");
        const CaseCounts s1 = board.counts();
        CHECK(s1.total() == 76 && s1.notAnswered >= 3 && s1.observed >= 1,
              "§1: 76 cases; 105200 gives 1.4, 1.20 and 1.30 NOT ANSWERED, 1.29 OBSERVED");
        CHECK(board.sectionLabel() == QStringLiteral("SIF §1"), "label: SIF §1");
        CHECK(board.summary().startsWith(QLatin1String("76 case(s): "))
                  && board.summary().contains(QStringLiteral("%1 NOT ANSWERED").arg(s1.notAnswered)),
              "summary: '76 case(s): … NOT ANSWERED …'");
        CHECK(panel.summaryText() == board.summary(), "dock summary = the board's");

        const TestCaseBoard::Event e = board.lastEvent();
        const TestAssertion *ea = e.valid() ? find306(en, e.id) : nullptr;
        CHECK(ea && TestAssertionEngine::sectionOf(e.id) == QLatin1String("1")
                  && TestCaseBoard::stateOf(*ea) == e.state && e.tabKey == kTab306 && e.ms > 0,
              "the latest change in §1 is a §1 case, still in that state, with its tab and time");

        // -- the dock's rows
        QTableWidget *t = panel.table();
        const int r30 = rowOf306(t, QStringLiteral("1.30"));
        CHECK(cell306(t, r30, TestCasePanel::ColState) == QLatin1String("NOT ANSWERED")
                  && cell306(t, r30, TestCasePanel::ColSource) == kTab306
                  && c30 && cell306(t, r30, TestCasePanel::ColAt) == stamp306(c30->worstMs)
                  && cell306(t, r30, TestCasePanel::ColEvidence) == c30->violationText,
              "1.30 row: NOT ANSWERED, at the unanswered precondition, tab 1_1, its evidence");
        CHECK(t->item(r30, TestCasePanel::ColState)->foreground().color() == UiColor::error(),
              "NOT ANSWERED in the error colour");
        const int r29 = rowOf306(t, QStringLiteral("1.29"));
        CHECK(cell306(t, r29, TestCasePanel::ColState) == QLatin1String("OBSERVED") && c29
                  && cell306(t, r29, TestCasePanel::ColAt) == stamp306(c29->firstMs)
                  && cell306(t, r29, TestCasePanel::ColEvidence) == c29->firstText,
              "1.29 row: OBSERVED, when first seen, its evidence");
        CHECK(t->item(r29, TestCasePanel::ColCase)->toolTip().contains(QLatin1String("Double-click")),
              "a row with evidence says double-click shows it");
        const int r33 = rowOf306(t, QStringLiteral("1.33"));
        CHECK(cell306(t, r33, TestCasePanel::ColState) == QLatin1String("not observed")
                  && cell306(t, r33, TestCasePanel::ColAt).isEmpty()
                  && cell306(t, r33, TestCasePanel::ColSource).isEmpty(),
              "1.33 (no real frame): not observed, no time, no source");

        QSignalSpy jump(&panel, &TestCasePanel::jumpRequested);
        emit t->cellDoubleClicked(r30, TestCasePanel::ColClause);
        CHECK(jump.count() == 1 && jump.at(0).at(0).toString() == kTab306
                  && c30 && jump.at(0).at(1).toLongLong() == c30->worstMs,
              "double-click 1.30: jump to tab 1_1 at the unanswered precondition");
        emit t->cellDoubleClicked(r33, TestCasePanel::ColCase);
        CHECK(jump.count() == 1, "double-click a case with no evidence: no jump");
        t->setCurrentCell(r30, 0);
        panel.refresh();
        CHECK(t->currentRow() == rowOf306(t, QStringLiteral("1.30")), "a refresh keeps the selected case");

        // -- the strip
        CHECK(!strip->isHidden() && strip->countsText() == QStringLiteral("SIF §1: ") + board.summary(),
              "strip: 'SIF §1: 76 case(s): …', shown");
        CHECK(e.valid() && strip->lastText().startsWith(QStringLiteral("Last: %1 %2 at ")
                                                             .arg(e.id, TestCaseBoard::stateText(e.state)))
                  && strip->lastText().contains(QStringLiteral("(1_1)")),
              "strip: the latest change, its state, time and tab");
        QSignalSpy open(&board, &TestCaseBoard::openRequested);
        if (auto *b = strip->findChild<QPushButton *>(QStringLiteral("testCaseStripOpen"))) b->click();
        CHECK(open.count() == 1, "Results… asks for the dock");

        board.setSection(QStringLiteral("SEC"));
        CHECK(board.counts().total() > 0 && board.sectionLabel() == QLatin1String("SEC")
                  && panel.sectionBox()->currentData().toString() == QLatin1String("SEC"),
              "SEC: its own section, and the dock's box follows the board");
        board.setSection(QStringLiteral("1"));

        // -- a reset
        en.resetObservations();
        board.poll();
        CHECK(!board.lastEvent().valid() && board.counts().observed == 0 && board.counts().notAnswered == 0,
              "after a reset: no event shown, nothing counted as seen");
        CHECK(strip->lastText() == QLatin1String("Last: nothing observed yet")
                  && cell306(t, rowOf306(t, QStringLiteral("1.30")), TestCasePanel::ColState) == QLatin1String("not observed"),
              "after a reset: the strip has nothing yet, 1.30 is not observed again");
    }
    {
        TestAssertionEngine en;
        TestCaseBoard board(&en);
        CHECK(en.loadFromJson(R"([{"id":"9.1","title":"t","query":"field:loco_mode=stand_bye"},
                                  {"id":"9.2","title":"u","query":"@lsrp"}])"),
              "a file with one misspelt case loads");
        const TestAssertion *bad = find306(en, QStringLiteral("9.1"));
        CHECK(bad && TestCaseBoard::stateOf(*bad) == CaseState::QueryError
                  && board.counts().queryError == 1 && board.summary().contains(QLatin1String("1 query error")),
              "a case whose query did not parse: 'query error', counted apart");
    }

    // ---- the strip's place in the DMI window and the console ------------
    {
        TestAssertionEngine en;
        en.load(cases);
        TestCaseBoard board(&en);

        DmiWindow dmi(nullptr);
        auto *s1 = new TestCaseStrip(&board, &dmi);
        dmi.setTestStrip(s1);
        CHECK(dmi.testStrip() == s1 && s1->parentWidget() == &dmi && !s1->isHidden(),
              "DMI window: the strip is under the panels, shown with cases loaded");
        auto *again = new TestCaseStrip(&board, &dmi);
        dmi.setTestStrip(again);
        CHECK(dmi.testStrip() == s1, "DMI window: one strip, a second is refused");

        MessageDispatcher disp;
        LocoConsoleWindow console(&disp, nullptr);
        auto *s2 = new TestCaseStrip(&board, nullptr);
        console.setTestStrip(s2);
        CHECK(console.testStrip() == s2 && s2->parentWidget() == console.centralWidget() && !s2->isHidden(),
              "Live Loco Console: the strip under its tabs, shown with cases loaded");
    }

    if (!hadCatalog) cat.clear();
}
