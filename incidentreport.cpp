#include "incidentreport.h"

#include "capturedecoder.h"
#include "dmipanel.h"
#include "dmitimetravel.h"
#include "fieldplot.h"
#include "logmodel.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QSet>

#include <algorithm>

namespace IncidentReport {
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

QString esc(const QString &t) { return t.toHtmlEscaped(); }

// Binary-search bounds, same convention as fieldplot.cpp's collectRowFields:
// first row with epochMs >= ms.
int firstAtOrAfter(const LogModel *model, qint64 ms)
{
    int a = 0, b = model->count();
    while (a < b) {
        const int mid = (a + b) / 2;
        const LogEntryPtr e = model->entryAt(mid);
        if (e && e->epochMs < ms) a = mid + 1; else b = mid;
    }
    return a;
}

QByteArray toPng(QWidget *w)
{
    const QImage img = w->grab().toImage();
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return bytes;
}

QByteArray renderDmi(const CaptureLine &cap)
{
    DmiView view;
    view.resize(view.sizeHint());
    view.setState(dmiStateFromCapture(cap));
    return toPng(&view);
}

QByteArray renderSpeedPlot(const SpeedDistance::Trace &trace)
{
    SpeedDistanceCanvas canvas;
    canvas.resize(900, 460);   // SpeedDistanceCanvas::sizeHint() (protected)
    canvas.setTrace(trace);
    return toPng(&canvas);
}

// The tab's own latest @dmi at or before `ms`, or an empty KeyMoment if the
// tab has none that early. Single-tab scope: only `model` is searched, so a
// moment names at most one loco.
KeyMoment keyMomentAt(LogModel *model, qint64 ms, const QString &label)
{
    KeyMoment km;
    km.ms = ms;
    km.label = label;
    const int row = firstAtOrAfter(model, ms + 1) - 1;
    if (row < 0) return km;
    const DmiMoment moment = dmiMomentFromModels({}, model, row, label);
    if (!moment.valid || moment.frames.isEmpty()) return km;
    const DmiFrameAt &f = moment.frames.first();
    km.hasDmi = true;
    km.dmiKey = f.key;
    km.dmiPng = renderDmi(f.cap);
    return km;
}

QString durationText(qint64 ms)
{
    const qint64 s = ms / 1000;
    if (s < 60) return QStringLiteral("%1.%2 s").arg(s).arg((ms % 1000) / 100);
    if (s < 3600) return QStringLiteral("%1 min %2 s").arg(s / 60).arg(s % 60);
    return QStringLiteral("%1 h %2 min").arg(s / 3600).arg((s % 3600) / 60);
}

}  // namespace

