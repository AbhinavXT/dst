#include "testutil.h"

#include "brakechain.h"
#include "brakechainwindow.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "runreport.h"
#include "theme.h"
#include "uistyle.h"

#include <QDateTime>
#include <QFile>
#include <QSignalSpy>
#include <QTableWidget>

// =============================================================================
//  Session 453 — the brake chain: each brake KAVACH applied, and the time to
//  each link the capture shows.
//  Real frames: replay/2026-10-08/loco_1_1_08102026_101300.cap (81_1, 10:13 to
//  10:17:30). schema/engine.py over it: @dmi brake_type 4 at 10:15:22,
//  10:15:48 and 10:16:20; in the same second DOP1 / DOP2 emergency_brake
//  output 1 -> 0 and full_service_brake 0 -> 1, and DIP2 le_relay_fb_no 0 -> 1.
//  No real @biu_* or non-zero @analog_* exists: those links are tested on
//  lines made for it (the fixture's byte layout, schema/fixtures/
//  biu_synthetic.log), placed around the real 10:15:22 EB.
// =============================================================================

namespace {

qint64 at(const char *iso, int ms = 0)
{
    return QDateTime::fromString(QString::fromLatin1(iso), Qt::ISODate).toMSecsSinceEpoch() + ms;
}

void ingestFile(MessageDispatcher &disp, const QString &path, qint64 fromMs = 0, qint64 toMs = 0, bool skipAnalog = false)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3 || !l.startsWith('@')) continue;
        const qint64 ms = QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch();
        if (toMs && (ms < fromMs || ms > toMs)) continue;
        if (skipAnalog && l.startsWith("@analog_")) continue;
        disp.ingestLocal(81, 1, l, ms, QString());
    }
}

}  // namespace

