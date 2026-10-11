#include "testutil.h"

#include "logentry.h"
#include "logmodel.h"
#include "nmslog.h"
#include "nmslogwindow.h"

#include <QCoreApplication>
#include <QFile>
#include <QTableView>
#include <QTextStream>

// =============================================================================
//  Session 249 — the NMS tables: a row that arrives after the window opened
//  is never cut off. Its column widens to fit (and never narrows: the operator
//  may have widened it). Real frames from replay/loco_1_1_26062026_162418.cap:
//  a one-fault "28 (GSM-2)" packet first, then a one-fault
//  "2 (VCC Mc-2) / 26 (safety error)" packet, wider in the same columns.
// =============================================================================

namespace {

QVector<LogEntryPtr> faultsOf(const QString &path)
{
    QVector<LogEntryPtr> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    QTextStream ts(&f);
    qint64 ms = 1782465000000;
    while (!ts.atEnd()) {
        const QString line = ts.readLine().trimmed();
        if (!line.startsWith(QLatin1String("@nmsflt_"))) continue;
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

QString cellOf(const LogEntryPtr &e, const QString &column)
{
    NmsLog::Table t;
    NmsLog::append(t, NmsLog::Kind::Fault, *e);
    return t.index.contains(column) ? t.cell(0, t.index.value(column)) : QString();
}

// Every cell fits its column: the width the view would give it, at least.
bool allFit(QTableView *v, QString *firstCut)
{
    QAbstractItemModel *m = v->model();
    for (int r = 0; r < m->rowCount(); ++r)
        for (int c = 0; c < m->columnCount(); ++c)
            if (v->columnWidth(c) < v->sizeHintForIndex(m->index(r, c)).width()) {
                if (firstCut) *firstCut = QStringLiteral("row %1 '%2'").arg(r + 1).arg(m->index(r, c).data().toString());
                return false;
            }
    return true;
}

}  // namespace

TEST_SUITE(session249)
{
    const QVector<LogEntryPtr> faults = faultsOf(QStringLiteral("../replay/loco_1_1_26062026_162418.cap"));
    LogEntryPtr narrow, wide;
    for (const LogEntryPtr &e : faults) {
        if (cellOf(e, QStringLiteral("fault_count")) != QLatin1String("1")) continue;
        const QString module = cellOf(e, QStringLiteral("fault[1] module"));
        if (!narrow && module == QLatin1String("28 (GSM-2)")) narrow = e;
        if (!wide && module == QLatin1String("2 (VCC Mc-2)")) wide = e;
    }
    CHECK(narrow && wide, "fixture: one-fault packets for module 28 (GSM-2) and 2 (VCC Mc-2)");
    if (!narrow || !wide) return;
    CHECK(cellOf(wide, QStringLiteral("fault[1] fault_id")) == QLatin1String("26 (safety error)"),
          "fixture: the wide one is 26 (safety error)");

    LogModel model(nullptr, 1000);
    model.appendEntries({ narrow });
    NmsLogWindow w(NmsLog::Kind::Fault, &model, QStringLiteral("1_1"), QStringLiteral("L1"));
    QTableView *v = w.view();
    const int idCol = w.table().index.value(QStringLiteral("fault[1] fault_id"));
    QString cut;
    CHECK(allFit(v, &cut), qPrintable("at open: every cell fits " + cut));
    // Sized to exactly the first row's text, as at open when the cell (not
    // the header) sets the width: what the font in use decides otherwise.
    v->setColumnWidth(idCol, v->sizeHintForIndex(v->model()->index(0, idCol)).width() + (v->showGrid() ? 1 : 0));
    const int before = v->columnWidth(idCol);
    const int columns = v->model()->columnCount();

    model.appendEntries({ wide });
    QCoreApplication::processEvents();
    CHECK(v->model()->rowCount() == 2 && v->model()->columnCount() == columns,
          "the wide packet arrives: a row, no new column");
    cut.clear();
    CHECK(allFit(v, &cut), qPrintable("after it arrives: every cell fits " + cut));
    CHECK(v->columnWidth(idCol) > before, "fault[1] fault_id widened for '26 (safety error)'");

    // Widened by hand, then a narrower row: the column keeps its width.
    v->setColumnWidth(idCol, before + 200);
    model.appendEntries({ narrow });
    QCoreApplication::processEvents();
    CHECK(v->model()->rowCount() == 3 && v->columnWidth(idCol) == before + 200,
          "a narrower row: a column the operator widened is not narrowed");
}
