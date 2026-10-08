#include "runreport.h"

#include "capturedecoder.h"
#include "fieldplot.h"
#include "frameclock.h"
#include "locoidentity.h"
#include "logmodel.h"
#include "rejectrules.h"
#include "speeddistance.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QSet>

namespace RunReport {
namespace {

// "1 episode" / "3 episodes" (session 143; the headings read "episode(s)").
QString countOf(int n, const char *one, const char *many)
{
    return QStringLiteral("%1 %2").arg(n).arg(QLatin1String(n == 1 ? one : many));
}



QString timeText(qint64 ms)
{
    return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss.zzz"));
}

QString durationText(qint64 ms)
{
    const qint64 s = ms / 1000;
    if (s < 60) return QStringLiteral("%1.%2 s").arg(s).arg((ms % 1000) / 100);
    if (s < 3600) return QStringLiteral("%1 min %2 s").arg(s / 60).arg(s % 60);
    return QStringLiteral("%1 h %2 min").arg(s / 3600).arg((s % 3600) / 60);
}

QString esc(const QString &t) { return t.toHtmlEscaped(); }

}  // namespace

Summary summarise(const LogModel *model, const QString &tabKey, const QString &tabName, const Options &options)
{
    Summary s;
    s.tabKey = tabKey;
    s.tabName = tabName;
    if (!model) return s;
    const int n = model->count();
    s.rows = n;
    if (n == 0) return s;

    // ---- one pass over every row ------------------------------------------------
    LocoIdentity identity;
    qint64 prevMs = -1;
    QString prevType;
    qint64 locoFrameNum = -1, locoFrameMs = 0;
    QString mode;
    qint64 lastTag = -1;
    QSet<qint64> tags;
    QSet<QString> activeFaults;
    bool faultsSeen = false;
    EpisodeTracker emergency{ &s.emergencies };
    EpisodeTracker skew{ &s.clockSkew };
    int included = 0;
    bool haveFirst = false;
    bool inMissionStart = false;

    for (int i = 0; i < n; ++i) {
        const LogEntryPtr e = model->entryAt(i);
        if (!e) continue;
        if (options.hasWindow && (e->epochMs < options.fromMs || e->epochMs > options.toMs)) continue;
        ++included;
        if (!haveFirst) { s.firstMs = e->epochMs; haveFirst = true; }
        s.lastMs = qMax(s.lastMs, e->epochMs);
        const QString type = captureTypeOf(e->text);
        s.packetCounts[type.isEmpty() ? QStringLiteral("(text)") : type] += 1;
        if (prevMs >= 0 && e->epochMs - prevMs >= options.gapThresholdMs) {
            s.gaps << Gap{ prevMs, e->epochMs, prevType };
        }
        prevMs = e->epochMs;
        prevType = type.isEmpty() ? QStringLiteral("text") : type;

        // Only the types this report reads are decoded.
        const bool wanted = type == QLatin1String("lsrp") || type == QLatin1String("arp")
                         || type == QLatin1String("slrp") || type == QLatin1String("nmsflt")
                         || type == QLatin1String("arprecv");
        if (!wanted) continue;
        const CaptureLine cap = CaptureDecoder::parseLine(e->text);
        if (!cap.valid || cap.bytes.isEmpty()) continue;
        QHash<QString, qint64> raw;
        const QVector<FieldRow> rows = CaptureDecoder::describe(cap, nullptr, 0, &raw);
        const QString sourceKey = e->tabKey();
        if (type == QLatin1String("lsrp") || type == QLatin1String("arp")) {
            identity.observe(sourceKey, type, raw);
            if (raw.contains(QStringLiteral("FRAME_NUM"))) {
                locoFrameNum = raw.value(QStringLiteral("FRAME_NUM"));
                locoFrameMs = e->epochMs;
            }
        }
        if (type == QLatin1String("arp")) {
            const bool som = CaptureDecoder::isStartOfMission(raw);
            if (som && !inMissionStart) {
                Episode m;
                m.fromMs = m.toMs = e->epochMs;
                m.row = i;
                s.missionStarts << m;
            } else if (som) {
                s.missionStarts.last().toMs = e->epochMs;
            } else if (inMissionStart) {
                for (const FieldRow &r : rows) {
                    if (r.field.trimmed() == QLatin1String("LOCO_MODE")) { s.missionStarts.last().what = r.value.trimmed(); break; }
                }
            }
            inMissionStart = som;
        }
        if (type == QLatin1String("lsrp")) {
            QString modeText;
            QString emergencyText;
            for (const FieldRow &r : rows) {
                const QString f = r.field.trimmed();
                if (f == QLatin1String("LOCO_MODE") && modeText.isEmpty()) modeText = r.value.trimmed();
                if (f == QLatin1String("EMERGENCY_STATUS") && emergencyText.isEmpty()) emergencyText = r.value.trimmed();
            }
            if (!modeText.isEmpty()) {
                if (mode.isEmpty()) s.firstMode = modeText;
                else if (modeText != mode) s.modeChanges << Change{ e->epochMs, i, mode, modeText };
                mode = modeText;
            }
            const qint64 em = raw.value(QStringLiteral("EMERGENCY_STATUS"), 0);
            emergency.observe(em != 0, e->epochMs, i, emergencyText, double(em), false);
            if (raw.contains(QStringLiteral("LAST_RFID_TAG"))) {
                const qint64 tag = raw.value(QStringLiteral("LAST_RFID_TAG"));
                if (tag != lastTag && tag != 0) {
                    s.tagReads << Change{ e->epochMs, i, lastTag > 0 ? QString::number(lastTag) : QString(),
                                          QString::number(tag) };
                    tags.insert(tag);
                }
                lastTag = tag;
            }
        } else if (type == QLatin1String("slrp")) {
            ++s.slrpFrames;
            // Clock skew: this frame's FRAME_NUM against the loco's own,
            // heard within the last 10 s (older is not a comparison).
            if (raw.contains(QStringLiteral("FRAME_NUM")) && locoFrameNum >= 0
                && e->epochMs - locoFrameMs <= 10000) {
                const int ls = FrameClock::secondsSinceMidnight(locoFrameNum);
                const int ss = FrameClock::secondsSinceMidnight(raw.value(QStringLiteral("FRAME_NUM")));
                if (ls >= 0 && ss >= 0) {
                    int gap = ls - ss;
                    if (gap >  FrameClock::kSecondsPerDay / 2) gap -= FrameClock::kSecondsPerDay;
                    if (gap < -FrameClock::kSecondsPerDay / 2) gap += FrameClock::kSecondsPerDay;
                    ++s.skewComparisons;
                    const FrameClock::Accept a = FrameClock::classify(gap);
                    const bool out = a == FrameClock::Accept::Stale || a == FrameClock::Accept::Ahead;
                    skew.observe(out, e->epochMs, i, QString(), double(gap));
                }
            }
            QHash<QString, qint64> judged = raw;
            const QHash<QString, qint64> ctx = identity.contextFor(sourceKey);
            for (auto it = ctx.cbegin(); it != ctx.cend(); ++it) judged.insert(it.key(), it.value());
            for (const RejectRules::Finding &f : kavachRejectRules().evaluate(judged, type)) {
                s.rejectClauses[QStringLiteral("%1  %2").arg(f.rule.clause, f.rule.field)] += 1;
            }
        } else if (type == QLatin1String("arprecv")) {
            // Session 170: another loco's ARP, unless it carries this
            // loco's own ID (rejectrules.xml, clause "DLConsole").
            ++s.arpRecvFrames;
            QHash<QString, qint64> judged = raw;
            const QHash<QString, qint64> ctx = identity.contextFor(sourceKey);
            for (auto it = ctx.cbegin(); it != ctx.cend(); ++it) judged.insert(it.key(), it.value());
            for (const RejectRules::Finding &f : kavachRejectRules().evaluate(judged, type)) {
                s.arpRecvRejects[QStringLiteral("%1  %2").arg(f.rule.clause, f.rule.field)] += 1;
                if (f.rule.field == QLatin1String("SOURCE_LOCO_ID")) {
                    if (!s.arpRecvOwnFirstMs) s.arpRecvOwnFirstMs = e->epochMs;
                    s.arpRecvOwnLastMs = e->epochMs;
                }
            }
        } else if (type == QLatin1String("nmsflt")) {
            // Each frame lists the faults active now: a fault appearing is
            // raised, one disappearing is cleared.
            QSet<QString> now;
            for (const ActiveFaultInfo &fi : CaptureDecoder::faultsOf(cap)) {
                now.insert(QStringLiteral("%1 / %2: %3").arg(fi.subsystemName, fi.moduleName, fi.faultName));
            }
            for (const QString &f : now) if (!activeFaults.contains(f)) s.faults << FaultEvent{ e->epochMs, true, f };
            if (faultsSeen) {
                for (const QString &f : activeFaults) if (!now.contains(f)) s.faults << FaultEvent{ e->epochMs, false, f };
            }
            activeFaults = now;
            faultsSeen = true;
        }
    }
    s.distinctTags = tags.size();
    if (options.hasWindow) s.rows = included;

    // ---- speed, from the same extraction as the speed–distance view ----------------
    const SpeedDistance::Trace trace = options.hasWindow
        ? SpeedDistance::extract(model, 200000, options.fromMs, options.toMs)
        : SpeedDistance::extract(model);
    s.speedSource = trace.source;
    EpisodeTracker over{ &s.overspeed };
    for (const SpeedDistance::Sample &smp : trace.samples) {
        if (smp.speedKmh > s.maxSpeedKmh) { s.maxSpeedKmh = smp.speedKmh; s.maxSpeedMs = smp.epochMs; }
        over.observe(smp.overspeed(), smp.epochMs, smp.row,
                     QStringLiteral("%1 km/h against %2 at %3 km").arg(smp.speedKmh, 0, 'f', 0)
                         .arg(smp.permittedKmh, 0, 'f', 0).arg(smp.locM / 1000.0, 0, 'f', 3),
                     smp.speedKmh - smp.permittedKmh);
    }
    return s;
}

QString toHtml(const Summary &s, const Options &options)
{
    const int cap = options.maxListRows;
    QString h;
    h += QStringLiteral("<html><head><meta charset=\"utf-8\"><title>Run summary — %1</title>"
                        "<style>body{font-family:sans-serif;font-size:10pt;margin:16px;background:#ffffff;color:#111111}"
                        "h1{font-size:15pt}h2{font-size:12pt;margin-top:18px;border-bottom:1px solid #999}"
                        "table{border-collapse:collapse}td,th{padding:2px 10px 2px 0;text-align:left;vertical-align:top}"
                        "th{border-bottom:1px solid #bbb}.muted{color:#666}.num{text-align:right}</style></head><body>")
             .arg(esc(s.tabName.isEmpty() ? s.tabKey : s.tabName));
    h += QStringLiteral("<h1>Run summary — %1</h1>").arg(esc(s.tabName.isEmpty() ? s.tabKey : s.tabName));
    const QString by = QStringLiteral("%1 %2").arg(QCoreApplication::applicationName(),
                                                   QCoreApplication::applicationVersion()).trimmed();
    h += QStringLiteral("<p class=\"muted\">Source tab %1 · generated %2 by %3.<br>"
                        "This report lists what the capture shows (OBSERVED). It makes no pass/fail judgement.</p>")
             .arg(esc(s.tabKey), QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")), esc(by));
    if (s.rows == 0) {
        h += QStringLiteral("<p>The tab has no rows.</p></body></html>");
        return h;
    }

    auto more = [cap](int total) {
        return total > cap ? QStringLiteral("<p class=\"muted\">… and %1 more (%2 in all).</p>").arg(total - cap).arg(total)
                           : QString();
    };

    // ---- span ------------------------------------------------------------------------
    // The span and the per-packet counts side by side (session 143): one
    // under the other, fourteen packet rows ran down a narrow column with
    // the page's right side empty.
    h += QStringLiteral("<h2>Span and traffic</h2><table class=\"side\"><tr><td valign=\"top\"><table>");
    h += QStringLiteral("<tr><td>From</td><td>%1</td></tr><tr><td>To</td><td>%2</td></tr>"
                        "<tr><td>Duration</td><td>%3</td></tr><tr><td>Rows</td><td>%4</td></tr></table></td>"
                        "<td valign=\"top\" style=\"padding-left:48px\">")
             .arg(QDateTime::fromMSecsSinceEpoch(s.firstMs).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                  QDateTime::fromMSecsSinceEpoch(s.lastMs).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                  durationText(s.lastMs - s.firstMs))
             .arg(s.rows);
    h += QStringLiteral("<table><tr><th>Packet</th><th class=\"num\">Rows</th></tr>");
    for (auto it = s.packetCounts.constBegin(); it != s.packetCounts.constEnd(); ++it) {
        h += QStringLiteral("<tr><td>%1</td><td class=\"num\">%2</td></tr>").arg(esc(it.key())).arg(it.value());
    }
    h += QStringLiteral("</table></td></tr></table>");

    // ---- gaps --------------------------------------------------------------------------
    h += QStringLiteral("<h2>Silences over %1 s: %2</h2>").arg(options.gapThresholdMs / 1000).arg(s.gaps.size());
    if (!s.gaps.isEmpty()) {
        h += QStringLiteral("<table><tr><th>From</th><th>To</th><th>Length</th><th>Last packet before</th></tr>");
        for (int i = 0; i < s.gaps.size() && i < cap; ++i) {
            const Gap &g = s.gaps.at(i);
            h += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td></tr>")
                     .arg(timeText(g.fromMs), timeText(g.toMs), durationText(g.toMs - g.fromMs), esc(g.lastBefore));
        }
        h += QStringLiteral("</table>") + more(s.gaps.size());
    }

    // ---- modes ---------------------------------------------------------------------------
    h += QStringLiteral("<h2>Loco mode (LSRP): %1</h2>").arg(countOf(s.modeChanges.size(), "change", "changes"));
    if (s.firstMode.isEmpty()) {
        h += QStringLiteral("<p class=\"muted\">No LSRP frames in this tab.</p>");
    } else {
        h += QStringLiteral("<p>First seen: %1</p>").arg(esc(s.firstMode));
        if (!s.modeChanges.isEmpty()) {
            h += QStringLiteral("<table><tr><th>Time</th><th>From</th><th>To</th></tr>");
            for (int i = 0; i < s.modeChanges.size() && i < cap; ++i) {
                const Change &c = s.modeChanges.at(i);
                h += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td></tr>").arg(timeText(c.ms), esc(c.from), esc(c.to));
            }
            h += QStringLiteral("</table>") + more(s.modeChanges.size());
        }
    }

    // ---- start of mission (session 168) ------------------------------------------------------
    h += QStringLiteral("<h2>Start of mission (ARP): %1</h2>").arg(s.missionStarts.size());
    if (!s.missionStarts.isEmpty()) {
        h += QStringLiteral("<p class=\"muted\">ARP in Stand_By with no direction, no RFID tag and no location.</p>"
                            "<table><tr><th>From</th><th>Last such ARP</th><th>Then</th></tr>");
        for (int i = 0; i < s.missionStarts.size() && i < cap; ++i) {
            const Episode &m = s.missionStarts.at(i);
            h += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td></tr>")
                     .arg(timeText(m.fromMs), timeText(m.toMs),
                          m.what.isEmpty() ? QStringLiteral("<span class=\"muted\">still so at the end</span>") : esc(m.what));
        }
        h += QStringLiteral("</table>") + more(s.missionStarts.size());
    }

    // ---- emergency -------------------------------------------------------------------------
    h += QStringLiteral("<h2>Emergency status other than 0 (LSRP): %1</h2>").arg(countOf(s.emergencies.size(), "episode", "episodes"));
    if (!s.emergencies.isEmpty()) {
        h += QStringLiteral("<table><tr><th>From</th><th>To</th><th>Status</th></tr>");
        for (int i = 0; i < s.emergencies.size() && i < cap; ++i) {
            const Episode &e = s.emergencies.at(i);
            h += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td></tr>").arg(timeText(e.fromMs), timeText(e.toMs), esc(e.what));
        }
        h += QStringLiteral("</table>") + more(s.emergencies.size());
    }

    // ---- speed -------------------------------------------------------------------------------
    h += QStringLiteral("<h2>Speed</h2>");
    if (s.speedSource.isEmpty()) {
        h += QStringLiteral("<p class=\"muted\">No speed in this tab (no @dmi or @lsrp).</p>");
    } else {
        h += QStringLiteral("<p>Highest: %1 km/h at %2 (from @%3).</p>")
                 .arg(s.maxSpeedKmh, 0, 'f', 0).arg(timeText(s.maxSpeedMs), s.speedSource);
        if (s.speedSource == QLatin1String("dmi")) {
            h += QStringLiteral("<p>Above the permitted speed the DMI showed: %1.</p>").arg(countOf(s.overspeed.size(), "episode", "episodes"));
            if (!s.overspeed.isEmpty()) {
                h += QStringLiteral("<table><tr><th>From</th><th>To</th><th>Most above</th><th>At</th></tr>");
                for (int i = 0; i < s.overspeed.size() && i < cap; ++i) {
                    const Episode &e = s.overspeed.at(i);
                    h += QStringLiteral("<tr><td>%1</td><td>%2</td><td>+%3 km/h</td><td>%4</td></tr>")
                             .arg(timeText(e.fromMs), timeText(e.toMs)).arg(e.worst, 0, 'f', 0).arg(esc(e.what));
                }
                h += QStringLiteral("</table>") + more(s.overspeed.size());
            }
        } else {
            h += QStringLiteral("<p class=\"muted\">Permitted speed is only in @dmi, which this tab does not have.</p>");
        }
    }

    // ---- tags ----------------------------------------------------------------------------------
    h += QStringLiteral("<h2>RFID tags (LSRP LAST_RFID_TAG): %1, %2 distinct</h2>")
             .arg(countOf(s.tagReads.size(), "read", "reads")).arg(s.distinctTags);
    if (!s.tagReads.isEmpty()) {
        h += QStringLiteral("<table><tr><th>Time</th><th>Tag</th></tr>");
        for (int i = 0; i < s.tagReads.size() && i < cap; ++i) {
            h += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(timeText(s.tagReads.at(i).ms), esc(s.tagReads.at(i).to));
        }
        h += QStringLiteral("</table>") + more(s.tagReads.size());
    }

    // ---- clock skew --------------------------------------------------------------------------------
    h += QStringLiteral("<h2>Loco and station clocks outside the accept window: %1</h2>").arg(countOf(s.clockSkew.size(), "episode", "episodes"));
    h += QStringLiteral("<p class=\"muted\">Each SLRP FRAME_NUM against the loco's own (LSRP/ARP) heard within 10 s "
                        "before it: %1. Window: more than 4 s old, or 2 s or more ahead, is discarded.</p>")
             .arg(countOf(s.skewComparisons, "comparison", "comparisons"));
    if (!s.clockSkew.isEmpty()) {
        h += QStringLiteral("<table><tr><th>From</th><th>To</th><th>Worst gap (loco − station)</th></tr>");
        for (int i = 0; i < s.clockSkew.size() && i < cap; ++i) {
            const Episode &e = s.clockSkew.at(i);
            h += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3%4 s</td></tr>")
                     .arg(timeText(e.fromMs), timeText(e.toMs), e.worst > 0 ? QStringLiteral("+") : QString())
                     .arg(e.worst, 0, 'f', 0);
        }
        h += QStringLiteral("</table>") + more(s.clockSkew.size());
    }

    // ---- faults ---------------------------------------------------------------------------------------
    int raisedCount = 0;
    for (const FaultEvent &f : s.faults) if (f.raised) ++raisedCount;
    h += QStringLiteral("<h2>Faults (NMS): %1 raised, %2 cleared</h2>").arg(raisedCount).arg(s.faults.size() - raisedCount);
    if (!s.faults.isEmpty()) {
        h += QStringLiteral("<table><tr><th>Time</th><th></th><th>Fault</th></tr>");
        for (int i = 0; i < s.faults.size() && i < cap; ++i) {
            const FaultEvent &f = s.faults.at(i);
            h += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td></tr>")
                     .arg(timeText(f.ms), f.raised ? QStringLiteral("raised") : QStringLiteral("cleared"), esc(f.text));
        }
        h += QStringLiteral("</table>") + more(s.faults.size());
    }

    // ---- reject conditions ---------------------------------------------------------------------------------
    int rejected = 0;
    for (int v : s.rejectClauses) rejected += v;
    h += QStringLiteral("<h2>SLRP frames matching a reject condition: %1 over %2</h2>")
             .arg(countOf(rejected, "match", "matches"), countOf(s.slrpFrames, "frame", "frames"));
    if (!s.rejectClauses.isEmpty()) {
        h += QStringLiteral("<table><tr><th>Clause · field</th><th class=\"num\">Frames</th></tr>");
        for (auto it = s.rejectClauses.constBegin(); it != s.rejectClauses.constEnd(); ++it) {
            h += QStringLiteral("<tr><td>%1</td><td class=\"num\">%2</td></tr>").arg(esc(it.key())).arg(it.value());
        }
        h += QStringLiteral("</table>");
    }
    // Session 170: received ARPs.
    if (s.arpRecvFrames > 0) {
        int recvRejected = 0;
        for (int v : s.arpRecvRejects) recvRejected += v;
        h += QStringLiteral("<h2>Received ARP frames matching a reject condition: %1 over %2</h2>")
                 .arg(countOf(recvRejected, "match", "matches"), countOf(s.arpRecvFrames, "frame", "frames"));
        if (s.arpRecvOwnFirstMs)
            h += QStringLiteral("<p>Received ARP carrying this loco's own ID (its own transmission coming back, or "
                                "another loco configured with the same ID), %1 to %2: the loco would not process "
                                "these as another loco.</p>").arg(timeText(s.arpRecvOwnFirstMs), timeText(s.arpRecvOwnLastMs));
        if (!s.arpRecvRejects.isEmpty()) {
            h += QStringLiteral("<table><tr><th>Clause · field</th><th class=\"num\">Frames</th></tr>");
            for (auto it = s.arpRecvRejects.constBegin(); it != s.arpRecvRejects.constEnd(); ++it) {
                h += QStringLiteral("<tr><td>%1</td><td class=\"num\">%2</td></tr>").arg(esc(it.key())).arg(it.value());
            }
            h += QStringLiteral("</table>");
        }
    }
    h += QStringLiteral("</body></html>");
    return h;
}

}  // namespace RunReport
