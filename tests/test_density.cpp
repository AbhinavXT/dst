#include "testutil.h"
#include "settings.h"

// Row density. Small surface, but the pairing of height with wrap is a
// deliberate constraint rather than a coincidence, and it is exactly the
// kind of thing a later "let's make wrap independent" change would break
// without noticing.
TEST_SUITE(density)
{
    const int original = Settings::rowDensity();

    // ---- heights ---------------------------------------------------------
    CHECK(Settings::rowHeightFor(0) == 22, "compact is 22px");
    CHECK(Settings::rowHeightFor(1) == 30, "normal is 30px");
    CHECK(Settings::rowHeightFor(2) == 44, "comfortable is 44px (the old default)");
    CHECK(Settings::rowHeightFor(0) < Settings::rowHeightFor(1),
          "heights increase with density level");
    CHECK(Settings::rowHeightFor(1) < Settings::rowHeightFor(2),
          "and keep increasing");

    // Out-of-range levels must fall back to Normal rather than returning a
    // nonsense height that would collapse or explode every row.
    CHECK(Settings::rowHeightFor(-1) == 30, "negative level falls back to normal");
    CHECK(Settings::rowHeightFor(99) == 30, "too-large level falls back to normal");

    // ---- wrap pairing ----------------------------------------------------
    CHECK(!Settings::wordWrapFor(0), "compact does not wrap (only one line fits)");
    CHECK(!Settings::wordWrapFor(1), "normal does not wrap");
    CHECK(Settings::wordWrapFor(2),  "comfortable wraps");
    // The invariant that makes the single control honest: wrap is only ever
    // enabled where more than one line of text can actually be shown.
    for (int d = 0; d <= 2; ++d) {
        const bool roomForTwoLines = Settings::rowHeightFor(d) >= 40;
        CHECK(Settings::wordWrapFor(d) == roomForTwoLines,
              "wrap is enabled exactly when the row is tall enough for it");
    }

    // ---- persistence and clamping ----------------------------------------
    for (int d = 0; d <= 2; ++d) {
        Settings::setRowDensity(d);
        CHECK(Settings::rowDensity() == d, "density round-trips");
    }
    Settings::setRowDensity(-5);
    CHECK(Settings::rowDensity() == 0, "negative density clamps to compact");
    Settings::setRowDensity(17);
    CHECK(Settings::rowDensity() == 2, "excessive density clamps to comfortable");

    Settings::setRowDensity(original);
    CHECK(Settings::rowDensity() == original, "restored the original setting");
}
