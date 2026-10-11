#include "testutil.h"
#include "layoutaudit.h"

#include "logmodel.h"
#include "rfidtag.h"
#include "stationlayout.h"
#include "theme.h"
#include "trackdiagram.h"
#include "trackdiagramwindow.h"
#include "uistyle.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSlider>
#include <QTemporaryDir>

#include <cstdio>

// =============================================================================
//  Session 209 — a station layout over the Track diagram.
//
//  Real run: replay/loco_1_1_27062026_170217.cap (tags 12, 14, 16, 18, 906,
//  898 read). The layout is built here from those tags' own locations, plus
//  a tag the run never reached on the same line and a tag on another line,
//  so it runs in CI. With tests/fixtures/station_layout/station.xlsx (the
//  real station file, git-ignored) present, that is checked too.
// =============================================================================

namespace {

void loadCapture(LogModel *m, const QString &name)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/") + name);
    if (!f.open(QIODevice::ReadOnly)) return;
    QVector<LogEntryPtr> v;
    while (!f.atEnd()) {
        const QString l = QString::fromUtf8(f.readLine()).trimmed();
        if (!l.startsWith(QLatin1Char('@'))) continue;
        auto e = QSharedPointer<LogEntry>::create();
        e->text = l;
        e->epochMs = QDateTime::fromString(l.section(QLatin1Char(' '), 1, 1), Qt::ISODate).toMSecsSinceEpoch();
        e->cacheDerived();
        v << e;
    }
    m->appendEntries(v);
}

StationLayout::Tag makeTag(int unique, qint64 loc)
{
    QHash<QString, qint64> v{ { QStringLiteral("type"), 9 }, { QStringLiteral("unique"), unique },
                              { QStringLiteral("abs_loc"), loc } };
    const QByteArray b = RfidTag::build(v);
    return StationLayout::Tag{ QString::number(unique), RfidTag::pageX(b), RfidTag::pageY(b) };
}

int hitsStarting(const TrackDiagramCanvas &c, const QString &prefix)
{
    int n = 0;
    for (const TrackDiagramCanvas::Hit &h : c.hits()) n += h.text.startsWith(prefix) ? 1 : 0;
    return n;
}

}  // namespace

