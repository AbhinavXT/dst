#ifndef BOOKMARKS_H
#define BOOKMARKS_H

// =============================================================================
//  BookmarkStore
//  -----------------------------------------------------------------------------
//  Operator-placed markers on individual log rows, with optional notes.
//
//  WHY
//    Incident work is iterative: you find something at 14:03:22, keep
//    looking, find a related thing four minutes later, and then need to get
//    back to the first one — by which point the tab has scrolled past
//    thousands of rows. Today that means writing timestamps on paper.
//
//  IDENTITY
//    A bookmark refers to (tabKey, epochMs), NOT to a LogEntry pointer.
//    Entries live in a bounded ring and are evicted; a pointer would either
//    dangle or pin memory forever. The timestamp pair also survives a
//    restart and still resolves against a reloaded .dlr session, which a
//    pointer never could.
//
//    The consequence is that a bookmark can outlive the row it refers to.
//    That is treated as normal, not as an error: the panel still lists it
//    with its note and timestamp, and jumping to it lands on the nearest
//    surviving row. A marker whose row has aged out is still the most
//    useful record of "something happened here" that we have.
//
//    Sub-millisecond bursts can share a timestamp, so (tabKey, epochMs) is
//    not strictly unique. In practice a bookmark identifies a moment rather
//    than a specific row, which is what an operator means anyway.
//
//  PERSISTENCE
//    JSON next to the settings file. Written on every mutation rather than
//    at shutdown, because the shutdown path is exactly the one that does
//    not run when the interesting thing (a crash, a kill) happens.
// =============================================================================

#include <QObject>
#include <QString>
#include <QVector>

class LogModel;

struct Bookmark {
    QString tabKey;
    qint64  epochMs   = 0;
    QString note;
    QString messageSnippet;   // copy of the row text, so the panel still
                              // shows something once the entry is evicted
    qint64  createdMs = 0;
};

class BookmarkStore : public QObject
{
    Q_OBJECT

public:
    explicit BookmarkStore(QObject *parent = nullptr);

    // Path defaults to bookmarks.json beside the app's INI.
    bool load(const QString &path = QString());
    bool save() const;

    // Returns true if a bookmark was ADDED, false if an existing one at the
    // same (tabKey, epochMs) was removed — the caller uses this to update
    // the entry's paint flag either way.
    bool toggle(const QString &tabKey, qint64 epochMs,
                const QString &messageSnippet);

    void remove(int index);
    void setNote(int index, const QString &note);
    void clear();

    // The whole set at once: undo of Remove / Remove all (session 79).
    void replaceAll(const QVector<Bookmark> &marks);

    bool has(const QString &tabKey, qint64 epochMs) const;

    const QVector<Bookmark> &all() const { return m_marks; }
    int  count() const { return m_marks.size(); }

    // Re-apply the `bookmarked` paint flag to every entry in a model whose
    // timestamp matches a stored bookmark. Needed after loading a recorded
    // session, and at startup for any tab that reloads history — the flag
    // lives on LogEntry for cheap painting, but the truth lives here.
    void applyToModel(LogModel *model, const QString &tabKey) const;

signals:
    void changed();

private:
    int indexOf(const QString &tabKey, qint64 epochMs) const;

    QVector<Bookmark> m_marks;
    QString           m_path;
};

#endif // BOOKMARKS_H
