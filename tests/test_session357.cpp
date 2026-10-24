#include "testutil.h"

#include "cabio.h"
#include "settings.h"

#ifdef DL_HAVE_SERIAL
#include "cabiosimwindow.h"
#endif

#include <QFile>
#include <QSettings>

// =============================================================================
//  Session 357 — Cab I/O Simulator: brake pressures ramp on the analog card.
//
//  Abhinav: "Ramp, but make the values configurable from the ui for nb, fsb
//  and eb for mr, bp and bc pressure inputs." BP, BC, MR (analog channels
//  1-3) ramp to the values set for the strongest brake applied.
//
//  Real frame: the bench capture's output data 0x03E1 (every SIFA brake bit
//  0: EB applied, and so the EB values), tests/fixtures/cabio_frames_20261023.txt.
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

void clearSaved()
{
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("cabio"));
    for (const QString &k : s.childKeys())
        if (k.startsWith(QLatin1String("brakeKg_")) || k.startsWith(QLatin1String("analog")) || k == QLatin1String("rampRate")
            || k == QLatin1String("brakePressures") || k.startsWith(QLatin1String("rules_")) || k == QLatin1String("cab"))
            s.remove(k);
}

bool near(double a, double b) { return qAbs(a - b) < 0.02; }

#ifdef DL_HAVE_SERIAL
double kg(CabIoSimWindow &w, int ch)
{
    return currentToPressure(w.analogMa(ch), w.analogScale(ch)).kgcm2;
}
#endif

}  // namespace

TEST_SUITE(session357)
{
    // ---- no window ------------------------------------------------------------------------------
    // SIFA brakes act on 0: bits 10-13 all 1 is no brake.
    const quint16 none = 0x3C00;
    auto apply = [](quint16 w, int bit) { return quint16(w & ~(1u << bit)); };
    CHECK(brakeLevel(Cab::Sifa, none) == NoBrake, "SIFA, every brake bit 1: no brake");
    CHECK(brakeLevel(Cab::Sifa, apply(none, 12)) == NbApplied, "NB bit 12 = 0: NB");
    CHECK(brakeLevel(Cab::Sifa, apply(apply(none, 12), 11)) == FsbApplied, "NB and FSB: FSB wins");
    CHECK(brakeLevel(Cab::Sifa, 0x03E1) == EbApplied, "real 0x03E1, all applied: EB wins");
    CHECK(brakeLevel(Cab::Sifa, apply(none, 10)) == NoBrake, "LE alone: not a pressure level");
    CHECK(brakeLevel(Cab::Old, 0x0040) == FsbApplied && brakeLevel(Cab::Old, 0) == NoBrake, "old cab acts on 1");
    CHECK(rampToward(5.0, 3.5, 0.1) == 4.9 && rampToward(3.55, 3.5, 0.1) == 3.5 && rampToward(0, 2, 0.5) == 0.5,
          "the ramp moves at most a step and lands on the value");
    CHECK(defaultBrakePressure(NoBrake, 0) == 5.0 && defaultBrakePressure(EbApplied, 0) == 0.0, "BP 5.0 released, 0 on EB");

#ifdef DL_HAVE_SERIAL
    // ---- the window -----------------------------------------------------------------------------
    clearSaved();
    {
        CabIoSimWindow w(nullptr);
        w.setWriteSink([](const QByteArray &) {});
        w.setCab(Cab::Sifa);
        CHECK(w.brakePressuresOn() && w.brakeLevelNow() == -1, "on, and no level before output data");
        CHECK(near(kg(w, 0), 5.0) && near(kg(w, 1), 0.0) && near(kg(w, 2), 9.0), "opens at BP 5.0, BC 0, MR 9.0");

        w.setBrakePressure(FsbApplied, 0, 3.4);
        w.setBrakePressure(FsbApplied, 1, 2.0);
        w.setBrakePressure(FsbApplied, 2, 8.5);
        w.setRampRate(1.0);
        w.feedBytes(outputData(13, 1, quint16(apply(none, 11)), WireFormat()));
        CHECK(w.brakeLevelNow() == FsbApplied && w.ramping(), "FSB applied: ramping");
        w.stepRamp(0.5);
        CHECK(near(kg(w, 0), 4.5) && near(kg(w, 1), 0.5) && near(kg(w, 2), 8.5), "after 0.5 s at 1/s: BP 4.5, BC 0.5, MR 8.5");
        w.stepRamp(1.0);
        CHECK(near(kg(w, 0), 3.5) && near(kg(w, 1), 1.5), "after 1.5 s: BP 3.5, BC 1.5");
        w.stepRamp(5.0);
        CHECK(near(kg(w, 0), 3.4) && near(kg(w, 1), 2.0) && !w.ramping(), "lands on the FSB values and stops");

        // The same brake again: a pressure set by hand stands.
        w.setAnalogPressure(0, 4.0);
        w.feedBytes(outputData(13, 2, quint16(apply(none, 11)), WireFormat()));
        CHECK(!w.ramping() && near(kg(w, 0), 4.0), "same brake, next frame: the hand-set BP stands");

        // The real frame: EB.
        w.feedBytes(realOutputData());
        CHECK(w.brakeLevelNow() == EbApplied && w.ramping(), "real 0x03E1: EB, ramping");
        w.stepRamp(10.0);
        CHECK(near(kg(w, 0), w.brakePressure(EbApplied, 0)) && near(kg(w, 1), w.brakePressure(EbApplied, 1)),
              "lands on the EB values");

        // Editing the value in force re-ramps to it.
        w.setBrakePressure(EbApplied, 1, 3.0);
        CHECK(w.ramping(), "EB's BC edited while EB applied: ramps again");
        w.stepRamp(10.0);
        CHECK(near(kg(w, 1), 3.0), "BC 3.0");

        // Released.
        w.feedBytes(outputData(13, 3, none, WireFormat()));
        w.stepRamp(10.0);
        CHECK(w.brakeLevelNow() == NoBrake && near(kg(w, 0), 5.0) && near(kg(w, 1), 0.0), "released: BP 5.0, BC 0");

        // Off: frames move nothing.
        w.setBrakePressuresOn(false);
        w.feedBytes(outputData(13, 4, quint16(apply(none, 13)), WireFormat()));
        CHECK(!w.ramping() && near(kg(w, 0), 5.0), "off: EB applied, BP stays 5.0");
        w.setBrakePressuresOn(true);
        CHECK(w.ramping() && w.brakeLevelNow() == EbApplied, "back on: ramps to what is applied now");
        w.close();
    }
    {
        CabIoSimWindow w(nullptr);
        CHECK(near(w.brakePressure(FsbApplied, 0), 3.4) && near(w.brakePressure(FsbApplied, 2), 8.5)
              && near(w.brakePressure(EbApplied, 1), 3.0), "the values come back when the window opens again");
    }
    clearSaved();
#endif
}
