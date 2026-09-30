#include "keyblock.h"

#include <QDateTime>
#include <QRegularExpression>

namespace KeyBlock {

QString KeySet::startText() const
{
    if (startY < 0) return QString();
    return QStringLiteral("20%1-%2-%3 %4:00")
        .arg(startY, 2, 10, QChar('0')).arg(startM, 2, 10, QChar('0'))
        .arg(startD, 2, 10, QChar('0')).arg(startH, 2, 10, QChar('0'));
}
QString KeySet::endText() const
{
    if (endY < 0) return QString();
    return QStringLiteral("20%1-%2-%3 %4:00")
        .arg(endY, 2, 10, QChar('0')).arg(endM, 2, 10, QChar('0'))
        .arg(endD, 2, 10, QChar('0')).arg(endH, 2, 10, QChar('0'));
}

double Result::daysRemaining(bool *ok) const
{
    if (ok) *ok = false;
    if (currentEpoch <= 0) return 0.0;
    // The set that is currently valid; if none is, the one that ends last,
    // so an already-expired block reports how long ago rather than nothing.
    qint64 best = 0;
    for (const KeySet &s : sets) {
        if (s.endEpoch <= 0) continue;
        if (s.startEpoch > 0 && currentEpoch >= s.startEpoch
            && currentEpoch <= s.endEpoch) {
            best = s.endEpoch;
            break;
        }
        if (s.endEpoch > best) best = s.endEpoch;
    }
    if (best <= 0) return 0.0;
    if (ok) *ok = true;
    return double(best - currentEpoch) / 86400.0;
}

bool isBlockStart(const QString &line)
{
    return line.trimmed().startsWith(QLatin1String("AUTH_KEYS"));
}

int expectedLineCount()
{
    // marker + 4 key lines + 4 KEY IDX time lines + 1 epoch line.
    // The epoch line is optional in practice, so the assembler treats this
    // as a maximum rather than a requirement.
    return 10;
}

Result parse(const QStringList &lines)
{
    Result r;
    if (lines.isEmpty() || !isBlockStart(lines.first())) {
        r.error = QStringLiteral("not an AUTH_KEYS block");
        return r;
    }

    // ---- the four hex lines: 16 bytes each, TWO per set ----------------
    QVector<QByteArray> keys;
    static const QRegularExpression hexLine(
        QStringLiteral("^\\s*(?:[0-9A-Fa-f]{2}[\\s]+){15}[0-9A-Fa-f]{2}\\s*$"));

    for (int i = 1; i < lines.size(); ++i) {
        const QString ln = lines.at(i);
        if (!hexLine.match(ln).hasMatch()) continue;
        QByteArray k;
        for (const QString &tok : ln.split(QRegularExpression("\\s+"),
                                           Qt::SkipEmptyParts)) {
            bool ok = false;
            const int b = tok.toInt(&ok, 16);
            if (ok) k.append(char(b));
        }
        if (k.size() == 16) keys.append(k);
    }

    // Pair them: key[2] per KEY_SET_INFO. Reading these as four independent
    // keys would attach every key to the wrong validity window.
    for (int i = 0; i + 1 < keys.size(); i += 2) {
        KeySet s;
        s.index = i / 2;
        s.key0  = keys.at(i);
        s.key1  = keys.at(i + 1);
        r.sets.append(s);
    }

    auto setAt = [&r](int idx) -> KeySet * {
        for (KeySet &s : r.sets) if (s.index == idx) return &s;
        // Times can be printed for a set whose keys were not captured.
        r.sets.append(KeySet{});
        r.sets.last().index = idx;
        return &r.sets.last();
    };

    // ---- "KEY IDX: [0] START TIME: [26][5][24][0]" ---------------------
    static const QRegularExpression timeLine(
        QStringLiteral("KEY[ _]?IDX:\\s*\\[(\\d+)\\]\\s*(START|END)\\s+TIME:\\s*"
                       "\\[(\\d+)\\]\\[(\\d+)\\]\\[(\\d+)\\]\\[(\\d+)\\]"));
    // ---- "KEY_IDX: [0] START: [1779580800] END:[1788220800] CURR:[...]"
    static const QRegularExpression epochLine(
        QStringLiteral("KEY[ _]?IDX:\\s*\\[(\\d+)\\]\\s*START:\\s*\\[(\\d+)\\]"
                       "\\s*END:\\s*\\[(\\d+)\\](?:\\s*CURR:\\s*\\[(\\d+)\\])?"));

    for (int i = 1; i < lines.size(); ++i) {
        const QString ln = lines.at(i);

        auto em = epochLine.match(ln);
        if (em.hasMatch()) {
            KeySet *s = setAt(em.captured(1).toInt());
            s->startEpoch = em.captured(2).toLongLong();
            s->endEpoch   = em.captured(3).toLongLong();
            if (!em.captured(4).isEmpty()) {
                r.currentEpoch = em.captured(4).toLongLong();
            }
            continue;
        }

        auto tm = timeLine.match(ln);
        if (!tm.hasMatch()) continue;
        KeySet *s = setAt(tm.captured(1).toInt());
        const bool isStart = (tm.captured(2) == QLatin1String("START"));
        const int y = tm.captured(3).toInt(), mo = tm.captured(4).toInt();
        const int d = tm.captured(5).toInt(), h  = tm.captured(6).toInt();
        if (isStart) { s->startY = y; s->startM = mo; s->startD = d; s->startH = h; }
        else         { s->endY   = y; s->endM   = mo; s->endD   = d; s->endH   = h; }
    }

    // Fill epochs from the printed [yy][mm][dd][hh] where the trailing line
    // did not supply them, so every set reports a comparable time.
    for (KeySet &s : r.sets) {
        auto toEpoch = [](int y, int mo, int d, int h) -> qint64 {
            if (y < 0) return 0;
            const QDateTime dt(QDate(2000 + y, mo, d), QTime(h, 0), Qt::UTC);
            return dt.isValid() ? dt.toSecsSinceEpoch() : 0;
        };
        if (s.startEpoch <= 0) s.startEpoch = toEpoch(s.startY, s.startM, s.startD, s.startH);
        if (s.endEpoch   <= 0) s.endEpoch   = toEpoch(s.endY,   s.endM,   s.endD,   s.endH);
    }

    r.valid = !r.sets.isEmpty();
    if (!r.valid) r.error = QStringLiteral("no key sets found in the block");
    return r;
}

QVector<FieldRow> describe(const Result &r)
{
    QVector<FieldRow> rows;
    if (!r.valid) {
        rows.push_back({ QStringLiteral("AUTH_KEYS"),
                         r.error.isEmpty() ? QStringLiteral("(unparsed)") : r.error });
        return rows;
    }

    rows.push_back({ QStringLiteral("AUTH_KEYS"),
                     QStringLiteral("%1 key set(s), 2 keys each").arg(r.sets.size()) });

    if (r.currentEpoch > 0) {
        rows.push_back({ QStringLiteral("  key_time_now"),
                         QDateTime::fromSecsSinceEpoch(r.currentEpoch, Qt::UTC)
                             .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) + "Z" });
    }

