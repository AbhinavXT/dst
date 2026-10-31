#include "testutil.h"

#include "stationkavach.h"
#include "stationkavacheditor.h"
#include "stationlayout.h"
#include "stationprofile.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTabWidget>
#include <QTableWidget>

// =============================================================================
//  Session 462 — a route's speed and gradient profile filled in as the
//  firm's track profile sheet writes it (StationProfile), and in the KAVACH
//  tab: pasted, made to cover the route, a row split, read in km/h and
//  "1 in N". Real data: HBL's LPI track profile table 2.0.3 (the text as
//  printed) and the rows kavach_sheets.py made of it, which station 527's
//  own SLRPs of 26 Jun 2026 carry for S50-S48 (session 456).
// =============================================================================

TEST_SUITE(session462)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/schema/fixtures/kavach_sheets/LPI_routes/station_527.json"));
    CHECK(f.open(QIODevice::ReadOnly), "HBL's LPI routes");
    StationLayout::Layout lay;
    QString err;
    CHECK(StationLayout::fromJson(f.readAll(), &lay, &err), QByteArray("read ") + err.toUtf8());
    StationKavach::Station st;
    CHECK(StationKavach::fromJson(lay.kavach, &st, &err) && st.routes.size() == 8, "8 routes");
    const StationKavach::Route &main = st.routes.at(0);   // S50-S48 Down Main, 1670 m

    // ---- SSP, as the sheet prints it ----
    const QVector<StationProfile::Row> ssp = StationProfile::parseSsp(QStringLiteral("(U,110,1670)"), &err);
    CHECK(ssp == main.ssp, QByteArray("(U,110,1670): the row S50-S48 carries (1670 m, universal 22 = 110 km/h) ") + err.toUtf8());
    const QVector<StationProfile::Row> two = StationProfile::parseSsp(QStringLiteral("1000 110\n670 m 80 km/h"), &err);
    CHECK(two.size() == 2 && two[1].value(QStringLiteral("d")) == 670 && two[1].value(QStringLiteral("u")) == 16, "\"length speed\" lines too");
    CHECK(StationProfile::parseSsp(QStringLiteral("(U,112,1670)"), &err).isEmpty() && err.contains(QStringLiteral("5 km/h")),
          "112 km/h: refused, not rounded");
    CHECK(StationProfile::parseSsp(QStringLiteral("(A,100,500)"), &err).isEmpty() && err.contains(QStringLiteral("category")),
          "category speeds: refused by name");
    CHECK(StationProfile::parseSsp(QStringLiteral("(U,110,40000)"), &err).isEmpty() && err.contains(QStringLiteral("32767")),
          "a length past 15 bits: refused");

    // ---- gradient, as the sheet prints the averages ----
    const QVector<StationProfile::Row> g = StationProfile::parseGradient(QStringLiteral("F 1 in 209 1245\nLEVEL 320\nR 1 in 260 105"), &err);
    CHECK(g == main.gradient, QByteArray("F 1 in 209 1245, LEVEL 320, R 1 in 260 105: S50-S48's rows (value 4, 0, 3) ") + err.toUtf8());
    const QVector<StationProfile::Row> paren = StationProfile::parseGradient(QStringLiteral("(F 1 in 150, 445) (F 1 in 260, 740) (Level,320)"), &err);
    CHECK(paren.size() == 3 && paren[0].value(QStringLiteral("value")) == 6 && paren[1].value(QStringLiteral("value")) == 3
              && paren[2].value(QStringLiteral("value")) == 0, "the bracketed form, several on a line: 1000/150 = 6, 1000/260 = 3, level 0");
    CHECK(StationProfile::parseGradient(QStringLiteral("F 1 in 20 100"), &err).isEmpty() && err.contains(QStringLiteral("31")),
          "1 in 20 (50 per thousand): past the 5-bit value, refused");
    CHECK(StationProfile::parseGradient(QStringLiteral("down a bit 100"), &err).isEmpty(), "anything else: refused");

    // ---- cover and split ----
    QVector<StationProfile::Row> rows = StationProfile::parseSsp(QStringLiteral("(U,110,1000)"));
    rows = StationProfile::cover(rows, main.length(), StationProfile::Row{ { "class", 0 }, { "u", 16 } }, &err);
    CHECK(rows.size() == 2 && StationProfile::covered(rows) == 1670 && rows[1].value(QStringLiteral("u")) == 16,
          "covered: the 670 m left at 80 km/h");
    CHECK(StationProfile::cover(rows, 1600, StationProfile::Row{}, &err).size() == 2 && err.contains(QStringLiteral("1670")),
          "rows past the route: refused, left as they were");
    CHECK(StationProfile::split(&rows, 500, &err) && rows.size() == 3 && rows[0].value(QStringLiteral("d")) == 500
              && rows[1].value(QStringLiteral("d")) == 500 && StationProfile::covered(rows) == 1670,
          "split at 500 m: 500 + 500, still 1670");
    CHECK(!StationProfile::split(&rows, 1000, &err) && !StationProfile::split(&rows, 2000, &err), "at a row's end, or past them: refused");
    CHECK(StationProfile::speedText(main.ssp[0]) == QStringLiteral("110 km/h") && StationProfile::gradientText(main.gradient[0]).startsWith(QStringLiteral("F 4"))
              && StationProfile::gradientText(main.gradient[1]) == QStringLiteral("Level"),
          "in words: 110 km/h, F 4 per thousand, Level");

    // ---- the KAVACH tab ----
    StationKavachEditor ed;
    ed.setSeedSource(&lay);
    ed.setKavach(lay.kavach);
    QObject::connect(&ed, &StationKavachEditor::edited, &ed, [&ed](const QJsonObject &k, const QString &) { ed.setKavach(k); });
    ed.selectRoute(0);
    CHECK(ed.pasteProfile(StationKavachEditor::Gradient, QStringLiteral("F 1 in 150 1000\nLEVEL 670")), "Paste… onto S50-S48's gradient");
    CHECK(ed.station().routes.at(0).gradient.size() == 2 && ed.station().routes.at(0).gradient[0].value(QStringLiteral("value")) == 6,
          "its rows replaced: one edit");
    CHECK(!ed.pasteProfile(StationKavachEditor::Gradient, QStringLiteral("F 1 in 150 1000")) && ed.station().routes.at(0).gradient.size() == 2,
          "rows that cover 1000 of 1670 m: refused, the station would refuse its answer");
    CHECK(ed.splitProfile(StationKavachEditor::Ssp, 800) && ed.station().routes.at(0).ssp.size() == 2, "Split at 800 m: two SSP rows");
    StationKavach::Route r = ed.station().routes.at(0);
    QTableWidget *t = ed.listTable(StationKavachEditor::Ssp);
    bool kmh = false, from = false;
    for (int c = 0; c < t->columnCount(); ++c) {
        const QString h = t->horizontalHeaderItem(c)->text();
        if (h == QStringLiteral("km/h")) kmh = t->item(0, c)->text() == QStringLiteral("110 km/h");
        if (h == QStringLiteral("From entry (m)")) from = t->item(1, c)->text() == QStringLiteral("800");
    }
    CHECK(kmh && from, "the SSP table shows each row's speed in km/h and where it starts");
    QTableWidget *gt = ed.listTable(StationKavachEditor::Gradient);
    bool inN = false;
    for (int c = 0; c < gt->columnCount(); ++c)
        if (gt->horizontalHeaderItem(c)->text() == QStringLiteral("Gradient")) inN = gt->item(1, c)->text() == QStringLiteral("Level");
    CHECK(inN, "the gradient table shows Level / F … in words");
    // The helper columns are not read back as fields.
    t->item(0, 0)->setText(QStringLiteral("700"));
    t->item(1, 0)->setText(QStringLiteral("970"));
    bool clean = true;
    for (const StationProfile::Row &x : ed.station().routes.at(0).ssp)
        for (const QString &k : x.keys()) clean = clean && !k.contains(QLatin1Char(' ')) && k != QStringLiteral("km/h");
    CHECK(clean && ed.station().routes.at(0).ssp[0].value(QStringLiteral("d")) == 700, "a length typed: d changed, the helper columns not taken as fields");
    CHECK(ed.coverProfile(StationKavachEditor::Ssp, 80) == false && ed.station().routes.at(0).ssp.size() == 2,
          "Cover the route when the rows already do: refused");
}
