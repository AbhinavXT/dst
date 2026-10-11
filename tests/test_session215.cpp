#include "testutil.h"

#include "fleetwindow.h"
#include "statusline.h"
#include "theme.h"
#include "uistyle.h"

#include <QDir>
#include <QFile>
#include <QTableWidget>
#include <QTemporaryDir>
#include <cmath>

// =============================================================================
//  Session 215 — the fleet summary.
//  A folder made of real fixtures: the two 81_1 excerpts of 2026-10-08
//  (loco_1_1_radiolinks.log, 04:55-05:03 UTC; loco_1_1_odometry.log,
//  05:28-08:27 UTC: one loco-day, two files) and loco_2_1_08102026_162008.cap
//  in a subfolder (loco 2_1, the same day), plus a .log that is not
//  DLConsole's (left out).
//  Expected for 81_1, from the reports already tested on the same lines
//  (213, 214) and schema/engine.py for the rest: SLRP 115 received, 7 lost
//  (5.7 %), 2 outages; odometry 20 reads, median 0.56 m; 10 EB, no FSB (@dmi
//  brake_type 4 spells); top speed 110 km/h; 6.444 km (DMI location steps,
//  speed known, location known, steps over 500 m left out).
// =============================================================================

TEST_SUITE(session215)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "a scratch folder");
    const QString src = QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/");
    QDir(tmp.path()).mkdir(QStringLiteral("loco2"));
    bool copied = QFile::copy(src + QStringLiteral("loco_1_1_radiolinks.log"), tmp.filePath(QStringLiteral("loco_1_1_radiolinks.log")))
               && QFile::copy(src + QStringLiteral("loco_1_1_odometry.log"), tmp.filePath(QStringLiteral("loco_1_1_odometry.log")))
               && QFile::copy(src + QStringLiteral("loco_2_1_08102026_162008.cap"), tmp.filePath(QStringLiteral("loco2/loco_2_1_08102026_162008.cap")));
    QFile other(tmp.filePath(QStringLiteral("notes.log")));
    copied = copied && other.open(QIODevice::WriteOnly) && other.write("some other program's log\n2026-10-08 @dmi nothing\n") > 0;
    other.close();
    CHECK(copied, "fixtures copied");

    const QVector<Fleet::Group> groups = Fleet::scan(tmp.path());
    CHECK(groups.size() == 2, QByteArray("two loco-days, the stray .log left out (") + QByteArray::number(groups.size()) + ")");
    if (groups.size() != 2) return;
    CHECK(groups.at(0).loco == QStringLiteral("2_1") && groups.at(1).loco == QStringLiteral("81_1")
              && groups.at(1).files.size() == 2 && groups.at(1).files.at(0).endsWith(QStringLiteral("radiolinks.log"))
              && groups.at(1).day == QDate(2026, 10, 8),
          "2_1 from the .cap's name; 81_1 from the .log header, its two files in time order, one day");

    const Fleet::Row r = Fleet::summarise(groups.at(1));
    CHECK(r.loco == QStringLiteral("81_1") && r.error.isEmpty() && r.frames > 1000, "81_1 read, both files");
    CHECK(r.slrpReceived == 115 && r.slrpLost == 7 && r.outages == 2 && std::fabs(r.slrpLostPercent() - 700.0 / 122) < 1e-9,
          "SLRP: 115 received, 7 lost, 2 outages (as session 214)");
    CHECK(r.odoReads == 20 && std::fabs(r.odoMedianM - 0.56) < 0.01, "odometry: 20 reads, median 0.56 m (as session 213)");
    CHECK(r.eb == 10 && r.fsb == 0, QByteArray("10 EB, no FSB (") + QByteArray::number(r.eb) + " " + QByteArray::number(r.fsb) + ")");
    CHECK(r.maxSpeedKmh == 110, "top speed 110 km/h");
    CHECK(std::fabs(r.distanceKm - 6.444) < 0.001, QByteArray("6.444 km (") + QByteArray::number(r.distanceKm) + ")");
    CHECK(!r.modesText().isEmpty() && r.firstMs < r.lastMs, QByteArray("modes: ") + r.modesText().toUtf8());

    // Stop: the callback saying no ends it with an empty row.
    const Fleet::Row stopped = Fleet::summarise(groups.at(1), []() { return false; });
    CHECK(stopped.frames == 0 && stopped.loco.isEmpty(), "stopped mid-read: no row");

    const QString csv = Fleet::toCsv({ r });
    CHECK(csv.startsWith(QStringLiteral("loco,day,files,")) && csv.count(QLatin1Char('\n')) == 2
              && csv.contains(QStringLiteral("81_1,2026-10-08,loco_1_1_radiolinks.log; loco_1_1_odometry.log,")),
          "CSV: a header and the row");

    // The window.
    FleetWindow w;
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    w.show();
    w.run(tmp.path());
    CHECK(w.rows().size() == 2 && w.table()->rowCount() == 2, "the window: one row per loco-day");
    CHECK(w.table()->item(1, 0)->text() == QStringLiteral("81_1") && w.table()->item(1, 6)->data(Qt::DisplayRole).toInt() == 10,
          "81_1's row, EB 10 as a number (sorts as one)");
    if (!qgetenv("DL_SHOTS").isEmpty()) w.grab().save(QString::fromLocal8Bit(qgetenv("DL_SHOTS")) + QStringLiteral("/fleet.png"));
    w.run(tmp.filePath(QStringLiteral("nope")));
    CHECK(w.rows().size() == 2, "a folder that is not there: refused, the table kept");
    CHECK(w.minimumSizeHint().width() <= 1100 && w.minimumSizeHint().height() <= 700, "fits a laptop");
}
