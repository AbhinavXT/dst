#include "dmidiff.h"

#include "dmipanel.h"

#include <QRegularExpression>
#include <QStringList>

namespace DmiDiff {

QString Item::text() const
{
    return QStringLiteral("%1 %2 → %3").arg(label, a.isEmpty() ? QStringLiteral("—") : a,
                                             b.isEmpty() ? QStringLiteral("—") : b);
}

QString brakeName(int t)
{
    switch (t) {
    case 0: return QStringLiteral("none");
    case 1: return QStringLiteral("overspeed warning");
    case 2: return QStringLiteral("NB");
    case 3: return QStringLiteral("FSB");
    case 4: return QStringLiteral("EB");
    case 5: return QStringLiteral("LE");
    default: return QString::number(t);
    }
}

namespace {

QString kmh(bool has, int v) { return has ? QStringLiteral("%1 km/h").arg(v) : QString(); }
QString metres(int v) { return v ? QStringLiteral("%1 m").arg(v) : QString(); }

// One state as (key, label, value, continuous), in panel order.
struct Field { const char *key; const char *label; QString value; bool continuous; };

QVector<Field> fieldsOf(const DmiState &s)
{
    QString tag;
    if (s.tagId) tag = QStringLiteral("%1%2").arg(s.tagId).arg(s.tagDir.isEmpty() ? QString() : QLatin1Char(' ') + s.tagDir);
    return {
        { "mode", "Mode", s.modeText.isEmpty() && s.mode ? QString::number(s.mode) : s.modeText, false },
        { "brake", "Brake", brakeName(s.brakeType), false },
        { "speed", "Speed", QStringLiteral("%1 km/h").arg(s.speed), true },
        { "permitted", "Permitted speed", kmh(s.hasPermitted, s.permitted), false },
        { "target_speed", "Target speed", kmh(s.hasTargetSpeed, s.targetSpeed), false },
        { "target_type", "Target", s.targetType, false },
        { "target_distance", "Target distance", metres(s.targetDistance), true },
        { "section_speed", "Section speed", s.sectionSpeed ? QStringLiteral("%1 km/h").arg(s.sectionSpeed) : QString(), false },
        { "ma", "Movement authority", metres(s.movementAuthority), true },
        { "aspect", "Signal aspect", s.aspect ? QString::number(s.aspect) : QString(), false },
        { "signal", "Signal", s.signalName, false },
        { "signal_distance", "Signal distance", metres(s.signalDistance), true },
        { "disc", "Disc", s.disc, false },
        { "route", "Route", s.route ? QString::number(s.route) : QString(), false },
        { "system", "System messages", s.systemMessages.join(QStringLiteral("; ")), false },
        { "context", "Messages", s.contextMessages.join(QStringLiteral("; ")), false },
        { "tag", "Tag", tag, false },
        { "next_tag", "Next tag", metres(s.nextTagDistance), true },
        { "location", "Location", QStringLiteral("%1 km").arg(s.locationKm, 0, 'f', 3), true },
        { "train_length", "Train length", metres(s.trainLength), false },
        { "radio", "Radio", QStringLiteral("%1 bar(s)").arg(s.rfBars), false },
        { "loco", "Loco", s.locoId, false },
    };
}

}  // namespace

QVector<Item> compare(const DmiState &a, const DmiState &b)
{
    QVector<Item> out;
    if (!a.valid && !b.valid) return out;
    if (!a.valid || !b.valid) {
        out.append({ QStringLiteral("dmi"), QStringLiteral("DMI"), a.valid ? QStringLiteral("a frame") : QStringLiteral("no frame"),
                     b.valid ? QStringLiteral("a frame") : QStringLiteral("no frame"), false });
        return out;
    }
    const QVector<Field> fa = fieldsOf(a), fb = fieldsOf(b);
    // A message that counts down ("End of Authority in 3250m" → "… 3242m",
    // "PSR in 0143m with speed limit 80Kmph" → "… 0139m …") changes every
    // frame while the train moves: the same message, its numbers moving. It
    // is marked as moving, like speed. Its coming and going is a change.
    static const QRegularExpression digits(QStringLiteral("\\d+"));
    auto counting = [](const QString &x, const QString &y) {
        if (x.isEmpty() || y.isEmpty()) return false;
        QString a = x, b = y;
        a.replace(digits, QStringLiteral("#"));
        b.replace(digits, QStringLiteral("#"));
        return a == b;
    };
    for (int i = 0; i < fa.size(); ++i) {
        if (fa.at(i).value == fb.at(i).value) continue;
        const QString key = QLatin1String(fa.at(i).key);
        const bool moving = fa.at(i).continuous
                            || ((key == QLatin1String("system") || key == QLatin1String("context"))
                                && counting(fa.at(i).value, fb.at(i).value));
        out.append({ key, QLatin1String(fa.at(i).label), fa.at(i).value, fb.at(i).value, moving });
    }
    return out;
}

}  // namespace DmiDiff
