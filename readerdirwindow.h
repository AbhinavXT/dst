#ifndef READERDIRWINDOW_H
#define READERDIRWINDOW_H

// =============================================================================
//  Reader direction window (session 194) — Tools ▸ Monitor ▸ Reader direction…
//  -----------------------------------------------------------------------------
//  The RFID readers' directions, read by read, from @rdir (readerdir.h):
//
//    observed   one row per tag read: time, reader, tag (R1.. by location),
//               Reader-1 / Reader-2 / OVK direction after it, and the tag the
//               loco's next ARP reported to the Stationary KAVACH
//    expected   with an RDSO test case picked (FRS 18.1-18.7): its table, each
//               row "seen at read N" or "not seen in order"; the OK / Not OK
//               column is left for the signatory
//
//  "From read" / "to read" bound the test run (both readers back to U at its
//  start, as at a start of mission; a later run in the same log cannot
//  satisfy its rows). Copy puts both tables on the
//  clipboard, tab-separated, for the test record.
// =============================================================================

#include "readerdir.h"

#include <QWidget>

class MessageDispatcher;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;
class StatusLine;

class ReaderDirWindow : public QWidget
{
    Q_OBJECT
public:
    explicit ReaderDirWindow(MessageDispatcher *dispatcher, QWidget *parent = nullptr);

    void setSource(const QString &key);
    QString sourceKey() const { return m_key; }
    // "18.4", or "" for none.
    void setTestCase(const QString &id);
    void setFromRead(int index);          // 0-based
    void setToRead(int index);            // 0-based; -1 the last

    const ReaderDir::Log &log() const { return m_log; }
    const QVector<ReaderDir::Read> &reads() const { return m_reads; }
    const QVector<int> &matches() const { return m_match; }
    QTableWidget *observedTable() const { return m_observed; }
    QTableWidget *expectedTable() const { return m_expected; }
    QLabel *mapping() const { return m_mapping; }
    StatusLine *status() const { return m_status; }
    QString asText() const;               // what Copy puts on the clipboard

signals:
    void jumpRequested(const QString &key, qint64 ms);

public slots:
    void rebuild();
    void refreshPicker();

private:
    void fill();

    MessageDispatcher *m_dispatcher = nullptr;
    QString m_key;
    ReaderDir::Log m_log;
    QVector<ReaderDir::Read> m_reads;
    QVector<quint16> m_order;
    QVector<int> m_match;

    QComboBox *m_picker = nullptr;
    QComboBox *m_test = nullptr;
    QSpinBox *m_from = nullptr;
    QSpinBox *m_to = nullptr;
    QLabel *m_mapping = nullptr;
    QLabel *m_testInput = nullptr;
    QTableWidget *m_observed = nullptr;
    QTableWidget *m_expected = nullptr;
    QPushButton *m_copy = nullptr;
    StatusLine *m_status = nullptr;
};

#endif // READERDIRWINDOW_H
