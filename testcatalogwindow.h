#ifndef TESTCATALOGWINDOW_H
#define TESTCATALOGWINDOW_H

// =============================================================================
//  Tools ▸ Test Case Catalog… (session 231) — testcatalog.h for the data.
//  -----------------------------------------------------------------------------
//  Left: every clause of SIF 0533 v4.22 by section (and a radio case's
//  operations under it), a search box over all their text, and Show:
//  every clause / changed from the SIF / added here / deleted from the SIF /
//  with a figure. Right: the selected clause, every field editable in place
//  (Ctrl+Z undoes, one step per field edited), its SIF page, whether the SIF
//  draws a figure the text cannot carry, and Revert to the SIF's text. New
//  clause / sub-clause / section, Duplicate, Delete (sub-clauses go with
//  their clause). Open / Save a catalog file; the SIF as built in is never
//  overwritten.
//
//  Session 263: TEST RUNS (testrun.h). A run is one campaign's results over
//  the catalog, in its own file, saved as results are set. The bar under
//  the buttons names the open run and counts it; each clause has a result
//  card: Pass / Fail / N/A / Not tested (Ctrl+1 / 2 / 3 / 4), who set it and
//  when, remarks, evidence. The list gets a Result column and Show: failed /
//  passed / not tested yet. The result is the tester's, set by hand.
// =============================================================================

#include "testcatalog.h"

#include <QHash>
#include <QWidget>

#include "testrun.h"

class QButtonGroup;
class QCheckBox;
class QFrame;
class QTimer;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class StatusLine;
class UndoLog;

class TestCatalogWindow : public QWidget
{
    Q_OBJECT
public:
    enum Show { ShowAll, ShowChanged, ShowAdded, ShowDeleted, ShowFigures,
                ShowFailed, ShowPassed, ShowUntested };      // the last three: session 263
    enum Field { Title, Purpose, Methodology, Setup, Input, Expected, Result, Observed, Notes, FieldCount };

    explicit TestCatalogWindow(QWidget *parent = nullptr);

    // The file last opened (Settings), else the SIF as built in.
    bool reopenLast();
    bool loadFile(const QString &path);
    void loadBuiltin();
    bool saveFile(const QString &path);
    const TestCatalog::Catalog &catalog() const { return m_cat; }
    QString filePath() const { return m_path; }
    bool isModified() const { return m_modified; }

    // What the list shows.
    void setSearch(const QString &text);
    void setShow(Show s);
    int shownCount() const { return m_shown; }
    bool matches(const TestCatalog::Case &k) const;     // against the search box

    // Select a clause ("4.93") or a section ("4"); false if not shown.
    bool select(const QString &id);
    QString currentId() const { return m_current; }     // clause id, "S4" for a section, "" for none

    // Editing the selected clause, as typing into its field does.
    void setField(Field f, const QString &text);
    QString field(Field f) const;
    bool renameCurrent(const QString &to);              // as the id box does
    QString addClause();                // in the selected clause's section (or the selected section)
    QString addSubClause();             // under the selected clause; "" when it is a sub-clause itself
    bool addSection(int no, const QString &title);
    QString duplicateCurrent();
    int deleteSelected();               // clauses removed, sub-clauses included
    bool revertCurrent();               // the SIF's text back
    bool restoreCurrent();              // a clause deleted from the SIF, put back
    bool undo();

    // Session 232: the case the Packet Matrix would make presets for — the
    // selected clause when it has operations, its case when it is one.
    QString matrixCase() const;
    class PacketMatrixDialog *openMatrix();
    // "Preset: <file>" into each clause's notes, one undo step.
    void notePresets(const QHash<QString, QString> &files);

    // Session 263: test runs. newRun writes `path` at once; every result set
    // after is saved to it (remarks and evidence after a pause in typing).
    bool newRun(const TestRun::Run &details, const QString &path);
    bool openRun(const QString &path);
    void closeRun();
    bool hasRun() const { return !m_runPath.isEmpty(); }
    const TestRun::Run &run() const { return m_run; }
    QString runPath() const { return m_runPath; }
    TestRun::Counts runCounts() const;
    // On the selected clause, as the result card does.
    void setVerdict(TestRun::Verdict v);
    void setTester(const QString &name);
    void setRemarks(const QString &text);
    void setEvidence(const QString &text);
    TestRun::Result currentResult() const;
    // The next clause in list order with no result after the selected one
    // (wrapping); false when every shown clause has one.
    bool selectNextUntested();
    // .html or .csv, by the file's extension.
    bool exportReport(const QString &path);
    QLabel *runSummary() const { return m_runSummary; }
    QFrame *resultCard() const { return m_resultCard; }

