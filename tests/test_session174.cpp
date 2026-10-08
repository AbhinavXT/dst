#include "testutil.h"

#include "faulttimelinewindow.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "theme.h"
#include "uistyle.h"

#include <QDateTime>
#include <QFile>

// =============================================================================
//  Session 174 — fault timeline.
//  Real frames: replay/2026-10-08/loco_1_1_08102026_105200.cap (81_1,
//  10:52-11:13: 57 @nmsflt, 226 @ccsys). Expected, from schema/engine.py
//  with the same rules (an NMS fault clears when a later frame from the same
//  reporting subsystem omits it; an LCU element is down while its bit is 0):
//    97 bars on 23 rows, 2 still open at the end of the log;
//    System_Failure at 10:52:36, 10:56:27 and 11:08:52 (the missions'
//    modes), with 15, 1 and 9 faults raised then;
//    the first bar: Analogue Card Mc-2, safety error, 10:52:20-10:52:22.
// =============================================================================

namespace {
qint64 at174(const char *hms)
{
    return QDateTime::fromString(QStringLiteral("2026-10-08T") + QLatin1String(hms), Qt::ISODate).toMSecsSinceEpoch();
}
}  // namespace

TEST_SUITE(session174)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    MessageDispatcher disp;
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_105200.cap"));
    CHECK(f.open(QIODevice::ReadOnly), "fixture");
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3 || !l.startsWith('@')) continue;
        disp.ingestLocal(81, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
    }
    disp.drainNow();
    LogModel *model = disp.modelForKey(QStringLiteral("81_1"));
    CHECK(model != nullptr, "fixture: the tab");
    if (!model) return;

    const FaultTimeline::Timeline t = FaultTimeline::build(model);
    int open = 0;
    for (const FaultTimeline::Bar &b : t.bars) open += b.open ? 1 : 0;
    CHECK(t.bars.size() == 97 && t.rows.size() == 23 && open == 2,
          QByteArray("97 bars on 23 rows, 2 open at the end (") + QByteArray::number(t.bars.size()) + "/"
              + QByteArray::number(t.rows.size()) + "/" + QByteArray::number(open) + ")");
    const FaultTimeline::Bar &first = t.bars.first();
    CHECK(first.row.contains(QStringLiteral("Analogue Card Mc-2")) && first.fault.contains(QStringLiteral("safety error"))
              && first.fromMs == at174("10:52:20") && first.toMs == at174("10:52:22") && !first.open && first.source == QLatin1String("NMS"),
          QByteArray("the first: ") + first.row.toUtf8() + " / " + first.fault.toUtf8());
    CHECK(t.rows.contains(QStringLiteral("LCU-2 can0")) && t.rows.contains(QStringLiteral("LCU-1 radio1")), "LCU elements have rows of their own");
    CHECK(t.failures == (QVector<qint64>{ at174("10:52:36"), at174("10:56:27"), at174("11:08:52") }),
          "System_Failure at 10:52:36, 10:56:27 and 11:08:52");
    CHECK(t.activeAt(at174("10:52:36")).size() == 15 && t.activeAt(at174("10:56:27")).size() == 1
              && t.activeAt(at174("11:08:52")).size() == 9,
          "15, 1 and 9 faults raised at them");
    bool gsm = false;
    for (const FaultTimeline::Bar &b : t.activeAt(at174("10:56:27"))) gsm = b.row.contains(QStringLiteral("GSM-1"));
    CHECK(gsm, "the one at 10:56:27 is GSM-1");
    CHECK(!t.modes.isEmpty() && t.modes.first().fromMs == at174("10:52:00"), "the mode band starts with the log");

    const QString html = FaultTimeline::failuresHtml(t);
    CHECK(html.contains(QStringLiteral("10:52:36 — 15 raised")) && html.contains(QStringLiteral("10:56:27 — 1 raised")),
          "the list under the chart");

    FaultTimelineWindow w(model, QStringLiteral("81_1"), QStringLiteral("81_1"));
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    w.resize(1000, 640);
    w.show();
    CHECK(w.timeline().bars.size() == 97 && w.failuresText().contains(QStringLiteral("15 raised")), "the window shows both");
    FaultTimelineCanvas *c = w.canvas();
    const int row = t.rows.indexOf(first.row);
    const QString tip = c->describeAt(QPoint(c->xAtMs(at174("10:52:21")), c->rowRect(row).center().y()));
    CHECK(tip.contains(QStringLiteral("safety error")) && tip.contains(QStringLiteral("10:52:20 to 10:52:22")),
          QByteArray("hover a bar: ") + tip.toUtf8());
    const QString modeTip = c->describeAt(QPoint(c->xAtMs(at174("10:52:36")), c->rowRect(-1).center().y()));
    CHECK(modeTip.contains(QStringLiteral("System_Failure")), QByteArray("hover the mode band at a failure: ") + modeTip.toUtf8());
    if (!qgetenv("DL_SHOTS").isEmpty()) w.grab().save(QString::fromLocal8Bit(qgetenv("DL_SHOTS")) + QStringLiteral("/fault_timeline.png"));
    CHECK(w.minimumSizeHint().width() <= 1100 && w.minimumSizeHint().height() <= 700, "fits a laptop");
}