TEST_SUITE(session453)
{
    using namespace BrakeChain;
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    const QString fixture = QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_101300.cap");

    // ---- the real capture: decision, output, relay feedback -------------------------------
    {
        MessageDispatcher disp;
        ingestFile(disp, fixture);
        disp.drainNow();
        LogModel *model = disp.modelForKey(QStringLiteral("81_1"));
        CHECK(model && model->count() > 4800, "fixture: the tab");
        if (!model) return;
        const Chain c = build(model);
        CHECK(c.apps.size() == 3, QByteArray("three brake applications (") + QByteArray::number(c.apps.size()) + ")");
        if (c.apps.size() != 3) return;
        const qint64 onsets[3] = { at("2026-10-08T10:15:22"), at("2026-10-08T10:15:48"), at("2026-10-08T10:16:20") };
        bool allEb = true, allOnset = true, outputs = true, relays = true;
        for (int i = 0; i < 3; ++i) {
            const Application &a = c.apps.at(i);
            allEb = allEb && a.brakeType == 4 && a.type.contains(QLatin1String("EMERGENCY_BRAKE"));
            allOnset = allOnset && a.fromMs == onsets[i];
            outputs = outputs && a.delayMs(Output) == 0 && a.links[Output].what.contains(QStringLiteral("emergency_brake output 1 → 0"))
                      && a.links[Output].what.contains(QStringLiteral("full_service_brake output 0 → 1"));
            relays = relays && a.delayMs(RelayFeedback) == 0 && a.links[RelayFeedback].what.contains(QStringLiteral("DIP2 le_relay_fb_no 0 → 1"));
        }
        CHECK(allEb && allOnset, "EB at 10:15:22, 10:15:48, 10:16:20, as engine.py reads the DMI");
        CHECK(outputs, "each: the DOP brake outputs change in the same second (+0 s), as logged");
        CHECK(relays, "each: DIP2 le_relay_fb_no 0 → 1 in the same second");
        CHECK(c.apps.at(0).links[Output].what.startsWith(QLatin1String("DOP1 ")) && c.apps.at(0).links[Output].what.contains(QLatin1String("DOP1 emergency_brake")),
              "the first change named, with what else changed at that instant");
        bool noBiu = true;
        for (const Application &a : c.apps)
            for (int s : { BiuCommand, BiuMoves, BiuAtTarget, AnalogMoves, AnalogAtTarget }) noBiu = noBiu && !a.links[s].seen();
        CHECK(noBiu && c.biuCommands == 0 && c.analogFrames > 500, "no @biu_* in it; its 537 analog frames are all zero: no pressure link");
        CHECK(c.apps.at(0).speedAtOnset == 0 && !c.apps.at(0).links[SpeedFalls].seen(), "standing at the onset: no speed link");
        // engine.py: 14 km/h at 10:15:48, 12 at :50, 0 at :56; 14 at 10:16:20, 12 at :22, 0 at :28.
        CHECK(c.apps.at(1).speedAtOnset == 14 && c.apps.at(1).delayMs(SpeedFalls) == 2000 && c.apps.at(1).delayMs(Standstill) == 8000
                  && c.apps.at(1).links[SpeedFalls].what == QStringLiteral("14 → 12 km/h"),
              "EB at 14 km/h: the speed falls at +2 s, standstill at +8 s");
        CHECK(c.apps.at(2).delayMs(SpeedFalls) == 2000 && c.apps.at(2).delayMs(Standstill) == 8000, "and the same at 10:16:20");
        // The reasons are session 175's.
        const QVector<RunReport::BrakeEvent> ev = RunReport::brakeEvents(model);
        bool sameReasons = true;
        for (const Application &a : c.apps)
            for (const RunReport::BrakeEvent &e : ev)
                if (a.fromMs >= e.fromMs && a.fromMs <= e.toMs) sameReasons = sameReasons && a.reasons == e.reasons;
        CHECK(sameReasons, "reasons as RunReport::brakeEvents gives them");
        const QString csv = toCsv(c);
        CHECK(csv.count(QLatin1Char('\n')) == 4 && csv.contains(QStringLiteral("2026-10-08 10:15:22.000")), "CSV: a header and three rows");

        // The window over the same tab.
        BrakeChainWindow w(model, QStringLiteral("81_1"), QStringLiteral("Loco 81"));
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.resize(1100, 680);
        w.show();
        QTableWidget *t = w.table();
        int outCol = -1, biuCol = -1, standCol = -1;
        for (int k = 0; k < t->columnCount(); ++k) {
            const QString h = t->horizontalHeaderItem(k)->text();
            if (h == stepName(Output)) outCol = k;
            if (h == stepName(BiuCommand)) biuCol = k;
            if (h == stepName(Standstill)) standCol = k;
        }
        CHECK(t->rowCount() == 3 && outCol > 0 && t->item(0, outCol)->text() == QLatin1String("+0 s")
                  && t->item(0, biuCol)->text() == QStringLiteral("—") && t->item(0, standCol)->text() == QLatin1String("standing"),
              "the table: +0 s for the output, — for the BIU, standing for the speed");
        const int x = w.chart()->xAtMs(onsets[0]);
        CHECK(w.chart()->describeAt(QPoint(x, w.chart()->height() / 2)).contains(QStringLiteral("KAVACH decides +0 s")),
              "the chart: hovering the decision's mark names it");
        QSignalSpy jump(&w, &BrakeChainWindow::jumpRequested);
        emit t->cellDoubleClicked(1, outCol);
        CHECK(jump.count() == 1 && jump.at(0).at(1).toLongLong() == onsets[1], "double-click a cell: the log at that link");
    }

    // ---- the BIU and the analog card, around the real 10:15:22 EB ------------------------------
    {
        MessageDispatcher disp;
        // The real analog frames (all zero) left out: the made ones stand for the card.
        ingestFile(disp, fixture, at("2026-10-08T10:15:00"), at("2026-10-08T10:15:50"), true);
        const struct { qint64 ms; const char *line; } made[] = {
            { at("2026-10-08T10:15:19", 0),   "@biu_cmd_1_1 2026-10-08T10:15:19 1 E8 03 12 0C 64 00 00 00" },     // A9 5.00
            { at("2026-10-08T10:15:19", 100),   "@biu_press_1_1 2026-10-08T10:15:19 2 64 00 B4 64 00 00 00 1F" },   // BP 5.00 BC 0
            { at("2026-10-08T10:15:19", 200),   "@analog_top_1_1 2026-10-08T10:15:19 3 00 00 A0 40 00 00 00 00 00 00 10 41 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00" },
            { at("2026-10-08T10:15:22", 250), "@biu_cmd_1_1 2026-10-08T10:15:22 4 E8 03 12 0C 00 30 00 00" },     // A9 0, SA9 2.40
            { at("2026-10-08T10:15:22", 500), "@biu_press_1_1 2026-10-08T10:15:22 5 5A 08 B4 00 30 00 00 1F" },   // BP 4.50 BC 0.40
            { at("2026-10-08T10:15:22", 750), "@analog_top_1_1 2026-10-08T10:15:22 6 33 33 93 40 CD CC CC 3D 00 00 10 41 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00" },
            { at("2026-10-08T10:15:24", 0),   "@biu_press_1_1 2026-10-08T10:15:24 7 01 30 B4 00 30 00 00 1F" },   // BP 0.05 BC 2.40
            { at("2026-10-08T10:15:24", 500), "@analog_top_1_1 2026-10-08T10:15:24 8 00 00 00 00 CD CC 1C 40 00 00 10 41 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00" },
        };
        disp.drainNow();
        for (const auto &m : made) disp.ingestLocal(81, 1, QByteArray(m.line), m.ms, QString());
        disp.drainNow();
        LogModel *model = disp.modelForKey(QStringLiteral("81_1"));
        CHECK(model != nullptr, "the mixed tab");
        if (!model) return;
        const Chain c = build(model);
        CHECK(c.apps.size() == 2 && c.biuCommands == 2 && c.biuPressures == 3, "two EBs in 10:15:00-50; the made lines read");
        if (c.apps.isEmpty()) return;
        const Application &a = c.apps.first();
        CHECK(a.delayMs(BiuCommand) == 250 && a.links[BiuCommand].what == QStringLiteral("A9 5.00 → 0.00, SA9 0.00 → 2.40 kg/cm²"),
              qPrintable(QStringLiteral("BIU command +0.25 s (%1): %2").arg(a.delayMs(BiuCommand)).arg(a.links[BiuCommand].what)));
        CHECK(a.delayMs(BiuMoves) == 500 && a.links[BiuMoves].what == QStringLiteral("BIU BP 5.00 → 4.50, BC 0.00 → 0.40 kg/cm²"),
              qPrintable(QStringLiteral("BIU pressure moves +0.5 s: %1").arg(a.links[BiuMoves].what)));
        CHECK(a.delayMs(BiuAtTarget) == 2000 && a.links[BiuAtTarget].what.contains(QStringLiteral("BP 0.05 (A9 0.00)")),
              "BIU at target +2 s (BP within 0.1 of A9)");
        CHECK(a.delayMs(AnalogMoves) == 750 && a.links[AnalogMoves].what.startsWith(QStringLiteral("analog top BP 5.00 → 4.60")),
              qPrintable(QStringLiteral("analog moves +0.75 s: %1").arg(a.links[AnalogMoves].what)));
        CHECK(a.delayMs(AnalogAtTarget) == 2500, "analog at target +2.5 s (BC 2.45 against SA9 2.40)");
        CHECK(delayText(250) == QLatin1String("+0.250 s") && delayText(500) == QLatin1String("+0.5 s")
                  && delayText(2000) == QLatin1String("+2 s") && delayText(-1000) == QStringLiteral("−1 s"),
              "delays as text");
        CHECK(c.series[A9].size() == 2 && c.series[BiuBc].size() == 3 && c.series[AnalogTopBp].size() >= 3, "the chart's series");
        // The second EB (10:15:48): the command has not changed since: no BIU command link.
        CHECK(c.apps.size() == 2 && !c.apps.at(1).links[BiuCommand].seen(), "a command unchanged since the last brake: not a link");
    }
}
