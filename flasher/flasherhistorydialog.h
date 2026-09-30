#ifndef FLASHERHISTORYDIALOG_H
#define FLASHERHISTORYDIALOG_H
// =============================================================================
//  flasherhistorydialog.{h,cpp} -- History.dc.html: the audit trail.
//
//  Reads the append-only JSON Lines file every time it is opened or
//  refreshed, so it always shows what is on disk (and a second DLConsole on
//  the same install sees the first one's flashes). Read-only by design:
//  nothing here edits or deletes a record.
// =============================================================================
#include <QDialog>
#include <QList>

#include "flashercore.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTableWidget;

class FlasherHistoryDialog : public QDialog
{
    Q_OBJECT
public:
    FlasherHistoryDialog(const QString &historyPath, QWidget *parent = nullptr);

    // Re-read the file and re-apply the filters.
    void reload();

    int visibleRowCount() const;   // for the tests

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void applyFilters();
    void onRowSelected();
    void exportCsv();

private:
    Flasher::HistoryLog      m_log;
    QList<Flasher::HistoryRecord> m_all;       // newest first
    QList<Flasher::HistoryRecord> m_visible;   // after filters, newest first

    QLineEdit      *m_search = nullptr;
    QComboBox      *m_cardFilter = nullptr;
    QComboBox      *m_resultFilter = nullptr;
    QComboBox      *m_periodFilter = nullptr;
    QTableWidget   *m_table = nullptr;
    QLabel         *m_detailHeader = nullptr;
    QLabel         *m_detailFacts = nullptr;
    QPlainTextEdit *m_detailLog = nullptr;
    QLabel         *m_footer = nullptr;
};

#endif  // FLASHERHISTORYDIALOG_H
