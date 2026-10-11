#include "testutil.h"

#include "frozencolumns.h"
#include "logentry.h"
#include "logmodel.h"
#include "nmslog.h"
#include "nmslogwindow.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QFile>
#include <QHeaderView>
#include <QMenu>
#include <QScrollBar>
#include <QTableView>
#include <QTextStream>
#include <QTimer>

// =============================================================================
//  Session 253 — NMS tables: freeze columns, pin rows. Frozen columns stay at
//  the left while the rest scroll sideways (FrozenColumns: an overlay view on
//  the same model); pinned rows sit in a strip above the table, in its
//  columns, whatever it scrolls to or filters. Real frames:
//  replay/loco_1_1_29062026_134509.cap (2081 health packets).
// =============================================================================

namespace {

QVector<LogEntryPtr> healthOf(const QString &path)
{
    QVector<LogEntryPtr> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    QTextStream ts(&f);
    qint64 ms = 1782465000000;
    while (!ts.atEnd()) {
        const QString line = ts.readLine().trimmed();
        if (!line.startsWith(QLatin1String("@nmshlth_"))) continue;
        auto e = LogEntryPtr::create();
        e->header.source_id = 21;
        e->header.kvchId = 1;
        e->epochMs = ms += 1000;
        e->severity = Severity::Info;
        e->text = line;
        e->cacheDerived();
        out << e;
    }
    return out;
}

void settle()
{
    for (int i = 0; i < 5; ++i) QApplication::processEvents();
}

// Right-click `pos` in `w` (a view's viewport or header), pick the menu
// action named `name`. False if the menu had no such action.
bool pickFromMenu(QWidget *w, const QPoint &pos, const QString &name, QStringList *texts = nullptr)
{
    bool found = false;
    QTimer::singleShot(50, [&]() {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu) return;
        for (QAction *a : menu->actions()) {
            if (texts && !a->text().isEmpty()) *texts << a->text();
            if (a->objectName() == name || a->text() == name) { found = true; a->trigger(); }
        }
        menu->close();
    });
    emit w->customContextMenuRequested(pos);
    settle();
    return found;
}

}  // namespace

