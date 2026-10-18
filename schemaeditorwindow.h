#ifndef SCHEMAEDITORWINDOW_H
#define SCHEMAEDITORWINDOW_H
// =============================================================================
//  schemaeditorwindow.{h,cpp} -- Tools ▸ Schema ▸ Schema Editor (session 308)
//
//  Three panes, left to right:
//    OUTLINE     every packet, struct, enum, flag table, event table and CRC
//                definition in kavach.xml, with a find box;
//    MEMBERS     what the selected one holds, in file order: a packet's
//                fields with their bits and bit offsets, an enum's values, a
//                flag table's bits;
//    PROPERTIES  the selected element's attributes as a form, with what each
//                one does, the file's own comment above it, and its start tag
//                as it now reads.
//
//  Edits are staged (SchemaEdit::Session): each is checked, and refused with
//  the reason if the decoder would not load the result. Undo / Redo walk
//  them. Review & save shows every change and the line diff, then writes the
//  file (backup beside it) and MainWindow reloads the decoder.
//
//  The built-in schema is shown read-only, with an offer to save an editable
//  copy and use it from then on.
// =============================================================================
#include <QDialog>
#include <QHash>
#include <QPointer>

#include "schemaedit.h"

class QAction;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
class QFormLayout;
class QScrollArea;
class StatusLine;

class SchemaEditorWindow : public QDialog
{
    Q_OBJECT

public:
    // Opens `path` (":/schema/kavach.xml" = the built-in copy, read-only).
    explicit SchemaEditorWindow(const QString &path, QWidget *parent = nullptr);
    ~SchemaEditorWindow() override;

    const SchemaEdit::Session &session() const { return m_session; }

    // Select an element (a node index of session().doc()) everywhere.
    void selectNode(int node);
    int  selectedNode() const { return m_selected; }
    // Select a container by tag and name ("packet", "SLRP").
    bool selectContainer(const QString &tag, const QString &name);

    // The edits the form makes, callable directly (the tests do).
    bool editAttribute(int node, const QString &attr, const QString &value);
    bool dropAttribute(int node, const QString &attr);
    bool rename(int node, const QString &newName, bool withReferences);
    // Phase 2: structure. Each selects what it made (or what is now there).
    bool insertElement(int anchor, SchemaDoc::Document::Place place, const SchemaEdit::NewElement &element);
    bool deleteSelected();
    bool moveSelected(int delta);
    bool duplicateSelected();
    // Opens the Add dialog for the selection.
    void addElement();
    // Phase 3: whole packets, structs, enums, tables, CRC definitions —
    // the container selected in the outline.
    bool addContainer(const SchemaEdit::NewContainer &container);
    void newContainer(const QString &tag);          // the New dialog
    bool duplicateContainer();
    bool moveContainer(int delta);
    bool deleteContainer(bool ask = true);
    bool registerStruct(const QString &packet, int type);
    void useAsSubpacket();                           // the dialog for registerStruct
    bool undo();
    bool redo();

    // Writes the staged edits. *summary says what changed, for the
    // notification.
    bool save(QString *summary, QString *error);
    // Before → after sizes of every packet / struct whose fixed size changed.
    QStringList sizeChanges() const;
    // What the review dialog shows.
    QStringList reviewLines() const;

    QTreeWidget *outline() const { return m_outline; }
    QTreeWidget *members() const { return m_members; }
    QLineEdit   *findEdit() const { return m_find; }
    StatusLine  *status() const { return m_status; }
    QPushButton *saveButton() const { return m_saveButton; }
    QToolButton *addButton() const { return m_addButton; }
    QToolButton *deleteButton() const { return m_deleteButton; }
    QToolButton *upButton() const { return m_upButton; }
    QToolButton *downButton() const { return m_downButton; }
    QToolButton *duplicateButton() const { return m_duplicateButton; }
    QToolButton *newButton() const { return m_newButton; }
    QAction     *containerDuplicateButton() const { return m_cDuplicateButton; }
    QToolButton *containerUpButton() const { return m_cUpButton; }
    QToolButton *containerDownButton() const { return m_cDownButton; }
    QAction     *containerDeleteButton() const { return m_cDeleteButton; }
    QToolButton *subpacketButton() const { return m_subpacketButton; }
    // The form's editor for one attribute of the selected element, or null.
    QWidget *editorFor(const QString &attr) const { return m_editors.value(attr); }

    // Rebuild every pane now (normally queued after an edit).
    void refreshNow();

signals:
    // The file was written; MainWindow reloads the decoder if it is the one in use.
    void schemaSaved(const QString &path, const QString &summary);
    // An editable copy of the built-in schema was made and set as the
    // external schema.
    void schemaFileChanged(const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;
    void reject() override;

private:
    void buildUi();
    void openPath(const QString &path);
    void scheduleRefresh();
    void fillOutline();
    void syncOutline();
    void fillMembers();
    void fillForm();
    void addMemberItems(QTreeWidgetItem *parent, int node, int offset);
    void applyFind();
    void updateActions();
    void showResult(bool ok, const QString &why);
    // After an edit that may renumber elements: select `node`, or the
    // element now at the old selection's place.
    void reselect(int node, const QVector<int> &oldPath);
    void memberMenu(const QPoint &pos);
    void outlineMenu(const QPoint &pos);
    void makeEditableCopy();
    void reviewAndSave();
    void askRename(int node, const QString &newName);
    QWidget *makeEditor(int node, const SchemaEdit::AttrSpec &spec, bool present, const QString &value);

    SchemaEdit::Session  m_session;
    int                  m_container = -1;
    int                  m_selected = -1;
    bool                 m_refreshQueued = false;
    bool                 m_asking = false;   // the rename question is up
    QString              m_refocus;      // the attribute whose editor had focus

    QLabel      *m_fileLabel = nullptr;
    QLabel      *m_readOnlyBanner = nullptr;
    QPushButton *m_copyButton = nullptr;
    QPushButton *m_undoButton = nullptr;
    QPushButton *m_redoButton = nullptr;
    QPushButton *m_discardButton = nullptr;
    QPushButton *m_saveButton = nullptr;
    QLineEdit   *m_find = nullptr;
    QTreeWidget *m_outline = nullptr;
    QLabel      *m_membersTitle = nullptr;
    QLabel      *m_membersInfo = nullptr;
    QTreeWidget *m_members = nullptr;
    QToolButton *m_addButton = nullptr;
    QToolButton *m_duplicateButton = nullptr;
    QToolButton *m_upButton = nullptr;
    QToolButton *m_downButton = nullptr;
    QToolButton *m_deleteButton = nullptr;
    QToolButton *m_subpacketButton = nullptr;
    QToolButton *m_newButton = nullptr;
    QAction     *m_cDuplicateButton = nullptr;
    QToolButton *m_cUpButton = nullptr;
    QToolButton *m_cDownButton = nullptr;
    QAction     *m_cDeleteButton = nullptr;
    QScrollArea *m_formArea = nullptr;
    QLabel      *m_changesLabel = nullptr;
    StatusLine  *m_status = nullptr;
    QHash<QString, QWidget *> m_editors;
};

#endif  // SCHEMAEDITORWINDOW_H
