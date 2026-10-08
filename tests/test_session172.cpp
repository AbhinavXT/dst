#include "testutil.h"

#include "capturedecoder.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "twolocoview.h"
#include "twolocowindow.h"
#include "theme.h"
#include "uistyle.h"

#include <QComboBox>
#include <QDateTime>
#include <QFile>

// =============================================================================
//  Session 172 — two locos from one log: the other loco as this loco's
//  received ARPs report it.
//  Real frames: replay/2026-10-08/loco_1_1_08102026_105200.cap (loco 1, 10:52-11:13 on
//  2026-10-08), which holds 305 received ARPs, all from loco 2, 3 of them
//  failing their CRC (left out). Expected, from schema/engine.py (8-byte
//  received header) over the 305: located
//  161,099-161,694 m; 100 km/h at most; System_Failure from 10:58:33; one
//  SoS spell 11:03:33-11:06:41; no Trip.
//  And replay/2026-10-08/loco_2_1_08102026_162008.cap (loco 2): it hears itself (not a
//  heard loco, session 170) and loco 1.
// =============================================================================

namespace {
qint64 at172(const char *hms)
{
    return QDateTime::fromString(QStringLiteral("2026-10-08T") + QLatin1String(hms), Qt::ISODate).toMSecsSinceEpoch();
}

void load172(MessageDispatcher &disp, int src, int kv, const char *file)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/") + QLatin1String(file));
    if (!f.open(QIODevice::ReadOnly)) return;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3 || !l.startsWith('@')) continue;
        disp.ingestLocal(src, kv, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
    }
    disp.drainNow();
}
}  // namespace

TEST_SUITE(session172)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    MessageDispatcher one;
    load172(one, 81, 1, "loco_1_1_08102026_105200.cap");
    LogModel *m1 = one.modelForKey(QStringLiteral("81_1"));
    CHECK(m1 && m1->count() > 4000, "fixture: loco 1's excerpt");
    if (!m1) return;

    int crcFailed = 0;
    for (int i = 0; i < m1->count(); ++i) {
        const QString tx = m1->entryAt(i)->text;
        if (!tx.startsWith(QLatin1String("@arprecv_"))) continue;
        const CaptureLine c = CaptureDecoder::parseLine(tx);
        crcFailed += c.crcChecked && !c.crcOk ? 1 : 0;
    }
    CHECK(crcFailed == 3, "fixture: 3 of the 305 received ARPs fail their CRC (10:53:43, 10:57:59, 11:10:47)");
    CHECK(TwoLocoView::heardLocos(m1) == QVector<qint64>{ 2 }, "loco 1 heard one other loco: 2");
    const SpeedDistance::Trace t = TwoLocoView::heardTrace(m1, 2);
    CHECK(t.source == QLatin1String("arprecv") && t.samples.size() == 302,
          QByteArray("its trace: every received ARP whose CRC passes, 305 - 3 (") + QByteArray::number(t.samples.size()) + ")");
    CHECK(t.minLocM == 161099 && t.maxLocM == 161694 && t.maxSpeedKmh == 100,
          "located 161,099 to 161,694 m, 100 km/h at most (ABS_LOCO_LOC, TRAIN_SPEED)");
    CHECK(t.samples.first().mode == QLatin1String("2 (Staff_Responsible)") && t.samples.last().mode == QLatin1String("6 (On_Sight)"),
          "with its mode on each sample");

    const TwoLocoView::Pair p = TwoLocoView::buildHeard(m1, QStringLiteral("81_1"), m1, 2, QStringLiteral("loco 2 (heard)"));
    CHECK(p.eventsB.failure.size() == 1 && p.eventsB.failure.first().fromMs == at172("10:58:33") && p.eventsB.trip.isEmpty(),
          "loco 2's System_Failure from 10:58:33, as loco 1 heard it; no Trip");
    CHECK(p.eventsB.sos.size() == 1 && p.eventsB.sos.first().fromMs == at172("11:03:33") && p.eventsB.sos.first().toMs == at172("11:06:41")
              && p.eventsB.sos.first().what == QLatin1String("2 (SoS)"),
          "one SoS spell, 11:03:33 to 11:06:41 (EMERGENCY_STATUS 2)");
    CHECK(!p.a.isEmpty() && !p.gap.isEmpty(), QByteArray("loco 1's own trace and the gap between them (") + QByteArray::number(p.gap.size()) + " samples)");
    bool gapRight = true;
    for (const TwoLocoView::GapSample &g : p.gap) gapRight &= qFuzzyCompare(g.gapM + 1.0, g.bLocM - g.aLocM + 1.0) && g.aLocM > 0 && g.bLocM > 0;
    CHECK(gapRight, "each gap is B's location minus A's, both known");
    CHECK(p.eventsA.failure.size() >= 1, "loco 1's own System_Failure is marked too (from its mode)");

    // Loco 2's log: its own received ARP is not a heard loco.
    MessageDispatcher two;
    load172(two, 81, 2, "loco_2_1_08102026_162008.cap");
    LogModel *m2 = two.modelForKey(QStringLiteral("81_2"));
    CHECK(m2 && TwoLocoView::heardLocos(m2) == QVector<qint64>{ 1 }, "loco 2 heard loco 1; its own ARP coming back is not listed");

    // The window, with one tab: that tab and the loco it heard.
    TwoLocoWindow w(&one);
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    QComboBox *boxB = nullptr;
    const QList<QComboBox *> boxes = w.findChildren<QComboBox *>();
    if (boxes.size() >= 2) boxB = boxes.at(1);
    CHECK(boxB && boxB->findData(QStringLiteral("heard:81_1:2")) >= 0
              && boxB->itemText(boxB->findData(QStringLiteral("heard:81_1:2"))).startsWith(QStringLiteral("Loco 2, as heard by")),
          "Loco B offers 'Loco 2, as heard by 81_1'");
    CHECK(boxB && boxB->currentData().toString() == QStringLiteral("heard:81_1:2"),
          "with only one tab open, the window starts on it and the loco it heard");
}
