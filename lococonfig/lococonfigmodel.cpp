#include "lococonfigmodel.h"

#include "flasherstyle.h"
#include "uicolors.h"
#include "uistyle.h"

#include <QFont>

// =============================================================================
//  LocoFieldModel
// =============================================================================

LocoFieldModel::LocoFieldModel(const LocoInfo::Layout *layout,
                               const LocoInfo::Presentation *presentation, QObject *parent)
    : QAbstractTableModel(parent)
    , m_layout(layout)
    , m_presentation(presentation)
{
    for (int index = 0; index < m_layout->fields().size(); ++index) {
        if (!m_layout->fields().at(index).isCrc) {
            m_rows.append(index);
        }
    }
}

void LocoFieldModel::setBaseFont(const QFont &font)
{
    m_baseFont = font;
    if (!m_rows.isEmpty()) {
        emit dataChanged(index(0, 0), index(m_rows.size() - 1, ColumnCount - 1), { Qt::FontRole });
    }
}

void LocoFieldModel::setConfig(const LocoInfo::Values &values, const LocoInfo::Values &defaults,
                               const LocoInfo::Values &lastSent)
{
    beginResetModel();
    m_values = values;
    m_defaults = defaults;
    m_lastSent = lastSent;
    endResetModel();
}

void LocoFieldModel::setLocked(const QStringList &keys)
{
    const QSet<QString> next(keys.begin(), keys.end());
    if (next == m_locked) {
        return;
    }
    m_locked = next;
    if (!m_rows.isEmpty()) {
        emit dataChanged(index(0, 0), index(m_rows.size() - 1, ColumnCount - 1));
    }
}

void LocoFieldModel::setValue(const QString &key, const QVariant &value)
{
    m_values.insert(key, value);
    const int row = rowForKey(key);
    if (row >= 0) {
        emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
    }
}

const LocoInfo::Field &LocoFieldModel::fieldAt(int row) const
{
    return m_layout->fields().at(m_rows.at(row));
}

int LocoFieldModel::rowForKey(const QString &key) const
{
    const int layoutIndex = m_layout->indexOf(key);
    return m_rows.indexOf(layoutIndex);
}

int LocoFieldModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_rows.size();
}

int LocoFieldModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return ColumnCount + m_live.size();
}

QVariant LocoFieldModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    // Session 322: a VCC column says what it is in its tooltip; the header
    // is the IP alone, so four of them fit side by side.
    if (orientation == Qt::Horizontal && role == Qt::ToolTipRole && section >= ColumnCount
        && section < ColumnCount + m_live.size()) {
        return tr("VCC %1: what it holds, from its latest @linfo").arg(m_live.at(section - ColumnCount).ip);
    }
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QVariant();
    }
    switch (section) {
    case ColumnField:    return tr("Field");
    case ColumnValue:    return tr("Value");
    case ColumnDefault:  return tr("Default");
    case ColumnLastSent: return tr("Last sent");
    case ColumnType:     return tr("Type");
    default:
        if (section >= ColumnCount && section < ColumnCount + m_live.size()) {
            return m_live.at(section - ColumnCount).ip;   // session 320; the IP alone since 322
        }
        return QVariant();
    }
}

