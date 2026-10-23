#include "testutil.h"

#include "clockskewalarm.h"
#include "fieldcatalog.h"
#include "mainwindow.h"
#include "soundalerts.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMenuBar>

// =============================================================================
//  Session 341 — View ▸ Sounds: the system sound when a source goes offline,
//  when the loco and station clocks fall out of step, and on an error
//  notification. Each off until ticked; at most one sound every two seconds.
// =============================================================================

namespace {
int g_sounds = 0;
void countSound() { ++g_sounds; }

QMenu *soundsMenu(MainWindow &w)
{
    for (QAction *top : w.menuBar()->actions()) {
        if (!top->menu() || top->text().remove(QLatin1Char('&')) != QLatin1String("View")) continue;
        for (QAction *a : top->menu()->actions())
            if (a->menu() && a->text().remove(QLatin1Char('&')) == QLatin1String("Sounds")) return a->menu();
    }
    return nullptr;
}
}  // namespace

TEST_SUITE(session341)
{
    using namespace SoundAlerts;
    const Event all[] = { Event::SourceOffline, Event::ClockSkew, Event::ErrorNote };
    for (Event e : all) setEnabled(e, false);
    resetQuiet();
    SoundAlerts::setSounder(&countSound);

    // ---- the switches and the quiet time ---------------------------------------------
    CHECK(!ring(Event::ErrorNote, 1000) && g_sounds == 0, "off (the default): no sound");
    setEnabled(Event::ErrorNote, true);
    CHECK(enabled(Event::ErrorNote) && !enabled(Event::ClockSkew), "each event is its own switch");
    CHECK(ring(Event::ErrorNote, 1000) && g_sounds == 1, "on: one sound");
    CHECK(!ring(Event::ErrorNote, 1000 + kQuietMs - 1) && g_sounds == 1, "a second within two seconds: none");
    CHECK(ring(Event::ErrorNote, 1000 + kQuietMs) && g_sounds == 2, "two seconds on: again");
    setEnabled(Event::ErrorNote, false);

    // ---- the window ---------------------------------------------------------------------
    const bool catalogWasLoaded = FieldCatalog::instance().isLoaded();
    [&]() {
        MainWindow w;
        QMenu *m = soundsMenu(w);
        CHECK(m, "View > Sounds");
        if (!m) return;
        QStringList titles;
        for (QAction *a : m->actions()) if (a->isCheckable()) titles << a->text();
        CHECK(titles == QStringList({ title(Event::SourceOffline), title(Event::ClockSkew), title(Event::ErrorNote) }),
              "one tick box per event");
        QAction *skew = nullptr;
        for (QAction *a : m->actions()) if (a->text() == title(Event::ClockSkew)) skew = a;
        CHECK(skew && !skew->isChecked(), "unticked to start");
        if (!skew) return;
        skew->trigger();
        CHECK(enabled(Event::ClockSkew), "ticking it switches it on, in the INI");

        // The clocks out of step for longer than the alarm's hold: one sound.
        auto *alarm = w.findChild<ClockSkewAlarm *>();
        CHECK(alarm, "fixture: the window's clock-skew alarm");
        if (!alarm) return;
        resetQuiet();
        g_sounds = 0;
        alarm->sample(10'000, true, 12);
        alarm->sample(14'000, true, 12);
        QApplication::processEvents();
        CHECK(alarm->isRaised() && g_sounds == 1, "the clocks 12 s apart for 4 s: the alarm is raised, with a sound");
        skew->trigger();
        CHECK(!enabled(Event::ClockSkew), "unticking switches it off");
    }();
    if (!catalogWasLoaded) FieldCatalog::instance().clear();
    for (Event e : all) setEnabled(e, false);
    SoundAlerts::setSounder(nullptr);
}
