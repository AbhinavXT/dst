#include "flasherqueuemodel.h"

#include "flasherstyle.h"
#include "uicolors.h"
#include "uistyle.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QUrl>

// =============================================================================
//  FlasherQueueRow
// =============================================================================

Flasher::NameCheckResult FlasherQueueRow::nameCheck() const
{
    return Flasher::checkImageName(QFileInfo(image.path).fileName(), cardType);
}

// =============================================================================
//  FlasherQueueModel
// =============================================================================

FlasherQueueModel::FlasherQueueModel(QObject *parent)
    : QAbstractTableModel(parent)
{
    // All four card types, in the default order, none selected, all empty.
    // Selecting happens on the page: browsing or dropping an image onto a
    // row selects it; loading a whole folder or a profile does not.
    for (int cardType : Flasher::defaultCardOrder()) {
        FlasherQueueRow row;
        row.cardType = cardType;
        m_rows.append(row);
    }
}

int FlasherQueueModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_rows.size();
}

int FlasherQueueModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return ColumnCount;
}

QVariant FlasherQueueModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QVariant();
    }
    switch (section) {
    case ColumnCard:  return tr("Card");
    case ColumnImage: return tr("Image");
    case ColumnSize:  return tr("Size");
    case ColumnCrc:   return tr("CRC-32");
    case ColumnCheck: return tr("Image check");
    default:          return QString();
    }
}

QVariant FlasherQueueModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return QVariant();
    }
    const FlasherQueueRow &row = m_rows[index.row()];
    const int column = index.column();

    if (column == ColumnCard) {
        if (role == Qt::CheckStateRole) {
            if (row.ticked) {
                return Qt::Checked;
            }
            return Qt::Unchecked;
        }
        if (role == Qt::DisplayRole) {
            return Flasher::cardName(row.cardType);
        }
        if (role == SecondaryTextRole) {
            return Flasher::cardIdText(row.cardType);
        }
    }

    if (column == ColumnImage) {
        if (role == Qt::DisplayRole) {
            if (!row.ticked && !row.hasImage()) {
                return tr("No image");
            }
            if (!row.hasImage()) {
                return tr("Choose .appimage…");
            }
            return QFileInfo(row.image.path).fileName();
        }
        if (role == SecondaryTextRole) {
            if (!row.ticked && !row.hasImage()) {
                return tr("Drop a file here or click to browse");
            }
            if (!row.hasImage()) {
                return tr("Drop a file here or click to browse");
            }
            if (!row.image.loaded) {
                return row.image.error;
            }
            return QDir::toNativeSeparators(QFileInfo(row.image.path).absolutePath());
        }
        if (role == MonospaceRole) {
            return row.hasImage();
        }
        if (role == MissingImageRole) {
            return row.ticked && !row.hasImage();
        }
        if (role == Qt::ToolTipRole) {
            if (row.hasImage()) {
                return QDir::toNativeSeparators(row.image.path);
            }
            return tr("Click to choose an image, or drop one on this row");
        }
    }

    if (column == ColumnSize) {
        if (role == Qt::DisplayRole) {
            if (row.image.loaded) {
                return Flasher::formatBytes(static_cast<quint64>(row.image.sizeBytes));
            }
            return QStringLiteral("—");
        }
        if (role == SecondaryTextRole) {
            if (row.image.loaded) {
                return tr("%1 blocks").arg(row.image.blockCount);
            }
            return QString();
        }
    }

    if (column == ColumnCrc) {
        if (role == Qt::DisplayRole) {
            if (row.image.loaded) {
                return row.image.crcText();
            }
            return QStringLiteral("—");
        }
        if (role == MonospaceRole) {
            return row.image.loaded;
        }
    }

    if (column == ColumnCheck) {
        const Flasher::NameCheckResult check = row.nameCheck();
        if (role == Qt::DisplayRole) {
            if (!row.ticked && !row.hasImage()) {
                return QString();
            }
            if (row.hasImage() && !row.image.loaded) {
                return tr("Unreadable");
            }
            return check.badgeText;
        }
        if (role == BadgeKindRole) {
            if (!row.ticked && !row.hasImage()) {
                return 2;
            }
            if (row.image.loaded && check.kind == Flasher::NameCheck::Matches) {
                return 0;
            }
            return 1;
        }
        if (role == Qt::ToolTipRole) {
            return check.explanation;
        }
    }

    if (column == ColumnRemove) {
        if (role == Qt::DisplayRole && row.hasImage()) {
            return QStringLiteral("✕");
        }
        if (role == Qt::ToolTipRole && row.hasImage()) {
            return tr("Clear this card's image");
        }
    }


    return QVariant();
}

