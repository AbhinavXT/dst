#include "testutil.h"
#include "queryhistory.h"

// Query history and presets. The store is small, but its two policies —
// move-to-front dedup and the cap — are what stop it degrading into an
// unusable transcript, and both would fail silently.
TEST_SUITE(queryhistory)
{
    QueryHistory::clearRecent();
    for (const QueryPreset &p : QueryHistory::presets()) {
        QueryHistory::removePreset(p.name);
    }
    CHECK(QueryHistory::recent().isEmpty(),   "starts with no history");
    CHECK(QueryHistory::presets().isEmpty(),  "starts with no presets");

    // ---- recency ----------------------------------------------------------
    QueryHistory::remember("sev:error");
    QueryHistory::remember("src:33_1");
    CHECK(QueryHistory::recent().size() == 2, "two remembered");
    CHECK(QueryHistory::recent().first() == "src:33_1",
          "newest is first — the list is ordered by recency");

    // Re-running an old query promotes it rather than duplicating it.
    QueryHistory::remember("sev:error");
    CHECK(QueryHistory::recent().size() == 2, "no duplicate created");
    CHECK(QueryHistory::recent().first() == "sev:error",
          "re-run query moves to the front");
    CHECK(QueryHistory::recent().at(1) == "src:33_1", "the other slides down");

    // Blank input is what an empty box looks like; it must not enter the list.
    QueryHistory::remember("");
    QueryHistory::remember("   ");
    CHECK(QueryHistory::recent().size() == 2, "blank queries are ignored");

    // Whitespace is trimmed, so " x " and "x" are the same entry.
    QueryHistory::remember("  sev:warn  ");
    CHECK(QueryHistory::recent().first() == "sev:warn", "stored trimmed");
    QueryHistory::remember("sev:warn");
    CHECK(QueryHistory::recent().size() == 3,
          "trimmed and untrimmed forms are one entry");

    // ---- the cap ----------------------------------------------------------
    QueryHistory::clearRecent();
    for (int i = 0; i < QueryHistory::kMaxRecent + 15; ++i) {
        QueryHistory::remember(QStringLiteral("query %1").arg(i));
    }
    CHECK(QueryHistory::recent().size() == QueryHistory::kMaxRecent,
          "history is capped");
    CHECK(QueryHistory::recent().first() == "query 39",
          "newest survives the cap");
    CHECK(!QueryHistory::recent().contains("query 0"),
          "oldest is dropped, not the newest");

    // ---- presets ----------------------------------------------------------
    QueryHistory::savePreset("Link errors", "sev:error RAD");
    QueryHistory::savePreset("This loco",   "src:33_1");
    CHECK(QueryHistory::presets().size() == 2, "two presets saved");
    CHECK(QueryHistory::presets().at(0).name == "Link errors",
          "insertion order preserved");
    CHECK(QueryHistory::presets().at(0).query == "sev:error RAD",
          "query stored with the name");

    // Saving an existing name replaces rather than duplicating — otherwise
    // correcting a preset would leave the wrong one still in the list.
    QueryHistory::savePreset("Link errors", "sev:error RAD NOT \"No Error\"");
    CHECK(QueryHistory::presets().size() == 2, "no duplicate name");
    CHECK(QueryHistory::presets().at(0).query.contains("No Error"),
          "the preset was updated in place");
    QueryHistory::savePreset("LINK ERRORS", "changed again");
    CHECK(QueryHistory::presets().size() == 2,
          "name matching is case-insensitive");

    // An unnamed preset is not saveable — it could never be picked again.
    QueryHistory::savePreset("", "orphan");
    QueryHistory::savePreset("   ", "orphan");
    CHECK(QueryHistory::presets().size() == 2, "blank names are refused");

    // Removal.
    QueryHistory::removePreset("This loco");
    CHECK(QueryHistory::presets().size() == 1, "preset removed");
    CHECK(QueryHistory::presets().at(0).name == "Link errors", "the right one");
    QueryHistory::removePreset("nonexistent");
    CHECK(QueryHistory::presets().size() == 1, "removing a missing name is a no-op");

    // Presets and history are independent stores.
    QueryHistory::clearRecent();
    CHECK(QueryHistory::recent().isEmpty(),  "history cleared");
    CHECK(QueryHistory::presets().size() == 1,
          "clearing history leaves presets — they are a decision, not a scratchpad");

    // Round-trip through the INI: values with commas and quotes are exactly
    // what a query looks like, and QStringList storage must survive them.
    QueryHistory::savePreset("Tricky", "a,b \"quoted phrase\" NOT c");
    bool found = false;
    for (const QueryPreset &p : QueryHistory::presets()) {
        if (p.name == "Tricky") {
            found = p.query == "a,b \"quoted phrase\" NOT c";
        }
    }
    CHECK(found, "commas and quotes survive the round-trip");

    for (const QueryPreset &p : QueryHistory::presets()) {
        QueryHistory::removePreset(p.name);
    }
    QueryHistory::clearRecent();
}
