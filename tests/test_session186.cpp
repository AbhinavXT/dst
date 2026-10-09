#include "testutil.h"
#include "sosfixture.h"

#include "incidentreport.h"
#include "soslog.h"
#include "theme.h"
#include "twolocoview.h"
#include "twolocowindow.h"
#include "uistyle.h"

#include <QCoreApplication>

// =============================================================================
//  Session 186 — SoS, phase C: the two-loco view and the incident pack read @sos.
//  SYNTHETIC frames (tests/sosgen): loco 1 sees it all; loco 2 holds a manual
//  SoS 10:00:09-10:01:52 and tracks loco 1 as a quiet source.
// =============================================================================

using namespace SosFixture;

TEST_SUITE(session186)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    MessageDispatcher d;
    LogModel *m1 = load(d, 1);
    LogModel *m2 = load(d, 2);
    CHECK(m1 && m2, "fixtures: both locos' synthetic SoS logs");
    if (!m1 || !m2) return;

    // ==== the two-loco view ================================================================
    const TwoLocoView::Pair p = TwoLocoView::build(m1, QStringLiteral("1_1"), m2, QStringLiteral("2_1"));
    CHECK(p.a.source == QLatin1String("sos") && p.b.source == QLatin1String("sos"),
          "no @dmi or @lsrp in either log: both traces come from @sos");
    CHECK(p.a.samples.size() == 281 && p.b.samples.size() == 131, "one sample per 1 s snapshot (281 and 131)");
    CHECK(!p.a.samples.isEmpty() && qFuzzyCompare(p.a.samples.first().locM, 10000.0)
              && qFuzzyCompare(p.a.samples.first().speedKmh, 72.0),
          "loco 1 at 10 000 m, 72 km/h (sensor_speed 20, taken as m/s)");

    // Events from the firmware's own spells, not from @lsos.
    CHECK(p.eventsA.sos.size() == 5 && p.eventsA.headOn.size() == 1 && p.eventsA.rearEnd.size() == 1,
          QByteArray("loco 1: 5 SoS spells, 1 head-on, 1 rear-end (") + QByteArray::number(p.eventsA.sos.size()) + ", "
              + QByteArray::number(p.eventsA.headOn.size()) + ", " + QByteArray::number(p.eventsA.rearEnd.size()) + ")");
    if (!p.eventsA.sos.isEmpty())
        CHECK(p.eventsA.sos.first().what == QLatin1String("Manual SoS from loco 2") && p.eventsA.sos.first().fromMs == atSos("10:00:10"),
              "the first: loco 2's manual SoS, from 10:00:10");
    if (!p.eventsA.headOn.isEmpty())
        CHECK(p.eventsA.headOn.first().fromMs == atSos("10:02:30"), "the head-on, from 10:02:30");
    CHECK(p.eventsB.sos.isEmpty() && p.eventsB.headOn.isEmpty(), "loco 2 had no threat in its own table");

    // Each loco as the other's SoS table had it.
    CHECK(!p.bByA.isEmpty() && !p.aByB.isEmpty(), "both directions: loco 2 in loco 1's table, loco 1 in loco 2's");
    if (!p.bByA.isEmpty()) {
        const TwoLocoView::GapSample &g = p.bByA.first();
        CHECK(g.ms == atSos("10:00:02") && qFuzzyCompare(g.bLocM, 11010.0) && qFuzzyCompare(g.gapM, 11010.0 - 10040.0),
              "loco 1 first had loco 2 at 11 010 m, 970 m ahead, at 10:00:02");
        CHECK(p.bByA.last().ms <= atSos("10:02:15"), "and until it released it");
    }
    if (!p.aByB.isEmpty())
        CHECK(qFuzzyCompare(p.aByB.first().aLocM, 11010.0), "loco 2's view is from its own position, 11 010 m");
    CHECK(!p.gap.isEmpty() && p.plausible, "the gap from the two traces, and the pair reads as one section");

    {
        TwoLocoWindow w(&d);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.resize(1100, 680);
        w.setSources(QStringLiteral("1_1"), QStringLiteral("2_1"));
        w.show();
        for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
        CHECK(w.pair().bByA.size() == p.bByA.size(), "the window shows the same pair");
        CHECK(!w.canvas()->grab().isNull(), "and draws it");
    }

    // ==== the incident pack ==================================================================
    IncidentReport::Options o;
    o.beforeMs = 20000;
    o.afterMs = 20000;
    const IncidentReport::Summary s = IncidentReport::build(m1, QStringLiteral("1_1"), QString(), atSos("10:01:31"), o);
    QStringList labels;
    bool allSos = !s.keyMoments.isEmpty();
    for (const IncidentReport::KeyMoment &km : s.keyMoments) {
        labels << km.label;
        allSos = allSos && km.hasSos && !km.sosPng.isEmpty() && !km.sosLines.isEmpty();
    }
    CHECK(labels.contains(QStringLiteral("SoS: Unusual stop from loco 3 started"))
              && labels.contains(QStringLiteral("SoS: Emergency brake applied: Unusual stop (LB code 22), 1680 m")),
          QByteArray("the unusual stop and its brake are key moments: ") + labels.join(QLatin1String(" | ")).toUtf8());
    CHECK(allSos, "every key moment carries the SoS strip and the decision lines");
    const QString html = IncidentReport::toHtml(s, o);
    CHECK(html.contains(QLatin1String("<ul class=\"sos\">"))
              && html.contains(QLatin1String("<li>Target: loco 2, manual SoS, 0 m (0 m: target distance removed or source passed).</li>")),
          "the decision lines in the pack");
    CHECK(html.contains(QLatin1String("SoS threats (firmware, @sos): 2 threats"))
              && html.contains(QLatin1String("SoS brake decisions (firmware, @sosev): 1 decision")),
          "the window's threats and decisions listed");
}
