#include "logtableview.h"
#include "uistyle.h"

#include <QFontMetrics>

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
        // Not Message (session 152): it is the stretched last column, and a
        // stored width set on it held until the next resize, so a narrower
        // pane than the one it was saved in scrolled sideways.
        if (c == LogModel::ColMessage) { continue; }
        if (widths.at(c) > 0) { view->setColumnWidth(c, widths.at(c)); }
    }
}

void applyDefaultColumnWidths(QTableView *view)
{
    if (!view) { return; }
    // Measured, not fixed (session 118). The widths were pixel constants —
    // Time 100, Source 60 — and "16:32:27.012" plus the cell's padding does
    // not fit 100 px at a 13 px font: the column read "16:32:27.0…" and its
    // header "ime (local". A larger text size or a Windows font only made it
    // worse. Each column is now the wider of a sample value and its header,
    // in the fonts they are drawn in, plus padding.
    const QFontMetrics cell(view->font());
    const QFontMetrics mono(UiStyle::monoFont());           // the Time column's painter
    QFont hf = view->horizontalHeader()->font();
    hf.setWeight(QFont::DemiBold);                          // headers are drawn semi-bold
    const QFontMetrics head(hf);
    auto headerText = [view](int col) {
        return view->model() ? view->model()->headerData(col, Qt::Horizontal).toString() : QString();
    };
    auto fit = [&](int col, int sample, int pad) {
        const int h = head.horizontalAdvance(headerText(col)) + 28;   // + the sort arrow
        view->setColumnWidth(col, qMax(sample + pad, h));
    };
    fit(LogModel::ColTime,      mono.horizontalAdvance(QStringLiteral("\u25CF 88:88:88.888")), 16);
    fit(LogModel::ColSource,    cell.horizontalAdvance(QStringLiteral("888_888")), 16);
    fit(LogModel::ColFriendly,  cell.horizontalAdvance(QStringLiteral("Fault_L1V1W")), 16);
    fit(LogModel::ColDirection, cell.horizontalAdvance(QStringLiteral("OUT")), 16);
    fit(LogModel::ColSeverity,  cell.horizontalAdvance(QStringLiteral("\u25B2 WARN")), 16);
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