Qt::ItemFlags FlasherQueueModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        // Dropping between rows (onto the view's empty space) is allowed so a
        // row can be dragged to the very end.
        return Qt::ItemIsDropEnabled;
    }
    // Drop-enabled for files from Explorer; not drag-enabled, because rows
    // are no longer reordered.
    Qt::ItemFlags itemFlags = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled;
    if (index.column() == ColumnCard) {
        itemFlags |= Qt::ItemIsUserCheckable;
    }
    return itemFlags;
}

bool FlasherQueueModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.column() != ColumnCard || role != Qt::CheckStateRole) {
        return false;
    }
    const bool ticked = (value.toInt() == Qt::Checked);
    setTicked(index.row(), ticked);
    return true;
}

Qt::DropActions FlasherQueueModel::supportedDropActions() const
{
    return Qt::CopyAction;
}

QStringList FlasherQueueModel::mimeTypes() const
{
    // Files from Explorer only. Rows are not dragged (see the header).
    return QStringList{ QStringLiteral("text/uri-list") };
}

bool FlasherQueueModel::canDropMimeData(const QMimeData *data, Qt::DropAction, int, int,
                                        const QModelIndex &) const
{
    return data != nullptr && data->hasUrls();
}

bool FlasherQueueModel::dropMimeData(const QMimeData *data, Qt::DropAction, int row, int,
                                     const QModelIndex &parent)
{
    if (data == nullptr || !data->hasUrls()) {
        return false;
    }
    // Qt reports a drop ON an item as parent = that item and row = -1.
    int targetRow = row;
    if (targetRow < 0 && parent.isValid()) {
        targetRow = parent.row();
    }
    if (targetRow < 0 || targetRow >= m_rows.size()) {
        return false;
    }
    for (const QUrl &url : data->urls()) {
        if (url.isLocalFile()) {
            emit fileDropped(targetRow, url.toLocalFile());
            return true;   // one file per row; extras are ignored
        }
    }
    return false;
}

int FlasherQueueModel::rowForCard(int cardType) const
{
    for (int index = 0; index < m_rows.size(); ++index) {
        if (m_rows[index].cardType == cardType) {
            return index;
        }
    }
    return -1;
}

void FlasherQueueModel::setImage(int rowIndex, const Flasher::ImageInfo &image)
{
    if (rowIndex < 0 || rowIndex >= m_rows.size()) {
        return;
    }
    // Loading an image never changes which card is selected; the page
    // decides that (an explicit browse or drop selects the row).
    m_rows[rowIndex].image = image;
    emit dataChanged(index(rowIndex, 0), index(rowIndex, ColumnCount - 1));
    emit queueChanged();
}

void FlasherQueueModel::clearImage(int rowIndex)
{
    if (rowIndex < 0 || rowIndex >= m_rows.size()) {
        return;
    }
    m_rows[rowIndex].image = Flasher::ImageInfo();
    m_rows[rowIndex].ticked = false;
    m_rows[rowIndex].shaPinMismatch = false;
    emit dataChanged(index(rowIndex, 0), index(rowIndex, ColumnCount - 1));
    emit queueChanged();
}

void FlasherQueueModel::setTicked(int rowIndex, bool ticked)
{
    if (rowIndex < 0 || rowIndex >= m_rows.size()) {
        return;
    }
    bool changed = false;
    for (int other = 0; other < m_rows.size(); ++other) {
        bool wanted = false;
        if (other == rowIndex) {
            wanted = ticked;
        } else if (ticked) {
            wanted = false;                  // exclusive: selecting one clears the rest
        } else {
            wanted = m_rows[other].ticked;   // unselecting leaves the others alone
        }
        if (m_rows[other].ticked != wanted) {
            m_rows[other].ticked = wanted;
            changed = true;
            emit dataChanged(index(other, 0), index(other, ColumnCount - 1));
        }
    }
    if (changed) {
        emit queueChanged();
    }
}

int FlasherQueueModel::tickedRow() const
{
    for (int index = 0; index < m_rows.size(); ++index) {
        if (m_rows[index].ticked) {
            return index;
        }
    }
    return -1;
}

