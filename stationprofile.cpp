#include "stationprofile.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QStringList>

namespace StationProfile {

namespace {

QString tr(const char *s) { return QCoreApplication::translate("StationProfile", s); }

constexpr qint64 kMaxLen = (1 << 15) - 1;   // d: 15 bits

bool fail(QString *err, const QString &why)
{
    if (err) *err = why;
    return false;
}

bool speedRow(qint64 kmh, qint64 len, const QString &where, Row *out, QString *err)
{
    if (kmh % 5 != 0 || kmh < 0 || kmh > 250) return fail(err, tr("%1: %2 km/h is not a speed the SSP holds (5 km/h steps, up to 250)").arg(where).arg(kmh));
    if (len <= 0 || len > kMaxLen) return fail(err, tr("%1: a length of %2 m does not fit (1 to %3 m)").arg(where).arg(len).arg(kMaxLen));
    *out = Row{ { QStringLiteral("d"), len }, { QStringLiteral("class"), 0 }, { QStringLiteral("u"), kmh / 5 } };
    return true;
}

}  // namespace

QVector<Row> parseSsp(const QString &text, QString *err)
{
    QVector<Row> out;
    static const QRegularExpression group(QStringLiteral(R"(\(\s*([A-Za-z])\s*,\s*(\d+)\s*,\s*(\d+)\s*\))"));
    auto it = group.globalMatch(text);
    if (it.hasNext()) {
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            const QString where = m.captured(0);
            if (m.captured(1).toUpper() != QLatin1String("U")) {
                if (err) *err = tr("%1: category speeds (A, B, C) are given in the table, not pasted").arg(where);
                return {};
            }
            Row r;
            if (!speedRow(m.captured(2).toLongLong(), m.captured(3).toLongLong(), where, &r, err)) return {};
            out << r;
        }
        return out;
    }
    static const QRegularExpression line(QStringLiteral(R"(^\s*(\d+)\s*(?:m)?[\s,;\t]+(\d+)\s*(?:km/?h|kmph)?\s*$)"), QRegularExpression::CaseInsensitiveOption);
    for (const QString &l : text.split(QLatin1Char('\n'))) {
        if (l.trimmed().isEmpty()) continue;
        const QRegularExpressionMatch m = line.match(l);
        if (!m.hasMatch()) {
            if (err) *err = tr("\"%1\" is neither (U,speed,length) nor \"length speed\"").arg(l.trimmed());
            return {};
        }
        Row r;
        if (!speedRow(m.captured(2).toLongLong(), m.captured(1).toLongLong(), l.trimmed(), &r, err)) return {};
        out << r;
    }
    if (out.isEmpty() && err) *err = tr("no SSP rows in it");
    return out;
}

