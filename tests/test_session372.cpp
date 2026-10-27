#include "testutil.h"

#include "colorrules.h"
#include "faulttimelinewindow.h"
#include "iotimelinewindow.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "namemap.h"
#include "odometrywindow.h"
#include "radiohealthwindow.h"
#include "runreportwindow.h"
#include "sessionfile.h"
#include "sessionwindow.h"
#include "speeddistance.h"
#include "theme.h"
#include "trackdiagramwindow.h"
#include "uistyle.h"

#include <QAction>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QMenu>
#include <QMenuBar>
#include <QRegularExpression>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>

// =============================================================================
//  Session 372 — the Monitor tools that read one tab, over a recording: Run
//  summary, Mission report, Speed vs distance, Radio and GPS health,
//  Odometry at the tags, Fault timeline, Cab inputs and outputs, Track
//  diagram. Real frames: replay/loco_1_1_27062026_140226.cap to 14:06,
//  written to a .dlr the way Save writes it. Each tool over the recording
//  says what the same tool says over the live tab.
// =============================================================================

namespace {

QString writeDlr372(const QString &path, LogModel *m)
{
    QFile f(path);
    if (!m || !f.open(QIODevice::WriteOnly)) return QString();
    SessionFile::FileHeader h;
    h.createdMs = 1782558000000LL;
    h.sourceId = 21;
    h.kvchId = 1;
    f.write(SessionFile::encodeHeader(h));
    for (int r = 0; r < m->rowCount(); ++r) {
        const LogEntryPtr e = m->entryAt(r);
        if (e && !e->rawBytes.isEmpty()) f.write(SessionFile::encodeRecord(e->epochMs, e->rawBytes));
    }
    return path;
}

// The report without the line that says when it was made.
QString undated(const QString &html)
{
    QString h = html;
    h.remove(QRegularExpression(QStringLiteral("generated [^<]*")));
    return h;
}

}  // namespace

