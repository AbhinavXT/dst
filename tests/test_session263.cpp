#include "testutil.h"

#include "settings.h"
#include "statusline.h"
#include "testcatalog.h"
#include "testcatalogwindow.h"
#include "testrun.h"
#include "theme.h"
#include "uistyle.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QTreeWidget>

// =============================================================================
//  Session 263 — Test Case Catalog: Pass / Fail / N/A / Not tested, recorded
//  by the tester in a test run file (one per campaign), over the SIF 0533
//  v4.22 catalog as built in.
// =============================================================================

namespace {
QByteArray readAll263(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
QStringList shownIds(QTreeWidget *tree)
{
    QStringList out;
    std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *it) {
        if (it->data(0, Qt::UserRole).toInt() == 2) out << it->data(0, Qt::UserRole + 1).toString();
        for (int i = 0; i < it->childCount(); ++i) walk(it->child(i));
    };
    for (int i = 0; i < tree->topLevelItemCount(); ++i) walk(tree->topLevelItem(i));
    return out;
}
}  // namespace

TEST_SUITE(session263)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    const QString savedRun = Settings::testRunLastFile();
    const QString savedTester = Settings::testRunTester();
    QTemporaryDir dir;

    // ---- the data ----------------------------------------------------------------------------
    {
        TestRun::Run r;
        r.title = QStringLiteral("FRS 4, loco 21");
        r.loco = QStringLiteral("21");
        r.firmware = QStringLiteral("LKAVACH v1.2.10");
        TestRun::Result pass;
        pass.verdict = TestRun::Verdict::Pass;
        pass.tester = QStringLiteral("A");
        pass.when = QDateTime(QDate(2026, 10, 13), QTime(10, 0));
        TestRun::setResult(&r, QStringLiteral("4.1"), pass);
        TestRun::Result fail = pass;
        fail.verdict = TestRun::Verdict::Fail;
        fail.remarks = QStringLiteral("EB late, \"1.2 s\", see log");
        fail.evidence = QStringLiteral("loco_1_1.cap 13:55:36");
        TestRun::setResult(&r, QStringLiteral("4.3"), fail);
        TestRun::Result na = pass;
        na.verdict = TestRun::Verdict::NotApplicable;
        TestRun::setResult(&r, QStringLiteral("4.4"), na);
        TestRun::Result note;                       // not tested, but a note made
        note.remarks = QStringLiteral("needs the LC gate rig");
        TestRun::setResult(&r, QStringLiteral("4.12"), note);
        TestRun::setResult(&r, QStringLiteral("4.13"), TestRun::Result());      // nothing: not stored
        TestRun::setResult(&r, QStringLiteral("99.1"), pass);                    // not in the catalog
        CHECK(r.results.size() == 5 && !r.results.contains(QStringLiteral("4.13")),
              "a result with nothing in it is not stored; a not-tested note is");

        QStringList ids;
        for (const TestCatalog::Case &k : TestCatalog::builtin().cases) ids << k.id;
        const TestRun::Counts c = TestRun::count(r, ids);
        CHECK(c.pass == 1 && c.fail == 1 && c.na == 1 && c.total() == ids.size() && c.done() == 3,
              QByteArray("counted over the catalog: 1 pass, 1 fail, 1 N/A of ") + QByteArray::number(ids.size()));
        CHECK(TestRun::orphans(r, ids) == QStringList{ QStringLiteral("99.1") }, "a result for a clause the catalog lacks: kept, listed");

        const QString path = dir.filePath(QStringLiteral("a.testrun.json"));
        QString err;
        CHECK(TestRun::save(path, r, &err), QByteArray("saved: ") + err.toUtf8());
        TestRun::Run back;
        CHECK(TestRun::load(path, &back, &err) && back.title == r.title && back.firmware == r.firmware
                  && back.results.size() == 5 && back.results.value(QStringLiteral("4.3")).remarks == fail.remarks
                  && back.results.value(QStringLiteral("4.3")).verdict == TestRun::Verdict::Fail
                  && back.results.value(QStringLiteral("4.1")).when == pass.when
                  && back.results.value(QStringLiteral("4.12")).verdict == TestRun::Verdict::NotTested,
              QByteArray("read back the same: ") + err.toUtf8());
        CHECK(!TestRun::load(dir.filePath(QStringLiteral("none.json")), &back),
              "a missing file is refused");
        QFile other(dir.filePath(QStringLiteral("other.json")));
        other.open(QIODevice::WriteOnly);
        other.write("{\"kind\":\"something else\"}");
        other.close();
        CHECK(!TestRun::load(other.fileName(), &back, &err) && err.contains(QLatin1String("not a DLConsole test run")),
              "another JSON file is refused, said why");

        const QString html = TestRun::toHtml(r, TestCatalog::builtin());
        CHECK(html.contains(QLatin1String("does not set or judge them")) && html.contains(QLatin1String("Failed (1)"))
                  && html.indexOf(QLatin1String("Failed (1)")) < html.indexOf(QLatin1String("<h2>1 "))
                  && html.contains(QLatin1String("LKAVACH v1.2.10")) && html.contains(QLatin1String("99.1")),
              "the report: who decides, the failed clause first, the run's details, the orphan listed");
        const QString csv = TestRun::toCsv(r, TestCatalog::builtin());
        const QStringList lines = csv.trimmed().split(QLatin1Char('\n'));
        CHECK(lines.size() == ids.size() + 1 && lines.first().startsWith(QLatin1String("Clause,Section"))
                  && csv.contains(QLatin1String("\"EB late, \"\"1.2 s\"\", see log\"")),
              "the CSV: a row per clause, quotes and commas escaped");
    }

    // ---- the window -------------------------------------------------------------------------
    {
        TestCatalogWindow w;
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.loadBuiltin();
        w.show();
        QCoreApplication::processEvents();
        w.select(QStringLiteral("4.3"));
        CHECK(!w.hasRun() && w.findChild<QLabel *>(QStringLiteral("testResultNoRun"))->isVisibleTo(&w),
              "with no run open the result card says how to start one");
        w.setVerdict(TestRun::Verdict::Pass);
        CHECK(w.currentResult().verdict == TestRun::Verdict::NotTested, "and setting a result does nothing");

        TestRun::Run details;
        details.title = QStringLiteral("Bench");
        details.loco = QStringLiteral("21");
        const QString path = dir.filePath(QStringLiteral("bench.testrun.json"));
        CHECK(w.newRun(details, path) && QFile::exists(path) && w.hasRun(), "New run writes the file at once");
        CHECK(w.runSummary()->text().contains(QLatin1String("0 Pass")) && w.runSummary()->text().contains(QLatin1String("not tested")),
              QByteArray("the bar counts it: ") + w.runSummary()->text().toUtf8());

        w.setTester(QStringLiteral("Abhinav"));
        w.select(QStringLiteral("4.1"));
        w.setVerdict(TestRun::Verdict::Pass);
        TestRun::Run onDisk;
        TestRun::load(path, &onDisk);
        CHECK(onDisk.results.value(QStringLiteral("4.1")).verdict == TestRun::Verdict::Pass
                  && onDisk.results.value(QStringLiteral("4.1")).tester == QLatin1String("Abhinav")
                  && onDisk.results.value(QStringLiteral("4.1")).when.isValid(),
              "a result is saved to the file the moment it is set, with who and when");
        auto *group = w.resultCard()->findChild<QButtonGroup *>();
        CHECK(group && group->checkedId() == 0, "the card shows Pass checked");

        w.select(QStringLiteral("4.3"));
        w.setVerdict(TestRun::Verdict::Fail);
        w.setRemarks(QStringLiteral("FSB not applied"));
        w.setEvidence(QStringLiteral("loco_1_1.cap 13:55:36"));
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 1200) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        TestRun::load(path, &onDisk);
        CHECK(onDisk.results.value(QStringLiteral("4.3")).remarks == QLatin1String("FSB not applied")
                  && onDisk.results.value(QStringLiteral("4.3")).evidence == QLatin1String("loco_1_1.cap 13:55:36"),
              "remarks and evidence are saved after a pause in typing");

        // Someone else's result shown: the Tester box still says who is at the bench.
        w.setTester(QStringLiteral("Second"));
        w.select(QStringLiteral("4.1"));
        auto *tester = w.findChild<QLineEdit *>(QStringLiteral("testTester"));
        CHECK(tester && tester->text() == QLatin1String("Second") && w.currentResult().tester == QLatin1String("Abhinav"),
              "viewing a result does not change who is testing, nor who set it");
        w.select(QStringLiteral("4.4"));
        w.setVerdict(TestRun::Verdict::NotApplicable);
        CHECK(w.currentResult().tester == QLatin1String("Second"), "the next result goes under the new name");

        // The list.
        auto *result = [&]() {
            for (int i = 0; i < w.tree()->topLevelItemCount(); ++i)
                for (int j = 0; j < w.tree()->topLevelItem(i)->childCount(); ++j)
                    if (w.tree()->topLevelItem(i)->child(j)->text(0) == QLatin1String("4.3")) return w.tree()->topLevelItem(i)->child(j);
            return static_cast<QTreeWidgetItem *>(nullptr);
        }();
        CHECK(result && result->text(3).contains(QLatin1String("Fail")), "the Result column says Fail on 4.3");
        w.setShow(TestCatalogWindow::ShowFailed);
        CHECK(shownIds(w.tree()) == QStringList{ QStringLiteral("4.3") }, "Show: failed in this run");
        w.setShow(TestCatalogWindow::ShowPassed);
        CHECK(shownIds(w.tree()) == QStringList{ QStringLiteral("4.1") }, "Show: passed in this run");
        w.setShow(TestCatalogWindow::ShowUntested);
        const QStringList untested = shownIds(w.tree());
        CHECK(untested.size() == TestCatalog::builtin().cases.size() - 3 && !untested.contains(QStringLiteral("4.4")),
              "Show: not tested yet: every clause but the three");
        w.setShow(TestCatalogWindow::ShowAll);
        w.select(QStringLiteral("4.3"));
        CHECK(w.selectNextUntested() && w.currentId() == QLatin1String("4.5"),
              QByteArray("Next not tested from 4.3 skips 4.4 (N/A) to 4.5: ") + w.currentId().toUtf8());
        const QString html = dir.filePath(QStringLiteral("r.html"));
        CHECK(w.exportReport(html) && readAll263(html).contains("Failed (1)")
                  && w.exportReport(dir.filePath(QStringLiteral("r.csv")))
                  && readAll263(dir.filePath(QStringLiteral("r.csv"))).startsWith("Clause,"),
              QByteArray("Report writes HTML and CSV: ") + w.status()->text().toUtf8());
        w.close();
    }
    {
        // Reopened: the last run comes back with the window.
        TestCatalogWindow again;
        again.setAttribute(Qt::WA_DeleteOnClose, false);
        again.reopenLast();
        CHECK(again.hasRun() && again.runCounts().pass == 1 && again.runCounts().fail == 1 && again.runCounts().na == 1,
              "the window reopens the last run, its results with it");
        again.closeRun();
        CHECK(!again.hasRun() && Settings::testRunLastFile().isEmpty(), "Close run: none reopens next time");
    }
    Settings::setTestRunLastFile(savedRun);
    Settings::setTestRunTester(savedTester);
}
