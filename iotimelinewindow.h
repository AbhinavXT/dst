#ifndef IOTIMELINEWINDOW_H
#define IOTIMELINEWINDOW_H

// Cab inputs and outputs (session 176): iotimeline.h on the fault
// timeline's chart (faulttimelinewindow.h).

#include "faulttimeline.h"

#include <QWidget>

class FaultTimelineCanvas;
class LogModel;
class QLabel;
class StatusLine;

class IoTimelineWindow : public QWidget
{
    Q_OBJECT
public:
    IoTimelineWindow(LogModel *model, const QString &tabKey, const QString &tabName, QWidget *parent = nullptr);
    const FaultTimeline::Timeline &timeline() const { return m_t; }
    FaultTimelineCanvas *canvas() const { return m_canvas; }

public slots:
    void rebuild();

signals:
    void jumpRequested(const QString &tabKey, qint64 epochMs);

private:
    LogModel *m_model = nullptr;
    QString   m_tabKey;
    FaultTimeline::Timeline m_t;
    FaultTimelineCanvas *m_canvas = nullptr;
    QLabel     *m_note = nullptr;
    StatusLine *m_status = nullptr;
};

#endif // IOTIMELINEWINDOW_H
