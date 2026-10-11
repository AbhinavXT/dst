#include "testutil.h"

#include "settings.h"
#include "stationlayout.h"
#include "stationlayoutwindow.h"
#include "xlsxbook.h"

#include <QTemporaryDir>

// =============================================================================
//  Session 237 — a station made from scratch in Station Layout opens in the
//  Python debugging tool. The tool's load_station reads the "relaymap" sheet
//  unconditionally (NMS field-input bit positions) and stopped with
//  "Worksheet named 'relaymap' not found" on such a file; Export now writes
//  an empty relaymap (its header row) when the layout carries none. Checked
//  against the tool's own load_station (pandas) on 2026-10-11: tags, signal,
//  point, line, station and text all read.
// =============================================================================

namespace {

const XlsxBook::Sheet *sheet(const QVector<XlsxBook::Sheet> &book, const QString &name)
{
    for (const XlsxBook::Sheet &s : book)
        if (s.name == name) return &s;
    return nullptr;
}

int count(const QVector<XlsxBook::Sheet> &book, const QString &name)
{
    int n = 0;
    for (const XlsxBook::Sheet &s : book) n += s.name == name ? 1 : 0;
    return n;
}

} // namespace

TEST_SUITE(session237)
{
    QTemporaryDir tmp;
    StationLayout::Layout fresh;
    fresh.stations << StationLayout::Station{ 900, 100500 };
    fresh.texts << StationLayout::Text{ QStringLiteral("NEW STATION"), 100500, -1 };
    fresh.lines << StationLayout::Line{ QStringLiteral("DN"), QStringLiteral("DM"), {} };

    // the window's Export .xlsx
    StationLayoutWindow w;
    w.setStation(fresh);
    const QString a = tmp.filePath(QStringLiteral("new.xlsx"));
    CHECK(w.exportXlsx(a), "a new station exports");
    QString err;
    const QVector<XlsxBook::Sheet> book = XlsxBook::readFile(a, &err);
    QStringList names;
    for (const XlsxBook::Sheet &s : book) names << s.name;
    for (const char *n : { "tags", "signals", "points", "lines", "station", "texts", "relaymap" })
        CHECK(names.contains(QLatin1String(n)), QByteArray("the tool's sheet ") + n + " is there (" + names.join(',').toUtf8() + ")");
    const XlsxBook::Sheet *relay = sheet(book, QStringLiteral("relaymap"));
    QStringList header;
    if (relay && !relay->rows.isEmpty())
        for (const QVariant &v : relay->rows.first()) header << v.toString();
    CHECK(relay && relay->rows.size() == 1
              && header == QStringList({ "EqipType", "EqipName", "EqipId", "EqipAttr", "BitPos", "StationId" }),
          "relaymap: the tool's header row, no rows");

    // StationLayout::save to .xlsx does the same
    const QString b = tmp.filePath(QStringLiteral("saved.xlsx"));
    CHECK(StationLayout::save(b, fresh, &err) && count(XlsxBook::readFile(b), QStringLiteral("relaymap")) == 1,
          "saving as .xlsx writes it too");

    // read back and exported again: still one relaymap
    StationLayout::Layout back;
    CHECK(StationLayout::load(a, &back, &err), "the export reads back");
    const QString c = tmp.filePath(QStringLiteral("again.xlsx"));
    CHECK(StationLayout::save(c, back, &err) && count(XlsxBook::readFile(c), QStringLiteral("relaymap")) == 1,
          "read back and saved again: one relaymap, not two");

    // a real station's relaymap is kept as it is, not replaced
    StationLayout::Layout real;
    CHECK(StationLayout::load(StationLayout::defaultFile(), &real, &err), "station.xlsx");
    const QString d = tmp.filePath(QStringLiteral("real.xlsx"));
    const QVector<XlsxBook::Sheet> realOut = (StationLayout::save(d, real, &err), XlsxBook::readFile(d));
    const XlsxBook::Sheet *rr = sheet(realOut, QStringLiteral("relaymap"));
    CHECK(count(realOut, QStringLiteral("relaymap")) == 1 && rr && rr->rows.size() > 10,
          "station.xlsx keeps its own relaymap rows");
}