TEST_SUITE(session253)
{
    const QVector<LogEntryPtr> hl = healthOf(QStringLiteral("../replay/loco_1_1_29062026_134509.cap"));
    CHECK(hl.size() == 2081, "fixture: 2081 @nmshlth packets");
    LogModel model(nullptr, 100000);
    model.appendEntries(hl.mid(0, 1500));
    NmsLogWindow w(NmsLog::Kind::Health, &model, QStringLiteral("1_1"), QStringLiteral("L1"));
    w.resize(900, 500);
    w.show();
    settle();
    QTableView *v = w.view();
    auto *frozen = w.findChild<QTableView *>(QStringLiteral("frozenColumns"));
    CHECK(frozen && !frozen->isVisible() && w.frozenColumns() == 0, "at open: nothing frozen");
    CHECK(!w.pinnedView()->isVisible(), "at open: no pinned strip");

    // ---- freeze: PC time … CRC (six columns) ----
    w.freezeColumns(6);
    settle();
    CHECK(frozen->isVisible(), "freeze 6: the frozen columns are shown");
    int shown = 0, width = 0;
    bool sameWidths = true;
    for (int c = 0; c < frozen->model()->columnCount(); ++c) {
        if (frozen->isColumnHidden(c)) continue;
        ++shown;
        width += frozen->columnWidth(c);
        sameWidths = sameWidths && frozen->columnWidth(c) == v->columnWidth(c) && c < 6;
    }
    CHECK(shown == 6 && sameWidths, "freeze 6: exactly the first six, at the table's widths");
    CHECK(frozen->width() == width && frozen->x() == v->verticalHeader()->width() + v->frameWidth(),
          "freeze 6: over the table's first six, right of the row numbers");
    v->horizontalScrollBar()->setValue(v->horizontalScrollBar()->maximum());
    v->verticalScrollBar()->setValue(500);
    settle();
    CHECK(v->horizontalScrollBar()->value() > 0 && frozen->x() == v->verticalHeader()->width() + v->frameWidth()
              && frozen->horizontalHeader()->logicalIndexAt(1) == 0,
          "scrolled to the far right: the frozen columns stay at the left, from PC time");
    CHECK(frozen->verticalScrollBar()->value() == 500, "scrolled down: the frozen rows follow");
    {
        const QModelIndex top = frozen->indexAt(QPoint(2, 2));
        CHECK(top.isValid() && top.row() == v->indexAt(QPoint(2, 2)).row(), "the frozen rows are the table's rows");
    }

    model.appendEntries(hl.mid(1500));
    settle();

    // ---- pin rows ----
    w.setPinned(2035, true);    // the first DMI NOT OK
    w.setPinned(10, true);
    settle();
    QTableView *pv = w.pinnedView();
    CHECK(pv->isVisible() && pv->model()->rowCount() == 2 && w.pinnedRows() == QList<int>({ 10, 2035 }),
          "pin rows 11 and 2036: the strip shows both, in order");
    CHECK(w.tableRow(pv->model()->index(1, 0)) == 2035, "the strip's second row is packet 2036");
    {
        bool same = true;
        for (int c = 0; c < v->model()->columnCount(); ++c)
            same = same && pv->columnWidth(c) == v->columnWidth(c);
        CHECK(same && pv->verticalHeader()->width() == v->verticalHeader()->width(),
              "the strip has the table's column widths and row-number width");
    }
    v->horizontalScrollBar()->setValue(v->horizontalScrollBar()->maximum() / 2);
    settle();
    CHECK(pv->horizontalScrollBar()->value() == v->horizontalScrollBar()->value(), "the strip scrolls sideways with the table");
    QCheckBox *changes = w.findChild<QCheckBox *>(QStringLiteral("nmsChangesOnly"));
    changes->setChecked(true);
    settle();
    CHECK(pv->model()->rowCount() == 2, "Changes only: the pinned rows stay pinned");
    changes->setChecked(false);
    {
        qint64 jumped = -1;
        auto c = QObject::connect(&w, &NmsLogWindow::jumpRequested, [&](const QString &, qint64 ms) { jumped = ms; });
        emit pv->doubleClicked(pv->model()->index(1, 0));
        QObject::disconnect(c);
        CHECK(jumped == w.table().epochMs.at(2035), "double-click a pinned row: that packet's frame");
    }

    // ---- the right-click menus ----
    {
        // A cell in the table: pin it, then freeze up to its column.
        v->scrollToTop();
        v->horizontalScrollBar()->setValue(0);
        w.freezeColumns(0);
        settle();
        const QRect cell = v->visualRect(v->model()->index(3, 8));
        QStringList texts;
        CHECK(pickFromMenu(v, v->viewport()->mapTo(v, cell.center()), QStringLiteral("nmsPinRow"), &texts),
              qPrintable("right-click a cell: 'Pin row …' (" + texts.join(" | ") + ")"));
        CHECK(w.pinnedRows().contains(3), "and row 4 is pinned");
        CHECK(texts.contains(QStringLiteral("Freeze columns up to “%1”").arg(w.table().header(8))),
              "the same menu offers freezing up to that column");
        CHECK(pickFromMenu(v, v->viewport()->mapTo(v, cell.center()), QStringLiteral("nmsFreeze")) && w.frozenColumns() == 9,
              "right-click, Freeze: the columns up to that one are frozen (9)");
        // The header: freeze up to the third column.
        const int x = v->horizontalHeader()->sectionViewportPosition(2) + 5;
        CHECK(pickFromMenu(v->horizontalHeader(), QPoint(x, 5), QStringLiteral("nmsFreeze")) && w.frozenColumns() == 3,
              "right-click the header of seq: freeze up to it (3)");
        // The strip: Unpin all.
        const QRect pinnedCell = pv->visualRect(pv->model()->index(0, 1));
        CHECK(pickFromMenu(pv, pv->viewport()->mapTo(pv, pinnedCell.center()), QStringLiteral("Unpin all"))
                  && w.pinnedRows().isEmpty(),
              "right-click the strip, Unpin all: nothing pinned");
        settle();
        CHECK(!pv->isVisible(), "and the strip is gone");
        CHECK(pickFromMenu(v, v->viewport()->mapTo(v, cell.center()), QStringLiteral("Unfreeze columns"))
                  && w.frozenColumns() == 0 && !frozen->isVisible(),
              "Unfreeze columns: nothing frozen");
    }

    // Scrolled before the window is shown, then left to settle: the frozen
    // rows are still the table's (Qt moved the overlay's scroll bar back to 0
    // during the first layout until the overlay was made to follow it).
    {
        NmsLogWindow w3(NmsLog::Kind::Health, &model, QStringLiteral("1_1"), QStringLiteral("L1"));
        w3.freezeColumns(6);
        w3.setPinned(0, true);
        w3.view()->scrollTo(w3.view()->model()->index(2035, 30), QAbstractItemView::PositionAtCenter);
        w3.resize(1100, 720);
        w3.show();
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 500) QApplication::processEvents(QEventLoop::AllEvents, 20);
        auto *f3 = w3.findChild<QTableView *>(QStringLiteral("frozenColumns"));
        CHECK(f3 && w3.view()->rowAt(0) > 1000 && f3->rowAt(0) == w3.view()->rowAt(0),
              qPrintable(QStringLiteral("scrolled before shown, settled: frozen top row %1, table's %2")
                             .arg(f3 ? f3->rowAt(0) : -1).arg(w3.view()->rowAt(0))));
    }

    // The tab cleared: pins go with the rows they named.
    w.setPinned(1, true);
    model.clear();
    settle();
    CHECK(w.pinnedRows().isEmpty() && !w.pinnedView()->isVisible(), "the tab cleared: no pins left");

    // A new column arriving live (a reset of the table): still six frozen.
    // The window opens just before the packet that brings the last new column.
    {
        NmsLog::Table t;
        int split = -1, cols = 0;
        for (int i = 0; i < hl.size(); ++i) {
            NmsLog::append(t, NmsLog::Kind::Health, *hl.at(i));
            if (t.columnCount() > cols) { cols = t.columnCount(); split = i; }
        }
        CHECK(split > 0, "fixture: a packet that brings a new column after the first");
        LogModel m2(nullptr, 100000);
        m2.appendEntries(hl.mid(0, split));
        NmsLogWindow w2(NmsLog::Kind::Health, &m2, QStringLiteral("1_1"), QStringLiteral("L1"));
        w2.show();
        w2.freezeColumns(6);
        settle();
        const int before = w2.view()->model()->columnCount();
        m2.appendEntries(hl.mid(split));
        settle();
        auto *f2 = w2.findChild<QTableView *>(QStringLiteral("frozenColumns"));
        int nowShown = 0;
        for (int c = 0; c < f2->model()->columnCount(); ++c) if (!f2->isColumnHidden(c)) ++nowShown;
        CHECK(w2.view()->model()->columnCount() > before && nowShown == 6 && f2->isVisible(),
              qPrintable(QStringLiteral("a column added live (%1 -> %2): still six frozen").arg(before).arg(w2.view()->model()->columnCount())));
    }
}
