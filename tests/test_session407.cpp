#include "testutil.h"

#include "benchrecord.h"
#include "biusim.h"
#include "brakebenchwindow.h"
#include "locosimwindow.h"
#include "rfidtag.h"
#include "scenario.h"
#include "settings.h"
#include "simpreview.h"

#include <QFile>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>

#include <cmath>
#include <memory>

// =============================================================================
//  Session 407 — scripted scenarios: the Loco Simulator's Scenario tab, a
//  list of steps (a trigger: a time, a tag, a location; an action on the
//  loco, its readers or the Brake Test Bench), run the same way every time,
//  saved as .scenario.json, each step marked in a recording. The route is
//  LocoTcasSimulator's own Configuration1.xml.
// =============================================================================

using Scenario::Do;
using Scenario::When;

namespace {

Scenario::Step step407(When when, double at, Do action, const QString &on = QString(), const QString &kind = QString(),
                       double value = 0, const QString &text = QString(), const QString &tag = QString())
{
    Scenario::Step s;
    s.when = when;
    s.at = at;
    s.tag = tag;
    s.action = action;
    s.on = on;
    s.kind = kind;
    s.value = value;
    s.text = text;
    return s;
}

bool same407(const Scenario::Step &a, const Scenario::Step &b)
{
    return a.when == b.when && (a.when == When::Tag ? a.tag == b.tag : a.at == b.at) && a.action == b.action && a.on == b.on
           && a.kind == b.kind && a.value == b.value && a.text == b.text;
}

}  // namespace

