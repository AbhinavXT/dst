#ifndef SCHEMACONTAINERDIALOG_H
#define SCHEMACONTAINERDIALOG_H
// =============================================================================
//  schemacontainerdialog.{h,cpp} -- Schema Editor ▸ New (session 310, phase 3)
//
//  A new packet, struct, enum, flag table, event table or CRC definition.
//  A packet asks for the captures it decodes, its bit order, CRC and
//  trailer; a struct can be registered as a sub-packet type at once; a CRC
//  definition asks for its parameters. The block is shown as it will be
//  written, and Add stays disabled, with the reason, while the session
//  (previewContainer) would refuse it.
// =============================================================================
#include <QDialog>

#include "schemaedit.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

class SchemaContainerDialog : public QDialog
{
    Q_OBJECT

public:
    SchemaContainerDialog(const SchemaEdit::Session &session, const QString &tag, QWidget *parent = nullptr);

    SchemaEdit::NewContainer container() const;
    QString problem() const { return m_problem; }

    QComboBox   *kindBox() const { return m_kind; }
    QLineEdit   *nameEdit() const { return m_name; }
    QLineEdit   *capturesEdit() const { return m_captures; }
    QComboBox   *wireBox() const { return m_wire; }
    QComboBox   *crcBox() const { return m_crc; }
    QSpinBox    *trailerSpin() const { return m_trailer; }
    QSpinBox    *bodyOffsetSpin() const { return m_bodyOffset; }
    QComboBox   *casePacketBox() const { return m_casePacket; }
    QSpinBox    *caseTypeSpin() const { return m_caseType; }
    QComboBox   *crcTypeBox() const { return m_crcType; }
    QLineEdit   *polyEdit() const { return m_poly; }
    QLineEdit   *initEdit() const { return m_init; }
    QLineEdit   *xorEdit() const { return m_xor; }
    QCheckBox   *reflectInBox() const { return m_reflectIn; }
    QCheckBox   *reflectOutBox() const { return m_reflectOut; }
    QLabel      *previewLabel() const { return m_preview; }
    QPushButton *addButton() const { return m_add; }

private:
    void update();

    const SchemaEdit::Session &m_session;
    QString      m_problem;
    bool         m_capturesTouched = false;
    QComboBox   *m_kind = nullptr;
    QLineEdit   *m_name = nullptr;
    QWidget     *m_packetBox = nullptr;
    QWidget     *m_structBox = nullptr;
    QWidget     *m_crcDefBox = nullptr;
    QLineEdit   *m_captures = nullptr;
    QComboBox   *m_wire = nullptr;
    QComboBox   *m_crc = nullptr;
    QSpinBox    *m_trailer = nullptr;
    QSpinBox    *m_bodyOffset = nullptr;
    QComboBox   *m_casePacket = nullptr;
    QSpinBox    *m_caseType = nullptr;
    QComboBox   *m_crcType = nullptr;
    QLineEdit   *m_poly = nullptr;
    QLineEdit   *m_init = nullptr;
    QLineEdit   *m_xor = nullptr;
    QCheckBox   *m_reflectIn = nullptr;
    QCheckBox   *m_reflectOut = nullptr;
    QSpinBox    *m_crcTrailer = nullptr;
    QLabel      *m_preview = nullptr;
    QLabel      *m_effect = nullptr;
    QLabel      *m_reason = nullptr;
    QPushButton *m_add = nullptr;
};

#endif  // SCHEMACONTAINERDIALOG_H
