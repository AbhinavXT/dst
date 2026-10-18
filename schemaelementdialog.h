#ifndef SCHEMAELEMENTDIALOG_H
#define SCHEMAELEMENTDIALOG_H
// =============================================================================
//  schemaelementdialog.{h,cpp} -- Schema Editor ▸ Add (session 308, phase 2)
//
//  A field, a pad or a note, placed relative to the selected element (after,
//  before, inside a condition / repeat / group) or at the start or end of
//  the packet or struct. The dialog shows the line exactly as it will be
//  written, what it does to the sizes, and — with Add disabled — why it
//  cannot be added. The checks are the session's own (previewInsert), so
//  what the dialog accepts is what the session stages.
// =============================================================================
#include <QDialog>

#include "schemaedit.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QWidget;

class SchemaElementDialog : public QDialog
{
    Q_OBJECT

public:
    SchemaElementDialog(const SchemaEdit::Session &session, int anchor, QWidget *parent = nullptr);

    // What Add stages.
    int anchor() const;
    SchemaDoc::Document::Place place() const;
    SchemaEdit::NewElement element() const;

    // Why it cannot be added now, or empty.
    QString problem() const { return m_problem; }

    QComboBox   *kindBox() const { return m_kind; }
    QComboBox   *whereBox() const { return m_where; }
    QLineEdit   *nameEdit() const { return m_name; }
    QComboBox   *typeBox() const { return m_type; }
    QSpinBox    *bitsSpin() const { return m_bits; }
    QSpinBox    *countSpin() const { return m_count; }
    QCheckBox   *signedBox() const { return m_signed; }
    QLineEdit   *unitEdit() const { return m_unit; }
    QComboBox   *enumBox() const { return m_enum; }
    QSpinBox    *hexSpin() const { return m_hex; }
    QSpinBox    *padBitsSpin() const { return m_padBits; }
    QLineEdit   *noteEdit() const { return m_note; }
    QLabel      *previewLabel() const { return m_preview; }
    QLabel      *effectLabel() const { return m_effect; }
    QPushButton *addButton() const { return m_add; }

private:
    void update();

    const SchemaEdit::Session &m_session;
    QString      m_problem;
    QComboBox   *m_kind = nullptr;
    QComboBox   *m_where = nullptr;
    QWidget     *m_fieldBox = nullptr;
    QWidget     *m_padBox = nullptr;
    QWidget     *m_noteBox = nullptr;
    QLineEdit   *m_name = nullptr;
    QComboBox   *m_type = nullptr;
    QSpinBox    *m_bits = nullptr;
    QLabel      *m_countLabel = nullptr;
    QSpinBox    *m_count = nullptr;
    QLabel      *m_bitsLabel = nullptr;
    QCheckBox   *m_signed = nullptr;
    QLineEdit   *m_unit = nullptr;
    QComboBox   *m_enum = nullptr;
    QSpinBox    *m_hex = nullptr;
    QSpinBox    *m_padBits = nullptr;
    QLineEdit   *m_note = nullptr;
    QLabel      *m_preview = nullptr;
    QLabel      *m_effect = nullptr;
    QLabel      *m_reason = nullptr;
    QPushButton *m_add = nullptr;
};

#endif  // SCHEMAELEMENTDIALOG_H
