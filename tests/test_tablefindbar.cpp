#include "testutil.h"

#include "lococonfigcore.h"
#include "tablefindbar.h"

#include <QApplication>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QSignalSpy>
#include <QTableWidget>

// =============================================================================
//  TableFindBar -- find in the Live Loco Console's Field | Value tables.
//
//  The table is filled the way LocoConsoleWindow::refreshTypeTables() fills
//  it -- new items for every row, on every refresh -- with the real LINFO
//  field names read from kavach.xml, so "a 170-row struct, rebuilt four
//  times a second" is what is being searched.
// =============================================================================

namespace {

struct Row { QString field; QString value; };

// Rebuild every item, as the console does on each refresh.
void fill(QTableWidget *table, const QVector<Row> &rows)
{
    table->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row) {
        table->setItem(row, 0, new QTableWidgetItem(rows.at(row).field));
        table->setItem(row, 1, new QTableWidgetItem(rows.at(row).value));
    }
}

QVector<Row> linfoRows()
{
    QVector<Row> rows;
    LocoInfo::Layout layout;
    QString error;
    layout.load(QStringLiteral(":/schema/kavach.xml"), &error);
    for (const LocoInfo::Field &field : layout.fields()) {
        QString value = QString::number(field.offset);
        if (field.name == QLatin1String("nms_ip_address")) {
            value = QStringLiteral("127.0.0.1");
        }
        rows.append({ field.name, value });
    }
    return rows;
}

bool tinted(QTableWidget *table, int row)
{
    return table->item(row, 0)->background().style() != Qt::NoBrush;
}

int hiddenRows(QTableWidget *table)
{
    int hidden = 0;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (table->isRowHidden(row)) {
            ++hidden;
        }
    }
    return hidden;
}

}  // namespace

