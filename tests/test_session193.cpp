#include "testutil.h"
#include "sosfixture.h"

#include "soslog.h"
#include "sosstrip.h"
#include "soswindow.h"
#include "theme.h"
#include "uistyle.h"

#include <QFile>

// =============================================================================
//  Session 193 — the review's findings on the SoS work (184-192), fixed.
//    1  SosStrip::configFromLog decodes only the newest @linfo (was all of
//       them: 2.9 s on a day's tab)
//    2  SosWindow::setSource does not rebuild the tab it already shows
//    3  minimal layout: a threat already on in the first snapshot is "already
//       on when the log starts", not "started"
//    4  minimal layout: DEST_LOCO_SOS has no station id: "from a station"
//    5  the lanes' collision colour is decided in soslog, beside the names
//  SYNTHETIC SoS frames (tests/sosgen); the @linfo in them is a real frame.
// =============================================================================

using namespace SosFixture;

namespace {

LogModel *loadMinimal(MessageDispatcher &d, int loco)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/schema/fixtures/sos_minimal_loco%1.log").arg(loco));
    if (!f.open(QIODevice::ReadOnly)) return nullptr;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3 || !l.startsWith('@')) continue;
        d.ingestLocal(quint8(loco), 1, l,
                      QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
    }
    d.drainNow();
    return d.modelForKey(QStringLiteral("%1_1").arg(loco));
}

}  // namespace

TEST_SUITE(session193)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    // ---- 1. the newest @linfo ---------------------------------------------------------------
    {
        MessageDispatcher d;
        LogModel *m = load(d, 1);
        CHECK(m != nullptr, "fixture: loco 1, full layout, with a real @linfo");
        if (!m) return;
        const SosStrip::Config c = SosStrip::configFromLog(m);
        CHECK(c.sosTriggerM == 3000 && c.collisionTriggerM == 1000 && c.cancelM == 500,
              "the trigger distances, read from the newest @linfo alone");
        MessageDispatcher none;
        none.ingestLocal(9, 1, QByteArray("@dmi_9_1 2026-10-09T10:00:00 1 00"), atSos("10:00:00"), QString());
        none.drainNow();
        CHECK(!SosStrip::configFromLog(none.modelForKey(QStringLiteral("9_1"))).known()
                  && !SosStrip::configFromLog(nullptr).known(),
              "no @linfo (or no tab): not known, not guessed");

        // ---- 2. the tab already shown is not read again ---------------------------------------
        SosWindow w(&d);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.showMoment(atSos("10:01:40"));
        const int before = w.currentSnapshot();
        w.setSource(QStringLiteral("1_1"));
        CHECK(before > 0 && w.currentSnapshot() == before,
              "setSource on the tab shown keeps the moment (a rebuild would jump to the newest snapshot)");
    }

    MessageDispatcher d;
    LogModel *m = loadMinimal(d, 1);
    CHECK(m != nullptr, "fixture: loco 1, minimal layout");
    if (!m) return;

    // ---- 3. a window that starts while loco 2's manual SoS is on ---------------------------------
    {
        const SosLog::Timeline t = SosLog::extract(m, atSos("10:00:30"), atSos("10:03:00"));
        CHECK(t.minimal() && t.eventsDerived && !t.events.isEmpty(), "the minimal log read from 10:00:30");
        QStringList words;
        for (const SosLog::Event &e : t.events) words << SosLog::eventText(e);
        CHECK(words.first() == QLatin1String("Manual SoS from loco 2 already on when the log starts, 410 m"),
              QByteArray("the first word on it: ") + words.first().toUtf8());
        CHECK(!words.contains(QStringLiteral("Loco 2 added to the SoS table, 410 m away")),
              "loco 2 is not said to be added at the window's start");
        CHECK(words.contains(QStringLiteral("Loco 4 added to the SoS table, 2000 m away")),
              "a loco added later still is");
        const QVector<SosLog::Spell> sp = SosLog::spells(t);
        const SosLog::Spell *manual = nullptr;
        for (const SosLog::Spell &s : sp) if (s.threat == SosLog::ManualSos) manual = &s;
        CHECK(manual && manual->onAtLogStart && manual->fromMs == atSos("10:00:30"), "its spell knows it began before");
        bool said = false;
        for (const RunReport::Episode &e : SosLog::spellEpisodes(t))
            said = said || e.what.startsWith(QLatin1String("Manual SoS from loco 2 (already on when the log starts)"));
        CHECK(said, "and the lanes and reports say so");

        const SosLog::Timeline whole = SosLog::extract(m);
        bool anyOnAtStart = false;
        for (const SosLog::Spell &s : SosLog::spells(whole)) anyOnAtStart = anyOnAtStart || s.onAtLogStart;
        CHECK(!anyOnAtStart, "the whole log starts quiet: nothing marked");
    }

    // ---- 4. DEST_LOCO_SOS from a station not logged -------------------------------------------
    {
        const QVector<SosLog::Spell> sp = SosLog::spells(SosLog::extract(m));
        const SosLog::Spell *dest = nullptr;
        for (const SosLog::Spell &s : sp) if (s.threat == SosLog::DestGeneral) dest = &s;
        CHECK(dest && dest->source == 0 && dest->what() == QLatin1String("Station DEST_LOCO_SOS (general) from a station"),
              QByteArray("no \"station 0\": ") + (dest ? dest->what().toUtf8() : QByteArray("none")));
    }

    // ---- 5. the collision colour follows the names ------------------------------------------------
    {
        bool agree = true;
        for (int threat = SosLog::HeadOn; threat <= SosLog::ShuntingLimit; ++threat) {
            SosLog::Spell s;
            s.threat = threat;
            s.source = 4;
            s.station = threat >= SosLog::StationGeneral;
            agree = agree && (SosLog::isCollisionText(s.what() + QStringLiteral(" — ended: timeout")) == s.isCollision());
        }
        CHECK(agree, "every threat's text is a collision exactly when the threat is");
        CHECK(SosLog::isCollisionText(QStringLiteral("Head-on from loco 4 — (no end)"))
                  && !SosLog::isCollisionText(QStringLiteral("Manual SoS from loco 2 — (no end)")),
              "head-on is, manual SoS is not");
    }
}
