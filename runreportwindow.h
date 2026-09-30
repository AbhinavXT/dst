#ifndef RUNREPORTWINDOW_H
#define RUNREPORTWINDOW_H

// The run summary (runreport.h) shown, and saved as an HTML file that opens
// in any browser and prints to PDF from there.

#include "runreport.h"

#include <QWidget>

class LogModel;
class QTextBrowser;
class StatusLine;

class RunReportWindow : public QWidget
{
    Q_OBJECT
public:
    RunReportWindow(LogModel *model, const QString &tabKey, const QString &tabName, QWidget *parent = nullptr);
    const RunReport::Summary &summary() const { return m_summary; }
    QString html() const { return m_html; }
    bool saveHtml(const QString &path) const;

public slots:
    void rebuild();

private:
    LogModel *m_model = nullptr;
    QString   m_tabKey, m_tabName;
    RunReport::Summary m_summary;
    QString   m_html;
    QTextBrowser *m_view = nullptr;
    StatusLine   *m_status = nullptr;
};

#endif // RUNREPORTWINDOW_H
