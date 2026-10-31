#include "testutil.h"

#include "stationlayout.h"
#include "stationlayoutwindow.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTableWidget>
#include <QTemporaryDir>

#include <algorithm>

// =============================================================================
//  Session 431 — the station XML (station_<id>.xml, "kavsite-xml 1"), opened
//  and saved by Station Layout. The fixture is a real one: station_527.xml,
//  Lingampalli (LPI), as the kav_sim Config Maker wrote it (203 tags, 6
//  lines, 16 points, 49 signals of which 14 a route's, 31 track circuits,
//  222 relay-map rows, 1 route, the TIN index).
// =============================================================================

namespace {

QByteArray fixture527()
{
    QFile f(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_527.xml"));
    if (!f.open(QIODevice::ReadOnly)) return QByteArray();
    return f.readAll().replace("\r\n", "\n");   // a Windows checkout has CRLF
}

const XlsxBook::Sheet *sheetOf(const StationLayout::Layout &l, const QString &name)
{
    for (const XlsxBook::Sheet &sh : l.otherSheets)
        if (sh.name == name) return &sh;
    return nullptr;
}

// The other sheets by name: the XML writes them in its own order.
StationLayout::Layout byName(StationLayout::Layout l)
{
    std::sort(l.otherSheets.begin(), l.otherSheets.end(),
              [](const XlsxBook::Sheet &a, const XlsxBook::Sheet &b) { return a.name < b.name; });
    return l;
}

} // namespace

TEST_SUITE(session431)
{
    const QByteArray real = fixture527();
    CHECK(!real.isEmpty(), "the fixture station_527.xml is there");

    // ---- read ----
    StationLayout::Layout l;
    QString err;
    QStringList notes;
    CHECK(StationLayout::fromXml(real, &l, &err, &notes), "station_527.xml reads");
    CHECK(notes.isEmpty(), "every tag's fields agree with its hex; nothing left unread");
    CHECK(l.stations.size() == 1 && l.stations.first().id == 527 && l.stations.first().location == 161060, "station 527 at 161.060 km");
    CHECK(l.tags.size() == 203 && l.lines.size() == 6 && l.points.size() == 16 && l.signalList.size() == 49,
          "203 tags, 6 lines, 16 points, 49 signals");
    CHECK(l.tags.first().name == QStringLiteral("981") && l.tags.first().pageY == QStringLiteral("62c1bd9c15010781")
              && l.tags.first().pageX == QStringLiteral("0829a9828078f559"),
          "hex = page_y then page_x");
    const StationLayout::TagInfo t981 = StationLayout::info(l.tags.first());
    CHECK(t981.ok && t981.absLoc == 163960 && t981.tinNom == 83 && t981.tinRev == 83 && t981.placement == 2 && t981.type == 9,
          "tag 981 decodes as the file says: 163960 m, TIN 83 / 83, placement 2, normal");
    CHECK(l.lines.first().name == QStringLiteral("DN") && l.lines.first().line == QStringLiteral("DM")
              && l.lines.first().tags.mid(0, 3) == (QStringList{ QStringLiteral("148"), QStringLiteral("150"), QStringLiteral("550") }),
          "a line's <tag id> children, in order");
    const StationLayout::Point *p21 = nullptr;
    for (const StationLayout::Point &p : l.points) if (p.name == QStringLiteral("P21")) p21 = &p;
    CHECK(p21 && p21->connX == QStringLiteral("161217,161305") && p21->connLine == QStringLiteral("DL3,DL1")
              && p21->connTin == QStringLiteral("62,62") && p21->tags.isEmpty(),
          "point P21: its two <conn> back as the three comma lists");
    int routeSignals = 0;
    for (const StationLayout::Signal &s : l.signalList) if (s.route == QStringLiteral("DN_TLP_LNGP_CRC")) ++routeSignals;
    CHECK(routeSignals == 14, "14 signals of the route DN_TLP_LNGP_CRC (no StationId)");
    const XlsxBook::Sheet *circuits = sheetOf(l, QStringLiteral("circuits"));
    const XlsxBook::Sheet *relays = sheetOf(l, QStringLiteral("relaymap"));
    const XlsxBook::Sheet *routes = sheetOf(l, QStringLiteral("routes"));
    CHECK(circuits && circuits->rows.size() == 32 && relays && relays->rows.size() == 223 && routes && routes->rows.size() == 2,
          "circuits (31), relaymap (222) and routes (1) as the Python tool's sheets, under their header");
    CHECK(relays && relays->rows.at(1).value(4).userType() != QMetaType::QString && relays->rows.at(1).value(1).toString() == QStringLiteral("S50"),
          "relaymap: BitPos a number, EqipName text");
    CHECK(routes && routes->rows.at(1).value(2).toString().startsWith(QStringLiteral("148,148D,150")), "the route's tags as one comma list");

    // ---- write: the same file, byte for byte ----
    QStringList wnotes;
    const QByteArray again = StationLayout::toXml(l, QStringLiteral("station_527"), &wnotes);
    CHECK(again == real, "written back, station_527.xml is the same byte for byte (tags' fields from their bits, the TIN index derived)");
    CHECK(wnotes.isEmpty(), "and nothing it could not hold");

    // ---- a field written by hand that the bits do not say ----
    QByteArray edited = real;
    edited.replace("tag=\"981\" type=\"normal\" location=\"163960\"", "tag=\"981\" type=\"normal\" location=\"163961\"");
    StationLayout::Layout e;
    notes.clear();
    CHECK(StationLayout::fromXml(edited, &e, &err, &notes)
              && notes == QStringList{ QStringLiteral("tag 981: location says 163961, its bits 163960 (the bits are kept)") },
          "a location the hex does not hold: named, the bits kept");
    CHECK(StationLayout::info(e.tags.first()).absLoc == 163960, "the tag is where its bits put it");
    QByteArray noHex = real;
    noHex.replace(" hex=\"62c1bd9c150107810829a9828078f559\"", "");
    notes.clear();
    CHECK(StationLayout::fromXml(noHex, &e, &err, &notes) && e.tags.size() == 202 && notes.size() == 1
              && notes.first().startsWith(QStringLiteral("tag 981: no hex")),
          "a tag without its hex is not guessed: skipped and named");
    CHECK(!StationLayout::fromXml("<station/>", &e, &err) && err.contains(QStringLiteral("<kavsite>")), "not a kavsite file: refused");
    CHECK(!StationLayout::fromXml("<kavsite><tags><tag", &e, &err) && err.startsWith(QStringLiteral("XML error")), "broken XML: refused");

    // ---- every built-in station file through the XML and back ----
    // The tool's files have notes in cells under no header (relaymap's "Red",
    // "NORMAL") and sheets lacking a column (Originals/station_lingampalli's
    // relaymap has no StationId): kept, by place and by the columns used. The
    // sheets come back in the XML's order (the tool finds them by name).
    QStringList lost;
    int files = 0;
    for (const QString &path : StationLayout::library()) {
        StationLayout::Layout a, b;
        if (!StationLayout::load(path, &a)) { lost << path + QStringLiteral(" (does not load)"); continue; }
        ++files;
        if (!StationLayout::fromXml(StationLayout::toXml(a, QStringLiteral("x")), &b) || StationLayout::toJson(byName(a)) != StationLayout::toJson(byName(b)))
            lost << path;
    }
    CHECK(files >= 40 && lost.isEmpty(), qPrintable(QStringLiteral("all %1 built-in station files: through the XML, every sheet as it was%2")
                                                       .arg(files).arg(lost.isEmpty() ? QString() : QStringLiteral(" — not: ") + lost.join(QStringLiteral(", ")))));

    // ---- the KAVACH data and an unnamed sheet ride along ----
    {
        StationLayout::Layout k = l;
        QFile kf(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_kavach/section30_527.json"));
        CHECK(kf.open(QIODevice::ReadOnly), "the KAVACH fixture");
        k.kavach = QJsonDocument::fromJson(kf.readAll()).object();
        XlsxBook::Sheet extra;
        extra.name = QStringLiteral("notes & more");
        extra.rows << XlsxBook::Row{ QStringLiteral("what"), QStringLiteral("n") }
                   << XlsxBook::Row{ QStringLiteral("a < b"), qlonglong(7) } << XlsxBook::Row{ QVariant(), 2.5 };
        k.otherSheets << extra;
        StationLayout::Layout back;
        CHECK(!k.kavach.isEmpty() && StationLayout::fromXml(StationLayout::toXml(k, QStringLiteral("k")), &back, &err)
                  && back.kavach == k.kavach && StationLayout::toJson(back) == StationLayout::toJson(k),
              "the KAVACH data (as JSON in <kavach>) and any other sheet (<sheet name>) come back as they were");
    }

    // ---- the window: open it, Save as… it ----
    QTemporaryDir dir;
    StationLayoutWindow w;
    CHECK(w.openFile(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_527.xml")), "Station Layout opens station_527.xml");
    CHECK(w.station().tags.size() == 203 && w.suggestedXmlName() == QStringLiteral("station_527.xml"),
          "its tags, and Save as… offers station_527.xml");
    QTableWidget *sig = w.table(1);
    int routeCol = -1;
    for (int c = 0; sig && c < sig->columnCount(); ++c)
        if (sig->horizontalHeaderItem(c)->text() == QStringLiteral("route")) routeCol = c;
    CHECK(routeCol >= 0 && sig->item(48, routeCol) && sig->item(48, routeCol)->text() == QStringLiteral("DN_TLP_LNGP_CRC"),
          "the Signals tab has a route column: the route's signals say which");
    sig->item(0, 1)->setText(QStringLiteral("S1640A"));   // an edit through the sheet keeps the routes
    int still = 0;
    for (const StationLayout::Signal &s : w.station().signalList) if (!s.route.isEmpty()) ++still;
    CHECK(w.station().signalList.first().name == QStringLiteral("S1640A") && still == 14, "editing a signal keeps the route signals'");
    const QString out = dir.filePath(QStringLiteral("station_527.xml"));
    CHECK(w.saveFile(out), "saved as XML");
    QFile saved(out);
    CHECK(saved.open(QIODevice::ReadOnly), "the saved file");
    QByteArray expect = real;
    expect.replace("signal=\"S1640\"", "signal=\"S1640A\"");
    CHECK(saved.readAll() == expect, "the saved file: station_527.xml with the one edit");
    StationLayout::Layout reopened;
    CHECK(StationLayout::load(out, &reopened) && reopened.signalList.first().name == QStringLiteral("S1640A"), "and it opens again");
}