TEST_SUITE(session372)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    MessageDispatcher disp;
    {
        QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
        CHECK(f.open(QIODevice::ReadOnly), "fixture: the capture opens");
        const qint64 end = QDateTime::fromString(QStringLiteral("2026-06-27T14:06:00"), Qt::ISODate).toMSecsSinceEpoch();
        while (!f.atEnd()) {
            const QByteArray l = f.readLine().trimmed();
            const QList<QByteArray> tok = l.split(' ');
            if (tok.size() < 3 || !l.startsWith('@')) continue;
            const qint64 ms = QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch();
            if (ms >= end) break;
            disp.ingestLocal(21, 1, l, ms, QString());
        }
        disp.drainNow();
    }
    LogModel *live = disp.modelForKey(QStringLiteral("21_1"));
    CHECK(live && live->count() > 1000, "fixture: the live tab");
    if (!live) return;
    QTemporaryDir tmp;
    const QString dlr = writeDlr372(tmp.filePath(QStringLiteral("21_1.dlr")), live);

    ColorRules rules;
    NameMap names;
    SessionWindow w(&rules, &names, Theme::Light);
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    CHECK(w.loadFiles({ dlr }) == live->count(), "every record loads");
    w.show();
    for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();

    // ---- the menu -----------------------------------------------------------------------------
    QStringList names372;
    for (QAction *m : w.menuBar()->actions())
        if (m->menu())
            for (QAction *a : m->menu()->actions())
                if (a->objectName().startsWith(QLatin1String("session"))) names372 << a->objectName();
    for (const char *n : { "sessionRunReport", "sessionMissionReport", "sessionSpeedDistance", "sessionRadioHealth",
                           "sessionOdometry", "sessionFaultTimeline", "sessionCabIo", "sessionTrackDiagram" })
        CHECK(names372.contains(QLatin1String(n)), QByteArray("Tools has ") + n);

    using T = SessionWindow::ReviewTool;
    // ---- each tool, over the recording, says what it says live -------------------------------
    {
        auto *rec = qobject_cast<RunReportWindow *>(w.openReviewTool(T::RunReport));
        RunReportWindow liveW(live, QStringLiteral("21_1"), QStringLiteral("21_1"));
        CHECK(rec && rec->parent() == &w && rec->kind() == RunReportWindow::Kind::Run
                  && rec->windowTitle().endsWith(QStringLiteral("— recording")),
              "Run summary report: opens over the recording, titled so, owned by the window: "
                  + (rec ? rec->windowTitle().toUtf8() + (rec->parent() == &w ? " (owned)" : " (not owned)") : QByteArray("none")));
        CHECK(rec && !rec->html().isEmpty() && undated(rec->html()) == undated(liveW.html()),
              "and reports what the live tab's report does");
        delete rec;
    }
    {
        auto *rec = qobject_cast<RunReportWindow *>(w.openReviewTool(T::MissionReport));
        RunReportWindow liveW(live, QStringLiteral("21_1"), QStringLiteral("21_1"), nullptr, RunReportWindow::Kind::Missions);
        CHECK(rec && rec->kind() == RunReportWindow::Kind::Missions && undated(rec->html()) == undated(liveW.html()),
              "Mission report: the same as live");
        delete rec;
    }
    {
        auto *rec = qobject_cast<RadioHealthWindow *>(w.openReviewTool(T::RadioHealth));
        RadioHealthWindow liveW(live, QStringLiteral("21_1"), QStringLiteral("21_1"));
        CHECK(rec && !rec->summary().isEmpty() && rec->summary() == liveW.summary()
                  && rec->linksSummary() == liveW.linksSummary(),
              "Radio and GPS health: the same as live: " + (rec ? rec->summary().toUtf8() : QByteArray()));
        // "Show in log" lands on the recording's row.
        if (rec) {
            const qint64 target = live->entryAt(live->count() / 2)->epochMs;
            emit rec->jumpRequested(QStringLiteral("21_1"), target);
            QTableView *view = nullptr;
            for (QTableView *tv : w.findChildren<QTableView *>())
                if (tv->isVisible() && tv->model() && tv->model()->rowCount() == live->count()) view = tv;
            auto *proxy = view ? qobject_cast<QSortFilterProxyModel *>(view->model()) : nullptr;
            auto *model = proxy ? qobject_cast<LogModel *>(proxy->sourceModel()) : nullptr;
            CHECK(model && view->currentIndex().isValid()
                      && model->entryAt(proxy->mapToSource(view->currentIndex()).row())->epochMs == target,
                  "a jump from the tool lands on that moment in the recording");
        }
        delete rec;
    }
    {
        auto *rec = qobject_cast<OdometryWindow *>(w.openReviewTool(T::Odometry));
        OdometryWindow liveW(live, QStringLiteral("21_1"), QStringLiteral("21_1"));
        CHECK(rec && rec->summary() == liveW.summary() && rec->table()->rowCount() == liveW.table()->rowCount(),
              "Odometry at the tags: the same as live: " + (rec ? rec->summary().toUtf8() : QByteArray()));
        delete rec;
    }
    {
        auto *rec = qobject_cast<FaultTimelineWindow *>(w.openReviewTool(T::FaultTimeline));
        FaultTimelineWindow liveW(live, QStringLiteral("21_1"), QStringLiteral("21_1"));
        CHECK(rec && rec->failuresText() == liveW.failuresText(), "Fault timeline: the same as live");
        delete rec;
    }
    {
        QWidget *sd = w.openReviewTool(T::SpeedDistance);
        QWidget *io = w.openReviewTool(T::CabIo);
        QWidget *td = w.openReviewTool(T::TrackDiagram);
        CHECK(qobject_cast<SpeedDistanceWindow *>(sd) && qobject_cast<IoTimelineWindow *>(io)
                  && qobject_cast<TrackDiagramWindow *>(td),
              "Speed vs distance, Cab inputs and outputs and Track diagram open over it");
        delete sd;
        delete io;
        delete td;
    }

    // ---- an empty window has nothing to report on ----------------------------------------------
    {
        SessionWindow none(&rules, &names, Theme::Light);
        none.setAttribute(Qt::WA_DeleteOnClose, false);
        CHECK(none.openReviewTool(T::RunReport) == nullptr, "no recording loaded: no tool, and no crash");
    }
}
