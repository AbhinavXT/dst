#include "trackdiagram.h"

#include "capturedecoder.h"
#include "dmipanel.h"
#include "logmodel.h"
#include "runreport.h"
#include "twolocoview.h"

#include <QSet>

#include <algorithm>

namespace TrackDiagram {
namespace {

// Same "0 and 2^23-1 both mean no fix" rule as ReplayWindow's track axis
// (replaywindow.cpp) — a null-balise / keep-alive @rfid read decodes fine
// but must not place a tag at location 0.
constexpr qint64 kRfidLocUnknown = (1LL << 23) - 1;
bool rfidHasFix(const RfidInfo &t) { return t.valid && t.absLoc > 0 && t.absLoc != kRfidLocUnknown; }

// Last trace sample with sample.row <= row, or -1. Samples are in row order
// (SpeedDistance::extract walks the model forward), so a binary search holds.
int sampleAtOrBeforeRow(const QVector<SpeedDistance::Sample> &v, int row)
{
    int lo = 0, hi = v.size();
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (v.at(mid).row <= row) lo = mid + 1; else hi = mid;
    }
    return lo - 1;
}

// Same, by time — for events (NMS faults) that carry no row.
int sampleAtOrBeforeMs(const QVector<SpeedDistance::Sample> &v, qint64 ms)
{
    int lo = 0, hi = v.size();
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (v.at(mid).epochMs <= ms) lo = mid + 1; else hi = mid;
    }
    return lo - 1;
}

void pinByRow(const SpeedDistance::Trace &trace, qint64 ms, int row,
             const QString &kind, const QString &label, Diagram *d)
{
    const int idx = sampleAtOrBeforeRow(trace.samples, row);
    if (idx < 0) return;
    if (!locationKnown(trace.samples.at(idx))) { ++d->unpinnedEvents; return; }
    d->events.append(EventMark{ ms, trace.samples.at(idx).locM, kind, label, row });
}

void pinByMs(const SpeedDistance::Trace &trace, qint64 ms,
            const QString &kind, const QString &label, Diagram *d)
{
    const int idx = sampleAtOrBeforeMs(trace.samples, ms);
    if (idx < 0) return;
    if (!locationKnown(trace.samples.at(idx))) { ++d->unpinnedEvents; return; }
    d->events.append(EventMark{ ms, trace.samples.at(idx).locM, kind, label, trace.samples.at(idx).row });
}

}  // namespace

