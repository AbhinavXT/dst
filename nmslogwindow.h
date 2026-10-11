#ifndef NMSLOGWINDOW_H
#define NMSLOGWINDOW_H

// =============================================================================
//  NMS packet log window (session 247) — Tools ▸ Monitor ▸ Equipment ▸
//  NMS health / fault / RSSI log…
//  -----------------------------------------------------------------------------
//  One window per packet type (NmsLog::Kind). It lists every packet of that
//  type the tab already holds, then follows the tab: rows are added as the
//  tab's model grows (live, serial, a log loaded into it), and the table is
//  rebuilt if the tab is cleared. Rows the tab trims at capacity stay here.
//  Export: CSV or .xlsx. Double-click a row: the frame in its tab.
// =============================================================================

#include "logmodel.h"
#include "nmslog.h"

#include <QAbstractTableModel>
#include <QPointer>
#include <QWidget>

class QCheckBox;
class QLabel;
class QPushButton;
class QTableView;
class StatusLine;

class NmsLogTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit NmsLogTableModel(QObject *parent = nullptr) : QAbstractTableModel(parent) {}
    const NmsLog::Table &table() const { return m_table; }
    void reset(const NmsLog::Table &t);
    // Adds packets of `kind` from `entries`; returns how many were added.
    int  append(NmsLog::Kind kind, const QVector<LogEntryPtr> &entries);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation o, int role = Qt::DisplayRole) const override;

private:
    NmsLog::Table m_table;
};

class NmsLogWindow : public QWidget
{
    Q_OBJECT
public:
    NmsLogWindow(NmsLog::Kind kind, LogModel *model, const QString &tabKey, const QString &tabName,
                 QWidget *parent = nullptr);
    NmsLog::Kind kind() const { return m_kind; }
    const NmsLog::Table &table() const { return m_rows->table(); }
    QTableView *view() const { return m_view; }

    // The export buttons' work, without the file dialog (and for the tests).
    bool saveCsv(const QString &path, QString *err = nullptr) const;
    bool saveXlsx(const QString &path, QString *err = nullptr) const;

public slots:
    void rebuild();

signals:
    void jumpRequested(const QString &tabKey, qint64 epochMs);

private:
    void onRowsInserted(int first, int last);
    void exportTo(bool xlsx);
    void showCount();

    NmsLog::Kind        m_kind;
    QPointer<LogModel>  m_model;
    QString             m_tabKey;
    NmsLogTableModel   *m_rows = nullptr;
    QTableView         *m_view = nullptr;
    QCheckBox          *m_follow = nullptr;
    QLabel             *m_count = nullptr;
    StatusLine         *m_status = nullptr;
};

#endif // NMSLOGWINDOW_H
