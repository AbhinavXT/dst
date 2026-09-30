#include "livefields.h"

#include <QObject>

#include <algorithm>

QString LiveFieldRef::serialise() const
{
    QString safeLabel = label;
    safeLabel.remove(QLatin1Char('|'));
    QString out = safeLabel + QLatin1Char('|') + sources.join(QLatin1Char(','));
    if (rule.isSet()) {
        out += QLatin1Char('|') + rule.serialise();
    }
    return out;
}

LiveFieldRef LiveFieldRef::parse(const QString &text)
{
    LiveFieldRef ref;
    const int bar = text.indexOf(QLatin1Char('|'));
    if (bar <= 0) {
        return ref;
    }
    ref.label = text.left(bar);
    const QString rest = text.mid(bar + 1);
    const int second = rest.indexOf(QLatin1Char('|'));
    ref.sources = (second < 0 ? rest : rest.left(second)).split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (second >= 0) {
        ref.rule = TileRule::parse(rest.mid(second + 1));
    }
    return ref;
}

int TileRule::level(double value, bool haveRef, double ref) const
{
    switch (kind) {
    case Kind::None:
        return 0;
    case Kind::Fixed:
        if (above) {
            if (value >= alarm) return 2;
            if (value >= warn) return 1;
        } else {
            if (value <= alarm) return 2;
            if (value <= warn) return 1;
        }
        return 0;
    case Kind::Relative:
        if (!haveRef) return 0;
        if (value > ref) return 2;
        if (value >= ref - margin) return 1;
        return 0;
    }
    return 0;
}

QString TileRule::serialise() const
{
    switch (kind) {
    case Kind::None:
        return QString();
    case Kind::Fixed:
        return QStringLiteral("fixed:%1:%2:%3").arg(above ? QStringLiteral("above") : QStringLiteral("below"))
            .arg(warn, 0, 'g', 10).arg(alarm, 0, 'g', 10);
    case Kind::Relative:
        return QStringLiteral("rel:%1:%2").arg(refSource).arg(margin, 0, 'g', 10);
    }
    return QString();
}

TileRule TileRule::parse(const QString &text)
{
    TileRule r;
    const QStringList p = text.split(QLatin1Char(':'));
    bool ok1 = false, ok2 = false;
    if (p.size() == 4 && p.at(0) == QLatin1String("fixed")) {
        r.above = p.at(1) != QLatin1String("below");
        r.warn = p.at(2).toDouble(&ok1);
        r.alarm = p.at(3).toDouble(&ok2);
        if (ok1 && ok2) r.kind = Kind::Fixed;
    } else if (p.size() == 4 && p.at(0) == QLatin1String("rel")) {
        // rel:<token>:<field>:<margin> — the source itself contains a colon.
        r.refSource = p.at(1) + QLatin1Char(':') + p.at(2);
        r.margin = p.at(3).toDouble(&ok1);
        if (ok1 && !p.at(1).isEmpty() && !p.at(2).isEmpty()) r.kind = Kind::Relative;
    }
    if (r.kind == Kind::None) r = TileRule();
    return r;
}

QString TileRule::describe() const
{
    switch (kind) {
    case Kind::None:
        return QString();
    case Kind::Fixed:
        return above ? QObject::tr("amber at %1, red at %2 and above").arg(warn).arg(alarm)
                     : QObject::tr("amber at %1, red at %2 and below").arg(warn).arg(alarm);
    case Kind::Relative:
        return QObject::tr("amber within %1 of %2, red above it").arg(margin).arg(refSource);
    }
    return QString();
}

namespace LiveFields {

QString tokenFor(CapType type)
{
    // typeLabel() is what the tabs show ("nms hlth"); the token is what a
    // capture line starts with ("nmshlth"). Try both, keep whichever reads
    // back as the same type.
    QString label = QString::fromLatin1(CaptureDecoder::typeLabel(type));
    if (CaptureDecoder::typeFromToken(label) == type) {
        return label;
    }
    label.remove(QLatin1Char(' '));
    if (CaptureDecoder::typeFromToken(label) == type) {
        return label;
    }
    return QStringLiteral("#%1").arg(static_cast<int>(type));
}

bool parseSource(const QString &source, CapType *type, QString *field)
{
    const int colon = source.indexOf(QLatin1Char(':'));
    if (colon <= 0 || colon == source.size() - 1) {
        return false;
    }
    const QString token = source.left(colon);
    if (token.startsWith(QLatin1Char('#'))) {
        bool ok = false;
        const int number = token.mid(1).toInt(&ok);
        if (!ok) {
            return false;
        }
        *type = static_cast<CapType>(number);
    } else {
        *type = CaptureDecoder::typeFromToken(token);
        if (*type == CapType::Unknown) {
            return false;
        }
    }
    *field = source.mid(colon + 1);
    return true;
}

QString sourceFor(CapType type, const QString &field)
{
    return tokenFor(type) + QLatin1Char(':') + field.trimmed();
}

QString valueIn(const QVector<FieldRow> &rows, const QString &field)
{
    const QString wanted = field.trimmed();
    for (const FieldRow &row : rows) {
        if (row.field.trimmed() == wanted) {
            return row.value;
        }
    }
    return QString();
}

bool isFrameNumber(const QString &field)
{
    return field.trimmed() == QLatin1String("FRAME_NUM");
}

QString frameClock(const QString &frameValue)
{
    bool ok = false;
    const int frame = frameValue.trimmed().section(QLatin1Char(' '), 0, 0).toInt(&ok);
    if (!ok || frame < 1 || frame > 86400) {
        return QString();
    }
    const int seconds = frame - 1;   // FRAME_NUM = seconds since midnight + 1
    return QStringLiteral("%1:%2:%3")
        .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QStringList byFreshness(const QStringList &sources, const QHash<int, qint64> &lastSeenMs)
{
    QVector<QPair<qint64, QString>> ranked;
    for (const QString &source : sources) {
        CapType type = CapType::Unknown;
        QString field;
        qint64 seen = 0;
        if (parseSource(source, &type, &field)) seen = lastSeenMs.value(int(type), 0);
        ranked.append(qMakePair(seen, source));
    }
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const QPair<qint64, QString> &a, const QPair<qint64, QString> &b) { return a.first > b.first; });
    QStringList out;
    for (const auto &r : ranked) out << r.second;
    return out;
}

QVector<LiveFieldRef> defaultBigNumbers()
{
    // The loco's own radio packets: LSRP, and ARP when LSRP is not there.
    return {
        // Speed turns amber within 5 km/h of the permitted speed the DMI
        // shows, red above it (session 82). No DMI: no colour, not a guess.
        { QStringLiteral("Speed"),       { QStringLiteral("lsrp:TRAIN_SPEED"), QStringLiteral("arp:TRAIN_SPEED") },
          TileRule::parse(QStringLiteral("rel:dmi:speed_limit_permissible:5")) },
        { QStringLiteral("Mode"),        { QStringLiteral("lsrp:LOCO_MODE"),   QStringLiteral("arp:LOCO_MODE") } },
        { QStringLiteral("Frame clock"), { QStringLiteral("lsrp:FRAME_NUM"),   QStringLiteral("arp:FRAME_NUM") } },
        { QStringLiteral("Brake"),       { QStringLiteral("lsrp:Brake_Applied") } },
    };
}

}  // namespace LiveFields
