#include "testutil.h"

#include "packetbuilder.h"
#include "rfidtag.h"
#include "stationkavach.h"
#include "stationlayout.h"
#include "stationlayoutwindow.h"

#include <QListWidget>
#include <QTabWidget>

// =============================================================================
//  Session 455 — a firm's Table of Control read into Stationary KAVACH routes
//  (scripts/kavach_sheets.py). The station: HBL's NPL-TLPR IB, station 526,
//  its tag data and Table of Control as committed extracts
//  (schema/fixtures/kavach_sheets/NPL-TLPR_IB/station_526.json; the PDFs stay
//  local). Five route rows, 1-4 and 6:
//    S9D -> S9 ID       (R-134,746), (R-136,250)            996 m   Down
//    S9 ID -> S9/S2D    (R-138,792), (R-140,250)            1042 m  Down
//    S9/S2D -> -        (R-142,48), (R-144,737), (R-146,230) 1015 m  into TLPR (501)
//    S14 ID -> S14      (R-175,780), (R-177,240)            1020 m  Up
//    S14 -> -           (R-179,691), (R-181,250)            941 m   into NPL (525)
//  The last distance of each runs to the exit signal's foot tag, which the
//  cell does not name: S9 ID's R-138, the border tags R-148 and R-183 of the
//  "Last Stop Signal" table. The tag data puts R-177 at 155566 m, so the
//  linking distances 780 / 240 do not lie on the tags (770 / 250): DLConsole's
//  own KAVACH checks say so. With S9 ID Green the station answers the loco at
//  R-134 with an MA of three routes, 3053 m, and hands it over to TLPR (501).
// =============================================================================

namespace {

const char *kIb = DL_SRC_DIR "/schema/fixtures/kavach_sheets/NPL-TLPR_IB/station_526.json";

int routeNamed(const StationKavach::Station &s, const QString &name)
{
    for (int i = 0; i < s.routes.size(); ++i) if (s.routes[i].name == name) return i;
    return -1;
}

QVector<int> dists(const StationKavach::Route &r)
{
    QVector<int> out;
    for (const StationKavach::RouteTag &t : r.tags) out << t.dist;
    return out;
}

QVector<int> numbers(const StationKavach::Route &r)
{
    QVector<int> out;
    for (const StationKavach::RouteTag &t : r.tags) out << t.tag;
    return out;
}

qint64 maField(const StationKavach::Answer &a, const char *field)
{
    for (const Schema::SubEntry &x : a.subs) if (x.type == 0) return x.values.value(QLatin1String(field), -1);
    return -1;
}

QStringList checksTab(StationLayoutWindow &w)
{
    for (int i = 0; i < w.tabs()->count(); ++i)
        if (w.tabs()->tabText(i).startsWith(QStringLiteral("Checks"))) {
            QStringList out;
            auto *l = qobject_cast<QListWidget *>(w.tabs()->widget(i));
            for (int j = 0; l && j < l->count(); ++j) out << l->item(j)->text();
            return out;
        }
    return {};
}

} // namespace

