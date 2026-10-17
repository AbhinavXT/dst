#ifndef LINFOFIELDDIALOG_H
#define LINFOFIELDDIALOG_H
// =============================================================================
//  LinfoAddFieldDialog / LinfoRemoveFieldDialog (session 307)
//  -----------------------------------------------------------------------------
//  Loco Configuration ▸ ☰ ▸ Fields ▸ Add a field… / Remove a field…, and the
//  same on a row's right-click. The dialog says what will change before
//  anything is written:
//    - the exact kavach.xml line added (or removed), and the file;
//    - LOCO_INFO's size before and after;
//    - that the firmware and the loco_config tool must change the same way,
//      and sync_linfo.py checks it (ADDING_A_FIELD.md).
//  The OK button is enabled only while LinfoEdit accepts the field. The
//  window does the writing (lococonfigwindow.cpp).
// =============================================================================
#include "linfoedit.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

class LinfoAddFieldDialog : public QDialog
{
    Q_OBJECT
public:
    // `afterKey`: preselected; empty = the last member before the CRC.
    LinfoAddFieldDialog(const LocoInfo::Layout &layout, const QStringList &groups,
                        const QString &schemaPath, const QString &afterKey, QWidget *parent = nullptr);

    LinfoEdit::NewField field() const;
    QString problemText() const;      // why Add is disabled, or empty
    QString previewText() const;

    // For the tests.
    QLineEdit   *nameEdit() const { return m_name; }
    QComboBox   *typeBox() const { return m_type; }
    QSpinBox    *countBox() const { return m_count; }
    QComboBox   *afterBox() const { return m_after; }
    QComboBox   *groupBox() const { return m_group; }
    QLineEdit   *noteEdit() const { return m_note; }
    QLineEdit   *defaultEdit() const { return m_default; }
    QCheckBox   *hexBox() const { return m_hex; }
    QPushButton *addButton() const { return m_ok; }

private:
    void refresh();

    const LocoInfo::Layout &m_layout;
    QString      m_schemaPath;
    QLineEdit   *m_name = nullptr;
    QComboBox   *m_type = nullptr;
    QSpinBox    *m_count = nullptr;
    QComboBox   *m_after = nullptr;
    QComboBox   *m_group = nullptr;
    QLineEdit   *m_note = nullptr;
    QLineEdit   *m_default = nullptr;
    QCheckBox   *m_hex = nullptr;
    QLabel      *m_preview = nullptr;
    QLabel      *m_problem = nullptr;
    QPushButton *m_ok = nullptr;
};

class LinfoRemoveFieldDialog : public QDialog
{
    Q_OBJECT
public:
    // `lockedIn`: key -> the saved configurations that have it locked.
    LinfoRemoveFieldDialog(const LocoInfo::Layout &layout, const QString &schemaPath,
                           const QString &key, const QHash<QString, QStringList> &lockedIn,
                           QWidget *parent = nullptr);

    QString key() const;
    QString infoText() const;

    QComboBox   *fieldBox() const { return m_field; }      // for the tests
    QPushButton *removeButton() const { return m_ok; }

private:
    void refresh();

    const LocoInfo::Layout &m_layout;
    QString      m_schemaPath;
    QHash<QString, QStringList> m_lockedIn;
    QComboBox   *m_field = nullptr;
    QLabel      *m_info = nullptr;
    QPushButton *m_ok = nullptr;
};

#endif  // LINFOFIELDDIALOG_H
