#ifndef SPEEDGAUGE_H
#define SPEEDGAUGE_H

// =============================================================================
//  SpeedGauge  (session 393)
//  -----------------------------------------------------------------------------
//  A round speedometer for the Loco Simulator: 0 to the scale's top over 270
//  degrees, a tick every 5 km/h, numbered every 20 (40 above 200). The band
//  above the limit (the loco's max speed) is red. Editable gauges take a
//  click or drag on the dial (1 km/h steps), the wheel and arrow keys (1),
//  Page Up/Down (10). The same look as PressureGauge.
// =============================================================================

#include <QWidget>

class SpeedGauge : public QWidget
{
    Q_OBJECT

public:
    explicit SpeedGauge(const QString &title, bool editable, QWidget *parent = nullptr);

    void   setScaleMaximum(double kmh);
    double scaleMaximum() const { return m_scaleMax; }
    void   setValue(double kmh);           // does not emit valueEdited
    double value() const { return m_value; }
    void   setLimit(double kmh);           // red above it; negative = none
    double limit() const { return m_limit; }
    void   setTitle(const QString &title);
    void   setCaption(const QString &caption);   // under the reading, e.g. "16753 pulses"

    double valueAt(const QPointF &p) const;  // -1 off the dial

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void valueEdited(double kmh);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

private:
    void   edit(double kmh);
    QRectF dialRect() const;

    QString m_title;
    QString m_caption;
    bool    m_editable;
    double  m_scaleMax = 160;
    double  m_value = 0;
    double  m_limit = -1;
};

#endif  // SPEEDGAUGE_H
