#include "testutil.h"

#include "incidentreport.h"
#include "laneband.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "missionreport.h"
#include "runreport.h"
#include "theme.h"
#include "uistyle.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QVBoxLayout>

// =============================================================================
//  Session 175 — brake reasons beside every EB / FSB.
//  Real frames: replay/2026-10-08/loco_1_1_08102026_105200.cap (81_1).
//  Expected, from schema/engine.py: five EMERGENCY_BRAKE spells on the DMI,
//    10:52:36-10:52:40  DMI alarm "System Fault, Isolate or Restart KAVACH";
//                       NMS BRAKE_APPLICATION_REASON 3 (Overspeed) at 10:52:36/38
//    11:04:09-36, 11:04:40-11:05:08, 11:05:12-38, 11:05:42-11:06:08
//                       DMI context "SOS - Other Loco Manual", collision_loco_id 2
//                       (loco 2's SoS, session 172); no NMS reason near them.
// =============================================================================

namespace {
qint64 at175(const char *hms)
{
    return QDateTime::fromString(QStringLiteral("2026-10-08T") + QLatin1String(hms), Qt::ISODate).toMSecsSinceEpoch();
}
}  // namespace

TEST_SUITE(session175)
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

    const QVector<RunReport::BrakeEvent> b = RunReport::brakeEvents(model);
    CHECK(b.size() == 5, QByteArray("five EB spells (") + QByteArray::number(b.size()) + ")");
    if (b.size() != 5) return;
    CHECK(b[0].fromMs == at175("10:52:36") && b[0].toMs == at175("10:52:40") && b[0].type.startsWith(QStringLiteral("4")),
          "the first: 10:52:36 to 10:52:40, EMERGENCY_BRAKE");
    CHECK(b[0].reasons.contains(QStringLiteral("DMI alarm: System Fault, Isolate or Restart KAVACH"))
              && b[0].reasons.contains(QStringLiteral("NMS: 3  (Overspeed)")),
          QByteArray("with the DMI's alarm and the NMS reason: ") + b[0].reasonsText().toUtf8());
    const char *const from[4] = { "11:04:09", "11:04:40", "11:05:12", "11:05:42" };
    bool sos = true;
    for (int i = 1; i < 5; ++i) {
        sos &= b[i].fromMs == at175(from[i - 1]) && b[i].reasons.contains(QStringLiteral("DMI: SOS - Other Loco Manual"))
               && b[i].reasons.contains(QStringLiteral("DMI: collision target loco 2"));
        for (const QString &r : b[i].reasons) sos &= !r.startsWith(QLatin1String("NMS"));
    }
    CHECK(sos, "the four at 11:04-11:06: loco 2's manual SoS, collision target loco 2; no NMS reason");

    // Everywhere brakes are listed.
    const RunReport::Summary run = RunReport::summarise(model, QStringLiteral("81_1"), QString());
    CHECK(run.brakes.size() == 5 && RunReport::toHtml(run).contains(QStringLiteral("SOS - Other Loco Manual"))
              && RunReport::toHtml(run).contains(QStringLiteral("Reasons in the capture")),
          "the run summary lists them, with reasons");
    const IncidentReport::Summary inc = IncidentReport::build(model, QStringLiteral("81_1"), QString(), at175("11:05:00"));
    CHECK(inc.brakeEpisodes.size() == inc.run.brakes.size() && inc.brakeEpisodes.size() >= 3
              && IncidentReport::toHtml(inc).contains(QStringLiteral("DMI: collision target loco 2")),
          "the incident report around 11:05 lists the EBs with their reasons");
    const QVector<Missions::Mission> ms = Missions::split(model);
    CHECK(ms.size() == 4 && ms[0].brakes.size() == 1 && ms[2].brakes.size() == 4 && ms[1].brakes.isEmpty() && ms[3].brakes.isEmpty(),
          "each mission holds its own: one before the first start, four in mission 2");
    CHECK(Missions::toHtml(ms, QStringLiteral("81_1"), QString()).contains(QStringLiteral("NMS: 3  (Overspeed)")),
          "the mission report shows the reasons");

    // Lanes: the Safety lane names them on hover.
    QWidget host;
    auto *lay = new QVBoxLayout(&host);
    auto *band = new LaneBand;
    lay->addWidget(band);
    host.resize(1200, 300);
    host.show();
    band->setModel(model, QStringLiteral("81_1"), QStringLiteral("81_1"));
    QCoreApplication::processEvents();
    CHECK(band->summary().brakes.size() == 4, "the band shows the newest 15 minutes (10:58 on): the four at 11:04-11:06");
    const QRect r = band->laneRect(1);
    const double t = double(at175("11:04:20") - band->fromMs()) / double(qMax<qint64>(1, band->toMs() - band->fromMs()));
    const QString tip = band->describeAt(QPoint(r.left() + int(t * r.width()), r.center().y()));
    CHECK(tip.contains(QStringLiteral("EMERGENCY_BRAKE")) && tip.contains(QStringLiteral("SOS - Other Loco Manual")),
          QByteArray("hover the Safety lane at 11:04:20: ") + tip.toUtf8());
}
