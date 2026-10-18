#ifndef SCHEMAPASTEDIALOG_H
#define SCHEMAPASTEDIALOG_H
// =============================================================================
//  schemapastedialog.{h,cpp} -- Schema Editor ▸ Paste… (session 311, phase 4)
//
//  Rows pasted into an enum, flag table or event table: lines copied from a
//  SIF annexure, or two columns from a spreadsheet. The preview lists each
//  row and what it would do (new, a label changed, the same as now) and the
//  lines that were not understood. OK stages them as one step
//  (Session::applyRows); "Replace the whole table" makes every row new.
// =============================================================================
#include <QDialog>

#include "schemaedit.h"

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;

class SchemaPasteDialog : public QDialog
{
    Q_OBJECT

public:
    SchemaPasteDialog(const SchemaEdit::Session &session, int table, QWidget *parent = nullptr);

    QVector<SchemaEdit::TableRow> rows() const { return m_rows; }
    bool replace() const;
    QStringList problems() const { return m_problems; }

    QPlainTextEdit *textEdit() const { return m_text; }
    QCheckBox      *replaceBox() const { return m_replace; }
    QTreeWidget    *previewTree() const { return m_preview; }
    QPushButton    *okButton() const { return m_ok; }

private:
    void update();

    const SchemaEdit::Session &m_session;
    int                        m_table = -1;
    QVector<SchemaEdit::TableRow> m_rows;
    QStringList                m_problems;
    QPlainTextEdit *m_text = nullptr;
    QCheckBox      *m_replace = nullptr;
    QTreeWidget    *m_preview = nullptr;
    QLabel         *m_problemLabel = nullptr;
    QPushButton    *m_ok = nullptr;
};

#endif  // SCHEMAPASTEDIALOG_H
