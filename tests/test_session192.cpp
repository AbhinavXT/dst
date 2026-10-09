#include "testutil.h"
#include "sosfixture.h"

#include "capturedecoder.h"
#include "runreport.h"
#include "soslog.h"
#include "sosrelay.h"
#include "soswindow.h"
#include "statusline.h"
#include "theme.h"
#include "twolocoview.h"
#include "uistyle.h"

#include <QCoreApplication>
#include <QFile>
#include <QLabel>
#include <QTableWidget>

// =============================================================================
//  Session 192 — the minimal SoS layout (README 03, @sos version 2).
//  SYNTHETIC frames: schema/fixtures/sos_minimal_loco{1,2}.log, made by
//  tests/sosgen from README 03's function on the same scenario as the full
//  layout. Expected values from schema/sos_oracle.py (snapshot_v2) with the
//  same "compare one second with the next" rule, not from the code under test.
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

const SosLog::Spell *findSpell(const QVector<SosLog::Spell> &v, int threat, quint32 src)
{
    for (const SosLog::Spell &s : v)
        if (s.threat == threat && s.source == src) return &s;
    return nullptr;
}

}  // namespace

TEST_SUITE(session192)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    MessageDispatcher d;
    LogModel *m1 = loadMinimal(d, 1);
    LogModel *m2 = loadMinimal(d, 2);
    CHECK(m1 && m2, "fixtures: both locos' minimal SoS logs");
    if (!m1 || !m2) return;

    // ---- the decoder agrees with the schema ------------------------------------------------
    int frames = 0, agree = 0;
    for (int i = 0; i < m1->count(); ++i) {
        const CaptureLine c = CaptureDecoder::parseLine(m1->entryAt(i)->text);
        if (!c.valid || c.type != CapType::Sos) continue;
        QHash<QString, qint64> raw;
        CaptureDecoder::describe(c, nullptr, 0, &raw);
        SosLog::Snapshot s;
        ++frames;
        if (SosLog::decodeSnapshot(c.bytes, &s) && s.version == 2
            && raw.value(QStringLiteral("version")) == 2
            && raw.value(QStringLiteral("own_abs_loc")) == qRound64(s.ownLocM * 10)
            && raw.value(QStringLiteral("collision_loco_id")) == qint64(s.collisionLoco)
            && raw.value(QStringLiteral("sos_distance")) == qRound64(s.sosDistM * 10)
            && raw.value(QStringLiteral("own_emergency_status")) == s.ownEmergency
            && raw.value(QStringLiteral("n_src")) == s.sources.size()
            && raw.value(QStringLiteral("n_stn")) == s.stations.size())
            ++agree;
    }
    CHECK(frames == 281 && agree == frames, QByteArray("281 version-2 frames, the decoder agrees with the schema on all (")
                                                + QByteArray::number(agree) + "/" + QByteArray::number(frames) + ")");

    // ---- the timeline: snapshots carry their entries; events derived ------------------------
    const SosLog::Timeline t = SosLog::extract(m1);
    CHECK(t.minimal() && t.eventsDerived && t.snaps.size() == 281 && t.orphanSources == 0 && t.otherVersion == 0,
          "version 2: 281 snapshots, events derived");
    {
        const int i = t.snapAtOrBefore(atSos("10:00:30"));
        const SosLog::Snapshot &s = t.snaps.at(qMax(0, i));
        CHECK(i >= 0 && s.aggThreat == SosLog::ManualSos && s.collisionLoco == 2 && qFuzzyCompare(s.sosDistM, 410.0)
                  && SosLog::decisionLines(s).first() == QLatin1String("Target: loco 2, manual SoS, 410 m."),
              "10:00:30: the target worked out from the flags: loco 2, manual SoS, 410 m");
    }
    {
        const SosLog::Snapshot &s = t.snaps.at(qMax(0, t.snapAtOrBefore(atSos("10:01:40"))));
        const SosLog::Source *l3 = s.source(3);
        CHECK(l3 && l3->has(SosLog::BitUnusual) && (l3->closest & SosLog::BitUnusual) && l3->emergency == -1
                  && l3->eval == 0 && l3->rawLocM == l3->locM,
              "loco 3: unusual stop, closest of its kind (worked out); no ARP status, checks or raw position");
    }
    {
        const SosLog::Snapshot &s = t.snaps.at(qMax(0, t.snapAtOrBefore(atSos("10:03:50"))));
        CHECK(s.aggStationWon && s.aggStation == 4501 && s.aggThreat == SosLog::StationGeneral,
              "10:03:50: a station won (loco flags clear, station general SoS on)");
    }

    const QVector<SosLog::Spell> sp = SosLog::spells(t);
    CHECK(sp.size() == 7, QByteArray("seven spells, as with the full layout (") + QByteArray::number(sp.size()) + ")");
    const struct { int threat; quint32 src; const char *from; const char *to; } want[] = {
        { SosLog::ManualSos, 2, "10:00:10", "10:01:46" }, { SosLog::UnusualStop, 3, "10:01:31", "10:03:15" },
        { SosLog::HeadOn, 4, "10:02:30", "10:02:38" }, { SosLog::RearEnd, 5, "10:03:19", "10:03:45" },
        { SosLog::TrainParted, 6, "10:03:36", "10:03:56" }, { SosLog::StationGeneral, 4501, "10:03:48", "10:04:00" } };
    for (const auto &w : want) {
        const SosLog::Spell *s = findSpell(sp, w.threat, w.src);
        CHECK(s && s->fromMs == atSos(w.from) && s->toMs == atSos(w.to) && s->endedBy == QLatin1String("reason not logged"),
              QByteArray(SosLog::threatName(w.threat).toUtf8()) + " from " + QByteArray::number(w.src) + ": " + w.from + " to "
                  + w.to + ", reason not logged");
    }
    QStringList words;
    for (const SosLog::Event &e : t.events) words << SosLog::eventText(e);
    CHECK(words.contains(QStringLiteral("Manual SoS from loco 2 ended (reason not logged), at 0 m")),
          "a derived end says the reason is not logged");
    CHECK(words.contains(QStringLiteral("Own ARP status 0 (No Emergency) → 4 (Head-On Collision)")),
          "the own ARP status changing is an event");
    CHECK(SosLog::brakeDecisions(t).isEmpty(), "no brake decisions in this layout");

    // ---- the window --------------------------------------------------------------------------
    {
        SosWindow w(&d);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.resize(1100, 680);
        w.setSource(QStringLiteral("1_1"));
        w.show();
        for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
        CHECK(w.status()->text().contains(QLatin1String("minimal layout: threats to the second, no reasons or checks"))
                  && w.status()->text().contains(QLatin1String("changes")),
              QByteArray("the status says which layout: ") + w.status()->text().toUtf8());
        w.showMoment(atSos("10:01:40"));
        QTableWidget *tb = w.sourceTable();
        const int cAdj = w.sourceColumn(QStringLiteral("Adjusted")), cArp = w.sourceColumn(QStringLiteral("ARP status")),
                  cClosest = w.sourceColumn(QStringLiteral("Closest"));
        CHECK(tb->rowCount() == 2 && tb->item(1, cAdj) && tb->item(1, cAdj)->text().isEmpty()
                  && tb->item(1, cAdj)->toolTip().contains(QLatin1String("minimal layout")),
              "the check columns are blank, and say why");
        CHECK(tb->item(1, cArp)->text() == QStringLiteral("–") && tb->item(1, cClosest)->text() == QStringLiteral("✓"),
              "ARP status not logged; closest worked out");
        CHECK(w.decision()->text().contains(QLatin1String("Target: loco 2, manual SoS, 0 m")), "the decision panel");
        w.lowerTabs()->setCurrentIndex(1);
        CHECK(w.relay()->table()->rowCount() == 2
                  && w.relay()->summary()->text().contains(QLatin1String("heard = the threat it makes here")),
              "Two logs: matched on the threat, and it says so");
    }

    // ---- two logs ---------------------------------------------------------------------------
    const QVector<SosLog::Relay> r = SosLog::relay(SosLog::extract(m2), t, 2);
    CHECK(r.size() == 2 && r[0].byThreat && r[0].heardMs == atSos("10:00:10") && r[0].actedMs == atSos("10:00:10")
              && r[0].acted == QLatin1String("Manual SoS from loco 2 started, 810 m"),
          "loco 2's SoS: heard (as loco 1's manual SoS threat) and acted on at 10:00:10");
    CHECK(r.size() == 2 && r[1].heardMs == atSos("10:01:52") && r[1].actedMs < 0,
          "its release at 10:01:52: no manual SoS threat then, no decision");

    // ---- the two-loco view and the run summary -------------------------------------------------
    const TwoLocoView::Pair p = TwoLocoView::build(m1, QStringLiteral("1_1"), m2, QStringLiteral("2_1"));
    CHECK(p.a.source == QLatin1String("sos") && p.a.samples.size() == 281 && p.eventsA.sos.size() == 5
              && p.eventsA.headOn.size() == 1 && p.eventsA.rearEnd.size() == 1 && !p.bByA.isEmpty(),
          "the two-loco view reads it as it reads the full layout");
    const RunReport::Summary sum = RunReport::summarise(m1, QStringLiteral("1_1"), QString());
    CHECK(sum.sosThreats.size() == 7 && sum.sosDecisions.isEmpty(), "the run summary: 7 threats, no brake decisions");
}
