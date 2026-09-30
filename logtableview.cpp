#include "logtableview.h"

#include "logmodel.h"
#include "logtimedelegate.h"
#include "settings.h"

#include <QHeaderView>
#include <QTableView>

namespace LogTableView {

void applyDensity(QTableView *view)
{
    if (!view) { return; }
    const int d = Settings::rowDensity();
    view->verticalHeader()->setDefaultSectionSize(Settings::rowHeightFor(d));
    view->setWordWrap(Settings::wordWrapFor(d));
}

void applyDensityNow(QTableView *view)
{
    if (!view) { return; }
    applyDensity(view);
    view->verticalHeader()->reset();
}

void applyColumnVisibility(QTableView *view)
{
    if (!view) { return; }
    const QList<int> hidden = Settings::hiddenColumns();
    for (int c = 0; c < LogModel::ColumnCount; ++c) {
        const bool hide = (c != LogModel::ColMessage) && hidden.contains(c);
        view->setColumnHidden(c, hide);
    }
}

void applyColumnWidths(QTableView *view)
{
    if (!view) { return; }
    const QList<int> widths = Settings::logColumnWidths();
    if (widths.size() != LogModel::ColumnCount) { return; }
    for (int c = 0; c < LogModel::ColumnCount; ++c) {
        if (widths.at(c) > 0) { view->setColumnWidth(c, widths.at(c)); }
    }
}

void applyDefaultColumnWidths(QTableView *view)
{
    if (!view) { return; }
    view->setColumnWidth(LogModel::ColTime,      100);
    view->setColumnWidth(LogModel::ColSource,     60);
    view->setColumnWidth(LogModel::ColFriendly,  120);
    view->setColumnWidth(LogModel::ColDirection,  40);
    view->setColumnWidth(LogModel::ColSeverity,   66);   // fits "✕ ERR"
}

void configure(QTableView *view)
{
    if (!view) { return; }
    view->setAlternatingRowColors(true);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    // ExtendedSelection: shift-click a span, ctrl-click to add. Copying a
    // block of rows into an incident report is the single most common thing
    // anyone does with one of these tables, and single-selection made it a
    // row-at-a-time job.
    //
    // Panels that show ONE frame — raw bytes, decoded fields — follow the
    // CURRENT row rather than the selection, so they stay meaningful during
    // a multi-row selection: "the bytes of these fifty rows" is not a thing
    // they could show.
    view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->setTextElideMode(Qt::ElideRight);
    view->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    view->verticalHeader()->setVisible(false);
    view->horizontalHeader()->setStretchLastSection(true);

    // The Time column gets its own painter: a fixed font so the digits line
    // up down the column, and the leading characters shared with the row
    // above drawn muted so the ones that changed carry the contrast.
    //
    // Installed here rather than per window so every log table agrees —
    // that is the whole reason this file exists.
    if (!view->itemDelegateForColumn(LogModel::ColTime)) {
        view->setItemDelegateForColumn(LogModel::ColTime,
                                       new LogTimeDelegate(view));
    }

    applyDensity(view);
    applyDefaultColumnWidths(view);
    applyColumnWidths(view);        // stored widths win over the defaults
    applyColumnVisibility(view);
}

}  // namespace LogTableView
