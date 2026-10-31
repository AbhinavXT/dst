#include "tinlayout.h"

#include "uicolors.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPolygonF>

#include <cmath>

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

QRectF drawSignal(QPainter &p, const QPointF &foot, const SignalHead &head, int dir, int lit, const QColor &ink)
{
    const QPalette pal = QApplication::palette();
    const double r = 4.5, cr = 2.8, sr = 7.5;       // main lamp, calling-on lamp, shunt disc
    const double d = dir < 0 ? -1 : 1;
    const double x = foot.x(), cy = foot.y() + 13;  // the heads' centre line
    QRectF box(QPointF(x - 3, foot.y()), QPointF(x + 3, cy + r));
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPen thin(ink, 1.1);
    p.setPen(thin);
    p.setBrush(ink);
    p.drawPolygon(QPolygonF() << QPointF(x, foot.y()) << QPointF(x - 2.5, foot.y() + 4) << QPointF(x + 2.5, foot.y() + 4));   // Qt 5: no {…}
    p.drawLine(QPointF(x, foot.y() + 4), QPointF(x, cy + r + 1));

    // Outwards: the arm, then each head touching the one before.
    double at = x + d * 5;
    p.drawLine(QPointF(x, cy), QPointF(at, cy));
    if (head.shunt || head.aspects <= 0) {
        QPainterPath disc;
        disc.moveTo(at, cy - sr);
        disc.arcTo(QRectF(at - sr, cy - sr, 2 * sr, 2 * sr), 90, -180 * d);
        disc.closeSubpath();
        p.setBrush(pal.color(QPalette::Base));
        p.drawPath(disc);
        p.setBrush(ink);
        for (const QPointF &o : { QPointF(0.3, -0.45), QPointF(0.3, 0.45), QPointF(0.68, 0.0) })
            p.drawEllipse(QPointF(at + d * sr * o.x(), cy + sr * o.y()), 1.3, 1.3);
        box |= disc.boundingRect();
        at += d * sr;
    }
    const int lamps = qBound(0, head.aspects, 4);
    const double firstLamp = at + d * r;   // the Red lamp's centre: a route indicator hangs from it
    const int flag[] = { LampRed, LampYellow, LampGreen, LampYellow2 };
    const char *colour[] = { "red", "yellow", "green", "yellow" };
    for (int i = 0; i < lamps; ++i) {
        const QPointF c(at + d * r, cy);
        const bool on = lit & flag[i];
        p.setPen(thin);
        p.setBrush(on ? UiColor::signalLamp(QLatin1String(colour[i])) : pal.color(QPalette::Base));
        p.drawEllipse(c, r, r);
        p.setPen(QPen(on ? UiColor::withContrast(ink, UiColor::signalLamp(QLatin1String(colour[i])), 3.0) : ink, 0.9));
        const double k = r * 0.68;
        if (i == 0) {                                       // Red: three bars
            for (const double dx : { -r * 0.42, 0.0, r * 0.42 }) {
                const double h = std::sqrt(r * r - dx * dx) * 0.86;
                p.drawLine(QPointF(c.x() + dx, c.y() - h), QPointF(c.x() + dx, c.y() + h));
            }
        } else if (i == 2) {                                // Green: a bar
            p.drawLine(QPointF(c.x() - k * 1.2, c.y()), QPointF(c.x() + k * 1.2, c.y()));
        } else {                                            // Yellow: a slash
            p.drawLine(QPointF(c.x() - k, c.y() + k), QPointF(c.x() + k, c.y() - k));
        }
        box |= QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r);
        at += d * 2 * r;
    }
    if (head.indicator >= 1 && head.indicator <= 6 && lamps > 0) {   // session 435: the route indicator
        static const double degrees[] = { 30, 50, 70, 70, 50, 30 };
        const double side = head.indicator <= 3 ? -d : d;
        const double a = degrees[head.indicator - 1] * 3.14159265358979 / 180.0;
        const QPointF from(firstLamp, cy + r);
        const QPointF step(side * std::cos(a) * 3.4, std::sin(a) * 3.4);
        p.setPen(thin);
        p.drawLine(from, from + step * 0.6);
        p.setBrush(lit & LampIndicator ? ink : pal.color(QPalette::Base));   // lit: solid (a white lamp on a white sheet would not show)
        for (int i = 1; i <= 5; ++i) {
            const QPointF c = from + step * (i + 0.2);
            p.drawEllipse(c, 1.5, 1.5);
            box |= QRectF(c.x() - 1.5, c.y() - 1.5, 3, 3);
        }
    }
    if (head.callingOn || (lit & LampCallingOn)) {          // behind the mast, small
        const QPointF c(x - d * (4 + cr), cy);
        p.setPen(thin);
        p.drawLine(QPointF(x, cy), QPointF(x - d * 4, cy));
        p.setBrush(lit & LampCallingOn ? ink : pal.color(QPalette::Base));   // lit: solid, as the indicator
        p.drawEllipse(c, cr, cr);
        p.drawLine(QPointF(c.x() - cr * 0.7, c.y() + cr * 0.7), QPointF(c.x() + cr * 0.7, c.y() - cr * 0.7));
        box |= QRectF(c.x() - cr, c.y() - cr, 2 * cr, 2 * cr);
    }
    p.restore();
    return box.adjusted(-1, 0, 1, 1);
}

}  // namespace TinLayout
