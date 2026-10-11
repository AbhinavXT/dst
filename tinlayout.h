#ifndef TINLAYOUT_H
#define TINLAYOUT_H

// =============================================================================
//  RFID Tag-TIN layout drawing (session 212) — RDSO/SPN/196/2020 Annexure-H
//  (Amdt-5, 03.06.2024), shared by Station Layout and the Tag Builder strip.
//  -----------------------------------------------------------------------------
//    H2.15  a main + duplicate tag set is one symbol: a rectangle when both
//           hold the same location, a triangle when they do not, its tip
//           towards the duplicate;
//    H2.17  one colour per TIN section, neighbours in different colours;
//    H2.18  non-Kavach territory (TIN 0 here) in white;
//    H2.21  a letter for the tag's placement: N S T X D G L A.
//  The drawings stay to scale by absolute location (decided 2026-10-11).
// =============================================================================

#include <QChar>
#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QString>

class QPainter;

namespace TinLayout {

// H2.21: inline N, signal foot S, TIN discrimination/turnout T, exit X,
// dead stop D (type 9 by placement), LC gate G (10), adjacent line L (11),
// adjustment/junction A (12). '?' for anything else.
QChar notation(int type, int placement);
QString notationName(QChar letter);           // "Signal foot tag", ...

// H2.17 / H2.18: the band colour of a TIN; invalid for TIN 0 (non-Kavach:
// drawn hollow). A graphical colour (3:1), never text.
QColor tinColor(int tin);
QString tinLabel(int tin);                    // "(N-65)", as on the RDSO layouts

enum Shape { Box, TipRight, TipLeft };
// H2.15: the shape of a set, from the main's and the duplicate's locations
// (no duplicate: a box).
Shape shapeOf(qint64 mainLoc, qint64 dupLoc, bool haveDup);

// The symbol centred on `c`, its letter inside; returns where it went.
QRectF drawTag(QPainter &p, const QPointF &c, QChar letter, Shape s, const QColor &edge, double edgeWidth = 1.2);
// "R-046" in a blue-edged box whose bottom centre is `bottom`; returns it.
QRectF drawIdBox(QPainter &p, const QPointF &bottom, const QString &text);
QString idText(const QString &tagName);       // "981" -> "R-981"

}  // namespace TinLayout

#endif  // TINLAYOUT_H
