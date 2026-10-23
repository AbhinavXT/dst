#ifndef ENGUNITS_H
#define ENGUNITS_H

// =============================================================================
//  engunits.h -- values typed with their unit in the Packet Maker (session 342)
//  ---------------------------------------------------------------------------
//  Most fields kavach.xml gives a unit are carried as they are: a distance
//  in metres, a speed in km/h. A few are codes with a formula: a profile
//  speed (enum speed6) of 9 means 9*5 = 45 km/h. Typing "45" into such a
//  field built a frame saying 45, a reserved code. With the unit typed,
//  "45 km/h", the value is the code that means 45 km/h; a speed no code
//  means ("47 km/h" in 5 km/h steps) is refused by name, never rounded.
//  The unit must be the field's own: "1.2 km" into a field in metres is
//  refused, not converted. A bare number is still the raw value.
// =============================================================================

#include <QString>
#include <QVector>

namespace EngUnits {

// One <map from= to= formula= unit=> of an enum.
struct Range {
    qint64  from = 0, to = 0;
    QString formula;   // "v*5", "v-31", "v"
    QString unit;      // "km/h"
};

// The formula at v: "v", "v*N", "v/N", "v+N", "v-N" (N an integer). False
// for any other form: then it is not offered for typing.
bool apply(const QString &formula, qint64 v, double *out);

// Is there a unit after the number ("45 km/h", "45km/h")?
bool hasUnit(const QString &text);

// "45 km/h" -> the code meaning it, from the first range with that unit and
// a code giving exactly that value. False, with the reason in plain words.
bool toRaw(const QVector<Range> &ranges, const QString &text, qint64 *raw, QString *why);

// A field carried as it is, in `unit`: "1200 m" -> 1200. False, with the
// reason, for another unit or a value that is not a whole number.
bool plainToRaw(const QString &unit, const QString &text, qint64 *raw, QString *why);

// "45 km/h" for a code inside one of the ranges, empty otherwise.
QString text(const QVector<Range> &ranges, qint64 raw);

}  // namespace EngUnits

#endif  // ENGUNITS_H
