#include "testutil.h"

#include "rfidtag.h"
#include "stationlayout.h"
#include "xlsxbook.h"

#include <QDir>

#include <cstdio>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>

// =============================================================================
//  Session 208 — station layout files: XlsxBook (zip + sheet xml) and the
//  StationLayout model, JSON and the old Python tool's .xlsx.
//
//  tests/fixtures/station_layout_synthetic.xlsx (in git, made by
//  scripts/make_station_layout_fixture.py) is deflated with a shared-strings
//  table, as Excel and openpyxl write them; its three tags are real ones
//  from tag_scenarios/. The real station files (tests/fixtures/
//  station_layout/, git-ignored, from DEBUGGING_TOOL_KAVACH) are checked
//  when present.
// =============================================================================

namespace {

QString fixture(const QString &name) { return QStringLiteral(DL_SRC_DIR "/tests/fixtures/") + name; }

const XlsxBook::Sheet *sheetNamed(const QVector<XlsxBook::Sheet> &v, const QString &name)
{
    for (const XlsxBook::Sheet &s : v) if (s.name == name) return &s;
    return nullptr;
}

}  // namespace

TEST_SUITE(session208)
{
    // ---- the pieces ----------------------------------------------------------------
    CHECK(XlsxBook::crc32("123456789") == 0xCBF43926u, "CRC-32 check value");
    CHECK(XlsxBook::columnName(0) == QLatin1String("A") && XlsxBook::columnName(25) == QLatin1String("Z")
          && XlsxBook::columnName(26) == QLatin1String("AA") && XlsxBook::columnName(701) == QLatin1String("ZZ")
          && XlsxBook::columnName(702) == QLatin1String("AAA"), "column names A, Z, AA, ZZ, AAA");
    {
        bool ok = true;
        XlsxBook::inflate(QByteArray("\xff\xff\xff", 3), &ok);
        CHECK(!ok, "a corrupt deflate stream is refused");
        QString err;
        CHECK(XlsxBook::read("not a zip at all", &err).isEmpty() && !err.isEmpty(), "not a zip: refused with a reason");
    }

    // ---- a deflated workbook with shared strings ---------------------------------------
    QString err;
    const QVector<XlsxBook::Sheet> syn = XlsxBook::readFile(fixture(QStringLiteral("station_layout_synthetic.xlsx")), &err);
    CHECK(syn.size() == 7, QByteArray("synthetic fixture: seven sheets (") + err.toUtf8() + ")");
    QStringList names;
    for (const XlsxBook::Sheet &s : syn) names << s.name;
    CHECK(names.join(QLatin1Char(',')) == QLatin1String("tags,signals,points,lines,relaymap,station,texts"), "sheets in workbook order");
    if (const XlsxBook::Sheet *t = sheetNamed(syn, QStringLiteral("texts")))
        CHECK(t->rows.value(1).value(0).toString() == QLatin1String("<< A & B >>"), "shared string with markup characters");
    if (const XlsxBook::Sheet *s = sheetNamed(syn, QStringLiteral("signals"))) {
        CHECK(s->rows.value(1).value(2).userType() == QMetaType::LongLong, "a number cell reads as a whole number");
        CHECK(s->rows.value(1).value(0).userType() == QMetaType::QString, "a text cell reads as text");
    }

    StationLayout::Layout l;
    QStringList notes;
    CHECK(StationLayout::fromSheets(syn, &l, &err, &notes), QByteArray("synthetic fixture: a layout (") + err.toUtf8() + ")");
    CHECK(notes.isEmpty(), QByteArray("no notes: ") + notes.join(QStringLiteral("; ")).toUtf8());
    CHECK(l.tags.size() == 3 && l.signalList.size() == 2 && l.points.size() == 2 && l.lines.size() == 2
          && l.stations.size() == 1 && l.texts.size() == 1, "every row of every sheet");
    CHECK(l.otherSheets.size() == 1 && l.otherSheets.first().name == QLatin1String("relaymap")
          && l.otherSheets.first().rows.value(1).value(6).toString() == QLatin1String("Red"),
          "relaymap carried whole, its unlabelled 7th column too");
    CHECK(l.points.at(0).tags == QLatin1String("2") && l.points.at(1).tags == QLatin1String("2,4"),
          "a point's tags read the same whether the cell was a number or text");
    CHECK(l.lines.at(0).tags == QStringList({ "804", "2", "4" }), "a line's tags, in order");

    // The tag's own location, against the route file it came from
    // (tag_scenarios: rfid_id 804, abs_loc 159500).
    const StationLayout::TagInfo t804 = StationLayout::info(l.tags.at(0));
    CHECK(t804.ok && t804.unique == 804 && t804.absLoc == 159500 && t804.crcOk, "tag 804 decodes: abs_loc 159500, CRC good");
    CHECK(StationLayout::tagLocations(l).value(QStringLiteral("4")) == 160155, "tag 4 at 160155, as its route file says");
    CHECK(StationLayout::lineOfTag(l).value(QStringLiteral("2")) == QLatin1String("DM"), "a tag on two lines: the first wins");

    // ---- moving a tag re-encodes it -------------------------------------------------------
    {
        StationLayout::Tag moved = l.tags.at(0);
        CHECK(StationLayout::moveTag(&moved, 159550, &err), QByteArray("move tag 804 (") + err.toUtf8() + ")");
        const StationLayout::TagInfo m = StationLayout::info(moved);
        CHECK(m.ok && m.absLoc == 159550 && m.crcOk && m.unique == 804 && m.type == t804.type
              && m.tinNom == t804.tinNom && m.tinRev == t804.tinRev, "moved: new location, fresh CRC, the rest kept");
        CHECK(moved.pageX != l.tags.at(0).pageX || moved.pageY != l.tags.at(0).pageY, "page_x / page_y rewritten");
        StationLayout::Tag tooFar = l.tags.at(0);
        CHECK(!StationLayout::moveTag(&tooFar, qint64(1) << 40, &err) && tooFar.pageX == l.tags.at(0).pageX,
              "a location too wide for the field is refused, the tag untouched");
    }

    // ---- JSON and xlsx round trips ----------------------------------------------------------------
    const QByteArray json = StationLayout::toJson(l);
    {
        StationLayout::Layout back;
        CHECK(StationLayout::fromJson(json, &back, &err) && StationLayout::toJson(back) == json, "JSON round trip");
        CHECK(!StationLayout::fromJson("{\"format\":\"something-else\"}", &back, &err) && !err.isEmpty(),
              "a JSON file of another format is refused");
    }
    {
        const QByteArray x = XlsxBook::write(StationLayout::toSheets(l));
        StationLayout::Layout back;
        CHECK(StationLayout::fromSheets(XlsxBook::read(x, &err), &back, &err) && StationLayout::toJson(back) == json,
              "xlsx export, then import: the same layout");
        const QVector<XlsxBook::Sheet> out = XlsxBook::read(x);
        const XlsxBook::Sheet *sig = sheetNamed(out, QStringLiteral("signals"));
        const XlsxBook::Sheet *pts = sheetNamed(out, QStringLiteral("points"));
        CHECK(sig && sig->rows.value(1).value(0).userType() == QMetaType::QString
              && sig->rows.value(1).value(2).userType() == QMetaType::LongLong,
              "export: sig_foot_tag as text, sig_id as a number, as the tool's files have them");
        CHECK(pts && pts->rows.value(1).value(1).userType() == QMetaType::LongLong
              && pts->rows.value(1).value(5).userType() == QMetaType::QString,
              "export: point locations as numbers, its tags as text");
        CHECK(x == XlsxBook::write(StationLayout::toSheets(back)), "export is deterministic");
    }
    {
        QTemporaryDir dir;
        const QString jp = dir.filePath(QStringLiteral("a.json")), xp = dir.filePath(QStringLiteral("a.xlsx"));
        StationLayout::Layout a, b;
        CHECK(StationLayout::save(jp, l, &err) && StationLayout::save(xp, l, &err)
              && StationLayout::load(jp, &a, &err) && StationLayout::load(xp, &b, &err)
              && StationLayout::toJson(a) == json && StationLayout::toJson(b) == json,
              "save / load by extension: .json own file, .xlsx the tool's");
    }

    // ---- the real station files, when present ---------------------------------------------------
    const QStringList real = QDir(fixture(QStringLiteral("station_layout"))).entryList({ QStringLiteral("*.xlsx") });
    if (real.isEmpty()) {
        std::printf("  NOTE session208: tests/fixtures/station_layout/ is empty; real station files not checked\n");
    }
    for (const QString &f : real) {
        const QVector<XlsxBook::Sheet> sheets = XlsxBook::readFile(fixture(QStringLiteral("station_layout/")) + f, &err);
        StationLayout::Layout rl;
        notes.clear();
        CHECK(StationLayout::fromSheets(sheets, &rl, &err, &notes), QByteArray("real: ") + f.toUtf8() + " reads (" + err.toUtf8() + ")");
        const XlsxBook::Sheet *tags = sheetNamed(sheets, QStringLiteral("tags"));
        int tagRows = 0;
        if (tags) for (int r = 1; r < tags->rows.size(); ++r) tagRows += tags->rows.at(r).isEmpty() ? 0 : 1;
        CHECK(tags && rl.tags.size() == tagRows, QByteArray("real: ") + f.toUtf8() + ": every tag row read ("
              + QByteArray::number(rl.tags.size()) + " of " + QByteArray::number(tagRows) + "; "
              + notes.join(QStringLiteral("; ")).toUtf8() + ")");
        // Rows the file gets wrong are kept and reported, not dropped.
        const QStringList findings = StationLayout::checks(rl);
        if (!findings.isEmpty())
            std::printf("  NOTE session208: %s: %s\n", qPrintable(f), qPrintable(findings.join(QStringLiteral("; "))));
        if (f == QLatin1String("station_adjustment.xlsx"))
            CHECK(findings.contains(QStringLiteral("tag 820D: its bits say 828D"))
                  && findings.filter(QStringLiteral("tag 821D: page_x / page_y do not make a tag")).size() == 1,
                  "real: the file's two bad tag rows (820D holds 828D; 821D has a 17-digit page_y) are reported");
        const QByteArray rj = StationLayout::toJson(rl);
        StationLayout::Layout back;
        CHECK(StationLayout::fromSheets(XlsxBook::read(XlsxBook::write(StationLayout::toSheets(rl))), &back)
              && StationLayout::toJson(back) == rj, QByteArray("real: ") + f.toUtf8() + ": export then import keeps everything");
        CHECK(!rl.otherSheets.isEmpty() && rl.otherSheets.first().name == QLatin1String("relaymap"),
              QByteArray("real: ") + f.toUtf8() + ": relaymap carried");
    }
}

