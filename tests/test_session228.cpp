#include "testutil.h"

#include "rfidtag.h"
#include "settings.h"
#include "stationlayoutwindow.h"

#include <QPushButton>

// =============================================================================
//  Session 228 — Station Layout: Fix CRCs over the whole layout.
//  Real file: the built-in station_lingampalli.xlsx (session 211's library),
//  whose Checks list many tags with "CRC-30 does not match its contents"
//  (session 208 saw the same in the Python tool's copy).
// =============================================================================

TEST_SUITE(session228)
{
    QString path;
    for (const QString &f : StationLayout::library())
        if (f.endsWith(QLatin1String("/station_lingampalli.xlsx"))) path = f;
    CHECK(!path.isEmpty(), "station_lingampalli.xlsx is in the library");
    if (path.isEmpty()) return;
    Settings::setStationLayoutLastFile(QString());
    StationLayoutWindow w;
    CHECK(w.openFile(path), "it opens");
    const StationLayout::Layout before = w.station();
    const int bad = StationLayout::checks(before).filter(QStringLiteral("CRC-30 does not match")).size();
    CHECK(bad > 10, QByteArray("Checks: ") + QByteArray::number(bad) + " CRCs do not match");

    QStringList left;
    CHECK(w.fixAllCrcs(&left) == bad && left.isEmpty(), "Fix CRCs fixes exactly those");
    CHECK(StationLayout::checks(w.station()).filter(QStringLiteral("CRC-30")).isEmpty(), "Checks: no CRC finding left");
    bool sameFields = true, onlyCrcBits = true;
    for (int i = 0; i < before.tags.size(); ++i) {
        const QByteArray a = RfidTag::fromPages(before.tags.at(i).pageX, before.tags.at(i).pageY);
        const QByteArray b = RfidTag::fromPages(w.station().tags.at(i).pageX, w.station().tags.at(i).pageY);
        sameFields = sameFields && RfidTag::values(a) == RfidTag::values(b) && before.tags.at(i).name == w.station().tags.at(i).name;
        // CRC-30 sits in the top 30 bits of page_y (bits 34..63 of bytes 8-15).
        onlyCrcBits = onlyCrcBits && a.left(8) == b.left(8);
    }
    CHECK(sameFields && onlyCrcBits, "every tag keeps its fields and page_x; only CRC bits changed");
    CHECK(w.fixAllCrcs() == 0, "a second Fix finds nothing");
    CHECK(w.undo() && w.station().tags.at(0).pageY == before.tags.at(0).pageY
              && StationLayout::checks(w.station()).filter(QStringLiteral("CRC-30 does not match")).size() == bad,
          "one Undo puts every CRC back");

    // A tag that does not decode is left and named.
    StationLayout::Layout withBad = before;
    withBad.tags << StationLayout::Tag{ QStringLiteral("999"), QStringLiteral("zz"), QStringLiteral("1") };
    w.setStation(withBad);
    CHECK(w.fixAllCrcs(&left) == bad && left == QStringList{ QStringLiteral("999") }
              && w.station().tags.last().pageX == QLatin1String("zz"),
          "a tag that does not decode is left as it is, and named");

    bool button = false;
    for (QPushButton *b : w.findChildren<QPushButton *>()) button = button || b->text() == QStringLiteral("Fix CRCs");
    CHECK(button, "a Fix CRCs button under the sheets");
}