TEST_SUITE(session407)
{
    {
        QSettings settings(Settings::iniPath(), QSettings::IniFormat);
        settings.remove(QStringLiteral("locosim"));
        settings.remove(QStringLiteral("brakebench"));
    }
    const QVector<RfidTag::Route> routes
        = RfidTag::readFile(QStringLiteral(":/tag_scenarios/LocoTcasSimulator/Configuration1.xml"));
    CHECK(!routes.isEmpty(), "the simulator's Configuration1.xml");
    if (routes.isEmpty()) return;
    const QVector<SimPreview::Row> rows = SimPreview::rowsOf(routes.first());
    const double sign = routes.first().dir == RfidTag::DirReverse ? -1 : 1;
    // The first tag reader 1 sends after the start (its duplicate, 4 m on: the next main tag is
    // 820 m away and the run covers about 200 m), and one further on to fault.
    QString triggerTag;
    for (const SimPreview::Row &r : rows) {
        const double on = (r.absLoc - rows.first().absLoc) * sign;
        if (triggerTag.isEmpty() && on > 0 && on < 120) triggerTag = r.id;
    }
    const QString faultTag = rows.value(rows.size() - 4).id;
    CHECK(!triggerTag.isEmpty(), qPrintable(QStringLiteral("a tag just past the start: %1").arg(triggerTag)));

    // ---- the script and its file ------------------------------------------------------------------
    Scenario::Script script;
    script.name = QStringLiteral("FSB with BC stuck");
    script.steps = {
        step407(When::Time, 0, Do::Speed, {}, {}, 72),
        step407(When::Tag, 0, Do::TagFault, faultTag, QStringLiteral("crc"), 0, {}, triggerTag),
        step407(When::Time, 3, Do::AirFault, QStringLiteral("BC"), QStringLiteral("stuck")),
        step407(When::Location, rows.first().absLoc + sign * 150, Do::Speed, {}, {}, 30),
        step407(When::Time, 5, Do::BiuSilent, {}, {}, 1),
        step407(When::Time, 6, Do::BiuFault, {}, {}, 0, QStringLiteral("0x1040")),
        step407(When::Time, 7, Do::SkipTag, QStringLiteral("NOPE"), {}, 1),
        step407(When::Time, 8, Do::Note, {}, {}, 0, QStringLiteral("the end")),
    };
    QString why;
    CHECK(Scenario::check(script, &why), qPrintable(QStringLiteral("a script that can run (%1)").arg(why)));
    Scenario::Script back;
    CHECK(Scenario::fromJson(Scenario::toJson(script), &back) && back.name == script.name && back.steps.size() == script.steps.size(),
          "its JSON reads back");
    bool allSame = back.steps.size() == script.steps.size();
    for (int i = 0; allSame && i < back.steps.size(); ++i) allSame = same407(back.steps.at(i), script.steps.at(i));
    CHECK(allSame, "every step as written");
    CHECK(Scenario::describe(script.steps.at(2)) == QLatin1String("at 3 s: BC stuck")
              && Scenario::describe(script.steps.at(1)) == QStringLiteral("at tag %1: tag %2 crc").arg(triggerTag, faultTag),
          "each step in words");
    CHECK(!Scenario::fromJson("{\"format\":\"other\"}", &back, &why) && !why.isEmpty(), "not a scenario file: refused");
    CHECK(!Scenario::fromJson("{\"format\":\"dlconsole-scenario\",\"steps\":[{\"when\":\"time\",\"do\":\"fly\"}]}", &back, &why)
              && why.contains(QLatin1String("fly")),
          "an action it does not know: refused by name");
    Scenario::Script badScript;
    badScript.steps = { step407(When::Tag, 0, Do::Hold), step407(When::Time, 0, Do::AirFault, QStringLiteral("BC"), QStringLiteral("melt")) };
    CHECK(!Scenario::check(badScript, &why) && why.startsWith(QLatin1String("Step 1")), "a tag trigger with no tag: refused");
    badScript.steps.removeFirst();
    CHECK(!Scenario::check(badScript, &why) && why.contains(QLatin1String("stuck")), "an air fault it does not know: refused");

    // ---- the tab ------------------------------------------------------------------------------------
    qint64 t = 1782558147000LL;
    auto bench = std::make_unique<BrakeBenchWindow>(nullptr);
    bench->setAttribute(Qt::WA_DeleteOnClose, false);
    bench->setClock([&t]() { return t; });
    bench->biu()->setSendSink([](const QByteArray &) {});
    bench->setAnalogSink([](const QByteArray &) {});
    LocoSimWindow window(nullptr);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setClock([&t]() { return t; });
    window.setSendSink([](const QByteArray &) {});
    window.setTagSink([](int, const QByteArray &) {});
    window.loadRouteFile(QStringLiteral(":/tag_scenarios/LocoTcasSimulator/Configuration1.xml"));
    window.setRoute(0);

    window.setScenario(script);
    Scenario::Script fromTable = window.scenario(&why);
    allSame = why.isEmpty() && fromTable.steps.size() == script.steps.size();
    for (int i = 0; allSame && i < fromTable.steps.size(); ++i) allSame = same407(fromTable.steps.at(i), script.steps.at(i));
    CHECK(allSame && fromTable.name == script.name, "into the table and out of it, the same script");
    auto *steps = window.findChild<QTableWidget *>(QStringLiteral("locoSimSteps"));
    QTemporaryDir dir;
    CHECK(window.saveScenario(dir.filePath(QStringLiteral("s.scenario.json"))), "saved");
    window.setScenario(Scenario::Script());
    CHECK(steps && steps->rowCount() == 0 && window.loadScenario(dir.filePath(QStringLiteral("s.scenario.json")))
              && steps->rowCount() == script.steps.size(),
          "loaded back");
    if (steps) {
        steps->item(2, 1)->setText(QStringLiteral("three"));
        CHECK(!window.runScenario() && !window.scenarioRunning(), "\"three\" seconds: not run, said why");
        steps->item(2, 1)->setText(QStringLiteral("3"));
    }

    // Run it, the bench open and recording.
    window.followBench(bench.get());
    BenchRecord::Recorder recorder;
    window.setRecorder(&recorder);
    recorder.start(dir.filePath(QStringLiteral("r.benchrec")), t);
    CHECK(window.runScenario() && window.scenarioRunning() && window.running(), "Run: the steps' clock, and the loco sending");
    CHECK(window.motion().speed() == 72 && window.stepStatus(0).startsWith(QLatin1String("done at 0.0")), "at 0 s: 72 km/h");
    bool speed30 = false;
    for (int i = 0; i < 100; ++i) {
        t += 100;
        bench->advance(0.1);
        window.tick();
        if (i == 35) CHECK(bench->plant().fault(BrakePlant::Bc) == BrakePlant::Fault::Stuck, "at 3 s: BC stuck on the bench");
        speed30 = speed30 || window.motion().speed() == 30;
    }
    CHECK(window.stepStatus(1).startsWith(QLatin1String("done")) && window.tagFault(faultTag).kind == LocoSim::TagFault::BadCrc,
          qPrintable(QStringLiteral("at tag %1 (reader 1 sent it): tag %2 given a bad CRC").arg(triggerTag, faultTag)));
    CHECK(speed30 && window.stepStatus(3).startsWith(QLatin1String("done")), "150 m on: 30 km/h");
    CHECK(bench->biu()->silent(), "at 5 s: the BIU silent");
    CHECK(bench->biu()->faultQueue().contains(0x1040), "at 6 s: BIU fault code 0x1040 raised");
    CHECK(window.stepStatus(6) == QLatin1String("not done: no tag NOPE on this route"), "a step it cannot do: not done, and why");
    CHECK(window.stepStatus(7).startsWith(QLatin1String("done")), "the note at 8 s");
    window.stop();
    CHECK(!window.scenarioRunning(), "stopping the loco stops the scenario");
    recorder.stop(t);
    BenchRecord::File f;
    BenchRecord::read(dir.filePath(QStringLiteral("r.benchrec")), &f);
    QStringList marks;
    for (const BenchRecord::Item &i : f.items)
        if (i.kind == QLatin1String("event")) marks << i.says;
    CHECK(marks.contains(QStringLiteral("Scenario FSB with BC stuck started")) && marks.contains(QStringLiteral("Step 3: at 3 s: BC stuck"))
              && marks.contains(QStringLiteral("Step 7: at 7 s: skip tag NOPE (not done: no tag NOPE on this route)"))
              && marks.contains(QStringLiteral("Step 8: at 8 s: the end")),
          qPrintable(QStringLiteral("the recording marks the run and every step (%1 marks)").arg(marks.size())));
    window.setRecorder(nullptr);

    // A bench step with no bench.
    bench.reset();
    Scenario::Script air;
    air.steps = { step407(When::Time, 0, Do::AirFault, QStringLiteral("BP"), QStringLiteral("leak"), 0.2) };
    window.setScenario(air);
    CHECK(window.runScenario() && window.stepStatus(0) == QLatin1String("not done: the Brake Test Bench is not open"),
          "no bench open: a bench step is not done, and says why");
    window.stop();

    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    settings.remove(QStringLiteral("locosim"));
    settings.remove(QStringLiteral("brakebench"));
}
