#include "testutil.h"

#include "fleetsummary.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "missionreport.h"

#include <QDateTime>
#include <QFile>

// =============================================================================
//  Session 219 — the start-up, step by step (mission report, fleet summary).
//  Real frames: session 171's fixture, replay/2026-10-08/loco_1_1_08102026_105200.cap.
//  Expected, with schema/engine.py and the capture's own lines:
//    mission 2 (10:56:29)  "Select Train Config" 10:56:34 to 11:00:48 (engine.py),
//                          Staff_Responsible 11:00:50 (session 171), first
//                          @rfid 11:01:04; no self test, no supervised mode.
//                          The longest step: train config, 4 min 14 s.
//    mission 3 (11:08:56)  Staff_Responsible 11:09:10, first @rfid 11:09:15,
//                          On_Sight 11:12:32 (session 171): the longest step.
//  Mission 2's DMI shows "System Self Test" again at 11:08:45, after its
//  Staff_Responsible: not a start-up step.
// =============================================================================

namespace {
qint64 at219(const char *hms)
{
    return QDateTime::fromString(QStringLiteral("2026-10-08T") + QLatin1String(hms), Qt::ISODate).toMSecsSinceEpoch();
}
qint64 stepAt(const QVector<Missions::StartUpStep> &steps, const char *name, qint64 *stepMs = nullptr)
{
    for (const Missions::StartUpStep &s : steps)
        if (s.name == QLatin1String(name)) { if (stepMs) *stepMs = s.stepMs; return s.ms; }
    return -1;
}
}  // namespace

TEST_SUITE(session219)
{
    MessageDispatcher disp;
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_105200.cap"));
    CHECK(f.open(QIODevice::ReadOnly), "fixture: the excerpt");
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3 || !l.startsWith('@')) continue;
        disp.ingestLocal(81, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
    }
    disp.drainNow();
    LogModel *model = disp.modelForKey(QStringLiteral("81_1"));
    CHECK(model && model->count() > 4000, "fixture: the tab's rows");
    if (!model) return;
    const QVector<Missions::Mission> ms = Missions::split(model);
    CHECK(ms.size() == 4, "the stretch before, and three missions");
    if (ms.size() != 4) return;

    // ---- mission 2 ----
    const QVector<Missions::StartUpStep> s2 = Missions::startUpSteps(ms[2]);
    qint64 step = 0;
    CHECK(ms[2].firstTagMs == at219("11:01:04"), "mission 2: the first tag read at 11:01:04");
    CHECK(stepAt(s2, "Self test starts") == 0 && stepAt(s2, "Self test ends") == 0,
          "mission 2: the self test at 11:08:45, after Staff_Responsible, is not a start-up step");
    CHECK(stepAt(s2, "Train config starts", &step) == at219("10:56:34") && step == 5000, "train config starts 5 s after the start");
    CHECK(stepAt(s2, "Train config ends", &step) == at219("11:00:48") && step == 254000, "train config ends 4 min 14 s later");
    CHECK(stepAt(s2, "Staff_Responsible", &step) == at219("11:00:50") && step == 2000, "Staff_Responsible 2 s after it");
    CHECK(stepAt(s2, "First tag", &step) == at219("11:01:04") && step == 14000, "the first tag 14 s after that");
    CHECK(stepAt(s2, "Supervised") == 0, "mission 2 never supervised");
    CHECK(Missions::slowestStep(s2) == QLatin1String("Train config ends"), "mission 2: train config is the slow step");

    // ---- mission 3 ----
    const QVector<Missions::StartUpStep> s3 = Missions::startUpSteps(ms[3]);
    CHECK(stepAt(s3, "Staff_Responsible", &step) == at219("11:09:10") && step == 14000, "mission 3: Staff_Responsible after 14 s");
    CHECK(stepAt(s3, "First tag", &step) == at219("11:09:15") && step == 5000, "mission 3: first tag 5 s later");
    CHECK(stepAt(s3, "Supervised", &step) == at219("11:12:32") && step == 197000, "mission 3: On_Sight 3 min 17 s after the tag");
    CHECK(Missions::slowestStep(s3) == QLatin1String("Supervised"), "mission 3: reaching supervision is the slow step");
    CHECK(Missions::slowestStep(Missions::startUpSteps(ms[1])).isEmpty(), "mission 1 (no @dmi, no SR): no step");

    // ---- the rule on its own: same instant, out of order ----
    Missions::Mission m;
    m.index = 1;
    m.fromMs = 1000;
    m.trainConfigFromMs = 2000;
    m.trainConfigToMs = 9000;                      // repeated after SR: cut at SR
    m.firstSrMs = 5000;
    m.firstTagMs = 5000;                           // same instant as SR: 0 s after it
    m.selfTestFromMs = 6000;                       // after SR: dropped
    m.selfTestToMs = 7000;
    const QVector<Missions::StartUpStep> sx = Missions::startUpSteps(m);
    CHECK(stepAt(sx, "Train config ends", &step) == 5000 && step == 3000, "a phase still shown after SR ends at SR");
    CHECK(stepAt(sx, "First tag", &step) == 5000 && step == 0, "a tag at the SR instant: 0 s after it");
    CHECK(stepAt(sx, "Self test starts") == 0, "a self test after SR is left out");

    // ---- the report ----
    const QString html = Missions::toHtml(ms, QStringLiteral("81_1"), QString());
    CHECK(html.contains(QStringLiteral("Start-up, step by step")) && html.contains(QStringLiteral("<b>4 min 14 s"))
              && html.contains(QStringLiteral("<b>3 min 17 s")),
          "the report's table, the slow step of missions 2 and 3 in bold");

    // ---- the fleet summary ----
    const Fleet::Row r = Fleet::summariseModel(model, QStringLiteral("81_1"));
    CHECK(r.startUps == 2 && r.startUpMedianMs == (261000 + 14000) / 2,
          QByteArray("two start-ups (4 min 21 s, 14 s), their median (") + QByteArray::number(r.startUpMedianMs) + ")");
    CHECK(r.slowestSteps == QStringLiteral("Supervised ×1; Train config ends ×1"), r.slowestSteps.toUtf8());
    const QString csv = Fleet::toCsv({ r });
    CHECK(csv.contains(QStringLiteral(",startups,startup_median_s,slowest_steps,error\n"))
              && csv.contains(QStringLiteral(",2,137,Supervised ×1; Train config ends ×1,")),
          "the CSV carries them");
}
