#include "testutil.h"

#include "cabio.h"
#include "dmipanel.h"
#include "settings.h"

#ifdef DL_HAVE_SERIAL
#include "cabiosimwindow.h"
#endif

#include <QDir>
#include <QFile>
#include <QSet>
#include <QSettings>

// =============================================================================
//  Session 356 — Cab I/O Simulator: scenario rules.
//
//  Abhinav: "allow user to make customized scenerios, like when mode fs then
//  disable cab input. ... when output has eb applied the input should change
//  the eb no nc feedback from 01 to 10, same for fsb and nb. When any brake
//  applied traction cuttoff then its feedback shkuld change". Asked and
//  answered: the mode comes from the DMI frames; the full set of built-in
//  feedback rules; traction_cutoff_fb is 1 while any brake is applied.
//
//  Real frames: tests/fixtures/cabio_frames_20261023.txt (output data 0x03E1:
//  every SIFA brake bit 0, i.e. applied) and the DMI frames in replay/.
// =============================================================================

namespace {

using namespace CabIo;

QByteArray realOutputData()
{
    QFile f(QStringLiteral(DL_SRC_DIR "/tests/fixtures/cabio_frames_20261023.txt"));
    if (!f.open(QIODevice::ReadOnly)) return QByteArray();
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        Frame fr;
        if (l.startsWith("CC CC") && parse(QByteArray::fromHex(QByteArray(l).replace(' ', "")), &fr) && fr.isOutputData())
            return fr.raw;
    }
    return QByteArray();
}

// A real DMI line whose mode is known.
QString realDmiLine()
{
    const QStringList files = QDir(QStringLiteral(DL_SRC_DIR "/replay")).entryList({ QStringLiteral("*.cap") });
    for (const QString &name : files) {
        QFile f(QStringLiteral(DL_SRC_DIR "/replay/") + name);
        if (!f.open(QIODevice::ReadOnly)) continue;
        while (!f.atEnd()) {
            const QString l = QString::fromLatin1(f.readLine().trimmed());
            if (l.startsWith(QLatin1String("@dmi_")) && dmiStateFromLine(l).mode > 0) return l;
        }
    }
    return QString();
}

#ifdef DL_HAVE_SERIAL
// The state the simulator sends for input `name` (base, as clicked or set).
int inputState(CabIoSimWindow &w, const char *name)
{
    for (int b = 0; b < w.bankCount(); ++b)
        for (int i = 0; i < w.bank(b)->count(); ++i)
            if (w.bank(b)->input(i).name == QLatin1String(name)) return w.bank(b)->state(i);
    return -9;
}

void setInput(CabIoSimWindow &w, const char *name, int s)
{
    for (int b = 0; b < w.bankCount(); ++b)
        for (int i = 0; i < w.bank(b)->count(); ++i)
            if (w.bank(b)->input(i).name == QLatin1String(name)) w.bank(b)->setState(i, s);
}
#endif

void clearSaved()
{
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    for (const char *k : { "cabio/rules_sifa", "cabio/rules_old", "cabio/scenarios", "cabio/cab",
                           "cabio/default_sifa1", "cabio/default_sifa2", "cabio/default_old" })
        s.remove(QLatin1String(k));
}

}  // namespace

