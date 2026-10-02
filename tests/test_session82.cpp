#include "testutil.h"

#include "bignumberpanel.h"
#include "cabpanel.h"
#include "capturedecoder.h"
#include "clockskewalarm.h"
#include "fieldplot.h"
#include "findbar.h"
#include "livefields.h"
#include "lococonsolewindow.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "replaywindow.h"
#include "searchwindow.h"
#include "settings.h"
#include "undolog.h"
#include "watchlist.h"
#include "watchpanel.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>

// =============================================================================
//  Session 82: big-number rules, sparklines and time in state; the cab view,
//  heartbeat and health lights; watch actions and occurrences; the find
//  bar's "All tabs" and grouped search; clock history; replay speed and
//  event stepping. The console and cab parts run on REAL frames from
//  replay/*.cap.
// =============================================================================

namespace {

QStringList realLines(const QString &type, int count)
{
    QStringList out;
    const QDir dir(QStringLiteral(DL_SRC_DIR) + QStringLiteral("/replay"));
    for (const QString &f : dir.entryList({ QStringLiteral("loco_1_1_*.cap") }, QDir::Files, QDir::Name)) {
        QFile file(dir.filePath(f));
        if (!file.open(QIODevice::ReadOnly)) continue;
        while (!file.atEnd() && out.size() < count) {
            const QString line = QString::fromLatin1(file.readLine()).trimmed();
            if (line.startsWith(QStringLiteral("@%1_").arg(type))) out << line;
        }
        if (out.size() >= count) break;
    }
    return out;
}

void feed(LocoConsoleWindow &window, const QString &line)
{
    LogEntryPtr entry(new LogEntry);
    entry->text = line;
    entry->epochMs = QDateTime::currentMSecsSinceEpoch();
    QMetaObject::invokeMethod(&window, "onEntryAppended", Q_ARG(QString, QStringLiteral("1_1")),
                              Q_ARG(LogEntryPtr, entry));
}

LogEntryPtr entry(const QString &text, qint64 ms)
{
    auto e = QSharedPointer<LogEntry>::create();
    e->text = text;
    e->epochMs = ms;
    e->cacheDerived();
    return e;
}

QString shotDir() { return QString::fromLocal8Bit(qgetenv("DL_SHOTS")); }

}  // namespace

