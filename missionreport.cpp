#include "missionreport.h"

#include <algorithm>

#include "capturedecoder.h"
#include "fieldplot.h"
#include "logmodel.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QSet>

namespace Missions {
namespace {

QString timeText(qint64 ms)
{
    return ms ? QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss")) : QStringLiteral("—");
}

QString dateTimeText(qint64 ms)
{
    return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString durationText(qint64 ms)
{
    if (ms <= 0) return QStringLiteral("—");
    const qint64 s = ms / 1000;
    if (s < 60) return QStringLiteral("%1 s").arg(s);
    if (s < 3600) return QStringLiteral("%1 min %2 s").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1 h %2 min").arg(s / 3600).arg((s % 3600) / 60, 2, 10, QLatin1Char('0'));
}

QString esc(const QString &t) { return t.toHtmlEscaped(); }

// Supervision ranking for "the highest reached".
int rankOf(qint64 mode)
{
    switch (mode) {
    case 4: return 5;   // Full_Supervision
    case 3: return 4;   // Limited_Supervision
    case 6: return 3;   // On_Sight
    case 2: return 2;   // Staff_Responsible
    case 1: return 1;   // Stand_By
    default: return 0;
    }
}

QString rowText(const QVector<FieldRow> &rows, const char *name)
{
    for (const FieldRow &r : rows)
        if (r.field.trimmed() == QLatin1String(name)) return r.value.trimmed();
    return QString();
}

// Modes from one source (the DMI's loco_mode, or the ARP's LOCO_MODE).
struct ModeTrack {
    QVector<RunReport::Change> modes;
    QMap<QString, qint64> timeIn;
    QString first, last, highest;
    int     highestRank = -1;
    qint64  since = 0, raw = -1;
    qint64  sr = 0, os = 0, ls = 0, fs = 0, sf = 0;
    int     trips = 0;
    bool    seen = false;

    void observe(qint64 mode, const QString &text, qint64 ms, int row)
    {
        seen = true;
        if (text != last) {
            if (last.isEmpty()) first = text;
            else {
                modes << RunReport::Change{ ms, row, last, text };
                if (since) timeIn[last] += ms - since;
            }
            last = text;
            since = ms;
            if (mode == 7 && raw != 7) ++trips;
            raw = mode;
        }
        if (mode == 2 && !sr) sr = ms;
        if (mode == 6 && !os) os = ms;
        if (mode == 3 && !ls) ls = ms;
        if (mode == 4 && !fs) fs = ms;
        if (mode == 12 && !sf) sf = ms;
        if (rankOf(mode) > highestRank) { highestRank = rankOf(mode); highest = text; }
    }
    void closeAt(qint64 toMs)
    {
        if (!last.isEmpty() && since) timeIn[last] += qMax<qint64>(0, toMs - since);
    }
};

// The building state of the mission being read.
struct Builder {
    Mission m;
    ModeTrack modes;
    qint64  lastDmiMs = 0;             // the DMI's mode wins while it talks
    qint64  prevDmiMs = 0;
    bool    prevNoRadio = false;
    bool    any = false;               // read anything of interest
    RunReport::EpisodeTracker eb{ nullptr }, fsb{ nullptr };

    void close(qint64 toMs)
    {
        m.toMs = qMax(m.fromMs, toMs);
        ModeTrack &t = modes;
        if (!t.seen) return;
        t.closeAt(m.toMs);
        m.modeSource = m.dmiFrames > 0 && m.arpModeFrames > 0 ? QStringLiteral("DMI + ARP")
                     : m.dmiFrames > 0                        ? QStringLiteral("DMI")
                                                              : QStringLiteral("ARP");
        m.modes = t.modes;
        m.timeInMode = t.timeIn;
        m.firstMode = t.first;
        m.lastMode = t.last;
        m.highestMode = t.highest;
        m.firstSrMs = t.sr;
        m.firstOsMs = t.os;
        m.firstLsMs = t.ls;
        m.firstFsMs = t.fs;
        m.systemFailure = t.sf != 0;
        m.systemFailureMs = t.sf;
        m.trips = t.trips;
    }
};

}  // namespace

QString modeName(const QString &mode)
{
    const int open = mode.indexOf(QLatin1Char('(')), close = mode.lastIndexOf(QLatin1Char(')'));
    return (open > 0 && close > open) ? mode.mid(open + 1, close - open - 1).trimmed() : mode;
}

QVector<Mission> split(const LogModel *model, const Options &options)
{
    QVector<Mission> out;
    if (!model || model->count() == 0) return out;

    Builder b;
    auto begin = [&](int index, qint64 ms, int row) {
        b = Builder();
        b.m.index = index;
        b.m.fromMs = ms;
        b.m.fromRow = row;
        b.eb.out = &b.m.eb;
        b.fsb.out = &b.m.fsb;
    };
    auto finish = [&](qint64 toMs) {
        b.close(toMs);
        // Mission 0 (before the first start) only when it holds anything.
        if (b.m.index > 0 || b.any) out << b.m;
    };

    bool inSom = false;
    QSet<QString> activeFaults;
    bool faultsSeen = false;
    QString lastTag;
    qint64 lastMs = 0;
    int next = 1;
    begin(0, model->entryAt(0) ? model->entryAt(0)->epochMs : 0, 0);

    for (int i = 0; i < model->count(); ++i) {
        const LogEntryPtr e = model->entryAt(i);
        if (!e) continue;
        lastMs = qMax(lastMs, e->epochMs);
        const QString type = captureTypeOf(e->text);
        const bool wanted = type == QLatin1String("arp") || type == QLatin1String("dmi")
                         || type == QLatin1String("rfid") || type == QLatin1String("nmsflt");
        if (!wanted) continue;
        const CaptureLine cap = CaptureDecoder::parseLine(e->text);
        if (!cap.valid || cap.bytes.isEmpty()) continue;
        QHash<QString, qint64> raw;
        const QVector<FieldRow> rows = CaptureDecoder::describe(cap, nullptr, 0, &raw);
        const qint64 ms = e->epochMs;

        if (type == QLatin1String("arp")) {
            const bool som = CaptureDecoder::isStartOfMission(raw);
            if (som && !inSom) {                       // a new mission
                finish(ms);
                begin(next++, ms, i);
                lastTag.clear();
                b.any = true;
            }
            // The ARP's mode counts while the DMI is silent (> 3 s since
            // its last frame): an ARP lags the DMI by up to its period, and
            // taking both would flicker at every change.
            if (raw.contains(QStringLiteral("LOCO_MODE")) && (!b.lastDmiMs || ms - b.lastDmiMs > options.dmiSilentMs)) {
                b.modes.observe(raw.value(QStringLiteral("LOCO_MODE")), rowText(rows, "LOCO_MODE"), ms, i);
                ++b.m.arpModeFrames;
            }
            if (som) b.m.standbyToMs = ms;
            else if (inSom) b.m.afterStandby = rowText(rows, "LOCO_MODE");
            inSom = som;
            b.any = true;
            continue;
        }
        if (type == QLatin1String("rfid")) {
            if (!raw.contains(QStringLiteral("unique"))) continue;
            const QString tag = QString::number(raw.value(QStringLiteral("unique")));
            if (tag != lastTag && tag != QLatin1String("0")) {
                b.m.tags << tag;
                lastTag = tag;
            }
            b.any = true;
            continue;
        }
        if (type == QLatin1String("nmsflt")) {
            QSet<QString> now;
            for (const ActiveFaultInfo &fi : CaptureDecoder::faultsOf(cap))
                now.insert(QStringLiteral("%1 / %2: %3").arg(fi.subsystemName, fi.moduleName, fi.faultName));
            for (const QString &f : now) {
                if ((!faultsSeen || !activeFaults.contains(f)) && !b.m.faultsRaised.contains(f)) b.m.faultsRaised << f;
            }
            activeFaults = now;
            faultsSeen = true;
            b.any = true;
            continue;
        }

        // ---- @dmi --------------------------------------------------------------------------
        b.any = true;
        ++b.m.dmiFrames;
        if (raw.contains(QStringLiteral("loco_mode"))) {
            b.modes.observe(raw.value(QStringLiteral("loco_mode")), rowText(rows, "loco_mode"), ms, i);
            b.lastDmiMs = ms;
        }
        const QString alarm = rowText(rows, "alarm_code");
        if (alarm.contains(QLatin1String("System Self Test In Progress"))) {
            if (!b.m.selfTestFromMs) b.m.selfTestFromMs = ms;
            b.m.selfTestToMs = ms;
        }
        if (alarm.contains(QLatin1String("Select Train Config"))) {
            if (!b.m.trainConfigFromMs) b.m.trainConfigFromMs = ms;
            b.m.trainConfigToMs = ms;
        }
        const qint64 bt = raw.value(QStringLiteral("brake_type"), 0);
        const QString btText = rowText(rows, "brake_type");
        b.eb.observe(bt == 4, ms, i, btText, 0.0, false);
        b.fsb.observe(bt == 3, ms, i, btText, 0.0, false);
        const double speed = double(raw.value(QStringLiteral("train_speed"), 0));
        if (speed > b.m.maxSpeedKmh) { b.m.maxSpeedKmh = speed; b.m.maxSpeedMs = ms; }
        const qint64 loc = raw.value(QStringLiteral("abs_loco_loc"), 0);
        if (loc > 0) {
            b.m.minLocM = b.m.minLocM ? qMin(b.m.minLocM, loc) : loc;
            b.m.maxLocM = qMax(b.m.maxLocM, loc);
        }
        const bool noRadio = raw.contains(QStringLiteral("signal_strength")) && raw.value(QStringLiteral("signal_strength")) == 0;
        if (noRadio && b.prevNoRadio && b.prevDmiMs && ms - b.prevDmiMs <= options.noRadioGapCapMs)
            b.m.noRadioMs += ms - b.prevDmiMs;
        b.prevNoRadio = noRadio;
        b.prevDmiMs = ms;
    }
    finish(lastMs);
    // Session 175: each mission's EB/FSB, with the reasons the capture gives.
    const QVector<RunReport::BrakeEvent> brakes = RunReport::brakeEvents(model);
    for (Mission &m : out)
        for (const RunReport::BrakeEvent &b : brakes)
            if (b.fromMs >= m.fromMs && (b.fromMs < m.toMs || (b.fromMs == m.toMs && &m == &out.last()))) m.brakes << b;
    // Session 181: each mission's level-crossing approaches.
    const QVector<LcCheck::Approach> lcs = LcCheck::build(model);
    for (Mission &m : out)
        for (const LcCheck::Approach &a : lcs)
            if (a.fromMs >= m.fromMs && (a.fromMs < m.toMs || &m == &out.last())) m.lcApproaches << a;
    if (!out.isEmpty() && out.last().index > 0) out.last().endsWithLog = true;
    if (!out.isEmpty() && out.last().index == 0) out.last().endsWithLog = true;
    return out;
}

void addDmiMoments(const LogModel *model, QVector<Mission> *missions, int maxPerMission)
{
    if (!model || !missions) return;
    struct Pick { qint64 ms; int priority; QString label; };
    for (Mission &m : *missions) {
        QVector<Pick> picks;
        picks << Pick{ m.fromMs, 0, m.index ? QStringLiteral("Start of mission") : QStringLiteral("Log start") };
        const struct { qint64 ms; const char *label; } firsts[] = {
            { m.firstSrMs, "First Staff_Responsible" }, { m.firstOsMs, "First On_Sight" },
            { m.firstLsMs, "First Limited_Supervision" }, { m.firstFsMs, "First Full_Supervision" },
            { m.systemFailureMs, "System_Failure" } };
        for (const auto &f : firsts)
            if (f.ms) picks << Pick{ f.ms, 1, QLatin1String(f.label) };
        for (const RunReport::BrakeEvent &b : m.brakes)
            picks << Pick{ b.fromMs, 1, QStringLiteral("Brake: %1 applied").arg(b.type) };
        picks << Pick{ m.toMs, 2, m.endsWithLog ? QStringLiteral("Log end") : QStringLiteral("Mission end") };
        for (const RunReport::Change &c : m.modes)
            picks << Pick{ c.ms, 3, QStringLiteral("Mode: %1 \u2192 %2").arg(modeName(c.from), modeName(c.to)) };

        // One panel per instant: the higher-priority label wins.
        std::stable_sort(picks.begin(), picks.end(), [](const Pick &a, const Pick &b) { return a.priority < b.priority; });
        QVector<Pick> kept;
        for (const Pick &p : picks) {
            bool dup = false;
            for (const Pick &k : kept) dup = dup || k.ms == p.ms;
            if (dup) continue;
            if (kept.size() >= maxPerMission) { m.dmiMomentsCapped = true; continue; }
            kept << p;
        }
        std::sort(kept.begin(), kept.end(), [](const Pick &a, const Pick &b) { return a.ms < b.ms; });
        m.dmiMoments.clear();
        for (const Pick &p : kept) {
            IncidentReport::KeyMoment km = IncidentReport::dmiMomentAt(model, p.ms, p.label);
            // A frame from before this mission is another mission's screen: not shown.
            if (km.hasDmi && km.dmiFrameMs < m.fromMs) { km.hasDmi = false; km.dmiPng.clear(); }
            m.dmiMoments << km;
        }
    }
}

QString toHtml(const QVector<Mission> &missions, const QString &tabKey, const QString &tabName, const Options &options)
{
    const QString title = tabName.isEmpty() ? tabKey : tabName;
    QString h;
    h += QStringLiteral("<html><head><meta charset=\"utf-8\"><title>Mission report — %1</title>"
                        "<style>body{font-family:sans-serif;font-size:10pt;margin:16px;background:#ffffff;color:#111111}"
                        "h1{font-size:15pt}h2{font-size:12pt;margin-top:22px;border-bottom:1px solid #999}"
                        "h3{font-size:10.5pt;margin:12px 0 4px 0}"
                        "table{border-collapse:collapse}td,th{padding:2px 10px 2px 0;text-align:left;vertical-align:top}"
                        "th{border-bottom:1px solid #bbb}.muted{color:#666}.num{text-align:right}"
                        ".flag{color:#a40000;font-weight:bold}table.moments td{padding:0 16px 12px 0}"
                        "img.panel{max-width:420px;border:1px solid #ccc}</style></head><body>")
             .arg(esc(title));
    h += QStringLiteral("<h1>Mission report — %1</h1>").arg(esc(title));
    const QString by = QStringLiteral("%1 %2").arg(QCoreApplication::applicationName(),
                                                   QCoreApplication::applicationVersion()).trimmed();
    h += QStringLiteral("<p class=\"muted\">Source tab %1 · generated %2 by %3.<br>"
                        "A mission starts at a start of mission (ARP in Stand_By with no direction, no RFID tag "
                        "and no location) and ends at the next one. Modes, brakes, speed and radio are the DMI's "
                        "(@dmi); while the DMI is silent (over 3 s) the modes are the loco's own ARP LOCO_MODE. This report lists what the capture shows (OBSERVED). It makes no pass/fail judgement.</p>")
             .arg(esc(tabKey), QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")), esc(by));
    if (options.tabFull && !missions.isEmpty())
        h += QStringLiteral("<p class=\"flag\">The tab is full (%1 rows): it holds the traffic from %2 on. "
                            "Missions before that are not in it.</p>").arg(options.tabRows).arg(dateTimeText(missions.first().fromMs));
    int started = 0;
    for (const Mission &m : missions) started += m.index > 0 ? 1 : 0;
    if (missions.isEmpty()) {
        h += QStringLiteral("<p>The tab has no ARP, DMI, RFID or NMS fault frames.</p></body></html>");
        return h;
    }
    if (started == 0)
        h += QStringLiteral("<p>No start of mission in this tab: everything below is one stretch begun before the log.</p>");

    // ---- summary -------------------------------------------------------------------------------
    h += QStringLiteral("<h2>%1 mission(s)</h2>").arg(started);
    h += QStringLiteral("<table><tr><th>#</th><th>Start</th><th>End</th><th>Length</th><th>To Staff_Resp.</th>"
                        "<th>Highest mode</th><th>Ended in</th><th>Seen</th><th class=\"num\">Max km/h</th>"
                        "<th class=\"num\">Tags</th><th>No radio</th><th class=\"num\">Faults</th></tr>");
    for (const Mission &m : missions) {
        QStringList seen;
        if (m.systemFailure) seen << QStringLiteral("<span class=\"flag\">System_Failure</span>");
        if (m.trips) seen << QStringLiteral("<span class=\"flag\">Trip ×%1</span>").arg(m.trips);
        if (!m.eb.isEmpty()) seen << QStringLiteral("<span class=\"flag\">EB ×%1</span>").arg(m.eb.size());
        if (!m.fsb.isEmpty()) seen << QStringLiteral("FSB ×%1").arg(m.fsb.size());
        h += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td><td>%6</td><td>%7</td>"
                            "<td>%8</td><td class=\"num\">%9</td><td class=\"num\">%10</td><td>%11</td><td class=\"num\">%12</td></tr>")
                 .arg(m.index ? QString::number(m.index) : QStringLiteral("0 <span class=\"muted\">(before)</span>"),
                      timeText(m.fromMs), m.endsWithLog ? timeText(m.toMs) + QStringLiteral(" <span class=\"muted\">(log ends)</span>") : timeText(m.toMs),
                      durationText(m.durationMs()), m.index ? durationText(m.startUpMs()) : QStringLiteral("—"),
                      esc(modeName(m.highestMode)), esc(modeName(m.lastMode)), seen.join(QStringLiteral(", ")))
                 .arg(m.maxSpeedKmh, 0, 'f', 0).arg(m.tags.size()).arg(durationText(m.noRadioMs)).arg(m.faultsRaised.size());
    }
    h += QStringLiteral("</table>");

    // ---- each mission --------------------------------------------------------------------------
    const int cap = options.maxListRows;
    for (const Mission &m : missions) {
        h += m.index ? QStringLiteral("<h2>Mission %1 — %2 to %3</h2>").arg(m.index).arg(dateTimeText(m.fromMs), timeText(m.toMs))
                     : QStringLiteral("<h2>Before the first start of mission — %1 to %2</h2>").arg(dateTimeText(m.fromMs), timeText(m.toMs));
        h += QStringLiteral("<p>Length %1%2. %3 @dmi frames.</p>")
                 .arg(durationText(m.durationMs()), m.endsWithLog ? QStringLiteral(", to the end of the log") : QString())
                 .arg(m.dmiFrames);
        h += QStringLiteral("<h3>Start-up</h3><table><tr><th>Phase</th><th>From</th><th>To</th></tr>");
        if (m.index)
            h += QStringLiteral("<tr><td>Stand_By, no direction / tag / location (ARP)</td><td>%1</td><td>%2</td></tr>")
                     .arg(timeText(m.fromMs), timeText(m.standbyToMs));
        if (m.selfTestFromMs)
            h += QStringLiteral("<tr><td>System self test in progress (DMI)</td><td>%1</td><td>%2</td></tr>")
                     .arg(timeText(m.selfTestFromMs), timeText(m.selfTestToMs));
        if (m.trainConfigFromMs)
            h += QStringLiteral("<tr><td>Select train config (DMI)</td><td>%1</td><td>%2</td></tr>")
                     .arg(timeText(m.trainConfigFromMs), timeText(m.trainConfigToMs));
        const struct { const char *name; qint64 ms; } firsts[] = {
            { "First Staff_Responsible", m.firstSrMs }, { "First On_Sight", m.firstOsMs },
            { "First Limited_Supervision", m.firstLsMs }, { "First Full_Supervision", m.firstFsMs },
            { "System_Failure", m.systemFailureMs } };
        for (const auto &f : firsts)
            if (f.ms) h += QStringLiteral("<tr><td>%1</td><td>%2</td><td></td></tr>").arg(QLatin1String(f.name), timeText(f.ms));
        h += QStringLiteral("</table>");
        if (m.index && !m.afterStandby.isEmpty())
            h += QStringLiteral("<p>After Stand_By the ARP reported %1.</p>").arg(esc(m.afterStandby));

        h += QStringLiteral("<h3>Modes (%1)</h3>").arg(m.modeSource.isEmpty() ? QStringLiteral("none") : m.modeSource);
        if (m.modeSource == QLatin1String("ARP"))
            h += QStringLiteral("<p class=\"muted\">No @dmi in this mission: the modes are the loco's own ARP LOCO_MODE.</p>");
        else if (m.modeSource == QLatin1String("DMI + ARP"))
            h += QStringLiteral("<p class=\"muted\">The DMI's loco_mode while it was sending; the ARP's LOCO_MODE "
                                "(%1 frames) while the DMI was silent.</p>").arg(m.arpModeFrames);
        if (m.lastMode.isEmpty()) {
            h += QStringLiteral("<p class=\"muted\">No @dmi or ARP mode in this mission.</p>");
        } else {
            h += QStringLiteral("<p>First %1; highest %2; last %3.</p>").arg(esc(m.firstMode), esc(m.highestMode), esc(m.lastMode));
            h += QStringLiteral("<table><tr><th>Mode</th><th>Time in it</th></tr>");
            for (auto it = m.timeInMode.constBegin(); it != m.timeInMode.constEnd(); ++it)
                h += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(esc(it.key()), durationText(it.value()));
            h += QStringLiteral("</table>");
            if (!m.modes.isEmpty()) {
                h += QStringLiteral("<table><tr><th>Time</th><th>From</th><th>To</th></tr>");
                for (int i = 0; i < m.modes.size() && i < cap; ++i) {
                    const RunReport::Change &c = m.modes.at(i);
                    h += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td></tr>").arg(timeText(c.ms), esc(c.from), esc(c.to));
                }
                h += QStringLiteral("</table>");
            }
        }
        // Session 191: the DMI at the key moments, two to a row, as in the incident pack.
        if (!m.dmiMoments.isEmpty()) {
            int shown = 0;
            for (const IncidentReport::KeyMoment &km : m.dmiMoments) shown += km.hasDmi ? 1 : 0;
            h += QStringLiteral("<h3>DMI at key moments (%1)</h3>").arg(m.dmiMoments.size());
            if (m.dmiMomentsCapped)
                h += QStringLiteral("<p class=\"muted\">Up to %1 per mission: the start, the first of each mode, "
                                    "brakes and the end first; some mode changes are left out.</p>").arg(m.dmiMoments.size());
            if (shown == 0) {
                h += QStringLiteral("<p class=\"muted\">No @dmi in this tab at or before these moments.</p>");
            } else {
                h += QStringLiteral("<table class=\"moments\">");
                for (int i = 0; i < m.dmiMoments.size(); ++i) {
                    const IncidentReport::KeyMoment &km = m.dmiMoments.at(i);
                    if (i % 2 == 0) h += QStringLiteral("<tr>");
                    h += QStringLiteral("<td valign=\"top\"><b>%1</b> \u2014 %2").arg(esc(km.label), timeText(km.ms));
                    if (km.hasDmi) {
                        h += QStringLiteral("<p><img class=\"panel\" width=\"420\" src=\"data:image/png;base64,%1\"></p>")
                                 .arg(QString::fromLatin1(km.dmiPng.toBase64()));
                        if (km.ms - km.dmiFrameMs > 3000)
                            h += QStringLiteral("<p class=\"muted\">The DMI's last frame before this, %1 earlier (%2).</p>")
                                     .arg(durationText(km.ms - km.dmiFrameMs), timeText(km.dmiFrameMs));
                    } else {
                        h += QStringLiteral("<p class=\"muted\">No @dmi in this mission at or before this time.</p>");
                    }
                    h += QStringLiteral("</td>");
                    if (i % 2 == 1 || i == m.dmiMoments.size() - 1) h += QStringLiteral("</tr>");
                }
                h += QStringLiteral("</table>");
            }
        }
        if (!m.brakes.isEmpty()) {
            h += QStringLiteral("<h3>Brakes (DMI brake_type)</h3><table><tr><th>From</th><th>To</th><th>Type</th>"
                                "<th>Reasons in the capture</th></tr>");
            for (int i = 0; i < m.brakes.size() && i < cap; ++i) {
                const RunReport::BrakeEvent &b = m.brakes.at(i);
                h += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td></tr>")
                         .arg(timeText(b.fromMs), timeText(b.toMs), esc(b.type), esc(b.reasonsText()));
            }
            h += QStringLiteral("</table>");
        }
        if (!m.lcApproaches.isEmpty()) {
            h += QStringLiteral("<h3>Level crossings approached</h3><ul>");
            for (const LcCheck::Approach &a : m.lcApproaches) {
                QStringList horn;
                for (const auto &sp : a.horn) horn << timeText(sp.first);
                h += QStringLiteral("<li>LC %1 (%2) %3–%4: horn %5</li>")
                         .arg(esc(a.lc), esc(a.manning), timeText(a.fromMs), timeText(a.toMs),
                              !a.dioSeen ? QStringLiteral("not known (no @dip1)")
                                         : horn.isEmpty() ? QStringLiteral("<span class=\"flag\">none</span>")
                                                          : QStringLiteral("from ") + horn.join(QStringLiteral(", ")));
            }
            h += QStringLiteral("</ul>");
        }
        h += QStringLiteral("<h3>Run</h3><p>Highest speed %1 km/h%2. %3. No radio on the DMI for %4.</p>")
                 .arg(m.maxSpeedKmh, 0, 'f', 0)
                 .arg(m.maxSpeedMs ? QStringLiteral(" at %1").arg(timeText(m.maxSpeedMs)) : QString())
                 .arg(m.maxLocM ? QStringLiteral("Located from %1 to %2 km").arg(m.minLocM / 1000.0, 0, 'f', 3).arg(m.maxLocM / 1000.0, 0, 'f', 3)
                                : QStringLiteral("Never located on a tag"))
                 .arg(durationText(m.noRadioMs));
        h += QStringLiteral("<p>RFID tags read (%1): %2</p>").arg(m.tags.size())
                 .arg(m.tags.isEmpty() ? QStringLiteral("none") : esc(m.tags.mid(0, cap).join(QStringLiteral(", "))));
        if (!m.faultsRaised.isEmpty()) {
            h += QStringLiteral("<h3>NMS faults raised (%1)</h3><ul>").arg(m.faultsRaised.size());
            for (int i = 0; i < m.faultsRaised.size() && i < cap; ++i) h += QStringLiteral("<li>%1</li>").arg(esc(m.faultsRaised.at(i)));
            h += QStringLiteral("</ul>");
        }
    }
    h += QStringLiteral("</body></html>");
    return h;
}

}  // namespace Missions
