#include "engunits.h"

#include <QObject>
#include <QRegularExpression>
#include <QStringList>

#include <cmath>

namespace {
// "45 km/h" -> 45, "km/h". False when there is no number in front.
bool split(const QString &text, double *value, QString *unit)
{
    static const QRegularExpression re(QStringLiteral("^\\s*(-?\\d+(?:\\.\\d+)?)\\s*(.*?)\\s*$"));
    const auto m = re.match(text);
    if (!m.hasMatch()) return false;
    *value = m.captured(1).toDouble();
    *unit = m.captured(2);
    return true;
}

QString number(double v)
{
    return v == std::floor(v) ? QString::number(qint64(v)) : QString::number(v);
}
}  // namespace

bool EngUnits::apply(const QString &formula, qint64 v, double *out)
{
    const QString f = QString(formula).remove(QLatin1Char(' '));
    if (f == QLatin1String("v")) { *out = double(v); return true; }
    static const QRegularExpression re(QStringLiteral("^v([*/+-])(\\d+)$"));
    const auto m = re.match(f);
    if (!m.hasMatch()) return false;
    const double n = m.captured(2).toDouble();
    switch (m.captured(1).at(0).toLatin1()) {
    case '*': *out = v * n; return true;
    case '/': if (n == 0) return false; *out = v / n; return true;
    case '+': *out = v + n; return true;
    case '-': *out = v - n; return true;
    }
    return false;
}

bool EngUnits::hasUnit(const QString &text)
{
    double v = 0; QString unit;
    return split(text, &v, &unit) && !unit.isEmpty();
}

bool EngUnits::toRaw(const QVector<Range> &ranges, const QString &text, qint64 *raw, QString *why)
{
    double want = 0; QString unit;
    if (!split(text, &want, &unit) || unit.isEmpty()) {
        if (why) *why = QObject::tr("\"%1\" is not a number with a unit").arg(text.trimmed());
        return false;
    }
    QStringList offered;
    for (const Range &r : ranges) {
        if (r.unit.compare(unit, Qt::CaseInsensitive) != 0) continue;
        double lo = 0, hi = 0;
        if (!apply(r.formula, r.from, &lo) || !apply(r.formula, r.to, &hi)) continue;
        offered << QStringLiteral("%1–%2 %3").arg(number(lo), number(hi), r.unit);
        for (qint64 v = r.from; v <= r.to; ++v) {
            double got = 0;
            if (apply(r.formula, v, &got) && std::abs(got - want) < 1e-9) { *raw = v; return true; }
        }
    }
    if (why) {
        *why = offered.isEmpty()
                   ? QObject::tr("this field is not given in %1").arg(unit)
                   : QObject::tr("%1 %2 is not a value this field can carry (it carries %3)")
                         .arg(number(want), unit, offered.join(QStringLiteral(", ")));
    }
    return false;
}

bool EngUnits::plainToRaw(const QString &unit, const QString &text, qint64 *raw, QString *why)
{
    double v = 0; QString typed;
    if (!split(text, &v, &typed)) {
        if (why) *why = QObject::tr("\"%1\" is not a number").arg(text.trimmed());
        return false;
    }
    if (!typed.isEmpty() && typed.compare(unit, Qt::CaseInsensitive) != 0) {
        if (why) *why = unit.isEmpty() ? QObject::tr("this field has no unit; type the number alone")
                                       : QObject::tr("this field is in %1, not %2").arg(unit, typed);
        return false;
    }
    if (v != std::floor(v)) {
        if (why) *why = QObject::tr("%1 is not a whole number of %2").arg(number(v), unit);
        return false;
    }
    *raw = qint64(v);
    return true;
}

QString EngUnits::text(const QVector<Range> &ranges, qint64 raw)
{
    for (const Range &r : ranges) {
        double v = 0;
        if (raw >= r.from && raw <= r.to && apply(r.formula, raw, &v))
            return QStringLiteral("%1 %2").arg(number(v), r.unit).trimmed();
    }
    return QString();
}
