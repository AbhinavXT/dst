#include "testutil.h"

#include "logmodel.h"
#include "messagedispatcher.h"
#include "missionreport.h"
#include "runreportwindow.h"
#include "theme.h"
#include "uistyle.h"

#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>

// =============================================================================
//  Session 171 — missions and the mission report.
//  Real frames: replay/2026-10-08/loco_1_1_08102026_105200.cap, Abhinav's 81_1 capture
//  of 2026-10-08 from 10:52:00 to 11:13:00 (loco time), without its DIO,
//  analog and LINFO lines. Expected values from schema/engine.py, with the
//  report's rule (the DMI's mode; the ARP's while the DMI is silent > 3 s):
//    before   10:52:00  Limited_Supervision, Stand_By, System_Failure
//                       10:52:36; self test from 10:52:21; one EB
//    mission 1 10:52:51 no @dmi; ARP: Stand_By, System_Failure 10:56:27
//    mission 2 10:56:29 train config from 10:56:34, Staff_Responsible
//                       11:00:50; 15 tags; 4 EB; 110 km/h; System_Failure
//                       11:08:52 (ARP, the DMI silent) just before the next
//    mission 3 11:08:56 no @dmi; ARP: Staff_Responsible 11:09:10, On_Sight
//                       11:12:32, Full_Supervision 11:12:54; tags 14 16 18 906
// =============================================================================

namespace {
qint64 at171(const char *hms)
{
    return QDateTime::fromString(QStringLiteral("2026-10-08T") + QLatin1String(hms), Qt::ISODate).toMSecsSinceEpoch();
}
}  // namespace

