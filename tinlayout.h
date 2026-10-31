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

// Session 432: a signal as the SIP and the tag-TIN layouts draw it: a mast
// from `foot` (its top, on the track's edge; an arrow there points at the
// track), an arm, and the heads outwards in `dir` (1: towards rising x, the
// way a train it faces runs). A main head's lamps touch, from the mast: Red
// (three bars), Yellow (a slash), Green (a bar), a second Yellow (4 aspects);
// a shunt disc (position light: three small lamps) nearest the mast; a
// calling-on lamp, small, behind it. `lit` (Lamp flags): those lamps filled
// in their colour (the KAVACH aspect), the others drawn as on the sheets.
// Returns where it went.
// Session 435: `indicator` 1-6, a junction route indicator: five small lamps
// on an arm under the main head, 1-3 behind the mast's side, 4-6 ahead,
// steeper towards 3 and 4; LampIndicator lights them.
struct SignalHead { int aspects = 3; bool callingOn = false, shunt = false; int indicator = 0; };   // aspects 0: a shunt signal only
enum Lamp { LampRed = 1, LampYellow = 2, LampGreen = 4, LampYellow2 = 8, LampCallingOn = 16, LampIndicator = 32 };
QRectF drawSignal(QPainter &p, const QPointF &foot, const SignalHead &head, int dir, int lit, const QColor &ink);

}  // namespace TinLayout

#endif  // TINLAYOUT_H
