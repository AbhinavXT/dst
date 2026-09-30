#include "logtimedelegate.h"

#include "uicolors.h"
#include "uistyle.h"

#include <QApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>

namespace {
// The bookmark mark LogModel puts in front of a bookmarked row's time.
const QString kBullet = QStringLiteral("● ");
}  // namespace

LogTimeDelegate::LogTimeDelegate(QObject *parent)
    : QStyledItemDelegate(parent), m_mono(UiStyle::monoFont())
{
}

QString LogTimeDelegate::bulletOf(const QString &text)
{
    return text.startsWith(kBullet) ? kBullet : QString();
}

int LogTimeDelegate::sharedPrefix(const QString &current, const QString &previous)
{
    // Measured on the times themselves, with any bookmark bullet taken off
    // both first: a bookmarked row is still the same second as its
    // neighbour, and comparing the rendered strings would say otherwise.
    const QString a = current.mid(bulletOf(current).size());
    const QString b = previous.mid(bulletOf(previous).size());

    const int limit = qMin(a.size(), b.size());
    int n = 0;
    while (n < limit && a.at(n) == b.at(n)) { ++n; }

    // Never the whole string. Two rows in the same millisecond do happen —
    // a burst arriving together — and a fully muted row would read as
    // disabled rather than as identical.
    if (n >= a.size()) { n = qMax(0, a.size() - 1); }
    return n;
}

QSize LogTimeDelegate::sizeHint(const QStyleOptionViewItem &option,
                                const QModelIndex &index) const
{
    QStyleOptionViewItem opt(option);
    opt.font = m_mono;
    opt.fontMetrics = QFontMetrics(m_mono);
    return QStyledItemDelegate::sizeHint(opt, index);
}

void LogTimeDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                            const QModelIndex &index) const
{
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    opt.font = m_mono;
    opt.fontMetrics = QFontMetrics(m_mono);

    const QString text = opt.text;

    // Background, selection and focus ring are the style's business. Drawing
    // them by hand is how a delegate ends up looking almost but not quite
    // like every other cell, and how it stops following the theme.
    opt.text.clear();
    QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

    if (text.isEmpty()) { return; }

    const bool selected = opt.state & QStyle::State_Selected;
    const QPalette::ColorGroup group =
        (opt.state & QStyle::State_Enabled) ? QPalette::Normal : QPalette::Disabled;
    const QColor normal = selected
                              ? opt.palette.color(group, QPalette::HighlightedText)
                              : opt.palette.color(group, QPalette::Text);

    painter->save();
    painter->setFont(m_mono);

    const QRect cell = style->subElementRect(QStyle::SE_ItemViewItemText,
                                             &opt, opt.widget);
    const QFontMetrics fm(m_mono);

    // The row the eye compares against: the one above, in view order.
    const QString previous =
        index.row() > 0
            ? index.sibling(index.row() - 1, index.column()).data(Qt::DisplayRole).toString()
            : QString();

    // On the selected row nothing is muted. It is the row being read, its
    // background has changed underneath it, and a muted-on-highlight colour
    // is the one that fails a contrast check in one theme or the other.
    const int shared = (selected || previous.isEmpty())
                           ? 0
                           : sharedPrefix(text, previous);

    if (shared <= 0) {
        painter->setPen(normal);
        painter->drawText(cell, int(opt.displayAlignment), text);
        painter->restore();
        return;
    }

    const QString bullet = bulletOf(text);
    const QString body   = text.mid(bullet.size());
    const QString head   = body.left(shared);     // same as the row above
    const QString tail   = body.mid(shared);      // what changed

    // Laid out from the whole string's rect so the characters sit exactly
    // where they would if this were one drawText: splitting the string must
    // not move it, or the column would shimmer as rows scroll past.
    const int fullWidth = fm.horizontalAdvance(text);
    int x = cell.left();
    if (opt.displayAlignment & Qt::AlignHCenter) {
        x = cell.left() + (cell.width() - fullWidth) / 2;
    } else if (opt.displayAlignment & Qt::AlignRight) {
        x = cell.right() - fullWidth;
    }

    auto run = [&](const QString &s, const QColor &c) {
        if (s.isEmpty()) { return; }
        painter->setPen(c);
        const QRect r(x, cell.top(), fm.horizontalAdvance(s), cell.height());
        painter->drawText(r, Qt::AlignLeft | Qt::AlignVCenter, s);
        x += r.width();
    };

    run(bullet, normal);          // the bookmark mark keeps full weight
    run(head,   UiColor::muted());
    run(tail,   normal);

    painter->restore();
}
