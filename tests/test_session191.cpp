#include "testutil.h"

#include "logmodel.h"
#include "messagedispatcher.h"
#include "missionreport.h"
#include "theme.h"
#include "uistyle.h"

#include <QDateTime>
#include <QFile>

// =============================================================================
//  Session 191 — the mission report shows the DMI at each mission's key moments.
//  Real frames: replay/2026-10-08/loco_1_1_08102026_105200.cap (session 171's
//  fixture: the stretch before, then missions 1-3; mission 2 has @dmi and
//  reaches Staff_Responsible at 11:00:50; mission 3 has no @dmi at all).
// =============================================================================

namespace {
qint64 at191(const char *hms)
{
    return QDateTime::fromString(QStringLiteral("2026-10-08T") + QLatin1String(hms), Qt::ISODate).toMSecsSinceEpoch();
}
}  // namespace

TEST_SUITE(session191)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    MessageDispatcher d;
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_105200.cap"));
    CHECK(f.open(QIODevice::ReadOnly), "fixture: 81_1, 10:52-11:13");
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3 || !l.startsWith('@')) continue;
        d.ingestLocal(81, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
    }
    d.drainNow();
    LogModel *model = d.modelForKey(QStringLiteral("81_1"));
    if (!model) return;

    QVector<Missions::Mission> ms = Missions::split(model);
    CHECK(ms.size() == 4, "the stretch before and three missions");
    if (ms.size() != 4) return;
    CHECK(ms.at(2).dmiMoments.isEmpty(), "no panels until addDmiMoments runs (split stays cheap for the timelines)");
    Missions::addDmiMoments(model, &ms);

    // ---- mission 2: has @dmi --------------------------------------------------------------
    const Missions::Mission &m2 = ms.at(2);
    CHECK(!m2.dmiMoments.isEmpty() && m2.dmiMoments.size() <= 8, "mission 2: up to 8 key moments");
    bool startFirst = !m2.dmiMoments.isEmpty() && m2.dmiMoments.first().label == QLatin1String("Start of mission")
                      && m2.dmiMoments.first().ms == at191("10:56:29");
    CHECK(startFirst, "the first is the start of mission, 10:56:29");
    const IncidentReport::KeyMoment *sr = nullptr;
    for (const IncidentReport::KeyMoment &km : m2.dmiMoments)
        if (km.label == QLatin1String("First Staff_Responsible")) sr = &km;
    CHECK(sr && sr->ms == at191("11:00:50") && sr->hasDmi && !sr->dmiPng.isEmpty() && sr->dmiFrameMs <= sr->ms
              && sr->dmiFrameMs >= m2.fromMs,
          "the first Staff_Responsible, 11:00:50: the DMI rendered from this mission's own frame");
    bool ordered = true;
    for (int i = 1; i < m2.dmiMoments.size(); ++i) ordered = ordered && m2.dmiMoments.at(i - 1).ms < m2.dmiMoments.at(i).ms;
    CHECK(ordered, "in time order, one panel per instant");

    // ---- mission 3: no @dmi of its own: mission 2's last screen is NOT shown ---------------
    const Missions::Mission &m3 = ms.at(3);
    bool anyDmi = false;
    for (const IncidentReport::KeyMoment &km : m3.dmiMoments) anyDmi = anyDmi || km.hasDmi;
    CHECK(!m3.dmiMoments.isEmpty() && !anyDmi, "mission 3: key moments listed, but no panel (its DMI never sent)");

    // ---- the HTML ------------------------------------------------------------------------------
    const QString html = Missions::toHtml(ms, QStringLiteral("81_1"), QStringLiteral("81_1"));
    CHECK(html.contains(QStringLiteral("<h3>DMI at key moments (%1)</h3>").arg(m2.dmiMoments.size())),
          "each mission has its DMI section");
    CHECK(html.count(QLatin1String("src=\"data:image/png;base64,")) >= 2, "the panels are embedded");
    CHECK(html.contains(QStringLiteral("<b>First Staff_Responsible</b> \u2014 11:00:50")), "labelled with the moment");
    CHECK(html.contains(QLatin1String("No @dmi in this mission at or before this time.")), "and mission 3 says why it has none");
}
