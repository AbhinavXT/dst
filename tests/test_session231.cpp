#include "testutil.h"

#include "layoutaudit.h"
#include "settings.h"
#include "testcatalog.h"
#include "testcatalogwindow.h"
#include "theme.h"
#include "uistyle.h"
#include "undolog.h"

#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSet>
#include <QTemporaryDir>
#include <QTreeWidget>

// =============================================================================
//  Session 231 — Tools ▸ Test Case Catalog: every clause of RDSO SIF 0533
//  v4.22, built in (:/test_catalog/sif0533_v4_22.json, made by
//  scripts/sif_catalog.py from the PDF), and the window that edits it.
//  The clause texts checked here were read off the PDF's pages.
// =============================================================================

TEST_SUITE(session231)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    using namespace TestCatalog;

    // ---- the SIF, built in -------------------------------------------------------------------
    const Catalog &b = builtin();
    CHECK(b.sections.size() == 35 && b.cases.size() == 1887,
          QByteArray("35 sections, 1887 clauses (") + QByteArray::number(b.sections.size()) + ", "
              + QByteArray::number(b.cases.size()) + ")");
    bool numbered = true, codes = true;
    for (int i = 0; i < b.sections.size(); ++i) {
        numbered = numbered && b.sections.at(i).no == i + 1;
        codes = codes && b.sections.at(i).code.startsWith(QStringLiteral("KAVACH_")) && !b.sections.at(i).title.isEmpty();
    }
    CHECK(numbered && codes, "sections 1-35, each with its test case ID and title");
    CHECK(section(b, 4)->title == QLatin1String("Full Supervision Mode") && section(b, 4)->code == QLatin1String("KAVACH_FRS_04"),
          "section 4: KAVACH_FRS_04, Full Supervision Mode");

    // in order, no number twice, none skipped
    bool ordered = true, unique = true, contiguous = true;
    QSet<QString> ids;
    for (int i = 0; i < b.cases.size(); ++i) {
        const QString &id = b.cases.at(i).id;
        unique = unique && !ids.contains(id);
        ids.insert(id);
        if (i > 0) ordered = ordered && compareIds(b.cases.at(i - 1).id, id) < 0;
    }
    for (const QString &id : ids) {
        const QVector<int> p = idParts(id);
        if (p.last() > 1) {
            QStringList prev;
            for (int k = 0; k < p.size() - 1; ++k) prev << QString::number(p.at(k));
            prev << QString::number(p.last() - 1);
            contiguous = contiguous && ids.contains(prev.join(QLatin1Char('.')));
        }
        if (p.size() == 3) contiguous = contiguous && ids.contains(parentOf(id));
    }
    CHECK(ordered && unique && contiguous, "every clause once, in order, no number skipped, every operation under its case");

    auto clause = [&b](const char *id) { return b.cases.at(indexOf(b, QLatin1String(id))); };
    const Case c493 = clause("4.93");
    CHECK(c493.page == 127 && c493.title == QLatin1String("To check FS mode to SR mode transition when both GPS "
                                                          "(communication fail) view fail.")
              && c493.input.endsWith(QStringLiteral("\n4. Now make both GPS communication view failed for 30min."))
              && c493.expected.startsWith(QStringLiteral("1. Onboard shall transit to SR mode.\n2. Fault should to be logged.")),
          "4.93 as page 127 prints it, list items on their own lines");
    CHECK(clause("1.1").page == 19 && clause("35.36").page > 880, "1.1 on page 19, 35.36 at the end");
    const Case c316 = clause("31.6");
    CHECK(c316.title == QLatin1String("Testing of Processing of Various Packet Types by Onboard KAVACH")
              && c316.purpose.startsWith(QStringLiteral("This test is to verify the Onboard Kavach do not process"))
              && c316.methodology.contains(QStringLiteral("2.f. Number of valid bytes received.")),
          "31.6: a radio case with its title, purpose and methodology");
    CHECK(childrenOf(b, QStringLiteral("31.6")).size() == 10, "31.6 has operations 31.6.1-31.6.10");
    const Case c3161 = clause("31.6.1");
    CHECK(c3161.input == QLatin1String("Stationary KAVACH should send a packet with PKT_TYPE as 1000 (Reserved) followed by complete packet.")
              && c3161.inputLabel == QLatin1String("Transmission of packets from Station side")
              && c3161.expectedLabel == QLatin1String("Reception of packets at Onboard side"),
          "31.6.1: the station's side and the onboard's, headed as the SIF heads them");
    CHECK(clause("31.13.22").input == QLatin1String("xviii. Stationary KAVACH should send a valid packet with REF_PROF_ID."),
          "31.13.22: an id printed mid-row keeps the row's first line");
    CHECK(clause("32.1.5").expected == QLatin1String("Stationary KAVACH shall process the information received from "
                                                     "Onboard KAVACH and Send OS MA and track profile, if it is in SR Mode."),
          "32.1.5: section 32's columns (header \"O t / pera-\") read apart");
    CHECK(clause("22.1").expected.contains(QStringLiteral("\n| SR(P1) | ☒ | ☒ |"))
              && section(b, 22)->notes.contains(QStringLiteral("| M1 | No Change |")),
          "22.1's mode matrix keeps its rows (☒ from the Wingdings box); section 22 keeps its M1-M6 legend");
    CHECK(clause("33.3").input.contains(QStringLiteral("\n| 112 | 113 | 113 | 112 |\n")), "33.3's comparator table keeps its cells");
    CHECK(clause("34.1").title == QLatin1String("No of Reads of the Tag")
              && clause("34.1").expected.startsWith(QStringLiteral("The number of reads of full pages")),
          "34.1: section 34's shifted header read as scenario / procedure / expected");
    CHECK(clause("31.2").title == QLatin1String("Testing of Full Duplex communication with RF Channel attenuator")
              && clause("31.2").result.startsWith(QStringLiteral("The Bit Error Rate shall be less than")),
          "31.2: the title out of the Purpose cell; the RF test's expected result");
    bool joined = true, hyphens = false;
    for (const Case &k : b.cases) {
        const QString t = searchText(k);
        joined = joined && !t.contains(QStringLiteral("KA- VACH")) && !t.contains(QStringLiteral("KA-VACH"));
        hyphens = hyphens || t.contains(QStringLiteral("S-KAVACH"));
    }
    CHECK(joined && hyphens, "KA- / VACH joined at line ends; S-KAVACH keeps its hyphen");
    CHECK(!clause("22.1").figurePages.isEmpty() && clause("4.93").figurePages.isEmpty(), "22.1 has a figure, 4.93 none");

    // ---- ids ---------------------------------------------------------------------------------
    CHECK(compareIds(QStringLiteral("4.9"), QStringLiteral("4.10")) < 0
              && compareIds(QStringLiteral("31.6"), QStringLiteral("31.6.1")) < 0
              && compareIds(QStringLiteral("31.6.10"), QStringLiteral("31.7")) < 0,
          "4.9 < 4.10, 31.6 < 31.6.1, 31.6.10 < 31.7");
    CHECK(validId(QStringLiteral("4.93")) && validId(QStringLiteral("31.6.1")) && !validId(QStringLiteral("4"))
              && !validId(QStringLiteral("4.0")) && !validId(QStringLiteral("4.93.")) && !validId(QStringLiteral("a.1")),
          "clause numbers: 4.93 and 31.6.1, not 4, 4.0, 4.93. or a.1");
    CHECK(nextId(b, QStringLiteral("4")) == QLatin1String("4.174") && nextId(b, QStringLiteral("31.6")) == QLatin1String("31.6.11")
              && nextId(b, QStringLiteral("4.93")) == QLatin1String("4.93.1"),
          "the next free numbers: 4.174, 31.6.11, 4.93.1");
    {
        Catalog c = b;
        QString why;
        CHECK(rename(&c, QStringLiteral("31.6"), QStringLiteral("31.70"), &why) && indexOf(c, QStringLiteral("31.70.10")) >= 0
                  && indexOf(c, QStringLiteral("31.6.1")) < 0,
              "renaming 31.6 to 31.70 takes its operations along");
        CHECK(!rename(&c, QStringLiteral("4.93"), QStringLiteral("4.92"), &why) && why.contains(QStringLiteral("taken")),
              "a number in use is refused");
        CHECK(!rename(&c, QStringLiteral("31.70"), QStringLiteral("4.1.1"), &why), "a case with operations cannot become an operation");
        CHECK(!rename(&c, QStringLiteral("4.93"), QStringLiteral("4.93.1"), &why), "nor go under itself");
    }

    // ---- files -------------------------------------------------------------------------------
    QTemporaryDir tmp;
    const QString file = tmp.filePath(QStringLiteral("catalog.json"));
    {
        Catalog c = b;
        c.cases[indexOf(c, QStringLiteral("4.93"))].notes = QStringLiteral("set GPS 1 and 2 off at the IOA");
        QString err;
        CHECK(save(file, c, &err), "a catalog saves");
        Catalog back;
        CHECK(load(file, &back, &err) && back.cases.size() == c.cases.size() && back.sections.size() == 35,
              QByteArray("and loads back (") + err.toUtf8() + ")");
        bool same = true;
        for (int i = 0; i < c.cases.size(); ++i) same = same && back.cases.at(i) == c.cases.at(i);
        for (int i = 0; i < c.sections.size(); ++i) same = same && back.sections.at(i) == c.sections.at(i);
        CHECK(same, "every field of every clause and section survives the round trip");
        CHECK(originOf(back.cases.at(indexOf(back, QStringLiteral("4.93")))) == Origin::Changed
                  && originOf(back.cases.at(indexOf(back, QStringLiteral("4.92")))) == Origin::Sif,
              "4.93 with a note is changed from the SIF; 4.92 is as printed");
    }

    // ---- the window --------------------------------------------------------------------------
    Settings::setTestCatalogLastFile(QString());
    TestCatalogWindow w;
    CHECK(w.reopenLast() && w.catalog().cases.size() == 1887 && !w.isModified() && w.filePath() == builtinPath(),
          "no last file: the SIF as built in opens, unchanged");
    w.show();
    CHECK(w.tree()->topLevelItemCount() == 35, "one row per section");

    w.setSearch(QStringLiteral("GPS view"));
    CHECK(w.shownCount() > 0 && w.select(QStringLiteral("4.93")), "\"GPS view\" finds 4.93");
    w.setSearch(QStringLiteral("31.6"));
    CHECK(w.shownCount() == 11, QByteArray("31.6 finds the case and its 10 operations, not 31.60 (")
                                    + QByteArray::number(w.shownCount()) + ")");
    w.setSearch(QString());
    CHECK(w.select(QStringLiteral("4.93")) && w.editor(TestCatalogWindow::Input)->toPlainText() == c493.input
              && w.idEdit()->text() == QLatin1String("4.93") && w.originLabel()->text().contains(QStringLiteral("page 127")),
          "4.93 selected: its fields and SIF page in the editor");

    // typing in a field is one undo step per field
    w.editor(TestCatalogWindow::Expected)->setPlainText(QStringLiteral("SR mode within 30 min"));
    w.editor(TestCatalogWindow::Expected)->setPlainText(QStringLiteral("SR mode within 30 min, fault to NMS"));
    const Case edited = w.catalog().cases.at(indexOf(w.catalog(), QStringLiteral("4.93")));
    CHECK(w.isModified() && edited.expected == QLatin1String("SR mode within 30 min, fault to NMS")
              && originOf(edited) == Origin::Changed && w.tree()->currentItem()->text(2) == QLatin1String("changed"),
          "an edit goes into the catalog and the list marks 4.93 changed");
    CHECK(w.undoLog()->depth() == 1 && w.undo()
              && w.catalog().cases.at(indexOf(w.catalog(), QStringLiteral("4.93"))) == c493,
          "two edits of one field are one step: Undo puts the SIF's text back");
    w.select(QStringLiteral("4.93"));
    w.setField(TestCatalogWindow::Notes, QStringLiteral("GPS antennas covered"));
    CHECK(w.revertCurrent() && w.catalog().cases.at(indexOf(w.catalog(), QStringLiteral("4.93"))) == c493,
          "Revert to the SIF");

    // new, sub-clause, duplicate, rename, delete, restore
    w.select(QStringLiteral("4"));
    const QString added = w.addClause();
    CHECK(added == QLatin1String("4.174") && w.currentId() == added
              && originOf(w.catalog().cases.at(indexOf(w.catalog(), added))) == Origin::Added,
          "New clause in section 4: 4.174, selected, marked added");
    w.setField(TestCatalogWindow::Title, QStringLiteral("To check FS to SR when one GPS fails for 30 min"));
    CHECK(w.tree()->currentItem()->text(1) == QLatin1String("To check FS to SR when one GPS fails for 30 min"),
          "the list follows the title as it is typed");
    CHECK(w.renameCurrent(QStringLiteral("4.180")) && w.currentId() == QLatin1String("4.180")
              && indexOf(w.catalog(), QStringLiteral("4.174")) < 0,
          "the id box moves a clause");
    CHECK(!w.renameCurrent(QStringLiteral("4.93")) && w.currentId() == QLatin1String("4.180"), "onto a number in use: refused");
    w.select(QStringLiteral("31.6"));
    const QString sub = w.addSubClause();
    const Case subCase = w.catalog().cases.at(indexOf(w.catalog(), sub));
    CHECK(sub == QLatin1String("31.6.11") && subCase.inputLabel == c3161.inputLabel,
          "a sub-clause under 31.6: 31.6.11, its columns headed like its siblings'");
    w.select(QStringLiteral("4.93"));
    const QString dup = w.duplicateCurrent();
    const Case dupCase = w.catalog().cases.at(indexOf(w.catalog(), dup));
    CHECK(dup == QLatin1String("4.181") && dupCase.input == c493.input && dupCase.page == 0
              && dupCase.notes.startsWith(QStringLiteral("Copy of 4.93.")),
          "Duplicate: the next number, the same text, marked a copy");
    const int before = w.catalog().cases.size();
    w.select(QStringLiteral("31.6"));
    CHECK(w.deleteSelected() == 12 && w.catalog().cases.size() == before - 12 && indexOf(w.catalog(), QStringLiteral("31.6.5")) < 0,
          "Delete 31.6 takes its 11 operations with it");
    w.setShow(TestCatalogWindow::ShowDeleted);
    CHECK(w.shownCount() == 11 && w.select(QStringLiteral("31.6.3")) && w.restoreCurrent()
              && indexOf(w.catalog(), QStringLiteral("31.6.3")) >= 0,
          "Show: deleted from the SIF lists them (not 31.6.11, which the SIF never had); Restore puts one back");
    w.setShow(TestCatalogWindow::ShowAll);
    CHECK(w.undo() && indexOf(w.catalog(), QStringLiteral("31.6.3")) < 0 && w.undo()
              && indexOf(w.catalog(), QStringLiteral("31.6.11")) >= 0 && childrenOf(w.catalog(), QStringLiteral("31.6")).size() == 11,
          "Undo the restore, then the delete: all 12 back");
    w.setShow(TestCatalogWindow::ShowAdded);
    CHECK(w.shownCount() == 3, QByteArray("Show: added here: 4.180, 4.181, 31.6.11 (") + QByteArray::number(w.shownCount()) + ")");
    w.setShow(TestCatalogWindow::ShowAll);

    // saving: the built-in copy is never written; a file is, and reopens
    CHECK(!w.saveFile(builtinPath()), "the built-in catalog is not a file to save over");
    const QString mine = tmp.filePath(QStringLiteral("mine.json"));
    CHECK(w.saveFile(mine) && !w.isModified() && Settings::testCatalogLastFile() == mine, "Save as: written, remembered");
    {
        TestCatalogWindow again;
        CHECK(again.reopenLast() && again.filePath() == mine && indexOf(again.catalog(), QStringLiteral("4.181")) >= 0
                  && again.catalog().cases.size() == w.catalog().cases.size(),
              "the window reopens the saved file");
    }
    Settings::setTestCatalogLastFile(QString());

    CHECK(LayoutAudit::orphans(&w).isEmpty(), "no widget outside a layout");
    const QSize min = w.minimumSizeHint();
    CHECK(min.width() <= 1100 && min.height() <= 700,
          QByteArray("fits a laptop (") + QByteArray::number(min.width()) + "x" + QByteArray::number(min.height()) + ")");
}
