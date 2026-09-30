#ifndef LOCOCONFIGHISTORYDIALOG_H
#define LOCOCONFIGHISTORYDIALOG_H
// =============================================================================
//  lococonfighistorydialog.{h,cpp} -- every LOCO_INFO sent from this PC.
//
//  Reads loco_config_history.jsonl, newest first. Each record holds the
//  exact body that went out, so a past send can be exported again as
//  loco_info.bin (byte-identical) or loaded back into the open
//  configuration -- the way to undo a change on a loco, given that the VCC
//  never reports what it holds. Read-only: nothing here edits the log.
// =============================================================================
#include <QDialog>
#include <QList>

#include "lococonfigcore.h"

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

class LocoConfigHistoryDialog : public QDialog
{
    Q_OBJECT
public:
    LocoConfigHistoryDialog(const QString &historyPath, const LocoInfo::Layout *layout,
                            QWidget *parent = nullptr);
    void reload();
    int  visibleRowCount() const;   // for the tests

signals:
    // "Load into current configuration": the body and a description of the
    // send ("to 192.168.25.168:50001 on 2026-09-25 10:14").
    void loadRequested(const QByteArray &body, const QString &description);

private slots:
    void applyFilter();
    void onSelection();
    void exportSelected();

private:
    const LocoInfo::SendRecord *selectedRecord() const;
    QString describe(const LocoInfo::SendRecord &record) const;

    LocoInfo::SendHistory        m_history;
    const LocoInfo::Layout      *m_layout = nullptr;
    QList<LocoInfo::SendRecord>  m_all;       // newest first
    QList<LocoInfo::Verification> m_verifications;
    const LocoInfo::Verification *verificationFor(const LocoInfo::SendRecord &record) const;
    QList<LocoInfo::SendRecord>  m_visible;

    QLineEdit      *m_search = nullptr;
    QTableWidget   *m_table = nullptr;
    QPlainTextEdit *m_detail = nullptr;
    QPushButton    *m_exportButton = nullptr;
    QPushButton    *m_loadButton = nullptr;
    QLabel         *m_footer = nullptr;
};

#endif  // LOCOCONFIGHISTORYDIALOG_H
