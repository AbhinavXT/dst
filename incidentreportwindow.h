#ifndef INCIDENTREPORTWINDOW_H
#define INCIDENTREPORTWINDOW_H

// The incident report (incidentreport.h) shown, and saved as a single HTML
// file (DMI panels and the speed plot embedded as inline images) that opens
// in any browser and prints to PDF from there. Mirrors RunReportWindow.

#include "incidentreport.h"

#include <QWidget>

class LogModel;
class QTextBrowser;
class StatusLine;

class IncidentReportWindow : public QWidget
{
    Q_OBJECT
public:
    IncidentReportWindow(LogModel *model, const QString &tabKey, const QString &tabName,
                         qint64 atMs, const IncidentReport::Options &options, QWidget *parent = nullptr);
    const IncidentReport::Summary &summary() const { return m_summary; }
    QString html() const { return m_html; }
    bool saveHtml(const QString &path) const;

public slots:
    void rebuild();

private:
    LogModel *m_model = nullptr;
    QString   m_tabKey, m_tabName;
    qint64    m_atMs = 0;
    IncidentReport::Options m_options;
    IncidentReport::Summary m_summary;
    QString   m_html;
    QTextBrowser *m_view = nullptr;
    StatusLine   *m_status = nullptr;
};

#endif // INCIDENTREPORTWINDOW_H
