#ifndef TESTCATALOG_H
#define TESTCATALOG_H

// =============================================================================
//  Test case catalog (session 231) — the data side of Tools ▸ Test Case
//  Catalog… (testcatalogwindow.h).
//  -----------------------------------------------------------------------------
//  Every clause of RDSO SIF 0533 v4.22 ("Functional Test Cases at System
//  Level for KAVACH"), by section and number, as the document prints them:
//  test scenario, input specification, expected output, and for the radio
//  packet sections (31-33) the case's purpose / methodology and each
//  operation's two sides and expected result. Built in from
//  :/test_catalog/sif0533_v4_22.json, which scripts/sif_catalog.py makes
//  from the PDF; edited, added to and deleted from in the window, and saved
//  as a file of the same format.
//
//  Ids: "4.93" is clause 93 of section 4; "31.6.1" is operation 1 under
//  case 31.6. They order as numbers (4.9 < 4.10), never as text.
//
//  The built-in copy is the SIF as printed. A clause that differs from it
//  is "changed"; one it does not hold is "added"; one of its clauses
//  missing from a file is "deleted". The window shows all three and can
//  put a clause back as the SIF prints it.
// =============================================================================

#include <QString>
#include <QVector>

namespace TestCatalog {

struct Section {
    int     no = 0;
    QString code;          // KAVACH_FRS_04
    QString title;         // Full Supervision Mode
    QString description;   // the Test Definitions table's
    QString heading;       // the heading row as printed over the clauses
    QString notes;         // what the section prints under its heading (22: the M1-M6 legend)
    int     page = 0;      // the SIF's printed page number; 0 = not from the SIF
    bool operator==(const Section &o) const;
};

struct Case {
    QString id;
    QString title;         // Test Scenario (a radio case: its heading)
    QString purpose, methodology, setup;   // a radio case's rows (31.x, 32.x)
    QString input;         // Input Specification / Transmission of packets from ...
    QString expected;      // Expected Output/Values / Reception of packets at ...
    QString result;        // Expected result (sections 31-33, the third column)
    QString observed;      // what the Observed column prints (mode lists, matrices)
    QString inputLabel, expectedLabel;     // the two columns' headings when not the usual
    QString notes;         // the operator's own; never in the SIF
    int     page = 0;      // the SIF's printed page it starts on; 0 = added here
    QVector<int> pages;    // every page it runs over, when more than one
    QVector<int> figurePages;              // pages with a figure the text cannot carry
    bool operator==(const Case &o) const;
    bool operator!=(const Case &o) const { return !(*this == o); }
};

struct Catalog {
    QString source;
    QVector<Section> sections;
    QVector<Case> cases;     // ordered by id
};

// The SIF as built into the program.
QString builtinPath();
const Catalog &builtin();

bool load(const QString &path, Catalog *out, QString *err = nullptr);
bool save(const QString &path, const Catalog &c, QString *err = nullptr);

// ---- ids --------------------------------------------------------------------
bool validId(const QString &id);              // "4.93" or "31.6.1"
QVector<int> idParts(const QString &id);
int  compareIds(const QString &a, const QString &b);   // <0, 0, >0, numerically
int  sectionOf(const QString &id);
QString parentOf(const QString &id);           // "31.6" for "31.6.1"; "" for "4.93"
int  depth(const QString &id);                 // 1 for "4.93", 2 for "31.6.1"

void sort(Catalog *c);
int  indexOf(const Catalog &c, const QString &id);   // -1 if absent
int  sectionIndex(const Catalog &c, int no);
const Section *section(const Catalog &c, int no);
// The ids under `id` (its sub-clauses), in order.
QStringList childrenOf(const Catalog &c, const QString &id);
// The next free number: under a section ("4" -> "4.175") or a clause
// ("31.6" -> "31.6.11", "4.93" -> "4.93.1").
QString nextId(const Catalog &c, const QString &under);

// Renames `from` to `to`, its sub-clauses with it ("31.6" -> "31.70" makes
// "31.6.1" "31.70.1"). Refused, with the reason, when `to` is not an id,
// is taken, or a clause would land under itself.
bool rename(Catalog *c, const QString &from, const QString &to, QString *why = nullptr);

// ---- against the SIF ---------------------------------------------------------
enum class Origin { Sif, Changed, Added };
Origin originOf(const Case &k);               // against builtin()
QVector<Case> deletedFromSif(const Catalog &c);

// Every text of a clause in one string, for searching.
QString searchText(const Case &k);

} // namespace TestCatalog

#endif // TESTCATALOG_H
