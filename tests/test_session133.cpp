#include "testutil.h"
#include "layoutaudit.h"

#include "incidentreport.h"
#include "incidentreportdialog.h"
#include "incidentreportwindow.h"
#include "logmodel.h"
#include "statusline.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFile>
#include <QImage>
#include <QPushButton>
#include <QTextBrowser>
#include <QTextDocument>

// =============================================================================
//  Session 133 — UI revamp, tool windows 9: the Incident report.
//
//  The window showed every DMI panel and the speed plot as blank space: the
//  report embeds them as data: URIs, which QTextBrowser does not load. The
//  viewer now gets them as document resources; the saved file is unchanged.
//  The plot leaves 0 m (not localised) frames out (Abhinav, 2026-10-02), so
//  its axis is the incident's stretch, not 0..160 km. Moments two to a row.
//  Real capture: replay/loco_1_1_27062026_151052.cap, around its rear-end
//  moment, 15:13:10.
// =============================================================================

namespace {

void loadCapture(LogModel *m, const QString &name)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/") + name);
    if (!f.open(QIODevice::ReadOnly)) return;
    QVector<LogEntryPtr> v;
    while (!f.atEnd()) {
        const QString l = QString::fromUtf8(f.readLine()).trimmed();
        if (!l.startsWith(QLatin1Char('@'))) continue;
        auto e = QSharedPointer<LogEntry>::create();
        e->text = l;
        e->epochMs = QDateTime::fromString(l.section(QLatin1Char(' '), 1, 1), Qt::ISODate).toMSecsSinceEpoch();
        e->cacheDerived();
        v << e;
    }
    m->appendEntries(v);
}

}  // namespace

TEST_SUITE(session133)
{
    LogModel run(nullptr, 200000);
    loadCapture(&run, QStringLiteral("loco_1_1_27062026_151052.cap"));
    const qint64 at = QDateTime::fromString(QStringLiteral("2026-06-27T15:13:10"), Qt::ISODate).toMSecsSinceEpoch();
    CHECK(run.count() > 0, "fixture: a real run");

    // ---- the plot leaves 0 m out --------------------------------------------------------------
    const IncidentReport::Summary s = IncidentReport::build(&run, QStringLiteral("21_1"), QStringLiteral("L1_V1"), at);
    CHECK(s.valid && s.speedTrace.samples.size() == 81, "the window's 81 speed samples, all kept in speedTrace");
    CHECK(s.plotLeftOut == 56, QByteArray("56 of them at 0 m, left out of the plot (")
                                   + QByteArray::number(s.plotLeftOut) + ")");
    CHECK(s.plotTrace.samples.size() == 25 && s.plotTrace.minLocM > 160000.0 && s.plotTrace.maxLocM < 161000.0,
          QByteArray("the plot spans the incident's stretch (") + QByteArray::number(s.plotTrace.minLocM, 'f', 0)
              + ".." + QByteArray::number(s.plotTrace.maxLocM, 'f', 0) + " m), not 0..160 km");
    bool targetsReal = true;
    for (const SpeedDistance::Target &t : s.plotTrace.targets) targetsReal = targetsReal && t.locM > 100000.0;
    CHECK(targetsReal, "no target placed relative to a 0 m frame");
    CHECK(!s.speedPlotPng.isEmpty(), "the plot is rendered");

    const QString html = IncidentReport::toHtml(s);
    CHECK(html.contains(QLatin1String("56 of 81 frames report 0 m (not localised on an RFID tag) and are left out of the plot")),
          "the report says what the plot leaves out");
    CHECK(html.contains(QLatin1String("Highest: 100 km/h")), "the highest speed still counts every frame");
    CHECK(html.count(QLatin1String("data:image/png;base64,")) == 4, "the saved file embeds 3 DMI panels and the plot");
    CHECK(html.count(QLatin1String("width=\"440\"")) == 3 && html.contains(QLatin1String("width=\"760\"")),
          "each image carries a width the viewer honours");
    CHECK(html.contains(QLatin1String("<table class=\"moments\"><tr><td valign=\"top\">"))
              && html.count(QLatin1String("<td valign=\"top\">")) == 3,
          "the moments sit two to a row, tops aligned");

    // ---- the viewer gets the images ---------------------------------------------------------------
    {
        QTextDocument doc;
        const QString shown = IncidentReportWindow::forViewer(html, &doc);
        CHECK(!shown.contains(QLatin1String("data:image")), "no data: URI is left for the viewer to fail on");
        const QImage first = doc.resource(QTextDocument::ImageResource, QUrl(QStringLiteral("dlimg://0"))).value<QImage>();
        const QImage plot = doc.resource(QTextDocument::ImageResource, QUrl(QStringLiteral("dlimg://3"))).value<QImage>();
        CHECK(!first.isNull() && first.width() >= 400, "the first DMI panel is a real image in the document");
        CHECK(!plot.isNull() && plot.width() == 900, "and so is the plot");
        CHECK(QImage::fromData(s.keyMoments.first().dmiPng, "PNG") == first, "the same pixels as the panel rendered");
    }

    // ---- the window ------------------------------------------------------------------------------------
    {
        IncidentReportWindow w(&run, QStringLiteral("21_1"), QStringLiteral("L1_V1"), at, IncidentReport::Options());
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.resize(1000, 720);
        w.show();
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        auto *view = w.findChild<QTextBrowser *>();
        CHECK(view && !view->toHtml().contains(QLatin1String("data:image")), "the window shows resources, not data: URIs");
        CHECK(w.html().count(QLatin1String("data:image/png;base64,")) == 4, "what Save HTML writes still embeds them");
        auto *status = w.findChild<StatusLine *>();
        CHECK(status && status->text() == QStringLiteral("918 raw frames · 3 DMI moments"),
              QByteArray("the status reads as English (") + (status ? status->text().toUtf8() : QByteArray()) + ")");
        CHECK(w.minimumSizeHint().height() <= 700 && w.minimumSizeHint().width() <= 1366,
              QByteArray("fits a 1366 x 768 laptop (minimum ")
                  + QByteArray::number(w.minimumSizeHint().width()) + " x "
                  + QByteArray::number(w.minimumSizeHint().height()) + ")");
        const QStringList loose = LayoutAudit::orphans(&w);
        CHECK(loose.isEmpty(), QByteArray("no visible widget outside every layout (")
                                   + loose.join(QLatin1String(", ")).toUtf8() + ")");
    }

    // ---- the dialog --------------------------------------------------------------------------------------
    {
        IncidentReportDialog dlg(at, at - 60000, at + 20000);
        auto *box = dlg.findChild<QDialogButtonBox *>();
        CHECK(box && box->button(QDialogButtonBox::Ok)->text() == QStringLiteral("Build report"),
              "the dialog's button says what it does");
        CHECK(dlg.minimumSizeHint().height() <= 700, "the dialog fits");
    }
}
