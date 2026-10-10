#include "testutil.h"

#include "rfidexport.h"
#include "rfidtag.h"
#include "taglibrary.h"

#include <QFile>

// =============================================================================
//  Session 202 — the RFID simulator's own Configuration1.xml in the built-in
//  tag scenario library (LocoTcasSimulator/XML/Configuration1.xml, 9 Oct 2026:
//  the file the simulator runs from, newer than tags_sim's KAV_CONFIG copy).
// =============================================================================

TEST_SUITE(session202)
{
    const QString path = QStringLiteral(":/tag_scenarios/LocoTcasSimulator/Configuration1.xml");
    int routes = 0, tags = 0, failing = 0, lc = 0;
    for (const TagLibrary::Entry &e : TagLibrary::scan(TagLibrary::builtInRoot()))
        if (e.file == path) ++routes;
    for (const RfidTag::Route &r : RfidTag::readFile(path))
        for (const RfidTag::Tag &t : r.tags) {
            ++tags;
            failing += RfidTag::summary(t.bytes).crcOk ? 0 : 1;
            lc += RfidTag::summary(t.bytes).type == 10 ? 1 : 0;
        }
    CHECK(routes == 165 && tags == 6413, QByteArray("the simulator's Configuration1.xml is built in: ")
                                             + QByteArray::number(routes) + " routes, " + QByteArray::number(tags) + " tags");
    CHECK(failing == 37, QByteArray("37 of its tags fail their CRC-30 (a loco would not process them): ") + QByteArray::number(failing));
    // 55 rows SAY tag_type="10"; the tags themselves: 62 (the simulator picks
    // its packet layout by the attribute, not by the tag).
    CHECK(lc == 62, QByteArray("it holds LC gate tags (type 10): ") + QByteArray::number(lc));

    // The fix offered for it: all 37, nothing else in the file changed.
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    const QByteArray conf = f.readAll();
    QStringList changes;
    const QByteArray fixed = RfidExport::fixCrcs(conf, &changes);
    CHECK(changes.size() == 37 && fixed.size() == conf.size(),
          QByteArray("Fix the CRCs corrects those 37, the file the same size, though two of its rows have a space "
                     "inside page_x's quotes: ") + QByteArray::number(changes.size()));
    CHECK(fixed.contains("page_x=\"080505828848ce99 \""), "and that space is left where it was");
}
