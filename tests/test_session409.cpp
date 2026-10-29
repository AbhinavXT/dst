#include "testutil.h"

#include "benchrecord.h"
#include "locosim.h"
#include "locosimwindow.h"
#include "rfidtag.h"
#include "scenario.h"
#include "settings.h"
#include "tagbuilderwindow.h"

#include <QSettings>
#include <QTemporaryDir>

// =============================================================================
//  Session 409 — the RFID readers' health frames, as LocoTcasSimulator sends
//  them (locodialog.cpp sendRFidReader1Status / 2Status):
//  LPIM_LTCAS_RFID_READER_HEALTH, 7 B: AA AA, length 7, reader, 1 OK / 2 fail,
//  BB BB. Reader 1's every 1 s, reader 2's every 2 s, while the reader is on;
//  reader 2's on port B when it has its own port; a reader at Fail sends no
//  tags. No health frame has been captured off the line: the bytes are laid
//  out from the simulator's struct.
// =============================================================================

TEST_SUITE(session409)
{
    {
        QSettings settings(Settings::iniPath(), QSettings::IniFormat);
        settings.remove(QStringLiteral("locosim"));
    }
    // ---- the frame ------------------------------------------------------------------------------
    CHECK(LocoSim::healthFrame(1, true) == QByteArray::fromHex("AAAA070101BBBB"), "reader 1 OK: AA AA 07 01 01 BB BB");
    CHECK(LocoSim::healthFrame(2, false) == QByteArray::fromHex("AAAA070202BBBB"), "reader 2 fail: AA AA 07 02 02 BB BB");

    LocoSimWindow window(nullptr);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    qint64 t = 1782558147000LL;
    window.setClock([&t]() { return t; });
    window.setSendSink([](const QByteArray &) {});
    struct Out { qint64 ms; int port; QByteArray frame; };
    QVector<Out> health, tags;
    window.setHealthSink([&](int port, const QByteArray &f) { health.append({ t, port, f }); });
    window.setTagSink([&](int port, const QByteArray &f) { tags.append({ t, port, f }); });
    window.setReaderOn(1, false);
    window.setReaderOn(2, false);
    window.setReaderOn(1, true);
    window.setReaderOn(2, true);
    const qint64 t0 = t;
    const auto wait = [&](int ms) {
        for (int i = 0; i < ms / 100; ++i) {
            t += 100;
            window.healthTick();
        }
    };

    // ---- timing, stopped or running --------------------------------------------------------------
    wait(6000);
    int r1 = 0, r2 = 0;
    for (const Out &o : health) (o.frame.at(3) == 1 ? r1 : r2)++;
    CHECK(!window.running() && r1 == 6 && r2 == 3, qPrintable(QStringLiteral("6 s, the loco stopped: reader 1 six frames (%1), "
                                                                              "reader 2 three (%2)").arg(r1).arg(r2)));
    CHECK(!health.isEmpty() && health.first().ms == t0 + 1000 && health.first().frame == LocoSim::healthFrame(1, true),
          "the first one period after the reader is set, saying OK");
    bool onA = true;
    for (const Out &o : health) onA = onA && o.port == 0;
    CHECK(onA, "one port for both: all on port A");

    // ---- reader 2 on its own port; Fail ---------------------------------------------------------
    window.setReader2OwnPort(true);
    window.setReaderHealth(2, false);
    health.clear();
    wait(2000);
    bool r2onB = false;
    for (const Out &o : health)
        if (o.frame.at(3) == 2) r2onB = o.port == 1 && o.frame == LocoSim::healthFrame(2, false);
    CHECK(r2onB && !window.readerHealthOk(2), "reader 2 at Fail, on its own port: its frame on port B says 2");

    // Fail: a reader sends its health but no tags.
    window.loadRouteFile(QStringLiteral(":/tag_scenarios/LocoTcasSimulator/Configuration1.xml"));
    window.setRoute(0);
    window.setReader2OwnPort(false);
    window.setReaderHealth(1, false);
    window.setReaderHealth(2, true);
    window.motion().setSpeed(72);
    window.start();
    for (int i = 0; i < 600; ++i) {
        t += 100;
        window.tick();
    }
    window.stop();
    int tagsBy1 = 0, tagsBy2 = 0;
    for (const Out &o : tags) (quint8(o.frame.at(3)) == 1 ? tagsBy1 : tagsBy2)++;
    CHECK(tagsBy1 == 0 && tagsBy2 > 0, qPrintable(QStringLiteral("reader 1 at Fail: none of its tags (%1); reader 2's still (%2)")
                                                      .arg(tagsBy1).arg(tagsBy2)));
    window.setReaderHealth(1, true);
    CHECK(window.readerHealthOk(1), "back to OK");

    // Off: no health at all.
    window.setReaderOn(1, false);
    window.setReaderOn(2, false);
    health.clear();
    wait(4000);
    CHECK(health.isEmpty(), "both readers off: no health frames");

    // A recording carries them; a scenario step sets a reader's health.
    QTemporaryDir dir;
    BenchRecord::Recorder recorder;
    window.setRecorder(&recorder);
    window.setReaderOn(1, true);
    recorder.start(dir.filePath(QStringLiteral("h.benchrec")), t);
    wait(2000);
    recorder.stop(t);
    window.setRecorder(nullptr);
    BenchRecord::File f;
    BenchRecord::read(dir.filePath(QStringLiteral("h.benchrec")), &f);
    int recorded = 0;
    for (const BenchRecord::Item &i : f.items)
        recorded += i.kind == QLatin1String("health") && i.says == QStringLiteral("reader 1 \u00B7 health OK (1)") ? 1 : 0;
    CHECK(recorded == 2 && BenchRecord::kinds().contains(QStringLiteral("health")), qPrintable(QStringLiteral("recorded as health frames, with what they say (%1 of %2)").arg(recorded).arg(f.items.size())));

    Scenario::Script script;
    Scenario::Step fail;
    fail.when = Scenario::When::Time;
    fail.action = Scenario::Do::Reader;
    fail.on = QStringLiteral("2");
    fail.kind = QStringLiteral("fail");
    fail.value = 1;
    script.steps = { fail };
    QString why;
    CHECK(Scenario::check(script, &why) && Scenario::describe(fail) == QLatin1String("at 0 s: reader 2 on (fail)"),
          "a scenario step: reader 2 on, at Fail");
    window.setScenario(script);
    CHECK(window.runScenario() && !window.readerHealthOk(2), "run: reader 2 at Fail");
    window.stop();
    fail.kind = QStringLiteral("sick");
    script.steps = { fail };
    CHECK(!Scenario::check(script, &why), "a kind other than ok / fail: refused");

    // Abhinav: "if tag builder is minimized the rfid window shows no tag builder is open". The main
    // window's dock minimises a tool window by hiding it: hidden is still open; closed is not.
    {
        auto *builder = new TagBuilderWindow(nullptr);
        builder->setRoute(RfidTag::readFile(QStringLiteral(":/tag_scenarios/LocoTcasSimulator/Configuration1.xml")).value(0));
        builder->show();
        builder->hide();   // what MinimizedDock does
        window.setTagBuilders([builder]() { return QList<TagBuilderWindow *>{ builder }; });
        window.chooseBuilder();
        CHECK(window.followsBuilder(), "a minimised (hidden) Tag Builder is still followed");
        CHECK(builder->close() && builder->isClosed(), "closed: it says so before it is deleted");
        window.setTagBuilders([]() { return QList<TagBuilderWindow *>(); });
        delete builder;
    }

    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    settings.remove(QStringLiteral("locosim"));
}
