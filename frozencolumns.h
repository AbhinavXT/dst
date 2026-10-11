#ifndef FROZENCOLUMNS_H
#define FROZENCOLUMNS_H

// =============================================================================
//  FrozenColumns (session 253) — the first N columns of a QTableView stay put
//  while the rest scroll sideways, as a spreadsheet's frozen panes.
//  -----------------------------------------------------------------------------
//  Qt's "frozen column" technique: a second QTableView over the left of the
//  table, on the same model and selection, showing only those columns and
//  scrolling with the table up and down. The table underneath still has
//  every column; the overlay covers the frozen ones. Column widths, row
//  heights, the header and the vertical scroll follow the table.
//  Double-clicking the overlay is the table's double-click.
//  Used by the NMS packet logs (the table and its pinned-rows strip).
// =============================================================================

#include <QObject>
#include <QPointer>

class QTableView;

class FrozenColumns : public QObject
{
    Q_OBJECT
public:
    explicit FrozenColumns(QTableView *table);

    // 0: nothing frozen (the overlay hidden).
    void setCount(int n);
    int  count() const { return m_count; }
    QTableView *overlay() const { return m_overlay; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void apply();          // which columns the overlay shows, their widths
    void place();          // where it sits over the table

    QPointer<QTableView> m_table;
    QTableView *m_overlay = nullptr;
    int m_count = 0;
};

#endif // FROZENCOLUMNS_H
