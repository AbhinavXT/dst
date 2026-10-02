#include "testutil.h"
#include "layoutaudit.h"

#include "incidentreport.h"
#include "logmodel.h"
#include "runreport.h"
#include "runreportwindow.h"
#include "theme.h"
#include "uistyle.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QRegularExpression>

// =============================================================================
//  Session 143 — UI revamp, tool windows 20: the Run report.
//
//  The span and the per-packet counts sit side by side (fourteen packet rows
//  had run down a narrow column with the page's right side empty). The
//  headings count in English in the run report and the incident report
//  alike: "1 change", "3 episodes", "8 reads" (were "change(s)",
//  "episode(s)", "read(s)", "match(es)", "frame(s)", "sample(s)",
//  "comparison(s)"). Real run: replay/loco_1_1_26062026_162418.cap.
// =============================================================================

TEST_SUITE(session143)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    LogModel run(nullptr, 200000);
    {
        QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_26062026_162418.cap"));
        QVector<LogEntryPtr> v;
        if (f.open(QIODevice::ReadOnly))
            while (!f.atEnd()) {
                const QString l = QString::fromUtf8(f.readLine()).trimmed();
                if (!l.startsWith(QLatin1Char('@'))) continue;
                auto e = QSharedPointer<LogEntry>::create();
                e->text = l;
                e->epochMs = QDateTime::fromString(l.section(QLatin1Char(' '), 1, 1), Qt::ISODate).toMSecsSinceEpoch();
                e->cacheDerived();
                v << e;
            }
        run.appendEntries(v);
    }
    CHECK(run.count() == 9940, "fixture: the whole real run");

    const QRegularExpression maybePlural(QStringLiteral("\\w\\((s|es)\\)"));
    const RunReport::Summary s = RunReport::summarise(&run, QStringLiteral("21_1"), QStringLiteral("L1_V1"));
    const QString html = RunReport::toHtml(s);
    CHECK(!maybePlural.match(html).hasMatch(),
          QByteArray("the run report has no \"(s)\" counts (") + maybePlural.match(html).captured(0).toUtf8() + ")");
    CHECK(html.contains(QLatin1String("Loco mode (LSRP): 1 change</h2>")), "\"1 change\", singular");
    CHECK(html.contains(QLatin1String("<table class=\"side\"><tr><td valign=\"top\"><table>")),
          "the span and the packet counts sit side by side");

    const qint64 at = QDateTime::fromString(QStringLiteral("2026-06-26T16:28:00"), Qt::ISODate).toMSecsSinceEpoch();
    const QString incident = IncidentReport::toHtml(IncidentReport::build(&run, QStringLiteral("21_1"), QStringLiteral("L1_V1"), at));
    CHECK(!maybePlural.match(incident).hasMatch(),
          QByteArray("the incident report has none either (") + maybePlural.match(incident).captured(0).toUtf8() + ")");

    RunReportWindow w(&run, QStringLiteral("21_1"), QStringLiteral("L1_V1"));
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    w.resize(1000, 700);
    w.show();
    for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
    CHECK(w.minimumSizeHint().height() <= 700 && w.minimumSizeHint().width() <= 1366, "fits a laptop");
    const QStringList loose = LayoutAudit::orphans(&w);
    CHECK(loose.isEmpty(), QByteArray("no visible widget outside every layout (") + loose.join(QLatin1String(", ")).toUtf8() + ")");
}
