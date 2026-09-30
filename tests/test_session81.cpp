#include "testutil.h"

#include "capturedecoder.h"
#include "clockskewalarm.h"
#include "fieldplot.h"
#include "logmodel.h"
#include "runreport.h"
#include "runreportwindow.h"
#include "speeddistance.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

// =============================================================================
//  Session 81: several fields on one time axis, speed vs distance, export,
//  the run summary report and the clock-skew alarm.
//
//  Frames with chosen values are made from REAL captures by rewriting fields
//  at the bit span the decoder itself reports (FieldRow bitOffset/bitLength),
//  then decoding again to prove the value landed. Nothing hand-computes an
//  offset.
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
const QString kArp = QStringLiteral(
    "@arp_1_1 2026-06-27T14:02:26 21436 02 07 0D 00 27 00 00 00 0F 02 D3 AC 57 30 "
    "00 01 40 9F FB 47 D0 01 0E 04 1F C3 15 00 00 00 00 00 08 32 00 C9 5E DE 2F");

// Rewrite fields of a capture line, where the decoder says they are.
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
            // lsb-first: value bit i at stream bit (offset + i), LSB of each byte first;
            // msb-first: the field's MSB first, MSB of each byte first.
            const int bit = msbFirst ? int((v >> (r.bitLength - 1 - i)) & 1) : int((v >> i) & 1);
            const int mask = msbFirst ? (0x80 >> off) : (1 << off);
            bytes[byte] = char(bit ? (uchar(bytes.at(byte)) | mask) : (uchar(bytes.at(byte)) & ~mask));
        }
    }
    const QStringList tok = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return QStringList{ tok.at(0), tok.at(1), tok.at(2) }.join(QLatin1Char(' ')) + QLatin1Char(' ')
           + QString::fromLatin1(bytes.toHex(' ')).toUpper();
}

qint64 rawOf(const QString &line, const QString &field)
{
    QHash<QString, qint64> raw;
    CaptureDecoder::describe(CaptureDecoder::parseLine(line), nullptr, 0, &raw);
    return raw.value(field, -1);
}

LogEntryPtr entry(const QString &line, qint64 ms)
{
    auto e = QSharedPointer<LogEntry>::create();
    e->text = line;
    e->epochMs = ms;
    e->cacheDerived();
    return e;
}

FieldSeries series(const QString &field, const QVector<QPair<qint64, double>> &pts)
{
    FieldSeries s;
    s.fieldName = field;
    s.typeToken = QStringLiteral("x");
    for (const auto &p : pts) {
        FieldSeries::Point pt;
        pt.epochMs = p.first;
        pt.value = p.second;
        s.points << pt;
    }
    if (!pts.isEmpty()) {
        s.minMs = pts.first().first; s.maxMs = pts.last().first;
        s.minValue = s.maxValue = pts.first().second;
        for (const auto &p : pts) { s.minValue = qMin(s.minValue, p.second); s.maxValue = qMax(s.maxValue, p.second); }
    }
    return s;
}

}  // namespace

