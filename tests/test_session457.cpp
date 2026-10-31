#include "testutil.h"

#include "packetbuilder.h"
#include "stationkavach.h"
#include "stationkavacheditor.h"
#include "stationlayout.h"

#include <QJsonObject>
#include <QPushButton>

// =============================================================================
//  Session 457 — a TSR given by OHE poles. HBL's TSR route table for
//  Lingampalli (KAVACH_TSR_ROUTE_TABLE_LPI, 29.11.2023, its .docx) gives each
//  TSR route its start tag, the first OHE pole's distance from it and every
//  pole after as (span from the pole before - pole). scripts/kavach_sheets.py
//  adds them up into the station's "tsr_routes". The extract
//  (schema/fixtures/kavach_sheets/LPI_routes/, the table's rows 1 and 9):
//    24  S50 -> S48   from R-002: 159/24 at 42 m, 159/26 at 87, 159/28 at 132,
//                     159/30 at 186, ... 161/14 at 1726.74 m
//    12  S48 -> S43   from R-012: 161/12 at 33 m, ... 161/36 at 634.68 m
//  A TSR from pole to pole lands on each route it covers, cut to it, and the
//  station sends it (TSR_STATUS 2, cut at the MA end).
// =============================================================================

namespace {

const char *kLpi = DL_SRC_DIR "/schema/fixtures/kavach_sheets/LPI_routes/station_527.json";

int routeNamed(const StationKavach::Station &s, const QString &name)
{
    for (int i = 0; i < s.routes.size(); ++i) if (s.routes[i].name == name) return i;
    return -1;
}

int tsrRouteId(const StationKavach::Station &s, int id)
{
    for (int i = 0; i < s.tsrRoutes.size(); ++i) if (s.tsrRoutes[i].id == id) return i;
    return -1;
}

const Schema::SubEntry *subOf(const StationKavach::Answer &a, int type)
{
    for (const Schema::SubEntry &s : a.subs) if (s.type == type) return &s;
    return nullptr;
}

} // namespace

