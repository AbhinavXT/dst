#include "logmodel.h"
#include "uicolors.h"
#include "namemap.h"

#include <QBrush>
#include <QColor>
#include <QDateTime>

LogModel::LogModel(QObject *parent, int capacity, double trimFraction)
    : QAbstractTableModel(parent)
    , m_capacity (capacity > 0 ? capacity : 1)
    , m_trimChunk(std::max(1, int(m_capacity * trimFraction)))
{}

void LogModel::appendEntry(const LogEntryPtr &entry)
{
    if (!entry) return;

    // Batched trim — same idea as section 1d. Drop the oldest 10% in one
    // begin/endRemoveRows pair when at capacity, instead of trimming one
    // row per insert past the cap.
    if (static_cast<int>(m_entries.size()) >= m_capacity) {
        const int dropCount = std::min(m_trimChunk,
                                       static_cast<int>(m_entries.size()));
        beginRemoveRows(QModelIndex(), 0, dropCount - 1);
        for (int i = 0; i < dropCount; ++i) {
            m_entries.pop_front();
        }
        endRemoveRows();
    }

    const int row = static_cast<int>(m_entries.size());
    beginInsertRows(QModelIndex(), row, row);
    m_entries.push_back(entry);
    m_newestMs = std::max(m_newestMs, entry->epochMs);
    endInsertRows();
}

void LogModel::appendEntries(const QVector<LogEntryPtr> &batch)
{
    // Batched insert path — single beginInsertRows / endInsertRows pair
    // for the whole batch instead of one per entry. Crucial for handling
    // arrival bursts (e.g. 1000 packets in 50ms): without batching the
    // GUI thread runs the model-update machinery 1000 separate times,
    // each one triggering layout/repaint events that saturate the event
    // loop and freeze the UI. With batching, the same burst is one
    // insert + one repaint, which the view handles in microseconds.
    if (batch.isEmpty()) return;

    // First handle trimming, the same way as appendEntry. If the
    // incoming batch alone would push us past capacity, we may need
    // to drop more than the usual 10% — drop enough so that batch
    // size + remaining ≤ capacity, with at least one trim chunk's
    // worth.
    const int existing = static_cast<int>(m_entries.size());
    const int incoming = batch.size();
    if (existing + incoming > m_capacity) {
        int dropCount = std::max(m_trimChunk,
                                 existing + incoming - m_capacity);
        dropCount = std::min(dropCount, existing);
        if (dropCount > 0) {
            beginRemoveRows(QModelIndex(), 0, dropCount - 1);
            for (int i = 0; i < dropCount; ++i) {
                m_entries.pop_front();
            }
            endRemoveRows();
        }
    }

    // If the batch alone is larger than capacity (pathological case —
    // we got 250k entries in one tick when capacity is 200k), keep
    // only the most recent capacity-worth and start from a clean model.
    QVector<LogEntryPtr> effective = batch;
    if (effective.size() > m_capacity) {
        effective = effective.mid(effective.size() - m_capacity);
        // Wipe everything we already have — the batch alone fills the
        // model.
        if (!m_entries.empty()) {
            beginRemoveRows(QModelIndex(), 0,
                            static_cast<int>(m_entries.size()) - 1);
            m_entries.clear();
            m_newestMs = 0;
            endRemoveRows();
        }
    }

    const int first = static_cast<int>(m_entries.size());
    const int last  = first + effective.size() - 1;
    beginInsertRows(QModelIndex(), first, last);
    for (const LogEntryPtr &e : effective) {
        if (!e) continue;
        m_entries.push_back(e);
        m_newestMs = std::max(m_newestMs, e->epochMs);
    }
    endInsertRows();
}

int LogModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;   // tables aren't trees
    return static_cast<int>(m_entries.size());
}

int LogModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return ColumnCount;
}

