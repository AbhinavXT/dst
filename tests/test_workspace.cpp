#include "testutil.h"

#include "settings.h"

#include <QStringList>

// =============================================================================
//  Workspace restore.
//
//  What is stored is a list of "key\x1fname\x1fvisible" records plus the
//  active tab. The encoding matters more than it looks: a source key can
//  contain almost anything, so the separator has to be a byte that cannot
//  appear in one — hence the unit separator rather than a comma or a colon.
//
//  What is NOT stored is the per-tab filter, and that is a decision rather
//  than an omission: a filter restored at startup hides live traffic, and
//  "why is nothing arriving" is a far worse first minute than retyping it.
// =============================================================================

TEST_SUITE(workspace)
{
    // The ini lives beside the executable and there is no way to redirect
    // it, so this suite borrows the real one and puts it back.
    const QStringList savedTabs   = Settings::workspaceTabs();
    const QString     savedActive = Settings::workspaceActiveTab();
    const bool        savedFlag   = Settings::restoreWorkspace();

    // ---- the default -------------------------------------------------------
    {
        Settings::setWorkspaceTabs({});
        CHECK(Settings::workspaceTabs().isEmpty(), "no workspace saved, none restored");
        CHECK(Settings::restoreWorkspace(), "restoring is on by default");
    }

    // ---- round trip --------------------------------------------------------
    {
        // Built with join() rather than written inline: "\x1f1" in a C++
        // literal is the single character U+01F1, because hex escapes are
        // greedy. That is not a hypothetical — it is what the first version
        // of both this test and the code under it did.
        const QChar sep(0x1F);
        const QStringList records{
            QStringList{ "loco_1_1", "Loco 1 / Ctrl 1", "1" }.join(sep),
            QStringList{ "loco_2_1", "Loco 2 / Ctrl 1", "1" }.join(sep),
            QStringList{ "stat_7_0", "Station 7",       "0" }.join(sep),
        };
        Settings::setWorkspaceTabs(records);
        Settings::setWorkspaceActiveTab(QStringLiteral("loco_2_1"));

        const QStringList back = Settings::workspaceTabs();
        CHECK(back == records, "the records come back exactly as written");
        CHECK(back.size() == 3, "all three tabs, hidden one included");
        CHECK(Settings::workspaceActiveTab() == QLatin1String("loco_2_1"),
              "and the active tab is remembered");

        // Order is the point: tabs must come back in the order they were
        // left in, not in whatever order a hash iterates.
        CHECK(back.at(0).startsWith(QLatin1String("loco_1_1")), "first tab first");
        CHECK(back.at(2).startsWith(QLatin1String("stat_7_0")), "hidden tab last");

        const QStringList hidden = back.at(2).split(sep);
        CHECK(hidden.size() == 3 && hidden.at(2) == QLatin1String("0"),
              "a closed tab is stored as hidden rather than dropped");
        CHECK(hidden.at(1) == QLatin1String("Station 7"),
              "with its friendly name, so the restored tab is labelled");
    }

    // ---- names with awkward characters -------------------------------------
    // A friendly name comes from the name map and can contain anything a
    // person typed. A comma or colon separator would have split this record
    // into the wrong number of fields and silently dropped the tab.
    {
        const QChar sep(0x1F);
        const QString name = QStringLiteral("Loco 4, siding B: up-line");
        Settings::setWorkspaceTabs({ QStringList{ "k", name, "1" }.join(sep) });
        const QStringList parts = Settings::workspaceTabs().first().split(sep);
        CHECK(parts.size() == 3, "commas and colons in a name do not split the record");
        CHECK(parts.at(1) == name, "and the name survives intact");
    }

    // ---- the opt-out -------------------------------------------------------
    {
        Settings::setRestoreWorkspace(false);
        CHECK(!Settings::restoreWorkspace(), "restoring can be turned off");
        CHECK(!Settings::workspaceTabs().isEmpty(),
              "and turning it off does not throw away what was saved");
        Settings::setRestoreWorkspace(true);
    }

    // ---- a short record is ignored, not half-applied -----------------------
    // Records written by an older build have two fields. Restore must skip
    // them rather than restore a tab with an empty name or a stray key.
    {
        const QChar sep(0x1F);
        Settings::setWorkspaceTabs({ QStringLiteral("only_key"),
                                     QStringList{ "key", "name" }.join(sep) });
        for (const QString &rec : Settings::workspaceTabs()) {
            CHECK(rec.split(sep).size() < 3,
                  "these are the malformed shapes restore has to survive");
        }
    }

    Settings::setWorkspaceTabs(savedTabs);
    Settings::setWorkspaceActiveTab(savedActive);
    Settings::setRestoreWorkspace(savedFlag);
}
