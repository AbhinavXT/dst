#include "testutil.h"

#include "laneband.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "runreport.h"
#include "theme.h"
#include "uistyle.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QVBoxLayout>

// =============================================================================
//  Session 178 — the loco's own SoS (NMS LOCO_SELF_SOS) and collision
//  detections (NMS COLLISION_DETECTION), as episodes and marks.
//  Real frames, Abhinav's 81_1 of 2026-10-08 (expected from schema/engine.py):
//    replay/2026-10-08/loco_1_1_08102026_101300.cap: "Unusual stop start"
//      10:13:36 / 37, "Unusual stop end" 10:13:59 / 10:14:00 -> one episode
//    replay/2026-10-08/loco_1_1_08102026_105200.cap: start 11:08:14 / 15 and
//      no end before the excerpt ends at 11:13:00; COLLISION_DETECTION
//      "513 (loco 2, code 1)" twice at 11:03:34 -> one mark
// =============================================================================

namespace {
qint64 at178(const char *hms)
{
    return QDateTime::fromString(QStringLiteral("2026-10-08T") + QLatin1String(hms), Qt::ISODate).toMSecsSinceEpoch();
}
LogModel *load178(MessageDispatcher &d, const char *file)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/") + QLatin1String(file));
    if (!f.open(QIODevice::ReadOnly)) return nullptr;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3 || !l.startsWith('@')) continue;
        d.ingestLocal(81, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
    }
    d.drainNow();
    return d.modelForKey(QStringLiteral("81_1"));
}
}  // namespace

TEST_SUITE(session178)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    {
        MessageDispatcher d;
        LogModel *m = load178(d, "loco_1_1_08102026_101300.cap");
        CHECK(m != nullptr, "fixture: 10:13-10:17:30");
        if (!m) return;
        const RunReport::Summary s = RunReport::summarise(m, QStringLiteral("81_1"), QString());
        CHECK(s.selfSos.size() == 1 && s.selfSos.first().fromMs == at178("10:13:36") && s.selfSos.first().toMs == at178("10:13:59")
                  && s.selfSos.first().what == QLatin1String("Unusual stop"),
              "one unusual stop, 10:13:36 to 10:13:59 (the repeated start and end folded)");
        CHECK(s.collisionDetections.isEmpty(), "no collision detection then");
        CHECK(RunReport::toHtml(s).contains(QStringLiteral("Loco's own SoS (NMS LOCO_SELF_SOS): 1 episode")), "in the run summary");
    }
    {
        MessageDispatcher d;
        LogModel *m = load178(d, "loco_1_1_08102026_105200.cap");
        CHECK(m != nullptr, "fixture: 10:52-11:13");
        if (!m) return;
        const RunReport::Summary s = RunReport::summarise(m, QStringLiteral("81_1"), QString());
        CHECK(s.selfSos.size() == 1 && s.selfSos.first().fromMs == at178("11:08:14") && s.selfSos.first().toMs == at178("11:13:00")
                  && s.selfSos.first().what == QLatin1String("Unusual stop (no end)"),
              "a start with no end runs to the end of the log, and says so");
        CHECK(s.collisionDetections.size() == 1 && s.collisionDetections.first().ms == at178("11:03:34")
                  && s.collisionDetections.first().to == QLatin1String("513  (loco 2, code 1)"),
              "one collision detection, loco 2, at 11:03:34 (two frames folded) -- when loco 2's SoS began");

        QWidget host;
        auto *lay = new QVBoxLayout(&host);
        auto *band = new LaneBand;
        lay->addWidget(band);
        host.resize(1200, 300);
        host.show();
        band->setModel(m, QStringLiteral("81_1"), QStringLiteral("81_1"));
        QCoreApplication::processEvents();
        const QRect r = band->laneRect(1);
        auto at = [&](qint64 ms) {
            const double t = double(ms - band->fromMs()) / double(qMax<qint64>(1, band->toMs() - band->fromMs()));
            return QPoint(r.left() + int(t * r.width()), r.top() + 2);
        };
        CHECK(band->describeAt(at(at178("11:03:34"))).startsWith(QStringLiteral("Collision detection (NMS) at")),
              QByteArray("the Safety lane marks the collision detection: ") + band->describeAt(at(at178("11:03:34"))).toUtf8());
        CHECK(band->describeAt(at(at178("11:10:00"))).startsWith(QStringLiteral("Own SoS (NMS): Unusual stop (no end)")),
              QByteArray("and the own SoS: ") + band->describeAt(at(at178("11:10:00"))).toUtf8());
    }
}