QVariant LogModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return QVariant();
    const int row = index.row();
    const int col = index.column();
    if (row < 0 || row >= static_cast<int>(m_entries.size())) return QVariant();
    if (col < 0 || col >= ColumnCount)                        return QVariant();

    const LogEntryPtr &e = m_entries[row];
    if (!e) return QVariant();

    switch (role) {
    case Qt::DisplayRole:
        switch (col) {
        case ColTime: {
            const QString t = e->cachedTime.isEmpty() ? formatTime(e->epochMs)
                                                      : e->cachedTime;
            // A leading bullet rather than a decoration icon: it survives
            // copy-paste into a report, needs no icon resources, and stays
            // legible in both themes.
            return e->bookmarked ? QStringLiteral("● ") + t : t;
        }
        case ColSource:    return e->tabKey();
        case ColFriendly:  return m_names ? m_names->lookupByKey(e->tabKey())
                                          : e->tabKey();
        case ColDirection: return directionString(e->direction);
        case ColSeverity:
            // Glyph + word, not colour alone.
            //
            // Severity was previously distinguished by red/orange/grey plus
            // a three-letter code. Red-green colour blindness affects
            // roughly one man in twelve, and red-on-grey is exactly the pair
            // it degrades; on a signalling team that is likely to be
            // somebody. A leading glyph is a redundant channel that costs
            // nothing, works in both themes, survives a greyscale printout,
            // and copy-pastes into a report as text.
            //
            // Info also gains a visible mark. It rendered as an empty
            // string, which made "classified as routine" and "no rule
            // matched this at all" look identical in the column whose whole
            // job is saying which it was.
            switch (e->severity) {
            case Severity::Info:  return QStringLiteral("· info");
            case Severity::Warn:  return QStringLiteral("▲ WARN");
            case Severity::Error: return QStringLiteral("✕ ERR");
            }
            return QString();
        case ColMessage:   return e->text;
        }
        break;

    case Qt::ForegroundRole: {
        // Priority order:
        //   1. Explicit foreground from the matched ColorRule (theme-
        //      aware: dark variant on a dark theme -- ThemeUtil::isDark()).
        //   2. Severity-derived colour (Error → red, Warn → orange), from
        //      UiColor so it follows the palette. It used to be the named
        //      colours "red" and "darkorange" for the light theme, which
        //      are 4.0:1 and 2.1:1 against a white row — on the surface of
        //      this program that is read the most, and where the whole
        //      point of the colour is to make a bad row catch the eye.
        //   3. System default (we return QVariant()).
        // Applied to both the Severity column and the Message column so
        // the row reads consistently.
        if (col == ColMessage || col == ColSeverity) {
            const QColor &fg = ThemeUtil::isDark(m_theme) ? e->fgDark
                                                         : e->fgLight;
            if (fg.isValid()) {
                return QBrush(fg);
            }
            switch (e->severity) {
            case Severity::Error: return QBrush(UiColor::error());
            case Severity::Warn:  return QBrush(UiColor::warning());
            case Severity::Info:
                return QVariant();
            }
        }
        break;
    }

    case Qt::BackgroundRole:
        // A find hit tints the whole row, and wins over the colour rule's
        // background: while a search is running, "does this row match" is
        // the question being asked of every row on screen.
        if (!m_findHits.isEmpty() && m_findHits.contains(e.data())) {
            QColor c = UiColor::accent();
            if (ThemeUtil::isDark(m_theme)) {
                c.setAlpha(70);
            } else {
                c.setAlpha(48);
            }
            return QBrush(c);
        }
        // Background from the matched rule (theme-aware). Applied to the
        // Message column only — coloring the entire row's background
        // fights the alternating-row-color stripe the table view uses,
        // so we keep the highlight scoped to where the user is reading.
        if (col == ColMessage) {
            const QColor &bg = ThemeUtil::isDark(m_theme) ? e->bgDark
                                                         : e->bgLight;
            if (bg.isValid()) return QBrush(bg);
        }
        break;

    case Qt::TextAlignmentRole:
        if (col == ColTime || col == ColSeverity || col == ColDirection) {
            return int(Qt::AlignCenter);
        }
        return int(Qt::AlignLeft | Qt::AlignVCenter);

    case Qt::ToolTipRole:
        // Hovering a row shows the raw (source_id, kvchId, msg_id) header
        // tuple — useful when the friendly name hides what's underneath.
        return QString("source=%1  kvch=%2  msg_id=%3  len=%4")
                   .arg(e->header.source_id)
                   .arg(e->header.kvchId)
                   .arg(e->header.message_id)
                   .arg(e->header.message_len);

    default:
        break;
    }
    return QVariant();
}

QVariant LogModel::headerData(int section, Qt::Orientation orientation,
                              int role) const
{
    if (role != Qt::DisplayRole) return QVariant();
    if (orientation == Qt::Vertical) {
        return section + 1;   // 1-based row numbers; cheap orientation aid
    }
    switch (section) {
    // The zone belongs in the header. A bare "Time" is what let screenshots
    // and log files silently disagree about which hour something happened.
    case ColTime:      return m_showUtc ? QStringLiteral("Time (UTC)")
                                        : QStringLiteral("Time (local)");
    case ColSource:    return QStringLiteral("Source");
    case ColFriendly:  return QStringLiteral("Name");
    case ColDirection: return QStringLiteral("Dir");
    case ColSeverity:  return QStringLiteral("Sev");
    case ColMessage:   return QStringLiteral("Message");
    }
    return QVariant();
}

QVector<LogEntryPtr> LogModel::snapshot() const
{
    QVector<LogEntryPtr> out;
    out.reserve(static_cast<int>(m_entries.size()));
    for (const auto &e : m_entries) {
        out.append(e);
    }
    return out;
}