QVector<Row> parseGradient(const QString &text, QString *err)
{
    QVector<Row> out;
    // "F 1 in 209 1245", "(F 1 in 150, 445)", "R 1 in 260 105", "LEVEL 320", "(Level,320)".
    static const QRegularExpression slope(QStringLiteral(R"(^\(?\s*(F|R|FALL|RISE)\s*1\s*in\s*(\d+(?:\.\d+)?)\s*[,;\t ]\s*(\d+)\s*\)?$)"),
                                          QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression level(QStringLiteral(R"(^\(?\s*(LEVEL|L)\s*[,;\t ]\s*(\d+)\s*\)?$)"), QRegularExpression::CaseInsensitiveOption);
    QStringList items;
    for (const QString &l : text.split(QLatin1Char('\n'))) {
        // Several "( … )" on one line: each its own.
        const QString t = l.trimmed();
        if (t.isEmpty()) continue;
        if (t.startsWith(QLatin1Char('(')) && t.count(QLatin1Char('(')) > 1) {
            static const QRegularExpression paren(QStringLiteral(R"(\([^)]*\))"));
            for (auto it = paren.globalMatch(t); it.hasNext();) items << it.next().captured(0);
        } else {
            items << t;
        }
    }
    for (const QString &t : items) {
        Row r;
        QRegularExpressionMatch m = slope.match(t);
        if (m.hasMatch()) {
            const double n = m.captured(2).toDouble();
            const qint64 len = m.captured(3).toLongLong();
            if (n < 1) { if (err) *err = tr("%1: 1 in %2 is not a gradient").arg(t, m.captured(2)); return {}; }
            const qint64 value = qint64(1000.0 / n);
            if (value > 31) { if (err) *err = tr("%1: %2 per thousand does not fit the 5-bit value (31 at most)").arg(t).arg(value); return {}; }
            if (len <= 0 || len > kMaxLen) { if (err) *err = tr("%1: a length of %2 m does not fit (1 to %3 m)").arg(t).arg(len).arg(kMaxLen); return {}; }
            const bool rise = m.captured(1).toUpper().startsWith(QLatin1Char('R'));
            r = Row{ { QStringLiteral("d"), len }, { QStringLiteral("dir"), rise ? 1 : 0 }, { QStringLiteral("value"), value } };
        } else if ((m = level.match(t)).hasMatch()) {
            const qint64 len = m.captured(2).toLongLong();
            if (len <= 0 || len > kMaxLen) { if (err) *err = tr("%1: a length of %2 m does not fit (1 to %3 m)").arg(t).arg(len).arg(kMaxLen); return {}; }
            r = Row{ { QStringLiteral("d"), len }, { QStringLiteral("dir"), 0 }, { QStringLiteral("value"), 0 } };
        } else {
            if (err) *err = tr("\"%1\" is not a gradient (F 1 in N length, R 1 in N length, LEVEL length)").arg(t);
            return {};
        }
        out << r;
    }
    if (out.isEmpty() && err) *err = tr("no gradient rows in it");
    return out;
}

qint64 covered(const QVector<Row> &rows)
{
    qint64 sum = 0;
    for (const Row &r : rows) sum += r.value(QStringLiteral("d"));
    return sum;
}

QVector<Row> cover(const QVector<Row> &rows, qint64 length, Row fill, QString *err)
{
    const qint64 have = covered(rows);
    if (have > length) { if (err) *err = tr("the rows already cover %1 m of the route's %2 m: take some off first").arg(have).arg(length); return rows; }
    if (have == length) { if (err) *err = tr("the rows already cover the route's %1 m").arg(length); return rows; }
    if (length - have > kMaxLen) { if (err) *err = tr("%1 m left: more than one row holds (%2 m)").arg(length - have).arg(kMaxLen); return rows; }
    fill.insert(QStringLiteral("d"), length - have);
    QVector<Row> out = rows;
    out << fill;
    return out;
}

bool split(QVector<Row> *rows, qint64 at, QString *err)
{
    qint64 from = 0;
    for (int i = 0; i < rows->size(); ++i) {
        const qint64 d = rows->at(i).value(QStringLiteral("d"));
        if (at > from && at < from + d) {
            Row a = rows->at(i), b = rows->at(i);
            a.insert(QStringLiteral("d"), at - from);
            b.insert(QStringLiteral("d"), from + d - at);
            (*rows)[i] = a;
            rows->insert(i + 1, b);
            return true;
        }
        if (at == from || at == from + d) return fail(err, tr("%1 m is already where a row ends").arg(at));
        from += d;
    }
    return fail(err, tr("%1 m is past the rows (they cover %2 m)").arg(at).arg(from));
}

QString speedText(const Row &r)
{
    if (r.value(QStringLiteral("class")) == 1)
        return tr("A %1, B %2, C %3 km/h").arg(r.value(QStringLiteral("a")) * 5).arg(r.value(QStringLiteral("b")) * 5).arg(r.value(QStringLiteral("c")) * 5);
    const qint64 u = r.value(QStringLiteral("u"));
    if (u == 0) return tr("dead stop");
    if (u > 50) return tr("code %1").arg(u);
    return tr("%1 km/h").arg(u * 5);
}

QString gradientText(const Row &r)
{
    const qint64 v = r.value(QStringLiteral("value"));
    if (v == 0) return tr("Level");
    return tr("%1 %2 ‰ (about 1 in %3)").arg(r.value(QStringLiteral("dir")) ? QStringLiteral("R") : QStringLiteral("F")).arg(v).arg(1000 / v);
}

}  // namespace StationProfile
