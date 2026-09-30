#include "testutil.h"
#include "commandpalette.h"

#include <QAction>
#include <QMenu>
#include <QMenuBar>

static bool fm(const char *hay, const char *needle) {
    int s = 0;
    return fuzzyMatch(QString::fromLatin1(hay), QString::fromLatin1(needle), &s);
}
static int score(const char *hay, const char *needle) {
    int s = -1;
    fuzzyMatch(QString::fromLatin1(hay), QString::fromLatin1(needle), &s);
    return s;
}

TEST_SUITE(commandpalette)
{
    // ---- subsequence matching --------------------------------------------
    CHECK(fm("Tools › Plot field over time", ""),      "empty needle matches all");
    CHECK(fm("Tools › Plot field over time", "plot"),  "plain substring");
    CHECK(fm("Tools › Plot field over time", "PLOT"),  "case-insensitive");
    CHECK(fm("Tools › Plot field over time", "pfot"),
          "initials match — the point of subsequence over substring");
    CHECK(fm("Tools › Plot field over time", "tpft"),  "spans the menu name too");
    CHECK(fm("View › Row density › Compact", "compact"), "submenu leaf");
    CHECK(!fm("Tools › Plot field over time", "zzz"),  "absent letters do not match");
    CHECK(!fm("Plot", "plott"),
          "needle longer than the match fails");
    CHECK(!fm("Plot field", "tolp"),
          "right letters in the wrong ORDER do not match");
    CHECK(fm("", ""),      "empty against empty");
    CHECK(!fm("", "x"),    "nothing matches in an empty haystack");

    // ---- ranking ----------------------------------------------------------
    // A tight run should beat the same letters scattered, or the useful
    // result sinks below noise.
    CHECK(score("Plot field", "plot") < score("Toggle Dark/Light", "plot")
          || !fm("Toggle Dark/Light", "plot"),
          "contiguous match outranks a scattered one");
    CHECK(score("Plot field over time", "plot")
          < score("Export › Plot something", "plot"),
          "an earlier match outranks a later one");
    CHECK(score("abc", "abc") == 0, "an exact prefix run scores zero");
    CHECK(score("xxabc", "abc") > 0, "a later match costs something");

    // ---- harvesting -------------------------------------------------------
    {
        QMenuBar bar;
        QMenu *file = bar.addMenu("&File");
        QAction *open = file->addAction("&Open recorded session…");
        open->setShortcut(QKeySequence("Ctrl+O"));
        file->addSeparator();
        QAction *quit = file->addAction("&Quit");

        QMenu *view = bar.addMenu("&View");
        QMenu *density = view->addMenu("Row &density");
        density->addAction("&Compact");
        density->addAction("&Normal");
        QAction *utc = view->addAction("Show times in &UTC");
        utc->setCheckable(true);

        const QVector<Command> cmds = harvestCommands(&bar);

        // Leaves only: 2 from File, 2 from the density submenu, 1 UTC.
        CHECK(cmds.size() == 5, "five leaf commands harvested");

        QStringList paths;
        for (const Command &c : cmds) paths << c.path;
        CHECK(paths.contains("File › Open recorded session…"),
              "path includes the menu name");
        CHECK(paths.contains("View › Row density › Compact"),
              "submenu path is nested, not flattened");
        CHECK(paths.contains("View › Show times in UTC"),
              "ampersands are stripped from the displayed path");

        // A submenu is not itself a command — triggering "Row density"
        // does nothing, and offering it would be a dead entry.
        bool hasBareSubmenu = false;
        for (const QString &p : paths) if (p == "View › Row density") hasBareSubmenu = true;
        CHECK(!hasBareSubmenu, "the submenu itself is not offered");

        // Separators must not become blank rows.
        for (const Command &c : cmds) {
            CHECK(!c.path.isEmpty(), "no empty command paths");
            CHECK(c.action != nullptr, "every command carries its action");
        }

        // Shortcuts come along, so the palette doubles as a key reference.
        bool foundShortcut = false;
        for (const Command &c : cmds) {
            if (c.path.contains("Open recorded")) {
                foundShortcut = !c.shortcut.isEmpty();
            }
        }
        CHECK(foundShortcut, "shortcut is captured with the command");

        // The harvested action must be the SAME object, or triggering it
        // from the palette would do nothing.
        for (const Command &c : cmds) {
            if (c.path.endsWith("Quit")) CHECK(c.action == quit,
                                               "action identity preserved");
        }
    }

    CHECK(harvestCommands(nullptr).isEmpty(), "null menu bar yields nothing");

    // An empty menu contributes nothing rather than a placeholder.
    {
        QMenuBar bar;
        bar.addMenu("&Empty");
        CHECK(harvestCommands(&bar).isEmpty(), "an empty menu yields no commands");
    }
}
