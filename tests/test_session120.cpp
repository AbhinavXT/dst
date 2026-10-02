#include "testutil.h"

#include "capturedecoder.h"
#include "fieldinspector.h"
#include "schema/schemadecoder.h"

#include <QFontInfo>
#include <QLabel>
#include <QTableWidget>

// =============================================================================
//  Session 120 — UI revamp step 4: the decoded inspector's summary card.
//
//  What the frame is before what its fields are: the packet and its size,
//  the CRC, whether a receiver would act on it, and the values a reader
//  looks for first, large. Real frames from replay/.
// =============================================================================

namespace {
// replay/loco_1_1_27062026_140226.cap: LOCO_MODE 7 (Trip), TRAIN_SPEED 0,
// ABS_LOCO_LOC 163821 m, CRC pass.
const QString kLsrp = QStringLiteral(
    "@lsrp_1_1 2026-06-27T14:02:27 21441 02 07 0A 00 27 00 00 00 0F 02 A3 AC 57 "
    "40 00 01 40 9F FB 41 E0 F0 7D 00 10 FC 30 15 20 8D 00 F2 F3 26 DD C6 ED 59 9B");
// replay/loco_1_1_29062026_134128.cap: a DMI frame, its fields lower case.
const QString kDmi = QStringLiteral(
    "@dmi_1_1 2026-06-29T13:41:29 2942 AA AA 74 02 01 0A 6F 00 10 17 00 00 00 00 00 00 00 00 00 00 1D 06 EA 07 0D 29 1D "
    "00 00 00 00 02 00 00 00 00 00 56 66 63 00 00 01 00 00 60 95 58 02 00 60 21 64 19 00 00 00 0F 00 80 59 06 E2 04 4B "
    "01 F5 01 00 00 FA 00 00 00 00 00 91 41 14 2D 6A AD 00 00 00 00 00 00 00 00 00 00 00 00 F0 3C 00 F4 01 00 00 00 00 "
    "08 06 0F 00 00 01 00 27 8B EE 3C BB BB");

LogEntryPtr entry(const QString &text)
{
    auto e = QSharedPointer<LogEntry>::create();
    e->text = text;
    e->epochMs = 1000;
    e->header.source_id = 21;
    e->header.kvchId = 1;
    e->cacheDerived();
    return e;
}
}  // namespace

TEST_SUITE(session120)
{
    CHECK(kavachSchema().isLoaded(), "schema loaded");
    FieldInspector fi;
    fi.setDecoder(&kavachSchema());

    fi.showEntry(entry(kLsrp));
    CHECK(fi.summaryTitle() == QStringLiteral("LSRP \u00B7 39 B"), QByteArray("the card names the packet and its size: ") + fi.summaryTitle().toUtf8());
    const QStringList tiles = fi.summaryTiles();
    CHECK(tiles.value(0) == QLatin1String("Loco mode=Trip"), QByteArray("the mode first, by its name: ") + tiles.join(" | ").toUtf8());
    CHECK(tiles.value(1) == QLatin1String("Speed=0 km/h"), "then the speed");
    CHECK(tiles.value(2) == QLatin1String("Location=163821 m"), "then the location (three tiles at most)");
    QLabel *crc = fi.findChild<QLabel *>(QStringLiteral("fieldCrcChip"));
    CHECK(crc && !crc->isHidden() && crc->text().contains(QLatin1String("CRC pass"))
              && crc->property("dlTone").toString() == QLatin1String("ok"),
          "the CRC as a chip, in the ok tone");

    QTableWidget *t = fi.findChild<QTableWidget *>();
    bool monoValues = t && t->rowCount() > 0;
    for (int r = 0; t && r < t->rowCount(); ++r)
        if (t->item(r, 1) && !QFontInfo(t->item(r, 1)->font()).fixedPitch()) monoValues = false;
    CHECK(monoValues, "values in the mono face, digit under digit");

    fi.showEntry(entry(kDmi));
    CHECK(fi.summaryTitle().startsWith(QStringLiteral("DMI \u00B7 ")) && !fi.summaryTiles().isEmpty(),
          QByteArray("a DMI frame's lower-case fields are found too: ") + fi.summaryTiles().join(" | ").toUtf8());

    fi.showEntry(entry(QStringLiteral("RAD IN Link 1 Error")));
    CHECK(fi.summaryTitle().isEmpty() && fi.summaryTiles().isEmpty(), "a text row has no card (nothing to decode)");
    fi.clear();
    CHECK(fi.summaryTitle().isEmpty(), "clear hides it");

    // Failed rows in the theme's error colour, not a literal.
    // (A CRC-failed frame: the real LSRP with its last byte changed.)
    QString bad = kLsrp;
    bad.replace(bad.size() - 2, 2, QStringLiteral("9C"));
    fi.showEntry(entry(bad));
    CHECK(crc && crc->text().contains(QLatin1String("CRC fail")) && crc->property("dlTone").toString() == QLatin1String("fail"),
          "a changed byte: the CRC chip says fail, in the fail tone");
}