// ---- the window --------------------------------------------------------------------------------
#include "layoutaudit.h"
#include "stationlayoutwindow.h"
#include "theme.h"
#include "uistyle.h"

#include <QTabWidget>
#include <QTableWidget>

TEST_SUITE(session208_window)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    StationLayoutWindow w;
    w.resize(1180, 720);
    w.show();
    CHECK(w.openFile(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_layout_synthetic.xlsx")), "import the synthetic station file");
    CHECK(!w.isModified(), "an import is not a change");
    CHECK(w.table(0)->rowCount() == 3 && w.table(1)->rowCount() == 2 && w.table(2)->rowCount() == 2
          && w.table(3)->rowCount() == 2 && w.table(4)->rowCount() == 1 && w.table(5)->rowCount() == 1,
          "one table row per sheet row");
    CHECK(w.table(0)->item(0, 3)->text() == QLatin1String("159500"), "the Tags table shows the location inside tag 804");

    // Every tag, signal and point is drawn, with a tooltip.
    w.canvas()->grab();
    int tagHits = 0, signalHits = 0, pointHits = 0;
    for (const StationLayoutCanvas::Hit &h : w.canvas()->hits()) {
        if (!h.tag.isEmpty()) ++tagHits;
        if (h.text.startsWith(QLatin1String("Signal "))) ++signalHits;
        if (h.text.startsWith(QLatin1String("Point "))) ++pointHits;
    }
    CHECK(tagHits == 3 && signalHits == 2 && pointHits == 2, "three tags, two signals, two points drawn");
    // Tags along one lane are drawn in location order.
    CHECK(w.canvas()->tagRect(QStringLiteral("804")).center().x() < w.canvas()->tagRect(QStringLiteral("4")).center().x(),
          "tag 804 (159500 m) left of tag 4 (160155 m)");

    // Editing the location re-encodes the tag; Undo puts it back.
    const QString before = w.station().tags.at(0).pageY;
    w.table(0)->item(0, 3)->setText(QStringLiteral("159520"));
    const StationLayout::TagInfo moved = StationLayout::info(w.station().tags.at(0));
    CHECK(moved.absLoc == 159520 && moved.crcOk && w.isModified(), "typed location: tag moved, CRC-30 good, window modified");
    CHECK(w.undo() && w.station().tags.at(0).pageY == before, "Undo restores the tag's bits");

    // What a drag does.
    CHECK(w.moveTag(QStringLiteral("2"), 159700), "drag tag 2 to 159700 m");
    CHECK(StationLayout::info(w.station().tags.at(1)).absLoc == 159700, "tag 2 now at 159700 m");
    CHECK(!w.moveTag(QStringLiteral("2"), qint64(1) << 40) && StationLayout::info(w.station().tags.at(1)).absLoc == 159700,
          "a drag to a location the field cannot hold is refused");
    CHECK(w.canvas()->locAt(w.canvas()->xOf(159700)) == 159700 || std::abs(w.canvas()->locAt(w.canvas()->xOf(159700)) - 159700) <= 2,
          "screen x and location convert both ways");

    // Editing a generic sheet goes through the sheet reader.
    w.table(1)->item(0, 1)->setText(QStringLiteral("S10A"));
    CHECK(w.station().signalList.at(0).name == QLatin1String("S10A"), "signal renamed from its table");
    w.table(1)->item(0, 2)->setText(QStringLiteral("four"));
    CHECK(w.station().signalList.at(0).sigId == 0, "a sig_id that is not a number reads as 0 (and is said)");

    // Save / reopen / export.
    QTemporaryDir dir;
    const QString jp = dir.filePath(QStringLiteral("s.json")), xp = dir.filePath(QStringLiteral("s.xlsx"));
    CHECK(w.saveFile(jp) && !w.isModified(), "saved as .json");
    CHECK(w.exportXlsx(xp), "exported .xlsx");
    StationLayout::Layout fromX;
    CHECK(StationLayout::load(xp, &fromX) && StationLayout::toJson(fromX) == StationLayout::toJson(w.station()),
          "the exported .xlsx holds the edited layout");
    StationLayoutWindow w2;
    CHECK(w2.openFile(jp) && StationLayout::toJson(w2.station()) == StationLayout::toJson(w.station()),
          "the .json reopens to the same layout");

    CHECK(LayoutAudit::orphans(&w).isEmpty(), "no widget outside a layout");
    CHECK(w.minimumSizeHint().width() <= 1100 && w.minimumSizeHint().height() <= 700,
          QByteArray("fits 1100 x 700 (") + QByteArray::number(w.minimumSizeHint().width()) + " x "
              + QByteArray::number(w.minimumSizeHint().height()) + ")");
    w.close();
}