    bool ok = false;
    const double days = r.daysRemaining(&ok);
    if (ok) {
        // Rendered so it compares numerically AND reads plainly:
        // field:key_days_remaining<7 is the query worth having.
        rows.push_back({ QStringLiteral("  key_days_remaining"),
                         QStringLiteral("%1 (%2)")
                             .arg(days, 0, 'f', 2)
                             .arg(days < 0 ? QStringLiteral("EXPIRED")
                                           : days < kWarnDays
                                                 ? QStringLiteral("expiring soon")
                                                 : QStringLiteral("ok")) });
    }

    for (const KeySet &s : r.sets) {
        const QString p = QStringLiteral("  set %1").arg(s.index);
        rows.push_back({ p + QStringLiteral(" valid_from"),
                         s.startText().isEmpty() ? QStringLiteral("—") : s.startText() });
        rows.push_back({ p + QStringLiteral(" valid_to"),
                         s.endText().isEmpty() ? QStringLiteral("—") : s.endText() });
        if (s.hasKeys()) {
            rows.push_back({ p + QStringLiteral(" key 0"),
                             QString::fromLatin1(s.key0.toHex(' ')).toUpper() });
            rows.push_back({ p + QStringLiteral(" key 1"),
                             QString::fromLatin1(s.key1.toHex(' ')).toUpper() });
        } else {
            rows.push_back({ p + QStringLiteral(" keys"),
                             QStringLiteral("(not captured)") });
        }
    }
    return rows;
}

}  // namespace KeyBlock
