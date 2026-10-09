#include "testutil.h"
#include "sosfixture.h"

#include "layoutaudit.h"

#include "capturedecoder.h"
#include "dmitimetravel.h"
#include "laneband.h"
#include "runreport.h"
#include "soslog.h"
#include "soswindow.h"
#include "statusline.h"
#include "theme.h"
#include "uistyle.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QHash>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTableWidget>

// =============================================================================
//  Session 184 — @sos / @sossrc / @sosev: the firmware's SoS table, decoded.
//
//  SYNTHETIC frames (tests/sosgen, from the README's own logging code); the
//  expected values below are the scenario's, read back with
//  schema/sos_oracle.py, not with the code under test.
// =============================================================================

using namespace SosFixture;

namespace {

const SosLog::Spell *findSpell(const QVector<SosLog::Spell> &v, int threat, quint32 src)
{
    for (const SosLog::Spell &s : v)
        if (s.threat == threat && s.source == src) return &s;
    return nullptr;
}

}  // namespace

TEST_SUITE(session184)
{
    MessageDispatcher d;
    LogModel *m = load(d, 1);
    CHECK(m != nullptr, "fixture: loco 1's synthetic SoS log");
    if (!m) return;

    // ---- the decoder agrees with the schema, frame by frame --------------------------
    int frames = 0, agree = 0;
    for (int i = 0; i < m->count(); ++i) {
        const CaptureLine c = CaptureDecoder::parseLine(m->entryAt(i)->text);
        if (!c.valid) continue;
        QHash<QString, qint64> raw;
        CaptureDecoder::describe(c, nullptr, 0, &raw);
        bool ok = false;
        if (c.type == CapType::Sos) {
            SosLog::Snapshot s;
            ok = SosLog::decodeSnapshot(c.bytes, &s)
                 && raw.value(QStringLiteral("snap_id")) == qint64(s.snapId)
                 && raw.value(QStringLiteral("own_abs_loc")) == qRound64(s.ownLocM * 10)
                 && raw.value(QStringLiteral("agg_threat")) == s.aggThreat
                 && raw.value(QStringLiteral("collision_loco_id")) == qint64(s.collisionLoco)
                 && raw.value(QStringLiteral("sos_distance")) == qRound64(s.sosDistM * 10)
                 && raw.value(QStringLiteral("collision_distance")) == qRound64(s.collisionDistM * 10)
                 && raw.value(QStringLiteral("own_emergency_status")) == s.ownEmergency
                 && raw.value(QStringLiteral("n_stn_slots")) == s.stations.size();
        } else if (c.type == CapType::SosSrc) {
            SosLog::Source s;
            ok = SosLog::decodeSource(c.bytes, &s)
                 && raw.value(QStringLiteral("source_loco_id")) == qint64(s.locoId)
                 && raw.value(QStringLiteral("raw_abs_loc")) == s.rawLocM
                 && raw.value(QStringLiteral("abs_loc")) == s.locM
                 && raw.value(QStringLiteral("tin")) == s.tin
                 && raw.value(QStringLiteral("sos_distance")) == qRound64(s.sosDistM * 10);
        } else if (c.type == CapType::SosEv) {
            SosLog::Event e;
            ok = SosLog::decodeEvent(c.bytes, &e)
                 && raw.value(QStringLiteral("event")) == e.code
                 && raw.value(QStringLiteral("aux1")) == e.aux1
                 && raw.value(QStringLiteral("id")) == qint64(e.id)
                 && raw.value(QStringLiteral("distance")) == qRound64(e.distM * 10);
        } else {
            continue;
        }
        ++frames;
        if (ok) ++agree;
    }
    CHECK(frames == 1272, QByteArray("fixture: 1272 SoS frames (") + QByteArray::number(frames) + ")");
    CHECK(agree == frames, QByteArray("SosLog's decode agrees with the schema's on every frame (")
                               + QByteArray::number(agree) + "/" + QByteArray::number(frames) + ")");
    CHECK(CaptureDecoder::typeFromToken(QStringLiteral("sossrc")) == CapType::SosSrc
              && CaptureDecoder::typeFromToken(QStringLiteral("sosev")) == CapType::SosEv
              && CaptureDecoder::typeFromToken(QStringLiteral("sos")) == CapType::Sos,
          "the three tokens are known capture types");

    // ---- the timeline ------------------------------------------------------------------
    const SosLog::Timeline t = SosLog::extract(m);
    CHECK(t.snaps.size() == 462 && t.events.size() == 33, "462 snapshots and 33 events");
    CHECK(t.orphanSources == 0 && t.otherVersion == 0, "every @sossrc found its @sos; one layout version");
    bool allComplete = true;
    int sources = 0;
    for (const SosLog::Snapshot &s : t.snaps) { allComplete = allComplete && s.complete(); sources += s.sources.size(); }
    CHECK(allComplete && sources == 777, "each snapshot has as many sources as its in-use mask says (777 in all)");
    CHECK(SosLog::hasSos(m), "the tab carries @sos");

    // 10:00:30: loco 2's manual SoS, 410 m ahead (own 10 600 m, loco 2 at 11 010 m)
    {
        const int i = t.snapAtOrBefore(atSos("10:00:30"));
        CHECK(i >= 0, "a snapshot at 10:00:30");
        if (i >= 0) {
            const SosLog::Snapshot &s = t.snaps.at(i);
            CHECK(s.aggThreat == SosLog::ManualSos && s.collisionLoco == 2 && qFuzzyCompare(s.sosDistM, 410.0),
                  "target: loco 2, manual SoS, 410 m");
            const QStringList lines = SosLog::decisionLines(s);
            CHECK(!lines.isEmpty() && lines.first() == QLatin1String("Target: loco 2, manual SoS, 410 m."),
                  QByteArray("in words: ") + (lines.isEmpty() ? QByteArray() : lines.first().toUtf8()));
            CHECK(lines.contains(QStringLiteral("SoS speed-limit flags set: other loco manual SoS.")),
                  "the speed-limit flag, named");
            CHECK(lines.contains(QStringLiteral("DMI: SOS - Other Loco Manual.")), "what the DMI shows");
        }
    }

    // 10:01:40: passed loco 2 (target 0 m) while loco 3's unusual stop is 1 500 m ahead
    {
        const int i = t.snapAtOrBefore(atSos("10:01:40"));
        if (i >= 0) {
            const SosLog::Snapshot &s = t.snaps.at(i);
            CHECK(s.aggThreat == SosLog::ManualSos && s.collisionLoco == 2 && s.sosDistM == 0.0,
                  "10:01:40: the passed loco 2 is still the target, at 0 m (bug #8 as scripted)");
            CHECK(SosLog::decisionLines(s).first().contains(QLatin1String("(0 m: target distance removed or source passed)")),
                  "and the 0 m is explained");
            const SosLog::Source *l3 = s.source(3);
            CHECK(l3 && l3->has(SosLog::BitUnusual) && l3->sosDistM > 0.0,
                  "loco 3's unusual stop is in the table with a real distance");
            if (l3) {
                CHECK(l3->check(SosLog::EvalAdjusted) && l3->rawLocM == 13480 && l3->locM == 13500
                          && l3->rawDir == 1 && l3->dir == 2,
                      "loco 3: the ARP said 13 480 m nominal, adjusted to 13 500 m reverse");
                CHECK(l3->check(SosLog::EvalAdjacent) && l3->check(SosLog::EvalAdjFromTag) && !l3->check(SosLog::EvalAdjFailOpen),
                      "adjacent line, from an RFID tag");
                const QVector<SosLog::Check> cs = SosLog::checks(*l3);
                CHECK(cs.size() == 9 && cs.at(4).label == QLatin1String("Adjusted") && cs.at(4).on
                          && cs.at(4).tip.contains(QLatin1String("ARP said 13480 m nominal; after SOSWithAdjustment 13500 m reverse")),
                      "the checks say so in words");
            }
        }
    }

    // 10:02:32: loco 4 head-on, 920 m (under collision_trigger_distance 1000); own ARP now broadcasts 4
    {
        const int i = t.snapAtOrBefore(atSos("10:02:32"));
        if (i >= 0) {
            const SosLog::Snapshot &s = t.snaps.at(i);
            CHECK(s.aggThreat == SosLog::HeadOn && s.collisionLoco == 4 && qFuzzyCompare(s.collisionDistM, 920.0)
                      && s.ownEmergency == 4, "10:02:32: head-on with loco 4 at 920 m; own status 4");
        }
    }

    // 10:04:20: nothing left, but own status still 5 and DEST_LOCO_SOS still set (bugs #1 and #2 as scripted)
    {
        const int i = t.snapAtOrBefore(atSos("10:04:20"));
        if (i >= 0) {
            const SosLog::Snapshot &s = t.snaps.at(i);
            const QStringList lines = SosLog::decisionLines(s);
            CHECK(s.ownEmergency == 5 && (s.lp & SosLog::LpDestLocoSos), "own status 5 and the DEST_LOCO_SOS flag, long after");
            CHECK(lines.contains(QStringLiteral("Own ARP broadcasts emergency status 5 (Rear-End Collision).")),
                  "in words: the status the loco broadcasts");
            CHECK(lines.contains(QStringLiteral("SoS speed-limit flags set: station DEST_LOCO_SOS.")),
                  "and the flag that keeps the SoS speed limit");
        }
    }

    // ---- spells ------------------------------------------------------------------------
    const QVector<SosLog::Spell> sp = SosLog::spells(t);
    CHECK(sp.size() == 7, QByteArray("seven spells (") + QByteArray::number(sp.size()) + ")");
    const SosLog::Spell *manual = findSpell(sp, SosLog::ManualSos, 2);
    CHECK(manual && manual->fromMs == atSos("10:00:10") && manual->toMs == atSos("10:01:46") && manual->endReason == 2,
          "manual SoS from loco 2: 10:00:10 to 10:01:46, no longer to be processed");
    const SosLog::Spell *unusual = findSpell(sp, SosLog::UnusualStop, 3);
    CHECK(unusual && unusual->fromMs == atSos("10:01:31") && unusual->toMs == atSos("10:03:15") && unusual->endReason == 2,
          "unusual stop from loco 3: 10:01:31 to 10:03:15");
    const SosLog::Spell *ho = findSpell(sp, SosLog::HeadOn, 4);
    CHECK(ho && ho->fromMs == atSos("10:02:30") && ho->toMs == atSos("10:02:38") && ho->endReason == 3,
          "head-on with loco 4: 10:02:30 to 10:02:38, the collision condition no longer holds");
    const SosLog::Spell *re = findSpell(sp, SosLog::RearEnd, 5);
    CHECK(re && re->fromMs == atSos("10:03:19") && re->toMs == atSos("10:03:45"), "rear-end with loco 5: 10:03:19 to 10:03:45");
    const SosLog::Spell *parted = findSpell(sp, SosLog::TrainParted, 6);
    CHECK(parted && parted->fromMs == atSos("10:03:36") && parted->toMs == atSos("10:03:56") && parted->endReason == 1,
          "train parted, loco 6: 10:03:36 to 10:03:56, the source stopped sending it");
    const SosLog::Spell *stn = findSpell(sp, SosLog::StationGeneral, 4501);
    CHECK(stn && stn->station && stn->fromMs == atSos("10:03:48") && stn->toMs == atSos("10:04:00") && stn->endReason == 6,
          "station 4501's general SoS: 10:03:48 to 10:04:00, cancelled");
    const SosLog::Spell *dest = findSpell(sp, SosLog::DestGeneral, 4501);
    CHECK(dest && !dest->open && dest->fromMs == atSos("10:04:05") && dest->toMs == atSos("10:04:30")
              && dest->endedBy == QLatin1String("is_dest_loco_sos_recvd cleared"),
          "the DEST_LOCO_SOS flag: 10:04:05 until the reset at 10:04:30, though the station sent 0 at 10:04:11 (bug #2 as scripted)");
    bool anyOpen = false;
    for (const SosLog::Spell &s : sp) anyOpen = anyOpen || s.open;
    CHECK(!anyOpen, "the table reset closed every spell");
    if (manual)
        CHECK(manual->what() == QLatin1String("Manual SoS from loco 2"), "a spell names itself");

    // ---- brake decisions and event words -----------------------------------------------
    const QVector<SosLog::Event> br = SosLog::brakeDecisions(t);
    CHECK(br.size() == 4, "four brake decisions (all applied in this scenario)");
    if (!br.isEmpty())
        CHECK(SosLog::eventText(br.first()) == QLatin1String("Emergency brake applied: Manual SoS (LB code 21), 810 m"),
              QByteArray("the first, in words: ") + SosLog::eventText(br.first()).toUtf8());
    QStringList words;
    for (const SosLog::Event &e : t.events) words << SosLog::eventText(e);
    CHECK(words.contains(QStringLiteral("Manual SoS from loco 2 ended: no longer to be processed (range, direction or adjacency), at 0 m")),
          "a threat end says why");
    CHECK(words.contains(QStringLiteral("Passed loco 2: SoS target distance removed (was 10 m)")), "passing the source");
    CHECK(words.contains(QStringLiteral("DEST_LOCO_SOS from station 4501: 8 (Station General SoS)"))
              && words.contains(QStringLiteral("DEST_LOCO_SOS from station 4501 back to 0")),
          "the station's DEST_LOCO_SOS, on and off");
    CHECK(words.contains(QStringLiteral("SoS table reset: Non-Leading mode")), "the table reset");
    CHECK(words.contains(QStringLiteral("Own unusual-stoppage alarm acknowledged")), "the loco's own alarm");

    // ---- another layout version is counted, not guessed ----------------------------------
    {
        const CaptureLine c = CaptureDecoder::parseLine(m->entryAt(0)->text);
        QByteArray b = c.bytes;
        SosLog::Snapshot s;
        CHECK(SosLog::decodeSnapshot(b, &s), "fixture: the first frame decodes");
        b[0] = char(2);
        CHECK(!SosLog::decodeSnapshot(b, &s), "a version-2 frame is not read as version 1");
        CHECK(!SosLog::decodeSnapshot(c.bytes.left(40), &s), "nor a short one");
    }
    CHECK(SosLog::locoOfKey(QStringLiteral("2_1")) == 2 && SosLog::locoOfKey(QStringLiteral("x")) == 0, "loco of a tab key");

    // ==== the window ====================================================================
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    {
        SosWindow w(&d);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.resize(1100, 680);
        w.show();
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();

        CHECK(w.sourceKey() == QLatin1String("1_1") && w.timeline().snaps.size() == 462,
              "opens on the tab that has @sos");
        CHECK(w.slider()->maximum() == 461 && w.currentSnapshot() == 461, "the slider spans the snapshots, at the newest");
        CHECK(w.status()->text().contains(QLatin1String("462 snapshots")) && w.status()->text().contains(QLatin1String("33 decisions"))
                  && w.status()->text().contains(QLatin1String("7 threats")),
              QByteArray("the status counts them: ") + w.status()->text().toUtf8());
        CHECK(w.eventTable()->rowCount() == 33, "every decision is listed");

        w.showMoment(atSos("10:01:40"));
        QTableWidget *t = w.sourceTable();
        CHECK(t->rowCount() == 2, "10:01:40: two locos in the table");
        const int cThreat = w.sourceColumn(QStringLiteral("Threat")), cAdj = w.sourceColumn(QStringLiteral("Adjusted")),
                  cPassed = w.sourceColumn(QStringLiteral("Passed")), cSos = w.sourceColumn(QStringLiteral("SoS dist."));
        CHECK(cThreat > 0 && cAdj > 0 && cPassed > 0 && cSos > 0, "the columns are there");
        if (t->rowCount() == 2 && cThreat > 0 && cAdj > 0 && cPassed > 0) {
            CHECK(t->item(0, 0)->text().startsWith(QLatin1String("Loco 2")) && t->item(0, 0)->text().contains(QLatin1String("target"))
                      && t->item(0, 0)->font().bold(),
                  "loco 2 is the target, and its row says so");
            CHECK(t->item(0, cThreat)->text() == QLatin1String("manual SoS") && t->item(0, cPassed)->text() == QStringLiteral("\u2713"),
                  "loco 2: manual SoS, passed");
            CHECK(t->item(0, cSos)->text() == QLatin1String("0 m") && t->item(0, cSos)->toolTip().contains(QLatin1String("target distance was removed")),
                  "its 0 m SoS distance is explained on hover");
            CHECK(t->item(1, 0)->text() == QLatin1String("Loco 3") && t->item(1, cThreat)->text() == QLatin1String("unusual stop")
                      && t->item(1, cAdj)->text() == QStringLiteral("\u2713")
                      && t->item(1, cAdj)->toolTip().contains(QLatin1String("ARP said 13480 m nominal")),
                  "loco 3: unusual stop, its position adjusted, the ARP's own on hover");
        }
        CHECK(w.readout()->text().startsWith(QLatin1String("10:01:40 ")), QByteArray("the readout: ") + w.readout()->text().toUtf8());

        // The decisions at or before the cursor: the current one selected.
        const int cur = w.eventTable()->currentRow();
        CHECK(cur >= 0 && w.eventTable()->item(cur, 1)->text().startsWith(QLatin1String("Emergency brake applied: Unusual stop")),
              "the latest decision at or before 10:01:40 is current");
        // Next decision: 10:01:46, the manual SoS from loco 2 ends.
        const int before = w.currentSnapshot();
        for (QPushButton *b : w.findChildren<QPushButton *>())
            if (b->text().endsWith(QStringLiteral("\u25B6"))) b->click();
        CHECK(w.currentSnapshot() > before && w.timeline().snaps.at(w.currentSnapshot()).epochMs == atSos("10:01:46"),
              "Decision \u25B6 goes to the next decision (10:01:46)");

        // Follow the log: a moment picked elsewhere moves it.
        w.setFollowLog(true);
        CHECK(w.followBox()->isChecked(), "following");
        DmiTimeTravel::instance()->offer(&d, []() {
            DmiMoment m;
            m.valid = true;
            m.atMs = atSos("10:02:20");
            m.preferredKey = QStringLiteral("1_1");
            return m;
        });
        for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
        CHECK(w.currentSnapshot() >= 0 && w.timeline().snaps.at(w.currentSnapshot()).epochMs == atSos("10:02:20"),
              "following: 10:02:20 picked in the log, 10:02:20 shown");
        w.setFollowLog(false);
        DmiTimeTravel::instance()->reset();

        CHECK(w.minimumSizeHint().width() <= 1100 && w.minimumSizeHint().height() <= 700,
              QByteArray("fits a laptop (minimum ") + QByteArray::number(w.minimumSizeHint().width()) + " x "
                  + QByteArray::number(w.minimumSizeHint().height()) + ")");
        const QStringList loose = LayoutAudit::orphans(&w);
        CHECK(loose.isEmpty(), QByteArray("no visible widget outside every layout (") + loose.join(QLatin1String(", ")).toUtf8() + ")");
    }

    // A tab without @sos says so.
    {
        MessageDispatcher d2;
        d2.ingestLocal(7, 1, QByteArray("@dmi_7_1 2026-10-09T10:00:00 1 00"), atSos("10:00:00"), QString());
        d2.drainNow();
        SosWindow w(&d2);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        CHECK(w.timeline().isEmpty() && w.status()->text().contains(QLatin1String("no SoS lines")),
              "a log without @sos: empty, and the status says why");
    }

    // ==== lanes and the run summary =======================================================
    {
        const RunReport::Summary sum = RunReport::summarise(m, QStringLiteral("1_1"), QString());
        CHECK(sum.sosThreats.size() == 7 && sum.sosDecisions.size() == 4, "the run summary has the 7 threats and 4 decisions");
        const QString html = RunReport::toHtml(sum);
        CHECK(html.contains(QLatin1String("SoS threats (firmware, @sos): 7 threats"))
                  && html.contains(QLatin1String("SoS brake decisions (firmware, @sosev): 4 decisions"))
                  && html.contains(QStringLiteral("Manual SoS from loco 2 \u2014 ended: no longer to be processed")),
              "and its report lists them");

        LaneBand lanes;
        lanes.resize(1000, lanes.sizeHint().height());
        lanes.setModel(m, QStringLiteral("1_1"), QString());
        lanes.show();
        lanes.rebuildNow();
        CHECK(lanes.summary().sosThreats.size() == 7, "the Safety lane draws them");
        const QRect r = lanes.laneRect(1);
        const qint64 span = lanes.toMs() - lanes.fromMs();
        const int x = r.left() + int(double(atSos("10:00:50") - lanes.fromMs()) / double(qMax<qint64>(1, span)) * r.width());
        const QString tip = lanes.describeAt(QPoint(x, r.center().y()));
        CHECK(tip.startsWith(QLatin1String("SoS (firmware): Manual SoS from loco 2")), QByteArray("hover names it: ") + tip.toUtf8());
    }
}
