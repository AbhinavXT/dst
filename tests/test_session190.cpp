#include "testutil.h"

#include "laneband.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "runreport.h"
#include "theme.h"
#include "uistyle.h"

#include <QDateTime>
#include <QFile>

// =============================================================================
//  Session 190 — the mode lane shows SR (and Stand_By, System_Failure) while
//  the loco has no direction and no location.
//
//  A loco that is not localised sends no LSRP at all, only ARPs; the mode was
//  read from LSRP only. Now the ARP's LOCO_MODE counts when no LSRP has been
//  heard for 3 s. Real frames, Abhinav's 2026-10-08 logs; expected values
//  from schema/engine.py with the same rule:
//    loco_2_1_08102026_162008.cap   56 ARPs, all SR, direction 0, location 0,
//                                    no LSRP: SR from 16:20:09, no change
//    loco_1_1_08102026_101300.cap   7 changes, 6 of them from ARPs, among them
//                                    Stand_By -> SR at 10:16:04 and 10:17:28
// =============================================================================

namespace {
qint64 at190(const char *hms)
{
    return QDateTime::fromString(QStringLiteral("2026-10-08T") + QLatin1String(hms), Qt::ISODate).toMSecsSinceEpoch();
}
LogModel *load190(MessageDispatcher &d, const char *file, quint8 src)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/") + QLatin1String(file));
    if (!f.open(QIODevice::ReadOnly)) return nullptr;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3 || !l.startsWith('@')) continue;
        d.ingestLocal(src, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
    }
    d.drainNow();
    return d.modelForKey(QStringLiteral("%1_1").arg(src));
}
}  // namespace

TEST_SUITE(session190)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    // ---- loco 2, SR the whole time, never localised: ARPs only ---------------------
    {
        MessageDispatcher d;
        LogModel *m = load190(d, "loco_2_1_08102026_162008.cap", 82);
        CHECK(m != nullptr, "fixture: loco 2, 16:20");
        if (!m) return;
        const RunReport::Summary s = RunReport::summarise(m, QStringLiteral("82_1"), QString());
        CHECK(s.firstMode == QLatin1String("2 (Staff_Responsible)") && s.modeChanges.isEmpty(),
              QByteArray("SR from the first ARP, no change (first mode: ") + s.firstMode.toUtf8() + ")");

        LaneBand lanes;
        lanes.resize(1000, lanes.sizeHint().height());
        lanes.setModel(m, QStringLiteral("82_1"), QString());
        lanes.show();
        lanes.rebuildNow();
        const QRect r = lanes.laneRect(0);
        const QString tip = lanes.describeAt(r.center());
        CHECK(tip.startsWith(QLatin1String("Mode 2 (Staff_Responsible) at ")),
              QByteArray("the mode lane shows SR (was empty): ") + tip.toUtf8());
    }

    // ---- loco 1: SR after a restart, at 0 m with no direction ---------------------------
    {
        MessageDispatcher d;
        LogModel *m = load190(d, "loco_1_1_08102026_101300.cap", 81);
        CHECK(m != nullptr, "fixture: loco 1, 10:13-10:17:30");
        if (!m) return;
        const RunReport::Summary s = RunReport::summarise(m, QStringLiteral("81_1"), QString());
        CHECK(s.modeChanges.size() == 7, QByteArray("7 mode changes (") + QByteArray::number(s.modeChanges.size()) + ")");
        bool srAt1604 = false, srAt1728 = false, lsAt1401 = false;
        for (const RunReport::Change &c : s.modeChanges) {
            if (c.ms == at190("10:16:04") && c.from == QLatin1String("1 (Stand_By)") && c.to == QLatin1String("2 (Staff_Responsible)")) srAt1604 = true;
            if (c.ms == at190("10:17:28") && c.to == QLatin1String("2 (Staff_Responsible)")) srAt1728 = true;
            if (c.ms == at190("10:14:01") && c.to == QLatin1String("3 (Limited_Supervision)")) lsAt1401 = true;
        }
        CHECK(srAt1604 && srAt1728, "Stand_By -> SR at 10:16:04 and again at 10:17:28, from the ARP");
        CHECK(lsAt1401, "and Limited_Supervision at 10:14:01, which no LSRP carried");
        CHECK(s.modeChanges.first().ms == at190("10:13:24") && s.modeChanges.first().to == QLatin1String("4 (Full_Supervision)"),
              "where LSRP is heard it still decides (On_Sight -> Full_Supervision, 10:13:24)");
        CHECK(RunReport::toHtml(s).contains(QLatin1String("Loco mode (LSRP, else ARP): 7 changes")), "the report says where modes come from");
    }
}