TEST_SUITE(session81)
{
    const qint64 t0 = 1782480000000LL;

    // ---- the frame builder does what it says ----------------------------------------
    const QString dmiA = withFields(kDmi, { { "abs_loco_loc", 123456 }, { "train_speed", 77 },
                                            { "speed_limit_permissible", 80 } }, false);
    CHECK(rawOf(dmiA, "abs_loco_loc") == 123456 && rawOf(dmiA, "train_speed") == 77
              && rawOf(dmiA, "speed_limit_permissible") == 80,
          "a DMI frame rewritten at the decoder's own bit spans decodes to the new values");
    const QString lsrpMode = withFields(kLsrp, { { "LOCO_MODE", 3 } }, true);
    CHECK(rawOf(lsrpMode, "LOCO_MODE") == 3 && rawOf(kLsrp, "LOCO_MODE") != 3, "and an LSRP one (msb-first)");

    // =================================================================================
    //  Several fields on one time axis
    // =================================================================================
    {
        const FieldSeries a = series("A", { { 1000, 1 }, { 2000, 2 }, { 4000, 3 } });
        const FieldSeries b = series("B", { { 2000, 10 }, { 3000, 20 } });
        CursorValue cv = valueAtOrBefore(a, 3500, 10000);
        CHECK(cv.has && cv.value == 2 && cv.atMs == 2000, "the cursor reads the sample at or before it (a state holds)");
        CHECK(!valueAtOrBefore(a, 500, 10000).has, "nothing before the first sample");
        CHECK(!valueAtOrBefore(a, 60000, 5000).has, "nor long after the last one");

        const QString csv = seriesToCsv({ a, b }, 0, 3500);
        const QStringList lines = csv.trimmed().split(QLatin1Char('\n'));
        CHECK(lines.first() == QLatin1String("time_local,epoch_ms,x.A,x.B"), "CSV header: time, then one column per field");
        CHECK(lines.size() == 4, "one row per distinct sample time in the view (1000, 2000, 3000)");
        CHECK(lines.at(1).endsWith(QLatin1String(",1000,1,")), "a field with no sample at that time is left empty");
        CHECK(lines.at(2).endsWith(QLatin1String(",2000,2,10")), "both fields where both sampled");
        CHECK(lines.at(3).endsWith(QLatin1String(",3000,,20")), "nothing carried forward into an empty cell");
    }
    {
        LogModel m(nullptr, 5000);
        QVector<LogEntryPtr> v;
        for (int i = 0; i < 20; ++i) v << entry(kLsrp, t0 + i * 1000) << entry(kArp, t0 + i * 1000 + 500);
        m.appendEntries(v);
        FieldPlotWindow w(&m, QStringLiteral("1_1"));
        w.resize(900, 560);
        w.show();
        w.plotField(QStringLiteral("LOCO_MODE"), QStringLiteral("lsrp"));
        CHECK(w.addField(QStringLiteral("LOCO_MODE"), QStringLiteral("arp")), "a second field is added");
        CHECK(w.seriesCount() == 2 && w.canvas()->seriesList().size() == 2, "both are plotted");
        CHECK(w.canvas()->laneCount() == 2, "each in its own lane by default");
        CHECK(!w.addField(QStringLiteral("LOCO_MODE"), QStringLiteral("arp")), "the same field twice is refused");
        w.setOverlay(true);
        CHECK(w.canvas()->laneCount() == 1, "\"One axis\" puts them on one axis");
        w.setOverlay(false);
        CHECK(w.addField(QStringLiteral("TRAIN_SPEED"), QStringLiteral("lsrp")), "a third");
        int added = w.seriesCount();
        const QStringList more{ "FRAME_NUM", "PKT_LENGTH", "TIN", "MOVEMENT_DIR" };
        for (const QString &f : more) if (w.addField(f, QStringLiteral("lsrp"))) ++added;
        CHECK(w.seriesCount() == FieldPlotWindow::kMaxSeries, "at most six");
        CHECK(w.removeSeries(1) && w.seriesCount() == FieldPlotWindow::kMaxSeries - 1, "✕ removes one");
        CHECK(w.currentField() == QLatin1String("LOCO_MODE"), "the first stays first");
        while (w.seriesCount() > 1) w.removeSeries(w.seriesCount() - 1);
        CHECK(!w.removeSeries(0), "the last field cannot be removed");

        w.addField(QStringLiteral("LOCO_MODE"), QStringLiteral("arp"));
        w.canvas()->setCursorMs(t0 + 5200);
        QTemporaryDir dir;
        const QString png = dir.filePath(QStringLiteral("p.png")), csvPath = dir.filePath(QStringLiteral("p.csv"));
        CHECK(w.saveImage(png) && QFile(png).size() > 1000, "Export: the plot saves as a PNG");
        CHECK(w.saveCsv(csvPath), "and its data as CSV");
        QFile f(csvPath);
        f.open(QIODevice::ReadOnly);
        const QStringList rows = QString::fromUtf8(f.readAll()).trimmed().split(QLatin1Char('\n'));
        CHECK(rows.size() == 41 && rows.first().contains(QLatin1String("lsrp.LOCO_MODE"))
                  && rows.first().contains(QLatin1String("arp.LOCO_MODE")),
              "40 sample times (20 LSRP + 20 ARP), both fields named by packet");
        const QByteArray dirEnv = qgetenv("DL_SHOTS");
        if (!dirEnv.isEmpty()) w.grab().save(QDir(QString::fromLocal8Bit(dirEnv)).filePath(QStringLiteral("multi.png")));
    }

    // =================================================================================
    //  Speed vs distance
    // =================================================================================
    using namespace SpeedDistance;
    {
        QVector<Sample> up, down;
        for (int i = 0; i < 10; ++i) {
            Sample s; s.locM = 1000 + i * 20; up << s;
            Sample d; d.locM = 5000 - i * 20; down << d;
        }
        CHECK(travelDirection(up) == 1 && travelDirection(down) == -1, "direction comes from the locations");
        QVector<Sample> reset = up;
        reset[5].locM = 90000;   // a location reset, not 90 km of travel
        for (int i = 6; i < 10; ++i) reset[i].locM = 90000 + (i - 5) * 20;
        CHECK(travelDirection(reset) == 1, "a jump over 500 m is a reset, not travel");
        CHECK(targetLocation(1000, 300, 1) == 1300 && targetLocation(1000, 300, -1) == 700,
              "a target is ahead in the direction of travel");
    }
    LogModel run(nullptr, 20000);
    {
        // A run: 60 DMI frames at 1 s, 50 m apart, speed rising past a
        // permitted 80 km/h from sample 40 to 44, one target 300 m ahead.
        // LSRPs alongside, with a mode change at 30 s, an RFID tag, and a
        // 12 s silence after 50 s.
        QVector<LogEntryPtr> v;
        for (int i = 0; i < 60; ++i) {
            const qint64 ms = t0 + i * 1000 + (i >= 50 ? 12000 : 0);
            const qint64 speed = (i >= 40 && i <= 44) ? 85 : 60 + i / 3;
            v << entry(withFields(kDmi, { { "abs_loco_loc", 200000 + i * 50 }, { "train_speed", speed },
                                          { "speed_limit_permissible", 80 }, { "target_distance", 300 },
                                          { "target_speed", 0 } }, false), ms);
            v << entry(withFields(kLsrp, { { "LOCO_MODE", i < 30 ? 2 : 3 }, { "LAST_RFID_TAG", i < 20 ? 0 : 417 },
                                           { "EMERGENCY_STATUS", 0 } }, true), ms + 100);
        }
        run.appendEntries(v);
    }
    {
        const Trace t = extract(&run);
        CHECK(t.source == QLatin1String("dmi") && t.samples.size() == 60, "the trace comes from DMI, one sample per frame");
        CHECK(t.direction == 1 && t.minLocM == 200000 && t.maxLocM == 200000 + 59 * 50, "locations and direction");
        CHECK(t.overspeedSamples == 5 && t.maxSpeedKmh == 85, "five samples above the permitted 80, max 85");
        CHECK(t.targets.size() >= 1 && t.targets.first().locM == 200300, "the target, 300 m ahead of the first sample");
        const QString csv = toCsv(t, 200000, 201000);
        CHECK(csv.startsWith(QLatin1String("time_local,epoch_ms,location_m,speed_kmh,permitted_kmh"))
                  && csv.trimmed().split(QLatin1Char('\n')).size() == 1 + 21,
              "CSV of a stretch: the samples between the two locations");

        LogModel lsrpOnly(nullptr, 1000);
        QVector<LogEntryPtr> v;
        for (int i = 0; i < 5; ++i) v << entry(withFields(kLsrp, { { "ABS_LOCO_LOC", 9000 - i * 30 }, { "TRAIN_SPEED", 40 } }, true), t0 + i * 1000);
        lsrpOnly.appendEntries(v);
        const Trace l = extract(&lsrpOnly);
        CHECK(l.source == QLatin1String("lsrp") && l.samples.size() == 5 && !l.samples.first().hasPermitted,
              "no DMI: LSRP location and speed, and no permitted speed claimed");
        CHECK(l.direction == -1, "and a train counting down is recognised");

        SpeedDistanceWindow w(&run, QStringLiteral("1_1"));
        w.resize(1000, 560);
        w.show();
        QTest::qWait(20);
        SpeedDistanceCanvas *c = w.canvas();
        CHECK(!c->isZoomed(), "opens on the whole run");
        c->setDistanceView(200500, 201500);
        CHECK(c->isZoomed() && c->viewFromM() >= 200499 && c->viewToM() <= 201501, "zooms to a stretch of track");
        c->resetZoom();
        c->setCursorSample(42);
        QTemporaryDir dir;
        CHECK(w.saveImage(dir.filePath(QStringLiteral("sd.png"))), "Export: image");
        CHECK(w.saveCsv(dir.filePath(QStringLiteral("sd.csv"))), "Export: CSV");
        CHECK(c->brakingNote().contains(QLatin1String("no @uba")), "no @uba in the tab: says so rather than drawing nothing silently");
        const QByteArray dirEnv = qgetenv("DL_SHOTS");
        if (!dirEnv.isEmpty()) w.grab().save(QDir(QString::fromLocal8Bit(dirEnv)).filePath(QStringLiteral("speed_distance.png")));
    }

    // =================================================================================
    //  Clock-skew alarm
    // =================================================================================
    {
        ClockSkewAlarm a(3000);
        int raised = 0, cleared = 0;
        QObject::connect(&a, &ClockSkewAlarm::raised, [&](const ClockSkewAlarm::Episode &) { ++raised; });
        QObject::connect(&a, &ClockSkewAlarm::cleared, [&](const ClockSkewAlarm::Episode &) { ++cleared; });
        a.sample(0, true, 1);
        a.sample(1000, true, 9);
        CHECK(!a.isRaised(), "one second outside is not yet an episode");
        a.sample(2000, true, 1);
        a.sample(3000, true, 9);
        a.sample(5000, true, 9);
        CHECK(!a.isRaised(), "a brief excursion that came back is not one either");
        a.sample(6500, true, 12);
        CHECK(a.isRaised() && raised == 1, "outside for 3 s: raised, once");
        CHECK(a.episodes().last().startMs == 3000 && a.episodes().last().worstGap == 12,
              "the episode starts when it went outside, and keeps the worst gap");
        a.sample(7000, true, -40);
        CHECK(a.episodes().last().worstGap == -40 && raised == 1, "the worst gap updates; not raised again");
        a.sample(20000, false, 0);
        CHECK(a.isRaised(), "no data is not \"back in step\"");
        a.sample(21000, true, 0);
        a.sample(22000, true, 1);
        CHECK(a.isRaised(), "back inside for under 3 s: still raised");
        a.sample(24000, true, 0);
        CHECK(!a.isRaised() && cleared == 1 && a.episodes().last().endMs == 21000,
              "inside for 3 s: cleared, ending when it came back");
        CHECK(FrameClock::classify(-2) != FrameClock::Accept::Ok && FrameClock::classify(4) != FrameClock::Accept::Stale,
              "the window is the asymmetric one: -2 s is out, +4 s is in");
        a.sample(30000, true, -2);
        a.sample(33500, true, -2);
        CHECK(a.isRaised() && raised == 2, "2 s early is outside");
        CHECK(ClockSkewAlarm::gapText(-2).contains(QLatin1String("behind")) && ClockSkewAlarm::durationText(125000) == QLatin1String("2 min 5 s"),
              "texts say which way and how long");
    }

    // =================================================================================
    //  Run summary report
    // =================================================================================
    {
        const RunReport::Summary s = RunReport::summarise(&run, QStringLiteral("1_1"), QStringLiteral("L1_V1"));
        CHECK(s.rows == 120 && s.packetCounts.value(QStringLiteral("dmi")) == 60
                  && s.packetCounts.value(QStringLiteral("lsrp")) == 60,
              "rows and packets counted");
        CHECK(s.gaps.size() == 1 && s.gaps.first().toMs - s.gaps.first().fromMs >= 12000, "the 12 s silence is found");
        CHECK(s.modeChanges.size() == 1 && s.modeChanges.first().from.startsWith(QLatin1String("2"))
                  && s.modeChanges.first().to.startsWith(QLatin1String("3")),
              "the mode change, with the schema's names");
        CHECK(s.tagReads.size() == 1 && s.tagReads.first().to == QLatin1String("417") && s.distinctTags == 1,
              "the RFID tag read once");
        CHECK(s.speedSource == QLatin1String("dmi") && s.maxSpeedKmh == 85, "highest speed");
        CHECK(s.overspeed.size() == 1 && s.overspeed.first().worst == 5.0,
              "one above-permitted episode, 5 km/h over at worst");
        CHECK(s.emergencies.isEmpty(), "no emergency status");
        const QString html = RunReport::toHtml(s);
        CHECK(html.contains(QLatin1String("OBSERVED")) && !html.contains(QLatin1String(">PASS<"))
                  && !html.contains(QLatin1String(">FAIL<")),
              "says OBSERVED and passes no verdict");
        for (const char *section : { "Silences over", "Loco mode", "Speed", "RFID tags", "clocks outside",
                                     "Faults", "reject condition" }) {
            CHECK(html.contains(QLatin1String(section)), QByteArray("the report has a section on: ") + section);
        }
        RunReport::Options few;
        few.maxListRows = 0;
        CHECK(RunReport::toHtml(s, few).contains(QLatin1String("in all")), "a capped list says how many there were in all");
        const QByteArray dirEnv = qgetenv("DL_SHOTS");
        if (!dirEnv.isEmpty()) {
            RunReportWindow rw(&run, QStringLiteral("1_1"), QStringLiteral("L1_V1"));
            rw.resize(820, 1400);
            rw.show();
            QTest::qWait(30);
            rw.grab().save(QDir(QString::fromLocal8Bit(dirEnv)).filePath(QStringLiteral("report.png")));
        }
        {
            RunReportWindow rw(&run, QStringLiteral("1_1"), QStringLiteral("L1_V1"));
            QTemporaryDir dir;
            const QString path = dir.filePath(QStringLiteral("r.html"));
            CHECK(rw.saveHtml(path) && QFile(path).size() > 1000, "the report window saves its HTML");
        }
    }
    {
        // Clock skew, offline: SLRP frames against the loco's own FRAME_NUM.
        const QString slrp = QStringLiteral(
            "@slrp_1_1 2026-06-27T14:02:27 21440 90 81 8A E6 04 1E 80 00 05 38 60 00 08 "
            "01 60 94 BD 80 10 00 64 00 00 00 13 12 CB 04 DA A0 37 D0");
        CHECK(rawOf(slrp, "FRAME_NUM") > 0, "the SLRP sample carries a FRAME_NUM");
        LogModel m(nullptr, 1000);
        QVector<LogEntryPtr> v;
        for (int i = 0; i < 20; ++i) {
            // Loco at 50000 + i; station in step for 10 frames, then 9 s behind.
            v << entry(withFields(kLsrp, { { "FRAME_NUM", 50000 + i } }, true), t0 + i * 1000);
            v << entry(withFields(slrp, { { "FRAME_NUM", 50000 + i - (i >= 10 ? 9 : 0) } }, true), t0 + i * 1000 + 300);
        }
        m.appendEntries(v);
        const RunReport::Summary s = RunReport::summarise(&m, QStringLiteral("1_1"), QString());
        CHECK(s.skewComparisons == 20, "every SLRP is compared with the loco's own clock");
        CHECK(s.clockSkew.size() == 1 && s.clockSkew.first().worst == 9.0
                  && s.clockSkew.first().fromMs == t0 + 10 * 1000 + 300,
              "one episode, from the first frame outside the window, loco 9 s ahead");
        const RunReport::Summary empty = RunReport::summarise(nullptr, QStringLiteral("x"), QString());
        CHECK(RunReport::toHtml(empty).contains(QLatin1String("no rows")), "an empty tab gives a report that says so");
    }
}
