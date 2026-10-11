#ifndef ODOMETRYWINDOW_H
#define ODOMETRYWINDOW_H

// Odometry at the tags (session 213): the summary over one row per tag read
// (odometry.h). Double-click a row: show that read in the log.

#include "odometry.h"

#include <QWidget>

class LogModel;
class QLabel;
class QTableWidget;
class StatusLine;

class OdometryWindow : public QWidget
{
    Q_OBJECT
public:
    OdometryWindow(LogModel *model, const QString &tabKey, const QString &tabName, QWidget *parent = nullptr);
    const Odometry::Report &report() const { return m_report; }
    QTableWidget *table() const { return m_table; }
    QString summary() const;

public slots:
    void rebuild();

signals:
    void jumpRequested(const QString &tabKey, qint64 epochMs);

private:
    LogModel *m_model = nullptr;
    QString   m_tabKey;
    Odometry::Report m_report;
    QLabel       *m_summary = nullptr;
    QTableWidget *m_table = nullptr;
    StatusLine   *m_status = nullptr;
};

#endif // ODOMETRYWINDOW_H
