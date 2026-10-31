#ifndef STATIONPROFILE_H
#define STATIONPROFILE_H
// =====================================================================
//  stationprofile.{h,cpp} -- session 462: a route's speed and gradient
//  profile filled in as the firm's track profile sheet writes it.
//
//  SSP rows (StaticSpeedProfile "speed": d segment length, class 0, u =
//  km/h / 5) and gradient rows (GradientProfile "grad": d, dir 1 uphill /
//  0 downhill, value = 1000 / N truncated) chain from the route's entry
//  signal; together they must cover the route exactly, or the station
//  refuses the whole answer (StationKavach::checks says so).
//
//  PASTED, as HBL's sheets print them (LPI track profile table 2.0.3):
//    SSP       "(U,110,1670)" groups, or lines "<length> <km/h>";
//              category speeds (A/B/C) are refused by name: the table
//              takes them.
//    gradient  lines "F 1 in 209 1245", "R 1 in 260 105", "LEVEL 320",
//              or "(F 1 in 150, 445)": rise uphill, fall and level
//              downhill, the value 1000/N truncated (kavach_sheets.py's
//              rule; F 1 in 209 -> 4, R 1 in 260 -> 3, Level -> 0). Station
//              527's own SLRPs carry exactly these rows for S50-S48.
//  Refused by name: a speed not in 5 km/h steps or over 250 km/h, a length
//  past 15 bits, a value past its field, a line that is neither.
// =====================================================================
#include "stationkavach.h"

#include <QString>
#include <QVector>

namespace StationProfile {

using Row = StationKavach::Row;

QVector<Row> parseSsp(const QString &text, QString *err = nullptr);
QVector<Row> parseGradient(const QString &text, QString *err = nullptr);

qint64 covered(const QVector<Row> &rows);       // the rows' lengths added
// `rows` made to cover `length` m: `fill` (its d set) appended for what is
// left, or the whole route when there are none. Refused when they already
// cover more than the route.
QVector<Row> cover(const QVector<Row> &rows, qint64 length, Row fill, QString *err = nullptr);
// The row holding `at` m from the entry split into two there. Refused at a
// row's end or past the rows.
bool split(QVector<Row> *rows, qint64 at, QString *err = nullptr);

// In words for the helper columns: "110 km/h"; "F 1 in 250", "Level".
QString speedText(const Row &r);
QString gradientText(const Row &r);

}  // namespace StationProfile

#endif // STATIONPROFILE_H
