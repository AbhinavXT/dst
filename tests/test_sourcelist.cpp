#include "testutil.h"
#include "namemap.h"

// Source sidebar filtering. The list itself is widget plumbing; this is the
// decision it makes, and the one that would quietly regress.
TEST_SUITE(sourcelist)
{
    // Empty filter shows everything — including sources with no name at all.
    CHECK(NameMap::matchesFilter("L2_V1", "33_1", ""),      "empty filter matches");
    CHECK(NameMap::matchesFilter("L2_V1", "33_1", "   "),   "whitespace-only matches");
    CHECK(NameMap::matchesFilter("", "33_1", ""),           "unnamed source matches empty filter");

    // Friendly name.
    CHECK(NameMap::matchesFilter("Loco 2 VCC", "33_1", "Loco"),  "prefix of name");
    CHECK(NameMap::matchesFilter("Loco 2 VCC", "33_1", "VCC"),   "suffix of name");
    CHECK(NameMap::matchesFilter("Loco 2 VCC", "33_1", "2 V"),   "substring with a space");
    CHECK(NameMap::matchesFilter("Loco 2 VCC", "33_1", "loco"),  "case-insensitive");
    CHECK(NameMap::matchesFilter("Loco 2 VCC", "33_1", "  Loco  "), "filter is trimmed");

    // Raw key — the case that matters for sources the CSV does not cover.
    CHECK(NameMap::matchesFilter("Loco 2 VCC", "33_1", "33_1"),  "full key");
    CHECK(NameMap::matchesFilter("Loco 2 VCC", "33_1", "33"),    "key prefix");
    CHECK(NameMap::matchesFilter("Loco 2 VCC", "33_1", "_1"),    "key suffix");
    CHECK(NameMap::matchesFilter("", "99_7", "99"),
          "unnamed source is still reachable by key");

    // Non-matches.
    CHECK(!NameMap::matchesFilter("Loco 2 VCC", "33_1", "zzz"),  "unrelated text");
    CHECK(!NameMap::matchesFilter("Loco 2 VCC", "33_1", "44"),   "different key");
    CHECK(!NameMap::matchesFilter("Loco 2 VCC", "33_1", "VCD"),  "near-miss on name");

    // A key-shaped filter must not accidentally match a different source
    // whose name happens to contain the digits.
    CHECK(NameMap::matchesFilter("Track 33 sensor", "21_2", "33"),
          "digits in the NAME do match — this is substring search, not exact");
    CHECK(!NameMap::matchesFilter("Track 33 sensor", "21_2", "33_"),
          "but a key-shaped filter does not match that name");
}