QVariant LocoFieldModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) {
        return QVariant();
    }
    const LocoInfo::Field &field = fieldAt(index.row());
    const QString format = m_presentation->formats.value(field.key);
    const QVariant current = m_values.value(field.key);
    const bool changedFromDefault = !LocoInfo::sameValue(field, current, m_defaults.value(field.key));
    bool changedSinceSent = false;
    if (!m_lastSent.isEmpty()) {
        changedSinceSent = !LocoInfo::sameValue(field, current, m_lastSent.value(field.key));
    }

    switch (role) {
    case KeyRole:                return field.key;
    case GroupRole:              return m_presentation->groupOf(field.key);
    case ChangedFromDefaultRole: return changedFromDefault;
    case ChangedSinceSentRole:   return changedSinceSent;
    case SearchTextRole:
        return field.key + QLatin1Char(' ') + m_presentation->notes.value(field.key);
    case LockedRole:             return m_locked.contains(field.key);
    case DiffersFromLiveRole:
        for (int i = 0; i < m_live.size(); ++i)
            if (differsOnLive(i, field)) return true;
        return false;
    default:
        break;
    }
    const bool locked = m_locked.contains(field.key);

    const int column = index.column();
    // Session 320: a VCC's column. Highlighted where it holds another value.
    if (column >= ColumnCount) {
        const int i = column - ColumnCount;
        if (i >= m_live.size()) return QVariant();
        const LiveColumn &live = m_live.at(i);
        const bool differs = differsOnLive(i, field);
        switch (role) {
        case Qt::DisplayRole:
            return live.seen ? LocoInfo::formatValue(field, live.values.value(field.key), format) : QStringLiteral("—");
        case Qt::ToolTipRole:
            if (!live.seen) return tr("No @linfo from %1 yet: the VCC prints its configuration every so often").arg(live.ip);
            return differs ? tr("%1 holds %2; this configuration has %3")
                                 .arg(live.ip, LocoInfo::formatValue(field, live.values.value(field.key), format),
                                      LocoInfo::formatValue(field, current, format))
                           : tr("%1 holds the same value").arg(live.ip);
        case Qt::BackgroundRole:
            return differs ? QVariant(UiColor::bannerBg()) : QVariant();
        case Qt::ForegroundRole:
            return differs ? UiColor::bannerFg() : FlasherStyle::muted();
        case Qt::FontRole:
            if (differs) {
                QFont font = m_baseFont;
                font.setWeight(QFont::Bold);
                return font;
            }
            return QVariant();
        default:
            return QVariant();
        }
    }
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        if (column == ColumnField) {
            QString label = field.name;
            if (!field.section.isEmpty()) {
                label = field.section + QStringLiteral(" › ") + field.name;
            }
            if (changedSinceSent && role == Qt::DisplayRole) {
                return QStringLiteral("● ") + label;
            }
            return label;
        }
        if (column == ColumnValue) {
            return LocoInfo::formatValue(field, current, format);
        }
        if (column == ColumnDefault) {
            return LocoInfo::formatValue(field, m_defaults.value(field.key), format);
        }
        if (column == ColumnLastSent) {
            if (m_lastSent.isEmpty()) {
                return QStringLiteral("—");
            }
            return LocoInfo::formatValue(field, m_lastSent.value(field.key), format);
        }
        if (column == ColumnType) {
            if (format == QLatin1String("ipv4")) {
                return field.typeText() + QStringLiteral(" · IPv4");
            }
            return field.typeText();
        }
    }

    if (role == Qt::ToolTipRole) {
        QString tip = QStringLiteral("%1 · %2 · byte %3").arg(field.key, field.typeText()).arg(field.offset);
        const QString note = m_presentation->notes.value(field.key);
        if (!note.isEmpty()) {
            tip += QLatin1Char('\n') + note;
        }
        if (changedSinceSent) {
            tip += QLatin1Char('\n') + tr("Changed since the last send from this configuration");
        }
        if (locked) {
            tip += QLatin1Char('\n') + tr("Locked: right-click \u25B8 Unlock field to change it");
        }
        return tip;
    }

    if (role == Qt::DecorationRole && column == ColumnField && locked) {
        // Drawn in the current theme's colour each time (a cached icon would
        // keep the colour of the theme it was drawn in).
        return UiIcons::icon(QStringLiteral("lock"), FlasherStyle::muted(), 14);
    }

    // Session 279: changed from the default reads at a glance: the field and
    // its value bold (in the table's own font), the value in the accent.
    if (role == Qt::FontRole && changedFromDefault
        && (column == ColumnField || column == ColumnValue)) {
        QFont font = m_baseFont;
        font.setWeight(QFont::Bold);
        return font;
    }

    if (role == Qt::ForegroundRole) {
        if (column == ColumnValue && changedFromDefault) {
            return FlasherStyle::primary();
        }
        if (column == ColumnField && changedSinceSent) {
            return FlasherStyle::primary();
        }
        if (column == ColumnDefault || column == ColumnLastSent || column == ColumnType) {
            return FlasherStyle::muted();
        }
    }
    return QVariant();
}

bool LocoFieldModel::differsOnLive(int i, const LocoInfo::Field &field) const
{
    const LiveColumn &live = m_live.at(i);
    return live.seen && !LocoInfo::sameValue(field, m_values.value(field.key), live.values.value(field.key));
}