TEST_SUITE(session82)
{
    // =================================================================================
    //  Tile rules
    // =================================================================================
    {
        TileRule r = TileRule::parse(QStringLiteral("fixed:above:70:80"));
        CHECK(r.kind == TileRule::Kind::Fixed && r.above && r.warn == 70 && r.alarm == 80, "a fixed rule parses");
        CHECK(r.level(60) == 0 && r.level(70) == 1 && r.level(79.9) == 1 && r.level(80) == 2,
              "fine below, amber from the warning value, red from the alarm value");
        CHECK(TileRule::parse(r.serialise()).serialise() == r.serialise(), "and round-trips");
        TileRule low = TileRule::parse(QStringLiteral("fixed:below:5:4"));
        CHECK(low.level(6) == 0 && low.level(5) == 1 && low.level(3) == 2, "a bad-when-low rule works downwards");
        TileRule rel = TileRule::parse(QStringLiteral("rel:dmi:speed_limit_permissible:5"));
        CHECK(rel.kind == TileRule::Kind::Relative && rel.refSource == QLatin1String("dmi:speed_limit_permissible"),
              "a relative rule keeps its limit field (whose own colon is not a separator)");
        CHECK(rel.level(70, true, 80) == 0 && rel.level(75, true, 80) == 1 && rel.level(81, true, 80) == 2,
              "amber within 5 of the limit, red above it");
        CHECK(rel.level(200, false, 0) == 0, "no limit known: no colour, not a guess");
        CHECK(!TileRule::parse(QStringLiteral("fixed:above:x:80")).isSet() && !TileRule::parse(QStringLiteral("junk")).isSet(),
              "a damaged rule is no rule");

        const LiveFieldRef old = LiveFieldRef::parse(QStringLiteral("Speed|lsrp:TRAIN_SPEED,arp:TRAIN_SPEED"));
        CHECK(old.isValid() && old.sources.size() == 2 && !old.rule.isSet(), "a tile saved by an older build still loads");
        LiveFieldRef withRule = old;
        withRule.rule = r;
        const LiveFieldRef back = LiveFieldRef::parse(withRule.serialise());
        CHECK(back.sources == old.sources && back.rule.serialise() == r.serialise(), "a tile with a rule round-trips");
        CHECK(LiveFields::defaultBigNumbers().first().rule.kind == TileRule::Kind::Relative,
              "the default Speed tile is coloured against the DMI's permitted speed");
    }

    // =================================================================================
    //  The tile panel: level, sparkline, time in state, plot, undo
    // =================================================================================
    QSettings ini(Settings::iniPath(), QSettings::IniFormat);
    const QVariant savedTiles = ini.value(QStringLiteral("lococonsole/bigNumbers"));
    {
        BigNumberPanel panel;
        UndoLog undo;
        panel.setUndoLog(&undo);
        panel.setTiles({ { QStringLiteral("Speed"), { QStringLiteral("lsrp:TRAIN_SPEED") },
                           TileRule::parse(QStringLiteral("fixed:above:70:80")) },
                         { QStringLiteral("Mode"), { QStringLiteral("lsrp:LOCO_MODE") } } });
        panel.show();
        const qint64 t0 = 1000000;
        auto values = [](double speed, const QString &mode, int level) {
            BigNumberPanel::TileValue s;
            s.missing = false; s.text = QStringLiteral("%1 km/h").arg(speed); s.hasNumber = true; s.number = speed;
            s.level = level;
            BigNumberPanel::TileValue m;
            m.missing = false; m.text = mode; m.hasNumber = true; m.number = mode.left(1).toDouble();
            return QVector<BigNumberPanel::TileValue>{ s, m };
        };
        panel.setValues(values(50, QStringLiteral("2 (Staff_Responsible)"), 0), t0);
        CHECK(panel.levelAt(0) == 0, "fine: no colour");
        CHECK(!panel.stateTextAt(1).isEmpty() && panel.stateTextAt(0).isEmpty(),
              "an enum field shows time in state; a measurement does not");
        for (int i = 1; i <= 30; ++i) panel.setValues(values(50 + i, QStringLiteral("2 (Staff_Responsible)"), 0), t0 + i * 1000);
        CHECK(panel.sparkPointsAt(0) == 31 && panel.sparkPointsAt(1) == 0, "the measurement collects a sparkline; the state does not");
        CHECK(panel.stateTextAt(1) == QLatin1String("for 30 s"), "the mode has held for 30 s");
        panel.setValues(values(85, QStringLiteral("2 (Staff_Responsible)"), 2), t0 + 65000);
        CHECK(panel.levelAt(0) == 2, "a red value shows red");
        if (!shotDir().isEmpty()) {
            panel.resize(760, 190);
            QTest::qWait(30);
            panel.grab().save(QDir(shotDir()).filePath(QStringLiteral("tiles.png")));
        }
        CHECK(panel.stateTextAt(1) == QLatin1String("for 1 min 5 s"), "time in state keeps counting");
        CHECK(panel.sparkPointsAt(0) <= 27, "and the sparkline keeps only the last minute");
        panel.setValues(values(85, QStringLiteral("6 (On_Sight)"), 2), t0 + 66000);
        CHECK(panel.stateTextAt(1) == QLatin1String("for 0 s"), "a new mode starts its own count");
        QVector<BigNumberPanel::TileValue> stale = values(85, QStringLiteral("6 (On_Sight)"), 2);
        stale[0].stale = true;
        panel.setValues(stale, t0 + 67000);
        CHECK(panel.levelAt(0) == 0 && panel.isStaleAt(0), "an old value is muted, never red");

        QSignalSpy plot(&panel, &BigNumberPanel::plotRequested);
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);   // the default tiles, replaced above
        QList<QFrame *> frames;
        for (QFrame *f : panel.findChildren<QFrame *>(QStringLiteral("bigTile"))) frames << f;
        CHECK(frames.size() == 2, "two tiles");
        if (frames.size() == 2) QTest::mouseDClick(frames.at(1), Qt::LeftButton);
        CHECK(plot.count() == 1 && plot.first().at(0).toInt() == 1, "double-clicking a tile asks to plot it");

        panel.setRule(1, TileRule::parse(QStringLiteral("fixed:above:5:6")));
        CHECK(panel.tiles().at(1).rule.isSet() && undo.canUndo(), "a rule is set, undoably");
        CHECK(BigNumberPanel::loadSaved().at(1).rule.serialise() == QLatin1String("fixed:above:5:6"), "and saved");
        undo.undo();
        CHECK(!panel.tiles().at(1).rule.isSet(), "Ctrl+Z takes it back");
    }
    if (savedTiles.isValid()) ini.setValue(QStringLiteral("lococonsole/bigNumbers"), savedTiles);
    else ini.remove(QStringLiteral("lococonsole/bigNumbers"));

    // =================================================================================
    //  Cab state and lamps, from real frames
    // =================================================================================
    const QStringList dmi = realLines(QStringLiteral("dmi"), 400);
    // SLRPs that carry a movement authority (and with it CUR/NEXT_SIG_ASPECT);
    // a bare SLRP has none.
    QStringList slrp;
    int slrpScanned = 0;
    for (const QString &l : realLines(QStringLiteral("slrp"), 4000)) {
        ++slrpScanned;
        // Read from the rows: sub-packet fields are not in the raw map.
        if (!LiveFields::valueIn(CaptureDecoder::describe(CaptureDecoder::parseLine(l)),
                                 QStringLiteral("CUR_SIG_ASPECT")).isNull()) slrp << l;
        if (slrp.size() >= 20) break;
    }
    CHECK(!slrp.isEmpty(), QByteArray("real SLRPs with a signal aspect found: ") + QByteArray::number(slrp.size())
                               + " in the first " + QByteArray::number(slrpScanned));
    const QStringList lsrp = realLines(QStringLiteral("lsrp"), 50);
    const QStringList nmsflt = realLines(QStringLiteral("nmsflt"), 5);
    CHECK(!dmi.isEmpty() && !lsrp.isEmpty(), "real DMI and LSRP frames are in replay/");
    if (slrp.isEmpty() || dmi.isEmpty() || lsrp.isEmpty()) return;
    {
        // A DMI frame with a target, if the capture has one.
        QString withTarget = dmi.value(0);
        for (const QString &l : dmi) {
            QHash<QString, qint64> raw;
            CaptureDecoder::describe(CaptureDecoder::parseLine(l), nullptr, 0, &raw);
            if (raw.value(QStringLiteral("target_distance")) > 0) { withTarget = l; break; }
        }
        QHash<QString, qint64> raw;
        QHash<QString, QString> text;
        for (const FieldRow &r : CaptureDecoder::describe(CaptureDecoder::parseLine(withTarget), nullptr, 0, &raw)) {
            if (!text.contains(r.field.trimmed())) text.insert(r.field.trimmed(), r.value);
        }
        const CabState cab = cabStateFrom(raw, text);
        CHECK(cab.hasDmi && cab.speed == double(raw.value(QStringLiteral("train_speed"))),
              "the cab shows the DMI's own train_speed");
        CHECK(cab.hasPermitted == (raw.value(QStringLiteral("speed_limit_permissible")) > 0), "permitted speed when there is one");
        CHECK(!cab.mode.isEmpty() && !cab.brake.isEmpty(), "mode and brake as the schema names them");
        CHECK(cab.aspectSource == QLatin1String("dmi"), "no SLRP given: the signal comes from the DMI");
        QHash<QString, qint64> sraw;
        QHash<QString, QString> stext;
        for (const FieldRow &r : CaptureDecoder::describe(CaptureDecoder::parseLine(slrp.first()), nullptr, 0, &sraw)) {
            if (!stext.contains(r.field.trimmed())) stext.insert(r.field.trimmed(), r.value);
        }
        const CabState withSlrp = cabStateFrom(raw, text, sraw, stext);
        CHECK(withSlrp.aspectSource == QLatin1String("slrp")
                  && withSlrp.currentAspect == stext.value(QStringLiteral("CUR_SIG_ASPECT")).trimmed().section(' ', 0, 0).toInt()
                  && !withSlrp.currentAspectText.isEmpty(),
              "with an SLRP: the aspects come from its movement authority");
        QHash<QString, qint64> eb = raw;
        eb.insert(QStringLiteral("brake_type"), 4);
        CHECK(cabStateFrom(eb, text).brakeLevel == 3, "emergency brake is the highest brake level");
    }
    CHECK(lampsFor(1).colours == QVector<QString>{ "red" } && lampsFor(11).colours == QVector<QString>{ "green" },
          "red is one red lamp, green one green");
    CHECK(lampsFor(10).colours.size() == 2 && lampsFor(15).extra == QLatin1String("calling-on"),
          "double yellow lights two; calling-on is named");
    CHECK(lampsFor(34).extra == QLatin1String("stencil route 3") && !lampsFor(9).known,
          "stencil routes are numbered; an unassigned value is not drawn as anything");
    CHECK(timeToTarget(500, 36) == QLatin1String("50 s") && timeToTarget(500, 0).isEmpty(),
          "time to target at the current speed; none when standing");
    CHECK(heartbeatState(900, 1000) == Heartbeat::State::Live && heartbeatState(3000, 1000) == Heartbeat::State::Late
              && heartbeatState(6000, 1000) == Heartbeat::State::Silent,
          "a 1 s packet: live, late after 2 s, silent after 5 s");
    CHECK(heartbeatState(15000, 4000) == Heartbeat::State::Late, "a slow packet is judged by its own rhythm");
    {
        const QVector<HealthLight> none = healthLightsFrom({}, false, {}, false);
        bool allUnknown = true;
        for (const HealthLight &l : none) allUnknown = allUnknown && l.state == HealthLight::State::Unknown;
        CHECK(allUnknown, "nothing heard: every light unknown, none green");
        QHash<QString, qint64> bits{ { "LKAVACH_health_status", 1 }, { "pg_health_status", 0 }, { "biu_health_status", 1 } };
        const QVector<HealthLight> some = healthLightsFrom(bits, true, { { "Radio", { "link down" } } }, true);
        CHECK(some.at(0).state == HealthLight::State::Ok && some.at(1).state == HealthLight::State::Fault,
              "the DMI's health bits: 1 OK, 0 fault");
        CHECK(some.last().name == QLatin1String("Radio") && some.last().state == HealthLight::State::Fault
                  && some.last().detail.contains(QLatin1String("link down")),
              "a module with an active NMS fault is red, naming the fault");
    }

    // =================================================================================
    //  The console with real frames
    // =================================================================================
    {
        QSettings s(Settings::iniPath(), QSettings::IniFormat);
        const QVariant cabWas = s.value(QStringLiteral("lococonsole/cabShown"));
        s.setValue(QStringLiteral("lococonsole/cabShown"), false);
        LocoConsoleWindow window(nullptr);
        window.resize(1400, 860);
        window.show();
        QApplication::processEvents();
        CHECK(!window.cabDisplay()->isVisible(), "the cab view is off until asked for");
        QPushButton *cabButton = nullptr;
        for (QPushButton *b : window.findChildren<QPushButton *>()) if (b->text() == QLatin1String("Cab view")) cabButton = b;
        CHECK(cabButton != nullptr, "there is a Cab view button");
        if (cabButton) cabButton->click();
        for (int i = 0; i < 3; ++i) {
            feed(window, lsrp.at(i));
            feed(window, dmi.at(i));
            feed(window, slrp.at(i));
            QTest::qWait(60);
        }
        for (const QString &l : nmsflt) feed(window, l);
        QMetaObject::invokeMethod(&window, "onRefreshTick");
        CHECK(window.cabDisplay()->isVisible() && window.cabDisplay()->state().hasDmi, "the cab view shows the DMI");
        CHECK(window.cabDisplay()->state().aspectSource == QLatin1String("slrp"), "with the signals from the SLRP");
        QStringList beatNames;
        for (const Heartbeat &b : window.linkLights()->beats()) beatNames << b.name;
        CHECK(beatNames.contains(QStringLiteral("dmi")) && beatNames.contains(QStringLiteral("lsrp"))
                  && beatNames.contains(QStringLiteral("slrp")),
              "a heartbeat light for each type the loco sent");
        bool someLive = false;
        for (const Heartbeat &b : window.linkLights()->beats()) someLive = someLive || b.state == Heartbeat::State::Live;
        CHECK(someLive, "and they are live");
        const QVector<HealthLight> lights = window.healthLights()->lights();
        CHECK(!lights.isEmpty() && lights.first().state != HealthLight::State::Unknown,
              "the Kavach unit's health comes from the real DMI");
        if (!shotDir().isEmpty()) {
            QTest::qWait(50);   // let the header relayout after its labels changed
            window.grab().save(QDir(shotDir()).filePath(QStringLiteral("console_cab.png")));
        }
        if (cabWas.isValid()) s.setValue(QStringLiteral("lococonsole/cabShown"), cabWas);
        else s.remove(QStringLiteral("lococonsole/cabShown"));
    }

    // =================================================================================
    //  Fix: a tile follows the FRESHEST of its sources. Reported: after a loco
    //  restart the console kept showing the old LSRP's Mode / Frame clock and
    //  never moved to the ARPs the restarted loco was sending.
    // =================================================================================
    {
        QHash<int, qint64> seen{ { int(CapType::LSRP), 1000 }, { int(CapType::ARP), 5000 } };
        const QStringList src{ QStringLiteral("lsrp:LOCO_MODE"), QStringLiteral("arp:LOCO_MODE") };
        CHECK(LiveFields::byFreshness(src, seen).first() == QLatin1String("arp:LOCO_MODE"),
              "the source heard most recently comes first");
        seen[int(CapType::LSRP)] = 9000;
        CHECK(LiveFields::byFreshness(src, seen).first() == QLatin1String("lsrp:LOCO_MODE"), "and back when LSRP resumes");
        CHECK(LiveFields::byFreshness(src, {}) == src, "never heard: the listed order");

        const QStringList arp = realLines(QStringLiteral("arp"), 3);
        CHECK(!arp.isEmpty(), "real ARP frames are in replay/");
        QSettings s(Settings::iniPath(), QSettings::IniFormat);
        const QVariant tilesWas = s.value(QStringLiteral("lococonsole/bigNumbers"));
        const QVariant shownWas = s.value(QStringLiteral("lococonsole/bigNumbersShown"));
        s.remove(QStringLiteral("lococonsole/bigNumbers"));               // the defaults
        s.setValue(QStringLiteral("lococonsole/bigNumbersShown"), true);
        if (!arp.isEmpty()) {
            LocoConsoleWindow window(nullptr);
            window.show();
            BigNumberPanel *big = window.bigNumbers();
            // Before the restart: LSRPs (the defaults list lsrp first).
            feed(window, lsrp.at(0));
            QMetaObject::invokeMethod(&window, "onRefreshTick");
            CHECK(big->detailTextAt(2).contains(QLatin1String("lsrp")), "running: the frame clock comes from LSRP");
            // The loco restarts: only ARPs from now on.
            QTest::qWait(30);
            feed(window, arp.at(0));
            QMetaObject::invokeMethod(&window, "onRefreshTick");
            const QString arpClock = LiveFields::frameClock(LiveFields::valueIn(
                CaptureDecoder::describe(CaptureDecoder::parseLine(arp.at(0))), QStringLiteral("FRAME_NUM")));
            const QString arpMode = LiveFields::valueIn(
                CaptureDecoder::describe(CaptureDecoder::parseLine(arp.at(0))), QStringLiteral("LOCO_MODE"));
            CHECK(big->detailTextAt(2).contains(QLatin1String("arp")) && big->valueTextAt(2) == arpClock,
                  "after the restart the frame clock follows the ARP");
            CHECK(big->valueTextAt(1) == arpMode && big->detailTextAt(1).contains(QLatin1String("arp")),
                  "and so does the mode");
            // LSRP comes back: it is the freshest again.
            QTest::qWait(30);
            feed(window, lsrp.at(1));
            QMetaObject::invokeMethod(&window, "onRefreshTick");
            CHECK(big->detailTextAt(2).contains(QLatin1String("lsrp")), "when LSRP resumes, the tiles go back to it");
        }
        if (tilesWas.isValid()) s.setValue(QStringLiteral("lococonsole/bigNumbers"), tilesWas);
        else s.remove(QStringLiteral("lococonsole/bigNumbers"));
        if (shownWas.isValid()) s.setValue(QStringLiteral("lococonsole/bigNumbersShown"), shownWas);
        else s.remove(QStringLiteral("lococonsole/bigNumbersShown"));
    }

    // =================================================================================
    //  Watch occurrences and actions
    // =================================================================================
    {
        WatchList list;
        const int i = list.add(QStringLiteral("hello"), QStringLiteral("greeting"));
        CHECK(list.observe(entry(QStringLiteral("hello a"), 1000), QStringLiteral("1_1")).size() == 1, "fires on the first match");
        CHECK(list.observe(entry(QStringLiteral("hello b"), 2000), QStringLiteral("1_1")).isEmpty(), "not again while it holds");
        CHECK(list.observe(entry(QStringLiteral("hello c"), 9000), QStringLiteral("1_1")).isEmpty(),
              "by default a later occurrence is counted, not announced");
        CHECK(list.watches().at(i).occurrences == 2 && list.watches().at(i).matches == 3,
              "two occurrences (a gap over 3 s), three matching frames");
        list.setActions(i, true, true, true);
        CHECK(list.observe(entry(QStringLiteral("hello d"), 20000), QStringLiteral("1_1")).size() == 1,
              "with \"every occurrence\": a new occurrence is announced");
        CHECK(list.watches().at(i).firedAtMs == 20000, "with its own frame as the evidence");
        WatchList copy;
        copy.fromStrings(list.toStrings());
        CHECK(copy.watches().at(0).beep && copy.watches().at(0).bookmark && copy.watches().at(0).everyTime,
              "actions are saved with the watch");
        WatchList legacy;
        legacy.fromStrings({ QStringLiteral("hello") + QChar(0x1F) + QStringLiteral("x") + QChar(0x1F) + QStringLiteral("1") });
        CHECK(legacy.count() == 1 && !legacy.watches().at(0).beep, "a watch saved by an older build loads, with no actions");
        list.rearm(i);
        CHECK(list.watches().at(i).occurrences == 0, "re-arming clears the count");

        WatchPanel panel;
        QSignalSpy marks(&panel, &WatchPanel::bookmarkRequested);
        panel.restore();
        const QStringList savedWatches = Settings::watches();
        Settings::setWatches({ QStringLiteral("zebra") });
        panel.restore();
        CHECK(panel.setActions(0, false, true, false), "the bookmark action is set from the panel");
        panel.observe(entry(QStringLiteral("a zebra"), 5000), QStringLiteral("9_1"));
        CHECK(marks.count() == 1 && marks.first().at(1).toString() == QLatin1String("9_1"),
              "firing asks for the frame to be bookmarked");
        Settings::setWatches(savedWatches);
    }

    // =================================================================================
    //  Find bar "All tabs" and grouped search
    // =================================================================================
    {
        using M = FindBar::Mode;
        CHECK(FindBar::toLogQuery(M::Text, QStringLiteral("RAD fail")) == QLatin1String("\"RAD fail\""),
              "text becomes a quoted phrase");
        CHECK(FindBar::toLogQuery(M::Text, QStringLiteral("say \"hi\"")).startsWith(QLatin1Char('/')),
              "text with a quote goes as an escaped regex");
        CHECK(FindBar::toLogQuery(M::Regex, QStringLiteral("err[0-9]+")) == QLatin1String("/err[0-9]+/"), "a regex stays a regex");
        QString why;
        CHECK(FindBar::toLogQuery(M::Regex, QStringLiteral("a/b"), &why).isEmpty() && !why.isEmpty(),
              "one that cannot be said is refused, with the reason");
        CHECK(FindBar::toLogQuery(M::Hex, QStringLiteral("0a 1b")) == QLatin1String("hex:\"0a 1b\""), "hex keeps its spaces, quoted");
        CHECK(FindBar::toLogQuery(M::Query, QStringLiteral("sev:error src:21_1")) == QLatin1String("sev:error src:21_1"),
              "a query is passed as it is");
        CHECK(FindBar::toLogQuery(M::Extended, QStringLiteral("a\\tb")) == QStringLiteral("\"a\tb\""), "extended is unescaped first");

        MessageDispatcher dispatcher;
        LogModel *a = dispatcher.ensureModel(QStringLiteral("21_1"));
        LogModel *b = dispatcher.ensureModel(QStringLiteral("81_1"));
        a->appendEntries({ entry(QStringLiteral("needle one"), 1000), entry(QStringLiteral("hay"), 2000),
                           entry(QStringLiteral("needle three"), 5000) });
        b->appendEntries({ entry(QStringLiteral("needle two"), 3000) });
        SearchWindow search(&dispatcher, nullptr);
        search.setQueryText(QStringLiteral("needle"), true);
        CHECK(search.hitCount() == 3 && search.resultRowCount() == 3, "ungrouped: three hits, interleaved by time");
        search.setGroupByTab(true);
        search.setQueryText(QStringLiteral("needle"), true);
        CHECK(search.hitCount() == 3 && search.resultRowCount() == 5, "grouped: the same hits under two tab headings");
    }

    // =================================================================================
    //  Clock history
    // =================================================================================
    {
        ClockHistory h;
        ClockHistory::Sample s;
        s.ms = 0; s.hasLoco = true; s.locoSkew = 1; s.hasStn = true; s.stnSkew = 0; s.hasGap = true; s.gap = 1;
        CHECK(h.add(s), "a sample is kept");
        s.ms = 400;
        CHECK(!h.add(s), "but not two in the same second");
        for (int i = 1; i <= 2000; ++i) { s.ms = i * 1000; s.gap = i % 7; h.add(s); }
        CHECK(h.samples().first().ms >= 2000 * 1000 - ClockHistory::kKeepMs, "only the last 30 minutes are kept");
        const QVector<FieldSeries> series = h.toSeries();
        CHECK(series.size() == 3 && series.first().fieldName == QString::fromUtf8("loco − station")
                  && series.first().unit == QLatin1String("s"),
              "three series for the graph, in seconds");
        ClockHistoryWindow w(&h);
        w.resize(800, 400);
        w.show();
        CHECK(w.canvas()->seriesList().size() == 3 && w.canvas()->laneCount() == 1, "the graph shows them on one axis");
        if (!shotDir().isEmpty()) w.grab().save(QDir(shotDir()).filePath(QStringLiteral("clock_history.png")));
    }

    // =================================================================================
    //  Replay: event stepping and speed, on a real capture
    // =================================================================================
    {
        const QDir dir(QStringLiteral(DL_SRC_DIR) + QStringLiteral("/replay"));
        const QString cap = dir.filePath(dir.entryList({ QStringLiteral("loco_1_1_*.cap") }, QDir::Files, QDir::Name).value(0));
        ReplayWindow replay(cap);
        CHECK(replay.recordCount() > 100 && !replay.events().isEmpty(), "a real capture loads, with events");
        const int first = replay.nextEventIndex(-1, +1, 0);
        CHECK(first >= 0, "there is a first event");
        replay.stepEvent(+1);
        const int at = replay.cursorIndex();
        CHECK(at > 0 && at == replay.nextEventIndex(0, +1, 0), "▶ goes to the next event");
        replay.stepEvent(+1);
        CHECK(replay.cursorIndex() > at, "and on to the one after");
        replay.stepEvent(-1);
        CHECK(replay.cursorIndex() == at, "◀ comes back");
        int errors = 0;
        for (const ReplayEvent &e : replay.events()) if (e.sev == ReplayEvent::Error) ++errors;
        const int firstError = replay.nextEventIndex(-1, +1, int(ReplayEvent::Error));
        CHECK((errors == 0) == (firstError < 0), "the error filter finds errors exactly when there are some");

        auto *speed = replay.findChild<QComboBox *>(QStringLiteral("replaySpeed"));
        CHECK(speed && speed->currentData().toDouble() == 1.0, "playback starts at 1×, in recorded time");
        if (speed) speed->setCurrentIndex(speed->findData(50.0));
        QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
        const int before = replay.cursorIndex();
        QApplication::sendEvent(&replay, &space);
        QTest::qWait(600);
        QApplication::sendEvent(&replay, &space);
        CHECK(replay.cursorIndex() > before, "50× plays forward");
    }
}
