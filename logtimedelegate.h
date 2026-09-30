#ifndef LOGTIMEDELEGATE_H
#define LOGTIMEDELEGATE_H
// =============================================================================
//  logtimedelegate.{h,cpp} — the Time column, drawn so the digits that
//  changed are the ones you see.
//
//  Two problems, both cosmetic, both costing real reading time.
//
//  FIRST, the column was drawn in the proportional UI font, so a 1 is
//  narrower than a 0 and no two timestamps line up. A column of times is
//  read by scanning DOWN it, and nothing in a proportional font lines up
//  vertically, so "which of these is 200 ms later" became a character-by-
//  character comparison.
//
//  SECOND, and worse on this data: consecutive rows share almost every
//  character. Reading 11:04:09.100 / 11:04:09.140 / 11:04:09.980, the eye
//  has to walk past eight identical characters to reach the three that
//  differ, on every row, for thousands of rows. The information is entirely
//  in the tail and the ink is almost entirely in the head.
//
//  So: a fixed font, and the run of leading characters identical to the row
//  ABOVE is drawn muted. Nothing is hidden or abbreviated — the full
//  timestamp is still there, still selectable, still copied in full — it is
//  only weighted so the changing digits carry the contrast.
//
//  Deliberately compared against the row above IN VIEW ORDER, not against
//  the previous entry in the model: under a filter or a sort the row above
//  is what the eye actually compares against, and dimming relative to a row
//  that is not on screen would mute the wrong characters.
// =============================================================================
#include <QFont>
#include <QStyledItemDelegate>

class LogTimeDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit LogTimeDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

    // How many leading characters this row shares with the one above it.
    //
    // Exposed because it is the whole behaviour and the rest is painting:
    // a test can check what would be dimmed without rendering a pixel.
    // The bookmark bullet is never counted — it is not part of the time,
    // and a bookmarked row would otherwise share nothing with its
    // neighbour and light up entirely for the wrong reason.
    static int sharedPrefix(const QString &current, const QString &previous);

    // The bullet LogModel prefixes to a bookmarked row, if present.
    static QString bulletOf(const QString &text);

private:
    QFont m_mono;
};

#endif  // LOGTIMEDELEGATE_H
