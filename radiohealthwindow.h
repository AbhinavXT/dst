#ifndef RADIOHEALTHWINDOW_H
#define RADIOHEALTHWINDOW_H

// Radio health (session 173): radiohealth.h's report drawn as time strips --
// the DMI's signal bars with the no-radio and radio-hole spells, radio not
// OK, radio and PA temperatures, forward power, GSM RSSI -- one time axis.
// Hover for the values at a moment; click to jump the log there.

#include "radiohealth.h"

#include <QWidget>

class LogModel;
class QLabel;
class StatusLine;

class RadioHealthCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit RadioHealthCanvas(QWidget *parent = nullptr);
    void setReport(const RadioHealth::Report &r);
    QSize sizeHint() const override { return QSize(900, 620); }
    QSize minimumSizeHint() const override { return QSize(480, 440); }
    // For the tests: the strip rectangles, and the text a hover at `ms` shows.
    QVector<QRect> strips() const;
    QString describeAt(qint64 ms) const;
    qint64 msAtX(int x) const;
    int    xAtMs(qint64 ms) const;

signals:
    void timeClicked(qint64 ms);

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    RadioHealth::Report m_r;
    qint64 m_hoverMs = -1;
};

class RadioHealthWindow : public QWidget
{
    Q_OBJECT
public:
    RadioHealthWindow(LogModel *model, const QString &tabKey, const QString &tabName, QWidget *parent = nullptr);
    const RadioHealth::Report &report() const { return m_report; }
    RadioHealthCanvas *canvas() const { return m_canvas; }
    QString summary() const;

public slots:
    void rebuild();

signals:
    void jumpRequested(const QString &tabKey, qint64 epochMs);

private:
    LogModel *m_model = nullptr;
    QString   m_tabKey;
    RadioHealth::Report m_report;
    RadioHealthCanvas *m_canvas = nullptr;
    QLabel     *m_summary = nullptr;
    StatusLine *m_status = nullptr;
};

#endif // RADIOHEALTHWINDOW_H
