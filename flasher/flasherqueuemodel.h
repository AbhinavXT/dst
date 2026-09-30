#ifndef FLASHERQUEUEMODEL_H
#define FLASHERQUEUEMODEL_H
// =============================================================================
//  flasherqueuemodel.{h,cpp} -- the Flash queue table on the Queue page.
//
//  One row per card type, always all four, each holding that card's image.
//  Exactly one row (or none) is SELECTED for flashing, radio-button style:
//  the updater only runs for 5 s after a power cycle and leaves for the
//  application once a card is done, so every card needs its own power cycle
//  and the flasher runs one card per Flash. The other rows keep their images
//  so the next card is one click away. The "remove" column clears a row's
//  image rather than removing the row.
//
//  A file dragged in from Explorer and dropped on a row becomes that row's
//  image. Rows are not reordered: with one card per run, order means nothing.
//
//  Columns follow Main.dc.html:
//    0  (unused spacer; kept so column numbers match the design)
//    1  radio + card name / ID
//    2  file name + directory       (click to browse)
//    3  size + block count
//    4  CRC-32
//    5  image-check badge
//    6  remove (clears the image)
// =============================================================================
#include <QAbstractTableModel>
#include <QStyledItemDelegate>
#include <QVector>

#include "flashercore.h"

struct FlasherQueueRow {
    int               cardType = 0;
    bool              ticked = false;
    Flasher::ImageInfo image;           // image.path empty = no image chosen
    bool              shaPinMismatch = false;

    bool hasImage() const { return !image.path.isEmpty(); }
    Flasher::NameCheckResult nameCheck() const;
};

class FlasherQueueModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column {
        ColumnHandle = 0,
        ColumnCard,
        ColumnImage,
        ColumnSize,
        ColumnCrc,
        ColumnCheck,
        ColumnRemove,
        ColumnCount
    };

    // Extra data roles the delegate reads. DisplayRole is the first line of a
    // cell; SecondaryTextRole the muted second line.
    enum Role {
        SecondaryTextRole = Qt::UserRole + 1,
        BadgeKindRole,          // int: 0 ok, 1 attention, 2 off
        MonospaceRole,          // bool: draw the first line in the mono face
        MissingImageRole        // bool: first line is the "Choose .appimage…" prompt
    };

    explicit FlasherQueueModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;

    // Drops: files from Explorer onto a row.
    Qt::DropActions supportedDropActions() const override;
    QStringList mimeTypes() const override;
    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                         const QModelIndex &parent) const override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override;

    // ---- the window's API ----------------------------------------------
    const QVector<FlasherQueueRow> &rows() const { return m_rows; }
    const FlasherQueueRow &row(int index) const { return m_rows[index]; }
    int  rowForCard(int cardType) const;

    void setImage(int rowIndex, const Flasher::ImageInfo &image);
    void clearImage(int rowIndex);
    // Selecting a row deselects every other row.
    void setTicked(int rowIndex, bool ticked);
    int  tickedRow() const;       // -1 when no card is selected
    void setShaPinMismatch(int rowIndex, bool mismatch);

signals:
    // Anything that could change the pre-flight verdict: the selection or an
    // image. The page re-evaluates the checklist on this.
    void queueChanged();

    // A file was dropped onto a row from outside; the page loads and hashes it
    // (the model does no file I/O itself).
    void fileDropped(int rowIndex, const QString &path);

private:
    QVector<FlasherQueueRow> m_rows;
};

// Paints the two-line cells, the badge and the dimmed unticked rows.
class FlasherQueueDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit FlasherQueueDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};

#endif  // FLASHERQUEUEMODEL_H