void FlasherQueueModel::setShaPinMismatch(int rowIndex, bool mismatch)
{
    if (rowIndex < 0 || rowIndex >= m_rows.size()) {
        return;
    }
    if (m_rows[rowIndex].shaPinMismatch == mismatch) {
        return;
    }
    m_rows[rowIndex].shaPinMismatch = mismatch;
    emit queueChanged();
}

// =============================================================================
//  FlasherQueueDelegate
// =============================================================================

FlasherQueueDelegate::FlasherQueueDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QSize FlasherQueueDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    // Two text lines plus the padding the design gives each row (12 px top
    // and bottom), whatever the font size.
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    const int lineHeight = option.fontMetrics.height();
    size.setHeight(lineHeight * 2 + 24);
    return size;
}

void FlasherQueueDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const
{
    QStyleOptionViewItem itemOption(option);
    initStyleOption(&itemOption, index);

    const QWidget *widget = option.widget;
    QStyle *style = QApplication::style();
    if (widget != nullptr) {
        style = widget->style();
    }

    // Background and selection first, with no text: the text is ours.
    QStyleOptionViewItem backgroundOption(itemOption);
    backgroundOption.text.clear();
    backgroundOption.features &= ~QStyleOptionViewItem::HasCheckIndicator;
    style->drawControl(QStyle::CE_ItemViewItem, &backgroundOption, painter, widget);

    const auto *model = qobject_cast<const FlasherQueueModel *>(index.model());
    bool rowTicked = true;
    if (model != nullptr) {
        rowTicked = model->row(index.row()).ticked;
    }

    painter->save();
    // Rows that are not selected are dimmed a little: present and readable,
    // clearly not the card that Flash will send to.
    if (!rowTicked && index.column() != FlasherQueueModel::ColumnCard) {
        painter->setOpacity(0.7);
    }

    QRect contentRect = itemOption.rect.adjusted(8, 0, -8, 0);

    // The selector, drawn inside exactly the rect QStyledItemDelegate's
    // editorEvent hit-tests, so clicking it works with the stock handling.
    //
    // Drawn by hand rather than by the style: Fusion's unchecked radio came
    // out as a solid dark dot on this palette, so every row looked selected
    // and the one that WAS could not be told apart. Now:
    //   selected   -- a filled GREEN circle (UiColor::selectedMark) with a
    //                 tick, inside a light ring so it stays distinct on the
    //                 blue selected-row highlight too;
    //   the others -- an empty circle with a grey outline.
    // Shape as well as colour carries it: filled vs empty.
    if (index.column() == FlasherQueueModel::ColumnCard) {
        QStyleOptionViewItem checkOption(itemOption);
        const QRect checkRect = style->subElementRect(QStyle::SE_ItemViewItemCheckIndicator,
                                                      &checkOption, widget);
        const QPointF centre = QRectF(checkRect).center();
        const qreal outer = 10.0;   // radius, px
        // The theme's real background, from the application palette. NOT
        // option.palette: the table has a style sheet ("background:
        // transparent"), which makes the view's Base transparent -- and a
        // transparent "light" drew every ring and dot black.
        const QColor light = QApplication::palette().color(QPalette::Base);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        if (itemOption.checkState == Qt::Checked) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(light);                          // separating ring
            painter->drawEllipse(centre, outer + 2.0, outer + 2.0);
            painter->setBrush(UiColor::selectedMark());        // green, >= 3:1 on the base
            painter->drawEllipse(centre, outer, outer);
            // A tick: "selected" by shape as well as colour.
            QPen tick(light);
            tick.setWidthF(2.4);
            tick.setCapStyle(Qt::RoundCap);
            tick.setJoinStyle(Qt::RoundJoin);
            painter->setPen(tick);
            painter->setBrush(Qt::NoBrush);
            QPainterPath mark;
            mark.moveTo(centre + QPointF(-outer * 0.45, outer * 0.02));
            mark.lineTo(centre + QPointF(-outer * 0.12, outer * 0.35));
            mark.lineTo(centre + QPointF(outer * 0.48, -outer * 0.32));
            painter->drawPath(mark);
        } else {
            QPen ring(FlasherStyle::muted());
            ring.setWidthF(1.6);
            painter->setPen(ring);
            painter->setBrush(light);
            painter->drawEllipse(centre, outer - 1.0, outer - 1.0);
        }
        painter->restore();
        contentRect.setLeft(checkRect.right() + 10);
        if (!rowTicked) {
            painter->setOpacity(0.7);
        }
    }

    const QString primaryText = index.data(Qt::DisplayRole).toString();
    const QString secondaryText = index.data(FlasherQueueModel::SecondaryTextRole).toString();
    QColor textColor = option.palette.color(QPalette::Text);
    if (option.state & QStyle::State_Selected) {
        textColor = option.palette.color(QPalette::HighlightedText);
    }

    // ---- the badge column ------------------------------------------------
    if (index.column() == FlasherQueueModel::ColumnCheck) {
        const int kind = index.data(FlasherQueueModel::BadgeKindRole).toInt();
        QColor ink = FlasherStyle::muted();
        if (kind == 0) {
            ink = FlasherStyle::accepted();
        } else if (kind == 1) {
            ink = FlasherStyle::attention();
        }
        QFont badgeFont = itemOption.font;
        badgeFont.setBold(true);
        badgeFont.setPointSizeF(badgeFont.pointSizeF() * 0.92);
        const QFontMetrics metrics(badgeFont);
        const int badgeHeight = metrics.height() + 8;
        const int textWidth = metrics.horizontalAdvance(primaryText);
        int badgeWidth = textWidth + 20;
        if (badgeWidth > contentRect.width()) {
            badgeWidth = contentRect.width();
        }
        const QRect badgeRect(contentRect.left(),
                              contentRect.center().y() - badgeHeight / 2,
                              badgeWidth, badgeHeight);
        painter->setRenderHint(QPainter::Antialiasing, true);
        QPainterPath path;
        path.addRoundedRect(badgeRect, badgeHeight / 2.0, badgeHeight / 2.0);
        painter->fillPath(path, FlasherStyle::tint(ink));
        painter->setPen(ink);
        painter->setFont(badgeFont);
        painter->drawText(badgeRect.adjusted(10, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft,
                          metrics.elidedText(primaryText, Qt::ElideRight, badgeRect.width() - 20));
        painter->restore();
        return;
    }

    // ---- the handle and remove columns: one centred glyph ---------------
    if (index.column() == FlasherQueueModel::ColumnHandle
        || index.column() == FlasherQueueModel::ColumnRemove) {
        painter->setPen(FlasherStyle::muted());
        painter->setFont(itemOption.font);
        painter->drawText(itemOption.rect, Qt::AlignCenter, primaryText);
        painter->restore();
        return;
    }

    // ---- two-line cells ----------------------------------------------------
    QFont primaryFont = itemOption.font;
    if (index.data(FlasherQueueModel::MonospaceRole).toBool()) {
        primaryFont = UiStyle::monoFont();
        primaryFont.setPointSizeF(itemOption.font.pointSizeF());
    }
    if (index.column() == FlasherQueueModel::ColumnCard) {
        primaryFont.setBold(true);
    }
    QColor primaryColor = textColor;
    if (index.data(FlasherQueueModel::MissingImageRole).toBool()) {
        primaryColor = FlasherStyle::attention();
        primaryFont.setBold(true);
    }

    const QFontMetrics primaryMetrics(primaryFont);
    QFont secondaryFont = itemOption.font;
    secondaryFont.setPointSizeF(itemOption.font.pointSizeF() * 0.9);
    const QFontMetrics secondaryMetrics(secondaryFont);

    int totalHeight = primaryMetrics.height();
    if (!secondaryText.isEmpty()) {
        totalHeight += secondaryMetrics.height() + 2;
    }
    int top = contentRect.center().y() - totalHeight / 2;

    painter->setFont(primaryFont);
    painter->setPen(primaryColor);
    painter->drawText(QRect(contentRect.left(), top, contentRect.width(), primaryMetrics.height()),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      primaryMetrics.elidedText(primaryText, Qt::ElideMiddle, contentRect.width()));
    top += primaryMetrics.height() + 2;

    if (!secondaryText.isEmpty()) {
        painter->setFont(secondaryFont);
        QColor secondaryColor = FlasherStyle::muted();
        if (option.state & QStyle::State_Selected) {
            secondaryColor = textColor;
        }
        painter->setPen(secondaryColor);
        // Paths are elided in the middle: the drive and the file's own folder
        // are the parts that tell two builds apart.
        painter->drawText(QRect(contentRect.left(), top, contentRect.width(), secondaryMetrics.height()),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          secondaryMetrics.elidedText(secondaryText, Qt::ElideMiddle, contentRect.width()));
    }
    painter->restore();
}
