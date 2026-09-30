#ifndef BRAKINGCURVES_H
#define BRAKINGCURVES_H

// =============================================================================
//  BrakingCurves
//  ---------------------------------------------------------------------------
//  The braking-curve model, with no Qt-widget dependency, so it can be unit
//  tested and reused by anything that needs a curve rather than only by the
//  panel that happens to draw one today.
//
//  WHAT A CURVE IS
//    The loco's uniform-braking module builds, for each target, a piecewise
//    curve of ALLOWED SPEED against TRACK LOCATION. Each piece is one
//    CURVE_SEGMENT, and the firmware stores it as a quadratic in SPEED, not
//    in distance (curve_manager.c, CreateCurveSegment):
//
//        location = A * speed^2 + C
//        A = -1 / (2a)                      a = net deceleration, m/s^2
//        C = (v_final^2 + 2*a*end_loc) / (2a)
//
//    So to DRAW it — speed on y, distance on x, which is how a driver reads
//    it — the relation has to be inverted:
//
//        speed = sqrt((location - C) / A)
//
//    Both A and (location - C) are negative inside the braking region, so the
//    quotient is positive and the root is real. Outside it the roots go
//    imaginary, which the firmware treats as "permitted speed 0"
//    (GetSpeedAtLocationOnCurve); speedAt() reports it as "not on this
//    segment" instead and lets the caller decide, because a plot that draws a
//    confident 0 km/h where the curve simply does not reach is worse than a
//    plot with a gap in it.
//
//  THE HORIZONTAL SPECIAL CASE
//    CreateCurveSegmentHorizontalLine stores A = 0 and C = speed — a flat
//    speed limit, not a quadratic. Inverting blindly divides by zero, so A==0
//    is branched on everywhere. This is the one case that silently produces
//    inf/NaN if it is forgotten, and a NaN in a QPainterPath takes the whole
//    polyline with it.
//
//  EMPTY SLOTS
//    CURVE is a fixed array; unused slots are all-zero (InitCurve memsets).
//    The firmware's IsCurveRangeValid is a memcmp against zero, reproduced
//    here as isEmpty(). An all-zero slot is NOT a segment at the origin.
//
//  WHY THE LAYOUT IS DETECTED RATHER THAN FIXED
//    The declared Target_Internal is  Target + CURVE[2], CURVE being
//    CURVE_SEGMENT[2*MAX_DEC_UNITS+1] = 11, i.e. 17 + 2*11*48 = 1073 B, with
//    curves_for_target[] indexed [UBA_EBD]=0 and [UBA_SBD]=1. Every @uba frame
//    captured so far is 452 B = 17 + 9*48 + 3 — ONE curve of nine slots, not
//    two of eleven. Whatever emits the capture is not writing the whole
//    struct, and that emitter has not been seen.
//
//    So the size of the frame picks the layout, instead of a constant doing
//    it. A build that starts sending the full struct is then decoded as the
//    two curves it is, rather than being rejected or — far worse — parsed with
//    the wrong stride into a plausible-looking wrong curve.
// =============================================================================

#include <QByteArray>
#include <QDateTime>
#include <QPointF>
#include <QString>
#include <QVector>

class LogModel;

namespace Braking {

constexpr int kSegmentBytes = 48;   // 6 unaligned doubles
constexpr double kMpsToKmph = 3.6;

// How a given payload size maps onto Target + curves.
struct Layout {
    int  targetBytes      = 17;  // 2 doubles + a ONE-byte enum (-fshort-enums)
    int  curves           = 1;
    int  segmentsPerCurve = 9;
    int  trailing         = 3;   // padding after the struct
    bool documented       = false;  // matches a layout we can name
    // True only when the frame carries curves_for_target[] itself, so a curve
    // index really is a CURVE_TYPE. A single-curve frame does NOT say which of
    // EBD/SBD it holds — the firmware picks by brk_type, which is not on the
    // wire — so its curve stays unnamed rather than being guessed at.
    bool curveNamesKnown  = false;
    QString name;

    int size() const
    { return targetBytes + curves * segmentsPerCurve * kSegmentBytes + trailing; }
    int totalSegments() const { return curves * segmentsPerCurve; }
};

// Pick the layout for a payload size. Falls back to "as many whole 48-byte
// segments as fit after a 17-byte Target", flagged undocumented, so an
// unfamiliar frame still yields something inspectable AND says so.
Layout detectLayout(int payloadBytes);

// One piece of the piecewise curve, exactly the firmware's CURVE_SEGMENT.
struct Segment {
    double a         = 0.0;   // eqn.A  = -1/(2*decel)   (0 => horizontal line)
    double c         = 0.0;   // eqn.C
    double startLoc  = 0.0;   // m, the high-speed end
    double endLoc    = 0.0;   // m, the low-speed end (nearer the target)
    double higherSpeed = 0.0; // m/s at startLoc
    double lowerSpeed  = 0.0; // m/s at endLoc

    // Mirrors IsCurveRangeValid(): an all-zero slot is an unused array entry.
    bool isEmpty() const;

    // A == 0 means CreateCurveSegmentHorizontalLine: a flat speed limit at C.
    bool isHorizontal() const { return a == 0.0; }

    // Net deceleration this segment was built with, m/s^2. Meaningless for a
    // horizontal segment, which is why it is reported separately from a.
    bool   hasDecel() const { return a != 0.0; }
    double decel()    const;

    // Allowed speed (m/s) at a track location, or false if the location is
    // outside [startLoc, endLoc] or the roots are imaginary.
    bool speedAt(double location, double *speedOut) const;