    QTreeWidget *tree() const { return m_tree; }
    QPlainTextEdit *editor(Field f) const { return m_edit[f]; }
    QLineEdit *idEdit() const { return m_idEdit; }
    QLabel *countLabel() const { return m_count; }
    QLabel *originLabel() const { return m_origin; }
    StatusLine *status() const { return m_status; }
    UndoLog *undoLog() const { return m_undo; }

signals:
    void fileChanged(const QString &path);

protected:
    void closeEvent(QCloseEvent *e) override;
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    void rebuildTree();
    // Session 263
    void buildRunBar(class QVBoxLayout *root);
    QFrame *buildResultCard();
    void showResult();
    void updateResultItems();
    void updateRunBar();
    void restyleVerdicts();
    void saveRunNow();
    void runChanged(bool soon = false);
    void showCurrent();
    void showClause(const TestCatalog::Case &k, bool deleted);
    void showSection(const TestCatalog::Section &s);
    void updateItem(const QString &id);
    void updateTitle();
    void updateCount();
    void pushUndo(const QString &label);
    void setModified(bool m);
    bool confirmDiscard();
    void fit(QPlainTextEdit *e);
    QString itemText(const TestCatalog::Case &k) const;
    QList<QTreeWidgetItem *> selectedItems() const;

    TestCatalog::Catalog m_cat;
    QString m_path;                 // a file, or TestCatalog::builtinPath()
    bool m_modified = false;
    QString m_current;
    QString m_editKey;              // "<id>/<field>" of the edit step that is open
    bool m_filling = false;         // the editor is being filled, not typed into
    int m_shown = 0;
    Show m_show = ShowAll;
    QHash<QString, QTreeWidgetItem *> m_items;

    QLineEdit *m_search = nullptr;
    QComboBox *m_showBox = nullptr;
    QLabel *m_count = nullptr;
    QTreeWidget *m_tree = nullptr;
    QStackedWidget *m_pages = nullptr;
    QLabel *m_hint = nullptr;
    // clause page
    QLineEdit *m_idEdit = nullptr;
    QLabel *m_heading = nullptr;
    QLabel *m_origin = nullptr;
    QLabel *m_figure = nullptr;
    QPushButton *m_revert = nullptr;
    QPushButton *m_restore = nullptr;
    QPushButton *m_matrix = nullptr;
    QCheckBox *m_allFields = nullptr;
    QLabel *m_label[FieldCount] = {};
    QPlainTextEdit *m_edit[FieldCount] = {};
    QScrollArea *m_scroll = nullptr;
    // section page
    QLabel *m_secHeading = nullptr;
    QLineEdit *m_secCode = nullptr;
    QLineEdit *m_secTitle = nullptr;
    QPlainTextEdit *m_secDesc = nullptr;
    QPlainTextEdit *m_secNotes = nullptr;

    QToolButton *m_newBtn = nullptr;
    QPushButton *m_dupBtn = nullptr;
    QPushButton *m_delBtn = nullptr;
    UndoLog *m_undo = nullptr;
    StatusLine *m_status = nullptr;

    // Session 263: the test run.
    TestRun::Run m_run;
    QString m_runPath;
    QTimer *m_runSave = nullptr;
    bool m_runDirty = false;
    QLabel *m_runName = nullptr;
    QLabel *m_runSummary = nullptr;
    QWidget *m_runProgress = nullptr;
    QPushButton *m_reportBtn = nullptr;
    QPushButton *m_closeRunBtn = nullptr;
    QFrame *m_resultCard = nullptr;
    QWidget *m_resultBody = nullptr;
    QLabel *m_noRun = nullptr;
    QButtonGroup *m_verdicts = nullptr;
    QLineEdit *m_tester = nullptr;
    QLabel *m_when = nullptr;
    QPlainTextEdit *m_remarks = nullptr;
    QLineEdit *m_evidence = nullptr;
    QPushButton *m_nextUntested = nullptr;
};

#endif // TESTCATALOGWINDOW_H
