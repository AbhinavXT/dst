#include "testutil.h"
#include "namemap.h"

#include <QFile>
#include <QTemporaryDir>

// friendly_names.csv parsing. Real files are written by spreadsheets, which
// pad every row to the widest one — so trailing commas are the norm, not an
// error, and a name that keeps one leaks into every log line, tab title and
// saved filename.
TEST_SUITE(namemap_csv)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp dir");
    const QString path = tmp.path() + "/friendly_names.csv";

    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write("# comment line,,\n"
            "21_1,L1_V1,\n"            // spreadsheet-padded: the real case
            "21_2,L2_V1,,\n"           // padded twice
            "33_1,Loco 33 VCC\n"       // no padding
            "0x22_3,Hex Keyed,\n"      // hex source id, padded
            "99_7, Spaced Name ,\n"    // whitespace around the name
            "\n"                        // blank line
            ",,\n");                    // empty key and name
    f.close();

    NameMap nm;
    CHECK(nm.loadFromFile(path), "csv loads");

    CHECK(nm.lookupByKey("21_1") == "L1_V1",
          "trailing comma stripped — was 'L1_V1,'");
    CHECK(nm.lookupByKey("21_2") == "L2_V1",
          "two trailing commas stripped");
    CHECK(nm.lookupByKey("33_1") == "Loco 33 VCC",
          "unpadded row is unaffected");
    CHECK(nm.lookupByKey("99_7") == "Spaced Name",
          "surrounding whitespace trimmed, inner space kept");
    CHECK(nm.lookupByKey("34_3") == "Hex Keyed",
          "hex source id still resolves (0x22 = 34)");

    // A name containing a comma cannot survive this format; the first comma
    // wins. Pinned so the behaviour is a decision rather than a surprise.
    CHECK(!nm.lookupByKey("21_1").contains(','),
          "no comma ever appears in a resolved name");

    // Unknown keys fall back to the key itself, which is what the UI shows
    // for sources the CSV does not cover.
    CHECK(nm.lookupByKey("77_9") == "77_9", "unknown key falls back to itself");
}