TEST_SUITE(session455)
{
    StationLayout::Layout lay;
    QString err;
    CHECK(StationLayout::load(QString::fromLatin1(kIb), &lay, &err), "station_526.json opens");
    StationKavach::Station s;
    QStringList notes;
    CHECK(StationKavach::fromJson(lay.kavach, &s, &err, &notes) && notes.isEmpty(), "its KAVACH data reads, nothing skipped");
    CHECK(s.stationId == 526 && s.routes.size() == 5 && s.signalList.size() == 7, "station 526: 5 routes, 7 signals");

    // ---- the routes ----
    const int r1 = routeNamed(s, QStringLiteral("S9D-S9 ID Down Main"));
    const int r3 = routeNamed(s, QStringLiteral("NPL S9/S2D TLPR-R-148 (501) Down Main"));
    const int r4 = routeNamed(s, QStringLiteral("S14 ID-S14 UP Main"));
    const int r6 = routeNamed(s, QStringLiteral("S14-R-183 (525) UP Main"));
    CHECK(r1 >= 0 && r3 >= 0 && r4 >= 0 && r6 >= 0, "named entry-exit line; into the block: the next station's border tag and id");
    if (r1 < 0 || r3 < 0 || r4 < 0 || r6 < 0) return;
    CHECK(numbers(s.routes[r1]) == (QVector<int>{ 134, 136, 138 }) && dists(s.routes[r1]) == (QVector<int>{ 0, 746, 250 })
              && s.routes[r1].length() == 996,
          "S9D-S9 ID: R-134, 746 m R-136, 250 m R-138 (S9 ID's foot): 996 m");
    CHECK(s.routes[r1].pktDir == 1 && s.routes[r4].pktDir == 2, "Down rising: Nominal; Up: Reverse (from the tags' locations)");
    CHECK(s.routes[r1].sectionType == 1 && s.routes[r1].authority == 2 && s.routes[r1].tin == 86 && s.routes[r4].tin == 85,
          "absolute block, FS; the TIN of the entry foot tag the way it runs (86 Down, 85 Up)");
    CHECK(numbers(s.routes[r3]) == (QVector<int>{ 142, 144, 146, 148 }) && dists(s.routes[r3]) == (QVector<int>{ 0, 48, 737, 230 })
              && s.routes[r3].nextStn == 501 && s.routes[r3].osSpeed == 15,
          "S9/S2D into the block: the last 230 m to TLPR's border tag R-148; hands over to 501; OS 15 km/h");
    CHECK(s.routes[r6].nextStn == 525 && s.routes[r6].tags.last().tag == 183, "S14 into the block: to NPL's border tag R-183, station 525");
    CHECK(dists(s.routes[r4]) == (QVector<int>{ 0, 780, 240 }) && s.routes[r4].tags[1].dupDir == 1,
          "S14 ID-S14 as printed, 780 m and 240 m; each duplicate 4 m back (dup_dir Reverse)");
    CHECK(StationKavach::checks(s).isEmpty(), "every route starts and ends on its signals' foot tags");

    // ---- what DLConsole's own checks say ----
    QSet<int> ids;
    for (const StationLayout::Tag &t : lay.tags) ids.insert((t.name.endsWith(QLatin1Char('D')) ? t.name.chopped(1) : t.name).toInt());
    const QStringList tc = StationKavach::tagChecks(s, ids);
    CHECK(tc.join('\n').contains(QStringLiteral("148")) && tc.join('\n').contains(QStringLiteral("183")),
          "this IB's sheets alone hold no R-148 or R-183 (the next stations' tags): said");
    const QStringList lc = StationKavach::locationChecks(s, StationLayout::tagLocationsByNumber(lay, true));
    CHECK(lc.size() == 2 && lc.join('\n').contains(QStringLiteral("tag 177 is 780 m on from tag 175; the layout's tags lie 770 m on"))
              && lc.join('\n').contains(QStringLiteral("tag 179 is 240 m on from tag 177; the layout's tags lie 250 m on")),
          "R-177: 780 / 240 in the ToC, 770 / 250 by the tag data: the only two");
    CHECK(lay.lines.size() == 2 && lay.signalList.size() == 5, "the drawing: two lines from the routes, this IB's 5 signals");

    // ---- a junction tag at either location ----
    QString berr;
    const QByteArray junction = RfidTag::build({ { "type", 12 }, { "version", 1 }, { "unique", 454 }, { "abs_loc_1", 131270 },
                                                  { "abs_loc_2", 131163 }, { "tin", 90 } }, &berr);
    StationLayout::Layout j;
    j.tags << StationLayout::Tag{ QStringLiteral("454"), RfidTag::pageX(junction), RfidTag::pageY(junction) };
    CHECK(!junction.isEmpty() && StationLayout::tagLocationsByNumber(j).value(454) == (QVector<qint64>{ 131270 })
              && StationLayout::tagLocationsByNumber(j, true).value(454) == (QVector<qint64>{ 131270, 131163 }),
          QByteArray("GGD's junction tag R-454: abs_loc_1 alone, or both for the linking check ") + berr.toUtf8());

    // ---- the station answers ----
    PacketBuilder b;
    const Schema::Encoder &enc = b.encoder();
    StationKavach::State st = StationKavach::State::initial(s);
    st.aspect.insert(QStringLiteral("S9 ID"), 11);     // Green
    st.aspect.insert(QStringLiteral("NPL S9/S2D TLPR"), 11);
    StationKavach::Loco loco;
    const QHash<QString, qint64> lsrp{ { "FRAME_NUM", 100 }, { "SOURCE_LOCO_ID", 7 }, { "LAST_RFID_TAG", 134 },
                                       { "MOVEMENT_DIR", 1 }, { "ABS_LOCO_LOC", 152700 } };
    StationKavach::Answer a = StationKavach::answer(s, st, &loco, lsrp, enc);
    CHECK(a.ok && maField(a, "MA_W_R_T_SIG") == 996 + 1042 + 1015,
          QByteArray("the loco past S9D, S9 ID Green: three routes, 3053 m ") + a.why.toUtf8());
    CHECK(maField(a, "NEXT_STN_COMM") == 1 && maField(a, "APPR_STN_ILC_IBS_ID") == 501, "handed over to TLPR, 501");
    st.aspect.insert(QStringLiteral("S9 ID"), 1);
    a = StationKavach::answer(s, st, &loco, lsrp, enc);
    CHECK(a.ok && maField(a, "MA_W_R_T_SIG") == 996 && maField(a, "NEXT_STN_COMM") == 0, "S9 ID Red: to S9 ID, no hand-over");

    // ---- the window ----
    StationLayoutWindow w;
    CHECK(w.openFile(QString::fromLatin1(kIb)), "Station Layout opens it");
    int k = -1;
    for (int i = 0; i < w.tabs()->count(); ++i) if (w.tabs()->tabText(i).startsWith(QStringLiteral("KAVACH"))) k = i;
    CHECK(k >= 0 && w.tabs()->tabText(k) == QStringLiteral("KAVACH (5 routes)"), "the KAVACH tab: 5 routes");
    const QStringList shown = checksTab(w);
    int linking = 0;
    for (const QString &c : shown) linking += c.startsWith(QStringLiteral("KAVACH:")) && c.contains(QStringLiteral("the layout's tags lie"));
    CHECK(linking == 2, "Checks: the two linking distances R-177's location does not bear out");
}
