#include "sourcerowdelegate.h"

#include "uicolors.h"
#include "uistyle.h"

#include <QApplication>
#include <QPainter>

QColor SourceRowDelegate::healthColor(int health)
{
    switch (health) {
    case Live:    return UiColor::ok();
    case Late:    return UiColor::warning();
    case Offline: return UiColor::error();
    default:      return UiColor::muted();
    }
}

QSize SourceRowDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QSize s = QStyledItemDelegate::sizeHint(option, index);
    // Two lines on every column, so the counters line up with the name.
    const QFontMetrics fm(option.font);
    s.setHeight(qMax(s.height(), fm.height() * 2 + UiStyle::space(3)));
    return s;
}

void SourceRowDelegate::paint(QPainter *p, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const
{
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    // The background (selection, hover, alternating) as the style draws it;
    // the text is ours.
    const QString text = opt.text;
    opt.text.clear();
    QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, p, opt.widget);

    const bool selected = opt.state & QStyle::State_Selected;
    const QPalette &pal = opt.palette;
    const QColor ink = selected ? pal.color(QPalette::HighlightedText) : pal.color(QPalette::Text);
    const QVariant fg = index.data(Qt::ForegroundRole);
    const QColor custom = fg.canConvert<QBrush>() ? fg.value<QBrush>().color() : QColor();
    const QColor muted = selected ? ink : UiColor::muted();
    const QRect r = opt.rect.adjusted(UiStyle::space(2), UiStyle::space(1), -UiStyle::space(2), -UiStyle::space(1));
    const QFontMetrics fm(opt.font);

    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);

    if (index.column() == 0) {
        const int health = index.data(HealthRole).toInt();
        const QColor dot = healthColor(health);
        const int d = 9;
        const QRectF dr(r.left(), r.top() + fm.height() / 2.0 - d / 2.0 + 1, d, d);
        if (health == Live) {
            p->setPen(Qt::NoPen);
            p->setBrush(dot);
            p->drawEllipse(dr);
        } else {
            p->setPen(QPen(dot, 1.8));
            p->setBrush(Qt::NoBrush);
            p->drawEllipse(dr.adjusted(1, 1, -1, -1));
            if (health == Offline) p->drawLine(dr.bottomLeft() + QPointF(1.5, -1.5), dr.topRight() + QPointF(-1.5, 1.5));
        }
        const int x = r.left() + d + UiStyle::space(2);
        QFont nameFont = opt.font;
        nameFont.setWeight(QFont::DemiBold);
        p->setFont(nameFont);
        p->setPen(custom.isValid() && !selected ? custom : ink);
        const QRect nameRect(x, r.top(), r.right() - x, fm.height());
        p->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                    QFontMetrics(nameFont).elidedText(text, Qt::ElideRight, nameRect.width()));
        QFont metaFont = opt.font;
        metaFont.setPointSizeF(qMax(7.5, opt.font.pointSizeF() - 1));
        p->setFont(metaFont);
        p->setPen(muted);
        const QRect metaRect(x, r.top() + fm.height() + 2, r.right() - x, fm.height());
        p->drawText(metaRect, Qt::AlignLeft | Qt::AlignVCenter,
                    QFontMetrics(metaFont).elidedText(index.data(MetaRole).toString(), Qt::ElideRight, metaRect.width()));
    } else {
        // The counters: mono, right-aligned, on the name's line.
        QFont mono = UiStyle::monoFont();
        mono.setPointSizeF(opt.font.pointSizeF() > 0 ? opt.font.pointSizeF() : mono.pointSizeF());
        p->setFont(mono);
        p->setPen(custom.isValid() && !selected ? custom : muted);
        p->drawText(QRect(r.left(), r.top(), r.width(), fm.height()), Qt::AlignRight | Qt::AlignVCenter, text);
    }
    p->restore();
}
