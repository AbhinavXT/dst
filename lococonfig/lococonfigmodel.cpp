#include "lococonfigmodel.h"

#include "flasherstyle.h"
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
    return ColumnCount;
}

QVariant LocoFieldModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QVariant();
    }
    switch (section) {
    case ColumnField:    return tr("Field");
    case ColumnValue:    return tr("Value");
    case ColumnDefault:  return tr("Default");
    case ColumnLastSent: return tr("Last sent");
    case ColumnType:     return tr("Type");
    default:             return QVariant();
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
    default:
        break;
    }
    const bool locked = m_locked.contains(field.key);

    const int column = index.column();
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

    if (role == Qt::FontRole && column == ColumnValue && changedFromDefault) {
        QFont font;
        font.setBold(true);
        return font;
    }

    if (role == Qt::ForegroundRole) {
        if (column == ColumnField && changedSinceSent) {
            return FlasherStyle::primary();
        }
        if (column == ColumnDefault || column == ColumnLastSent || column == ColumnType) {
            return FlasherStyle::muted();
        }
    }
    return QVariant();
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
    return index.data(LocoFieldModel::GroupRole).toString() == m_group;
}
