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
             const QString &kind, const QString &label, QVector<EventMark> *out)
{
    const int idx = sampleAtOrBeforeRow(trace.samples, row);
    if (idx < 0) return;
    out->append(EventMark{ ms, trace.samples.at(idx).locM, kind, label, row });
}

void pinByMs(const SpeedDistance::Trace &trace, qint64 ms,
            const QString &kind, const QString &label, QVector<EventMark> *out)
{
    const int idx = sampleAtOrBeforeMs(trace.samples, ms);
    if (idx < 0) return;
    out->append(EventMark{ ms, trace.samples.at(idx).locM, kind, label, trace.samples.at(idx).row });
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
    d.minLocM = d.trace.minLocM;
    d.maxLocM = d.trace.maxLocM;
    auto widen = [&d](double v) { d.minLocM = qMin(d.minLocM, v); d.maxLocM = qMax(d.maxLocM, v); };

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
            if (idx < 0) continue;
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
                    QStringLiteral("Mode: %1 → %2").arg(c.from, c.to), &d.events);
        }
        for (const RunReport::Episode &ep : run.overspeed) {
            pinByRow(d.trace, ep.fromMs, ep.row, QStringLiteral("overspeed"), ep.what, &d.events);
        }
        for (const RunReport::Episode &ep : run.emergencies) {
            pinByRow(d.trace, ep.fromMs, ep.row, QStringLiteral("emergency"), ep.what, &d.events);
        }
        for (const RunReport::FaultEvent &f : run.faults) {
            if (!f.raised) continue;   // the pin is where it was RAISED
            pinByMs(d.trace, f.ms, QStringLiteral("fault"), f.text, &d.events);
        }

        const TwoLocoView::Events ev = TwoLocoView::extractEvents(model);
        for (const RunReport::Episode &ep : ev.sos) {
            pinByRow(d.trace, ep.fromMs, ep.row, QStringLiteral("sos"), ep.what, &d.events);
        }
        for (const RunReport::Episode &ep : ev.headOn) {
            pinByRow(d.trace, ep.fromMs, ep.row, QStringLiteral("head-on"), ep.what, &d.events);
        }
        for (const RunReport::Episode &ep : ev.rearEnd) {
            pinByRow(d.trace, ep.fromMs, ep.row, QStringLiteral("rear-end"), ep.what, &d.events);
        }
        std::sort(d.events.begin(), d.events.end(),
                 [](const EventMark &a, const EventMark &b) { return a.epochMs < b.epochMs; });
    }

    return d;
}

}  // namespace TrackDiagram
