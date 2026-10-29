#ifndef TAGEDITDIALOG_H
#define TAGEDITDIALOG_H

// =============================================================================
//  Tag edit dialog (session 401) — one tag's fields, from the RFID Tag
//  Builder's route drawing: double-click a tag to change it, or right-click
//  the line ▸ New tag here… to add one there.
//  -----------------------------------------------------------------------------
//  The type and every field the schema gives that type (coded values by
//  name, Annexure D's description on each), the CRC-30 computed as a field
//  changes, the tag's page_x / page_y shown. OK hands the tag back
//  (TagBuilderWindow puts it in the route, one Undo step); Cancel leaves the
//  route as it was.
// =============================================================================

#include <QByteArray>
#include <QDialog>
#include <QHash>

class QComboBox;
class QFormLayout;
class QLabel;
class QPushButton;

class TagEditDialog : public QDialog
{
    Q_OBJECT
public:
    TagEditDialog(const QByteArray &tag, const QString &title, const QString &okText, QWidget *parent = nullptr);

    QByteArray tag() const { return m_tag; }
    // Set one field by schema name; false if the tag's type has no such field.
    bool setField(const QString &name, qint64 value);
    void setType(int type);
    QPushButton *okButton() const { return m_ok; }

private:
    void rebuildForm();
    void fillForm();
    void buildFromForm();
    void showTag();

    QByteArray m_tag;
    int        m_formType = -1;
    bool       m_updating = false;
    QComboBox *m_type = nullptr;
    QFormLayout *m_form = nullptr;
    QHash<QString, QWidget *> m_editors;
    QLabel    *m_name = nullptr;
    QLabel    *m_pages = nullptr;
    QLabel    *m_crc = nullptr;
    QPushButton *m_ok = nullptr;
};

#endif  // TAGEDITDIALOG_H
