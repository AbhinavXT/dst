#include "testutil.h"

#include "schema/schemadecoder.h"
#include "stationbuild.h"
#include "stationkavach.h"
#include "stationkavacheditor.h"
#include "stationlayout.h"
#include "stationlayoutwindow.h"
#include "stationsignals.h"
#include "stationsteps.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>

// =============================================================================
//  Session 461 — a station's signals and the MA each route would get.
//  CUR_SIG_INFO by its fields (type, direction, line, override, stop), as
//  station 527 sent it (S43 66400, A900 77664: section30_527.json, from its
//  SLRPs); the layout's signals against the KAVACH data's; the MA by aspect,
//  as Packet Maker answers it (1116 m at A900 Red, 1956 m at Yellow, 1650 m
//  from S48 with S43 Yellow: replay/2026-10-08, session 422).
// =============================================================================

TEST_SUITE(session461)
{
    // ---- CUR_SIG_INFO ----
    const StationSignals::SigInfo s43 = StationSignals::unpack(66400), a900 = StationSignals::unpack(77664);
    CHECK(s43.dir == 11 && s43.kind == 0x01 && s43.stop && !s43.running && s43.line == 0,
          "S43's 66400: DN MAIN, Adv-Str, a stop signal, standstill, no line");
    CHECK(a900.dir == 11 && a900.kind == 0x17 && a900.stop, "A900's 77664: DN MAIN, Semi-Auto Lit, a stop signal");
    CHECK(StationSignals::pack(s43) == 66400 && StationSignals::pack(a900) == 77664, "packed back: the same 17 bits");
    CHECK(StationSignals::describe(66400) == QStringLiteral("DN MAIN · Adv-Str · stop signal")
              && Schema::sigKindName(0x01) == QStringLiteral("Adv-Str") && Schema::sigDirName(11) == QStringLiteral("DN MAIN"),
          "in words, with the decoder's names");
    StationSignals::SigInfo all;
    all.line = 31; all.dir = 15; all.kind = 63; all.running = true; all.stop = true;
    CHECK(StationSignals::pack(all) == (1 << 17) - 1 && StationSignals::unpack((1 << 17) - 1).kind == 63, "every bit its field");
    bool home = false, dn = false;
    for (const auto &k : StationSignals::kinds()) home = home || (k.first == 0x18 && k.second == QStringLiteral("Home"));
    for (const auto &d : StationSignals::directions()) dn = dn || (d.first == 11 && d.second == QStringLiteral("DN MAIN"));
    CHECK(home && dn && StationSignals::kinds().size() == 36 && StationSignals::directions().size() == 12,
          "the pickers: the decoder's 36 named types and 12 directions");

    // ---- the MA by aspect, station 527 ----
    StationLayout::Layout lay;
    QString err;
    CHECK(StationLayout::load(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_kavach/section30_527.json"), &lay, &err), "section30_527.json");
    StationKavach::Station st;
    CHECK(StationKavach::fromJson(lay.kavach, &st, &err) && st.routes.size() == 3, "station 527's 3 routes");
    const QVector<StationSignals::MaRow> ma = StationSignals::maTable(st, 1);   // S43-A900 DN Auto
    auto at = [&ma](int aspect) { for (const auto &r : ma) if (r.aspect == aspect) return r; return StationSignals::MaRow(); };
    CHECK(ma.size() == 5 && at(1).maEnd == 1116 && at(1).got == 1 && at(15).maEnd == 1116,
          "S43-A900, A900 at Red or calling-on: 1 section, MA 1116 m (as 527 sent)");
    CHECK(at(2).maEnd == 1956 && at(2).got == 2 && at(2).routes == QStringList({ "S43-A900 DN Auto", "A900-A904 DN Auto" }),
          "at Yellow: 2 sections, MA 1956 m = 1116 + 840 (as 527 sent)");
    CHECK(at(11).wanted == 4 && at(11).got == 2 && at(11).maEnd == 1956 && !at(11).note.isEmpty(),
          "at Green: 4 wanted, the station has 2 routes on: MA 1956 m, said");
    const QVector<StationSignals::MaRow> from48 = StationSignals::maTable(st, 0);
    bool y1650 = false;
    for (const auto &r : from48) y1650 = y1650 || (r.aspect == 2 && r.maEnd == 1650);
    CHECK(y1650, "S48-S43 with S43 Yellow: MA 1650 m = 534 + 1116 (as 527 sent)");
    {   // The same numbers the engine answers with.
        StationKavach::State s = StationKavach::State::initial(st);
        for (const auto &g : st.signalList) s.aspect.insert(g.name, 11);
        s.aspect.insert(QStringLiteral("A900"), 2);
        CHECK(StationKavach::sectionsFrom(st, s, 1).maEnd == at(2).maEnd, "the engine's sections, the same MA");
    }

    // ---- one signal list ----
    CHECK(lay.signalList.isEmpty() && StationSignals::reconcile(lay, st).isEmpty(),
          "section30_527.json holds KAVACH data alone (no station signals drawn): nothing compared");
    for (const StationKavach::Signal &k : st.signalList) {   // the layout given its signals, where the KAVACH data puts them
        StationLayout::Signal g;
        g.name = k.name;
        g.footTag = QString::number(k.footTag);
        lay.signalList << g;
    }
    CHECK(StationSignals::reconcile(lay, st).isEmpty(), "the layout's signals where the KAVACH data has them: they agree");
    {
        StationKavach::Station k = st;
        k.signalList[1].footTag = 99;
        StationLayout::Layout l = lay;
        for (int i = l.signalList.size() - 1; i >= 0; --i) if (l.signalList[i].name == QStringLiteral("A904")) l.signalList.remove(i);
        const QStringList r = StationSignals::reconcile(l, k);
        bool foot = false, gone = false;
        for (const QString &x : r) {
            foot = foot || (x.contains(QStringLiteral("S43")) && x.contains(QStringLiteral("99")) && x.contains(QStringLiteral("18")));
            gone = gone || (x.contains(QStringLiteral("A904")) && x.contains(QStringLiteral("not in the layout")));
        }
        CHECK(foot && gone && r.size() == 2, "a foot tag that differs, a signal the layout lacks: said, each once");
        StationKavach::Station noRow = st;
        noRow.signalList.removeAt(noRow.signalIndex(QStringLiteral("A900")));
        bool named = false;
        for (const QString &x : StationSignals::reconcile(lay, noRow)) named = named || (x.contains(QStringLiteral("A900")) && x.contains(QStringLiteral("no row")));
        CHECK(named, "a route's signal with no KAVACH row: said");
    }

    // ---- the KAVACH tab ----
    {
        StationKavachEditor ed;
        ed.setSeedSource(&lay);
        ed.setKavach(lay.kavach);
        QObject::connect(&ed, &StationKavachEditor::edited, &ed, [&ed](const QJsonObject &k, const QString &) { ed.setKavach(k); });
        CHECK(ed.signalTable()->item(1, 2)->toolTip().startsWith(QStringLiteral("DN MAIN · Adv-Str · stop signal")), "S43's info in words, as its tooltip");
        QDialog *d = ed.signalInfoDialog(1);
        auto *kind = d ? d->findChild<QComboBox *>(QStringLiteral("skeSigKind")) : nullptr;
        auto *dir = d ? d->findChild<QComboBox *>(QStringLiteral("skeSigDir")) : nullptr;
        auto *stop = d ? d->findChild<QCheckBox *>(QStringLiteral("skeSigStop")) : nullptr;
        CHECK(kind && dir && stop && kind->currentData().toInt() == 0x01 && dir->currentData().toInt() == 11 && stop->isChecked(),
              "Signal info… on S43: Adv-Str, DN MAIN, stop");
        if (kind) kind->setCurrentIndex(kind->findData(0x18));
        if (auto *b = d ? d->findChild<QDialogButtonBox *>() : nullptr) b->button(QDialogButtonBox::Ok)->click();
        CHECK(ed.station().signalList.at(1).sigInfo == StationSignals::pack({ 0, 11, 0x18, false, true }), "Home now: one edit");
        delete d;
        CHECK(!ed.setSignalInfo(1, 1 << 17) && !ed.signalInfoDialog(-1), "past 17 bits, or no signal: refused");
        ed.selectRoute(1);
        QTableWidget *t = ed.maTable();
        bool yellow = false;
        for (int r = 0; t && r < t->rowCount(); ++r)
            yellow = yellow || (t->item(r, 0)->text().startsWith(QStringLiteral("Yellow")) && t->item(r, 2)->text() == QStringLiteral("1956"));
        CHECK(t && t->rowCount() == 5 && yellow, "route S43-A900 selected: the MA by aspect tab, 1956 m at Yellow");
        ed.listTabs()->setCurrentWidget(t);
        const int before = ed.station().routes.at(1).ssp.size();
        ed.addListRow();
        CHECK(ed.station().routes.at(1).ssp.size() == before, "Add row with the MA tab shown: nothing added");
    }

    // ---- the window: the Routes step says where the two lists disagree ----
    {
        StationLayoutWindow w;
        StationLayout::Layout l = lay;
        StationKavach::Station k = st;
        k.signalList[1].footTag = 99;
        l.kavach = StationKavach::toJson(k);
        w.setStation(l);
        bool listed = false;
        QListWidget *pl = w.stepsPanel()->problemList();
        w.showStep(StationBuild::StepRoutes);
        for (int i = 0; i < pl->count(); ++i) listed = listed || pl->item(i)->text().contains(QStringLiteral("puts its foot at tag 99"));
        CHECK(listed, "Routes step: S43's foot tag 99 against the layout's 18");
    }
}
