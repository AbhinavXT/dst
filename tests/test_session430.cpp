#include "testutil.h"

#include "layoutdrawing.h"
#include "stationlayout.h"
#include "stationlayoutwindow.h"

#include <QAction>
#include <QListWidget>
#include <QMenu>
#include <QTabWidget>

// =============================================================================
//  Session 430 — a station layout checked against the station's tag-TIN
//  layout drawing. The drawing: Lingampalli (LPI), station 527, page 9 of
//  RDSO's Annexure-H Amdt-3 (HBL, KAVACH_RFID_TAG_LAYOUT_LPI_2.0.2), read
//  off by hand into :/layout_drawings/LPI_527.json: 84 tags (number, km,
//  N/S/T/X/L, the way the triangle points, line, the TIN either side), 26
//  signals at their foot tags, the station at 161.060 km.
//
//  Checked against two real built-in layouts of the same station:
//  - Originals/station_lingampalli.xlsx (the Python tool's, real tags). Its
//    tin_rev / tin_nom agree with the colours read off the drawing on every
//    tag the two share but 800 (which it has at 158.800 km): the reading
//    and the tags agree, independently. What differs: tags 4, 6, 16 (S◁ on
//    the drawing) and 566, 15, 5, 3 (S▷) programmed as In line section;
//    S1649 at tag 910 where the drawing has S1647.
//  - Section_30.xlsx: its middle block is LPI's DN main; tag 2 again at
//    159.660 km (patch 429's finding), the TINs 84 / 85, 904 only in the
//    first block, 905 where the drawing has 904.
// =============================================================================

namespace {

using F = LayoutDrawing::Finding;

QString libraryFile(const QString &end)
{
    for (const QString &f : StationLayout::library())
        if (f.endsWith(end)) return f;
    return QString();
}

QStringList keysOf(const QVector<F> &fs, F::Kind k)
{
    QStringList out;
    for (const F &f : fs)
        if (f.kind == k) out << f.key;
    return out;
}

QStringList textsOf(const QVector<F> &fs, F::Kind k)
{
    QStringList out;
    for (const F &f : fs)
        if (f.kind == k) out << f.text;
    return out;
}

} // namespace

