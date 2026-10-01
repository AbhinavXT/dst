#include "testutil.h"

#include "capturedecoder.h"
#include "incidentreport.h"
#include "incidentreportdialog.h"
#include "incidentreportwindow.h"
#include "logmodel.h"
#include "runreport.h"
#include "speeddistance.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

// =============================================================================
//  Session 97: the incident report pack. One HTML about a chosen moment on
//  one tab -- DMI at key moments, the speed/permitted/target plot, mode
//  changes, EB/FSB applications, reject findings, raw frames -- built on top
//  of RunReport (now windowed) and SpeedDistance (now windowed).
// =============================================================================

namespace {

const QString kDmi = QStringLiteral(
    "@dmi_1_1 2026-06-26T16:24:19 997 AA AA 74 02 01 0A 6F 00 C9 08 00 00 00 00 00 00 00 00 00 00 1A 06 EA 07 10 18 13 "
    "00 00 00 00 00 00 00 00 40 00 00 00 00 00 00 01 00 00 20 00 00 00 00 00 00 00 00 00 00 00 50 00 00 00 00 00 00 00 "
    "00 00 00 00 00 FA 00 00 00 00 00 01 00 00 0D 6A 0D 00 00 00 00 00 00 00 00 00 00 00 00 F0 3C 00 F4 01 00 00 00 00 "
    "08 06 0F 00 00 01 00 D3 89 1D 43 BB BB");
const QString kLsrp = QStringLiteral(
    "@lsrp_1_1 2026-06-27T14:02:27 21441 02 07 0A 00 27 00 00 00 0F 02 A3 AC 57 "
    "40 00 01 40 9F FB 41 E0 F0 7D 00 10 FC 30 15 20 8D 00 F2 F3 26 DD C6 ED 59 9B");

QString withFields(const QString &line, const QHash<QString, qint64> &values, bool msbFirst)
{
    CaptureLine cap = CaptureDecoder::parseLine(line);
    QByteArray bytes = cap.bytes;
    for (const FieldRow &r : CaptureDecoder::describe(cap)) {
        const QString f = r.field.trimmed();
        if (!values.contains(f) || !r.hasSpan()) continue;
        const qint64 v = values.value(f);
        for (int i = 0; i < r.bitLength; ++i) {
            const int pos = r.bitOffset + i;
            const int byte = pos >> 3, off = pos & 7;
            const int bit = msbFirst ? int((v >> (r.bitLength - 1 - i)) & 1) : int((v >> i) & 1);
            const int mask = msbFirst ? (0x80 >> off) : (1 << off);
            bytes[byte] = char(bit ? (uchar(bytes.at(byte)) | mask) : (uchar(bytes.at(byte)) & ~mask));
        }
    }
    const QStringList tok = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return QStringList{ tok.at(0), tok.at(1), tok.at(2) }.join(QLatin1Char(' ')) + QLatin1Char(' ')
           + QString::fromLatin1(bytes.toHex(' ')).toUpper();
}

LogEntryPtr entry(const QString &line, qint64 ms)
{
    auto e = QSharedPointer<LogEntry>::create();
    e->text = line;
    e->epochMs = ms;
    e->cacheDerived();
    return e;
}

}  // namespace

