#include "blockmapwidget.h"

#include <QHelpEvent>
#include <QPainter>
#include <QToolTip>

#include "flash_engine.h"
#include "flasherstyle.h"
#include "uicolors.h"

BlockMapWidget::BlockMapWidget(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);

    // DLConsole integration: the colours come from the active theme (see
    // paintEvent), so a theme toggle has to trigger a repaint.
    UiColor::onThemeChange(this, [this]() { update(); });
}

QSize BlockMapWidget::sizeHint() const
{
    return QSize(25 * (kCell + kGap), heightForWidth(25 * (kCell + kGap)));
}

int BlockMapWidget::columnsForWidth(int width) const
{
    int columns = (width + kGap) / (kCell + kGap);
    if (columns < 1) {
        columns = 1;
    }
    return columns;
}

int BlockMapWidget::heightForWidth(int width) const
{
    const int columns = columnsForWidth(width);
    const int rows = (cells_.size() + columns - 1) / columns;
    return rows * (kCell + kGap);
}

void BlockMapWidget::reset(quint32 totalBlocks)
{
    cells_.fill(NotSent, static_cast<int>(totalBlocks));
    sendCursor_ = 0;
    haveBitmap_ = false;
    updateGeometry();
    update();
}

void BlockMapWidget::setSendCursor(quint32 nextBlockToSend)
{
    sendCursor_ = nextBlockToSend;
    if (!haveBitmap_) {
        update();
    }
}

void BlockMapWidget::setBitmap(const QByteArray& bitmap, quint32 totalBlocks)
{
    if (cells_.size() != static_cast<int>(totalBlocks)) {
        reset(totalBlocks);
    }

    const uchar* bits = reinterpret_cast<const uchar*>(bitmap.constData());
    for (quint32 block = 0; block < totalBlocks; ++block) {
        const int byteIndex = static_cast<int>(block >> 3);
        bool held = false;
        if (byteIndex < bitmap.size()) {
            held = ((bits[byteIndex] >> (block & 7)) & 1) != 0;
        }

        const quint8 previous = cells_[static_cast<int>(block)];
        if (held) {
            /* Orange only if the previous bitmap said this block was missing. */
            /* A block repaired in an earlier round settles back to blue. */
            if (haveBitmap_ && previous == Missing) {
                cells_[static_cast<int>(block)] = Repaired;
            } else {
                cells_[static_cast<int>(block)] = Held;
            }
        } else {
            cells_[static_cast<int>(block)] = Missing;
        }
    }
    haveBitmap_ = true;
    update();
}

int BlockMapWidget::cellAt(const QPoint& pos) const
{
    const int columns = columnsForWidth(width());
    const int column = pos.x() / (kCell + kGap);
    const int row    = pos.y() / (kCell + kGap);
    if (column >= columns) {
        return -1;
    }
    const int index = row * columns + column;
    if (index < 0 || index >= cells_.size()) {
        return -1;
    }
    return index;
}

void BlockMapWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    // DLConsole integration: the handoff's fixed hex values (#1F4E79 held,
    // #D98A3D repaired, #EFECE6 not sent, #A5520A / #F2C9A0 missing) are
    // replaced by their theme meanings, so the map follows Ayu Light/Dark and
    // stays inside the contrastaudit rule (no colour literals outside
    // uicolors/theme/uistyle). The legend under the map uses the same calls.
    const QColor heldColor = FlasherStyle::primary();
    const QColor repairedColor = FlasherStyle::active();
    const QColor notSentColor = FlasherStyle::notSent();
    const QColor missingBorder = FlasherStyle::danger();
    const QColor missingFill = palette().color(QPalette::Base);
    QBrush missingBrush(FlasherStyle::danger(), Qt::BDiagPattern);

    const int columns = columnsForWidth(width());
    for (int index = 0; index < cells_.size(); ++index) {
        const QRect rect((index % columns) * (kCell + kGap),
                         (index / columns) * (kCell + kGap), kCell, kCell);

        quint8 state = cells_[index];
        if (!haveBitmap_ && static_cast<quint32>(index) < sendCursor_) {
            state = Held; /* sent, not yet confirmed: draw lighter below */
        }

        if (state == Held) {
            QColor fill = heldColor;
            if (!haveBitmap_) {
                fill.setAlpha(110);
            }
            painter.fillRect(rect, fill);
        } else if (state == Repaired) {
            painter.fillRect(rect, repairedColor);
        } else if (state == Missing) {
            painter.fillRect(rect, missingFill);
            painter.fillRect(rect, missingBrush);
            painter.setPen(missingBorder);
            painter.drawRect(rect.adjusted(0, 0, -1, -1));
        } else {
            painter.fillRect(rect, notSentColor);
        }
    }
}

bool BlockMapWidget::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        QHelpEvent* help = static_cast<QHelpEvent*>(event);
        const int index = cellAt(help->pos());
        if (index >= 0) {
            const quint32 offset = static_cast<quint32>(index) * kflash::kBlockSize;
            QToolTip::showText(help->globalPos(),
                               QString("Block %1  ·  offset 0x%2")
                                   .arg(index)
                                   .arg(offset, 8, 16, QLatin1Char('0')));
        } else {
            QToolTip::hideText();
            event->ignore();
        }
        return true;
    }
    return QWidget::event(event);
}
