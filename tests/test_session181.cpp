#include "testutil.h"

#include "lccheck.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "runreport.h"

#include <QDateTime>
#include <QFile>

// =============================================================================
//  Session 181 — level crossings approached, and the horn.
//  Real frames: replay/2026-10-08/loco_1_1_08102026_103520.cap, Abhinav's 81_1
//  from 10:35:20 to 10:38:30, every packet type. Expected, from
//  schema/engine.py with the same rules:
//    LC 19 (Unmanned, auto whistle 1) 10:35:34-10:35:35, 727 -> 713 m,
//      horn at 10:35:35
//    LC 191 (Manned, auto whistle 1) 10:35:36-10:38:12, 699 -> 286 m,
//      horn 10:35:36-42, 10:35:50-10:36:02, 10:37:17-25, 10:37:31-10:38:06
//  Over the whole 81_1 day, the 12:02 and 14:54 approaches to LC 191 show no
//  horn at all (not in a fixture: noted in the CHANGELOG).
// =============================================================================

namespace {
qint64 at181(const char *hms)
{
    return QDateTime::fromString(QStringLiteral("2026-10-08T") + QLatin1String(hms), Qt::ISODate).toMSecsSinceEpoch();
}
}  // namespace

TEST_SUITE(session181)
{
    MessageDispatcher disp;
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_103520.cap"));
    CHECK(f.open(QIODevice::ReadOnly), "fixture");
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3 || !l.startsWith('@')) continue;
        disp.ingestLocal(81, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
    }
    disp.drainNow();
    LogModel *model = disp.modelForKey(QStringLiteral("81_1"));
    CHECK(model && model->count() > 3000, "fixture: the tab");
    if (!model) return;

    const QVector<LcCheck::Approach> a = LcCheck::build(model);
    CHECK(a.size() == 2, QByteArray("two approaches (") + QByteArray::number(a.size()) + ")");
    if (a.size() != 2) return;
    CHECK(a[0].lc == QLatin1String("19 suf=1") && a[0].manning == QLatin1String("Unmanned") && a[0].autoWhistle == QLatin1String("1")
              && a[0].fromMs == at181("10:35:34") && a[0].toMs == at181("10:35:35") && a[0].fromDistM == 727 && a[0].toDistM == 713,
          "LC 19, unmanned, 10:35:34-35, 727 -> 713 m");
    CHECK(a[0].horn.size() == 1 && a[0].horn.first().first == at181("10:35:35"), "the horn at 10:35:35");
    CHECK(a[1].lc == QLatin1String("191 suf=1") && a[1].manning == QLatin1String("Manned")
              && a[1].fromMs == at181("10:35:36") && a[1].toMs == at181("10:38:12") && a[1].fromDistM == 699 && a[1].toDistM == 286,
          "LC 191, manned, 10:35:36-10:38:12, 699 -> 286 m");
    const QVector<QPair<qint64, qint64>> want{ { at181("10:35:36"), at181("10:35:42") }, { at181("10:35:50"), at181("10:36:02") },
                                               { at181("10:37:17"), at181("10:37:25") }, { at181("10:37:31"), at181("10:38:06") } };
    CHECK(a[1].dioSeen && a[1].horn == want, "four horn spells during it, as engine.py reads the solenoid feedback");

    const QString html = LcCheck::toHtml(a);
    CHECK(html.contains(QStringLiteral("Level crossings approached (DMI lc.id): 2")) && html.contains(QStringLiteral("10:35:36–10:35:42")),
          "the table");
    const RunReport::Summary s = RunReport::summarise(model, QStringLiteral("81_1"), QString());
    CHECK(s.lcApproaches.size() == 2 && RunReport::toHtml(s).contains(QStringLiteral("Level crossings approached")),
          "in the run summary");
}
