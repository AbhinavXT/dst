#ifndef BRAKINGCURVEPLOT_H
#define BRAKINGCURVEPLOT_H

// =============================================================================
//  BrakingCurvePlot
//  ---------------------------------------------------------------------------
//  Draws a braking curve the way a driver reads one: SPEED on y, DISTANCE
//  along the track on x. The firmware stores the inverse (location as a
//  quadratic in speed), so all of the inversion lives in brakingcurves.cpp and
//  this widget only paints what it is handed.
//
//  WHAT IT SHOWS
//    - one polyline per active CURVE_SEGMENT, each in its own colour, so the
//      deceleration steps the curve is built from stay individually legible
//    - segment boundaries as tick marks: the junctions are where the curve's
//      shape is decided, and they are invisible on a smooth line
//    - the target: a vertical marker at target_location with its type
//    - optional GHOSTS — earlier snapshots drawn faintly behind the current
//      one, so a curve that is collapsing towards the loco shows as movement
//      rather than as a single still frame
//    - a crosshair on hover reading out (distance, allowed speed) evaluated
//      through the real curve equation, not interpolated off the polyline
//
//  AXES
//    Speed is shown in km/h because that is what the cab shows and what the
//    profile is specified in; the wire and all internal maths stay in m/s and
//    the conversion happens only at the label. Distance is absolute track
//    location in metres, unshifted — an operator correlating this against an
//    RFID location or an MA end point needs the same number, not a
//    curve-relative one.
// =============================================================================

#include <QVector>
#include <QWidget>

#include "brakingcurves.h"

class BrakingCurvePlot : public QWidget
{
    Q_OBJECT

public:
    explicit BrakingCurvePlot(QWidget *parent = nullptr);

    // The curve currently under the scrubber.
    void setSnapshot(const Braking::Snapshot &snap);

    // A whole computation cycle: every target ahead of the loco, each with its
    // own curve, plus the most-restrictive envelope across them.
    void setCycle(const Braking::Cycle &cycle);

    // Draw the envelope (the lowest allowed speed across all targets). Off by
    // default for a single target, where it is just the curve again.
    void setShowEnvelope(bool on);

    // Faint earlier curves drawn behind it. Pass an empty vector to clear.
    void setGhosts(const QVector<Braking::Snapshot> &ghosts);

    // Which curve to draw: -1 = all of them. With two curves (EBD and SBD)
    // both on one chart the SBD is dashed, so they stay distinguishable
    // without a legend having to be read.
    void setCurveFilter(int curveIndex);

    // A location the operator searched for. Drawn as a persistent marker with
    // the allowed speed there, so a location search answers "what did the
    // curve permit at this chainage" and not only "which frame".
    void setQueryLocation(double metres, bool active);

    // Draw a marker for where the loco is, when it is known. Not part of the
    // @uba frame — the caller supplies it from the DMI stream if it has one.
    void setLocoLocation(double metres, bool known);

    // Freeze the axes across snapshots. Without this the axes re-fit on every
    // frame and a curve that is not moving LOOKS like it is moving, which is
    // the opposite of what a time scrubber is for.
    void setLockedRange(bool on, double minLoc = 0, double maxLoc = 0,
                        double maxSpeedKmph = 0);

    QSize sizeHint() const override { return QSize(720, 380); }
    QSize minimumSizeHint() const override { return QSize(320, 200); }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct Range { double x0 = 0, x1 = 1, y0 = 0, y1 = 1; };

    Range computeRange() const;
    QPointF toPx(const Range &r, const QRectF &plot, double x, double y) const;

    Braking::Snapshot         m_snap;     // frames.at(0) — kept for the target marker
    Braking::Cycle            m_cycle;
    bool                      m_showEnvelope = true;
    QVector<Braking::Snapshot> m_ghosts;

    int    m_curveFilter = -1;

    double m_queryLoc    = 0.0;
    bool   m_queryActive = false;

    double m_locoLoc   = 0.0;
    bool   m_locoKnown = false;

    bool   m_locked        = false;
    double m_lockMinLoc    = 0.0;
    double m_lockMaxLoc    = 0.0;
    double m_lockMaxSpeed  = 0.0;   // km/h

    bool   m_hover  = false;
    QPoint m_hoverPx;
};

#endif // BRAKINGCURVEPLOT_H
