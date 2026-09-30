#include "brakingcurves.h"

#include "capturedecoder.h"
#include "logmodel.h"

#include <QSet>
#include <QtGlobal>
#include <cmath>
#include <cstring>

namespace Braking {

namespace {

// Little-endian IEEE-754 double at a byte offset. The struct is packed, so
// offsets are not 8-aligned and the bytes cannot be reinterpret_cast in place.
double leDouble(const QByteArray &b, int off)
{
    quint64 bits = 0;
    for (int i = 0; i < 8; ++i) {
        bits |= quint64(quint8(b[off + i])) << (8 * i);
    }
    double d = 0.0;
    std::memcpy(&d, &bits, sizeof(d));
    return d;
}

bool finitePair(double x, double y)
{
    return std::isfinite(x) && std::isfinite(y);
}

} // namespace

// ---- Segment ------------------------------------------------------------

bool Segment::isEmpty() const
{
    // The firmware's validity test is a memcmp against a zeroed struct, so
    // every field has to be zero — not just the equation. A real segment can
    // legitimately have A == 0 (a horizontal line) and C == 0.
    return a == 0.0 && c == 0.0 && startLoc == 0.0 && endLoc == 0.0
           && higherSpeed == 0.0 && lowerSpeed == 0.0;
}

double Segment::decel() const
{
    // A = -1/(2a)  =>  a = -1/(2A)
    if (a == 0.0) { return 0.0; }
    return -1.0 / (2.0 * a);
}

bool Segment::speedAt(double location, double *speedOut) const
{
    if (!speedOut || isEmpty()) { return false; }

    // Tolerance because start/end are themselves computed from the equation
    // and a location taken from the previous segment's boundary can land a
    // few ulp outside this one.
    const double lo = qMin(startLoc, endLoc) - 1e-6;
    const double hi = qMax(startLoc, endLoc) + 1e-6;
    if (location < lo || location > hi) { return false; }

    if (isHorizontal()) {          // CreateCurveSegmentHorizontalLine: v = C
        if (!std::isfinite(c)) { return false; }
        *speedOut = c;
        return true;
    }

    const double q = (location - c) / a;
    if (!(q >= 0.0) || !std::isfinite(q)) { return false; }   // imaginary roots
    const double v = std::sqrt(q);
    if (!std::isfinite(v)) { return false; }
    *speedOut = v;
    return true;
}

bool Segment::locationAt(double speed, double *locationOut) const
{
    if (!locationOut || isEmpty()) { return false; }

    const double lo = qMin(lowerSpeed, higherSpeed) - 1e-9;
    const double hi = qMax(lowerSpeed, higherSpeed) + 1e-9;
    if (speed < lo || speed > hi) { return false; }

    if (isHorizontal()) {
        // A flat segment holds one speed across a range of locations; there is
        // no single location to return. Report the low-speed end, which is
        // what the firmware's GetLocationFromSpeedOnCurve effectively yields.
        *locationOut = endLoc;
        return true;
    }

    const double loc = a * speed * speed + c;      // GetYFromX
    if (!std::isfinite(loc)) { return false; }
    *locationOut = loc;
    return true;
}

// ---- Snapshot -----------------------------------------------------------

Layout detectLayout(int payloadBytes)
{
    Layout l;

    // Documented shapes, most specific first.
    if (payloadBytes == 17 + 9 * kSegmentBytes + 3) {
        // What every capture seen so far carries. One curve of nine slots and
        // a 3-byte tail — consistent with a 449 B struct padded to a 4-byte
        // boundary by the framing.
        l = { 17, 1, 9, 3, true, false,
              QStringLiteral("1 curve x 9 segments (as captured) — EBD or SBD, "
                             "selected by brk_type, not stated in the frame") };
    } else if (payloadBytes == 17 + 2 * 11 * kSegmentBytes) {
        // The declared Target_Internal: Target + CURVE[NUM_OF_CURVES] with
        // CURVE_SEGMENT[2*MAX_DEC_UNITS+1].
        l = { 17, 2, 11, 0, true, true,
              QStringLiteral("full Target_Internal (EBD + SBD, 11 segments each)") };
    } else if (payloadBytes == 20 + 2 * 11 * kSegmentBytes) {
        // Same, from a build WITHOUT -fshort-enums, so target_type is 4 bytes.
        l = { 20, 2, 11, 0, true, true,
              QStringLiteral("full Target_Internal, 4-byte enum") };
    } else if (payloadBytes == 17 + 11 * kSegmentBytes) {
        l = { 17, 1, 11, 0, true, false,
              QStringLiteral("1 curve x 11 segments — EBD or SBD, selected by "
                             "brk_type, not stated in the frame") };
    } else {
        // Unfamiliar. Decode as many whole segments as fit after a 17-byte
        // Target and say plainly that the shape is unrecognised, rather than
        // forcing it into a known layout and drawing a wrong curve.
        const int avail = payloadBytes - 17;
        const int segs  = (avail > 0) ? (avail / kSegmentBytes) : 0;
        l.targetBytes      = 17;
        l.curves           = 1;
        l.segmentsPerCurve = segs;
        l.trailing         = (avail > 0) ? (avail % kSegmentBytes) : 0;
        l.documented       = false;
        l.curveNamesKnown  = false;
        l.name = QStringLiteral("unrecognised %1 B — read as 1 curve x %2 segments")
                     .arg(payloadBytes).arg(segs);
    }
    return l;
}

QString curveName(int index, bool namesKnown)
{
    // Only name a curve EBD/SBD when the frame actually carries the array and
    // the index therefore means CURVE_TYPE.
    //
    // It does NOT when a single curve arrives. uniform_braking.c selects one:
    //     if (brk_type == UBA_EB)  mrdt_curve = curves_for_target[UBA_EBD];
    //     else if (brk_type == UBA_FSB) mrdt_curve = curves_for_target[UBA_SBD];
    // so which of the two is on the wire depends on brk_type at that instant,
    // and brk_type is not in the frame. Calling it "EBD" would be a guess
    // dressed as a fact — and a guess that inverts the meaning of the plot
    // whenever the loco is in full-service braking.
    if (!namesKnown) { return QStringLiteral("curve"); }
    switch (index) {
    case 0: return QStringLiteral("EBD");   // curve_manager.h CURVE_TYPE
    case 1: return QStringLiteral("SBD");
    default: return QStringLiteral("curve %1").arg(index);
    }
}

QVector<Segment> Curve::activeSegments() const
{
    QVector<Segment> out;
    out.reserve(segments.size());
    for (const Segment &s : segments) {
        if (!s.isEmpty()) { out.push_back(s); }
    }
    // Firmware fills the array target-first (the segment nearest the target is
    // slot 0), which is the reverse of track order. Sort so a plot walks left
    // to right without the caller having to know that.
    std::sort(out.begin(), out.end(), [](const Segment &x, const Segment &y) {
        return x.startLoc < y.startLoc;
    });
    return out;
}

int Curve::activeCount() const
{
    int n = 0;
    for (const Segment &s : segments) { if (!s.isEmpty()) { ++n; } }
    return n;
}

bool Curve::span(double *minLoc, double *maxLoc,
                 double *minSpeed, double *maxSpeed) const
{
    const QVector<Segment> act = activeSegments();
    if (act.isEmpty()) { return false; }

    double lo = act.first().startLoc, hi = act.first().startLoc;
    double vlo = act.first().lowerSpeed, vhi = act.first().lowerSpeed;
    for (const Segment &s : act) {
        lo  = qMin(lo,  qMin(s.startLoc, s.endLoc));
        hi  = qMax(hi,  qMax(s.startLoc, s.endLoc));
        vlo = qMin(vlo, qMin(s.lowerSpeed, s.higherSpeed));
        vhi = qMax(vhi, qMax(s.lowerSpeed, s.higherSpeed));
    }
    if (!finitePair(lo, hi) || !finitePair(vlo, vhi)) { return false; }

    if (minLoc)   { *minLoc   = lo;  }
    if (maxLoc)   { *maxLoc   = hi;  }
    if (minSpeed) { *minSpeed = vlo; }
    if (maxSpeed) { *maxSpeed = vhi; }
    return true;
}

bool Curve::speedAt(double location, double *speedOut) const
{
    for (const Segment &s : segments) {
        if (s.isEmpty()) { continue; }
        if (s.speedAt(location, speedOut)) { return true; }
    }
    return false;
}

// ---- Snapshot (aggregate over curves) -----------------------------------

int Snapshot::activeCount() const
{
    int n = 0;
    for (const Curve &c : curves) { n += c.activeCount(); }
    return n;
}

bool Snapshot::span(double *minLoc, double *maxLoc,
                    double *minSpeed, double *maxSpeed) const
{
    bool any = false;
    double lo = 0, hi = 0, vlo = 0, vhi = 0;
    for (const Curve &c : curves) {
        double a = 0, b = 0, x = 0, y = 0;
        if (!c.span(&a, &b, &x, &y)) { continue; }
        if (!any) { lo = a; hi = b; vlo = x; vhi = y; any = true; }
        else {
            lo  = qMin(lo, a);  hi  = qMax(hi, b);
            vlo = qMin(vlo, x); vhi = qMax(vhi, y);
        }
    }
    if (!any) { return false; }
    if (minLoc)   { *minLoc   = lo;  }
    if (maxLoc)   { *maxLoc   = hi;  }
    if (minSpeed) { *minSpeed = vlo; }
    if (maxSpeed) { *maxSpeed = vhi; }
    return true;
}

bool Snapshot::speedAt(double location, double *speedOut, int curveIndex) const
{
    if (curveIndex >= 0) {
        if (curveIndex >= curves.size()) { return false; }
        return curves.at(curveIndex).speedAt(location, speedOut);
    }
    for (const Curve &c : curves) {
        if (c.speedAt(location, speedOut)) { return true; }
    }
    return false;
}

// ---- cycles -------------------------------------------------------------

bool Cycle::span(double *minLoc, double *maxLoc,
                 double *minSpeed, double *maxSpeed) const
{
    bool any = false;
    double lo = 0, hi = 0, vlo = 0, vhi = 0;
    for (const Snapshot &f : frames) {
        double a = 0, b = 0, x = 0, y = 0;
        if (!f.span(&a, &b, &x, &y)) { continue; }
        a = qMin(a, f.targetLocation);
        b = qMax(b, f.targetLocation);
        if (!any) { lo = a; hi = b; vlo = x; vhi = y; any = true; }
        else {
            lo  = qMin(lo, a);  hi  = qMax(hi, b);
            vlo = qMin(vlo, x); vhi = qMax(vhi, y);
        }
    }
    if (!any) { return false; }
    if (minLoc)   { *minLoc   = lo;  }
    if (maxLoc)   { *maxLoc   = hi;  }
    if (minSpeed) { *minSpeed = vlo; }
    if (maxSpeed) { *maxSpeed = vhi; }
    return true;
}

bool Cycle::mostRestrictive(double location, double *speedOut,
                            int *frameIndexOut, int curveIndex) const
{
    bool found = false;
    double best = 0.0;
    int    bestIdx = -1;
    for (int i = 0; i < frames.size(); ++i) {
        double v = 0.0;
        if (!frames.at(i).speedAt(location, &v, curveIndex)) { continue; }
        if (!found || v < best) { best = v; bestIdx = i; found = true; }
    }
    if (!found) { return false; }
    if (speedOut)      { *speedOut = best; }
    if (frameIndexOut) { *frameIndexOut = bestIdx; }
    return true;
}

QVector<Cycle> groupIntoCycles(const QVector<Snapshot> &snaps, qint64 maxGapMs)
{
    QVector<Cycle> out;
    Cycle cur;
    QSet<QString> seen;          // (type, location) already in this cycle

    auto keyOf = [](const Snapshot &s) {
        // Location quantised to a millimetre: the same target recomputed in
        // the next cycle lands on the same value, and two genuinely different
        // targets are not a millimetre apart.
        return QStringLiteral("%1@%2").arg(s.targetType)
                   .arg(qRound64(s.targetLocation * 1000.0));
    };

    for (const Snapshot &s : snaps) {
        const QString k = keyOf(s);
        const bool gap = !cur.frames.isEmpty()
                         && (s.epochMs - cur.frames.last().epochMs) > maxGapMs;
        if (!cur.frames.isEmpty() && (gap || seen.contains(k))) {
            out.push_back(cur);
            cur = Cycle();
            seen.clear();
        }
        if (cur.frames.isEmpty()) { cur.epochMs = s.epochMs; }
        cur.frames.push_back(s);
        seen.insert(k);
    }
    if (!cur.frames.isEmpty()) { out.push_back(cur); }
    return out;
}

// ---- decode -------------------------------------------------------------

QString targetTypeName(int type)
{
    switch (type) {
    case 1: return QStringLiteral("EOA");
    case 2: return QStringLiteral("TURNOUT");
    case 3: return QStringLiteral("TSR");
    case 4: return QStringLiteral("PSR");
    case 5: return QStringLiteral("COLLISION");
    case 6: return QStringLiteral("SOS_TARGET_TYPE");
    case 7: return QStringLiteral("CAUTION_ASPECT");
    case 8: return QStringLiteral("SVL");
    case 0: return QStringLiteral("(empty / no target)");
    default: return QStringLiteral("undefined (%1)").arg(type);
    }
}

bool significantlyDifferent(const Snapshot &a, const Snapshot &b,
                            double locTol, double speedTol)
{
    if (!a.valid || !b.valid)                      { return a.valid != b.valid; }
    if (a.targetType != b.targetType)              { return true; }
    if (qAbs(a.targetLocation - b.targetLocation) > locTol)   { return true; }
    if (qAbs(a.targetSpeed - b.targetSpeed)       > speedTol) { return true; }
    if (a.curves.size() != b.curves.size())        { return true; }

    for (int ci = 0; ci < a.curves.size(); ++ci) {
        const QVector<Segment> x = a.curves.at(ci).activeSegments();
        const QVector<Segment> y = b.curves.at(ci).activeSegments();
        if (x.size() != y.size()) { return true; }   // a step appeared or went
        for (int i = 0; i < x.size(); ++i) {
            if (qAbs(x.at(i).startLoc - y.at(i).startLoc) > locTol)   { return true; }
            if (qAbs(x.at(i).endLoc   - y.at(i).endLoc)   > locTol)   { return true; }
            if (qAbs(x.at(i).higherSpeed - y.at(i).higherSpeed) > speedTol) { return true; }
            if (qAbs(x.at(i).lowerSpeed  - y.at(i).lowerSpeed)  > speedTol) { return true; }
        }
    }
    return false;
}

Snapshot parseFrame(const QByteArray &payload)
{
    Snapshot s;
    s.layout = detectLayout(payload.size());

    // FNV-1a over the raw payload, before any interpretation.
    quint64 fp = 1469598103934665603ULL;
    for (int i = 0; i < payload.size(); ++i) {
        fp ^= quint8(payload[i]);
        fp *= 1099511628211ULL;
    }
    s.fingerprint = fp;

    // A Target plus at least one whole segment is the minimum that can be
    // drawn. Below that the struct has changed shape and guessing is worse
    // than refusing.
    if (payload.size() < s.layout.targetBytes + kSegmentBytes) { return s; }

    s.targetLocation = leDouble(payload, 0);
    s.targetSpeed    = leDouble(payload, 8);
    s.targetType     = quint8(payload[16]);      // ONE byte: -fshort-enums
    s.targetTypeName = targetTypeName(s.targetType);

    int off = s.layout.targetBytes;
    for (int ci = 0; ci < s.layout.curves; ++ci) {
        Curve curve;
        curve.index = ci;
        curve.name  = curveName(ci, s.layout.curveNamesKnown);
        curve.segments.reserve(s.layout.segmentsPerCurve);

        for (int k = 0; k < s.layout.segmentsPerCurve; ++k) {
            if (off + kSegmentBytes > payload.size()) { break; }
            Segment seg;
            seg.a           = leDouble(payload, off + 0);
            seg.c           = leDouble(payload, off + 8);
            seg.startLoc    = leDouble(payload, off + 16);
            seg.endLoc      = leDouble(payload, off + 24);
            seg.higherSpeed = leDouble(payload, off + 32);
            seg.lowerSpeed  = leDouble(payload, off + 40);
            curve.segments.push_back(seg);
            off += kSegmentBytes;
        }
        s.curves.push_back(curve);
    }

    s.valid = true;
    return s;
}

QVector<QVector<QPointF>> polylines(const Curve &curve, int samplesPerSegment)
{
    QVector<QVector<QPointF>> out;
    if (samplesPerSegment < 2) { samplesPerSegment = 2; }

    const QVector<Segment> act = curve.activeSegments();
    for (const Segment &seg : act) {
        const double x0 = qMin(seg.startLoc, seg.endLoc);
        const double x1 = qMax(seg.startLoc, seg.endLoc);

        QVector<QPointF> line;
        line.reserve(samplesPerSegment);

        if (seg.isHorizontal()) {
            // Two points is the whole shape; sampling a flat line 48 times
            // just makes a longer path with identical y.
            if (std::isfinite(seg.c)) {
                line.push_back(QPointF(x0, seg.c));
                line.push_back(QPointF(x1, seg.c));
            }
        } else {
            const double span = x1 - x0;
            for (int i = 0; i < samplesPerSegment; ++i) {
                const double t = double(i) / double(samplesPerSegment - 1);
                const double x = x0 + span * t;
                double v = 0.0;
                if (seg.speedAt(x, &v) && std::isfinite(v)) {
                    line.push_back(QPointF(x, v));
                }
            }
        }

        if (line.size() >= 2) { out.push_back(line); }
    }
    return out;
}

// ---- session walk -------------------------------------------------------

QVector<Snapshot> collect(const LogModel *model, int cap, bool *hitCap)
{
    QVector<Snapshot> out;
    if (hitCap) { *hitCap = false; }
    if (!model) { return out; }

    const int rows = model->rowCount();
    for (int r = 0; r < rows; ++r) {
        if (cap > 0 && out.size() >= cap) {
            if (hitCap) { *hitCap = true; }
            break;
        }
        const LogEntryPtr e = model->entryAt(r);
        if (!e) { continue; }

        // Cheap reject before the full parse: most rows in a session are not
        // @uba, and parseLine() on every one of them is the expensive path.
        if (!e->text.startsWith(QLatin1Char('@'))) { continue; }
        if (!e->text.startsWith(QLatin1String("@uba"))) { continue; }

        const CaptureLine cl = CaptureDecoder::parseLine(e->text);
        if (!cl.valid || cl.type != CapType::UBA) { continue; }

        Snapshot s = parseFrame(cl.bytes);
        if (!s.valid) { continue; }
        s.row     = r;
        s.epochMs = e->epochMs;
        s.rtc     = cl.rtc;
        s.seq     = cl.seq;
        out.push_back(s);
    }

    std::stable_sort(out.begin(), out.end(),
                     [](const Snapshot &x, const Snapshot &y) {
                         return x.epochMs < y.epochMs;
                     });
    return out;
}

int indexAtOrBefore(const QVector<Snapshot> &snaps, qint64 epochMs)
{
    if (snaps.isEmpty()) { return -1; }
    int lo = 0, hi = snaps.size() - 1, best = -1;
    while (lo <= hi) {
        const int mid = lo + (hi - lo) / 2;
        if (snaps.at(mid).epochMs <= epochMs) { best = mid; lo = mid + 1; }
        else                                  { hi = mid - 1; }
    }
    return best;
}

} // namespace Braking
