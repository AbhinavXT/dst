#include "testutil.h"

#include "logentry.h"
#include "logmodel.h"
#include "tabhealth.h"

#include <QDateTime>
#include <QFile>

// =============================================================================
//  Session 336 — the status bar's chip for the tab on show: rate, time since
//  the last row, and ▲ gaps (lines that may be missing), in the offline
//  thresholds' tones. Real run: replay/loco_1_1_27062026_170217.cap, with
//  @arp lines dropped from the middle to make a real gap.
// =============================================================================

namespace {
QVector<LogEntryPtr> realRows(int max, int dropArpFrom, int dropArpTo)
{
    QVector<LogEntryPtr> out;
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_170217.cap"));
    if (!f.open(QIODevice::ReadOnly)) return out;
    int n = 0;
    while (!f.atEnd() && n < max) {
        const QString l = QString::fromLatin1(f.readLine()).trimmed();
        if (!l.startsWith(QLatin1Char('@'))) continue;
        ++n;
        if (n >= dropArpFrom && n < dropArpTo && l.startsWith(QLatin1String("@arp_"))) continue;
        auto e = QSharedPointer<LogEntry>::create();
        e->header.source_id = 21;
        e->header.kvchId = 1;
        e->epochMs = QDateTime::fromString(l.section(QLatin1Char(' '), 1, 1), Qt::ISODate).toMSecsSinceEpoch();
        e->text = l;
        e->cacheDerived();
        out << e;
    }
    return out;
}
}  // namespace

TEST_SUITE(session336)
{
    using namespace TabHealth;

    // ---- the words and the tone -------------------------------------------------
    CHECK(describe(Reading()).text.isEmpty(), "no tab: no chip");
    {
        Reading r; r.name = QStringLiteral("21_1");
        const Shown s = describe(r);
        CHECK(s.text == QStringLiteral("21_1 · waiting for traffic") && s.tone == Tone::Idle, "nothing yet: waiting, no colour");
    }
    Reading live; live.name = QStringLiteral("21_1"); live.rate = 12.4; live.silentMs = 400;
    CHECK(describe(live).text == QStringLiteral("21_1 · 12/s · 0.4 s ago") && describe(live).tone == Tone::Ok,
          "live: rate and age, in step");
    Reading slow = live; slow.rate = 0.5; slow.silentMs = 12'000;
    CHECK(describe(slow).text == QStringLiteral("21_1 · 0.5/s · 12 s ago") && describe(slow).tone == Tone::Warn,
          "past the quiet threshold (10 s): amber");
    Reading dead = live; dead.rate = 0; dead.silentMs = 31'000;
    CHECK(describe(dead).text.contains(QStringLiteral("idle")) && describe(dead).tone == Tone::Fail
              && describe(dead).tip.contains(QStringLiteral("offline threshold")),
          "past the offline threshold (30 s): red, and the tip says which");
    Reading off = dead; off.warnSec = 0; off.errSec = 0;
    CHECK(describe(off).tone == Tone::Ok, "thresholds off: no alarm from silence");
    Reading gap = live; gap.lateGaps = 3; gap.sinceLastGapMs = 20'000;
    CHECK(describe(gap).text.endsWith(QStringLiteral("· 3 gaps")) && describe(gap).tone == Tone::Warn,
          "a recent ▲ gap: counted, amber");
    gap.sinceLastGapMs = 5 * 60'000;
    CHECK(describe(gap).tone == Tone::Ok && describe(gap).text.endsWith(QStringLiteral("· 3 gaps")),
          "an old one: still counted, no longer amber");

    // ---- the model counts ▲ gaps --------------------------------------------------
    // The run has gaps of its own; dropping ARP lines must add to them.
    int unbroken = -1;
    {
        LogModel model(nullptr, 100000);
        model.appendEntries(realRows(2000, 0, 0));
        unbroken = model.lateGapCount();
        CHECK(model.count() == 2000, QByteArray("fixture: 2000 real rows, with ") + QByteArray::number(unbroken)
                                         + " gaps of the run's own");
    }
    {
        LogModel model(nullptr, 100000);
        const QVector<LogEntryPtr> rows = realRows(2000, 1000, 1200);
        model.appendEntries(rows);
        CHECK(model.lateGapCount() > unbroken, "ARP lines dropped from the middle: one more ▲ gap at least");
        CHECK(model.lastLateGapMs() >= rows.first()->epochMs && model.lastLateGapMs() <= rows.last()->epochMs,
              "the newest one's time is a row's time");
        model.clear();
        CHECK(model.lateGapCount() == 0 && model.lastLateGapMs() < 0, "Clear starts the count again");
    }
}
