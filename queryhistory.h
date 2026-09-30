#ifndef QUERYHISTORY_H
#define QUERYHISTORY_H

// =============================================================================
//  QueryHistory
//  -----------------------------------------------------------------------------
//  Recently used queries, and named presets, shared by every query box.
//
//  WHY
//    Completion tells you what a field is called. It does not help with the
//    query you spent two minutes assembling ten minutes ago and now want
//    again — and during an incident that is the common case. The same
//    expression gets retyped repeatedly because there is nowhere for it to
//    live.
//
//    Presets cover the other half: the handful of queries a site uses every
//    shift ("link errors", "this loco only"). Those are worth naming and
//    keeping across restarts, which history deliberately is not — history
//    is a scratchpad, presets are a decision.
//
//  ONE STORE, NOT ONE PER BOX
//    A query typed into the filter bar is just as useful in cross-tab or
//    archive search; the three are the same language against different
//    scopes. Keeping separate histories would mean the box that happened to
//    be focused determined whether your last query still existed.
//
//  DEDUPLICATION IS MOST-RECENT-WINS
//    Re-running a query moves it to the top rather than adding a second
//    copy, so the list stays a set ordered by recency rather than a
//    transcript.
// =============================================================================

#include <QString>
#include <QStringList>
#include <QVector>

struct QueryPreset {
    QString name;
    QString query;
};

class QueryHistory
{
public:
    // Recently used, newest first. Capped.
    static QStringList recent();
    // Records `query` as most recent. Blank and whitespace-only queries are
    // ignored: they are what an empty box looks like, not something anyone
    // wants to recall.
    static void remember(const QString &query);
    static void clearRecent();

    // Named presets, in insertion order. Saving an existing name replaces
    // it rather than creating a duplicate.
    static QVector<QueryPreset> presets();
    static void savePreset(const QString &name, const QString &query);
    static void removePreset(const QString &name);

    static constexpr int kMaxRecent = 25;
};

#endif // QUERYHISTORY_H
