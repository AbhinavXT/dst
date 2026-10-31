#include "testutil.h"

#include "rfidtag.h"
#include "stationbuild.h"
#include "stationlayout.h"
#include "stationlayoutwindow.h"
#include "stationsteps.h"
#include "stationkavacheditor.h"

#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>

#include <algorithm>

// =============================================================================
//  Session 459 — building a station step by step in Station Layout.
//  StationBuild: the steps (Station, Lines, Tags, Points, Signals, Track
//  circuits, Labels, Routes, Check), each with its rows and problems, each
//  problem with its row (StationLayout::problems(), which checks() is the
//  text of); and New station…: DN / UP main, loops, points, tag pairs and
//  signals, with the TINs and placements the shipped stations have. The
//  window: the steps beside the drawing, a step showing its table, a
//  problem its row; the tag form setting TINs, placement and station; the
//  Track circuits tab. Real data: station_527.xml (LPI).
// =============================================================================

namespace {

const char *kLpi = DL_SRC_DIR "/tests/fixtures/station_527.xml";

QHash<QString, qint64> fieldsOf(const StationLayout::Tag &t)
{
    return RfidTag::values(RfidTag::fromPages(t.pageX, t.pageY));
}

const StationLayout::Tag *tagNamed(const StationLayout::Layout &l, const QString &name)
{
    for (const StationLayout::Tag &t : l.tags) if (t.name == name) return &t;
    return nullptr;
}

const StationLayout::Signal *signalNamed(const StationLayout::Layout &l, const QString &name)
{
    for (const StationLayout::Signal &s : l.signalList) if (s.name == name) return &s;
    return nullptr;
}

// On `line`, main tags by location: at each tag whose two TINs differ, tin_nom is a TIN of the tag
// after it (rising) and tin_rev one of the tag before it. Returns (agreeing, not).
QPair<int, int> tinDirection(const StationLayout::Layout &l, const QString &line)
{
    QVector<QPair<qint64, QHash<QString, qint64>>> seq;
    for (const StationLayout::Line &ln : l.lines) {
        if (ln.line != line) continue;
        for (const QString &n : ln.tags) {
            const StationLayout::Tag *t = tagNamed(l, n);
            if (!t || n.endsWith(QLatin1Char('D'))) continue;
            const QHash<QString, qint64> v = fieldsOf(*t);
            seq.append({ v.value(QStringLiteral("abs_loc")), v });
        }
    }
    std::sort(seq.begin(), seq.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    int ok = 0, bad = 0;
    for (int k = 1; k + 1 < seq.size(); ++k) {
        const qint64 n = seq[k].second.value(QStringLiteral("tin_nom")), r = seq[k].second.value(QStringLiteral("tin_rev"));
        if (n == r) continue;
        const auto &next = seq[k + 1].second, &prev = seq[k - 1].second;
        const bool good = (n == next.value(QStringLiteral("tin_nom")) || n == next.value(QStringLiteral("tin_rev")))
                       && (r == prev.value(QStringLiteral("tin_nom")) || r == prev.value(QStringLiteral("tin_rev")));
        (good ? ok : bad)++;
    }
    return { ok, bad };
}

}  // namespace

TEST_SUITE(session459)
{
    // ---- the convention, on a real station ----
    StationLayout::Layout lpi;
    QString err;
    CHECK(StationLayout::load(QString::fromLatin1(kLpi), &lpi, &err), QByteArray("station_527.xml ") + err.toUtf8());
    const QPair<int, int> real = tinDirection(lpi, QStringLiteral("DM"));
    CHECK(real.first == 9 && real.second == 0,
          QByteArray("LPI's DN main: at each of its 9 TIN changes tin_nom is the track on the rising side, tin_rev the falling side; ")
              + QByteArray::number(real.first) + " agree, " + QByteArray::number(real.second) + " not");

    // ---- problems: checks() with their rows ----
    {
        const QVector<StationLayout::Problem> ps = StationLayout::problems(lpi);
        QStringList texts;
        for (const auto &p : ps) texts << p.text;
        CHECK(texts == StationLayout::checks(lpi), "checks() is the problems' text, in order");
        StationLayout::Layout broken = lpi;
        StationLayout::Signal s;
        s.name = QStringLiteral("SX");
        s.footTag = QStringLiteral("9999");
        broken.signalList << s;
        broken.lines[1].tags << QStringLiteral("8888");
        broken.tags[3].pageY = QStringLiteral("0000000000000000");
        bool sig = false, line = false, tag = false;
        for (const auto &p : StationLayout::problems(broken)) {
            sig = sig || (p.sheet == StationLayout::SheetSignals && p.row == broken.signalList.size() - 1 && p.text.contains(QStringLiteral("9999")));
            line = line || (p.sheet == StationLayout::SheetLines && p.row == 1 && p.text.contains(QStringLiteral("8888")));
            tag = tag || (p.sheet == StationLayout::SheetTags && p.row == 3);
        }
        CHECK(sig && line && tag, "a signal's missing foot tag, a line's missing tag, a broken tag: each with its sheet and row");
    }

    // ---- the steps, on LPI ----
    {
        const QVector<StationBuild::StepState> st = StationBuild::steps(lpi, 3, { QStringLiteral("route X: no exit") });
        CHECK(st.size() == StationBuild::StepCount && st[StationBuild::StepTags].count == lpi.tags.size()
                  && st[StationBuild::StepSignals].count == lpi.signalList.size() && st[StationBuild::StepCircuits].count == 31
                  && st[StationBuild::StepStation].count == lpi.stations.size() && st[StationBuild::StepRoutes].count == 3,
              "each step counts its rows: LPI's tags, signals, 31 track circuits, the 3 routes given");
        CHECK(st[StationBuild::StepRoutes].problems.size() == 1 && st[StationBuild::StepRoutes].problems[0].sheet == StationLayout::SheetNone,
              "the KAVACH tab's notes are the Routes step's problems");
        int all = 0;
        for (int s = 0; s < StationBuild::StepCheck; ++s) all += st[s].problems.size();
        CHECK(st[StationBuild::StepCheck].problems.size() == all, "Check holds every step's problems");
        const QVector<StationBuild::StepState> empty = StationBuild::steps(StationLayout::Layout());
        CHECK(empty[StationBuild::StepStation].problems.size() == 1 && empty[StationBuild::StepLines].problems.size() == 1,
              "an empty layout: no station row, no lines, said");
    }

    // ---- New station ----
    StationBuild::Plan plan;
    plan.stationId = 900;
    plan.centre = 100000;
    const StationLayout::Layout ns = StationBuild::newStation(plan, &err);
    CHECK(!ns.isEmpty(), QByteArray("the default plan makes a station ") + err.toUtf8());
    CHECK(ns.stations.size() == 1 && ns.stations[0].id == 900 && ns.stations[0].location == 100000, "station 900 at 100000 m");
    QStringList lineIds;
    for (const auto &ln : ns.lines) lineIds << ln.line;
    CHECK(lineIds == QStringList({ QStringLiteral("DM"), QStringLiteral("DL1"), QStringLiteral("UM"), QStringLiteral("UL1") }),
          "DN main and its loop, UP main and its loop");
    CHECK(ns.tags.size() == 2 * StationBuild::tagsNeeded(plan) && ns.tags.size() == 32 && ns.points.size() == 4 && ns.signalList.size() == 6,
          "16 tag pairs, 4 points, 6 signals");
    CHECK(StationLayout::checks(ns).isEmpty(), "nothing for Checks to report");
    const QVector<StationBuild::StepState> nst = StationBuild::steps(ns);
    CHECK(nst[StationBuild::StepCheck].problems.isEmpty(), "nor for the steps");
    bool allGood = true;
    for (const auto &t : ns.tags) {
        const QHash<QString, qint64> v = fieldsOf(t);
        allGood = allGood && StationLayout::info(t).crcOk && v.value(QStringLiteral("version")) == 1
               && v.value(QStringLiteral("stn_nom")) == 900 && v.value(QStringLiteral("stn_rev")) == 900
               && v.value(QStringLiteral("duplication")) == (t.name.endsWith(QLatin1Char('D')) ? 1 : 0);
    }
    CHECK(allGood, "every tag: CRC-30 good, version 1, station 900 both ways, the D tag a duplicate");
    const QPair<int, int> dm = tinDirection(ns, QStringLiteral("DM")), um = tinDirection(ns, QStringLiteral("UM"));
    CHECK(dm.first == 2 && dm.second == 0 && um.first == 2 && um.second == 0,
          "its TIN changes follow LPI's: tin_nom the rising side, tin_rev the falling side, on both mains");
    const StationLayout::Signal *s1 = signalNamed(ns, QStringLiteral("S1"));
    const StationLayout::Tag *h = s1 ? tagNamed(ns, s1->footTag) : nullptr;
    const StationLayout::Tag *hd = s1 ? tagNamed(ns, s1->footTag + QLatin1Char('D')) : nullptr;
    CHECK(h && hd && fieldsOf(*h).value(QStringLiteral("abs_loc")) == 99350 && fieldsOf(*hd).value(QStringLiteral("abs_loc")) == 99354
              && fieldsOf(*h).value(QStringLiteral("placement")) == 1 && fieldsOf(*h).value(QStringLiteral("tin_nom")) == 2
              && fieldsOf(*h).value(QStringLiteral("tin_rev")) == 1 && s1->callingOn,
          "DN home S1: 200 m before the yard, placed 1 (Signal foot, Nominal), into the yard's TIN 2 rising, its D 4 m on");
    const StationLayout::Signal *s4 = signalNamed(ns, QStringLiteral("S4"));   // S3: the DN loop's starter
    const StationLayout::Tag *uh = s4 ? tagNamed(ns, s4->footTag) : nullptr;
    const StationLayout::Tag *uhd = s4 ? tagNamed(ns, s4->footTag + QLatin1Char('D')) : nullptr;
    CHECK(uh && uhd && fieldsOf(*uh).value(QStringLiteral("abs_loc")) == 100650 && fieldsOf(*uhd).value(QStringLiteral("abs_loc")) == 100646
              && fieldsOf(*uh).value(QStringLiteral("placement")) == 2 && fieldsOf(*uh).value(QStringLiteral("tin_rev")) == 6
              && fieldsOf(*uh).value(QStringLiteral("tin_nom")) == 5,
          "UP home S4: placed 2 (Reverse), into UP's yard TIN 6 falling, its D 4 m on the way UP is run");
    int footOk = 0;
    for (const auto &s : ns.signalList) {
        const qint64 p = fieldsOf(*tagNamed(ns, s.footTag)).value(QStringLiteral("placement"));
        QString line;
        for (const auto &ln : ns.lines) if (ln.tags.contains(s.footTag)) line = ln.line;
        footOk += (line.startsWith(QLatin1Char('D')) && p == 1) || (line.startsWith(QLatin1Char('U')) && p == 2);
    }
    CHECK(footOk == 6, "every signal's foot tag faces its line's way: 1 on DM and DL1, 2 on UM and UL1 (LPI: DM 1, UM 2)");
    // Refused, by name.
    StationBuild::Plan bad = plan;
    bad.firstTag = 1010;
    CHECK(StationBuild::newStation(bad, &err).isEmpty() && err.contains(QStringLiteral("1023")), "unique ids past 1023: refused");
    bad = plan;
    bad.firstTin = 250;
    CHECK(StationBuild::newStation(bad, &err).isEmpty() && err.contains(QStringLiteral("255")), "TINs past 255: refused");
    bad = plan;
    bad.centre = 500;
    CHECK(StationBuild::newStation(bad, &err).isEmpty() && err.contains(QStringLiteral("0 to")), "a tag before 0 m: refused");
    bad = plan;
    bad.downLoops = 3;
    bad.upLoops = 0;
    const StationLayout::Layout three = StationBuild::newStation(bad, &err);
    CHECK(three.lines.size() == 5 && three.points.size() == 6 && StationLayout::checks(three).isEmpty(), "3 DN loops, no UP loop");

    // ---- a signal whose foot tag is not placed at a signal foot ----
    {
        StationLayout::Layout l = ns;
        for (StationLayout::Tag &t : l.tags)
            if (t.name == s1->footTag) {
                QHash<QString, qint64> v = fieldsOf(t);
                v.insert(QStringLiteral("placement"), 0);
                const QByteArray b = RfidTag::build(v);
                t.pageX = RfidTag::pageX(b);
                t.pageY = RfidTag::pageY(b);
            }
        l.lines[0].tags.removeAll(QStringLiteral("1"));
        const QVector<StationBuild::StepState> st = StationBuild::steps(l);
        CHECK(st[StationBuild::StepSignals].problems.size() == 1 && st[StationBuild::StepSignals].problems[0].row == 0
                  && st[StationBuild::StepSignals].problems[0].text.contains(QStringLiteral("In line section")),
              "S1's foot tag placed 0: said in Signals, on S1's row");
        CHECK(st[StationBuild::StepTags].problems.size() == 1 && st[StationBuild::StepTags].problems[0].text.contains(QStringLiteral("no line")),
              "tag 1 taken off DN main: on no line or point, said in Tags");
    }

    // ---- the window ----
    StationLayoutWindow w;
    w.resize(1400, 860);
    w.show();
    StationStepsPanel *panel = w.stepsPanel();
    auto *stepsAct = w.findChild<QAction *>(QStringLiteral("stationLayoutSteps"));
    CHECK(panel && panel->isVisible() && stepsAct && stepsAct->isChecked() && panel->stepList()->count() == StationBuild::StepCount,
          "the steps beside the drawing, shown at first, all 9");
    CHECK(w.openFile(QString::fromLatin1(kLpi)), "open LPI");
    CHECK(panel->stepList()->item(StationBuild::StepCircuits)->text().contains(QStringLiteral("31")),
          QByteArray("Track circuits: 31 (") + panel->stepList()->item(StationBuild::StepCircuits)->text().toUtf8() + ")");
    emit panel->stepChosen(StationBuild::StepTags);
    CHECK(w.tabs()->currentWidget() == w.table(0), "step Tags: the Tags table");
    emit panel->stepChosen(StationBuild::StepRoutes);
    CHECK(w.tabs()->currentWidget() == w.kavachEditor(), "step Routes: the KAVACH tab");
    emit panel->stepChosen(StationBuild::StepCircuits);
    CHECK(w.tabs()->currentWidget() == w.circuitsTable() && w.circuitsTable()->rowCount() == 31
              && w.tabs()->tabText(w.tabs()->indexOf(w.circuitsTable())) == QStringLiteral("Track circuits (31)"),
          "step Track circuits: its table, LPI's 31");
    w.tabs()->setCurrentWidget(w.table(3));
    CHECK(panel->currentStep() == StationBuild::StepLines, "the Lines tab shown: the panel on Lines");
    CHECK(panel->helpLabel()->text().contains(QStringLiteral("DM")), "and says what goes in it");

    // A problem clicked: its row.
    {
        StationLayout::Layout l = lpi;
        StationLayout::Signal s;
        s.name = QStringLiteral("SX");
        s.footTag = QStringLiteral("9999");
        l.signalList << s;
        w.setStation(l);
        emit panel->stepChosen(StationBuild::StepSignals);
        QListWidget *pl = panel->problemList();
        QListWidgetItem *hit = nullptr;
        for (int i = 0; i < pl->count(); ++i) if (pl->item(i)->text().contains(QStringLiteral("9999"))) hit = pl->item(i);
        CHECK(hit && panel->stepList()->item(StationBuild::StepSignals)->text().contains(QStringLiteral("⚠")),
              "Signals: ⚠, and SX's foot tag listed");
        if (hit) emit pl->itemClicked(hit);
        CHECK(w.tabs()->currentWidget() == w.table(1) && w.table(1)->currentRow() == l.signalList.size() - 1,
              "clicked: the Signals table, SX's row");
    }

    // New station, from the window.
    CHECK(w.findChild<QAction *>(QStringLiteral("slNewStation")) && panel->newStationButton(), "Open ▸ New station…, and the panel's button");
    CHECK(w.newStation(plan) && w.station().tags.size() == 32 && w.isModified() && w.tabs()->currentWidget() == w.table(0),
          "New station: made, not saved yet, on the Tags step");
    bad = plan;
    bad.firstTag = 1020;
    CHECK(!w.newStation(bad) && w.station().tags.size() == 32,
          "a plan refused: the station left as it was");

    // The tag form: TINs, placement, station.
    w.setFormShown(true);
    w.selectSheetRow(0, 0);
    QWidget *f = w.formPanel();
    auto *tinNom = f->findChild<QSpinBox *>(QStringLiteral("formTag_tin_nom"));
    auto *stnRev = f->findChild<QSpinBox *>(QStringLiteral("formTag_stn_rev"));
    auto *place = f->findChild<QComboBox *>(QStringLiteral("formTag_placement"));
    CHECK(w.formKind() == 1 && tinNom && tinNom->value() == 1 && stnRev && stnRev->value() == 900 && place && place->currentIndex() == 0,
          "tag 1's form: TIN nominal 1, station 900, In line section");
    if (tinNom && place) {
        tinNom->setValue(9);
        place->setCurrentIndex(3);
    }
    CHECK(w.applyForm(), "Apply");
    const QHash<QString, qint64> now = fieldsOf(w.station().tags.at(0));
    CHECK(now.value(QStringLiteral("tin_nom")) == 9 && now.value(QStringLiteral("placement")) == 3
              && now.value(QStringLiteral("abs_loc")) == 98350 && StationLayout::info(w.station().tags.at(0)).crcOk,
          "tag 1: TIN 9, placed at a turnout, still at 98350 m, CRC-30 recomputed");
    CHECK(w.undo() && fieldsOf(w.station().tags.at(0)).value(QStringLiteral("tin_nom")) == 1, "Undo: one step");
    CHECK(!w.setTagFields(QStringLiteral("1"), { { QStringLiteral("unique"), 7 } }) && fieldsOf(w.station().tags.at(0)).value(QStringLiteral("unique")) == 1,
          "a change that would rename the tag: refused");
    CHECK(!w.setTagFields(QStringLiteral("1"), { { QStringLiteral("tin_nom"), 300 } }), "TIN 300: refused, too wide");

    // Track circuits, edited in their tab.
    emit panel->stepChosen(StationBuild::StepCircuits);
    QTableWidget *ct = w.circuitsTable();
    CHECK(ct->rowCount() == 0, "the new station has none");
    QPushButton *add = nullptr;
    for (QPushButton *b : w.findChildren<QPushButton *>())
        if (b->text() == QStringLiteral("Add row") && !w.kavachEditor()->isAncestorOf(b)) add = b;
    if (add) add->click();
    const QStringList cells{ QStringLiteral("1"), QStringLiteral("DMT"), QStringLiteral("DM"), QString(),
                             QStringLiteral("99550"), QStringLiteral("100450"), QStringLiteral("900") };
    for (int c = 0; c < cells.size(); ++c) ct->setItem(0, c, new QTableWidgetItem(cells.at(c)));
    const QVector<StationLayout::Circuit> cs = StationLayout::circuits(w.station());
    CHECK(cs.size() == 1 && cs[0].name == QStringLiteral("DMT") && cs[0].line == QStringLiteral("DM") && cs[0].from == 99550 && cs[0].to == 100450,
          "Add row, typed in: circuit DMT on DM from 99550 to 100450 m");
    CHECK(panel->stepList()->item(StationBuild::StepCircuits)->text().contains(QStringLiteral("1"))
              && !panel->stepList()->item(StationBuild::StepCircuits)->text().contains(QStringLiteral("⚠")),
          "the step counts it, nothing to report");
    ct->item(0, 2)->setText(QStringLiteral("XX"));
    CHECK(panel->stepList()->item(StationBuild::StepCircuits)->text().contains(QStringLiteral("⚠")), "on line XX: the lines lack it, said");
    ct->item(0, 2)->setText(QStringLiteral("DM"));
    QTemporaryDir tmp;
    const QString path = tmp.filePath(QStringLiteral("station_900.xml"));
    CHECK(w.saveFile(path), "saved as station_900.xml");
    StationLayout::Layout back;
    CHECK(StationLayout::load(path, &back, &err) && StationLayout::circuits(back).size() == 1 && back.tags.size() == 32
              && StationLayout::checks(back).isEmpty(),
          QByteArray("read back: its 32 tags and the circuit, nothing to report ") + err.toUtf8());

    // Steps hidden: the drawing takes the room.
    if (stepsAct) stepsAct->trigger();
    CHECK(!panel->isVisible() && !w.stepsShown(), "View ▸ Steps beside the drawing: hidden");
    if (stepsAct) stepsAct->trigger();
    CHECK(panel->isVisible(), "and back");
}
