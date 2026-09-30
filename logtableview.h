#ifndef LOGTABLEVIEW_H
#define LOGTABLEVIEW_H
// =============================================================================
//  logtableview.{h,cpp} — how a table of log rows is set up, in one place.
//
//  Every one of these was a private method of MainWindow, which meant a log
//  table anywhere else in the program — a compare pane, a session viewer —
//  got whatever its own construction happened to say. The result was two
//  tables side by side answering the same settings differently: the operator
//  set rows to Comfortable and the compare panes stayed at 44 px, hid the
//  Source column and the compare panes kept showing it.
//
//  These are pure functions of Settings and a view. They read no window
//  state, so anything holding a QTableView over a LogModel can call them,
//  and there is exactly one answer to "how tall is a row" in the program.
// =============================================================================

class QTableView;

namespace LogTableView {

// Row height and word wrap, from the density setting.
void applyDensity(QTableView *view);

// As above, then forces the header to re-lay-out rows it has already sized.
// The header caches the old default, so without this existing rows keep
// their previous height while new ones arrive at the new one.
void applyDensityNow(QTableView *view);

// Show or hide columns per the setting. The Message column is never hidden:
// a log table with no message in it is not a log table.
void applyColumnVisibility(QTableView *view);

// Stored column widths, where any are stored. Zero or negative widths are
// ignored — a column collapsed to nothing is almost always an accident, and
// restoring it leaves an invisible column with no obvious way back.
void applyColumnWidths(QTableView *view);

// The defaults a log table starts from, before stored widths win over them.
void applyDefaultColumnWidths(QTableView *view);

// Everything above, in the order a freshly built view wants it: shape, then
// defaults, then what the operator has stored.
void configure(QTableView *view);

}  // namespace LogTableView

#endif  // LOGTABLEVIEW_H
