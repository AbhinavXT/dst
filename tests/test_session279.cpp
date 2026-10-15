#include "testutil.h"

#include "flasherstyle.h"
#include "lococonfigmodel.h"
#include "lococonfigwindow.h"
#include "uistyle.h"

#include <QFont>
#include <QTableView>
#include <QTemporaryDir>

// =============================================================================
//  Session 279 — Loco Configuration: a field changed from its default reads
//  at a glance. Abhinav: "make change fields more bold" (fields changed from
//  the defaults in the config). Before, only the value was bold, and in a
//  bare QFont(): the app's proportional font, in a monospaced table.
// =============================================================================

TEST_SUITE(session279)
{
    QTemporaryDir temp;
    CHECK(temp.isValid(), "a temporary data directory");
    LocoConfigWindow window(nullptr, temp.path());
    CHECK(window.isUsable(), "the window loads the layout from the schema");
    LocoFieldModel *model = window.fieldModel();
    QTableView *table = window.findChild<QTableView *>();
    CHECK(table && model, "the field table");
    if (!table || !model) return;

    const int unitRow = model->rowForKey(QStringLiteral("loco_unit_id"));
    const int wheelRow = model->rowForKey(QStringLiteral("loco_wheel_dia1"));
    CHECK(model->setData(model->index(unitRow, LocoFieldModel::ColumnValue), QStringLiteral("30421"), Qt::EditRole),
          "loco_unit_id changed from its default");
    CHECK(!model->index(wheelRow, 0).data(LocoFieldModel::ChangedFromDefaultRole).toBool(),
          "fixture: loco_wheel_dia1 still at its default");

    const QFont tableFont = table->font();
    for (const int col : { int(LocoFieldModel::ColumnField), int(LocoFieldModel::ColumnValue) }) {
        const QVariant v = model->index(unitRow, col).data(Qt::FontRole);
        const QFont f = v.value<QFont>();
        CHECK(v.isValid() && f.bold(), QByteArray("changed: column ") + QByteArray::number(col) + " is bold");
        CHECK(f.family() == tableFont.family(),
              QByteArray("in the table's own font (") + f.family().toUtf8() + " vs " + tableFont.family().toUtf8() + ")");
    }
    CHECK(model->index(unitRow, LocoFieldModel::ColumnValue).data(Qt::ForegroundRole).value<QColor>()
              == FlasherStyle::primary(),
          "the changed value is in the accent colour");
    CHECK(!model->index(unitRow, LocoFieldModel::ColumnDefault).data(Qt::FontRole).isValid(),
          "the Default column is not bold");
    CHECK(!model->index(wheelRow, LocoFieldModel::ColumnField).data(Qt::FontRole).isValid()
              && !model->index(wheelRow, LocoFieldModel::ColumnValue).data(Qt::FontRole).isValid(),
          "an unchanged field stays plain");
}