TEST_SUITE(tablefindbar)
{
    // ---- the matching rule -----------------------------------------------------------
    const QStringList speedMargin = TableFindBar::termsFor(QStringLiteral("Speed margin"));
    CHECK(TableFindBar::matches(QStringLiteral("speed_margin_eb"), speedMargin), "words match across underscores");
    CHECK(TableFindBar::matches(QStringLiteral("SPEED_MARGIN_NSB"), speedMargin), "case-insensitive");
    CHECK(!TableFindBar::matches(QStringLiteral("sos_speed_limit"), speedMargin), "every word must appear");
    CHECK(TableFindBar::matches(QStringLiteral("loco_wheel_dia1"), TableFindBar::termsFor(QStringLiteral("wheel dia"))),
          "'wheel dia' finds loco_wheel_dia1");
    CHECK(TableFindBar::matches(QStringLiteral("speed_margin_eb"), TableFindBar::termsFor(QStringLiteral("margin_eb"))),
          "an underscore in the query works too");
    CHECK(!TableFindBar::matches(QStringLiteral("anything"), TableFindBar::termsFor(QStringLiteral("   "))),
          "an empty query matches nothing");

    // ---- a LINFO-sized table -------------------------------------------------------------
    const QVector<Row> rows = linfoRows();
    CHECK(rows.size() > 150, "the table has every LINFO member");

    int expected = 0;
    for (const Row &row : rows) {
        if (TableFindBar::matches(row.field, speedMargin)) {
            ++expected;
        }
    }
    CHECK(expected >= 5, "speed_margin_* appears at the top level and in national_values");

    QWidget host;
    host.resize(700, 300);
    auto *layout = new QVBoxLayout(&host);
    auto *table = new QTableWidget(0, 2, &host);
    auto *other = new QTableWidget(0, 2, &host);
    auto *bar = new TableFindBar(&host);
    layout->addWidget(table);
    layout->addWidget(bar);
    other->hide();
    fill(table, rows);
    fill(other, { { QStringLiteral("status_word"), QStringLiteral("0x0001") } });
    bar->setAllTables({ qMakePair(QStringLiteral("LINFO"), table), qMakePair(QStringLiteral("STATUS"), other) });
    bar->setTable(table);
    host.show();
    QApplication::processEvents();

    bar->open();
    bar->setQuery(QStringLiteral("speed margin"));
    CHECK(bar->matchCount() == expected, "every matching field is found");
    CHECK(bar->currentRow() >= 0 && rows.at(bar->currentRow()).field.contains(QStringLiteral("speed_margin")),
          "typing moves to the first match");
    CHECK(bar->statusText() == QStringLiteral("1 of %1").arg(expected), "and says '1 of N'");
    CHECK(tinted(table, bar->currentRow()), "matches are tinted");
    CHECK(!tinted(table, 0), "non-matches are not");

    bar->findNext();
    CHECK(bar->statusText() == QStringLiteral("2 of %1").arg(expected), "Next moves on");
    bar->findPrevious();
    bar->findPrevious();
    CHECK(bar->statusText() == QStringLiteral("%1 of %1").arg(expected), "Previous wraps to the last match");

    // ---- the live refresh: rows rebuilt, one inserted above --------------------------------
    const QString followed = bar->currentField();
    const int rowBefore = bar->currentRow();
    QVector<Row> refreshed = rows;
    refreshed.prepend({ QStringLiteral("rtc"), QStringLiteral("12:00:00") });
    fill(table, refreshed);
    CHECK(!tinted(table, rowBefore + 1), "a rebuild wipes the tint (new items)...");
    bar->reapply();
    CHECK(bar->currentField() == followed && bar->currentRow() == rowBefore + 1,
          "...and reapply() finds the same FIELD again, one row lower");
    CHECK(tinted(table, bar->currentRow()), "with the tint back");
    CHECK(table->selectionModel()->isRowSelected(bar->currentRow(), QModelIndex()), "and the row selected");

    // Duplicate names: LOCO_INFO has speed_margin_warning twice (top level
    // and national_values). Following the SECOND one must not snap back to
    // the first after a refresh.
    {
        bar->setQuery(QStringLiteral("speed_margin_warning"));
        CHECK(bar->matchCount() == 2, "speed_margin_warning appears twice in LINFO");
        bar->findNext();
        const int second = bar->currentRow();
        CHECK(bar->statusText() == QStringLiteral("2 of 2"), "on the second one");
        fill(table, refreshed);
        bar->reapply();
        CHECK(bar->currentRow() == second && bar->statusText() == QStringLiteral("2 of 2"),
              "after a refresh it is still the second one, not the first of that name");
        bar->setQuery(QStringLiteral("speed margin"));
    }

    // reapply() must not scroll: the operator may be reading elsewhere.
    table->verticalScrollBar()->setValue(0);
    fill(table, refreshed);
    bar->reapply();
    CHECK(table->verticalScrollBar()->value() == 0, "a refresh never moves the view");
    bar->findNext();
    CHECK(table->verticalScrollBar()->value() > 0, "while Next does bring the match into view");

    // ---- only matching rows --------------------------------------------------------------
    bar->setOnlyMatching(true);
    CHECK(hiddenRows(table) == refreshed.size() - bar->matchCount(), "'Only matching rows' hides the rest");
    fill(table, refreshed);
    bar->reapply();
    CHECK(hiddenRows(table) == refreshed.size() - bar->matchCount(), "and keeps hiding them across refreshes");
    bar->setOnlyMatching(false);
    CHECK(hiddenRows(table) == 0, "turned off, every row is back");

    // ---- values too ------------------------------------------------------------------------
    bar->setQuery(QStringLiteral("127.0.0.1"));
    CHECK(bar->matchCount() == 0, "values are not searched by default");
    bar->setValuesToo(true);
    CHECK(bar->matchCount() == 1 && bar->currentField() == QStringLiteral("nms_ip_address"),
          "'Values too' finds the field by its value");
    bar->setValuesToo(false);

    // ---- a field on another tab --------------------------------------------------------------
    QSignalSpy switched(bar, &TableFindBar::switchToTable);
    bar->setQuery(QStringLiteral("status word"));
    CHECK(bar->matchCount() == 0 && bar->statusText().contains(QStringLiteral("STATUS 1")),
          "no match here: the bar names the tab that has it");
    bar->findNext();
    CHECK(switched.count() == 1 && switched.first().first().value<QTableWidget *>() == other,
          "and Enter asks to switch there");
    bar->setTable(other);
    CHECK(bar->matchCount() == 1 && bar->currentRow() == 0, "on the new tab it lands on the match");

    // ---- leaving a table, closing ------------------------------------------------------------
    bar->setTable(table);
    bar->setQuery(QStringLiteral("wheel dia"));
    bar->setOnlyMatching(true);
    bar->setTable(other);
    CHECK(hiddenRows(table) == 0 && !tinted(table, 5), "a table the bar leaves is left clean");
    bar->setTable(table);
    bar->close();
    CHECK(hiddenRows(table) == 0, "closing unhides every row");
    bool anyTint = false;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (tinted(table, row)) {
            anyTint = true;
        }
    }
    CHECK(!anyTint, "and clears every tint");
    fill(table, refreshed);
    bar->reapply();
    CHECK(hiddenRows(table) == 0, "a closed bar does nothing on refresh");
}
