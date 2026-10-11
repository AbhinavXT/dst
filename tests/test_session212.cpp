#include "testutil.h"

#include "rfidtag.h"
#include "routestrip.h"
#include "stationlayout.h"
#include "stationlayoutwindow.h"
#include "theme.h"
#include "tinlayout.h"
#include "uistyle.h"

#include <QFile>
#include <QImage>
#include <QTemporaryDir>

// =============================================================================
//  Session 212 — Station Layout and the Tag Builder strip drawn as the RDSO
//  Annexure-H (RDSO/SPN/196/2020 Amdt-5) RFID Tag-TIN layouts are. Real tags:
//  the built-in station.xlsx (station 527, Lingampalli — the station of the
//  Annexure's own typical layout, H2.29).
// =============================================================================

TEST_SUITE(session212)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    // H2.21 notation, from the schema's type and rfidPlace values.
    const char *byPlacement = "NSSTXXSXDD";
    bool letters = true;
    for (int pl = 0; pl < 10; ++pl) letters = letters && TinLayout::notation(9, pl) == QLatin1Char(byPlacement[pl]);
    CHECK(letters, "normal tags: N S S T X X S X D D for placements 0..9");
    CHECK(TinLayout::notation(10, 0) == QLatin1Char('G') && TinLayout::notation(11, 0) == QLatin1Char('L')
          && TinLayout::notation(12, 0) == QLatin1Char('A') && TinLayout::notation(9, 15) == QLatin1Char('?'),
          "LC gate G, adjacent line L, adjustment A; an unknown placement '?'");
    // H2.15 shape, H2.17/18 colours.
    CHECK(TinLayout::shapeOf(100, 100, true) == TinLayout::Box && TinLayout::shapeOf(100, 104, true) == TinLayout::TipRight
          && TinLayout::shapeOf(100, 96, true) == TinLayout::TipLeft && TinLayout::shapeOf(100, 0, false) == TinLayout::Box,
          "same location: a box; else the tip points to the duplicate");
    CHECK(!TinLayout::tinColor(0).isValid() && TinLayout::tinColor(83) != TinLayout::tinColor(84),
          "TIN 0 drawn hollow (non-Kavach); neighbouring TINs differ");
    CHECK(TinLayout::tinLabel(65) == QLatin1String("(N-65)") && TinLayout::idText(QStringLiteral("46")) == QLatin1String("R-46"),
          "labels as on the RDSO layouts");

    // ---- Station Layout over station 527 ----------------------------------------------------
    StationLayoutWindow w;
    w.resize(1180, 720);
    w.show();
    CHECK(w.openFile(StationLayout::defaultFile()), "the built-in station.xlsx opens");
    w.canvas()->grab();
    const StationLayout::TagInfo t981 = StationLayout::info(w.station().tags.at(0));
    CHECK(w.station().tags.at(0).name == QLatin1String("981") && t981.absLoc == 163960, "fixture: tag 981 at 163960 m");
    QString tip981, tipDup, tinTip;
    int setHits = 0;
    for (const StationLayoutCanvas::Hit &h : w.canvas()->hits()) {
        if (h.tag == QLatin1String("981")) tip981 = h.text;
        if (h.tag == QLatin1String("981D")) tipDup = h.text;
        if (h.text.startsWith(QLatin1String("TIN 83 between tags"))) tinTip = h.text;
        if (!h.tag.isEmpty()) ++setHits;
    }
    const QString name981 = TinLayout::notationName(TinLayout::notation(t981.type, t981.placement));
    CHECK(tip981.contains(name981) && tip981.contains(QLatin1String("Duplicate 981D at 163.956 km"))
          && tip981.contains(QLatin1String("TIN 83 Nominal / 83 Reverse")),
          QByteArray("tag 981's set: its letter's name, its duplicate 4 m behind, its TIN (") + tip981.toUtf8() + ")");
    CHECK(tipDup.startsWith(QLatin1String("Duplicate tag 981D at 163.956 km")), "the duplicate away from its main is a mark of its own");
    CHECK(!tinTip.isEmpty(), "the band between tags names its TIN");
    CHECK(setHits < w.station().tags.size(), QByteArray("a duplicate at its main's location is in its main's symbol (")
          + QByteArray::number(setHits) + " marks for " + QByteArray::number(w.station().tags.size()) + " tags)");
    CHECK(w.minimumSizeHint().width() <= 1000, QByteArray("the button bar still fits (") + QByteArray::number(w.minimumSizeHint().width()) + ")");

    // ---- the drawing as a sheet ----------------------------------------------------------------
    QTemporaryDir dir;
    const QString pdf = dir.filePath(QStringLiteral("527.pdf")), png = dir.filePath(QStringLiteral("527.png"));
    CHECK(w.exportDrawing(pdf), "Export ▸ Drawing as PDF");
    QFile f(pdf);
    CHECK(f.open(QIODevice::ReadOnly) && f.read(5) == "%PDF-" && f.size() > 10000, "a PDF with the drawing in it");
    CHECK(w.exportDrawing(png), "Export ▸ Drawing as PNG");
    const QImage img(png);
    CHECK(img.width() >= 4800 && img.height() > 1000, QByteArray("a sheet-sized image (") + QByteArray::number(img.width())
          + " x " + QByteArray::number(img.height()) + ")");
    CHECK(!w.exportDrawing(dir.filePath(QStringLiteral("no/such/dir/x.png"))), "a path that cannot be written is refused");

    // ---- the Tag Builder strip with the same tags ------------------------------------------------
    RfidTag::Route r;
    r.dir = RfidTag::DirNominal;
    for (int i = 0; i < 4; ++i) {
        const StationLayout::Tag &t = w.station().tags.at(i);           // 981, 981D, 985, 985D
        r.tags.append({ t.name, RfidTag::fromPages(t.pageX, t.pageY), {} });
    }
    RouteStrip strip;
    strip.resize(600, 100);
    strip.setRoute(r);
    strip.grab();
    CHECK(strip.drawnTags() == 4, "the strip draws both sets");
    CHECK(strip.rowAt(QPoint(int(600 - 24), 64)) == 0, "tag 981, the far right, is still found by its x");
}