    // Location (m) at which the curve reaches a given speed, or false if that
    // speed is outside [lowerSpeed, higherSpeed].
    bool locationAt(double speed, double *locationOut) const;
};

// One CURVE out of curves_for_target[].
struct Curve {
    int     index = 0;          // 0 = UBA_EBD, 1 = UBA_SBD
    QString name;               // "EBD" / "SBD" / "curve N"
    QVector<Segment> segments;  // all slots, empty ones included

    // Active slots in TRACK order (lowest startLoc first). The firmware fills
    // the array target-first, which is the reverse of how it is drawn.
    QVector<Segment> activeSegments() const;
    int  activeCount() const;

    bool span(double *minLoc, double *maxLoc,
              double *minSpeed, double *maxSpeed) const;

    // Allowed speed across the whole curve. Mirrors GetSpeedAtLocationOnCurve:
    // the segment containing the location wins.
    bool speedAt(double location, double *speedOut) const;
};

// One @uba frame: a target plus the curve(s) computed towards it.
struct Snapshot {
    bool      valid   = false;
    int       row     = -1;        // row in the source LogModel, for jumping
    qint64    epochMs = 0;         // host receive time (what the scrubber uses)
    QDateTime rtc;                 // loco RTC from the capture line, 1 s
    quint32   seq     = 0;

    double targetLocation = 0.0;   // m
    double targetSpeed    = 0.0;   // m/s
    int    targetType     = 0;     // UBA_TARGET_TYPE
    QString targetTypeName;

    Layout          layout;
    QVector<Curve>  curves;

    // Cheap exact identity of the payload bytes. Two frames with the same
    // fingerprint are the same frame, full stop — no tolerance argument.
    // Used to answer "how many DISTINCT curves are in this session", which in
    // the captures seen so far is 1 out of 4694.
    quint64 fingerprint = 0;

    int  curveCount()  const { return curves.size(); }
    int  activeCount() const;                    // across every curve

    // Union of all curves. Returns false if nothing is active.
    bool span(double *minLoc, double *maxLoc,
              double *minSpeed, double *maxSpeed) const;

    // Allowed speed on one curve, or on the first curve that covers the
    // location when curveIndex < 0.
    bool speedAt(double location, double *speedOut, int curveIndex = -1) const;
};

QString targetTypeName(int type);
QString curveName(int index, bool namesKnown = true);

// ---- cycles -------------------------------------------------------------
//
// The loco recalculates EVERY target ahead of it on a 10 ms cycle and builds
// both curves for each, so one cycle produces several @uba frames — one per
// target (PSR, EOA, SVL, …) — not one. The frame carries no target index and
// no cycle counter, so the grouping below is a HEURISTIC: a new cycle starts
// when either the time gap exceeds maxGapMs, or a (target_type, location) pair
// that is already in the current cycle appears again.
//
// That is sound for well-formed data and will mis-group if two distinct
// targets share a type AND a location, or if a cycle straddles the gap
// threshold. Adding a target index and count to the capture emitter would make
// it exact and cost two bytes; until then `heuristic` stays true on every
// Cycle so nothing downstream mistakes this for authoritative framing.
struct Cycle {
    qint64            epochMs = 0;   // first frame in the group
    QVector<Snapshot> frames;        // one per target
    bool              heuristic = true;

    int targetCount() const { return frames.size(); }

    // Union extent over every target's curve.
    bool span(double *minLoc, double *maxLoc,
              double *minSpeed, double *maxSpeed) const;

    // The lowest allowed speed across all targets at a location — the curve
    // the loco is actually held to. Reports which target imposed it, because
    // "why am I being braked here" is answered by the target, not the speed.
    bool mostRestrictive(double location, double *speedOut,
                         int *frameIndexOut = nullptr, int curveIndex = -1) const;
};

QVector<Cycle> groupIntoCycles(const QVector<Snapshot> &snaps, qint64 maxGapMs = 5);

// Did the curve meaningfully change between two frames? Deliberately NOT
// fingerprint equality: on a moving train every frame differs in the last
// decimal place, which would make "jump to the next change" useless by
// flagging every frame. This asks the operator's question instead — did the
// target move or change type, did a segment appear or disappear, did a
// segment boundary shift — with a tolerance in metres and m/s.
bool significantlyDifferent(const Snapshot &a, const Snapshot &b,
                            double locTol = 0.5, double speedTol = 0.1);

// Decode one @uba payload. Returns an invalid Snapshot only if the payload is
// too short to hold a Target and one segment — a frame that small means the
// struct changed, and half a curve is not a safe thing to draw.
Snapshot parseFrame(const QByteArray &payload);

// Sample ONE curve into polylines in (distance m, speed m/s). Segments are
// sampled independently and returned separately, so a discontinuity between
// two segments stays visible instead of being joined by a line the curve does
// not contain.
QVector<QVector<QPointF>> polylines(const Curve &curve, int samplesPerSegment = 48);

// Walk a LogModel and decode every @uba row into a snapshot, oldest first.
// `cap` bounds the work on a long session; `hitCap` reports whether it bit.
QVector<Snapshot> collect(const LogModel *model, int cap, bool *hitCap = nullptr);

// Index of the snapshot at or before a time, or -1. Snapshots must be sorted
// by epochMs, which collect() guarantees.
int indexAtOrBefore(const QVector<Snapshot> &snaps, qint64 epochMs);

} // namespace Braking

#endif // BRAKINGCURVES_H
