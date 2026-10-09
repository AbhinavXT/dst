#include "testutil.h"
#include "sosfixture.h"
#include "layoutaudit.h"

#include "soslog.h"
#include "sosstrip.h"
#include "soswindow.h"
#include "theme.h"
#include "uistyle.h"

#include <QCoreApplication>
#include <QLabel>
#include <QPixmap>

// =============================================================================
//  Session 185 — SoS, phase B: the track strip and what the loco is reacting to.
//  SYNTHETIC frames (tests/sosgen); loco 1's @linfo in them is a real frame
//  (2026-10-08: sos_trigger_distance 3000, collision_trigger_distance 1000).
// =============================================================================

using namespace SosFixture;

namespace {

QString hitWith(const SosStrip *s, const QString &needle)
{
    for (const SosStrip::Hit &h : s->hits())
        if (h.text.contains(needle)) return h.text;
    return QString();
}

}  // namespace

TEST_SUITE(session185)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    MessageDispatcher d;
    LogModel *m = load(d, 1);
    CHECK(m != nullptr, "fixture: loco 1's synthetic SoS log");
    if (!m) return;

    // ---- the trigger distances come from the log's @linfo ------------------------------
    const SosStrip::Config cfg = SosStrip::configFromLog(m);
    CHECK(cfg.sosTriggerM == 3000 && cfg.collisionTriggerM == 1000 && cfg.cancelM == 500,
          "sos 3000 m, collision 1000 m, cancel 500 m, from the real @linfo");

    SosWindow w(&d);
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    w.resize(1100, 680);
    w.show();
    for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
    SosStrip *strip = w.strip();
    CHECK(strip != nullptr && w.decision() != nullptr, "the window has the strip and the decision panel");
    if (!strip || !w.decision()) return;

    // ---- 10:01:40: passed loco 2 on TIN 102; loco 3 on TIN 103, adjusted --------------
    w.showMoment(atSos("10:01:40"));
    strip->grab();
    CHECK(strip->laneNames() == QStringList({ QStringLiteral("TIN 102"), QStringLiteral("TIN 101 (own)"), QStringLiteral("TIN 103") }),
          QByteArray("one lane per TIN, the own in the middle: ") + strip->laneNames().join(QLatin1String(", ")).toUtf8());
    CHECK(strip->legend().startsWith(QLatin1String("Shaded: SoS trigger 3000 m, collision trigger 1000 m (@linfo).")),
          QByteArray("the legend names the bands: ") + strip->legend().toUtf8());
    const QString l2 = hitWith(strip, QStringLiteral("Loco 2"));
    CHECK(l2.contains(QLatin1String("(the target)")) && l2.contains(QLatin1String("990 m from the own loco"))
              && l2.contains(QLatin1String("manual SoS")),
          QByteArray("loco 2 drawn, the target, 990 m: ") + l2.toUtf8());
    const QString ghost = hitWith(strip, QStringLiteral("as its ARP put it"));
    CHECK(ghost == QLatin1String("Loco 3 as its ARP put it: 13480 m nominal (before SOSWithAdjustment)"),
          QByteArray("loco 3's ARP position drawn dashed: ") + ghost.toUtf8());
    CHECK(hitWith(strip, QStringLiteral("Own loco:")).startsWith(QLatin1String("Own loco: 12000 m nominal, TIN 101")),
          "the own loco drawn");
    CHECK(strip->spanM() >= 6000, "the span takes in the SoS trigger band both ways");

    const QString dec = w.decision()->text();
    CHECK(dec.contains(QLatin1String("What the loco is reacting to"))
              && dec.contains(QLatin1String("Target: loco 2, manual SoS, 0 m (0 m: target distance removed or source passed).")),
          "the decision panel says what the target is");
    CHECK(dec.contains(QLatin1String("Own ARP broadcasts emergency status 0 (No Emergency).")), "and what the own ARP says");

    // ---- 10:02:32: head-on with loco 4, on the own line ----------------------------------
    w.showMoment(atSos("10:02:32"));
    strip->grab();
    CHECK(strip->laneNames().contains(QStringLiteral("TIN 101 (own)")) && !strip->laneNames().contains(QStringLiteral("TIN 101")),
          "loco 4 shares the own lane");
    CHECK(hitWith(strip, QStringLiteral("Loco 4")).contains(QLatin1String("(the target)")), "loco 4 is the target");
    CHECK(w.decision()->text().contains(QLatin1String("Target: loco 4, head-on, 920 m."))
              && w.decision()->text().contains(QLatin1String("Own ARP broadcasts emergency status 4 (Head-On Collision).")),
          "head-on, 920 m; the own ARP says 4");

    // ---- 10:03:50: the station is the target; its pin --------------------------------------
    w.showMoment(atSos("10:03:50"));
    strip->grab();
    CHECK(hitWith(strip, QStringLiteral("Station 4501")).startsWith(QLatin1String("Station 4501 at 16000 m, general SoS on")),
          "the station's pin");
    CHECK(w.decision()->text().contains(QLatin1String("Target: station 4501, station general SoS, 1800 m.")),
          "the station is the target");
    CHECK(w.decision()->text().contains(QLatin1String("Own ARP broadcasts emergency status 5 (Rear-End Collision).")),
          "the own ARP still says 5, long after the rear-end ended (bug #1 as scripted)");

    // ---- a log without @linfo says the bands are not drawn ---------------------------------
    {
        SosStrip s;
        s.setConfig(SosStrip::Config());
        CHECK(s.legend().startsWith(QLatin1String("No @linfo in this log")), "no @linfo: said, not guessed");
        s.setSnapshot(nullptr);
        s.resize(600, 180);
        s.grab();
        CHECK(s.hits().isEmpty(), "and nothing drawn without a snapshot");
    }

    // ---- fits --------------------------------------------------------------------------------
    CHECK(w.minimumSizeHint().width() <= 1100 && w.minimumSizeHint().height() <= 700,
          QByteArray("fits a laptop (minimum ") + QByteArray::number(w.minimumSizeHint().width()) + " x "
              + QByteArray::number(w.minimumSizeHint().height()) + ")");
    CHECK(strip->height() >= 150, QByteArray("the strip gets its height (") + QByteArray::number(strip->height()) + ")");
    const QStringList loose = LayoutAudit::orphans(&w);
    CHECK(loose.isEmpty(), QByteArray("no visible widget outside every layout (") + loose.join(QLatin1String(", ")).toUtf8() + ")");
}