TEST_SUITE(session209)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    LogModel run(nullptr, 200000);
    loadCapture(&run, QStringLiteral("loco_1_1_27062026_170217.cap"));
    const TrackDiagram::Diagram d = TrackDiagram::build(&run, QStringLiteral("1_1"), QStringLiteral("L1"));
    CHECK(d.tags.size() >= 4, "fixture: a run that read several tags");

    // The layout: every tag the run read, on line DM, plus one it did not
    // read inside the run's span (between the first two), one on UM.
    StationLayout::Layout l;
    StationLayout::Line dm{ QStringLiteral("DN"), QStringLiteral("DM"), {} };
    QVector<TrackDiagram::RfidMark> byLoc = d.tags;
    std::sort(byLoc.begin(), byLoc.end(), [](const TrackDiagram::RfidMark &a, const TrackDiagram::RfidMark &b) { return a.locM < b.locM; });
    for (const TrackDiagram::RfidMark &t : byLoc) {
        l.tags << makeTag(int(t.uniqueId), qint64(t.locM));
        dm.tags << QString::number(t.uniqueId);
    }
    const qint64 missedLoc = qint64((byLoc.at(0).locM + byLoc.at(1).locM) / 2);
    l.tags << makeTag(777, missedLoc);
    dm.tags << QStringLiteral("777");
    l.tags << makeTag(778, missedLoc + 5);
    l.lines << dm << StationLayout::Line{ QStringLiteral("UP"), QStringLiteral("UM"), { QStringLiteral("778") } };
    l.signalList << StationLayout::Signal{ QString::number(byLoc.at(1).uniqueId), QStringLiteral("S99"), 4, 527 };
    CHECK(StationLayout::checks(l).isEmpty(), QByteArray("the built layout is clean: ") + StationLayout::checks(l).join(QStringLiteral("; ")).toUtf8());

    const TrackDiagram::LayoutOverlay o = TrackDiagram::overlay(d, l, QStringLiteral("built.json"));
    CHECK(o.lines == QStringList{ QStringLiteral("DM") }, "only the line this run read tags on");
    CHECK(o.tags.size() == byLoc.size() + 1 && o.unreadTags() == 1, "the run's tags read, tag 777 not; UM's 778 left out");
    CHECK(o.signalMarks.size() == 1 && o.signalMarks.first().name == QLatin1String("S99")
          && qRound(o.signalMarks.first().locM) == qRound(byLoc.at(1).locM), "S99 at its foot tag's location");
    CHECK(TrackDiagram::lineAt(d, o, byLoc.first().epochMs) == QLatin1String("DM"), "the line at the first read");
    CHECK(TrackDiagram::lineAt(d, o, d.tags.first().epochMs - 1).isEmpty(), "no line before any tag was read");

    // ---- the window ---------------------------------------------------------------------
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("built.json"));
    CHECK(StationLayout::save(path, l), "write the layout");
    TrackDiagramWindow w(&run, QStringLiteral("1_1"), QStringLiteral("L1"));
    w.resize(1100, 700);
    w.show();
    CHECK(w.loadLayout(path), "load it over the run");
    w.canvas()->grab();
    CHECK(hitsStarting(*w.canvas(), QStringLiteral("Layout tag 777")) == 1, "the unread tag is drawn, with its tooltip");
    CHECK(hitsStarting(*w.canvas(), QStringLiteral("Layout tag 778")) == 0, "the other line's tag is not");
    CHECK(hitsStarting(*w.canvas(), QStringLiteral("Layout signal S99")) == 1, "the layout's signal is drawn");
    bool lineTip = false;
    for (const TrackDiagramCanvas::Hit &h : w.canvas()->hits())
        lineTip = lineTip || (h.text.startsWith(QLatin1String("RFID tag")) && h.text.endsWith(QLatin1String(", line DM")));
    CHECK(lineTip, "a read tag's tooltip names its line");
    const QVector<QRect> &labels = w.canvas()->labelRects();
    bool overlap = false;
    for (int i = 0; i < labels.size(); ++i)
        for (int j = i + 1; j < labels.size(); ++j)
            overlap = overlap || labels.at(i).adjusted(1, 1, -1, -1).intersects(labels.at(j).adjusted(1, 1, -1, -1));
    CHECK(!overlap, "layout labels and run labels do not overlap");
    CHECK(LayoutAudit::orphans(&w).isEmpty(), "no widget outside a layout");
    CHECK(w.minimumSizeHint().width() <= 1100, QByteArray("fits 1100 wide (") + QByteArray::number(w.minimumSizeHint().width()) + ")");
    w.clearLayout();
    w.canvas()->grab();
    CHECK(hitsStarting(*w.canvas(), QStringLiteral("Layout ")) == 0, "Hide layout takes it off");
    CHECK(!w.loadLayout(dir.filePath(QStringLiteral("missing.json"))), "a missing file is refused");

    // ---- the real station file, when present ----------------------------------------------
    const QString real = QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_layout/station.xlsx");
    if (!QFile::exists(real)) {
        std::printf("  NOTE session209: tests/fixtures/station_layout/station.xlsx not here; real layout not checked\n");
        return;
    }
    StationLayout::Layout rl;
    CHECK(StationLayout::load(real, &rl), "real: station.xlsx reads");
    const TrackDiagram::LayoutOverlay ro = TrackDiagram::overlay(d, rl, QStringLiteral("station.xlsx"));
    CHECK(ro.lines.contains(QStringLiteral("DM")), QByteArray("real: the run's tags are on the station's DM line (")
          + ro.lines.join(QStringLiteral(",")).toUtf8() + ")");
    // Every tag the run read is where the station file puts it.
    int off = 0;
    const QHash<QString, qint64> locs = StationLayout::tagLocations(rl);
    for (const TrackDiagram::RfidMark &t : d.tags)
        if (locs.contains(QString::number(t.uniqueId)) && std::abs(locs.value(QString::number(t.uniqueId)) - qint64(t.locM)) > 0) ++off;
    CHECK(off == 0, QByteArray("real: the run's tags sit where station.xlsx puts them (") + QByteArray::number(off) + " not)");
    std::printf("  NOTE session209: station.xlsx over the run: lines %s, %d of %d tags read\n",
                qPrintable(ro.lines.join(QStringLiteral(","))), int(ro.tags.size() - ro.unreadTags()), int(ro.tags.size()));
}
