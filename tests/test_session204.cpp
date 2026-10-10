#include "testutil.h"

#include "rfidcheck.h"
#include "rfidtag.h"

// =============================================================================
//  Session 204 — the Checks tab reads a route.xml / Configuration1.xml row's
//  own attributes the way the RFID simulator (LocoTcasSimulator) does. A
//  route.xml made by DLConsole is clean; each attribute is then broken by
//  hand, and the simulator's own Configuration1.xml (built in since 202) is
//  counted.
// =============================================================================

namespace {

QByteArray normal(int unique, qint64 loc, bool dup = false)
{
    return RfidTag::build({ { QStringLiteral("type"), 9 },       { QStringLiteral("version"), 1 },
                            { QStringLiteral("unique"), unique }, { QStringLiteral("abs_loc"), loc },
                            { QStringLiteral("tin_nom"), 84 },   { QStringLiteral("tin_rev"), 84 },
                            { QStringLiteral("duplication"), dup ? 1 : 0 } });
}

// 100 / 100D / 101 / 101D / 102 / 102D, written to route.xml and read back:
// rows as the simulator would read them.
RfidTag::Route fileRoute()
{
    RfidTag::Route r;
    r.name = QStringLiteral("T");
    r.dir = RfidTag::DirNominal;
    for (int i = 0; i < 3; ++i) {
        r.tags.append({ QString(), normal(100 + i, 1000 + 300 * i) });
        r.tags.append({ QString(), normal(100 + i, 1004 + 300 * i, true) });
    }
    return RfidTag::readXml(RfidTag::toRouteXml(r)).value(0);
}

QStringList simFindings(const RfidTag::Route &r, int row = -2)
{
    QStringList out;
    for (const RfidCheck::Finding &f : RfidCheck::check(r))
        if (f.text.startsWith(QLatin1String("Simulator:")) && (row == -2 || f.row == row)) out << f.text;
    return out;
}

}  // namespace

