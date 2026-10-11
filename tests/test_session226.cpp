#include "testutil.h"

#include "settings.h"
#include "stationlayoutwindow.h"

#include <QApplication>
#include <QListWidget>
#include <QTabWidget>

// =============================================================================
//  Session 226 — Station Layout: compare with another station file.
//  Real files, both built in (session 211's library): station.xlsx (the
//  default, 211 tags) against station_8_MAY_2026.xlsx (204 tags). From the
//  8 May file to the default: tags 910, 400, 402, 404, 406, 408, 410 added
//  (164610 m, then every 500 m to 167500 m), line DM 39 -> 46 tags, and
//  signals S1647, S1649, S1651, S1653 added at 910, 402, 406, 410.
//  station_to_be_updated.xlsx is the same as the default.
// =============================================================================

TEST_SUITE(session226)
{
    StationLayout::Layout def, may;
    CHECK(StationLayout::load(StationLayout::defaultFile(), &def), "the default reads");
    QString mayPath, samePath;
    for (const QString &f : StationLayout::library()) {
        if (f.endsWith(QLatin1String("/station_8_MAY_2026.xlsx"))) mayPath = f;
        if (f.endsWith(QLatin1String("/station_to_be_updated.xlsx"))) samePath = f;
    }
    CHECK(!mayPath.isEmpty() && !samePath.isEmpty() && StationLayout::load(mayPath, &may), "the 8 May file reads");

    // ---- the comparison itself ----
    StationLayout::Layout same;
    StationLayout::load(samePath, &same);
    CHECK(StationLayout::diff(same, def).isEmpty(), "station_to_be_updated.xlsx: no difference");
    const StationLayout::Diff d = StationLayout::diff(may, def);
    QStringList added;
    QList<qint64> at;
    for (const StationLayout::TagChange &c : d.tags) {
        if (c.kind == StationLayout::TagChange::Added) { added << c.name; at << c.toLoc; }
    }
    CHECK(d.tags.size() == 7 && added == QStringList({ "910", "400", "402", "404", "406", "408", "410" }),
          added.join(QLatin1Char(',')).toUtf8());
    CHECK(at.value(0) == 164610 && at.value(1) == 165000 && at.value(6) == 167500, "at 164610 m, then 165000 to 167500 m");
    CHECK(d.other.contains(QStringLiteral("line DM: 39 tags → 46; added 910, 400, 402, 404, 406, 408, 410")),
          d.other.join(QStringLiteral(" | ")).toUtf8());
    CHECK(d.other.filter(QStringLiteral("signal added")).size() == 4
              && d.other.contains(QStringLiteral("signal added: S1647 at tag 910 (sig_id 4)")),
          "four signals added, S1647 at 910 among them");
    const StationLayout::Diff back = StationLayout::diff(def, may);
    CHECK(back.tags.size() == 7 && back.tags.first().kind == StationLayout::TagChange::Removed
              && back.other.filter(QStringLiteral("signal removed")).size() == 4,
          "the other way round: the same seven removed, four signals removed");

    // ---- a moved tag and a changed one ----
    StationLayout::Layout edited = def;
    for (StationLayout::Tag &t : edited.tags)
        if (t.name == QLatin1String("981")) StationLayout::moveTag(&t, 163990);
    const StationLayout::Diff m = StationLayout::diff(def, edited);
    CHECK(m.tags.size() == 1 && m.tags.first().kind == StationLayout::TagChange::Moved && m.tags.first().fromLoc == 163960
              && m.tags.first().toLoc == 163990,
          "981 moved 163960 → 163990 m");

    // ---- the window ----
    Settings::setStationLayoutLastFile(QString());
    StationLayoutWindow w;
    CHECK(w.reopenLast(), "the default opens");
    CHECK(w.compareWith(mayPath) && w.comparison().tags.size() == 7, "Compare with the 8 May file");
    auto *list = qobject_cast<QListWidget *>(w.tabs()->currentWidget());
    CHECK(list && list->count() == 1 + 7 + 5 && list->item(1)->text().startsWith(QStringLiteral("tag 910 added (at 164610 m)")),
          list ? list->item(1)->text().toUtf8() : QByteArray("no list"));
    CHECK(w.tabs()->tabText(w.tabs()->currentIndex()) == QStringLiteral("Compare (12)"), "the tab counts 12 rows");
    CHECK(w.moveTag(QStringLiteral("981"), 163990) && w.comparison().tags.size() == 8, "an edit joins the comparison (981 now differs too)");
    w.undo();
    CHECK(w.comparison().tags.size() == 7, "and leaves it on Undo");
    CHECK(!w.compareWith(QStringLiteral("/nonexistent/x.json")) && w.comparison().tags.size() == 7, "an unreadable file leaves it as it was");
    w.clearCompare();
    CHECK(w.comparison().isEmpty() && w.tabs()->tabText(w.tabs()->indexOf(list)) == QStringLiteral("Compare"), "Stop comparing");
}
