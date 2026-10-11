#ifndef NMSLOGWINDOW_H
#define NMSLOGWINDOW_H

// =============================================================================
//  NMS packet log window (session 247) — Tools ▸ Monitor ▸ NMS ▸
//  NMS health / fault / RSSI log…
//  -----------------------------------------------------------------------------
//  One window per packet type (NmsLog::Kind). It lists every packet of that
//  type the tab already holds, then follows the tab: rows are added as the
//  tab's model grows (live, serial, a log loaded into it), and the table is
//  rebuilt if the tab is cleared. Rows the tab trims at capacity stay here.
//  Export: CSV or .xlsx. Double-click a row: the frame in its tab.
//  Changes only (session 251): the rows where an event changed.
//  Session 253: right-click to freeze the columns up to one (they stay put
//  while the rest scroll sideways) or to pin a row: pinned rows are shown in
//  a strip above the table, whatever the table scrolls to or filters.
//  Session 254: Hide unchanging (the columns every packet so far reads the
//  same in) and Columns… (show or hide any column by hand).
//  Session 255 (fault log): Fault summary… — each fault from first listed
//  to no longer listed, how long, in how many packets.
//  Session 256: the find and filter bar (Ctrl+F): words in one column or the
//  whole row, matches tinted, Next / Previous, or only the matching rows;
//  Marked rows only. Export asks: what is shown, or everything.
// =============================================================================

#include "logmodel.h"
#include "nmslog.h"

#include <QAbstractTableModel>
#include <QSortFilterProxyModel>
#include <QPointer>
#include <QSet>
#include <QWidget>

class FrozenColumns;
class QCheckBox;
class QDialog;
class QComboBox;
class QLineEdit;
class QListWidget;
class QTimer;
class QToolButton;
class QTableWidget;
class QLabel;
class QPushButton;
class QTableView;
class StatusLine;

class NmsLogTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit NmsLogTableModel(QObject *parent = nullptr) : QAbstractTableModel(parent) {}
    const NmsLog::Table &table() const { return m_table; }
    void reset(const NmsLog::Table &t);
    // Adds packets of `kind` from `entries`; returns how many were added.
    int  append(NmsLog::Kind kind, const QVector<LogEntryPtr> &entries);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation o, int role = Qt::DisplayRole) const override;
    // Session 256: the cells to tint (the find's words; a column name, or
    // empty for every column).
    void setFind(const QStringList &words, const QString &column);

private:
    NmsLog::Table m_table;
    QStringList m_findWords;
    QString m_findColumn;
    mutable QHash<int, bool> m_spread;  // row: matches only across cells (for the tint)
};

// Session 251: which rows the view shows. Changes only: a packet whose events
// read differently from the last time each was carried (or whose CRC failed).
class NmsLogFilter : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    explicit NmsLogFilter(NmsLogTableModel *rows, QObject *parent = nullptr);
    void setChangesOnly(bool on);
    bool changesOnly() const { return m_changesOnly; }
    // Session 253: only these table rows (the pinned strip's filter).
    void setOnlyRows(const QSet<int> &rows);
    // Session 256: only the rows matching the find (words; column name or
    // empty for the whole row), and / or only the marked rows.
    void setMatching(const QStringList &words, const QString &column);
    void setMarkedOnly(bool on);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    NmsLogTableModel *m_rows;
    bool m_changesOnly = false;
    bool m_onlyListed = false;
    QSet<int> m_only;
    QStringList m_words;
    QString m_column;
    bool m_markedOnly = false;
};

class NmsLogWindow : public QWidget
{
    Q_OBJECT
public:
    NmsLogWindow(NmsLog::Kind kind, LogModel *model, const QString &tabKey, const QString &tabName,
                 QWidget *parent = nullptr);
    NmsLog::Kind kind() const { return m_kind; }
    const NmsLog::Table &table() const { return m_rows->table(); }
    QTableView *view() const { return m_view; }
    NmsLogFilter *filter() const { return m_filter; }
    // The table row (in table()) a view row shows; -1 if none.
    int tableRow(const QModelIndex &viewIndex) const;

    // Session 253: frozen columns (the first n stay put) and pinned rows.
    void freezeColumns(int n);
    int  frozenColumns() const;
    void setPinned(int tableRow, bool pinned);
    QList<int> pinnedRows() const;      // ascending
    QTableView *pinnedView() const { return m_pinned; }

