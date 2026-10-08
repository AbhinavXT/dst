#ifndef RUNREPORTWINDOW_H
#define RUNREPORTWINDOW_H

// The run summary (runreport.h) shown, and saved as an HTML file that opens
// in any browser and prints to PDF from there. Session 171: the same window
// shows the mission report (missionreport.h), Kind::Missions.

#include "missionreport.h"
#include "runreport.h"

#include <QWidget>

class LogModel;
class QTextBrowser;
class StatusLine;

class RunReportWindow : public QWidget
{
    Q_OBJECT
public:
    enum class Kind { Run, Missions };
    RunReportWindow(LogModel *model, const QString &tabKey, const QString &tabName, QWidget *parent = nullptr,
                    Kind kind = Kind::Run);
    const RunReport::Summary &summary() const { return m_summary; }
    const QVector<Missions::Mission> &missions() const { return m_missions; }
    Kind kind() const { return m_kind; }
    QString html() const { return m_html; }
    bool saveHtml(const QString &path) const;

public slots:
    void rebuild();

private:
    LogModel *m_model = nullptr;
    QString   m_tabKey, m_tabName;
    Kind      m_kind = Kind::Run;
    RunReport::Summary m_summary;
    QVector<Missions::Mission> m_missions;
    QString   m_html;
    QTextBrowser *m_view = nullptr;
    StatusLine   *m_status = nullptr;
};

#endif // RUNREPORTWINDOW_H
