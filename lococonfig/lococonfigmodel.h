#ifndef LOCOCONFIGMODEL_H
#define LOCOCONFIGMODEL_H
// =============================================================================
//  lococonfigmodel.{h,cpp} -- the LOCO_INFO field table.
//
//  One row per LOCO_INFO member (the CRC excluded: it is computed, and
//  shown in the send bar). Columns:
//
//    0  Field       name; ● when it differs from what was last sent
//    1  Value       editable; bold when it differs from the default
//    2  Default     the loco_config tool's value (loco_defaults.json)
//    3  Last sent   what went out last from this configuration
//    4  Type        u8 / u16 / u32 / f32 / char[40]
//
//  A LOCKED field (session 158) shows a lock beside its name and cannot be
//  edited here; right-click ▸ Unlock field first.
//
//  Editing goes through LocoInfo::parseValueText, so a value that cannot be
//  packed never reaches the configuration; the reason is emitted for the
//  status line instead.
// =============================================================================
#include <QAbstractTableModel>
#include <QSet>
#include <QSortFilterProxyModel>

#include "lococonfigcore.h"

class LocoFieldModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { ColumnField = 0, ColumnValue, ColumnDefault, ColumnLastSent, ColumnType, ColumnCount };
    enum Role {
        KeyRole = Qt::UserRole + 1,
        GroupRole,
        ChangedFromDefaultRole,
        ChangedSinceSentRole,
        SearchTextRole,
        LockedRole
    };

    LocoFieldModel(const LocoInfo::Layout *layout, const LocoInfo::Presentation *presentation,
                   QObject *parent = nullptr);

    // The configuration being edited. `lastSent` empty = never sent.
    void setConfig(const LocoInfo::Values &values, const LocoInfo::Values &defaults,
                   const LocoInfo::Values &lastSent);
    const LocoInfo::Values &values() const { return m_values; }
    void setValue(const QString &key, const QVariant &value);
    bool hasLastSent() const { return !m_lastSent.isEmpty(); }
    // Session 158: the locked fields (keys).
    void setLocked(const QStringList &keys);
    bool isLocked(const QString &key) const { return m_locked.contains(key); }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;

    int rowForKey(const QString &key) const;

signals:
    void valueEdited(const QString &key);
    void editRejected(const QString &reason);

private:
    const LocoInfo::Field &fieldAt(int row) const;

    const LocoInfo::Layout       *m_layout = nullptr;
    const LocoInfo::Presentation *m_presentation = nullptr;
    QVector<int>      m_rows;      // layout indices, CRC excluded
    LocoInfo::Values  m_values;
    LocoInfo::Values  m_defaults;
    LocoInfo::Values  m_lastSent;
    QSet<QString>     m_locked;
};

// Group + search filter over the model.
class LocoFieldFilter : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    // Special groups beside the presentation's titles.
    static QString allFields()      { return QStringLiteral("All fields"); }
    static QString changedFromDefault() { return QStringLiteral("Changed from defaults"); }
    static QString changedSinceSent()   { return QStringLiteral("Changed since last send"); }
    static QString lockedFields()       { return QStringLiteral("Locked fields"); }

    explicit LocoFieldFilter(QObject *parent = nullptr);
    void setGroup(const QString &group);
    void setSearch(const QString &text);
    // Re-evaluate the "changed" groups after an edit.
    void refresh();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QString m_group;
    QString m_search;
};

#endif  // LOCOCONFIGMODEL_H
