#ifndef LOGMODEL_H
#define LOGMODEL_H

// =============================================================================
//  LogModel
//  -----------------------------------------------------------------------------
//  Per-tab table model. Replaces the section-1 LogListModel, which stored
//  rows as HTML-tagged QStrings. After this refactor a row IS a LogEntry,
//  and the model presents it as columns:
//
//      0  Time      (formatted from epochMs)
//      1  Source    (raw "21_1" form — useful when the friendly name is
//                    ambiguous or the user wants to grep by routing key)
//      2  Friendly  (looked up from NameMap; falls back to raw)
//      3  Direction ("IN" / "OUT" / "")
//      4  Severity  (icon-only column; foreground/background drawn from data)
//      5  Message   (the text payload, plain)
//
//  Coloring is delivered through Qt::ForegroundRole and Qt::BackgroundRole
//  on the message column. The delegate doesn't need to parse HTML; it just
//  reads the role and applies the color. Filtering and sorting are then
//  free via QSortFilterProxyModel (used in Patch B item 2c).
//
//  Memory model:
//    – Underlying storage is std::deque<LogEntryPtr>. Same O(1)
//      push_back/pop_front as the section-1 model, but now the deque holds
//      shared pointers, so the QByteArray rawBytes inside isn't copied
//      when SaveData snapshots the entries on a worker thread.
//    – Capacity / batched-trim behaviour is preserved unchanged from
//      section 1d.
// =============================================================================

#include <QAbstractTableModel>
#include <QSet>
#include <QDateTime>
#include <QString>
#include <QVector>
#include <deque>

#include "logentry.h"
#include "theme.h"

class NameMap;

class LogModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    // Rows the find bar is currently matching, so EVERY hit is visible at
    // once rather than only the one the cursor happens to be on. Held as
    // entry pointers, not row numbers: the model evicts from the front once
    // a tab is full, and a remembered row number silently becomes a
    // different message a few seconds later.
    void setFindHits(const QSet<const LogEntry *> &hits);
    void clearFindHits();

    // Column indices. Public so views, delegates, and proxies can refer to
    // them by name rather than magic numbers.
    enum Column {
        ColTime      = 0,
        ColSource    = 1,
        ColFriendly  = 2,
        ColDirection = 3,
        ColSeverity  = 4,
        ColMessage   = 5,
        ColumnCount
    };

    explicit LogModel(QObject *parent      = nullptr,
                      int      capacity    = 200'000,
                      double   trimFraction = 0.10);

    // Borrowed reference for friendly-name lookups. Optional; the column
    // falls back to the raw key when null.
    void setNameMap(const NameMap *names) { m_names = names; }

    // Set the active theme. Causes a dataChanged signal across all rows
    // (Foreground + Background roles) so visible cells re-render with
    // the right palette. Cheap — only the visible region actually
    // repaints. Default is light.
    void setTheme(Theme t);

    // Render the Time column in UTC rather than local time. Invalidates
    // every entry's cached time string, because that cache was built in
    // the previous zone.
    void setShowUtc(bool utc);
    bool showUtc() const { return m_showUtc; }
    Theme theme() const { return m_theme; }

    // The single mutator. Takes ownership of the shared pointer (cheap —
    // just bumps the refcount). Triggers begin/endInsertRows for one row
    // and, when the buffer is at capacity, a single batched begin/endRemoveRows
    // pair for the oldest m_trimChunk rows.
    void appendEntry(const LogEntryPtr &entry);

    // Batched insert. Used by MessageDispatcher when a burst of UDP
    // packets has been coalesced into a single tick's worth of work.
    // Single begin/endInsertRows pair for the whole batch — crucial
    // for keeping the GUI responsive under heavy load (e.g. 1000
    // packets in 50ms).
    void appendEntries(const QVector<LogEntryPtr> &batch);

    // QAbstractItemModel overrides ----------------------------------------
    int rowCount   (const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data       (const QModelIndex &index, int role) const override;
    QVariant headerData (int section, Qt::Orientation orientation,
                         int role) const override;

    // Convenience used by status bar / save.
    int  count() const { return static_cast<int>(m_entries.size()); }
    // The newest timestamp among the rows added (0 when empty), kept as rows
    // go in, so a query's last: anchor costs nothing per keystroke (session
    // 114: the filter bar used to scan every row for it). Trimming drops the
    // oldest rows, so it stays right.
    qint64 newestMs() const { return m_newestMs; }

    // Snapshot for SaveData. Returns a deep-copied list of shared pointers
    // so the saver thread can iterate without us mutating m_entries under
    // it.
    QVector<LogEntryPtr> snapshot() const;

    // Return the LogEntry for a given row (source-model row index, not a
    // proxy row). Used by the raw-bytes side panel after mapping a proxy
    // selection back to its source row. Returns null if out of range.
    LogEntryPtr entryAt(int row) const;

    // The same entry without touching the reference count.
    //
    // entryAt returns a shared pointer by value, so every call is an atomic
    // increment and decrement. That is the right default — a caller holding
    // the entry while the model mutates must own it — but a scan that only
    // reads text and drops the entry immediately pays it a hundred thousand
    // times per keystroke. Valid only for the duration of the call chain;
    // never store it.
    const LogEntry *entryPtrAt(int row) const
    {
        if (row < 0 || row >= static_cast<int>(m_entries.size())) {
            return nullptr;
        }
        return m_entries[static_cast<size_t>(row)].data();
    }

    // Cheap reset.
    void clear();

    // clear(), handing back what was removed, oldest first (session 79,
    // for undo). The caller holds the shared pointers; nothing is copied.
    QVector<LogEntryPtr> takeAll();

    // Put rows taken by takeAll() back IN FRONT of whatever has arrived
    // since, as far as capacity allows (the newest of them win). Returns how
    // many came back.
    int restoreOlder(const QVector<LogEntryPtr> &older);

    int  capacity()    const { return m_capacity; }
    void setCapacity(int newCap);

private:
    // Storage. shared_ptr-by-value into a deque is cheap (each element is
    // 16 bytes) and gives us snapshot-without-copying-bytes.
    std::deque<LogEntryPtr> m_entries;
    int m_capacity;
    int m_trimChunk;

    // Borrowed. May be null.
    const NameMap *m_names = nullptr;

    // Active theme; flipped via setTheme(). Default light.
    Theme m_theme = Theme::Light;
    QSet<const LogEntry *> m_findHits;
    qint64 m_newestMs = 0;
    bool  m_showUtc = false;

    // Helpers for data().
    QString formatTime    (qint64 epochMs)  const;
    QString directionString(Direction d)    const;
};

#endif // LOGMODEL_H
