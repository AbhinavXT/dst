#include "testutil.h"

#include "stationbuild.h"
#include "stationkavach.h"
#include "stationkavacheditor.h"
#include "stationlayout.h"
#include "stationroutes.h"

#include <QDialog>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>

// =============================================================================
//  Session 460 — a station's routes proposed from its layout
//  (StationRoutes::propose): from each signal the way its foot tag faces, along
//  its line and over facing points, to the next signal facing the same way.
//  Real data: station_527.xml (LPI's layout, with its points) against HBL's
//  Table of Control for LPI (schema/fixtures/kavach_sheets/LPI_routes, 455).
//  Then the wizard's station (459), and the KAVACH tab's Propose….
// =============================================================================

namespace {

QSet<QString> bareSet(const QStringList &l)
{
    QSet<QString> out;
    for (QString n : l) {
        n = n.trimmed();
        if (n.startsWith(QLatin1Char('P'))) n.remove(0, 1);
        out.insert(n);
    }
    return out;
}

const StationRoutes::Proposal *findRoute(const StationRoutes::Result &r, const StationKavach::Route &want)
{
    for (const StationRoutes::Proposal &p : r.routes) {
        if (p.route.entry != want.entry || p.route.exit != want.exit || p.route.tags.size() != want.tags.size()) continue;
        bool same = true;
        for (int i = 0; same && i < want.tags.size(); ++i) same = p.route.tags.at(i).tag == want.tags.at(i).tag;
        if (same) return &p;
    }
    return nullptr;
}

}  // namespace

