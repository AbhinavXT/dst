#ifndef TRACKDIAGRAMWINDOW_H
#define TRACKDIAGRAMWINDOW_H

// The track diagram's canvas and window (trackdiagram.h for the data side).
// One rail drawn against absolute location: RFID tags (diamonds), signals
// (posts, coloured by aspect) with the movement authority end (a dashed
// line to a small flag), events (ticks), and the loco riding the rail at
// a draggable time cursor. Everything at or before the cursor is drawn at
// full strength; everything still ahead is muted — the one way a static
// diagram can still show "the loco moving along it over time" without
// actually animating the page.

#include "trackdiagram.h"

#include <QWidget>

class LogModel;
class StatusLine;
class QPainter;
class QPushButton;
class QSlider;
class QTimer;

class TrackDiagramCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit TrackDiagramCanvas(QWidget *parent = nullptr);
    void setDiagram(const TrackDiagram::Diagram &diagram);
    const TrackDiagram::Diagram &diagram() const { return m_d; }
    QSize sizeHint() const override { return QSize(1000, 380); }
    QSize minimumSizeHint() const override;   // taller when there are SLRP lanes (session 207)

    // Index into diagram().trace.samples; -1 = before the first sample.
    void setCursorIndex(int index);
    int  cursorIndex() const { return m_cursorIndex; }

    // What is under a point (the hover tooltip's text), for tests.
    struct Hit { QRect rect; QString text; };
    const QVector<Hit> &hits() const { return m_hits; }
    // The boxes of the tag and signal labels drawn, for tests.
    const QVector<QRect> &labelRects() const { return m_labels; }

signals:
    void cursorIndexChanged(int index);

protected:
    void paintEvent(QPaintEvent *) override;
    bool event(QEvent *e) override;   // tooltips: the marks under the pointer

private:
    QRect  trackRect() const;
    double xOf(double locM) const;
    qint64 cursorMsOrLast() const;
    // Session 207: the SLRP lanes (SSP, gradient, TSR, track conditions,
    // tag links) of the profile in force at the cursor. 0 = no @slrp in the tab.
    int  profileLanesHeight(int lineHeight) const;
    void paintProfile(QPainter &p, int top, int lineHeight, qint64 cursorMs);

    TrackDiagram::Diagram m_d;
    int m_cursorIndex = -1;
    QVector<Hit> m_hits;     // rebuilt by every paint
    QVector<QRect> m_labels; // likewise
};

class TrackDiagramWindow : public QWidget
{
    Q_OBJECT
public:
    TrackDiagramWindow(LogModel *model, const QString &tabKey, const QString &tabName, QWidget *parent = nullptr);
    const TrackDiagram::Diagram &diagram() const { return m_diagram; }
    TrackDiagramCanvas *canvas() const { return m_canvas; }
    bool saveImage(const QString &path) const;

public slots:
    void rebuild();

private:
    void onSliderMoved(int value);
    void onPlayToggled(bool on);
    void onTick();

    LogModel *m_model = nullptr;
    QString   m_tabKey, m_tabName;
    TrackDiagram::Diagram m_diagram;
    TrackDiagramCanvas *m_canvas = nullptr;
    QSlider     *m_slider = nullptr;
    QPushButton *m_play   = nullptr;
    QTimer      *m_timer  = nullptr;
    StatusLine  *m_status = nullptr;
};

#endif // TRACKDIAGRAMWINDOW_H
