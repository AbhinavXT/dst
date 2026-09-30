#ifndef TABLEFINDBAR_H
#define TABLEFINDBAR_H

// =============================================================================
//  TableFindBar
//  -----------------------------------------------------------------------------
//  Find (Ctrl+F) for a Field | Value table -- the Live Loco Console's
//  per-packet tabs, where a struct like LOCO_INFO is ~170 rows long.
//
//  WHY NOT FindBar
//    FindBar searches log tables: proxy models of LogEntry rows, time
//    columns, hex over the raw datagram. A field table is none of that --
//    it is a QTableWidget rebuilt from CaptureDecoder::describe() every time
//    a new frame arrives. What it needs is simpler, and one thing FindBar
//    does not have to deal with: the rows are REPLACED several times a
//    second while the operator is reading them.
//
//  SURVIVING THE LIVE REFRESH
//    Matches are held by FIELD NAME, never by row number. After the owner
//    rebuilds the table it calls reapply(): the tint and the hidden rows are
//    put back, and the current match is found again by name even if rows
//    were added above it. Names are not unique -- LOCO_INFO has
//    speed_margin_warning at the top level AND in national_values -- so the
//    current match is (name, which occurrence of that name). reapply() never scrolls -- only Next / Previous
//    move the view -- so a 4 Hz refresh cannot yank the table away from
//    the row being read.
//
//  MATCHING
//    Case-insensitive; "_" counts as a space; every word must appear. So
//    "speed margin" finds speed_margin_eb and national_values speed_margin_nsb,
//    and "margin eb" narrows to the EB ones. Field names only by default --
//    that is what "where is X in this struct" means; "Values too" adds the
//    Value column.
//
//  OTHER TABS
//    When nothing on the current tab matches, the bar says which other tabs
//    have matches ("not here -- LINFO 3, STATUS 1"), and Enter goes to the
//    first of them. The field name is often known; the packet it lives in
//    is not.
// =============================================================================

#include <QPointer>
#include <QTableWidget>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QToolButton;

class TableFindBar : public QWidget
{
    Q_OBJECT

public:
    explicit TableFindBar(QWidget *parent = nullptr);

    // The table being searched (the current tab's), or null.
    void setTable(QTableWidget *table);
    QTableWidget *table() const { return m_table; }

    // Every table the owner has, with the name to show for it ("LINFO"),
    // for the "not here -- found in ..." hint. Includes the current one.
    void setAllTables(const QVector<QPair<QString, QTableWidget *>> &tables);

    // Call after the table's rows have been rebuilt.
    void reapply();

    // Show, focus the input with its text selected.
    void open();
    void close();
    bool isActive() const;   // visible with a non-empty query

    void findNext();
    void findPrevious();

    // ---- for the tests and the owner ------------------------------------
    void setQuery(const QString &text);
    void setValuesToo(bool on);
    void setOnlyMatching(bool on);
    int  matchCount() const { return m_matchRows.size(); }
    int  currentRow() const;                 // table row of the current match, -1 if none
    QString currentField() const { return m_currentField; }
    QString statusText() const;

    // The matching rule, exposed so it can be tested on its own.
    static QStringList termsFor(const QString &query);
    static bool matches(const QString &text, const QStringList &terms);

signals:
    // "Not here -- found in ...": the owner switches to this table's tab.
    void switchToTable(QTableWidget *table);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rescan();                            // recompute matches, restyle rows
    void restyle();                           // tints + hidden rows for m_matchRows
    void jumpTo(int matchIndex, bool scroll); // make a match current
    void refind(bool jumpIfLost);              // put the cursor back on (name, occurrence)
    int  occurrenceOf(int row) const;          // how many rows above `row` have its name
    int  countIn(QTableWidget *table) const;
    bool rowMatches(QTableWidget *table, int row) const;
    void updateStatus();

    QPointer<QTableWidget> m_table;
    QVector<QPair<QString, QPointer<QTableWidget>>> m_allTables;

    QStringList  m_terms;
    QVector<int> m_matchRows;        // table rows, in order
    int          m_cursor = -1;      // index into m_matchRows
    QString      m_currentField;     // what the cursor points at, by name...
    int          m_currentOccurrence = 0;   // ...and which row of that name
    bool         m_open = false;     // opened and not closed (independent of the window being shown)

    QLineEdit   *m_input = nullptr;
    QCheckBox   *m_valuesToo = nullptr;
    QCheckBox   *m_onlyMatching = nullptr;
    QLabel      *m_status = nullptr;
    QToolButton *m_previous = nullptr;
    QToolButton *m_next = nullptr;
};

#endif // TABLEFINDBAR_H
