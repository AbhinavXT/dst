#include "tinlayout.h"

#include "uicolors.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QPainter>
#include <QPalette>
#include <QPolygonF>

namespace TinLayout {

QChar notation(int type, int placement)
{
    switch (type) {
    case 10: return QLatin1Char('G');
    case 11: return QLatin1Char('L');
    case 12: return QLatin1Char('A');
    case 9: break;
    default: return QLatin1Char('?');
    }
    switch (placement) {               // the schema's rfidPlace
    case 0: return QLatin1Char('N');
    case 1: case 2: case 6: return QLatin1Char('S');
    case 3: return QLatin1Char('T');
    case 4: case 5: case 7: return QLatin1Char('X');
    case 8: case 9: return QLatin1Char('D');
    default: return QLatin1Char('?');
    }
}

QString notationName(QChar letter)
{
    switch (letter.toLatin1()) {
    case 'N': return QObject::tr("Inline section tag");
    case 'S': return QObject::tr("Signal foot tag");
    case 'T': return QObject::tr("TIN discrimination / turnout tag");
    case 'X': return QObject::tr("Exit tag");
    case 'D': return QObject::tr("Dead stop tag");
    case 'G': return QObject::tr("LC gate tag");
    case 'L': return QObject::tr("Adjacent line info tag");
    case 'A': return QObject::tr("Adjustment / junction tag");
    default: return QObject::tr("Tag of an unknown type");
    }
}

QColor tinColor(int tin) { return tin <= 0 ? QColor() : UiColor::series(tin); }

QString tinLabel(int tin) { return QStringLiteral("(N-%1)").arg(tin); }

Shape shapeOf(qint64 mainLoc, qint64 dupLoc, bool haveDup)
{
    if (!haveDup || dupLoc == mainLoc) return Box;
    return dupLoc > mainLoc ? TipRight : TipLeft;
}

QRectF drawTag(QPainter &p, const QPointF &c, QChar letter, Shape s, const QColor &edge, double edgeWidth)
{
    const QPalette pal = QApplication::palette();
    const double hw = 8, hh = 6.5;
    QPolygonF poly;
    if (s == Box) {
        poly << QPointF(c.x() - hw, c.y() - hh) << QPointF(c.x() + hw, c.y() - hh)
             << QPointF(c.x() + hw, c.y() + hh) << QPointF(c.x() - hw, c.y() + hh);
    } else {
        const double d = s == TipRight ? 1 : -1;          // the tip's side
        poly << QPointF(c.x() - d * hw, c.y() - hh - 1.5) << QPointF(c.x() + d * (hw + 2), c.y())
             << QPointF(c.x() - d * hw, c.y() + hh + 1.5);
    }
    p.save();
    p.setPen(QPen(edge, edgeWidth));
    p.setBrush(pal.color(QPalette::Base));
    p.drawPolygon(poly);
    QFont f = p.font();
    f.setBold(true);
    f.setPixelSize(9);
    p.setFont(f);
    p.setPen(pal.color(QPalette::Text));
    // In a triangle the letter sits towards the wide end.
    const QRectF box = poly.boundingRect();
    const QRectF textBox = s == Box ? box : box.adjusted(s == TipRight ? 0 : hw * 0.6, 0, s == TipRight ? -hw * 0.6 : 0, 0);
    p.drawText(textBox, Qt::AlignCenter, QString(letter));
    p.restore();
    return box;
}

QRectF drawIdBox(QPainter &p, const QPointF &bottom, const QString &text)
{
    const QFontMetricsF fm(p.font());
    const double w = fm.horizontalAdvance(text) + 6, h = fm.height();
    const QRectF r(bottom.x() - w / 2, bottom.y() - h, w, h);
    p.save();
    p.setPen(QPen(UiColor::accent(), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r);
    p.setPen(QApplication::palette().color(QPalette::Text));
    p.drawText(r, Qt::AlignCenter, text);
    p.restore();
    return r;
}

QString idText(const QString &tagName) { return QStringLiteral("R-") + tagName; }

}  // namespace TinLayout
