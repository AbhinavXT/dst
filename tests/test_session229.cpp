#include "testutil.h"

#include "settings.h"
#include "stationlayoutwindow.h"

#include <QFile>
#include <QTemporaryDir>

// =============================================================================
//  Session 229 — Station Layout: the tag table, PDF or CSV.
//  The built-in station.xlsx (session 210: 211 tags, station 527 at
//  161060 m, tag 981 at 163960 m; session 226: 910 at 164610 m on DM,
//  signal S1647 at it; session 227: 906D, the duplicate of 906, on DM).
// =============================================================================

TEST_SUITE(session229)
{
    Settings::setStationLayoutLastFile(QString());
    StationLayoutWindow w;
    CHECK(w.reopenLast(), "the default opens");
    const QVector<StationLayoutWindow::TagRow> rows = w.tagTable();
    CHECK(rows.size() == 211, "one row per tag");
    StationLayoutWindow::TagRow r910;
    bool ordered = true;
    for (int i = 0; i < rows.size(); ++i) {
        if (rows.at(i).name == QLatin1String("910")) r910 = rows.at(i);
        if (i > 0 && rows.at(i).line == rows.at(i - 1).line) ordered = ordered && rows.at(i).locM >= rows.at(i - 1).locM;
    }
    CHECK(ordered, "by line, then location");
    CHECK(r910.line == QLatin1String("DM") && r910.locM == 164610 && r910.signalNames.contains(QStringLiteral("S1647")) && r910.crc == QStringLiteral("matches"),
          QByteArray("910: DM, 164.610 km, S1647, CRC matches (") + r910.line.toUtf8() + " " + QByteArray::number(r910.locM) + " " + r910.signalNames.toUtf8() + ")");
    // Session 227's run: 906D rides 906 on DM.
    bool dupMain = false;
    for (const auto &r : rows) if (r.name == QLatin1String("906D")) dupMain = r.mainTag == QLatin1String("906") && r.line == QLatin1String("DM");
    CHECK(dupMain, "906D: duplicate of 906, on its line");
    bool kinds = true;
    for (const auto &r : rows) kinds = kinds && (!r.decodes || !r.kind.isEmpty());
    CHECK(kinds, "every tag that decodes has its kind");

    QTemporaryDir tmp;
    const QString csv = tmp.filePath(QStringLiteral("tags.csv")), pdf = tmp.filePath(QStringLiteral("tags.pdf"));
    CHECK(w.exportTagTable(csv) && w.exportTagTable(pdf), "CSV and PDF written");
    QFile f(csv);
    f.open(QIODevice::ReadOnly);
    const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    CHECK(lines.size() == 212 && lines.first().startsWith(QStringLiteral("Tag,Line,Location (km),Kind,TIN nom,TIN rev")),
          "the CSV: a header and 211 rows");
    bool has910 = false;
    for (const QString &l : lines) has910 = has910 || l.startsWith(QStringLiteral("910,DM,164.610,"));
    CHECK(has910, "910,DM,164.610,... in the CSV");
    QFile p(pdf);
    p.open(QIODevice::ReadOnly);
    const QByteArray head = p.read(5);
    CHECK(head == "%PDF-" && p.size() > 5000, QByteArray("a PDF (") + QByteArray::number(p.size()) + " bytes)");
}
