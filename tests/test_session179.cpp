#include "testutil.h"

#include "laneband.h"
#include "logmodel.h"
#include "logquery.h"
#include "messagedispatcher.h"
#include "runreport.h"
#include "theme.h"
#include "uistyle.h"
#include "watchrules.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QVBoxLayout>

// =============================================================================
//  Session 179 — session keys.
//  Real frames: replay/2026-10-08/loco_1_1_08102026_105200.cap (81_1).
//  Expected, from schema/engine.py: around each restart the NMS reports key
//  set 30 with "0 (No keys)" for a few seconds, then key set 20 with 10 sets
//  left. No-keys spells 10:52:20, 10:52:46-48, 10:56:28-30, 11:08:37-38, all
//  within a minute of a start of mission (10:52:51, 10:56:29, 11:08:56);
//  7 changes of running key set; key loads (@auth_keys1) at 10:52:36,
//  10:52:44 and 11:08:53.
// =============================================================================

namespace {
qint64 at179(const char *hms)
{
    return QDateTime::fromString(QStringLiteral("2026-10-08T") + QLatin1String(hms), Qt::ISODate).toMSecsSinceEpoch();
}
}  // namespace

TEST_SUITE(session179)
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

    const RunReport::Summary s = RunReport::summarise(model, QStringLiteral("81_1"), QString());
    const QVector<QPair<const char *, const char *>> spells{ { "10:52:20", "10:52:20" }, { "10:52:46", "10:52:48" },
                                                             { "10:56:28", "10:56:30" }, { "11:08:37", "11:08:38" } };
    bool same = s.noKeys.size() == spells.size();
    for (int i = 0; same && i < spells.size(); ++i)
        same = s.noKeys.at(i).fromMs == at179(spells.at(i).first) && s.noKeys.at(i).toMs == at179(spells.at(i).second)
               && s.noKeys.at(i).what == QLatin1String("no keys (at a start of mission)");
    CHECK(same, QByteArray("four no-keys spells, each at a start of mission (") + QByteArray::number(s.noKeys.size()) + ")");
    CHECK(s.minRemainingKeys == 0, "fewest key sets left: 0");
    CHECK(s.keySets.size() == 7 && s.keySets.first().from == QLatin1String("30  (KMS key set 30)")
              && s.keySets.first().to == QLatin1String("20  (KMS key set 20)") && s.keySets.first().ms == at179("10:52:45"),
          "seven changes of running key set, the first 30 -> 20 at 10:52:45");
    CHECK(s.keyLoads == (QVector<qint64>{ at179("10:52:36"), at179("10:52:44"), at179("11:08:53") }),
          "key loads at 10:52:36, 10:52:44 and 11:08:53");
    const QString html = RunReport::toHtml(s);
    CHECK(html.contains(QStringLiteral("Fewest key sets left: 0. No keys in 4 spells, 4 of them within a minute of a start of mission. 3 key loads")),
          "the run summary says it, with where the spells fall");

    // The ready-made watch matches the NMS frames that report no keys.
    const WatchRule *rule = WatchRules::byId(QStringLiteral("nokeys"));
    CHECK(rule != nullptr, "a ready-made watch: No session keys");
    if (rule) {
        LogQuery q;
        CHECK(q.parse(rule->expr), QByteArray("its query parses: ") + rule->expr.toUtf8());
        int hits = 0, nmsWithKeys = 0;
        for (int i = 0; i < model->count(); ++i) {
            const LogEntryPtr e = model->entryAt(i);
            if (q.match(*e)) ++hits;
            if (e->text.startsWith(QLatin1String("@nmshlth_")) && e->text.contains(QLatin1String("T10:52:55"))) nmsWithKeys += q.match(*e) ? 1 : 0;
        }
        CHECK(hits == 9 && nmsWithKeys == 0, QByteArray("it matches the 9 frames reporting 0 key sets, not one reporting 10 (") + QByteArray::number(hits) + ")");
    }

    QWidget host;
    auto *lay = new QVBoxLayout(&host);
    auto *band = new LaneBand;
    lay->addWidget(band);
    host.resize(1200, 300);
    host.show();
    band->setModel(model, QStringLiteral("81_1"), QStringLiteral("81_1"));
    QCoreApplication::processEvents();
    const QRect r = band->laneRect(3);
    const double t = double(at179("11:08:53") - band->fromMs()) / double(qMax<qint64>(1, band->toMs() - band->fromMs()));
    const QString tip = band->describeAt(QPoint(r.left() + int(t * r.width()), r.top() + 2));
    CHECK(tip.startsWith(QStringLiteral("Session keys loaded")) || tip.startsWith(QStringLiteral("No session keys")),
          QByteArray("the Link lane names them on hover: ") + tip.toUtf8());
}
