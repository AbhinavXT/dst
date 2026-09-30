#ifndef NAMEMAP_H
#define NAMEMAP_H

// =============================================================================
//  NameMap
//  -----------------------------------------------------------------------------
//  Maps the wire-level (source_id, kvchId) pair to a human-friendly name like
//  "LK_1_VCC_1" for display in tabs, status bar, and saved-file names.
//
//  The internal routing key remains the raw "21_1" form — friendly names are
//  display-only. This is deliberate: an operator could rename "LK_1" to
//  "LK_PRIMARY" mid-session by editing the CSV and we don't want messages
//  routed to a different tab as a result.
//
//  CSV format (no header row required, comments allowed with #):
//
//      # source_id_kvchId, friendly_name
//      21_1, LK_1_VCC_1
//      21_2, LK_1_VCC_2
//      22_1, LK_2_VCC_1
//      8f_1, test_loopback
//
//  The source_id half can be decimal ("21") or hex ("0x21" / "8f"). Trailing
//  whitespace is trimmed. Blank lines and lines starting with '#' are
//  ignored. A malformed line is logged via qWarning() and skipped, never a
//  hard failure — operations folks should be able to typo a name without
//  breaking the entire mapping.
// =============================================================================

#include <QHash>
#include <QString>

class NameMap
{
public:
    // Does a source match a sidebar filter string?
    //
    // Matches the friendly name OR the raw key, because operators use both
    // and which one they reach for depends on whether friendly_names.csv
    // covers that source. Matching only the displayed name would make
    // unmapped sources — the ones most likely to be unfamiliar — the
    // hardest to find.
    //
    // Free-standing and static so it can be tested without a widget.
    static bool matchesFilter(const QString &friendlyName,
                              const QString &tabKey,
                              const QString &needle);

    // Load the map from a CSV file. Returns true if the file was opened
    // successfully (even if some lines were skipped). Returns false if the
    // file is missing or unreadable, in which case the map remains empty
    // and lookups will fall through to the raw "21_1" form.
    bool loadFromFile(const QString &path);

    // The primary lookup. If a friendly name is registered for this
    // (source, kvchId) pair, return it. Otherwise return the raw form
    // "%source%_%kvchId%" so callers always get a usable string.
    QString lookup(quint8 sourceId, quint16 kvchId) const;

    // Same but takes the already-formatted raw key. Convenience for callers
    // that already have it (LogEntry::tabKey()).
    QString lookupByKey(const QString &rawKey) const;

    // Number of entries in the map — useful for status bar / settings UI.
    int size() const { return m_map.size(); }

    // Reload from disk. Used by a future "reload friendly names" menu item.
    // For Patch A we just call loadFromFile() at startup.
    void clear() { m_map.clear(); }

private:
    // Keyed on the raw "21_1" string. We could key on a 32-bit composite
    // (source<<16 | kvch) but QString is the form we already build at
    // every call site, and the map is small (a handful of entries).
    QHash<QString, QString> m_map;

    // Helper: parse the source_id half, accepting decimal or hex. Returns
    // -1 on parse failure.
    static int parseSourceId(const QString &token);
};

#endif // NAMEMAP_H