Summary build(LogModel *tabModel, const QString &tabKey, const QString &tabName,
              qint64 atMs, const Options &options)
{
    Summary s;
    s.tabKey = tabKey;
    s.tabName = tabName;
    s.atMs = atMs;
    s.fromMs = atMs - options.beforeMs;
    s.toMs   = atMs + options.afterMs;
    if (!tabModel) return s;

    const int startRow = firstAtOrAfter(tabModel, s.fromMs);
    const int endRow   = firstAtOrAfter(tabModel, s.toMs + 1) - 1;
    s.valid = endRow >= startRow;

    // ---- the windowed run summary (modes, emergencies, overspeed, tags, skew, faults, reject) ----
    RunReport::Options ro;
    ro.hasWindow = true;
    ro.fromMs = s.fromMs;
    ro.toMs   = s.toMs;
    s.run = RunReport::summarise(tabModel, tabKey, tabName, ro);

    // ---- EB/FSB applications, from @dmi brake_type -----------------------------------
    {
        bool capped = false;
        const QVector<RowFields> rows = collectRowFields(tabModel, QStringLiteral("dmi"),
            { QStringLiteral("brake_type") }, 200000, s.fromMs, s.toMs, &capped);
        RunReport::EpisodeTracker brake{ &s.brakeEpisodes };
        for (const RowFields &r : rows) {
            if (!r.has(QStringLiteral("brake_type"))) continue;
            const qint64 bt = r.raw.value(QStringLiteral("brake_type"));
            const bool active = bt == 3 || bt == 4;   // FULL_SERVICE_BRAKE / EMERGENCY_BRAKE
            brake.observe(active, r.epochMs, r.row, r.display.value(QStringLiteral("brake_type")), 0.0, false);
        }
    }

    // ---- the speed/permitted/target plot, windowed ----------------------------------------
    s.speedTrace = SpeedDistance::extract(tabModel, 200000, s.fromMs, s.toMs);
    s.plotTrace = SpeedDistance::knownOnly(s.speedTrace, &s.plotLeftOut);
    if (!s.plotTrace.isEmpty()) s.speedPlotPng = renderSpeedPlot(s.plotTrace);

    // ---- key moments: window start/end, mode changes, EB/FSB onsets -------------------------
    QVector<KeyMoment> moments;
    moments << keyMomentAt(tabModel, s.fromMs, QStringLiteral("Window start"));
    for (const RunReport::Change &c : s.run.modeChanges) {
        moments << keyMomentAt(tabModel, c.ms, QStringLiteral("Mode: %1 → %2").arg(c.from, c.to));
    }
    for (const RunReport::Episode &e : s.brakeEpisodes) {
        moments << keyMomentAt(tabModel, e.fromMs, QStringLiteral("Brake: %1 applied").arg(e.what));
    }
    for (const RunReport::Episode &m : s.run.missionStarts) {
        moments << keyMomentAt(tabModel, m.fromMs, QStringLiteral("Start of mission (ARP)"));
    }
    moments << keyMomentAt(tabModel, s.toMs, QStringLiteral("Window end"));
    std::sort(moments.begin(), moments.end(), [](const KeyMoment &a, const KeyMoment &b) { return a.ms < b.ms; });
    if (moments.size() > options.maxDmiMoments) {
        moments.resize(options.maxDmiMoments);
        s.keyMomentsCapped = true;
    }
    s.keyMoments = moments;

    // ---- raw frames in the window ------------------------------------------------------------
    if (s.valid) {
        s.rawFrameTotal = endRow - startRow + 1;
        const int lastRow = qMin(endRow, startRow + options.maxRawFrames - 1);
        for (int i = startRow; i <= lastRow; ++i) {
            const LogEntryPtr e = tabModel->entryAt(i);
            if (!e) continue;
            s.rawFrames << RawFrame{ e->epochMs, e->text };
        }
    }
    return s;
}