TEST_SUITE(session171)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
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
    CHECK(ms.size() == 4, QByteArray("the stretch before the first start, and three missions (") + QByteArray::number(ms.size()) + ")");
    if (ms.size() != 4) return;
    const Missions::Mission &m0 = ms[0], &m1 = ms[1], &m2 = ms[2], &m3 = ms[3];

    CHECK(m0.index == 0 && m0.fromMs == at171("10:52:00") && m0.toMs == at171("10:52:51"),
          "before: 10:52:00 to the first start of mission");
    CHECK(m0.modeSource == QLatin1String("DMI + ARP") && m0.systemFailure && m0.systemFailureMs == at171("10:52:36")
              && m0.lastMode == QLatin1String("12 (System_Failure)") && m0.eb.size() == 1 && m0.selfTestFromMs == at171("10:52:21"),
          "before: the DMI went to System_Failure at 10:52:36, after a self test and one EB");

    CHECK(m1.index == 1 && m1.fromMs == at171("10:52:51") && m1.toMs == at171("10:56:29"), "mission 1: 10:52:51 to the next start");
    CHECK(m1.dmiFrames == 0 && m1.modeSource == QLatin1String("ARP"), "no @dmi in it: modes from the ARP");
    CHECK(m1.afterStandby == QLatin1String("12 (System_Failure)") && m1.systemFailure && m1.systemFailureMs == at171("10:56:27"),
          "after Stand_By the ARP reported System_Failure, at 10:56:27");

    CHECK(m2.index == 2 && m2.fromMs == at171("10:56:29") && m2.toMs == at171("11:08:56"), "mission 2: 10:56:29 to 11:08:56");
    CHECK(m2.modeSource == QLatin1String("DMI + ARP") && m2.trainConfigFromMs == at171("10:56:34") && m2.firstSrMs == at171("11:00:50")
              && m2.startUpMs() == at171("11:00:50") - at171("10:56:29"),
          "train config from 10:56:34, Staff_Responsible at 11:00:50: start-up 4 min 21 s");
    CHECK(m2.tags == (QStringList{ "800", "802", "804", "2", "4", "6", "8", "10", "12", "14", "16", "18", "906", "898", "900" }),
          QByteArray("its 15 tags, in order (") + m2.tags.join(QLatin1Char(',')).toLatin1() + ")");
    CHECK(m2.eb.size() == 4 && m2.maxSpeedKmh == 110 && m2.systemFailure && m2.systemFailureMs == at171("11:08:52"),
          "4 EB episodes, 110 km/h; System_Failure at 11:08:52 from the ARP while the DMI was silent");
    CHECK(!m2.endsWithLog, "it ends at the next start of mission, not with the log");

    CHECK(m3.index == 3 && m3.fromMs == at171("11:08:56") && m3.endsWithLog && m3.modeSource == QLatin1String("ARP")
              && m3.afterStandby == QLatin1String("2 (Staff_Responsible)"),
          "mission 3: from 11:08:56 to the end of the log; ARP then Staff_Responsible");
    CHECK(m3.tags == (QStringList{ "14", "16", "18", "906" }), "its own tags");
    CHECK(m3.firstSrMs == at171("11:09:10") && m3.firstOsMs == at171("11:12:32") && m3.firstFsMs == at171("11:12:54")
              && m3.highestMode == QLatin1String("4 (Full_Supervision)") && m3.startUpMs() == 14000,
          "Staff_Responsible 14 s after the start, then On_Sight and Full_Supervision");

    // ---- the report -------------------------------------------------------------------------
    const QString html = Missions::toHtml(ms, QStringLiteral("81_1"), QStringLiteral("81_1"));
    CHECK(html.contains(QStringLiteral("<h2>3 mission(s)</h2>")) && html.contains(QStringLiteral("Mission 2 — 2026-10-08 10:56:29"))
              && html.contains(QStringLiteral("Before the first start of mission")) && html.contains(QStringLiteral("System_Failure"))
              && html.contains(QStringLiteral("4 min 21 s")) && html.contains(QStringLiteral("(log ends)"))
              && html.contains(QStringLiteral("No @dmi in this mission: the modes are the loco's own ARP LOCO_MODE"))
              && html.contains(QStringLiteral("while the DMI was silent")),
          "the full report: summary table, a section per mission, start-up time, the source of each mission's modes");
    CHECK(html.contains(QStringLiteral("OBSERVED")) && !html.contains(QStringLiteral("invalid")), "verdict-free wording");

    RunReportWindow w(model, QStringLiteral("81_1"), QStringLiteral("81_1"), nullptr, RunReportWindow::Kind::Missions);
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    CHECK(w.missions().size() == 4 && w.windowTitle().startsWith(QStringLiteral("Mission report")), "the window builds it");
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("missions.html"));
    CHECK(w.saveHtml(path) && QFile(path).size() > 4000, "and saves it as one HTML file");

    // Opt-in, not in the gate: a whole DLConsole log (DL_MISSION_LOG) to an
    // HTML report in DL_SHOTS, for reading by eye.
    const QString big = QString::fromLocal8Bit(qgetenv("DL_MISSION_LOG"));
    if (!big.isEmpty() && !qgetenv("DL_SHOTS").isEmpty()) {
        MessageDispatcher d2;
        QFile lf(big);
        if (lf.open(QIODevice::ReadOnly)) {
            while (!lf.atEnd()) {
                const QByteArray raw = lf.readLine();
                const int at = raw.indexOf('@');
                if (at < 0) continue;
                const QByteArray l = raw.mid(at).trimmed();
                const QList<QByteArray> tok = l.split(' ');
                if (tok.size() < 3) continue;
                d2.ingestLocal(81, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
            }
            d2.drainNow();
            if (LogModel *bm = d2.modelForKey(QStringLiteral("81_1"))) {
                QFile out(QString::fromLocal8Bit(qgetenv("DL_SHOTS")) + QStringLiteral("/missions_full.html"));
                if (out.open(QIODevice::WriteOnly))
                    out.write(Missions::toHtml(Missions::split(bm), QStringLiteral("81_1"), QStringLiteral("81_1")).toUtf8());
            }
        }
    }
}
