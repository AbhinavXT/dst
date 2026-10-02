#ifndef SOURCEROWDELEGATE_H
#define SOURCEROWDELEGATE_H

// =============================================================================
//  SourceRowDelegate (session 119, UI revamp step 3)
//  -----------------------------------------------------------------------------
//  How a row of the Sources panel is DRAWN: a health dot, the source's name,
//  and a line under it saying what the name does not ("source 81 · channel
//  2 · 12/s"); the counters in the mono face. The tree, its items, columns,
//  sorting and activation are unchanged — only the painting is new.
//
//  The dot is not colour alone: its shape changes too (filled when live,
//  a ring when late, a ring with a slash when offline, hollow when never
//  heard), and the meta line says it in words when the source is late.
// =============================================================================

#include <QStyledItemDelegate>

class SourceRowDelegate : public QStyledItemDelegate
{
public:
    // Item data on column 0.
    static constexpr int HealthRole = Qt::UserRole + 10;   // int, Health below
    static constexpr int MetaRole   = Qt::UserRole + 11;   // QString, the second line

    enum Health { Unknown = 0, Live = 1, Late = 2, Offline = 3 };

    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    static QColor healthColor(int health);
};

#endif // SOURCEROWDELEGATE_H