QString toHtml(const Summary &s, const Options &options)
{
    const int cap = options.maxListRows;
    QString h;
    h += QStringLiteral("<html><head><meta charset=\"utf-8\"><title>Incident report — %1</title>"
                        "<style>body{font-family:sans-serif;font-size:10pt;margin:16px;background:#ffffff;color:#111111}"
                        "h1{font-size:15pt}h2{font-size:12pt;margin-top:18px;border-bottom:1px solid #999}"
                        "h3{font-size:10.5pt;margin-top:12px;margin-bottom:4px}"
                        "table{border-collapse:collapse}td,th{padding:2px 10px 2px 0;text-align:left;vertical-align:top}"
                        "th{border-bottom:1px solid #bbb}.muted{color:#666}.num{text-align:right}"
                        "table.moments td{padding:0 16px 12px 0}"
                        "img.panel{max-width:440px;border:1px solid #ccc}img.plot{max-width:760px;border:1px solid #ccc}"
                        "pre{font-size:8.5pt;white-space:pre-wrap;word-break:break-all;background:#f6f6f6;padding:8px;border:1px solid #ddd}"
                        "</style></head><body>")
             .arg(esc(s.tabName.isEmpty() ? s.tabKey : s.tabName));
    h += QStringLiteral("<h1>Incident report — %1</h1>").arg(esc(s.tabName.isEmpty() ? s.tabKey : s.tabName));
    const QString by = QStringLiteral("%1 %2").arg(QCoreApplication::applicationName(),
                                                   QCoreApplication::applicationVersion()).trimmed();
    h += QStringLiteral("<p class=\"muted\">Source tab %1 · moment %2 · window %3 → %4 (%5 before, %6 after)"
                        "<br>generated %7 by %8.<br>"
                        "This report lists what the capture shows (OBSERVED). It makes no pass/fail judgement.</p>")
             .arg(esc(s.tabKey), timeText(s.atMs), timeText(s.fromMs), timeText(s.toMs),
                  durationText(options.beforeMs), durationText(options.afterMs),
                  QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")), esc(by));
    if (!s.valid) {
        h += QStringLiteral("<p>The tab has no rows in this window.</p></body></html>");
        return h;
    }

    auto more = [cap](int total) {
        return total > cap ? QStringLiteral("<p class=\"muted\">… and %1 more (%2 in all).</p>").arg(total - cap).arg(total)
                           : QString();
    };
    // A width attribute as well as the CSS: the in-app viewer (QTextBrowser)
    // honours the attribute and ignores max-width.
    auto embed = [](const QByteArray &png, const char *cls) {
        const int w = qstrcmp(cls, "panel") == 0 ? 440 : 760;
        return png.isEmpty() ? QString()
             : QStringLiteral("<p><img class=\"%1\" width=\"%2\" src=\"data:image/png;base64,%3\"></p>")
                   .arg(QString::fromLatin1(cls)).arg(w).arg(QString::fromLatin1(png.toBase64()));
    };

    // ---- key moments: the DMI as it stood -----------------------------------------------------
    h += QStringLiteral("<h2>DMI at key moments: %1</h2>").arg(s.keyMoments.size());
    if (s.keyMomentsCapped) h += QStringLiteral("<p class=\"muted\">Capped at %1 moments.</p>").arg(options.maxDmiMoments);
    // Two to a row (session 133): eight moments one under another made a
    // page-long column of panels to scroll past before anything else.
    h += QStringLiteral("<table class=\"moments\">");
    for (int i = 0; i < s.keyMoments.size(); ++i) {
        const KeyMoment &km = s.keyMoments.at(i);
        if (i % 2 == 0) h += QStringLiteral("<tr>");
        h += QStringLiteral("<td valign=\"top\"><h3>%1 — %2</h3>").arg(esc(km.label), timeText(km.ms));
        if (km.hasDmi) h += embed(km.dmiPng, "panel");
        else h += QStringLiteral("<p class=\"muted\">No @dmi for this tab at or before this time.</p>");
        h += QStringLiteral("</td>");
        if (i % 2 == 1 || i == s.keyMoments.size() - 1) h += QStringLiteral("</tr>");
    }
    h += QStringLiteral("</table>");

    // ---- speed / permitted / target -------------------------------------------------------------
    h += QStringLiteral("<h2>Speed / permitted / target</h2>");
    if (s.speedTrace.isEmpty()) {
        h += QStringLiteral("<p class=\"muted\">No speed in this window (no @dmi or @lsrp).</p>");
    } else {
        if (s.plotTrace.isEmpty())
            h += QStringLiteral("<p class=\"muted\">No location to plot against: every frame in the window "
                                "reports 0 m (the loco had not localised on an RFID tag).</p>");
        else
            h += embed(s.speedPlotPng, "plot");
        if (s.plotLeftOut > 0 && !s.plotTrace.isEmpty())
            h += QStringLiteral("<p class=\"muted\">%1 of %2 frames report 0 m (not localised on an RFID tag) "
                                "and are left out of the plot.</p>")
                     .arg(s.plotLeftOut).arg(s.speedTrace.samples.size());
        h += QStringLiteral("<p>Highest: %1 km/h (from @%2). %3 above the permitted speed.</p>")
                 .arg(s.run.maxSpeedKmh, 0, 'f', 0).arg(s.run.speedSource)
                 .arg(countOf(s.run.overspeed.size(), "sample", "samples"));
    }

    // ---- mode changes -----------------------------------------------------------------------------
    h += QStringLiteral("<h2>Loco mode (LSRP): %1</h2>").arg(countOf(s.run.modeChanges.size(), "change", "changes"));
    if (!s.run.firstMode.isEmpty()) h += QStringLiteral("<p>At window start: %1</p>").arg(esc(s.run.firstMode));
    if (!s.run.modeChanges.isEmpty()) {
        h += QStringLiteral("<table><tr><th>Time</th><th>From</th><th>To</th></tr>");
        for (int i = 0; i < s.run.modeChanges.size() && i < cap; ++i) {
            const RunReport::Change &c = s.run.modeChanges.at(i);
            h += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td></tr>").arg(timeText(c.ms), esc(c.from), esc(c.to));
        }
        h += QStringLiteral("</table>") + more(s.run.modeChanges.size());
    }

    // ---- start of mission (session 168) --------------------------------------------------------------
    if (!s.run.missionStarts.isEmpty()) {
        h += QStringLiteral("<h2>Start of mission (ARP): %1</h2>").arg(s.run.missionStarts.size());
        h += QStringLiteral("<p class=\"muted\">ARP in Stand_By with no direction, no RFID tag and no location.</p>"
                            "<table><tr><th>From</th><th>Last such ARP</th><th>Then</th></tr>");
        for (int i = 0; i < s.run.missionStarts.size() && i < cap; ++i) {
            const RunReport::Episode &m = s.run.missionStarts.at(i);
            h += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td></tr>")
                     .arg(timeText(m.fromMs), timeText(m.toMs),
                          m.what.isEmpty() ? QStringLiteral("<span class=\"muted\">still so at window end</span>") : esc(m.what));
        }
        h += QStringLiteral("</table>") + more(s.run.missionStarts.size());
    }

    // ---- EB/FSB applications ------------------------------------------------------------------------
    h += QStringLiteral("<h2>EB/FSB applications (@dmi brake_type): %1</h2>").arg(countOf(s.brakeEpisodes.size(), "episode", "episodes"));
    if (!s.brakeEpisodes.isEmpty()) {
        h += QStringLiteral("<table><tr><th>From</th><th>To</th><th>Type</th></tr>");
        for (int i = 0; i < s.brakeEpisodes.size() && i < cap; ++i) {
            const RunReport::Episode &e = s.brakeEpisodes.at(i);
            h += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td></tr>").arg(timeText(e.fromMs), timeText(e.toMs), esc(e.what));
        }
        h += QStringLiteral("</table>") + more(s.brakeEpisodes.size());
    }

    // ---- reject findings --------------------------------------------------------------------------------
    int rejected = 0;
    for (int v : s.run.rejectClauses) rejected += v;
    h += QStringLiteral("<h2>SLRP frames matching a reject condition: %1 over %2</h2>")
             .arg(countOf(rejected, "match", "matches"), countOf(s.run.slrpFrames, "frame", "frames"));
    if (!s.run.rejectClauses.isEmpty()) {
        h += QStringLiteral("<table><tr><th>Clause · field</th><th class=\"num\">Frames</th></tr>");
        for (auto it = s.run.rejectClauses.constBegin(); it != s.run.rejectClauses.constEnd(); ++it) {
            h += QStringLiteral("<tr><td>%1</td><td class=\"num\">%2</td></tr>").arg(esc(it.key())).arg(it.value());
        }
        h += QStringLiteral("</table>");
    }

    // ---- raw frames ----------------------------------------------------------------------------------------
    h += QStringLiteral("<h2>Raw frames in the window: %1</h2>").arg(s.rawFrameTotal);
    if (!s.rawFrames.isEmpty()) {
        h += QStringLiteral("<pre>");
        for (const RawFrame &f : s.rawFrames) h += esc(f.text) + QLatin1Char('\n');
        h += QStringLiteral("</pre>");
        if (s.rawFrameTotal > s.rawFrames.size()) {
            h += QStringLiteral("<p class=\"muted\">… and %1 more (%2 in all).</p>")
                     .arg(s.rawFrameTotal - s.rawFrames.size()).arg(s.rawFrameTotal);
        }
    }

    h += QStringLiteral("</body></html>");
    return h;
}

}  // namespace IncidentReport