TEST_SUITE(session204)
{
    const RfidTag::Route clean = fileRoute();
    CHECK(clean.tags.size() == 6 && clean.tags.first().file.present, "route.xml read back: six rows with their attributes");
    CHECK(simFindings(clean) == QStringList({ QStringLiteral("Simulator: the route's last row (next_rfid_abs_loc = abs_loc): never sent") }),
          QByteArray("a route.xml DLConsole wrote: only its last row, never sent: ") + simFindings(clean).join(QLatin1String(" | ")).toUtf8());

    {
        RfidTag::Route r = clean;
        r.tags[2].file.tagType = 10;
        CHECK(simFindings(r, 2).value(0).contains(QLatin1String("tag_type is 10, the tag is type 9; it decodes and sends the tag with the LC gate layout")),
              QByteArray("tag_type against the tag: ") + simFindings(r, 2).value(0).toUtf8());
        r.tags[2].file.tagType = 0;
        CHECK(simFindings(r, 2).value(0).contains(QLatin1String("sends nothing for tag_type 0")), "tag_type 0: nothing sent");
    }
    {
        RfidTag::Route r = clean;
        r.tags[3].file.tagName = QStringLiteral("main");
        CHECK(simFindings(r, 3).value(0).contains(QLatin1String("the tag is a duplicate tag; reader 2 keeps it as the main tag")),
              QByteArray("tag_name against the duplication bit: ") + simFindings(r, 3).value(0).toUtf8());
    }
    {
        RfidTag::Route r = clean;
        r.tags[1].file.pageX = QLatin1Char(' ') + r.tags[1].file.pageX + QLatin1Char(' ');
        CHECK(simFindings(r, 1).isEmpty(), "spaces in a page: harmless (QByteArray::fromHex skips them)");
        // A page written without its leading zeros: the tag still reads, the simulator's join shifts.
        int found = 0;
        for (int i = 0; i < r.tags.size() && !found; ++i) {
            RfidTag::Route s = clean;
            QString &px = s.tags[i].file.pageY;
            if (!px.startsWith(QLatin1Char('0'))) continue;
            px = px.mid(1);
            found = simFindings(s, i).join(QLatin1Char(' ')).contains(QLatin1String("hex digits, not 16")) ? 1 : -1;
        }
        CHECK(found >= 0, found ? "a page short of its leading zero: shifted" : "NOTE: no page with a leading zero to drop");
    }
    {
        RfidTag::Route r = clean;
        r.tags[2].file.rfidId = QStringLiteral("101X");
        CHECK(simFindings(r, 2).value(0).contains(QLatin1String("rfid_id is \"101X\"")), "rfid_id against the tag's name");
        r.tags[2].file.rfidId = QStringLiteral("100D");
        CHECK(simFindings(r, 2).join(QLatin1Char(' ')).contains(QLatin1String("same rfid_id (100D); reader 1 does not send it again")),
              "a repeated rfid_id");
    }
    {
        RfidTag::Route r = clean;
        r.tags[2].file.absLoc = 1250;
        CHECK(simFindings(r, 2).value(0).contains(QLatin1String("abs_loc is 1250 m, route.xml would write 1300 m")),
              QByteArray("abs_loc against the tag: ") + simFindings(r, 2).value(0).toUtf8());
        CHECK(simFindings(r, 1).value(0).contains(QLatin1String("the next row starts at 1250 m: the rows overlap")),
              QByteArray("and the row before now overlaps it: ") + simFindings(r, 1).value(0).toUtf8());
        r.tags[2].file.absLoc = 1300;
        r.tags[1].file.nextAbsLoc = 1200;
        CHECK(simFindings(r, 1).value(0).contains(QLatin1String("a gap no row covers: the simulator ends the route there")), "a gap");
        r.tags[4].file.nextAbsLoc = 1260;
        r.tags[4].file.absLoc = 1210;
        CHECK(simFindings(r, 1).value(0).contains(QLatin1String("a gap; row 5 (102) covers it")),
              QByteArray("a gap another row covers: ") + simFindings(r, 1).value(0).toUtf8());
        r = clean;
        r.tags[1].file.nextAbsLoc = 1004;
        CHECK(simFindings(r, 1).value(0).contains(QLatin1String("no location is inside this row")), "a row with no length");
    }
    {
        RfidTag::Route r = clean;
        QHash<QString, qint64> v = RfidTag::values(r.tags[2].bytes);
        v.insert(QStringLiteral("tin_nom"), 85);
        r.tags[2].bytes = RfidTag::build(v);
        r.tags[2].file.tagType = 10;
        CHECK(simFindings(r, 2).isEmpty(), "an edited tag: its stale row is not checked");
        bool noted = false;
        for (const RfidCheck::Finding &f : RfidCheck::check(r)) noted = noted || f.text.startsWith(QLatin1String("1 row edited"));
        CHECK(noted, "and that is said");
    }
    {
        RfidTag::Route r = clean;
        r.dir = RfidTag::DirUnset;
        r.tags[2].file.tagType = 10;
        CHECK(simFindings(r, 2).size() == 1, "no direction: the row checks that need none still run");
    }

    // ---- the simulator's own file --------------------------------------------------------------
    const QVector<RfidTag::Route> routes =
        RfidTag::readFile(QStringLiteral(":/tag_scenarios/LocoTcasSimulator/Configuration1.xml"));
    int type = 0, noLength = 0, chain = 0, repeated = 0, lastRows = 0;
    for (const RfidTag::Route &r : routes)
        for (const QString &t : simFindings(r)) {
            type += t.contains(QLatin1String("tag_type is")) ? 1 : 0;
            noLength += t.contains(QLatin1String("no location is inside")) ? 1 : 0;
            chain += t.contains(QLatin1String("the next row starts at")) ? 1 : 0;
            repeated += t.contains(QLatin1String("same rfid_id")) ? 1 : 0;
            lastRows += t.contains(QLatin1String("last row")) ? 1 : 0;
        }
    CHECK(routes.size() == 165 && type == 10 && noLength == 142 && chain == 14 && repeated == 1 && lastRows == 146,
          QByteArray("Configuration1.xml: 10 tag_type, 142 rows with no length, 14 chain breaks, 1 repeated rfid_id, "
                     "146 routes ending in a row of no length: ")
              + QByteArray::number(routes.size()) + " routes, " + QByteArray::number(type) + " / " + QByteArray::number(noLength)
              + " / " + QByteArray::number(chain) + " / " + QByteArray::number(repeated) + " / " + QByteArray::number(lastRows));
}