TEST_SUITE(session356)
{
    const QByteArray real = realOutputData();
    Frame realFrame;
    CHECK(parse(real, &realFrame) && realFrame.outputWord() == 0x03E1, "fixture: the real output data, 0x03E1");

    // ---- the rules, no window ------------------------------------------------------------------
    {
        const QVector<Rule> sifa = defaultRules(Cab::Sifa);
        CHECK(sifa.size() == 9, "SIFA: nine built-in rules");
        QSet<QString> inputs;
        for (const InputLayout *l : inputLayouts(Cab::Sifa))
            for (const Input &in : l->inputs) inputs << in.name;
        bool allKnown = true;
        for (const Rule &r : sifa) allKnown = allKnown && inputs.contains(r.input) && whenKeys(Cab::Sifa).contains(r.when);
        CHECK(allKnown, "every SIFA rule names a SIFA input and a condition the list offers");
        bool oldKnown = true;
        for (const Rule &r : defaultRules(Cab::Old)) {
            bool found = false;
            for (const Input &in : oldCabInputs().inputs) found = found || in.name == r.input;
            oldKnown = oldKnown && found && whenKeys(Cab::Old).contains(r.when);
        }
        CHECK(oldKnown, "every old-cab rule names an old-cab input and a condition the list offers");

        Rule eb;
        eb.when = QStringLiteral("out:emergency_brake");
        CHECK(ruleHolds(eb, Cab::Sifa, 0, false, 0) == -1, "no output word yet: not known");
        CHECK(ruleHolds(eb, Cab::Sifa, 0x03E1, true, 0) == 1, "real 0x03E1: SIFA EB (bit 13 = 0, active low) applied");
        CHECK(ruleHolds(eb, Cab::Sifa, 0x3C00, true, 0) == 0, "bits 10-13 set: EB released");
        CHECK(ruleHolds(eb, Cab::Old, 0x0080, true, 0) == 1 && ruleHolds(eb, Cab::Old, 0, true, 0) == 0,
              "old cab EB acts on 1 (bit 7)");
        Rule any;
        any.when = QStringLiteral("anybrake");
        CHECK(ruleHolds(any, Cab::Sifa, 0x3C00, true, 0) == 0, "all four brake bits 1: no brake");
        CHECK(ruleHolds(any, Cab::Sifa, 0x3C00 & ~(1u << 12), true, 0) == 1, "NB alone (bit 12 = 0): a brake");
        Rule fs;
        fs.when = QStringLiteral("mode:4");
        CHECK(ruleHolds(fs, Cab::Sifa, 0, false, 0) == -1, "no DMI mode yet: not known");
        CHECK(ruleHolds(fs, Cab::Sifa, 0, false, 4) == 1 && ruleHolds(fs, Cab::Sifa, 0, false, 6) == 0, "mode 4 is FS, 6 is not");
        CHECK(whenText(QStringLiteral("mode:4"), Cab::Sifa) == QLatin1String("Mode is FS"), "\"Mode is FS\"");
        CHECK(whenText(QStringLiteral("out:emergency_brake"), Cab::Sifa) == QLatin1String("Emergency brake applied"),
              "\"Emergency brake applied\"");

        bool ok = false;
        const QVector<Rule> back = rulesFromJson(rulesToJson(sifa), &ok);
        bool same = ok && back.size() == sifa.size();
        for (int i = 0; same && i < back.size(); ++i)
            same = back[i].when == sifa[i].when && back[i].input == sifa[i].input && back[i].value == sifa[i].value
                   && back[i].otherwise == sifa[i].otherwise && back[i].on == sifa[i].on;
        CHECK(same, "rules survive JSON");
        rulesFromJson("not json", &ok);
        CHECK(!ok, "a file that is not rules is refused");
    }

#ifdef DL_HAVE_SERIAL
    // ---- the window: the real output frame sets the feedbacks -----------------------------------
    {
        clearSaved();
        CabIoSimWindow w(nullptr);
        w.setWriteSink([](const QByteArray &) {});
        w.setCab(Cab::Sifa);
        CHECK(w.scenariosOn() && w.rules().size() == 9, "opens with the rules on, the built-in nine");
        CHECK(inputState(w, "eb_relay_fb") == 1, "before any output data: EB relay FB at rest, 01");

        w.feedBytes(real);
        CHECK(inputState(w, "eb_relay_fb") == 2 && inputState(w, "fsb_relay_fb") == 2
              && inputState(w, "nb_relay_fb") == 2 && inputState(w, "le_relay_fb") == 2,
              "real frame, every brake applied: EB/FSB/NB/LE relay FB 01 -> 10");
        CHECK(inputState(w, "traction_cutoff_fb") == 1, "any brake applied: traction_cutoff_fb 1");
        CHECK(inputState(w, "pvef_disabled_fb") == 1 && inputState(w, "neutral_section_fb") == 1
              && inputState(w, "fouling_mark_entry_fb") == 1, "PVEF, neutral section, fouling mark energised: FBs 1");
        CHECK(inputState(w, "sifa_valve_fb") == 2, "SIFA valve open (bit 14 = 0): its FB 10");

        // A click in between stands until the condition changes again.
        setInput(w, "eb_relay_fb", 1);
        w.feedBytes(real);
        CHECK(inputState(w, "eb_relay_fb") == 1, "same frame again: the clicked 01 stands");

        w.feedBytes(outputData(13, 0x40, 0x7C00, WireFormat()));
        CHECK(inputState(w, "eb_relay_fb") == 1 && inputState(w, "nb_relay_fb") == 1, "brakes released: relay FBs 01");
        CHECK(inputState(w, "traction_cutoff_fb") == 0, "no brake: traction_cutoff_fb 0");
        CHECK(inputState(w, "sifa_valve_fb") == 1, "SIFA valve closed: its FB 01");
        CHECK(w.ruleFired(0) == 2, "the EB rule set its input twice");

        // NB alone: only NB's FB, and traction cut off.
        w.feedBytes(outputData(13, 0x41, quint16(0x7C00 & ~(1u << 12)), WireFormat()));
        CHECK(inputState(w, "nb_relay_fb") == 2 && inputState(w, "eb_relay_fb") == 1
              && inputState(w, "traction_cutoff_fb") == 1, "NB alone: NB FB 10, EB FB 01, traction cut off");

        // Off: the frames change nothing.
        w.setScenariosOn(false);
        w.feedBytes(outputData(13, 0x42, 0x7C00, WireFormat()));
        CHECK(inputState(w, "nb_relay_fb") == 2, "rules off: NB FB stays 10");
        w.setScenariosOn(true);
        CHECK(inputState(w, "nb_relay_fb") == 1, "back on: set from what holds now, 01");

        // A mode rule: FS -> cab1_active 0, otherwise left.
        Rule fs;
        fs.when = QStringLiteral("mode:4");
        fs.input = QStringLiteral("cab1_active");
        fs.value = 0;
        fs.otherwise = -1;
        QVector<Rule> r = w.rules();
        r << fs;
        w.setRules(r);
        setInput(w, "cab1_active", 1);
        w.setLocoMode(6);
        CHECK(inputState(w, "cab1_active") == 1, "OS: cab1_active left at 1");
        w.setLocoMode(4);
        CHECK(inputState(w, "cab1_active") == 0, "FS: cab1_active 0");
        setInput(w, "cab1_active", 1);
        w.setLocoMode(6);
        CHECK(inputState(w, "cab1_active") == 1, "out of FS with 'leave as is': stays as clicked");

        const QString dmi = realDmiLine();
        CHECK(!dmi.isEmpty(), "replay: a DMI frame with a mode");
        w.observeDmiLine(dmi);
        CHECK(w.locoMode() == dmiStateFromLine(dmi).mode, "the mode comes from a real DMI frame");

        // Saved per cab.
        w.close();
    }
    {
        CabIoSimWindow w(nullptr);
        w.setCab(Cab::Sifa);
        CHECK(w.rules().size() == 10 && w.rules().last().when == QLatin1String("mode:4"), "the edited rules come back");
        w.setCab(Cab::Old);
        CHECK(w.rules().size() == 6, "the old cab has its own six");
        w.feedBytes(outputData(13, 1, 0x0080, WireFormat()));
        CHECK(inputState(w, "eb_command_fdbk") == 1 && inputState(w, "eb_pressure_fdbk") == 1
              && inputState(w, "nb_command_fdbk") == 0, "old cab EB: its command and pressure FBs 1, NB's 0");
        w.setCab(Cab::Sifa);
    }
    clearSaved();
#endif
}
