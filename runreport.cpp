#include "runreport.h"

#include "soslog.h"

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

// First row with epochMs >= ms (rows are in arrival order).
static int logModelRowAfterMs(const LogModel *model, qint64 ms)
{
    int lo = 0, hi = model->count();
    while (lo < hi) {
        const int mid = lo + (hi - lo) / 2;
        const LogEntryPtr e = model->entryAt(mid);
        if (e && e->epochMs < ms) lo = mid + 1; else hi = mid;
    }
    return lo;
}

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
    qint64 lastLsrpMs = 0;                          // session 190
    constexpr qint64 kArpModeAfterMs = 3000;
    bool selfSosOpen = false;
    bool noKeysOpen = false;
    QString keySet;

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

        // Session 179: key loads, counted without decoding.
        if (type == QLatin1String("auth_keys1") && (s.keyLoads.isEmpty() || e->epochMs - s.keyLoads.last() > 5000))
            s.keyLoads << e->epochMs;

        // Only the types this report reads are decoded.
        const bool wanted = type == QLatin1String("lsrp") || type == QLatin1String("arp")
                         || type == QLatin1String("slrp") || type == QLatin1String("nmsflt")
                         || type == QLatin1String("arprecv") || type == QLatin1String("nmshlth");
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
        // Session 190: the mode from the ARP when no LSRP has been heard for 3 s.
        // A loco that is not localised (no direction, no location) sends no
        // LSRP at all, so SR, Stand_By and System_Failure there were only in
        // its ARPs, and the mode lane never showed them. LSRP stays the
        // source whenever it is there, so the two cannot flap at a change.
        if (type == QLatin1String("arp") && (lastLsrpMs == 0 || e->epochMs - lastLsrpMs > kArpModeAfterMs)) {
            for (const FieldRow &r : rows) {
                if (r.field.trimmed() != QLatin1String("LOCO_MODE")) continue;
                const QString modeText = r.value.trimmed();
                if (mode.isEmpty()) s.firstMode = modeText;
                else if (modeText != mode) s.modeChanges << Change{ e->epochMs, i, mode, modeText };
                mode = modeText;
                break;
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
            lastLsrpMs = e->epochMs;
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
        } else if (type == QLatin1String("nmshlth")) {
            // Session 178: event fields come as display rows only.
            for (const FieldRow &r : rows) {
                const QString f = r.field.trimmed();
                const QString v = r.value.trimmed();
                if (f == QLatin1String("LOCO_SELF_SOS")) {
                    const int code = v.section(QLatin1Char(' '), 0, 0).toInt();
                    const bool start = code == 1 || code == 3, end = code == 2 || code == 4;
                    const QString what = v.section(QLatin1Char('('), 1).section(QLatin1Char(')'), 0, 0);
                    if (start && !selfSosOpen) {
                        Episode ep;
                        ep.fromMs = ep.toMs = e->epochMs;
                        ep.row = i;
                        ep.what = what.endsWith(QLatin1String(" start")) ? what.left(what.size() - 6) : what;
                        s.selfSos << ep;
                        selfSosOpen = true;
                    } else if (end && selfSosOpen) {
                        s.selfSos.last().toMs = e->epochMs;
                        selfSosOpen = false;
                    }
                } else if (f == QLatin1String("CURRENT_RUNNING_KEY")) {
                    if (keySet.isEmpty()) keySet = v;
                    else if (v != keySet) { s.keySets << Change{ e->epochMs, i, keySet, v }; keySet = v; }
                } else if (f == QLatin1String("REMAINING_KEY_NUMBERS")) {
                    const int left = v.section(QLatin1Char(' '), 0, 0).toInt();
                    s.minRemainingKeys = s.minRemainingKeys < 0 ? left : qMin(s.minRemainingKeys, left);
                    if (left == 0) {
                        if (noKeysOpen) s.noKeys.last().toMs = e->epochMs;
                        else { s.noKeys << Episode{ e->epochMs, e->epochMs, QStringLiteral("no keys"), 0.0, i }; noKeysOpen = true; }
                    } else {
                        noKeysOpen = false;
                    }
                } else if (f == QLatin1String("COLLISION_DETECTION")) {
                    if (s.collisionDetections.isEmpty() || e->epochMs - s.collisionDetections.last().ms > 5000
                        || s.collisionDetections.last().to != v)
                        s.collisionDetections << Change{ e->epochMs, i, QString(), v };
                }
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
    if (selfSosOpen && !s.selfSos.isEmpty()) {
        s.selfSos.last().toMs = s.lastMs;
        s.selfSos.last().what += QStringLiteral(" (no end)");
    }
    for (Episode &k : s.noKeys) {
        for (const Episode &m : s.missionStarts)
            if (qAbs(k.fromMs - m.fromMs) <= 60000) { k.what += QStringLiteral(" (at a start of mission)"); break; }
    }
    s.distinctTags = tags.size();
    if (options.hasWindow) s.rows = included;
    s.brakes = options.hasWindow ? brakeEvents(model, options.fromMs, options.toMs) : brakeEvents(model);
    {
        // Windowed: read 10 minutes back too, so a threat that started before
        // the window and is still on in it is listed (its start, as logged).
        constexpr qint64 kSosLookbackMs = 10 * 60 * 1000;
        const SosLog::Timeline sos = options.hasWindow
            ? SosLog::extract(model, options.fromMs - kSosLookbackMs, options.toMs)
            : SosLog::extract(model);
        for (const Episode &e : SosLog::spellEpisodes(sos))
            if (!options.hasWindow || (e.toMs >= options.fromMs && e.fromMs <= options.toMs)) s.sosThreats << e;
        for (const Change &c : SosLog::brakeChanges(sos))
            if (!options.hasWindow || (c.ms >= options.fromMs && c.ms <= options.toMs)) s.sosDecisions << c;
    }
    s.tagCheck = options.hasWindow ? TagCheck::build(model, options.fromMs, options.toMs) : TagCheck::build(model);
    s.lcApproaches = options.hasWindow ? LcCheck::build(model, options.fromMs, options.toMs) : LcCheck::build(model);

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

QString BrakeEvent::reasonsText() const
{
    return reasons.isEmpty() ? QStringLiteral("no reason in the capture") : reasons.join(QStringLiteral("; "));
}

QVector<BrakeEvent> brakeEvents(const LogModel *model, qint64 fromMs, qint64 toMs)
{
    QVector<BrakeEvent> out;
    if (!model) return out;
    const QVector<RowFields> rows = collectRowFields(model, QStringLiteral("dmi"),
        { QStringLiteral("brake_type"), QStringLiteral("context_values"), QStringLiteral("alarm_code"),
          QStringLiteral("collision_loco_id") }, 200000, fromMs, toMs);
    bool open = false;
    auto addReason = [](BrakeEvent &e, const QString &r) { if (!r.isEmpty() && !e.reasons.contains(r)) e.reasons << r; };
    for (const RowFields &f : rows) {
        if (!f.has(QStringLiteral("brake_type"))) continue;
        const qint64 bt = f.raw.value(QStringLiteral("brake_type"));
        if (bt != 3 && bt != 4) { open = false; continue; }
        if (!open) {
            BrakeEvent e;
            e.fromMs = e.toMs = f.epochMs;
            e.row = f.row;
            e.type = f.display.value(QStringLiteral("brake_type"));
            out << e;
            open = true;
        }
        BrakeEvent &e = out.last();
        e.toMs = f.epochMs;
        const QString ctx = f.display.value(QStringLiteral("context_values"));
        if (!ctx.isEmpty() && ctx != QLatin1String("(none)"))
            for (const QString &c : ctx.split(QStringLiteral("; "), Qt::SkipEmptyParts)) addReason(e, QStringLiteral("DMI: ") + c);
        const QString alarm = f.display.value(QStringLiteral("alarm_code"));
        if (!alarm.isEmpty() && alarm != QLatin1String("no_alarm") && alarm != QLatin1String("(none)"))
            for (const QString &a : alarm.split(QStringLiteral("; "), Qt::SkipEmptyParts)) addReason(e, QStringLiteral("DMI alarm: ") + a);
        const qint64 other = f.raw.value(QStringLiteral("collision_loco_id"), 0);
        if (other > 0) addReason(e, QStringLiteral("DMI: collision target loco %1").arg(other));
    }
    if (out.isEmpty()) return out;

    // NMS reasons near each onset. Its fields are events: display rows only.
    const qint64 lo = out.first().fromMs - kBrakeReasonWindowMs, hi = out.last().fromMs + kBrakeReasonWindowMs;
    QVector<QPair<qint64, QString>> nms;
    for (int i = logModelRowAfterMs(model, lo); i < model->count(); ++i) {
        const LogEntryPtr e = model->entryAt(i);
        if (!e) continue;
        if (e->epochMs > hi) break;
        if (!e->text.startsWith(QLatin1String("@nmshlth_"))) continue;
        for (const FieldRow &r : CaptureDecoder::describe(CaptureDecoder::parseLine(e->text)))
            if (r.field.trimmed() == QLatin1String("BRAKE_APPLICATION_REASON")) nms << qMakePair(e->epochMs, r.value.trimmed());
    }
    for (BrakeEvent &e : out)
        for (const auto &n : nms)
            if (qAbs(n.first - e.fromMs) <= kBrakeReasonWindowMs) addReason(e, QStringLiteral("NMS: ") + n.second);
    return out;
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
    h += QStringLiteral("<h2>Loco mode (LSRP, else ARP): %1</h2>").arg(countOf(s.modeChanges.size(), "change", "changes"));
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

    // ---- brakes (session 175) -------------------------------------------------------------------
    h += QStringLiteral("<h2>EB / FSB applications (DMI brake_type): %1</h2>").arg(s.brakes.size());
    if (!s.brakes.isEmpty()) {
        h += QStringLiteral("<table><tr><th>From</th><th>To</th><th>Type</th><th>Reasons in the capture</th></tr>");
        for (int i = 0; i < s.brakes.size() && i < cap; ++i) {
            const BrakeEvent &b = s.brakes.at(i);
            h += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td></tr>")
                     .arg(timeText(b.fromMs), timeText(b.toMs), esc(b.type), esc(b.reasonsText()));
        }
        h += QStringLiteral("</table>") + more(s.brakes.size());
    }

    // ---- self SoS and collision detection (session 178) -----------------------------------------
    h += QStringLiteral("<h2>Loco's own SoS (NMS LOCO_SELF_SOS): %1</h2>").arg(countOf(s.selfSos.size(), "episode", "episodes"));
    if (!s.selfSos.isEmpty()) {
        h += QStringLiteral("<table><tr><th>From</th><th>To</th><th>What</th></tr>");
        for (int i = 0; i < s.selfSos.size() && i < cap; ++i) {
            const Episode &e = s.selfSos.at(i);
            h += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td></tr>").arg(timeText(e.fromMs), timeText(e.toMs), esc(e.what));
        }
        h += QStringLiteral("</table>") + more(s.selfSos.size());
    }
    // Session 184: only when the log has @sos, so other reports read as before.
    if (!s.sosThreats.isEmpty() || !s.sosDecisions.isEmpty()) {
        h += QStringLiteral("<h2>SoS threats (firmware, @sos): %1</h2>").arg(countOf(s.sosThreats.size(), "threat", "threats"));
        if (!s.sosThreats.isEmpty()) {
            h += QStringLiteral("<table><tr><th>From</th><th>To</th><th>Threat, and how it ended</th></tr>");
            for (int i = 0; i < s.sosThreats.size() && i < cap; ++i) {
                const Episode &e = s.sosThreats.at(i);
                h += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td></tr>").arg(timeText(e.fromMs), timeText(e.toMs), esc(e.what));
            }
            h += QStringLiteral("</table>") + more(s.sosThreats.size());
        }
        h += QStringLiteral("<h2>SoS brake decisions (firmware, @sosev): %1</h2>").arg(countOf(s.sosDecisions.size(), "decision", "decisions"));
        if (!s.sosDecisions.isEmpty()) {
            h += QStringLiteral("<table><tr><th>Time</th><th>Decision</th></tr>");
            for (int i = 0; i < s.sosDecisions.size() && i < cap; ++i)
                h += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(timeText(s.sosDecisions.at(i).ms), esc(s.sosDecisions.at(i).to));
            h += QStringLiteral("</table>") + more(s.sosDecisions.size());
        }
    }
    if (!s.collisionDetections.isEmpty()) {
        h += QStringLiteral("<h2>Collision detection (NMS): %1</h2><table><tr><th>Time</th><th>Value</th></tr>").arg(s.collisionDetections.size());
        for (int i = 0; i < s.collisionDetections.size() && i < cap; ++i)
            h += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(timeText(s.collisionDetections.at(i).ms), esc(s.collisionDetections.at(i).to));
        h += QStringLiteral("</table>") + more(s.collisionDetections.size());
    }

    // ---- session keys (session 179) -----------------------------------------------------------
    if (!s.keySets.isEmpty() || !s.noKeys.isEmpty() || s.minRemainingKeys >= 0 || !s.keyLoads.isEmpty()) {
        int atStart = 0;
        for (const Episode &k : s.noKeys) atStart += k.what.endsWith(QLatin1String("(at a start of mission)")) ? 1 : 0;
        h += QStringLiteral("<h2>Session keys (NMS)</h2><p>Fewest key sets left: %1. No keys in %2, %3 of them "
                            "within a minute of a start of mission. %4 (@auth_keys). %5 of running key set.</p>")
                 .arg(s.minRemainingKeys < 0 ? QStringLiteral("not reported") : QString::number(s.minRemainingKeys))
                 .arg(countOf(s.noKeys.size(), "spell", "spells")).arg(atStart)
                 .arg(countOf(s.keyLoads.size(), "key load", "key loads"))
                 .arg(countOf(s.keySets.size(), "change", "changes"));
        if (!s.noKeys.isEmpty()) {
            h += QStringLiteral("<table><tr><th>From</th><th>To</th><th></th></tr>");
            for (int i = 0; i < s.noKeys.size() && i < cap; ++i)
                h += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td></tr>")
                         .arg(timeText(s.noKeys.at(i).fromMs), timeText(s.noKeys.at(i).toMs), esc(s.noKeys.at(i).what));
            h += QStringLiteral("</table>") + more(s.noKeys.size());
        }
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

    // ---- RFID tag check (session 180) -------------------------------------------------------------------
    h += TagCheck::toHtml(s.tagCheck, cap);

    // ---- level crossings (session 181) ------------------------------------------------------------------
    if (!s.lcApproaches.isEmpty()) h += LcCheck::toHtml(s.lcApproaches, cap);

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
