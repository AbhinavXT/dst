#include "testutil.h"

#include "stationlayout.h"
#include "stationlayoutwindow.h"
#include "xlsxbook.h"

#include <QFile>
#include <QListWidget>
#include <QTabWidget>
#include <QTemporaryDir>

// =============================================================================
//  Session 454 — a firm's KAVACH RFID tag data sheet read into a station
//  layout (scripts/kavach_sheets.py). Its station_<id>.json files are made
//  from pages of HBL's 2023 sheets kept in schema/fixtures/kavach_sheets/
//  (the PDFs stay local); validate_kavach_sheets.py checks the reading, and
//  this suite what DLConsole does with the result:
//  - NPL-TLPR IB (station 526), the whole sheet: 22 tags, every CRC-30
//    holds, the sheet agrees with itself: nothing under Checks.
//  - SKP pages 16-23 (station 523): page words a spreadsheet turned into
//    numbers (384, 400, 401), a digit short (387D), 388D's comm bits, the
//    LC tags' applicable direction, the bit positions 'Y131' and 'Y-14-Y12'
//    printed on the LC rows. The pages go in as printed; the notes say what
//    the printed fields would make.
//  - LPI pages 1 and 12 (station 527): tag 6D's pages say main tag.
//  The notes ride the file (JSON, station XML, the tool's sheets) and are
//  listed under Checks as "Sheet: ...".
// =============================================================================

namespace {

const char *kDir = "../schema/fixtures/kavach_sheets/";

QListWidget *checksList(StationLayoutWindow &w, QString *title)
{
    for (int i = 0; i < w.tabs()->count(); ++i)
        if (w.tabs()->tabText(i).startsWith(QStringLiteral("Checks"))) {
            *title = w.tabs()->tabText(i);
            return qobject_cast<QListWidget *>(w.tabs()->widget(i));
        }
    return nullptr;
}

QStringList items(QListWidget *l)
{
    QStringList out;
    for (int i = 0; l && i < l->count(); ++i) out << l->item(i)->text();
    return out;
}

bool anyHas(const QStringList &list, const QStringList &words)
{
    for (const QString &s : list) {
        bool all = true;
        for (const QString &w : words) all = all && s.contains(w);
        if (all) return true;
    }
    return false;
}

} // namespace