Diagram build(const LogModel *model, const QString &tabKey, const QString &tabName, int maxRows)
{
    Diagram d;
    d.tabKey = tabKey;
    d.tabName = tabName;
    if (!model) return d;

    d.trace = SpeedDistance::extract(model, maxRows);
    if (d.trace.isEmpty()) return d;
    // The span from the samples whose location is known; the trace's own
    // min/max would count the 0 m ones.
    bool haveSpan = false;
    auto widen = [&d, &haveSpan](double v) {
        if (!haveSpan) { d.minLocM = d.maxLocM = v; haveSpan = true; return; }
        d.minLocM = qMin(d.minLocM, v);
        d.maxLocM = qMax(d.maxLocM, v);
    };
    for (const SpeedDistance::Sample &s : d.trace.samples) {
        if (locationKnown(s)) widen(s.locM);
        else ++d.unknownSamples;
    }

    const int n = model->count();

    // ---- RFID tags: first real fix per tag id -----------------------------------------
    {
        QSet<qint64> seen;
        for (int i = 0; i < n; ++i) {
            const LogEntryPtr e = model->entryAt(i);
            if (!e) continue;
            const CaptureLine c = CaptureDecoder::parseLine(e->text);
            if (!c.valid || c.type != CapType::Rfid) continue;
            const RfidInfo t = CaptureDecoder::decodeRfid(c.bytes);
            if (!rfidHasFix(t)) continue;
            if (seen.contains(t.unique)) continue;
            seen.insert(t.unique);
            d.tags << RfidMark{ e->epochMs, double(t.absLoc), t.unique, t.type, i };
            widen(double(t.absLoc));
        }
    }

    // ---- Signals and the MA end, from @dmi, deduped by name --------------------------
    {
        QHash<QString, int> bestDistance;   // signal name -> closest appr_sig_dist seen
        QHash<QString, SignalMark> marks;
        for (int i = 0; i < n; ++i) {
            const LogEntryPtr e = model->entryAt(i);
            if (!e) continue;
            const CaptureLine c = CaptureDecoder::parseLine(e->text);
            if (!c.valid || c.type != CapType::Dmi) continue;
            const DmiState s = dmiStateFromCapture(c);
            if (!s.valid || s.signalName.isEmpty() || s.signalDistance <= 0) continue;

            const int idx = sampleAtOrBeforeRow(d.trace.samples, i);
            if (idx < 0 || !locationKnown(d.trace.samples.at(idx))) continue;
            const double locoLoc = d.trace.samples.at(idx).locM;
            const double signalLoc = SpeedDistance::targetLocation(locoLoc, s.signalDistance, d.trace.direction);

            auto it = bestDistance.find(s.signalName);
            if (it != bestDistance.end() && *it <= s.signalDistance) continue;   // a closer fix already kept
            bestDistance.insert(s.signalName, s.signalDistance);

            SignalMark mk;
            mk.name = s.signalName;
            mk.locM = signalLoc;
            mk.aspect = s.aspect;
            mk.epochMs = e->epochMs;
            mk.row = i;
            if (s.movementAuthority > 0) {
                mk.hasMa = true;
                mk.maEndLocM = SpeedDistance::targetLocation(signalLoc, s.movementAuthority, d.trace.direction);
            }
            marks.insert(s.signalName, mk);
        }
        for (auto it = marks.constBegin(); it != marks.constEnd(); ++it) {
            d.signalMarks << it.value();
            widen(it.value().locM);
            if (it.value().hasMa) widen(it.value().maEndLocM);
        }
        std::sort(d.signalMarks.begin(), d.signalMarks.end(),
                 [](const SignalMark &a, const SignalMark &b) { return a.locM < b.locM; });
    }

    // ---- Events, pinned by the loco's last known location at or before them -----------
    {
        const RunReport::Summary run = RunReport::summarise(model, tabKey, tabName);
        for (const RunReport::Change &c : run.modeChanges) {
            pinByRow(d.trace, c.ms, c.row, QStringLiteral("mode"),
                    QStringLiteral("Mode: %1 → %2").arg(c.from, c.to), &d);
        }
        for (const RunReport::Episode &ep : run.overspeed) {
            pinByRow(d.trace, ep.fromMs, ep.row, QStringLiteral("overspeed"), ep.what, &d);
        }
        for (const RunReport::Episode &ep : run.emergencies) {
            pinByRow(d.trace, ep.fromMs, ep.row, QStringLiteral("emergency"), ep.what, &d);
        }
        for (const RunReport::FaultEvent &f : run.faults) {
            if (!f.raised) continue;   // the pin is where it was RAISED
            pinByMs(d.trace, f.ms, QStringLiteral("fault"), f.text, &d);
        }

        const TwoLocoView::Events ev = TwoLocoView::extractEvents(model);
        for (const RunReport::Episode &ep : ev.sos) {
            pinByRow(d.trace, ep.fromMs, ep.row, QStringLiteral("sos"), ep.what, &d);
        }
        for (const RunReport::Episode &ep : ev.headOn) {
            pinByRow(d.trace, ep.fromMs, ep.row, QStringLiteral("head-on"), ep.what, &d);
        }
        for (const RunReport::Episode &ep : ev.rearEnd) {
            pinByRow(d.trace, ep.fromMs, ep.row, QStringLiteral("rear-end"), ep.what, &d);
        }
        std::sort(d.events.begin(), d.events.end(),
                 [](const EventMark &a, const EventMark &b) { return a.epochMs < b.epochMs; });
    }

    // ---- SLRP profile in force, placed from its reference tag (session 207) ----------
    //  Not widening the span: a profile runs kilometres ahead of the loco and
    //  would squeeze the run into a corner. Lanes are clipped to the rail.
    {
        QHash<int, double> tagLoc;
        for (const RfidMark &t : d.tags) tagLoc.insert(int(t.uniqueId), t.locM);
        auto speed = [](int a, int b, int c, bool classified) {
            auto one = [](int v) { return v < 0 ? QStringLiteral("?") : QString::number(v); };
            return classified ? QStringLiteral("%1/%2/%3").arg(one(a), one(b), one(c)) : one(a);
        };
        SlrpProfile held;
        for (int i = 0; i < n; ++i) {
            const LogEntryPtr e = model->entryAt(i);
            if (!e) continue;
            const CaptureLine c = CaptureDecoder::parseLine(e->text);
            if (!c.valid || c.type != CapType::SLRP) continue;
            const SlrpProfile latest = CaptureDecoder::profileOf(c);
            if (!latest.valid) continue;
            held = CaptureDecoder::carryProfile(held, latest);

            Profile pr;
            pr.epochMs = e->epochMs;
            pr.row = i;
            pr.refRfid = held.refRfid;
            pr.refProfId = held.refProfId;
            pr.pktDir = held.pktDir;
            pr.tsrStatus = held.tsrStatus;
            pr.placed = tagLoc.contains(held.refRfid);
            if (pr.placed) {
                const double travel = held.pktDir == 2 ? -1.0 : 1.0;
                const double o = tagLoc.value(held.refRfid) + travel * held.distPktStart;
                auto at = [o, travel](double dist) { return o + travel * dist; };
                pr.startSignalM = o;
                if (held.haveMA) { pr.haveMa = true; pr.maEndM = at(held.maWrtSig); }
                double acc = 0.0;
                for (const SlrpProfile::SpeedStep &s : held.ssp) {
                    const QString v = speed(s.a, s.b, s.c, s.classified);
                    pr.ssp << ProfileSpan{ at(acc), at(acc + s.d), v, QObject::tr("Static speed %1 km/h").arg(v) };
                    acc += s.d;
                }
                acc = 0.0;
                for (const SlrpProfile::GradPt &g : held.grad) {
                    const QString v = QStringLiteral("%1%2").arg(g.value).arg(g.uphill ? QChar(0x2191) : QChar(0x2193));
                    pr.grad << ProfileSpan{ at(acc), at(acc + g.d), v,
                                            QObject::tr("Gradient %1 %2").arg(g.value).arg(g.uphill ? QObject::tr("rising") : QObject::tr("falling")) };
                    acc += g.d;
                }
                for (const SlrpProfile::TsrZone &z : held.tsr) {
                    const QString v = speed(z.a, z.b, z.c, z.classified);
                    pr.tsr << ProfileSpan{ at(z.d), at(z.d + z.len), QStringLiteral("TSR %1: %2").arg(z.id).arg(v),
                                           QObject::tr("TSR %1, %2 km/h, TSR_STATUS %3").arg(z.id).arg(v).arg(held.tsrStatus) };
                }
                for (const SlrpProfile::TrackCond &t : held.cond) {
                    const QString v = CaptureDecoder::tcTypeName(t.type);
                    pr.cond << ProfileSpan{ at(t.sd), at(t.sd + t.len), v, QObject::tr("Track condition: %1").arg(v) };
                }
                acc = 0.0;
                for (const SlrpProfile::TagLink &t : held.tags) {
                    acc += t.d;
                    pr.tagLinks << ProfileMark{ at(acc), QString::number(t.tag) };
                }
            }
            d.profiles << pr;
        }
    }

    if (!haveSpan) { d.minLocM = 0.0; d.maxLocM = 1.0; }
    else if (d.maxLocM - d.minLocM < 1.0) { d.minLocM -= 50.0; d.maxLocM += 50.0; }   // one point: give it room
    return d;
}

const Profile *Diagram::profileAt(qint64 ms) const
{
    const Profile *found = nullptr;
    for (const Profile &p : profiles)
        if (p.epochMs <= ms) found = &p;   // rows can step back in time; the last one at or before wins
    return found;
}

}  // namespace TrackDiagram
