#ifndef PACKETMATRIXDIALOG_H
#define PACKETMATRIXDIALOG_H

// =============================================================================
//  Packet Matrix (session 232) — from a Test Case Catalog case (31.6), one
//  preset per operation (31.6.1 ... 31.6.10), packetmatrix.h for the rules.
//  -----------------------------------------------------------------------------
//  A base preset (any Packet Maker preset of the packet the case tests), the
//  field the case varies (taken from the SIF's text when the packet has it),
//  and a row per operation: what the SIF sends, the value as printed, the
//  value the preset gets and how it was read. Rows the reading is not sure
//  of say "check"; rows that name another field or no value start unticked.
//  Make presets writes the ticked rows, each built and verified first, and a
//  sequence; Open the sequence loads it into Packet Sequence.
// =============================================================================

#include "packetbuilder.h"
#include "packetmatrix.h"
#include "testcatalog.h"

#include <QDialog>
#include <QHash>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class StatusLine;

class PacketMatrixDialog : public QDialog
{
    Q_OBJECT
public:
    enum Col { ColOn, ColClause, ColSends, ColPrinted, ColValue, ColReading, ColCount };

    // `caseId`: "31.6"; `ops`: its operations, in order.
    PacketMatrixDialog(const TestCatalog::Case &caseInfo, const QVector<TestCatalog::Case> &ops,
                       QWidget *parent = nullptr);

    bool setBasePreset(const QString &path);
    bool setField(const QString &label);            // "PKT_TYPE", "MovementAuthority.CUR_SIG_ASPECT"
    QString field() const;
    void setFolder(const QString &path);
    void setOverwrite(bool on);
    // Make presets, as the button does. The clause -> file of each written.
    QHash<QString, QString> makePresets();
    QString sequencePath() const { return m_seqPath; }

    QTableWidget *table() const { return m_table; }
    StatusLine *status() const { return m_status; }
    const QVector<PacketMatrix::Outcome> &outcomes() const { return m_outcomes; }

signals:
    // Written presets, clause -> file (the catalog notes them).
    void presetsMade(const QHash<QString, QString> &files);

private:
    void fillRows();
    void checkRow(int row);
    const PacketMatrix::FieldRef *currentField() const;

    TestCatalog::Case m_case;
    QVector<TestCatalog::Case> m_ops;
    PacketBuilder m_builder;
    PacketPreset m_base;
    bool m_haveBase = false;
    QVector<PacketMatrix::FieldRef> m_fields;
    QVector<PacketMatrix::Outcome> m_outcomes;
    QString m_seqPath;
    bool m_filling = false;

    QLineEdit *m_basePath = nullptr;
    QLabel *m_baseInfo = nullptr;
    QComboBox *m_fieldBox = nullptr;
    QTableWidget *m_table = nullptr;
    QLineEdit *m_folder = nullptr;
    QSpinBox *m_delay = nullptr;
    QCheckBox *m_overwrite = nullptr;
    QPushButton *m_make = nullptr;
    QPushButton *m_openSeq = nullptr;
    StatusLine *m_status = nullptr;
};

#endif // PACKETMATRIXDIALOG_H