TEST_SUITE(session460)
{
    StationLayout::Layout lpi;
    QString err;
    CHECK(StationLayout::load(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_527.xml"), &lpi, &err), QByteArray("station_527.xml ") + err.toUtf8());
    QFile f(QStringLiteral(DL_SRC_DIR "/schema/fixtures/kavach_sheets/LPI_routes/station_527.json"));
    CHECK(f.open(QIODevice::ReadOnly), "HBL's LPI routes");
    StationKavach::Station hbl;
    CHECK(StationKavach::fromJson(QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("kavach")).toObject(), &hbl, &err)
              && hbl.routes.size() == 8,
          "HBL's ToC for LPI: 8 routes");

    // ---- LPI, proposed with no routes of its own ----
    const StationRoutes::Result r = StationRoutes::propose(lpi, StationKavach::Station());
    int found = 0, distsOk = 0, reverseOk = 0, normalSub = 0, dupOk = 0;
    QStringList missing;
    for (const StationKavach::Route &want : hbl.routes) {
        const StationRoutes::Proposal *p = findRoute(r, want);
        if (!p) { missing << want.name; continue; }
        ++found;
        bool d = true, dup = true;
        for (int i = 0; i < want.tags.size(); ++i) {
            d = d && p->route.tags.at(i).dist == want.tags.at(i).dist;
            dup = dup && p->route.tags.at(i).dupDir == want.tags.at(i).dupDir;
        }
        distsOk += d;
        dupOk += dup;
        reverseOk += bareSet(p->route.pointsReverse) == bareSet(want.pointsReverse);
        normalSub += bareSet(want.pointsNormal).contains(bareSet(p->route.pointsNormal));
    }
    CHECK(found == 7 && missing == QStringList{ QStringLiteral("S50A-S48 Down Main") },
          QByteArray("HBL's 7 routes whose signals the layout has are proposed, with their tags in order (S50A, a calling-on "
                     "signal, is not in the layout): missing ") + missing.join(QStringLiteral(", ")).toUtf8());
    CHECK(distsOk == 7, "every linking distance as HBL's ToC has it");
    // dup_dir 1: the duplicate 4 m back, from the layout's own tags. Where LPI's 2026 layout and HBL's 2023 tag
    // data disagree on which side a duplicate lies (28, 29, 37, 38, 19: back in the layout), the layout's is taken.
    CHECK(dupOk == 4, "every duplicate direction as HBL's on the 4 routes whose tags the two agree on");
    {
        bool layoutRule = true;
        const QHash<QString, qint64> at = StationLayout::tagLocations(lpi);
        for (const StationRoutes::Proposal &p : r.routes)
            for (const StationKavach::RouteTag &t : p.route.tags) {
                const QString n = QString::number(t.tag);
                const bool back = at.contains(n + QLatin1Char('D')) && at.value(n + QLatin1Char('D')) < at.value(n);
                layoutRule = layoutRule && t.dupDir == (back ? 1 : 0);
            }
        CHECK(layoutRule, "and every proposed tag's dup_dir is 1 exactly when the layout's duplicate lies back");
    }
    CHECK(reverseOk == 7, "every route's Reverse points as HBL's");
    CHECK(normalSub == 7, "its Normal points: HBL's, less the flank points beside the route");
    int under = 0;
    for (const StationKavach::Route &want : hbl.routes)
        if (const StationRoutes::Proposal *p = findRoute(r, want)) under += p->against <= StationRoutes::kAgainstMetres;
    CHECK(under == 7, "none of HBL's 7 runs 300 m against its lines' way: all ticked");
    {
        StationKavach::Route s1s6;
        s1s6.entry = QStringLiteral("S1");
        s1s6.exit = QStringLiteral("S6");
        const StationRoutes::Proposal *x = nullptr;
        for (const StationRoutes::Proposal &p : r.routes)
            if (p.route.entry == s1s6.entry && p.route.exit == s1s6.exit && !p.route.pointsReverse.isEmpty()) x = &p;
        CHECK(x && x->against > 1000 && x->againstLine == QStringLiteral("DN"),
              "S1 over P18 onto the DN main, Reverse: over 1 km against DN's way, offered unticked");
    }
    const StationRoutes::Proposal *main = findRoute(r, hbl.routes.at(0));
    CHECK(main && main->route.pktDir == 1 && main->route.tin == 110 && main->route.tins == QStringList({ "N-50", "N-52", "N-54" })
              && main->route.trackCircuits == QStringList{ QStringLiteral("DMT") } && main->route.distDupTag == 4
              && main->route.length() == hbl.routes.at(0).length(),
          "S50-S48 Down Main: Nominal, TIN 110, TINs N-50 N-52 N-54, track circuit DMT, duplicates 4 m, HBL's length");
    const StationRoutes::Proposal *loop = findRoute(r, hbl.routes.at(3));
    CHECK(loop && bareSet(loop->route.pointsReverse) == (QSet<QString>{ "28", "27" }) && loop->line == QStringLiteral("DN"),
          "S50-S47 over P28 and P27 onto UL1, named from its line");

    // ---- against the station's own routes ----
    {
        const StationRoutes::Result again = StationRoutes::propose(lpi, hbl);
        const StationRoutes::Proposal *p = findRoute(again, hbl.routes.at(0));
        CHECK(p && p->status == StationRoutes::Proposal::Same && p->existing == 0 && p->route.pointsReverse.isEmpty(),
              "with HBL's routes in the station: S50-S48 already there");
        bool named = true;
        for (const StationRoutes::Proposal &x : again.routes)
            for (const QString &n : x.route.pointsNormal + x.route.pointsReverse) named = named && !n.startsWith(QLatin1Char('P'));
        CHECK(named, "points named as the station's routes name them (30, not P30)");
        StationKavach::Station moved = hbl;
        moved.routes[0].tags[2].dist += 5;
        const StationRoutes::Result d = StationRoutes::propose(lpi, moved);
        const StationRoutes::Proposal *q = findRoute(d, hbl.routes.at(0));
        CHECK(q && q->status == StationRoutes::Proposal::Differs && q->existing == 0, "a distance 5 m off: differs from the station's");
    }

    // ---- what is not proposed ----
    {
        StationLayout::Layout l = lpi;
        StationLayout::Signal s;
        s.name = QStringLiteral("SQ");
        s.footTag = QStringLiteral("8");   // tag 8: In line section
        l.signalList << s;
        const StationRoutes::Result n = StationRoutes::propose(l, StationKavach::Station());
        bool said = false;
        for (const QString &x : n.notes) said = said || (x.contains(QStringLiteral("SQ")) && x.contains(QStringLiteral("placed 0")));
        CHECK(said, "a signal on a tag placed 0: which way it faces is not known, said");
    }

    // ---- the wizard's station ----
    {
        StationBuild::Plan plan;
        const StationLayout::Layout ns = StationBuild::newStation(plan);
        const StationRoutes::Result w = StationRoutes::propose(ns, StationKavach::Station());
        QStringList got;
        for (const StationRoutes::Proposal &p : w.routes) got << QStringLiteral("%1>%2:%3").arg(p.route.entry, p.route.exit, p.route.pointsReverse.join(QLatin1Char('+')));
        got.sort();
        CHECK(got == QStringList({ "S1>S2:", "S1>S3:P1", "S4>S5:", "S4>S6:P3" }),
              QByteArray("New station's routes: each home to its main starter, and over its point to the loop's starter: ")
                  + got.join(QStringLiteral(" ")).toUtf8());
        int noRoute = 0;
        for (const QString &x : w.notes) noRoute += x.contains(QStringLiteral("no route from it"));
        CHECK(noRoute == 4, "the four starters lead to no signal: said");
        bool lengths = true;
        for (const StationRoutes::Proposal &p : w.routes) lengths = lengths && p.route.length() > 0 && p.route.tags.first().dist == 0;
        CHECK(lengths, "every route starts at its entry foot tag and has a length");

        // The KAVACH tab, with no KAVACH data yet.
        StationKavachEditor ed;
        ed.setSeedSource(&ns);
        ed.setKavach(QJsonObject());
        QJsonObject got1;
        QObject::connect(&ed, &StationKavachEditor::edited, &ed, [&](const QJsonObject &k, const QString &) { got1 = k; ed.setKavach(k); });
        QDialog *d = ed.proposeDialog();
        auto *t = d ? d->findChild<QTableWidget *>(QStringLiteral("skeProposals")) : nullptr;
        bool allTicked = t && t->rowCount() == 4;
        for (int i = 0; allTicked && i < 4; ++i) allTicked = t->item(i, 0)->checkState() == Qt::Checked;
        CHECK(allTicked, "Propose…: 4 rows, all new and with their lines' way, ticked");
        if (auto *add = d ? d->findChild<QPushButton *>(QStringLiteral("skeProposeAdd")) : nullptr) add->click();
        CHECK(ed.station().routes.size() == 4 && ed.station().signalList.size() == 6 && ed.station().stationId == 1,
              "Add ticked: the KAVACH data started from the layout (station 1, its 6 signals), the 4 routes in it");
        delete d;
        StationKavach::Station back;
        CHECK(StationKavach::fromJson(got1, &back) && StationKavach::checks(back).isEmpty(), "and nothing for the KAVACH checks to say");
        QSet<int> ids;
        for (const StationLayout::Tag &tg : ns.tags) ids.insert((tg.name.endsWith(QLatin1Char('D')) ? tg.name.chopped(1) : tg.name).toInt());
        CHECK(StationKavach::tagChecks(back, ids).isEmpty() && StationKavach::locationChecks(back, StationLayout::tagLocationsByNumber(ns)).isEmpty(),
              "its tags are the layout's, at the distances the layout gives");
        QDialog *d2 = ed.proposeDialog();
        auto *t2 = d2 ? d2->findChild<QTableWidget *>(QStringLiteral("skeProposals")) : nullptr;
        CHECK(t2 && !(t2->item(0, 0)->flags() & Qt::ItemIsEnabled) && t2->item(0, 1)->text() == QStringLiteral("already there"),
              "Propose… again: each already there, not tickable");
        CHECK(!ed.addProposedRoutes({ 0, 1, 2, 3 }) && ed.station().routes.size() == 4, "adding them again: refused, nothing doubled");
        delete d2;
    }
}
