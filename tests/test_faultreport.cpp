#include "testutil.h"
#include "faultpanelwindow.h"

#include <QDateTime>

using Fault = FaultPanelWindow::Fault;

static Fault mk(const char *sub, const char *mod, const char *name,
                bool active, qint64 first, qint64 last,
                int s = 1, int m = 2, int f = 3) {
    Fault x;
    x.subsystemName = sub; x.moduleName = mod; x.faultName = name;
    x.active = active; x.firstMs = first; x.lastMs = last;
    x.subsystem = s; x.moduleId = m; x.faultId = f;
    return x;
}

// The fault report. It goes into an incident record, so the properties that
// matter are that it is self-contained, that nothing in it can be
// misread as markup, and that its timestamps are unambiguous.
TEST_SUITE(faultreport)
{
    const qint64 now = QDateTime(QDate(2026,8,8), QTime(14,0,0), Qt::UTC)
                           .toMSecsSinceEpoch();

    // ---- empty ------------------------------------------------------------
    {
        const QString h = FaultPanelWindow::buildReportHtml({}, "Report", now);
        CHECK(h.startsWith("<!DOCTYPE html>"), "well-formed document");
        CHECK(h.contains("</html>"),           "closed");
        CHECK(h.contains("No faults"),         "says so rather than showing a blank table");
        CHECK(!h.contains("<table>"),          "and omits the empty table entirely");
    }

    // ---- content ----------------------------------------------------------
    {
        QVector<Fault> v{
            mk("VCC", "Radio", "Link timeout", true,  now - 60000, now - 1000),
            mk("Loco", "Brake", "Sensor fault", false, now - 300000, now - 200000),
        };
        const QString h = FaultPanelWindow::buildReportHtml(v, "Kavach fault report", now);

        CHECK(h.contains("Kavach fault report"), "title present");
        CHECK(h.contains("2 fault(s)"),          "total counted");
        CHECK(h.contains("1 currently active"),  "active counted separately");
        CHECK(h.contains("Link timeout"),        "fault name present");
        CHECK(h.contains("Sensor fault"),        "resolved fault still listed");
        CHECK(h.contains("ACTIVE"),              "active state marked");
        CHECK(h.contains("resolved"),            "resolved state marked");
        CHECK(h.contains("1:2:3"),               "numeric code present for lookup");

        // Timestamps must be unambiguous: this gets correlated against log
        // files that record UTC.
        CHECK(h.contains("Z</td>") || h.contains("Z<"), "timestamps carry a zone marker");
        CHECK(h.contains("2026-08-08"), "ISO-style date");

        // Self-contained: an emailed or archived report has no stylesheet
        // to link to.
        CHECK(h.contains("<style>"), "styles are inline");
        CHECK(!h.contains("<link"),  "no external stylesheet reference");
        CHECK(h.contains("charset='utf-8'"), "declares its encoding");
    }

    // ---- escaping ---------------------------------------------------------
    // Fault and module names come from the schema. A name containing < or &
    // must not become markup, or the report silently loses content — or
    // worse, renders as something else entirely.
    {
        QVector<Fault> v{ mk("<script>alert(1)</script>",
                             "A & B",
                             "value < 5 \"quoted\"",
                             true, now, now) };
        const QString h = FaultPanelWindow::buildReportHtml(v, "T", now);
        CHECK(!h.contains("<script>"), "angle brackets in data are escaped");
        CHECK(h.contains("&lt;script&gt;"), "escaped form present");
        CHECK(h.contains("A &amp; B"), "ampersand escaped");
        CHECK(h.contains("value &lt; 5"), "less-than escaped");
    }

    // Title is escaped too — it is passed in and could carry anything.
    {
        const QString h = FaultPanelWindow::buildReportHtml({}, "<b>x</b>", now);
        CHECK(!h.contains("<title><b>"), "title is escaped");
        CHECK(h.contains("&lt;b&gt;"),   "escaped title present");
    }

    // ---- missing timestamps ------------------------------------------------
    {
        QVector<Fault> v{ mk("S", "M", "F", true, 0, 0) };
        const QString h = FaultPanelWindow::buildReportHtml(v, "T", now);
        CHECK(h.contains("—"), "absent timestamps render as a dash, not 1970");
        CHECK(!h.contains("1970"), "no epoch-zero dates leak into the report");
    }

    // ---- row count matches input ------------------------------------------
    {
        QVector<Fault> v;
        for (int i = 0; i < 7; ++i) {
            v.append(mk("S", "M", "F", i % 2 == 0, now, now));
        }
        const QString h = FaultPanelWindow::buildReportHtml(v, "T", now);
        CHECK(h.count("<tr") == 8, "seven data rows plus the header row");
        CHECK(h.contains("7 fault(s)"), "count matches");
        CHECK(h.contains("4 currently active"), "active count matches");
    }
}
