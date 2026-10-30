#include "testutil.h"

#include "capturedecoder.h"
#include "packetbuilder.h"
#include "stationkavach.h"
#include "stationkavacheditor.h"
#include "stationlayout.h"
#include "stationlayoutwindow.h"
#include "statusline.h"

#include <QComboBox>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>

// =============================================================================
//  Session 423 — the Stationary KAVACH data (session 422) edited in Tools ▸
//  Station Layout, tab KAVACH: signals, routes, and the selected route's
//  tags and track profile; each edit one Undo step, what it would not do
//  under Checks, what cannot be read refused. Checked on station 527's data
//  (tests/fixtures/station_kavach/section30_527.json) and, through the
//  engine, against a real LSRP of replay/2026-10-08/loco_1_1_08102026_105200.cap.
// =============================================================================

namespace {

const char *const kFixture = DL_SRC_DIR "/tests/fixtures/station_kavach/section30_527.json";

QHash<QString, qint64> realLsrp(qint64 fn)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_105200.cap"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    for (const QString &l : QString::fromLatin1(f.readAll()).split(QLatin1Char('\n'))) {
        if (!l.startsWith(QLatin1String("@lsrp_"))) continue;
        QHash<QString, qint64> raw;
        CaptureDecoder::describe(CaptureDecoder::parseLine(l.trimmed()), nullptr, 0, &raw);
        if (raw.value(QStringLiteral("FRAME_NUM")) == fn) return raw;
    }
    return {};
}

// The MA the station's data answers that LSRP with, the loco new to it.
qint64 maFor(const QJsonObject &kavach, const QHash<QString, qint64> &lsrp, const Schema::Encoder &enc)
{
    StationKavach::Station s;
    if (!StationKavach::fromJson(kavach, &s)) return -1;
    StationKavach::Loco loco;
    const StationKavach::Answer a = StationKavach::answer(s, StationKavach::State::initial(s), &loco, lsrp, enc);
    for (const Schema::SubEntry &se : a.subs) if (se.type == 0) return se.values.value(QStringLiteral("MA_W_R_T_SIG"));
    return -1;
}

void setCell(QTableWidget *t, int r, int c, const QString &s)
{
    if (!t->item(r, c)) t->setItem(r, c, new QTableWidgetItem);
    t->item(r, c)->setText(s);
}

QString cellText(QTableWidget *t, int r, int c) { return t->item(r, c) ? t->item(r, c)->text() : QString(); }

bool checksSay(StationLayoutWindow &w, const QString &text)
{
    auto *list = qobject_cast<QListWidget *>(w.tabs()->widget(6));
    if (!list) return false;
    for (int i = 0; i < list->count(); ++i) if (list->item(i)->text().contains(text)) return true;
    return false;
}

} // namespace

