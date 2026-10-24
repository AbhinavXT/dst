#ifndef PRESSUREGAUGE_H
#define PRESSUREGAUGE_H

// =============================================================================
//  PressureGauge  (session 351)
//  -----------------------------------------------------------------------------
//  A round pressure dial for the Cab I/O Simulator's analog card: 0 to the
//  sensor's full scale x 1.02 kg/cm^2 over 270 degrees. Click or drag on the
//  dial to set it (0.1 steps); the wheel and arrow keys nudge by 0.1, Page
//  Up/Down by 1. A channel that reads no pressure (open loop, short) shows
//  why instead of a value and parks the needle at zero.
// =============================================================================

#include <QWidget>

class PressureGauge : public QWidget
{
    Q_OBJECT

public:
    explicit PressureGauge(const QString &title, QWidget *parent = nullptr);

    void   setMaximum(double kgcm2);
    double maximum() const { return m_max; }
    void   setValue(double kgcm2);         // does not emit valueEdited
    double value() const { return m_value; }
    // Shown instead of the value (e.g. "open loop"); empty = a reading.
    void   setFault(const QString &why);
    QString fault() const { return m_fault; }

    // The value under a point of the widget, snapped to 0.1; -1 off the dial.
    double valueAt(const QPointF &p) const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void valueEdited(double kgcm2);       // the operator moved it

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

private:
    void edit(double kgcm2);
    QRectF dialRect() const;

    QString m_title;
    QString m_fault;
    double  m_max   = 10.2;
    double  m_value = 0;
};

#endif  // PRESSUREGAUGE_H