int LocoFieldModel::liveDifferences(int i) const
{
    int n = 0;
    if (i < 0 || i >= m_live.size()) return 0;
    for (int row = 0; row < m_rows.size(); ++row) n += differsOnLive(i, fieldAt(row)) ? 1 : 0;
    return n;
}

void LocoFieldModel::setLiveColumns(const QVector<LiveColumn> &columns)
{
    // Columns are inserted and removed, not the model reset: a reset would
    // throw away the table's column widths.
    QStringList before, after;
    for (const LiveColumn &c : m_live) before << c.ip;
    for (const LiveColumn &c : columns) after << c.ip;
    if (before != after) {
        if (!m_live.isEmpty()) {
            beginRemoveColumns(QModelIndex(), ColumnCount, ColumnCount + m_live.size() - 1);
            m_live.clear();
            endRemoveColumns();
        }
        if (!columns.isEmpty()) {
            beginInsertColumns(QModelIndex(), ColumnCount, ColumnCount + columns.size() - 1);
            m_live = columns;
            endInsertColumns();
        }
    } else {
        m_live = columns;
    }
    if (!m_rows.isEmpty()) {
        // The Field column's role (DiffersFromLiveRole) changes too.
        emit dataChanged(index(0, 0), index(m_rows.size() - 1, columnCount() - 1));
    }
}

Qt::ItemFlags LocoFieldModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    Qt::ItemFlags itemFlags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == ColumnValue && !m_locked.contains(fieldAt(index.row()).key)) {
        itemFlags |= Qt::ItemIsEditable;
    }
    return itemFlags;
}

bool LocoFieldModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.column() != ColumnValue || role != Qt::EditRole) {
        return false;
    }
    const LocoInfo::Field &field = fieldAt(index.row());
    if (m_locked.contains(field.key)) {
        emit editRejected(tr("%1 is locked: right-click \u25B8 Unlock field to change it").arg(field.key));
        return false;
    }
    QVariant parsed;
    QString error;
    if (!LocoInfo::parseValueText(field, value.toString(), m_presentation->formats.value(field.key),
                                  &parsed, &error)) {
        emit editRejected(error);
        return false;
    }
    if (LocoInfo::sameValue(field, parsed, m_values.value(field.key))) {
        return true;   // nothing changed; no save, no "edited"
    }
    m_values.insert(field.key, parsed);
    emit dataChanged(this->index(index.row(), 0), this->index(index.row(), ColumnCount - 1));
    emit valueEdited(field.key);
    return true;
}

// =============================================================================
//  LocoFieldFilter
// =============================================================================

LocoFieldFilter::LocoFieldFilter(QObject *parent)
    : QSortFilterProxyModel(parent)
    , m_group(allFields())
{
}

void LocoFieldFilter::setGroup(const QString &group)
{
    m_group = group;
    invalidateFilter();
}

void LocoFieldFilter::setSearch(const QString &text)
{
    m_search = text.trimmed();
    invalidateFilter();
}

void LocoFieldFilter::refresh()
{
    // Only the "changed" views depend on values; the others need no re-filter
    // (and re-filtering would reset the editor the operator is typing in).
    if (m_group == changedFromDefault() || m_group == changedSinceSent() || m_group == lockedFields()) {
        invalidateFilter();
    }
}

bool LocoFieldFilter::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    if (!m_search.isEmpty()) {
        const QString haystack = index.data(LocoFieldModel::SearchTextRole).toString();
        if (!haystack.contains(m_search, Qt::CaseInsensitive)) {
            return false;
        }
        // A search looks across every group: finding a field should not
        // depend on having picked the right group first.
        return true;
    }
    if (m_group == allFields()) {
        return true;
    }
    if (m_group == changedFromDefault()) {
        return index.data(LocoFieldModel::ChangedFromDefaultRole).toBool();
    }
    if (m_group == changedSinceSent()) {
        return index.data(LocoFieldModel::ChangedSinceSentRole).toBool();
    }
    if (m_group == lockedFields()) {
        return index.data(LocoFieldModel::LockedRole).toBool();
    }
    if (m_group == differsFromLive()) {
        return index.data(LocoFieldModel::DiffersFromLiveRole).toBool();
    }
    return index.data(LocoFieldModel::GroupRole).toString() == m_group;
}
