#include "testutil.h"

#include "faulttimelinewindow.h"
#include "iotimeline.h"
#include "iotimelinewindow.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "theme.h"
#include "uistyle.h"

#include <QDateTime>
#include <QFile>

// =============================================================================
//  Session 176 — cab inputs and outputs (DIO) on a timeline.
//  Real frames: replay/2026-10-08/loco_1_1_08102026_101300.cap, Abhinav's
//  81_1 capture from 10:13:00 to 10:17:30, every packet type (799 @dip1,
//  800 @dip2, 799 @dop1, 799 @dop2): the horn, traction cut off with PVEF,
//  the cab switched forward / reverse, a DMI button press, and the start of
//  mission at 10:15:22. Expected, from schema/engine.py with the same rule
//  (a bar where a signal reads other than its usual value): 42 changing
//  signals, 126 bars; e.g. DIP1 cab1_reverse 3 bars reading 1 (usually 0),
//  DIP1 traction_cutoff_fb 6 reading 0 (usually 1), DIP2 eb_relay_fb_no 1.
// =============================================================================

TEST_SUITE(session176)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    MessageDispatcher disp;
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_101300.cap"));
    CHECK(f.open(QIODevice::ReadOnly), "fixture");
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3 || !l.startsWith('@')) continue;
        disp.ingestLocal(81, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
    }
    disp.drainNow();
    LogModel *model = disp.modelForKey(QStringLiteral("81_1"));
    CHECK(model && model->count() > 4800, "fixture: the tab");
    if (!model) return;

    const FaultTimeline::Timeline t = IoTimeline::build(model);
    CHECK(t.rows.size() == 42 && t.bars.size() == 126,
          QByteArray("42 changing signals, 126 bars (") + QByteArray::number(t.rows.size()) + "/" + QByteArray::number(t.bars.size()) + ")");
    auto barsOf = [&t](const QString &row, QString *what) {
        int n = 0;
        for (const FaultTimeline::Bar &b : t.bars) if (b.row == row) { ++n; if (what) *what = b.fault; }
        return n;
    };
    QString what;
    CHECK(barsOf(QStringLiteral("DIP1 cab1_reverse"), &what) == 3 && what == QStringLiteral("reads 1 (usually 0)"),
          "the cab switched to reverse three times");
    CHECK(barsOf(QStringLiteral("DIP1 traction_cutoff_fb"), &what) == 6 && what == QStringLiteral("reads 0 (usually 1)"),
          "traction cut off six times (active-low feedback reads the same way)");
    CHECK(barsOf(QStringLiteral("DIP2 eb_relay_fb_no"), nullptr) == 1 && barsOf(QStringLiteral("DIP1 horn1_solenoid_no"), nullptr) == 2,
          "the EB relay once, the horn twice");
    CHECK(t.rows.contains(QStringLiteral("DOP1 pin 2")) && !t.rows.contains(QStringLiteral("DIP1 dmi1_sos_nc")),
          "outputs by pin; a signal that never changes has no row");
    CHECK(!t.modes.isEmpty(), "under the loco's mode");

    IoTimelineWindow w(model, QStringLiteral("81_1"), QStringLiteral("81_1"));
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    w.resize(1000, 600);
    w.show();
    CHECK(w.timeline().bars.size() == 126, "the window draws them");
    const int row = t.rows.indexOf(QStringLiteral("DIP1 cab1_reverse"));
    qint64 at = 0;
    for (const FaultTimeline::Bar &b : t.bars) if (b.row == QLatin1String("DIP1 cab1_reverse")) { at = (b.fromMs + b.toMs) / 2; break; }
    const QString tip = w.canvas()->describeAt(QPoint(w.canvas()->xAtMs(at), w.canvas()->rowRect(row).center().y()));
    CHECK(tip.contains(QStringLiteral("cab1_reverse: reads 1 (usually 0)")), QByteArray("hover a bar: ") + tip.toUtf8());
    if (!qgetenv("DL_SHOTS").isEmpty()) w.grab().save(QString::fromLocal8Bit(qgetenv("DL_SHOTS")) + QStringLiteral("/cab_io.png"));
    CHECK(w.minimumSizeHint().width() <= 1100 && w.minimumSizeHint().height() <= 700, "fits a laptop");
}
