#ifndef SPEEDDISTANCE_H
#define SPEEDDISTANCE_H

// =============================================================================
//  Speed vs distance (session 81) — Tools ▸ Monitor ▸ Speed vs distance…
//  -----------------------------------------------------------------------------
//  What an ATP run is judged on: the train's actual speed against where it
//  was ON THE TRACK, with the ceiling it was held to. Plotted against time,
//  a stop at a signal and a slow crawl look alike; against distance, the
//  braking into the signal is the shape.
//
//  WHERE THE NUMBERS COME FROM
//    The loco's DMI frame (@dmi) carries, in ONE frame: abs_loco_loc (m),
//    train_speed, speed_limit_permissible, target_distance (m) and
//    target_speed (km/h), and loco_mode. Taking them from one frame is the
//    point: speed from one packet paired with location from another would
//    pair values from different instants.
//    A tab with no DMI falls back to LSRP ABS_LOCO_LOC and TRAIN_SPEED,
//    which has no permitted speed or target; the window says so.
//
//  DIRECTION
//    abs_loco_loc runs one way along the line. A train running towards
//    lower locations is drawn with the axis reversed, so the train always
//    travels left to right; the axis title says which way it counts.
//    Direction is read from the locations themselves (the net change over
//    the run), not from an enum.
//
//  TARGETS
//    A target is target_distance AHEAD of the train in the direction it is
//    travelling, at target_speed. Plotted as a marker at that location;
//    distinct targets once each.
//
//  BRAKING CURVES
//    The firmware's own curves come in @uba frames (see brakingcurves.h),
//    in metres and m/s. The curve in force at the cursor is overlaid. They
//    are only drawn if they fall on the same stretch of track as the DMI
//    trace: a curve kilometres away from every location the train reported
//    is on a different scale, and is reported as such rather than drawn.
//
//  OVERSPEED
//    A sample above its own permitted speed is marked, and counted. It is
//    an observation, not a verdict: margins (warning, FSB, EB) are the
//    firmware's business and are not second-guessed here.
// =============================================================================

#include <QString>
#include <QVector>
#include <QWidget>

class LogModel;
class StatusLine;
class QCheckBox;
class QToolButton;
namespace Braking { struct Snapshot; }

namespace SpeedDistance {

struct Sample {
    int     row = -1;
    qint64  epochMs = 0;
    double  locM = 0.0;
    double  speedKmh = 0.0;
    bool    hasPermitted = false;
    double  permittedKmh = 0.0;
    bool    hasTarget = false;
    double  targetDistM = 0.0;
    double  targetSpeedKmh = 0.0;
    QString mode;                 // display text of the loco mode, if any
    bool overspeed() const { return hasPermitted && speedKmh > permittedKmh; }
};

struct Target {
    double  locM = 0.0;
    double  speedKmh = 0.0;
    qint64  firstMs = 0;
    int     row = -1;
};

struct Trace {
    QString          source;       // "dmi", "lsrp", or empty
    QVector<Sample>  samples;      // time order
    QVector<Target>  targets;      // distinct
    int    direction = 0;          // +1 locations rising, -1 falling, 0 unknown
    int    overspeedSamples = 0;
    double maxSpeedKmh = 0.0;
    double minLocM = 0.0, maxLocM = 0.0;
    int    rowsSkipped = 0;        // speed unidentified (511) or no location
    bool   hitCap = false;
    bool isEmpty() const { return samples.isEmpty(); }
};

// Pull the trace out of a tab: DMI if it has any, else LSRP. fromMs/toMs
// (session 97, both 0 by default) restrict it to that window, as
// fieldplot.h's collectRowFields() already does for one field.
Trace extract(const LogModel *model, int maxRows = 200000, qint64 fromMs = 0, qint64 toMs = 0);

// +1 / -1 / 0 from the locations (sum of consecutive steps, ignoring jumps
// over 500 m which are location resets, not travel).
int travelDirection(const QVector<Sample> &samples);

// A target this far ahead of this location, for this direction.
double targetLocation(double locM, double targetDistM, int direction);

// CSV of the samples between two locations (inclusive, either order).
QString toCsv(const Trace &trace, double fromLocM, double toLocM);

}  // namespace SpeedDistance

class SpeedDistanceCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit SpeedDistanceCanvas(QWidget *parent = nullptr);
    void setTrace(const SpeedDistance::Trace &trace);
    const SpeedDistance::Trace &trace() const { return m_trace; }
    void setBrakingSnapshots(const QVector<Braking::Snapshot> &snaps);
    void setShowPermitted(bool on) { m_showPermitted = on; update(); }
    void setShowTargets(bool on)   { m_showTargets = on; update(); }
    void setShowBraking(bool on)   { m_showBraking = on; update(); }

    bool   isZoomed() const;
    double viewFromM() const { return m_x0; }
    double viewToM()   const { return m_x1; }
    double viewMaxKmh() const { return m_y1; }
    void   setDistanceView(double fromM, double toM);
    void   zoomIn();
    void   zoomOut();
    void   resetZoom();
    // The sample under the cursor (-1 none), and whether a braking curve is
    // drawn for it and why not if not. For the tests and the status line.
    int     cursorSample() const { return m_cursor; }
    void    setCursorSample(int index) { m_cursor = index; update(); }
    QString brakingNote() const { return m_brakingNote; }

signals:
    void sampleActivated(int modelRow, qint64 epochMs);
    void cursorChanged(int sampleIndex);

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void leaveEvent(QEvent *) override;
    QSize sizeHint() const override { return QSize(900, 460); }

private:
    QRect  plotRect() const;
    double xOf(double locM) const;
    double yOf(double kmh) const;
    double locAt(int px) const;
    double kmhAt(int py) const;
    int    nearestSample(int px, int py) const;
    void   fitY();
    void   setView(double x0, double x1);
    int    brakingIndexFor(int sample) const;

    SpeedDistance::Trace m_trace;
    QVector<Braking::Snapshot> *m_snaps = nullptr;   // owned; opaque in the header
    bool   m_showPermitted = true, m_showTargets = true, m_showBraking = true;
    double m_x0 = 0, m_x1 = 1, m_y0 = 0, m_y1 = 1;
    bool   m_yManual = false;
    int    m_cursor = -1;
    QString m_brakingNote;

    enum class Drag { None, Box, Pan };
    Drag   m_drag = Drag::None;
    QPoint m_dragStart, m_dragNow;
    double m_panX0 = 0, m_panX1 = 0;

public:
    ~SpeedDistanceCanvas() override;
};

class SpeedDistanceWindow : public QWidget
{
    Q_OBJECT
public:
    SpeedDistanceWindow(LogModel *model, const QString &tabKey, QWidget *parent = nullptr);
    SpeedDistanceCanvas *canvas() const { return m_canvas; }
    bool saveImage(const QString &path) const;
    bool saveCsv(const QString &path) const;

public slots:
    void reload();

signals:
    void jumpRequested(const QString &tabKey, qint64 epochMs);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void describeTrace();

    LogModel *m_model = nullptr;
    QString   m_tabKey;
    SpeedDistanceCanvas *m_canvas = nullptr;
    StatusLine *m_status = nullptr;
    QCheckBox *m_permitted = nullptr, *m_targets = nullptr, *m_braking = nullptr;
};

#endif // SPEEDDISTANCE_H