LogEntryPtr LogModel::entryAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_entries.size())) {
        return LogEntryPtr();
    }
    return m_entries[static_cast<size_t>(row)];
}

void LogModel::clear()
{
    if (m_entries.empty()) return;
    beginResetModel();
    m_entries.clear();
    m_newestMs = 0;
    endResetModel();
}

QVector<LogEntryPtr> LogModel::takeAll()
{
    QVector<LogEntryPtr> taken;
    if (m_entries.empty()) {
        return taken;
    }
    taken.reserve(static_cast<int>(m_entries.size()));
    for (const LogEntryPtr &e : m_entries) {
        taken.append(e);
    }
    beginResetModel();
    m_entries.clear();
    m_newestMs = 0;
    m_findHits.clear();
    endResetModel();
    return taken;
}

int LogModel::restoreOlder(const QVector<LogEntryPtr> &older)
{
    // Room left under capacity. Rows that arrived since the clear stay: they
    // are newer than anything restored and the operator may be watching them.
    const int room = std::max(0, m_capacity - static_cast<int>(m_entries.size()));
    const int keep = std::min(room, older.size());
    if (keep <= 0) {
        return 0;
    }
    // The NEWEST `keep` of the old rows: if they cannot all come back, the
    // ones nearest the present are the ones worth having.
    const int from = older.size() - keep;
    beginInsertRows(QModelIndex(), 0, keep - 1);
    for (int i = older.size() - 1; i >= from; --i) {
        m_entries.push_front(older.at(i));
        if (older.at(i)) m_newestMs = std::max(m_newestMs, older.at(i)->epochMs);
    }
    endInsertRows();
    return keep;
}

void LogModel::setFindHits(const QSet<const LogEntry *> &hits)
{
    if (hits == m_findHits) { return; }
    m_findHits = hits;
    if (rowCount() > 0) {
        emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1),
                         { Qt::BackgroundRole });
    }
}

void LogModel::clearFindHits()
{
    setFindHits({});
}

void LogModel::setTheme(Theme t)
{
    if (m_theme == t) return;
    m_theme = t;
    // Notify the view that everything in the message + severity columns
    // needs repainting. We don't need to re-emit dataChanged for every
    // role; the view's delegate calls data() lazily for whatever roles
    // it needs at paint time, and our role-handlers above already pick
    // by m_theme.
    if (!m_entries.empty()) {
        const QModelIndex topLeft     = index(0, ColSeverity);
        const QModelIndex bottomRight = index(int(m_entries.size()) - 1,
                                              ColMessage);
        emit dataChanged(topLeft, bottomRight,
                         {Qt::ForegroundRole, Qt::BackgroundRole});
    }
}

void LogModel::setCapacity(int newCap)
{
    if (newCap <= 0) return;
    m_capacity  = newCap;
    m_trimChunk = std::max(1, int(m_capacity * 0.10));
    if (static_cast<int>(m_entries.size()) > m_capacity) {
        const int over = static_cast<int>(m_entries.size()) - m_capacity;
        beginRemoveRows(QModelIndex(), 0, over - 1);
        for (int i = 0; i < over; ++i) {
            m_entries.pop_front();
        }
        endRemoveRows();
    }
}

void LogModel::setShowUtc(bool utc)
{
    if (m_showUtc == utc) return;
    m_showUtc = utc;

    // Drop every cached string: they were rendered in the other zone.
    // Clearing rather than re-rendering keeps this O(n) with no date
    // formatting — data() rebuilds each one lazily as it is painted, and
    // only the visible rows are ever painted.
    for (int i = 0; i < count(); ++i) {
        LogEntryPtr e = entryAt(i);
        if (e) e->cachedTime.clear();
    }
    if (count() > 0) {
        emit dataChanged(index(0, ColTime), index(count() - 1, ColTime),
                         { Qt::DisplayRole });
    }
    emit headerDataChanged(Qt::Horizontal, ColTime, ColTime);
}

QString LogModel::formatTime(qint64 epochMs) const
{
    // HH:mm:ss.zzz — the "what time today did this arrive" question is the
    // common one. Date isn't shown in the column because it's almost always
    // today; a future settings flag can switch to full ISO 8601.
    return (m_showUtc ? QDateTime::fromMSecsSinceEpoch(epochMs, Qt::UTC)
                      : QDateTime::fromMSecsSinceEpoch(epochMs))
               .toString("HH:mm:ss.zzz");
}

QString LogModel::directionString(Direction d) const
{
    switch (d) {
    case Direction::In:  return QStringLiteral("IN");
    case Direction::Out: return QStringLiteral("OUT");
    case Direction::None: return QString();
    }
    return QString();
}
