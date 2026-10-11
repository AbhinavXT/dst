#include "testutil.h"

#include "rfidtag.h"
#include "settings.h"
#include "stationlayoutwindow.h"
#include "tagbuilderwindow.h"

#include <QApplication>
#include <QPushButton>

// =============================================================================
//  Session 224 — Station Layout: a line as a Tag Builder route, and a route
//  as a new line. The built-in station.xlsx, and tags_sim's built-in route
//  3.40.tagroute.xml (KAV_CONFIG, session 201) for a real route file.
// =============================================================================

TEST_SUITE(session224)
{
    Settings::setStationLayoutLastFile(QString());
    StationLayoutWindow w;
    CHECK(w.reopenLast(), "the built-in layout opens");

    // A line with several tags, some with duplicates.
    QString lineId;
    QStringList lineTags;
    for (const StationLayout::Line &ln : w.station().lines)
        if (ln.tags.size() > lineTags.size()) { lineId = ln.line; lineTags = ln.tags; }
    CHECK(lineTags.size() > 3, QByteArray("line ") + lineId.toUtf8() + " has " + QByteArray::number(lineTags.size()) + " tags");

    // ---- line -> route ----
    const RfidTag::Route r = w.lineAsRoute(lineId);
    QStringList names;
    for (const RfidTag::Tag &t : r.tags) names << t.name;
    QStringList mains;
    for (const QString &n : names) if (!n.endsWith(QLatin1Char('D'))) mains << n;
    QStringList lineMains;
    for (const QString &n : lineTags) if (!n.endsWith(QLatin1Char('D'))) lineMains << n;
    CHECK(mains == lineMains, "the route's main tags are the line's, in its order");
    bool dupAfter = true;
    for (int i = 0; i < names.size(); ++i)
        if (names.at(i).endsWith(QLatin1Char('D')) && !lineTags.contains(names.at(i)))
            dupAfter = dupAfter && i > 0 && names.at(i - 1) + QLatin1Char('D') == names.at(i);
    CHECK(dupAfter && names.size() > mains.size(), "each duplicate right after its main tag");
    bool same = true;
    for (const RfidTag::Tag &t : r.tags) {
        for (const StationLayout::Tag &st : w.station().tags)
            if (st.name == t.name) same = same && RfidTag::pageX(t.bytes) == st.pageX.toLower() && RfidTag::pageY(t.bytes) == st.pageY.toLower();
    }
    CHECK(same, "the bytes are the layout's own");
    CHECK(r.dir == RfidTag::DirUnset, "the direction is left for the builder");
    CHECK(w.lineAsRoute(QStringLiteral("no-such-line")).tags.isEmpty(), "no such line: an empty route");

    TagBuilderWindow *tb = w.openLineInTagBuilder(lineId);
    CHECK(tb && tb->route().tags.size() == r.tags.size() && !tb->applyButton()->isVisibleTo(tb),
          "the builder opens on it as a route, no Apply button");

    // ---- route -> line: the round trip ----
    const int nTags = w.station().tags.size(), nLines = w.station().lines.size();
    CHECK(!w.addLineFromRoute(r, lineId, QString()), "a used line id is refused");
    CHECK(w.addLineFromRoute(r, QStringLiteral("UL9"), QStringLiteral("Copy")) && w.station().lines.size() == nLines + 1
              && w.station().tags.size() == nTags && w.station().lines.last().tags == lineMains,
          "the same route as line UL9: no new tag, its main tags in order");
    w.undo();

    // A route whose tag differs from the layout's is refused whole.
    RfidTag::Route bad = r;
    QHash<QString, qint64> v = RfidTag::values(bad.tags[0].bytes);
    v[v.contains(QStringLiteral("abs_loc")) ? QStringLiteral("abs_loc") : QStringLiteral("abs_loc_1")] += 7;
    bad.tags[0].bytes = RfidTag::build(v);
    CHECK(!w.addLineFromRoute(bad, QStringLiteral("UL9"), QString()) && w.station().lines.size() == nLines,
          "a route tag that differs from the layout's tag of the same name is refused");

    // ---- a real tags_sim route into an empty layout ----
    w.setStation(StationLayout::Layout());
    QString err;
    const QVector<RfidTag::Route> routes = RfidTag::readFile(QStringLiteral(":/tag_scenarios/3.40.tagroute.xml"), &err);
    CHECK(routes.size() == 1 && !routes.first().tags.isEmpty(), QByteArray("3.40.tagroute.xml reads (") + err.toUtf8() + ")");
    if (routes.isEmpty()) return;
    CHECK(w.addLineFromRoute(routes.first(), QStringLiteral("DM"), QStringLiteral("Main")), "it makes line DM");
    int routeMains = 0;
    QSet<QString> distinct;
    for (const RfidTag::Tag &t : routes.first().tags) {
        distinct.insert(RfidTag::nameOf(t.bytes));
        if (!RfidTag::nameOf(t.bytes).endsWith(QLatin1Char('D'))) ++routeMains;
    }
    CHECK(w.station().tags.size() == distinct.size() && w.station().lines.first().tags.size() <= routeMains
              && !w.station().lines.first().tags.isEmpty(),
          QByteArray("its ") + QByteArray::number(distinct.size()) + " tags added, its main tags in the line");
    CHECK(StationLayout::checks(w.station()).filter(QStringLiteral("not in the tags")).isEmpty(), "Checks: every line tag is a tag");
}
