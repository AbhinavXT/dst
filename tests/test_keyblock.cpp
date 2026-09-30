#include "testutil.h"
#include "keyblock.h"

#include <QStringList>

namespace {
// The block exactly as captured from the loco.
QStringList realBlock()
{
    return QStringList{
        "AUTH_KEYS",
        "3B 0C 84 47 02 24 31 58 16 5D 96 49 F0 52 52 C3",
        "6A 2B 03 74 F1 9F F5 A3 D1 8F 1B 36 B0 53 8B F1",
        "F5 6C D7 34 EC C7 68 9C F2 99 7E FF DC 93 28 C8",
        "80 C5 00 F9 EC E4 EE F0 FE 7B E4 D2 99 FC E3 93",
        "KEY IDX: [0] START TIME: [26][5][24][0]",
        "KEY IDX: [0] END TIME: [26][9][1][0]",
        "KEY IDX: [1] START TIME: [26][5][24][0]",
        "KEY IDX: [1] END TIME: [26][9][11][0]",
        "KEY_IDX: [0] START: [1779580800] END:[1788220800] CURR:[1787569048]"
    };
}
QString valueOf(const QVector<FieldRow> &rows, const QString &name)
{
    for (const FieldRow &r : rows)
        if (r.field.trimmed() == name) return r.value;
    return QString();
}
}  // namespace

