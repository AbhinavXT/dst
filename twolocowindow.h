#ifndef TWOLOCOWINDOW_H
#define TWOLOCOWINDOW_H

// The two-loco view's canvas and window (twolocoview.h for the data side).
// Two lanes sharing one time axis: location (both locos, gap shaded between
// them) above, speed (both locos) below. SoS/collision/head-on/rear-end
// events are vertical ticks across both lanes. No zoom/pan in this first
// cut — the whole window is drawn at once; see CHANGELOG session 98.

#include "twolocoview.h"

#include <QWidget>

class MessageDispatcher;
class StatusLine;
class QComboBox;

class TwoLocoCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit TwoLocoCanvas(QWidget *parent = nullptr);
    void setPair(const TwoLocoView::Pair &pair);
    const TwoLocoView::Pair &pair() const { return m_pair; }
    QSize sizeHint() const override { return QSize(960, 560); }

    // The instant last hovered/clicked, or -1. For the tests and the
    // window's readout.
    qint64 cursorMs() const { return m_cursorMs; }

signals:
    void cursorChanged(qint64 ms);   // -1 when the cursor leaves the canvas

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    QRect  locationLane() const;
    QRect  speedLane() const;
    double xOf(qint64 ms) const;
    qint64 msAt(int px) const;
    double yOfLoc(const QRect &r, double locM) const;
    double yOfSpeed(const QRect &r, double kmh) const;
    void   setCursor(qint64 ms);

    TwoLocoView::Pair m_pair;
    qint64 m_minMs = 0, m_maxMs = 1;
    double m_minLocM = 0, m_maxLocM = 1, m_maxSpeedKmh = 1;
    qint64 m_cursorMs = -1;
};

class TwoLocoWindow : public QWidget
{
    Q_OBJECT
public:
    explicit TwoLocoWindow(MessageDispatcher *dispatcher, QWidget *parent = nullptr);
    const TwoLocoView::Pair &pair() const { return m_pair; }
    TwoLocoCanvas *canvas() const { return m_canvas; }
    bool saveImage(const QString &path) const;
    bool saveCsv(const QString &path) const;

    // For the tests: pick A/B directly without driving the combo boxes.
    void setSources(const QString &keyA, const QString &keyB);

public slots:
    void rebuild();

private:
    void refreshPickers();
    void onCursorChanged(qint64 ms);

    MessageDispatcher *m_dispatcher = nullptr;
    QComboBox *m_boxA = nullptr, *m_boxB = nullptr;
    TwoLocoCanvas *m_canvas = nullptr;
    StatusLine *m_status = nullptr;
    TwoLocoView::Pair m_pair;
};

#endif // TWOLOCOWINDOW_H