    // Session 254: which columns are shown.
    void setHideUnchanging(bool on);
    void setColumnShown(const QString &name, bool shown);
    void showAllColumns();
    QStringList hiddenColumns() const;  // by name, in display order
    QDialog *columnChooser();           // opened by Columns…; built on first use
    QDialog *faultSummary();            // session 255, the fault log only; built on first use
    bool saveFaultSummaryCsv(const QString &path, QString *err = nullptr) const;

    // Session 256: the find and filter bar.
    void setFindText(const QString &text);          // applied at once (no typing delay)
    void setFindColumn(const QString &column);      // empty: all columns
    void setFilterRows(bool on);                    // only the matching rows
    void setMarkedOnly(bool on);
    int  findMatches() const { return m_matches.size(); }   // matching rows in the view
    int  findCurrent() const { return m_current; }          // the table row, -1 none
    void findNext();
    void findPrevious();
    // The export's "what is shown": the table rows, the display columns.
    QVector<int> shownRows() const;
    QVector<int> shownColumns() const;
    bool saveCsv(const QString &path, bool shownOnly, QString *err = nullptr) const;
    bool saveXlsx(const QString &path, bool shownOnly, QString *err = nullptr) const;

    // The export buttons' work, without the file dialog (and for the tests).
    bool saveCsv(const QString &path, QString *err = nullptr) const;
    bool saveXlsx(const QString &path, QString *err = nullptr) const;

public slots:
    void rebuild();

signals:
    void jumpRequested(const QString &tabKey, qint64 epochMs);

private:
    void onRowsInserted(int first, int last);
    void widenToFit(int firstRow, int lastRow);   // measures the rows, then fitColumns()
    void fitColumns();
    void exportTo(bool xlsx);
    void showCount();
    void contextMenuAt(QTableView *v, const QPoint &pos, bool header);
    void syncPinned();                  // the strip's columns, height, scroll follow the table
    void applyColumns();                // hide / show per the box and the chooser
    void fillChooser();
    void fillFaultSummary();
    void applyFind();                   // the query changed: filter, tint, count
    void recountMatches();
    void fillFindColumns();
    void showFindCount();
    void goToMatch(int step);
    int  findColumnPos() const;         // display position; -1 all; -2 a column not (yet) in the table

    NmsLog::Kind        m_kind;
    QPointer<LogModel>  m_model;
    QString             m_tabKey;
    NmsLogTableModel   *m_rows = nullptr;
    NmsLogFilter       *m_filter = nullptr;
    QCheckBox          *m_changesOnly = nullptr;
    QTableView         *m_view = nullptr;
    QTableView         *m_pinned = nullptr;
    NmsLogFilter       *m_pinFilter = nullptr;
    QSet<int>           m_pins;
    FrozenColumns      *m_frozen = nullptr;
    FrozenColumns      *m_pinFrozen = nullptr;
    QCheckBox          *m_hideUnchanging = nullptr;
    QSet<QString>       m_userHidden;   // by column name: positions move as columns arrive
    QPointer<QDialog>   m_chooser;
    QListWidget        *m_chooserList = nullptr;
    QPointer<QDialog>   m_faults;
    QTableWidget       *m_faultTable = nullptr;
    QLabel             *m_faultNote = nullptr;
    QLineEdit          *m_findEdit = nullptr;
    QComboBox          *m_findColumn = nullptr;
    QCheckBox          *m_filterRows = nullptr;
    QCheckBox          *m_markedOnly = nullptr;
    QLabel             *m_findCount = nullptr;
    QTimer             *m_findTimer = nullptr;
    QStringList         m_words;
    QVector<int>        m_matches;      // table rows that match and are in the view, in order
    int                 m_current = -1; // table row
    QVector<int>        m_need;         // by column id: the widest cell so far (session 254)
    QVector<int>        m_needChars;    // by column id: the longest text so far
    int                 m_needHex = 0, m_needHexChars = 0;   // the frame (hex) column
    int                 m_pad = 16;     // what the view adds to a text's width
    QCheckBox          *m_follow = nullptr;
    QLabel             *m_count = nullptr;
    StatusLine         *m_status = nullptr;
};

#endif // NMSLOGWINDOW_H
