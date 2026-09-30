/* =========================================================================
 * blockmapwidget.h  -  the "Block map" panel from the Flashing screen
 *
 * One cell per 1450-byte block, filled from the board's STATUS bitmap:
 *   Acknowledged       solid  #1F4E79
 *   Resent this round  solid  #D98A3D  (was missing in the previous bitmap,
 *                                        held in this one)
 *   Still missing      hatched #F2C9A0 on white, #A5520A border
 *   Not yet sent       #EFECE6 (first pass, beyond the send cursor)
 * Hover shows block number and byte offset.
 * ========================================================================= */
#pragma once

#include <QByteArray>
#include <QVector>
#include <QWidget>

class BlockMapWidget : public QWidget
{
    Q_OBJECT
public:
    explicit BlockMapWidget(QWidget* parent = nullptr);

    QSize sizeHint() const override;
    bool  hasHeightForWidth() const override { return true; }
    int   heightForWidth(int width) const override;

public slots:
    void reset(quint32 totalBlocks);
    void setBitmap(const QByteArray& bitmap, quint32 totalBlocks);
    void setSendCursor(quint32 nextBlockToSend);   /* first pass only */

protected:
    void paintEvent(QPaintEvent* event) override;
    bool event(QEvent* event) override;            /* tooltips */

private:
    enum CellState : quint8 { NotSent, Held, Repaired, Missing };

    int  columnsForWidth(int width) const;
    int  cellAt(const QPoint& pos) const;

    QVector<quint8> cells_;
    quint32         sendCursor_ = 0;
    bool            haveBitmap_ = false;

    static const int kCell = 14;
    static const int kGap  = 3;
};
