#ifndef RADIOCHARTDIALOG_H
#define RADIOCHARTDIALOG_H

// =============================================================================
//  Radio chart… (session 345) — the dialog over radiochart.h, opened from a
//  log row's right-click ("Radio chart around here…", the row's moment
//  ±30 s). From / To; the chart drawn as they change; Export PNG… / SVG….
// =============================================================================

#include <QDialog>
#include <QPointer>

#include "radiochart.h"

class LogModel;
class QDateTimeEdit;
class QLabel;
class QPushButton;
class StatusLine;

class RadioChartDialog : public QDialog
{
    Q_OBJECT
public:
    RadioChartDialog(LogModel *model, const QString &tabName, qint64 centreMs, QWidget *parent = nullptr);

    void setRange(qint64 fromMs, qint64 toMs);
    const RadioChart::Chart &chart() const { return m_chart; }
    // Write the chart to `path` (.svg or .png). False, with the reason on the
    // status line, when refused.
    bool exportTo(const QString &path);
    QLabel *summaryLabel() const { return m_summary; }
    QLabel *picture() const { return m_picture; }
    StatusLine *status() const { return m_status; }

signals:
    void exported(const QString &path);   // session 385: a chart written

private:
    void rebuild();
    void exportAs(const QString &suffix, const QString &filter);

    QPointer<LogModel> m_model;
    QString m_tabName;
    RadioChart::Chart m_chart;
    QDateTimeEdit *m_from = nullptr;
    QDateTimeEdit *m_to = nullptr;
    QLabel *m_summary = nullptr;
    QLabel *m_picture = nullptr;
    QPushButton *m_png = nullptr;
    QPushButton *m_svg = nullptr;
    StatusLine *m_status = nullptr;
};

#endif // RADIOCHARTDIALOG_H
