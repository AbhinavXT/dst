#include "testutil.h"

#include "logmodel.h"
#include "settings.h"
#include "stationlayout.h"
#include "stationlayoutwindow.h"
#include "theme.h"
#include "trackdiagram.h"
#include "trackdiagramwindow.h"
#include "uistyle.h"

#include <QDateTime>
#include <QFile>
#include <QMenu>
#include <QPushButton>

// =============================================================================
//  Session 210 — the default station layout, built in: the station file the
//  old Python tool opens at start (config/station/station.xlsx), unchanged,
//  as :/station_layouts/station.xlsx.
// =============================================================================

TEST_SUITE(session210)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    StationLayout::Layout l;
    QString err;
    QStringList notes;
    CHECK(StationLayout::load(StationLayout::defaultFile(), &l, &err, &notes),
          QByteArray("the built-in layout reads (") + err.toUtf8() + ")");
    CHECK(l.tags.size() == 211 && l.signalList.size() == 36 && l.points.size() == 18 && l.lines.size() == 6
          && l.stations.size() == 1 && l.texts.size() == 2, "all of station.xlsx: 211 tags, 36 signals, 18 points, 6 lines");
    CHECK(l.otherSheets.size() == 1 && l.otherSheets.first().name == QLatin1String("relaymap"), "relaymap carried");
    CHECK(l.stations.first().id == 527 && l.stations.first().location == 161060, "station 527 at 161060 m");
    CHECK(StationLayout::tagLocations(l).value(QStringLiteral("981")) == 163960, "tag 981 at 163960 m");

    // The window opens it when there is no last file.
    Settings::setStationLayoutLastFile(QString());
    {
        StationLayoutWindow w;
        CHECK(w.reopenLast() && w.station().tags.size() == 211, "no last file: the default opens");
        CHECK(!w.isModified(), "opening the default is not a change");
        w.show();
        CHECK(w.minimumSizeHint().width() <= 1000, QByteArray("the button bar leaves room for wider fonts (")
              + QByteArray::number(w.minimumSizeHint().width()) + ")");
    }
    // A last file that is gone falls back to the default too.
    Settings::setStationLayoutLastFile(QStringLiteral("/nonexistent/station.json"));
    {
        StationLayoutWindow w;
        CHECK(w.reopenLast() && w.station().tags.size() == 211, "a missing last file: the default opens");
    }
    Settings::setStationLayoutLastFile(QString());

    // Over a real run on that station.
    LogModel run(nullptr, 200000);
    {
        QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_170217.cap"));
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
        run.appendEntries(v);
    }
    TrackDiagramWindow tw(&run, QStringLiteral("1_1"), QStringLiteral("L1"));
    CHECK(tw.loadLayout(StationLayout::defaultFile()), "the Track diagram loads the default layout");
    const TrackDiagram::LayoutOverlay &o = tw.canvas()->overlay();
    CHECK(o.lines == QStringList{ QStringLiteral("DM") } && o.unreadTags() == 0 && o.tags.size() == 6,
          "the run is on DM; its six tags are all the default layout has there");
    QStringList sigs;
    for (const auto &s : o.signalMarks) sigs << s.name;
    CHECK(sigs.contains(QStringLiteral("S48")) && sigs.contains(QStringLiteral("S43")), "S48 and S43 at their foot tags");
    bool menu = false;
    for (QPushButton *b : tw.findChildren<QPushButton *>())
        if (b->text() == QStringLiteral("Station layout\u2026") && b->menu() && b->menu()->actions().size() == 2) menu = true;
    CHECK(menu, "Station layout... offers the default and a file");
}