TEST_SUITE(session430)
{
    // ---- the drawing ----
    const QStringList built = LayoutDrawing::builtIn();
    CHECK(built == QStringList{ QStringLiteral(":/layout_drawings/LPI_527.json") }, "one drawing built in: LPI_527.json");
    LayoutDrawing::Drawing d;
    QString err;
    CHECK(LayoutDrawing::load(built.value(0), &d, &err), "it reads");
    int dashed = 0, s = 0, t = 0;
    for (const LayoutDrawing::Tag &x : d.tags) { dashed += x.dashed; s += x.kind == QLatin1Char('S'); t += x.kind == QLatin1Char('T'); }
    CHECK(d.tags.size() == 84 && d.signalList.size() == 26 && d.stationId == 527 && d.stationLoc == 161060,
          "84 tags, 26 signals, station 527 at 161.060 km");
    CHECK(dashed == 11 && s == 33 && t == 9, "11 drawn dashed (another station's), 33 signal foot tags, 9 turnout tags");
    CHECK(d.title == QStringLiteral("Lingampalli (LPI), station 527") && d.source.contains(QStringLiteral("TC.1005B")),
          "the title, and the source names the ToC");
    CHECK(!LayoutDrawing::fromJson("{\"format\":\"dlconsole-station-layout\"}", &d, &err) && err.contains(QStringLiteral("dlconsole-layout-drawing")),
          "a station layout file is not a drawing");
    LayoutDrawing::load(built.value(0), &d);

    // ---- the Python tool's Lingampalli layout ----
    StationLayout::Layout lpi;
    CHECK(StationLayout::load(libraryFile(QStringLiteral("/Originals/station_lingampalli.xlsx")), &lpi), "station_lingampalli.xlsx reads");
    const QVector<F> fl = LayoutDrawing::check(d, lpi);
    CHECK(keysOf(fl, F::TinDiffers) == QStringList{ QStringLiteral("800") },
          "TINs: every tag both have agrees with the drawing's colours, but 800");
    CHECK((keysOf(fl, F::KindDiffers) == QStringList{ QStringLiteral("566"), QStringLiteral("4"), QStringLiteral("6"), QStringLiteral("16"),
                                                        QStringLiteral("800"), QStringLiteral("15"), QStringLiteral("5"), QStringLiteral("3") }),
          "kinds: 8 signal foot tags of the drawing are In line section in the tags");
    CHECK(textsOf(fl, F::KindDiffers).contains(QStringLiteral("tag 4: the drawing shows S◁ (Signal foot tag, reverse); "
                                                               "the tag says type 9 (Normal), placement 0 (In line section)")),
          "said as the drawing shows it and the tag says it");
    CHECK(textsOf(fl, F::Moved).contains(QStringLiteral("tag 566: 158.412 km on the drawing, 153.412 km in the layout (-5000 m)")),
          "566 is 5 km earlier in the layout");
    CHECK(textsOf(fl, F::SignalDiffers).contains(QStringLiteral("signals sheet: S1649 at tag 910; the drawing has S1647 there")),
          "S1649 where the drawing has S1647");
    CHECK((keysOf(fl, F::NotOnDrawing) == QStringList{ QStringLiteral("802"), QStringLiteral("804"), QStringLiteral("400"), QStringLiteral("402"),
                                                         QStringLiteral("401"), QStringLiteral("399"), QStringLiteral("23"), QStringLiteral("32") }),
          "8 tags in the drawing's stretch that it does not have");
    CHECK(textsOf(fl, F::NotInLayout) == (QStringList{ QStringLiteral("DN MAIN: 1 tag(s) on the drawing not in the layout: 912"),
                                                       QStringLiteral("UP MAIN: 5 tag(s) on the drawing not in the layout: 979, 977, 975, 872, 813"),
                                                       QStringLiteral("OEM LINE 2: none of its 2 tag(s) on the drawing is in the layout: 42, 43") }),
          "the drawing's tags it lacks, by line");
    CHECK(keysOf(fl, F::Repeated).isEmpty() && keysOf(fl, F::Station).isEmpty() && fl.size() == 35, "35 lines in all");

    // ---- Section_30.xlsx: LPI's DN main three times ----
    StationLayout::Layout s30;
    CHECK(StationLayout::load(QStringLiteral(":/station_layouts/Section_30.xlsx"), &s30), "Section_30.xlsx reads");
    const QVector<F> fs = LayoutDrawing::check(d, s30);
    CHECK(textsOf(fs, F::Repeated).contains(QStringLiteral("tag 2 again at 159.660 km (2#3), within the drawing's 158.412 to 165.540 km")),
          "the third tag 2, at 159.660 km like the second");
    CHECK(textsOf(fs, F::TinDiffers).contains(QStringLiteral("tag 18: N-56 | N-84 either side on the drawing (reverse | nominal); "
                                                             "the tag says tin_rev 56, tin_nom 85")),
          "18#2: TIN 85 ahead where the drawing has N-84");
    CHECK(keysOf(fs, F::TinDiffers).contains(QStringLiteral("2#2")) && keysOf(fs, F::KindDiffers).contains(QStringLiteral("4#2")),
          "the tags of the middle block are the ones checked");
    CHECK(textsOf(fs, F::Moved) == QStringList{ QStringLiteral("tag 904: 163.820 km on the drawing, 158.820 km in the layout (-5000 m)") },
          "904: only in the first block");
    CHECK(keysOf(fs, F::NotOnDrawing).contains(QStringLiteral("905")), "905 where the drawing has 904");
    CHECK(textsOf(fs, F::NotInLayout).contains(QStringLiteral("UP MAIN: none of its 23 tag(s) on the drawing is in the layout: 551, 603, 601, 605, "
                                                              "17, 15, 13, 11, 9, 7, 5, 3, 1, 991, 989, 987, 985, 981, 979, 977, 975, 872, 813")),
          "the UP main is not in Section_30");
    CHECK(fs.size() == 36, "36 lines in all");

    // ---- the window ----
    StationLayoutWindow w;
    CHECK(w.openFile(QStringLiteral(":/station_layouts/Section_30.xlsx")), "Section_30 open");
    QAction *pick = nullptr;
    for (QAction *a : w.findChildren<QAction *>())
        if (a->text() == d.title && a->parent() && qobject_cast<QMenu *>(a->parent())
            && static_cast<QMenu *>(a->parent())->title() == QStringLiteral("A tag-layout drawing")) pick = a;
    CHECK(pick && pick->toolTip() == d.source, "Open ▸ Compare with ▸ A tag-layout drawing ▸ Lingampalli (LPI), station 527");
    if (pick) pick->trigger();
    auto *list = qobject_cast<QListWidget *>(w.tabs()->currentWidget());
    CHECK(list && list->count() == 37 && w.drawingFindings().size() == 36, "the Compare tab: a heading and the 36 lines");
    CHECK(w.tabs()->tabText(w.tabs()->currentIndex()) == QStringLiteral("Compare (36)"), "the tab counts them");
    const QHash<QString, int> rings = w.canvas()->diffTags();
    CHECK(w.canvas()->diffByKey() && rings.contains(QStringLiteral("4#2")) && rings.contains(QStringLiteral("4#3"))
              && !rings.contains(QStringLiteral("4")),
          "by key: tag 4 of the middle block, and the third (again in the stretch); not the first, 5 km before it");
    CHECK(rings.value(QStringLiteral("904")) == StationLayout::TagChange::Changed && rings.value(QStringLiteral("905")) == StationLayout::TagChange::Added,
          "904 (moved and of another kind): changed; 905 (not on the drawing): added");
    for (int i = 0; list && i < list->count(); ++i)
        if (list->item(i)->data(Qt::UserRole).toString() == QStringLiteral("6#2")) { emit list->itemDoubleClicked(list->item(i)); break; }
    CHECK(w.selectedTags() == QStringList{ QStringLiteral("6#2") }, "double-click: the middle block's tag 6 is selected");
    CHECK(w.moveTag(QStringLiteral("904"), 163820) && w.drawingFindings().size() == 35, "904 moved to 163.820 km: one line fewer");
    CHECK(!w.compareWithDrawing(QStringLiteral("/nonexistent/x.json")) && w.drawingFindings().size() == 35, "an unreadable file leaves it as it was");
    w.clearCompare();
    CHECK(w.drawingFindings().isEmpty() && w.canvas()->diffTags().isEmpty() && w.tabs()->tabText(w.tabs()->indexOf(list)) == QStringLiteral("Compare"),
          "Stop comparing");
    CHECK(w.compareWith(QStringLiteral(":/station_layouts/Section_30.xlsx")) && !w.canvas()->diffByKey() && w.drawingFindings().isEmpty(),
          "comparing with a file afterwards: by name again");
}
