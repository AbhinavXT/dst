#include "testutil.h"
#include "layoutaudit.h"

#include "biuprotocol.h"
#include "biusim.h"
#include "brakebenchpreset.h"
#include "brakebenchwindow.h"
#include "brakeplant.h"
#include "settings.h"
#include "theme.h"
#include "uistyle.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QSettings>
#include <QToolButton>

#include <cmath>

// =============================================================================
//  Session 362 — the Brake Test Bench's saved presets.
//
//  docs/BRAKE_BENCH_NEXT.md item 5: "Save the bench's fault setup under a
//  name and load it later: plant faults, rates, analog offsets, forced bits,
//  manual pressures, validity modes, queued codes, frames off, silence ...
//  Opening the window still starts clean; a preset is loaded only on
//  request."
// =============================================================================

using namespace Biu;

namespace {

bool near362(double a, double b) { return std::fabs(a - b) < 1e-6; }

void clearSaved362()
{
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    s.remove(QStringLiteral("brakebench"));
}

// "EB; BC sticks at 1.0; the BIU says BP manual 3.0, MR not valid; isolated;
// 0x1004 and a display code queued; the discretes frame off".
void setUpFaults(BrakeBenchWindow &w)
{
    w.setRates(BrakePlant::Bp, 3.0, 0.4);
    w.setPlantFault(BrakePlant::Bc, BrakePlant::Fault::Stuck, 0);
    w.setPlantFault(BrakePlant::Mr, BrakePlant::Fault::Leak, 0.3);
    w.setAnalogOffset(BrakePlant::Bp, -0.25);
    w.biu()->setPressureManual(Bp, true, 3.0);
    w.biu()->setValidMode(Mr, BiuSimulator::ValidMode::Invalid);
    w.biu()->setBitMode(1, StManualIsolate, BiuSimulator::BitMode::On);
    w.biu()->setBitMode(3, 2, BiuSimulator::BitMode::Off);
    w.biu()->raiseFault(0x1004);
    w.biu()->raiseFault(0x1040);
    w.biu()->raiseDisplay(displayCodes().first().code);
    w.biu()->setFrameOn(2, false);
}

}  // namespace

