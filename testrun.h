#ifndef TESTRUN_H
#define TESTRUN_H

// =============================================================================
//  A test run (session 263) — the results of one test campaign over the
//  Test Case Catalog: which clauses were run on which loco and build, and
//  what the tester recorded for each.
//  -----------------------------------------------------------------------------
//  WHO DECIDES
//    The result is the TESTER's, set by hand: Pass, Fail, N/A (does not
//    apply to this loco or build) or Not tested. DLConsole records it, with
//    who set it and when; it never sets or changes a result itself. The
//    report says so at its head.
//
//  ONE FILE PER CAMPAIGN (*.testrun.json)
//    The run's own details (title, loco, firmware build, site, testers,
//    which catalog) and one result per clause id that has one. The catalog
//    is not copied in: a run is read over whichever catalog is open, and a
//    result whose clause the catalog does not have is kept and listed, not
//    dropped. Several runs over the same catalog (two locos, two builds)
//    are several files.
// =============================================================================

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace TestCatalog { struct Catalog; }

namespace TestRun {

enum class Verdict { NotTested, Pass, Fail, NotApplicable };

QString verdictName(Verdict v);              // "Pass", "Fail", "N/A", "Not tested"
QString verdictKey(Verdict v);               // "pass", "fail", "na", "" (the file's)
Verdict verdictFromKey(const QString &key);  // unknown -> NotTested

struct Result {
    Verdict   verdict = Verdict::NotTested;
    QString   tester;
    QDateTime when;            // when the verdict was set
    QString   remarks;
    QString   evidence;        // the log, the time, a file: free text
};

struct Run {
    QString title;
    QString loco;
    QString firmware;
    QString site;
    QString testers;
    QString catalog;           // the catalog it was made over (its file, or "SIF 0533 v4.22")
    QDateTime created;
    QHash<QString, Result> results;   // by clause id; NotTested is not stored
};

struct Counts {
    int pass = 0, fail = 0, na = 0, notTested = 0;
    int total() const { return pass + fail + na + notTested; }
    int done() const { return pass + fail + na; }
};

// The result for a clause (NotTested when it has none).
Result resultOf(const Run &run, const QString &id);
// Record one (NotTested clears it; tester / remarks / evidence are kept).
void setResult(Run *run, const QString &id, const Result &r);

// Over the catalog's clauses (`ids`; empty = every result in the run).
Counts count(const Run &run, const QStringList &ids);
// Results for clauses the catalog does not have (renamed, deleted).
QStringList orphans(const Run &run, const QStringList &ids);

bool load(const QString &path, Run *out, QString *err = nullptr);
bool save(const QString &path, const Run &run, QString *err = nullptr);

// The report: the run, the counts, every failed clause first with its
// remarks, then every clause by section with its result. HTML (prints and
// opens in Word) and CSV (Excel).
QString toHtml(const Run &run, const TestCatalog::Catalog &cat);
QString toCsv(const Run &run, const TestCatalog::Catalog &cat);

}  // namespace TestRun

#endif // TESTRUN_H
