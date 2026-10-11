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
// =============================================================================

#include "testcatalog.h"

#include <QHash>
#include <QWidget>

class QCheckBox;
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
    enum Show { ShowAll, ShowChanged, ShowAdded, ShowDeleted, ShowFigures };
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
};

#endif // TESTCATALOGWINDOW_H