TEST_SUITE(session457)
{
    StationLayout::Layout lay;
    QString err;
    CHECK(StationLayout::load(QString::fromLatin1(kLpi), &lay, &err), "LPI's station_527.json opens");
    StationKavach::Station s;
    CHECK(StationKavach::fromJson(lay.kavach, &s, &err), "its KAVACH data reads");
    const int t24 = tsrRouteId(s, 24), t12 = tsrRouteId(s, 12);
    CHECK(s.tsrRoutes.size() == 2 && t24 >= 0 && t12 >= 0, "two TSR routes, 24 and 12");
    if (t24 < 0 || t12 < 0) return;
    const StationKavach::TsrRoute &a = s.tsrRoutes[t24];
    CHECK(a.entry == QStringLiteral("S50") && a.routes == QStringList{ QStringLiteral("S50-S48 Down Main") } && a.poles.size() == 36,
          "24: S50 over S50-S48, 36 poles");
    CHECK(a.poles[0].name == QStringLiteral("159/24") && a.poles[0].dist == 42.0 && a.poles[3].name == QStringLiteral("159/30")
              && a.poles[3].dist == 186.0 && qAbs(a.poles.last().dist - 1726.74) < 0.005,
          "159/24 at 42 m, 159/30 at 186 m (42 + 45 + 45 + 54), 161/14 at 1726.74 m");
    StationKavach::Station back;
    CHECK(StationKavach::fromJson(StationKavach::toJson(s), &back) && back.tsrRoutes.size() == 2
              && back.tsrRoutes[t24].poles.size() == 36 && back.tsrRoutes[t24].poles[3].dist == 186.0,
          "tsr_routes written and read back");

    // ---- a TSR by poles ----
    const int r1 = routeNamed(s, QStringLiteral("S50-S48 Down Main")), r2 = routeNamed(s, QStringLiteral("S48-S43 Down Main"));
    const int p1010 = a.poleIndex(QStringLiteral("160/10"));
    CHECK(r1 >= 0 && r2 >= 0 && p1010 > 0, "the routes, and pole 160/10");
    if (r1 < 0 || r2 < 0 || p1010 <= 0) return;
    const qint64 end = qRound64(a.poles[p1010].dist);
    QVector<StationKavach::TsrPlacement> put = StationKavach::tsrByPoles(s, a, QStringLiteral("159/30"), QStringLiteral("160/10"), 30, 7, 0, &err);
    CHECK(put.size() == 1 && put[0].route == r1 && put[0].row.value("dist") == 186 && put[0].row.value("len") == end - 186
              && put[0].row.value("u") == 6 && put[0].row.value("id") == 7,
          QByteArray("159/30 to 160/10 at 30 km/h: one TSR row on S50-S48 from 186 m (u 6) ") + err.toUtf8());
    const QVector<StationKavach::TsrPlacement> rev = StationKavach::tsrByPoles(s, a, QStringLiteral("160/10"), QStringLiteral("159/30"), 30, 7, 0);
    CHECK(rev.size() == 1 && rev[0].row == put[0].row, "given the other way round: the same");
    CHECK(StationKavach::tsrByPoles(s, a, QStringLiteral("158/1"), QStringLiteral("160/10"), 30, 7, 0, &err).isEmpty()
              && err.contains(QStringLiteral("no OHE pole 158/1")), "a pole the TSR route lacks: refused by name");
    CHECK(StationKavach::tsrByPoles(s, a, QStringLiteral("159/30"), QStringLiteral("160/10"), 33, 7, 0, &err).isEmpty()
              && err.contains(QStringLiteral("33 km/h")), "33 km/h: refused (5 km/h steps)");
    CHECK(StationKavach::tsrByPoles(s, a, QStringLiteral("159/30"), QStringLiteral("160/10"), 30, 300, 0, &err).isEmpty()
              && err.contains(QStringLiteral("8 bits")), "TSR id 300: refused");
    // Over two routes end to end (a TSR route like 18, S43 -> S1629, S1637, ...; made here from these two).
    StationKavach::TsrRoute two;
    two.id = 99;
    two.routes = QStringList{ QStringLiteral("S50-S48 Down Main"), QStringLiteral("S48-S43 Down Main") };
    two.poles = { { QStringLiteral("A"), 1500.4 }, { QStringLiteral("B"), 1800.0 } };
    put = StationKavach::tsrByPoles(s, two, QStringLiteral("A"), QStringLiteral("B"), 50, 9, 1);
    CHECK(put.size() == 2 && put[0].route == r1 && put[0].row.value("dist") == 1500 && put[0].row.value("len") == 170
              && put[1].route == r2 && put[1].row.value("dist") == 0 && put[1].row.value("len") == 130,
          "across the end of S50-S48 (1670 m): 170 m on it, 130 m on S48-S43 from its start");

    // ---- in the editor: one edit ----
    StationKavachEditor e;
    e.setKavach(lay.kavach);
    CHECK(e.polesButton() && e.polesButton()->isEnabled(), "Station Layout's KAVACH tab: TSR by OHE poles…, enabled");
    QJsonObject edited;
    QString label, said;
    QObject::connect(&e, &StationKavachEditor::edited, [&](const QJsonObject &k, const QString &l) { edited = k; label = l; });
    QObject::connect(&e, &StationKavachEditor::said, [&](const QString &w) { said = w; });
    CHECK(e.addTsrByPoles(t24, QStringLiteral("159/30"), QStringLiteral("160/10"), 30, 7, 0) && !edited.isEmpty()
              && label.contains(QStringLiteral("159/30 to 160/10")) && said.contains(QStringLiteral("S50-S48 Down Main")),
          "added: one Undo step, said where it went");
    StationKavach::Station withTsr;
    StationKavach::fromJson(edited, &withTsr);
    CHECK(withTsr.routes[r1].tsr.size() == 1 && withTsr.routes[r1].tsr[0].value("dist") == 186, "S50-S48 holds it");
    QString refusedWhy;
    QObject::connect(&e, &StationKavachEditor::refused, [&](const QString &w) { refusedWhy = w; });
    CHECK(!e.addTsrByPoles(t24, QStringLiteral("159/30"), QStringLiteral("160/10"), 33, 7, 0) && refusedWhy.contains(QStringLiteral("33 km/h")),
          "a speed off the 5 km/h steps: refused, said");

    // ---- the station sends it ----
    PacketBuilder pb;
    StationKavach::State st = StationKavach::State::initial(withTsr);
    st.aspect.insert(QStringLiteral("S48"), 1);
    StationKavach::Loco loco;
    const QHash<QString, qint64> lsrp{ { "FRAME_NUM", 100 }, { "SOURCE_LOCO_ID", 1 }, { "LAST_RFID_TAG", 2 },
                                       { "MOVEMENT_DIR", 1 }, { "ABS_LOCO_LOC", 159700 } };
    const StationKavach::Answer ans = StationKavach::answer(withTsr, st, &loco, lsrp, pb.encoder());
    const Schema::SubEntry *t = subOf(ans, 7);
    CHECK(ans.ok && t && t->values.value("TSR_STATUS") == 2 && t->repeats.value("tsr").size() == 1
              && t->repeats.value("tsr").first().value("dist") == 186 && t->repeats.value("tsr").first().value("u") == 6,
          QByteArray("the loco past S50: TSR_STATUS 2, the TSR 186 m on at 30 km/h ") + ans.why.toUtf8());
}