// AUTH_KEYS: two 16-byte keys per SET, sharing one validity window.
TEST_SUITE(keyblock)
{
    CHECK(KeyBlock::isBlockStart("AUTH_KEYS"), "marker recognised");
    CHECK(KeyBlock::isBlockStart("  AUTH_KEYS  "), "with whitespace");
    CHECK(!KeyBlock::isBlockStart("AUTH_KEY"), "a near-miss is not the marker");
    CHECK(!KeyBlock::isBlockStart("RTC [2026]"), "unrelated line");

    const KeyBlock::Result r = KeyBlock::parse(realBlock());
    CHECK(r.valid, "the captured block parses");

    // The structural point: four hex lines are TWO sets of two keys, not
    // four keys. Getting this wrong pairs every key with the wrong window.
    CHECK(r.sets.size() == 2, "four key lines make TWO sets, per KEY_SET_INFO");
    CHECK(r.sets[0].hasKeys() && r.sets[1].hasKeys(), "both sets have their keys");
    CHECK(r.sets[0].key0.size() == 16 && r.sets[0].key1.size() == 16,
          "each key is 16 bytes");

    CHECK(quint8(r.sets[0].key0.at(0))  == 0x3B, "set 0 key 0 first byte");
    CHECK(quint8(r.sets[0].key0.at(15)) == 0xC3, "set 0 key 0 last byte");
    CHECK(quint8(r.sets[0].key1.at(0))  == 0x6A, "set 0 key 1 is the SECOND line");
    CHECK(quint8(r.sets[1].key0.at(0))  == 0xF5, "set 1 key 0 is the THIRD line");
    CHECK(quint8(r.sets[1].key1.at(15)) == 0x93, "set 1 key 1 last byte");

    // KEY_TIME is [yy][mm][dd][hh].
    CHECK(r.sets[0].startY == 26 && r.sets[0].startM == 5
          && r.sets[0].startD == 24 && r.sets[0].startH == 0,
          "set 0 start decoded as yy/mm/dd/hh");
    CHECK(r.sets[0].endM == 9 && r.sets[0].endD == 1, "set 0 end");
    CHECK(r.sets[1].endD == 11, "set 1 ends on a different day — the sets differ");
    CHECK(r.sets[0].startText() == "2026-05-24 00:00", "readable start");
    CHECK(r.sets[0].endText()   == "2026-09-01 00:00", "readable end");

    // The epoch line and the [yy][mm][dd][hh] form must agree — that
    // agreement is what confirms the field order was read correctly.
    CHECK(r.sets[0].startEpoch == 1779580800, "epoch start from the trailing line");
    CHECK(r.sets[0].endEpoch   == 1788220800, "epoch end");
    CHECK(r.currentEpoch == 1787569048, "CURR captured");
    {
        const QDateTime d = QDateTime::fromSecsSinceEpoch(r.sets[0].startEpoch, Qt::UTC);
        CHECK(d.date().year() == 2026 && d.date().month() == 5 && d.date().day() == 24,
              "the epoch agrees with the printed [26][5][24] — field order confirmed");
    }

    // Set 1 had no epoch line, so it must be derived from the printed time.
    CHECK(r.sets[1].endEpoch > 0, "set 1 end epoch derived from yy/mm/dd/hh");
    CHECK(r.sets[1].endEpoch > r.sets[0].endEpoch, "and it is the later of the two");

    // Days remaining is the operationally useful number: key expiry is a
    // loco health fault, so it is worth knowing before it fires.
    {
        bool ok = false;
        const double d = r.daysRemaining(&ok);
        CHECK(ok, "days remaining computed");
        CHECK(d > 7.0 && d < 8.0, "about 7.5 days left on the captured block");
    }

    // ---- rendering --------------------------------------------------------
    {
        const QVector<FieldRow> rows = KeyBlock::describe(r);
        CHECK(!rows.isEmpty(), "rows produced");
        CHECK(valueOf(rows, "AUTH_KEYS").contains("2 key set"), "summary");
        CHECK(valueOf(rows, "set 0 valid_from") == "2026-05-24 00:00", "set 0 from");
        CHECK(valueOf(rows, "set 1 valid_to").contains("09-11"), "set 1 to");
        CHECK(valueOf(rows, "set 0 key 0").startsWith("3B 0C 84"), "key rendered as hex");
        const QString days = valueOf(rows, "key_days_remaining");
        CHECK(days.startsWith("7."), "days value is numeric-first, so it compares");
        CHECK(days.contains("expiring soon"),
              "7.5 days of a ~100-day set is flagged, not called ok");
    }

    // ---- robustness -------------------------------------------------------
    CHECK(!KeyBlock::parse({}).valid, "empty input is not a block");
    CHECK(!KeyBlock::parse({"RTC [2026]"}).valid, "unrelated line is not a block");
    {
        // Truncated: marker and one key line only. No complete SET, so
        // nothing is claimed — a half-read key pairing would be worse than
        // reporting none.
        const KeyBlock::Result t = KeyBlock::parse(
            { "AUTH_KEYS", "3B 0C 84 47 02 24 31 58 16 5D 96 49 F0 52 52 C3" });
        CHECK(!t.valid, "one key line does not make a set");
    }
    {
        // Times without keys still decode: the validity window is useful on
        // its own, and refusing it would lose information we have.
        const KeyBlock::Result t = KeyBlock::parse(
            { "AUTH_KEYS", "KEY IDX: [0] START TIME: [26][5][24][0]",
              "KEY IDX: [0] END TIME: [26][9][1][0]" });
        CHECK(t.valid, "a times-only block still parses");
        CHECK(t.sets.size() == 1 && !t.sets[0].hasKeys(), "with no keys");
        CHECK(valueOf(KeyBlock::describe(t), "set 0 keys") == "(not captured)",
              "and says so rather than showing blanks");
    }
    {
        // An expired block must report a negative number, not hide it.
        QStringList ln = realBlock();
        ln.last() = "KEY_IDX: [0] START: [1779580800] END:[1788220800] "
                    "CURR:[1888220800]";
        const KeyBlock::Result t = KeyBlock::parse(ln);
        bool ok = false;
        const double d = t.daysRemaining(&ok);
        CHECK(ok && d < 0, "an expired set reports negative days");
        CHECK(valueOf(KeyBlock::describe(t), "key_days_remaining").contains("EXPIRED"),
              "and is labelled EXPIRED");
    }
}
