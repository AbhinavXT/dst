#include "testutil.h"

#include "logmodel.h"
#include "settings.h"
#include "stationlayoutwindow.h"

#include <QDateTime>
#include <QFile>
#include <QTabWidget>
#include <QTableWidget>

namespace {
void readCap(LogModel *run, const QString &path)
{
    QFile f(path);
    QVector<LogEntryPtr> v;
    if (f.open(QIODevice::ReadOnly))
        while (!f.atEnd()) {
            const QString line = QString::fromUtf8(f.readLine()).trimmed();
            if (!line.startsWith(QLatin1Char('@'))) continue;
            auto e = QSharedPointer<LogEntry>::create();
            e->text = line;
            e->epochMs = QDateTime::fromString(line.section(QLatin1Char(' '), 1, 1), Qt::ISODate).toMSecsSinceEpoch();
            e->cacheDerived();
            v << e;
        }
    run->appendEntries(v);
}
}  // namespace

// =============================================================================
//  Session 227 — Station Layout: a loco's run over the layout.
//  Real frames: replay/loco_1_1_27062026_170217.cap (session 209's run on DM)
//  over the built-in station.xlsx. Expected, from schema/engine.py over the
//  capture's @rfid frames: main tags 12 (17:02:35), 14 (17:03:41), 16
//  (17:04:14), 18 (17:04:44), 906 and 906D (17:06:22), 898 (17:09:00) and
//  898D (17:09:01); no duplicate of 12, 14, 16 or 18 is read; tag 16 is read
//  with placement 0, where the layout's 16 says 2.
// =============================================================================

TEST_SUITE(session227)
{
    Settings::setStationLayoutLastFile(QString());
    StationLayoutWindow w;
    CHECK(w.reopenLast(), "the default opens");
    LogModel run(nullptr, 200000);
    readCap(&run, QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_170217.cap"));
    w.showRun(&run, QStringLiteral("1_1"));
    QHash<QString, StationLayoutWindow::RunTag> by;
    for (const StationLayoutWindow::RunTag &t : w.runTags()) by.insert(t.name, t);
    auto at = [](const char *hms) {
        return QDateTime::fromString(QStringLiteral("2026-06-27T") + QLatin1String(hms), Qt::ISODate).toMSecsSinceEpoch();
    };
    CHECK(w.runTags().size() == 12, QByteArray("twelve tags judged, all on DM (") + QByteArray::number(w.runTags().size()) + ")");
    bool dm = true;
    for (const StationLayoutWindow::RunTag &t : w.runTags()) dm = dm && t.line == QLatin1String("DM");
    CHECK(dm, "all on DM: the run touched no other line");
    for (const char *n : { "12", "14", "18", "906", "906D", "898", "898D" })
        CHECK(by.value(QLatin1String(n)).state == PlanRun::State::Read, QByteArray(n) + " read, the same bytes");
    CHECK(by.value(QStringLiteral("12")).firstMs == at("17:02:35") && by.value(QStringLiteral("898D")).firstMs == at("17:09:01"),
          "first reads 17:02:35 (12) to 17:09:01 (898D)");
    CHECK(by.value(QStringLiteral("16")).state == PlanRun::State::Different
              && by.value(QStringLiteral("16")).detail.contains(QStringLiteral("placement 2 \u2192 0")),
          by.value(QStringLiteral("16")).detail.toUtf8());
    for (const char *n : { "12D", "14D", "16D", "18D" })
        CHECK(by.value(QLatin1String(n)).state == PlanRun::State::NotRead && by.value(QLatin1String(n)).firstMs == 0,
              QByteArray(n) + " not read");
    CHECK(w.runNotInLayout().isEmpty(), "every tag read is in the layout");
    CHECK(!by.contains(QStringLiteral("981")), "981 (another stretch of DM, never passed) is not judged");

    QTableWidget *table = qobject_cast<QTableWidget *>(w.tabs()->currentWidget());
    CHECK(table && table->rowCount() == 12 && w.tabs()->tabText(w.tabs()->currentIndex()) == QStringLiteral("Run (1_1)"),
          "the Run tab shows them");
    QString key;
    qint64 ms = 0;
    QObject::connect(&w, &StationLayoutWindow::jumpRequested, &w, [&](const QString &k, qint64 m) { key = k; ms = m; });
    int row16 = -1;
    for (int i = 0; i < w.runTags().size(); ++i) if (w.runTags().at(i).name == QLatin1String("16")) row16 = i;
    emit table->cellDoubleClicked(row16, 0);
    CHECK(key == QStringLiteral("1_1") && ms == at("17:04:14") && w.selectedTags() == QStringList{ QStringLiteral("16") },
          "double-click: tag 16 selected, its read asked for in 1_1");

    // An edit re-judges; the log closing hides the run.
    CHECK(w.moveTag(QStringLiteral("14"), 163000), "14 moved");
    StationLayoutWindow::RunTag t14;
    for (const StationLayoutWindow::RunTag &t : w.runTags()) if (t.name == QLatin1String("14")) t14 = t;
    CHECK(t14.state == PlanRun::State::Different && t14.detail.contains(QStringLiteral("abs_loc")),
          QByteArray("the run follows the edit: 14 now read but different (") + t14.detail.toUtf8() + ")");
    {
        auto *gone = new LogModel(nullptr, 1000);
        w.showRun(gone, QStringLiteral("x"));
        delete gone;
        w.undo();
        CHECK(w.runTags().isEmpty(), "a log that is gone: the run is hidden, nothing read from it");
    }
    w.clearRun();
    CHECK(w.runTags().isEmpty() && w.tabs()->tabText(w.tabs()->indexOf(table)) == QStringLiteral("Run"), "Hide the run");
}
