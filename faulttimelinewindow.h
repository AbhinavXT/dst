#ifndef FAULTTIMELINEWINDOW_H
#define FAULTTIMELINEWINDOW_H

// Fault timeline (session 174): faulttimeline.h drawn as a Gantt chart --
// the mode band on top with each System_Failure marked, one row per module
// below -- and, under it, the faults raised at each System_Failure.

#include "faulttimeline.h"

#include <QWidget>

class LogModel;
class QTextBrowser;
class StatusLine;

class FaultTimelineCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit FaultTimelineCanvas(QWidget *parent = nullptr);
    void setTimeline(const FaultTimeline::Timeline &t);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return QSize(480, 160); }
    QString describeAt(const QPoint &pos) const;   // tooltip text (tests)
    QRect rowRect(int row) const;                  // row -1: the mode band
    int   xAtMs(qint64 ms) const;

signals:
    void timeClicked(qint64 ms);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    bool event(QEvent *e) override;

private:
    qint64 msAtX(int x) const;
    int    labelW() const;
    FaultTimeline::Timeline m_t;
};

class FaultTimelineWindow : public QWidget
{
    Q_OBJECT
public:
    FaultTimelineWindow(LogModel *model, const QString &tabKey, const QString &tabName, QWidget *parent = nullptr);
    const FaultTimeline::Timeline &timeline() const { return m_t; }
    FaultTimelineCanvas *canvas() const { return m_canvas; }
    QString failuresText() const;

public slots:
    void rebuild();

signals:
    void jumpRequested(const QString &tabKey, qint64 epochMs);

private:
    LogModel *m_model = nullptr;
    QString   m_tabKey;
    FaultTimeline::Timeline m_t;
    FaultTimelineCanvas *m_canvas = nullptr;
    QTextBrowser *m_failures = nullptr;
    StatusLine   *m_status = nullptr;
};

#endif // FAULTTIMELINEWINDOW_H
