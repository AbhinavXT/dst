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
    QScrollArea *m_formArea = nullptr;
    QLabel      *m_changesLabel = nullptr;
    StatusLine  *m_status = nullptr;
    QHash<QString, QWidget *> m_editors;
};

#endif  // SCHEMAEDITORWINDOW_H
