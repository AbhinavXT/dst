#include "testutil.h"

#include "stationlayout.h"
#include "stationlayoutwindow.h"

#include <QAbstractItemDelegate>
#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QMenu>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>

// =============================================================================
//  Session 433 — Station Layout, easier to work in:
//    - right-click a signal on the drawing: its main head (2, 3, 4 aspects,
//      none: a shunt signal, from its name), calling-on, a shunt signal on
//      its mast; one Undo step each;
//    - the Signals tab: aspects, calling_on, shunt picked from a list;
//    - Tables (Ctrl+T): the tables hidden, the drawing has the window.
//  On station_527.xml (real: LPI).
// =============================================================================

namespace {

QAction *actionIn(QMenu *m, const QString &text)
{
    for (QAction *a : m->actions()) {
        if (a->text() == text) return a;
        if (a->menu())
            if (QAction *in = actionIn(a->menu(), text)) return in;
    }
    return nullptr;
}

int column(QTableWidget *t, const QString &header)
{
    for (int c = 0; c < t->columnCount(); ++c)
        if (t->horizontalHeaderItem(c)->text() == header) return c;
    return -1;
}

} // namespace

TEST_SUITE(session433)
{
    StationLayoutWindow w;
    CHECK(w.openFile(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_527.xml")), "station_527.xml");
    int s50 = -1;
    for (int i = 0; i < w.station().signalList.size(); ++i)
        if (w.station().signalList.at(i).name == QStringLiteral("S50")) s50 = i;
    CHECK(s50 >= 0, "signal S50 (LPI's home signal from Telapur)");

    // ---- right-click: its heads ----
    QMenu *m = w.signalMenuFor(s50);
    CHECK(m && actionIn(m, QStringLiteral("From its name")) && actionIn(m, QStringLiteral("From its name"))->isChecked(),
          "the menu: the head not said yet, from its name");
    if (m) actionIn(m, QStringLiteral("4 aspects: Red, Yellow, Green, Yellow"))->trigger();
    CHECK(w.station().signalList.at(s50).aspects == 4, "4 aspects, as the LPI sheet draws S50");
    delete m;
    m = w.signalMenuFor(s50);
    if (m) actionIn(m, QStringLiteral("Calling-on"))->trigger();
    CHECK(w.station().signalList.at(s50).callingOn && w.station().signalList.at(s50).aspects == 4, "and its calling-on (S50A)");
    CHECK(w.canvas()->selectedSignal() == s50, "the signal stays marked");
    delete m;
    CHECK(w.undo() && !w.station().signalList.at(s50).callingOn && w.station().signalList.at(s50).aspects == 4, "Undo: one step, the calling-on");
    CHECK(!w.signalMenuFor(-1) && !w.setSignalLook(s50, 5, false, false), "no signal, or 5 aspects: refused");
    m = w.signalMenuFor(s50);
    if (m) actionIn(m, QStringLiteral("None: a shunt signal"))->trigger();
    CHECK(w.station().signalList.at(s50).aspects == 0 && StationLayout::lookOf(w.station().signalList.at(s50)).shunt,
          "none: a shunt signal (the disc)");
    delete m;

    // ---- the Signals tab ----
    QTableWidget *t = w.table(1);
    const int ac = column(t, QStringLiteral("aspects")), cc = column(t, QStringLiteral("calling_on")), sc = column(t, QStringLiteral("shunt"));
    CHECK(ac >= 0 && cc == ac + 1 && sc == ac + 2, "columns aspects, calling_on, shunt");
    CHECK(t->item(s50, ac)->text() == QStringLiteral("0"), "S50's row says 0");
    QWidget *ed = t->itemDelegate()->createEditor(t->viewport(), QStyleOptionViewItem(), t->model()->index(s50, ac));
    auto *box = qobject_cast<QComboBox *>(ed);
    CHECK(box && box->count() == 5 && box->itemData(1).toString() == QStringLiteral("2"), "aspects: a list, not typed");
    if (box) {
        t->itemDelegate()->setEditorData(box, t->model()->index(s50, ac));
        CHECK(box->currentData().toString() == QStringLiteral("0"), "showing the cell's value");
        box->setCurrentIndex(2);
        t->itemDelegate()->setModelData(box, t->model(), t->model()->index(s50, ac));
    }
    delete ed;
    CHECK(w.station().signalList.at(s50).aspects == 3, "picked 3: the signal has 3 aspects");
    ed = t->itemDelegate()->createEditor(t->viewport(), QStyleOptionViewItem(), t->model()->index(s50, sc));
    box = qobject_cast<QComboBox *>(ed);
    if (box) { box->setCurrentIndex(1); t->itemDelegate()->setModelData(box, t->model(), t->model()->index(s50, sc)); }
    delete ed;
    CHECK(box && w.station().signalList.at(s50).shunt, "shunt: yes");
    ed = t->itemDelegate()->createEditor(t->viewport(), QStyleOptionViewItem(), t->model()->index(s50, 1));
    CHECK(ed && !qobject_cast<QComboBox *>(ed), "the name is still typed");
    delete ed;

    // ---- Tables ----
    w.resize(1200, 800);
    w.show();
    auto *btn = w.findChild<QAction *>(QStringLiteral("stationLayoutTables"));
    CHECK(btn && btn->isChecked() && w.tablesShown(), "Tables: shown at first");
    QCoreApplication::processEvents();
    const int before = w.canvas()->parentWidget()->height();
    if (btn) btn->trigger();
    QCoreApplication::processEvents();
    CHECK(!w.tablesShown() && !w.tabs()->isVisible(), "Tables off: hidden");
    CHECK(w.canvas()->parentWidget()->height() > before, "the drawing grows into the room");
    m = w.signalMenuFor(s50);
    if (m) actionIn(m, QStringLiteral("Show its row"))->trigger();
    delete m;
    CHECK(w.tablesShown() && btn && btn->isChecked() && w.tabs()->currentIndex() == 1 && t->currentRow() == s50,
          "Show its row: the tables back, the Signals tab on the signal's row");
}