TEST_SUITE(session423)
{
    PacketBuilder b;
    const QHash<QString, qint64> lsrp40380 = realLsrp(40380);
    CHECK(lsrp40380.value(QStringLiteral("LAST_RFID_TAG")) == 906, "the real LSRP 40380: the loco last read tag 906");

    // ---- the engine: tags the layout does not hold ----
    {
        StationLayout::Layout lay;
        StationKavach::Station st;
        CHECK(StationLayout::load(QString::fromLatin1(kFixture), &lay) && StationKavach::fromJson(lay.kavach, &st),
              "the station 527 fixture loads");
        CHECK(StationKavach::tagChecks(st, {}).isEmpty(), "a layout without tags: nothing said about tags");
        const QStringList c = StationKavach::tagChecks(st, { 12, 14, 16, 18 });
        CHECK(c.size() == 4 && c.contains(QStringLiteral("signal A900: foot tag 900 is not in the layout's tags"))
                  && c.contains(QStringLiteral("route S43-A900 DN Auto: tag(s) 906, 898, 900 not in the layout's tags")),
              "only the S48-S43 tags in the layout: A900, A904 and the two auto routes' tags are said");
    }

    // ---- the window: the fixture in the KAVACH tab ----
    StationLayoutWindow w;
    CHECK(w.openFile(QString::fromLatin1(kFixture)), "Station Layout opens the 527 file");
    StationKavachEditor *ed = w.kavachEditor();
    CHECK(ed && w.tabs()->indexOf(ed) >= 0, "the KAVACH tab is there");
    if (!ed) return;
    CHECK(w.tabs()->tabText(w.tabs()->indexOf(ed)) == QStringLiteral("KAVACH (3 routes)"), "the tab says 3 routes");
    QTableWidget *sigs = ed->signalTable(), *routes = ed->routeTable();
    CHECK(sigs->rowCount() == 4 && cellText(sigs, 2, 0) == QStringLiteral("A900") && cellText(sigs, 2, 1) == QStringLiteral("900")
              && cellText(sigs, 2, 2) == QStringLiteral("77664"),
          "4 signals; A900: foot tag 900, signal info 77664");
    CHECK(routes->rowCount() == 3 && cellText(routes, 1, 0) == QStringLiteral("S43-A900 DN Auto")
              && cellText(routes, 1, 15) == QStringLiteral("1116 m"),
          "3 routes; S43-A900 is 1116 m");
    auto *auth0 = qobject_cast<QComboBox *>(routes->cellWidget(0, 5));
    auto *os0 = qobject_cast<QComboBox *>(routes->cellWidget(0, 6));
    CHECK(auth0 && auth0->currentData().toInt() == 1 && os0 && os0->currentData().toInt() == 20,
          "S48-S43: OS authority at speed code 20, as the file has it");
    {
        StationKavach::Station read;
        StationKavach::fromJson(w.station().kavach, &read);
        CHECK(StationKavach::toJson(ed->station()) == StationKavach::toJson(read), "nothing lost: the editor holds the file's data");
    }
    CHECK(!w.isModified(), "showing it changes nothing");
    CHECK(maFor(w.station().kavach, lsrp40380, b.encoder()) == 1956, "as loaded, LSRP 40380 is answered MA 1956 m (A900 Yellow)");

    // ---- the selected route's tags and profile ----
    ed->selectRoute(1);
    QTableWidget *tags = ed->listTable(StationKavachEditor::Tags);
    CHECK(tags->rowCount() == 4 && cellText(tags, 1, 1) == QStringLiteral("906") && cellText(tags, 3, 3) == QStringLiteral("1116"),
          "S43-A900's tags: 906 second, the last 1116 m from the entry");
    QTableWidget *lc = ed->listTable(StationKavachEditor::Lc);
    const int lcD = [lc]() { for (int c = 0; c < lc->columnCount(); ++c) if (lc->horizontalHeaderItem(c)->text() == QLatin1String("d")) return c; return -1; }();
    const int lcId = [lc]() { for (int c = 0; c < lc->columnCount(); ++c) if (lc->horizontalHeaderItem(c)->text() == QLatin1String("id")) return c; return -1; }();
    CHECK(lc->rowCount() == 1 && lcD == 0 && cellText(lc, 0, lcD) == QStringLiteral("506") && cellText(lc, 0, lcId) == QStringLiteral("191"),
          "its LC gate: the schema's columns, d 506, id 191");
    QTableWidget *ssp = ed->listTable(StationKavachEditor::Ssp);
    QStringList sspCols;
    for (int c = 0; c < ssp->columnCount(); ++c) sspCols << ssp->horizontalHeaderItem(c)->text();
    CHECK(sspCols == (QStringList{ "d", "u", "a", "b", "c" }), "SSP columns d, u, a, b, c: class follows from them");

    // ---- edits: one Undo step each; Checks follow ----
    {
        auto *a900 = qobject_cast<QComboBox *>(sigs->cellWidget(2, 3));
        CHECK(a900 && a900->currentData().toInt() == 2, "A900 starts Yellow");
        if (a900) a900->setCurrentIndex(a900->findData(1));
        CHECK(w.isModified() && maFor(w.station().kavach, lsrp40380, b.encoder()) == 1116,
              "A900 set to start at Red: the same real LSRP is now answered MA 1116 m");
        CHECK(w.undo() && maFor(w.station().kavach, lsrp40380, b.encoder()) == 1956, "Undo: Yellow again, MA 1956 m");
    }
    {
        ed->selectRoute(1);
        ssp = ed->listTable(StationKavachEditor::Ssp);
        setCell(ssp, 0, 0, QStringLiteral("1000"));
        CHECK(checksSay(w, QStringLiteral("KAVACH: route S43-A900 DN Auto: its SSP covers 1000 m of its 1116 m")),
              "an SSP 116 m short: Checks says so");
        CHECK(ed->listTabs()->tabText(StationKavachEditor::Ssp) == QStringLiteral("SSP (1, 1000 of 1116 m)"),
              "and the SSP tab shows it");
        CHECK(ed->currentRoute() == 1, "the route stays selected through the edit");
        w.undo();
    }
    {
        const QJsonObject before = w.station().kavach;
        setCell(ed->listTable(StationKavachEditor::Lc), 0, 0, QStringLiteral("five hundred"));
        CHECK(w.station().kavach == before && w.status()->text().contains(QStringLiteral("is not a number")),
              "a word where a number goes: refused, said, nothing changed");
        CHECK(cellText(ed->listTable(StationKavachEditor::Lc), 0, 0) == QStringLiteral("506"), "the table is put back");
        setCell(routes, 1, 14, QStringLiteral("100, 1"));
        CHECK(w.station().kavach == before && w.status()->text().contains(QStringLiteral("three numbers")),
              "a location reset of two numbers: refused");
        setCell(routes, 1, 14, QStringLiteral("100, 1, 160000"));
        CHECK(ed->station().routes[1].locReset && ed->station().routes[1].absLocCorrection == 160000,
              "start, direction, correction: the route resets the location");
        w.undo();
    }
    {
        setCell(sigs, 2, 0, QStringLiteral("A901"));
        const StationKavach::Station &s = ed->station();
        CHECK(s.routes[1].exit == QStringLiteral("A901") && s.routes[2].entry == QStringLiteral("A901"),
              "A900 renamed A901: the routes naming it follow");
        CHECK(!checksSay(w, QStringLiteral("not in the signals")), "and Checks has nothing to say about it");
        w.undo();
        setCell(sigs, 2, 0, QStringLiteral("S43"));
        CHECK(ed->station().signalList[2].name == QStringLiteral("A900") && w.status()->text().contains(QStringLiteral("two signals")),
              "a second signal called S43: refused");
    }
    {
        ed->selectRoute(1);
        ed->addRoute(false);
        const StationKavach::Station &s = ed->station();
        CHECK(s.routes.size() == 4 && s.routes[2].entry == QStringLiteral("A900") && s.routes[2].tags.size() == 1
                  && s.routes[2].tags[0].tag == 900 && s.routes[2].pktDir == 1 && ed->currentRoute() == 2,
              "Add route after S43-A900: it starts at A900 (foot tag 900), same direction, selected");
        CHECK(checksSay(w, QStringLiteral("KAVACH: route A900-: exit signal  is not in the signals")),
              "its exit is not set yet: Checks says so");
        w.undo();
        ed->selectRoute(1);
        ed->addRoute(true);
        CHECK(ed->station().routes.size() == 4 && ed->station().routes[2].name == QStringLiteral("S43-A900 DN Auto (copy)")
                  && ed->station().routes[2].lc.size() == 1 && ed->station().routes[2].length() == 1116,
              "Copy route: tags and profile copied, named \"(copy)\"");
        w.undo();
        CHECK(ed->station().routes.size() == 3, "Undo: 3 routes again");
    }
    {
        ed->selectRoute(2);
        ed->listTabs()->setCurrentIndex(StationKavachEditor::Gradient);
        ed->listTable(StationKavachEditor::Gradient)->setCurrentCell(1, 0);
        ed->addListRow();
        CHECK(ed->station().routes[2].gradient.size() == 3 && ed->station().routes[2].gradient[2] == ed->station().routes[2].gradient[1],
              "Add row in the gradient: a copy of the row selected");
        CHECK(checksSay(w, QStringLiteral("its gradient covers 1360 m of its 840 m")), "now longer than the route: said");
        ed->listTable(StationKavachEditor::Gradient)->selectRow(2);
        ed->deleteListRows();
        CHECK(ed->station().routes[2].gradient.size() == 2 && !checksSay(w, QStringLiteral("gradient covers")), "deleted: as it was");
    }
    {
        auto *sections = ed->findChild<QLineEdit *>(QStringLiteral("skeSections"));
        CHECK(sections && sections->text().isEmpty(), "no sections set: Annexure-I's");
        sections->setText(QStringLiteral("2=1"));
        emit sections->editingFinished();
        CHECK(ed->station().sections.value(2) == 1 && maFor(w.station().kavach, lsrp40380, b.encoder()) == 1116,
              "Yellow given 1 section: LSRP 40380 is answered MA 1116 m");
        w.undo();
        sections->setText(QStringLiteral("Yellow"));
        emit sections->editingFinished();
        CHECK(ed->station().sections.isEmpty() && w.status()->text().contains(QStringLiteral("aspect=sections")),
              "\"Yellow\": refused");
    }

    // ---- saved, read back, answered ----
    {
        QTemporaryDir tmp;
        auto *a900 = qobject_cast<QComboBox *>(sigs->cellWidget(2, 3));
        if (a900) a900->setCurrentIndex(a900->findData(1));
        const QString path = tmp.filePath(QStringLiteral("s527.json"));
        CHECK(w.saveFile(path) && !w.isModified(), "saved");
        StationLayout::Layout back;
        CHECK(StationLayout::load(path, &back) && back.kavach == w.station().kavach, "read back the same");
        CHECK(maFor(back.kavach, lsrp40380, b.encoder()) == 1116, "the saved file answers LSRP 40380 with MA 1116 m");
    }

    // ---- remove; start from a layout's own signals ----
    {
        ed->findChild<QPushButton *>(QStringLiteral("skeRemove"))->click();
        CHECK(w.station().kavach.isEmpty() && w.tabs()->tabText(w.tabs()->indexOf(ed)) == QStringLiteral("KAVACH"),
              "Remove KAVACH data: gone, the tab plain");
        w.undo();
        CHECK(!w.station().kavach.isEmpty(), "Undo brings it back");

        CHECK(w.openFile(StationLayout::defaultFile()), "the built-in default layout opens");
        CHECK(w.station().kavach.isEmpty() && ed->startButton()->isVisibleTo(ed), "no KAVACH data: the start page");
        const StationLayout::Layout lay = w.station();
        ed->startButton()->click();
        const StationKavach::Station &s = ed->station();
        CHECK(!lay.signalList.isEmpty() && s.signalList.size() == lay.signalList.size() && s.routes.isEmpty()
                  && s.stationId == (lay.stations.isEmpty() ? 0 : int(lay.stations.first().id)),
              "started from the layout: its station id, every signal, no routes");
        bool red = true, feet = true;
        for (int i = 0; i < s.signalList.size(); ++i) {
            red = red && s.signalList[i].aspect == 1;
            QString foot = lay.signalList[i].footTag;
            if (foot.endsWith(QLatin1Char('D'))) foot.chop(1);
            feet = feet && s.signalList[i].footTag == foot.toInt() && s.signalList[i].name == lay.signalList[i].name.trimmed();
        }
        CHECK(red && feet, "each at Red, on its own foot tag");
        CHECK(!checksSay(w, QStringLiteral("is not in the layout's tags")), "their foot tags are all in the layout");
        w.undo();
        CHECK(w.station().kavach.isEmpty(), "Undo: no KAVACH data again");
    }
}
