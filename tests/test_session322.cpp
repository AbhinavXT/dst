#include "testutil.h"

#include "lococonfigmodel.h"
#include "lococonfigwindow.h"

#include <QFontMetrics>
#include <QHeaderView>
#include <QSet>
#include <QTableView>
#include <QTemporaryDir>

// =============================================================================
//  Session 322 — Loco Configuration's VCC columns: the whole IP in the header.
//
//  Abhinav's screenshot: with a loco's four controllers ticked, every VCC
//  column read "VCC 192.168.25.1…" (150 px cut the last digits), so they
//  could not be told apart. The header is now the IP alone, the column is as
//  wide as it, and "VCC …" moved to the header's tooltip.
// =============================================================================

TEST_SUITE(session322)
{
    QTemporaryDir temp;
    LocoConfigWindow window(nullptr, temp.path());
    QVector<LocoInfo::SendTarget> targets(4);
    const QStringList ips = { QStringLiteral("192.168.25.16"), QStringLiteral("192.168.25.102"),
                              QStringLiteral("192.168.25.103"), QStringLiteral("192.168.25.104") };
    for (int i = 0; i < 4; ++i) targets[i].ip = ips.at(i);
    window.setTargets(targets);
    LocoFieldModel *model = window.fieldModel();
    QTableView *table = window.findChild<QTableView *>();
    CHECK(model->liveColumnCount() == 4 && table, "four VCC columns");
    if (!table || model->liveColumnCount() != 4) return;

    const QFontMetrics fm(table->horizontalHeader()->font());
    QSet<QString> headers;
    bool fits = true;
    for (int i = 0; i < 4; ++i) {
        const int section = LocoFieldModel::ColumnCount + i;
        const QString text = model->headerData(section, Qt::Horizontal, Qt::DisplayRole).toString();
        headers.insert(text);
        fits = fits && text == ips.at(i) && table->columnWidth(section) >= fm.horizontalAdvance(text) + 16;
    }
    CHECK(headers.size() == 4, "four different headers");
    CHECK(fits, "each header is the whole IP, and its column is wide enough for it");
    CHECK(model->headerData(LocoFieldModel::ColumnCount, Qt::Horizontal, Qt::ToolTipRole).toString()
              .startsWith(QStringLiteral("VCC 192.168.25.16")),
          "the tooltip says it is a VCC's column");
}
