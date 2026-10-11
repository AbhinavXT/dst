#ifndef FLEETWINDOW_H
#define FLEETWINDOW_H

// Fleet summary (session 215): a folder of logs, one row per loco per day
// (fleetsummary.h). Read one group at a time on the GUI thread, painting
// between; Stop ends it after the frame batch in hand.

#include "fleetsummary.h"

#include <QWidget>

class QLineEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class StatusLine;

class FleetWindow : public QWidget
{
    Q_OBJECT
public:
    explicit FleetWindow(QWidget *parent = nullptr);
    const QVector<Fleet::Row> &rows() const { return m_rows; }
    QTableWidget *table() const { return m_table; }
    StatusLine *status() const { return m_status; }

public slots:
    void run(const QString &dir);
    void stop() { m_stop = true; }

private:
    void addRow(const Fleet::Row &r);

    QVector<Fleet::Row> m_rows;
    bool m_running = false, m_stop = false;
    QLineEdit    *m_folder = nullptr;
    QPushButton  *m_start = nullptr;
    QPushButton  *m_export = nullptr;
    QProgressBar *m_progress = nullptr;
    QTableWidget *m_table = nullptr;
    StatusLine   *m_status = nullptr;
};

#endif // FLEETWINDOW_H