TEST_SUITE(session97)
{
    const qint64 t0 = 1782480000000LL;

    // A 60 s run: DMI + LSRP at 1 s. LOCO_MODE changes at i=5 (2->3, OUTSIDE
    // the incident window below) and i=30 (3->4, INSIDE it). FSB applied
    // i=35..38 (INSIDE). Speed 85 against a permitted 80 at i=42..45 (INSIDE).
    LogModel run(nullptr, 20000);
    {
        QVector<LogEntryPtr> v;
        for (int i = 0; i < 60; ++i) {
            const qint64 ms = t0 + i * 1000;
            const qint64 speed = (i >= 42 && i <= 45) ? 85 : 60;
            const qint64 brake = (i >= 35 && i <= 38) ? 3 : 0;   // FULL_SERVICE_BRAKE
            v << entry(withFields(kDmi, { { "abs_loco_loc", 200000 + i * 50 }, { "train_speed", speed },
                                          { "speed_limit_permissible", 80 }, { "target_distance", 300 },
                                          { "target_speed", 0 }, { "brake_type", brake } }, false), ms);
            const qint64 mode = i < 5 ? 2 : (i < 30 ? 3 : 4);
            v << entry(withFields(kLsrp, { { "LOCO_MODE", mode }, { "EMERGENCY_STATUS", 0 } }, true), ms + 100);
        }
        run.appendEntries(v);
    }

    // =================================================================================
    //  RunReport windowing
    // =================================================================================
    {
        RunReport::Options ro;
        ro.hasWindow = true;
        ro.fromMs = t0 + 10000;
        ro.toMs   = t0 + 50000;
        const RunReport::Summary s = RunReport::summarise(&run, QStringLiteral("1_1"), QString(), ro);
        CHECK(s.rows == 81, "41 @dmi (i=10..50) + 40 @lsrp (i=10..49, the LSRP at i=50 lands 100 ms past toMs)");
        CHECK(s.firstMode == QLatin1String("3 (Limited_Supervision)"), "the mode at window start, not the tab's first row");
        CHECK(s.modeChanges.size() == 1 && s.modeChanges.first().ms == t0 + 30100,
              "only the change at i=30 is inside the window; i=5's is excluded");
        CHECK(s.overspeed.size() == 1 && s.overspeed.first().worst == 5.0, "the windowed overspeed episode");
        CHECK(s.maxSpeedKmh == 85, "max speed is windowed too (SpeedDistance::extract honours the window)");

        const RunReport::Summary whole = RunReport::summarise(&run, QStringLiteral("1_1"), QString());
        CHECK(whole.rows == 120 && whole.modeChanges.size() == 2, "unwindowed: unchanged, both mode changes");
    }

    // =================================================================================
    //  SpeedDistance::extract, windowed
    // =================================================================================
    {
        using namespace SpeedDistance;
        const Trace whole = extract(&run);
        const Trace win = extract(&run, 200000, t0 + 10000, t0 + 50000);
        CHECK(whole.samples.size() == 60 && win.samples.size() == 41, "the window keeps only its own samples");
        CHECK(win.overspeedSamples == 4, "four of the i=42..45 overspeed samples fall in [10,50]");
    }

    // =================================================================================
    //  IncidentReport::build + toHtml
    // =================================================================================
    {
        IncidentReport::Options opt;
        opt.beforeMs = 20000;
        opt.afterMs  = 20000;
        const IncidentReport::Summary s = IncidentReport::build(&run, QStringLiteral("1_1"), QStringLiteral("L1_V1"),
                                                                 t0 + 30000, opt);
        CHECK(s.valid && s.fromMs == t0 + 10000 && s.toMs == t0 + 50000, "the window matches before/after");
        CHECK(s.run.rows == 81, "the run summary underneath is windowed the same way");
        CHECK(s.brakeEpisodes.size() == 1 && s.brakeEpisodes.first().fromMs == t0 + 35000
                  && s.brakeEpisodes.first().toMs == t0 + 38000,
              "one FSB episode, i=35..38");
        CHECK(s.brakeEpisodes.first().what.contains(QLatin1String("FULL_SERVICE_BRAKE")), "named from the schema enum");
        CHECK(!s.speedTrace.isEmpty() && !s.speedPlotPng.isEmpty(), "the speed plot is rendered");
        CHECK(s.rawFrameTotal == 81 && s.rawFrames.size() == 81, "all 81 raw rows kept (under the 500 default cap)");

        // Key moments: window start (i=10), the i=30 mode change, the i=35
        // brake onset, window end (i=50) -- all with a @dmi at or before them.
        CHECK(s.keyMoments.size() == 4 && !s.keyMomentsCapped, "four key moments, nothing dropped");
        int withDmi = 0;
        for (const IncidentReport::KeyMoment &km : s.keyMoments) if (km.hasDmi) ++withDmi;
        CHECK(withDmi == 4, "every moment has a @dmi at or before it (one exists every second here)");
        CHECK(s.keyMoments.first().label == QLatin1String("Window start")
                  && s.keyMoments.last().label == QLatin1String("Window end"),
              "sorted by time: start first, end last");

        const QString html = IncidentReport::toHtml(s, opt);
        CHECK(html.contains(QLatin1String("OBSERVED")) && !html.contains(QLatin1String(">PASS<")),
              "same OBSERVED/no-verdict rule as RunReport");
        for (const char *section : { "DMI at key moments", "Speed / permitted / target", "Loco mode",
                                     "EB/FSB applications", "reject condition", "Raw frames" }) {
            CHECK(html.contains(QLatin1String(section)), QByteArray("has a section on: ") + section);
        }
        CHECK(html.contains(QLatin1String("data:image/png;base64,")), "DMI panels and the plot are embedded, not linked");

        // Capped raw frames: same rows, a tighter cap.
        IncidentReport::Options tight = opt;
        tight.maxRawFrames = 10;
        const IncidentReport::Summary capped = IncidentReport::build(&run, QStringLiteral("1_1"), QString(),
                                                                       t0 + 30000, tight);
        CHECK(capped.rawFrames.size() == 10 && capped.rawFrameTotal == 81, "capped to 10, but still says 81 in all");
        CHECK(IncidentReport::toHtml(capped, tight).contains(QLatin1String("71 more")), "the HTML says how many were dropped");

        // A window with nothing in it.
        const IncidentReport::Summary empty = IncidentReport::build(&run, QStringLiteral("1_1"), QString(), t0 - 1000000, opt);
        CHECK(!empty.valid, "a window entirely before the tab's data has no rows");
        CHECK(IncidentReport::toHtml(empty, opt).contains(QLatin1String("no rows in this window")), "says so");

        const QByteArray dirEnv = qgetenv("DL_SHOTS");
        if (!dirEnv.isEmpty()) {
            IncidentReportWindow w(&run, QStringLiteral("1_1"), QStringLiteral("L1_V1"), t0 + 30000, opt);
            w.resize(900, 1400);
            w.show();
            QTest::qWait(30);
            w.grab().save(QDir(QString::fromLocal8Bit(dirEnv)).filePath(QStringLiteral("incident_report.png")));
        }
    }

    // =================================================================================
    //  No @dmi in the tab at all: no DMI panel to show, though SpeedDistance
    //  still falls back to the LSRP trace (its own documented behaviour).
    // =================================================================================
    {
        LogModel lsrpOnly(nullptr, 1000);
        QVector<LogEntryPtr> v;
        for (int i = 0; i < 10; ++i)
            v << entry(withFields(kLsrp, { { "ABS_LOCO_LOC", 9000 - i * 30 }, { "TRAIN_SPEED", 40 } }, true), t0 + i * 1000);
        lsrpOnly.appendEntries(v);
        IncidentReport::Options opt;
        opt.beforeMs = 5000;
        opt.afterMs = 5000;
        const IncidentReport::Summary s = IncidentReport::build(&lsrpOnly, QStringLiteral("1_1"), QString(), t0 + 5000, opt);
        CHECK(s.valid && s.speedTrace.source == QLatin1String("lsrp") && !s.speedPlotPng.isEmpty(),
              "no @dmi, but the LSRP fallback trace still plots");
        CHECK(s.keyMoments.size() == 2, "window start/end only -- no mode change, no brake episode");
        for (const IncidentReport::KeyMoment &km : s.keyMoments) CHECK(!km.hasDmi, "no @dmi frame exists to show");
        CHECK(IncidentReport::toHtml(s, opt).contains(QLatin1String("No @dmi for this tab at or before this time")),
              "the key-moments section says so for each one");

        // An empty tab: no window has any rows, so no speed source at all.
        LogModel bare(nullptr, 10);
        const IncidentReport::Summary s2 = IncidentReport::build(&bare, QStringLiteral("1_1"), QString(), t0, opt);
        CHECK(!s2.valid && s2.speedTrace.isEmpty() && s2.speedPlotPng.isEmpty(), "an empty tab: no window, no plot");
    }

    // =================================================================================
    //  The dialog: seeds, clamps, and reads back before/after
    // =================================================================================
    {
        IncidentReportDialog dlg(t0 + 500000, t0, t0 + 100000, nullptr);
        CHECK(dlg.atMs() == t0 + 100000, "a seed past the tab's end clamps to it");
        CHECK(dlg.beforeMs() == 60000 && dlg.afterMs() == 60000, "60 s each side by default");
    }

    // =================================================================================
    //  The report window itself opens, builds, and saves
    // =================================================================================
    {
        IncidentReport::Options opt;
        IncidentReportWindow w(&run, QStringLiteral("1_1"), QStringLiteral("L1_V1"), t0 + 30000, opt);
        CHECK(w.summary().valid && !w.html().isEmpty(), "builds on construction");
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("i.html"));
        CHECK(w.saveHtml(path) && QFile(path).size() > 1000, "and saves its HTML, images included");
    }
}