TEST_SUITE(session454)
{
    // ---- NPL-TLPR IB: a clean sheet ----
    StationLayout::Layout ib;
    QString err;
    QStringList notes;
    CHECK(StationLayout::load(QString::fromLatin1(kDir) + QStringLiteral("NPL-TLPR_IB/station_526.json"), &ib, &err, &notes),
          "NPL-TLPR IB's station_526.json opens");
    CHECK(ib.tags.size() == 22 && ib.stations.size() == 1 && ib.stations.first().id == 526,
          "22 tags, station 526");
    bool allOk = true;
    for (const StationLayout::Tag &t : ib.tags) {
        const StationLayout::TagInfo in = StationLayout::info(t);
        allOk = allOk && in.ok && in.crcOk && QString::number(in.unique) + (in.duplicate ? QStringLiteral("D") : QString()) == t.name;
    }
    CHECK(allOk, "every tag decodes, its CRC-30 holds, its name is its unique id (D: duplicate)");
    CHECK(ib.sheetNotes.isEmpty() && StationLayout::checks(ib).isEmpty(), "the sheet agrees with itself: nothing to report");
    const QHash<QString, qint64> locs = StationLayout::tagLocations(ib);
    CHECK(locs.value(QStringLiteral("134")) == 152694 && locs.value(QStringLiteral("177")) == 155566
              && locs.value(QStringLiteral("177D")) == 155562,
          "134 at 152694 m; 177 at 155566 m, as the tag data prints it (the layout and ToC say 155.556)");
    CHECK(ib.stations.first().location == (152694 + 156336) / 2, "the station drawn at the middle of its normal tags");

    // ---- SKP pages 16-23: the sheet's disagreements ----
    StationLayout::Layout skp;
    CHECK(StationLayout::load(QString::fromLatin1(kDir) + QStringLiteral("SKP_extract/station_523.json"), &skp, &err),
          "SKP's station_523.json opens");
    CHECK(skp.tags.size() == 42 && skp.stations.value(0).id == 523, "42 tags, station 523");
    CHECK(skp.sheetNotes.size() == 13, "13 sheet notes");
    CHECK(anyHas(skp.sheetNotes, { QStringLiteral("tag 384M"), QStringLiteral("PAGE-X 0587070224406019"), QStringLiteral("the CRC printed") }),
          "384M: the page the printed fields make, whose CRC-30 is the one printed");
    CHECK(anyHas(skp.sheetNotes, { QStringLiteral("tag 388D"), QStringLiteral("comm nominal (Y32) printed 1, the pages hold 0") }),
          "388D: the comm bits");
    CHECK(anyHas(skp.sheetNotes, { QStringLiteral("'Y131'"), QStringLiteral("not checked") }),
          "'Y131' is named, not read as Y31 or Y13");
    int t384 = -1;
    for (int i = 0; i < skp.tags.size(); ++i)
        if (skp.tags.at(i).name == QStringLiteral("384")) t384 = i;
    CHECK(t384 >= 0 && skp.tags.at(t384).pageX == QStringLiteral("587070000000000"), "384's PAGE-X kept as printed");
    CHECK(!StationLayout::info(skp.tags.value(t384)).crcOk, "... and its CRC-30 does not hold");
    CHECK(anyHas(StationLayout::checks(skp), { QStringLiteral("384") }), "the layout's own checks say so too");

    // ---- the notes ride the file ----
    QTemporaryDir tmp;
    StationLayout::Layout back;
    CHECK(StationLayout::fromJson(StationLayout::toJson(skp), &back) && back.sheetNotes == skp.sheetNotes, "JSON round trip");
    back = StationLayout::Layout();
    QStringList xmlNotes;
    CHECK(StationLayout::fromXml(StationLayout::toXml(skp, QStringLiteral("station_523")), &back, &err, &xmlNotes)
              && back.sheetNotes == skp.sheetNotes,
          "station XML round trip (<sheet_notes><note>)");
    CHECK(!anyHas(xmlNotes, { QStringLiteral("sheet_notes") }), "... read, not skipped as unknown");
    back = StationLayout::Layout();
    CHECK(StationLayout::fromSheets(StationLayout::toSheets(skp), &back) && back.sheetNotes == skp.sheetNotes
              && back.otherSheets.isEmpty(),
          "the tool's sheets: a sheet_notes sheet, read back as notes (not an other sheet)");
    const StationLayout::Diff dd = StationLayout::diff(ib, skp);
    CHECK(anyHas(dd.other, { QStringLiteral("sheet notes: 0 -> 13") }), "Compare says the notes differ");

    // ---- LPI pages 1 and 12 ----
    StationLayout::Layout lpi;
    CHECK(StationLayout::load(QString::fromLatin1(kDir) + QStringLiteral("LPI_extract/station_527.json"), &lpi, &err),
          "LPI's station_527.json opens");
    CHECK(lpi.tags.size() == 12 && lpi.sheetNotes.size() == 1
              && lpi.sheetNotes.first().contains(QStringLiteral("tag 6D: duplication (Y31) printed 1, the pages hold 0")),
          "12 tags; 6D's pages say main tag");
    CHECK(anyHas(StationLayout::checks(lpi), { QStringLiteral("6D") }), "the layout's own checks see a tag named 6D that is a main tag");

    // ---- the window: Checks ----
    StationLayoutWindow w;
    CHECK(w.openFile(QString::fromLatin1(kDir) + QStringLiteral("SKP_extract/station_523.json")), "Station Layout opens it");
    QString title;
    QListWidget *list = checksList(w, &title);
    const QStringList shown = items(list);
    int sheet = 0;
    for (const QString &s : shown) sheet += s.startsWith(QStringLiteral("Sheet: "));
    CHECK(list && sheet == 13, "Checks lists the 13 sheet notes as \"Sheet: ...\"");
    CHECK(anyHas(shown, { QStringLiteral("Sheet: KAVACH_RFID_TAG_DATA_SKP_V2.0.0_pages16-23.html p18, tag 388D") }),
          "each names the sheet, its page and the tag");
    CHECK(title == QStringLiteral("Checks (%1)").arg(shown.size()), "the tab counts them with the layout's own");
    CHECK(w.openFile(QString::fromLatin1(kDir) + QStringLiteral("NPL-TLPR_IB/station_526.json")), "the clean one opens");
    list = checksList(w, &title);
    CHECK(items(list) == QStringList{ QStringLiteral("Nothing to report") } && title == QStringLiteral("Checks"),
          "nothing to report");
}