TEST_SUITE(session362)
{
    // ---- the data: JSON both ways --------------------------------------------------------------------
    {
        BenchPreset clean;
        BenchPreset p;
        p.air[BrakePlant::Bc].fault = int(BrakePlant::Fault::Slow);
        p.air[BrakePlant::Bc].param = 5;
        p.air[BrakePlant::Bp].offset = 0.4;
        p.air[BrakePlant::Mr].apply = 0.7;
        p.pressure[A9Ref].manual = true;
        p.pressure[A9Ref].kg = 3.45;
        p.pressure[Bc].valid = 2;
        p.bits[0][3] = 1;
        p.bits[2][7] = 2;
        p.faults = { 0x1004, 0x1040 };
        p.displays = { 0x2001 };
        p.frameOn[1] = false;
        p.silent = true;
        const QJsonObject j = p.toJson();
        const BenchPreset back = BenchPreset::fromJson(j, clean);
        CHECK(back.toJson() == j, "JSON round trip: the same document back");
        CHECK(back.air[BrakePlant::Bc].fault == int(BrakePlant::Fault::Slow) && near362(back.air[BrakePlant::Bc].param, 5)
                  && near362(back.air[BrakePlant::Bp].offset, 0.4) && near362(back.air[BrakePlant::Mr].apply, 0.7),
              "plant faults, offsets and rates");
        CHECK(back.pressure[A9Ref].manual && near362(back.pressure[A9Ref].kg, 3.45) && back.pressure[Bc].valid == 2,
              "manual pressures and validity");
        CHECK(back.bits[0][3] == 1 && back.bits[2][7] == 2 && back.bits[1][0] == 0, "forced bits");
        CHECK(back.faults == QVector<quint16>({ 0x1004, 0x1040 }) && back.displays == QVector<quint16>({ 0x2001 }),
              "queued codes, in order");
        CHECK(!back.frameOn[1] && back.frameOn[0] && back.silent, "frames off and silence");
        CHECK(j.value(QStringLiteral("air")).toObject().value(QStringLiteral("bc")).toObject().value(QStringLiteral("fault")).toString()
                      == QLatin1String("slow")
                  && j.value(QStringLiteral("bits")).toArray().at(0).toString() == QLatin1String("AAA1AAAA")
                  && j.value(QStringLiteral("faults")).toArray().at(0).toString() == QLatin1String("0x1004")
                  && j.value(QStringLiteral("frames")).toObject().value(QStringLiteral("0x320")).toBool(true) == false,
              "readable keys: \"slow\", \"AAA1AAAA\", \"0x1004\", \"0x320\"");

        // Missing keys take the defaults; unknown ones are skipped.
        BenchPreset defaults;
        defaults.air[BrakePlant::Bp].apply = 2.0;
        QJsonObject partial;
        partial.insert(QStringLiteral("silent"), true);
        partial.insert(QStringLiteral("fromTheFuture"), 42);
        const BenchPreset part = BenchPreset::fromJson(partial, defaults);
        CHECK(part.silent && near362(part.air[BrakePlant::Bp].apply, 2.0) && part.frameOn[2] && part.faults.isEmpty(),
              "a partial preset: what it says, the defaults for the rest");
        QJsonObject odd = p.toJson();
        odd.insert(QStringLiteral("faults"), QJsonArray({ QStringLiteral("0x1004"), QStringLiteral("nope"), 0, QStringLiteral("0x10000") }));
        CHECK(BenchPreset::fromJson(odd, clean).faults == QVector<quint16>({ 0x1004 }), "a code that is not one is dropped");

        const QStringList diff = p.differences(clean);
        CHECK(diff.contains(QStringLiteral("BC slow ×5")) && diff.contains(QStringLiteral("A9 ref manual 3.45"))
                  && diff.contains(QStringLiteral("2 bits forced")) && diff.contains(QStringLiteral("fault 0x1004 queued"))
                  && diff.contains(QStringLiteral("0x320 off")) && diff.contains(QStringLiteral("silent")),
              qPrintable(QStringLiteral("what differs, in words: %1").arg(diff.join(QStringLiteral(" | ")))));
        CHECK(clean.differences(clean).isEmpty(), "nothing differs from itself");
    }

    // ---- the store ----------------------------------------------------------------------------------
    clearSaved362();
    {
        CHECK(BenchPreset::names().isEmpty(), "no presets to start with");
        BenchPreset a, b;
        b.silent = true;
        BenchPreset::save(QStringLiteral("zeta"), a);
        BenchPreset::save(QStringLiteral("BC stuck / at 1.0"), b);
        BenchPreset::save(QStringLiteral("alpha"), a);
        CHECK(BenchPreset::names() == QStringList({ QStringLiteral("alpha"), QStringLiteral("BC stuck / at 1.0"), QStringLiteral("zeta") }),
              "names sorted, case aside; a slash in a name is fine");
        BenchPreset got;
        CHECK(BenchPreset::load(QStringLiteral("BC stuck / at 1.0"), a, &got) && got.silent, "loads by name");
        CHECK(!BenchPreset::load(QStringLiteral("nope"), a, &got), "an unknown name: false");
        BenchPreset::remove(QStringLiteral("zeta"));
        CHECK(!BenchPreset::exists(QStringLiteral("zeta")) && BenchPreset::names().size() == 2, "removed");
    }
    clearSaved362();

    // ---- the window ---------------------------------------------------------------------------------
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    {
        BrakeBenchWindow w(nullptr);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        qint64 t = 1000;
        w.setClock([&t]() { return t; });
        w.biu()->setSendSink([](const QByteArray &) {});
        const BenchPreset clean = w.cleanPreset();
        CHECK(w.currentPreset().toJson() == clean.toJson() && w.activePreset().isEmpty(), "opens clean, no preset");

        setUpFaults(w);
        const BenchPreset set = w.currentPreset();
        CHECK(set.air[BrakePlant::Bc].fault == int(BrakePlant::Fault::Stuck) && set.pressure[Bp].manual
                  && set.bits[0][StManualIsolate] == 1 && set.faults.size() == 2 && !set.frameOn[2],
              "currentPreset reads what is set");
        CHECK(w.savePreset(QStringLiteral("EB, BC stuck")) && w.activePreset() == QLatin1String("EB, BC stuck"), "saved");
        CHECK(!w.savePreset(QStringLiteral("  ")), "a blank name is refused");

        w.startClean();
        CHECK(w.currentPreset().toJson() == clean.toJson(), "Start clean: everything back");
        CHECK(w.plant().fault(BrakePlant::Bc) == BrakePlant::Fault::None && !w.biu()->isolated()
                  && w.biu()->faultQueue().isEmpty() && w.biu()->frameOn(2),
              "the plant, the isolation, the codes and the frames with it");

        CHECK(w.loadPreset(QStringLiteral("EB, BC stuck")), "loaded");
        CHECK(w.currentPreset().toJson() == set.toJson(), "the bench is set as it was saved");
        CHECK(w.biu()->isolated() && w.plant().fault(BrakePlant::Bc) == BrakePlant::Fault::Stuck
                  && near362(w.plant().faultParam(BrakePlant::Mr), 0.3) && near362(w.plant().applyRate(BrakePlant::Bp), 3.0),
              "the model follows, not just the widgets");
        const Status s = w.biu()->buildStatus(t);
        CHECK(s.faultCode == 0x1004 && !s.valid[Mr] && s.raw[Bp] == 60, "and it is what the BIU sends");
        CHECK(!w.loadPreset(QStringLiteral("not there")) && w.currentPreset().toJson() == set.toJson(),
              "an unknown name changes nothing");

        // Loading sets every field: what was set before does not leak in.
        w.startClean();
        w.biu()->setSilent(true);
        w.biu()->setPressureManual(FeedPipe, true, 1.0);
        BenchPreset other = clean;
        other.air[BrakePlant::Bp].fault = int(BrakePlant::Fault::Slow);
        other.air[BrakePlant::Bp].param = 4;
        BenchPreset::save(QStringLiteral("BP slow"), other);
        w.loadPreset(QStringLiteral("BP slow"));
        CHECK(!w.biu()->silent() && !w.biu()->pressureManual(FeedPipe) && w.plant().fault(BrakePlant::Bp) == BrakePlant::Fault::Slow,
              "a preset replaces the setup, it does not add to it");

        // The menu.
        auto *btn = w.findChild<QToolButton *>(QStringLiteral("benchPresets"));
        CHECK(btn && btn->text().contains(QLatin1String("BP slow")), "the button names the loaded preset");
        if (btn && btn->menu()) {
            emit btn->menu()->aboutToShow();
            QStringList texts;
            for (QAction *a : btn->menu()->actions()) texts << a->text();
            CHECK(texts.contains(QStringLiteral("EB, BC stuck")) && texts.contains(QStringLiteral("BP slow"))
                      && texts.contains(QStringLiteral("Start clean")),
                  qPrintable(QStringLiteral("the menu lists both presets and Start clean (%1)").arg(texts.join(QStringLiteral(" | ")))));
        }
        w.deletePreset(QStringLiteral("BP slow"));
        CHECK(!BenchPreset::exists(QStringLiteral("BP slow")) && w.activePreset().isEmpty(), "deleted, and no longer named");

        // Reopening still starts clean, the presets kept.
        w.loadPreset(QStringLiteral("EB, BC stuck"));
        w.close();
        BrakeBenchWindow w2(nullptr);
        w2.setAttribute(Qt::WA_DeleteOnClose, false);
        CHECK(w2.plant().fault(BrakePlant::Bc) == BrakePlant::Fault::None && !w2.biu()->isolated() && w2.biu()->faultQueue().isEmpty()
                  && w2.activePreset().isEmpty(),
              "reopened: clean, no preset loaded");
        CHECK(BenchPreset::exists(QStringLiteral("EB, BC stuck")), "and the preset is still there to load");

        w2.show();
        const QStringList loose = LayoutAudit::orphans(&w2);
        CHECK(loose.isEmpty(), qPrintable(QStringLiteral("no loose widget (%1)").arg(loose.join(QStringLiteral(", ")))));
        w2.close();
    }
    clearSaved362();
}
