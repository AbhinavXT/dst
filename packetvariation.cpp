#include "packetvariation.h"

#include <QJsonObject>
#include <QRandomGenerator>

#include <limits>

namespace PacketVary {

qint64 maxForBits(int bits)
{
    if (bits <= 0)  { return 0; }
    if (bits >= 63) { return std::numeric_limits<qint64>::max(); }
    return (qint64(1) << bits) - 1;
}

qint64 Rule::valueFor(int sendIndex, int bits, const LiveFrame &live) const
{
    const qint64 width = maxForBits(bits);
    const qint64 i     = sendIndex < 0 ? 0 : qint64(sendIndex);

    qint64 v = start;

    switch (mode) {
    case Increment: {
        // The wrap point is the rule's own max when it has one, otherwise the
        // field's width. Working in the span rather than letting the value run
        // and masking afterwards keeps the sequence contiguous: masking a
        // runaway counter produces the same numbers but skips whichever ones
        // straddle the boundary.
        const qint64 top  = (max > start) ? max : width;
        const qint64 span = (top >= start) ? (top - start + 1) : 1;
        const qint64 s    = (step == 0) ? 1 : step;
        if (span <= 1) { v = start; break; }
        // Modulo before adding, so a large index cannot overflow on the way.
        const qint64 off = ((i % span) * (s % span)) % span;
        v = start + off;
        break;
    }
    case Sweep: {
        const qint64 top = (max > start) ? max : start;
        const qint64 s   = (step == 0) ? 1 : (step < 0 ? -step : step);
        const qint64 steps = (top - start) / s + 1;
        if (steps <= 1) { v = start; break; }
        v = start + s * (i % steps);
        break;
    }
    case Live: {
        // Not a function of the run index: every send carries the number the
        // equipment is using at that moment. With nothing observed the rule
        // falls back to `start`, which is at least a number the operator
        // chose rather than a zero dressed up as an observation.
        v = live.valid() ? live.value : start;
        // step 0 repeats the observed number for as long as it stands;
        // step n advances within it and re-anchors when it moves.
        if (step != 0 && live.valid()) { v += step * qint64(live.sendsSinceChange); }
        break;
    }
    case Random: {
        const qint64 top = (max > start) ? max : (width > start ? width : start);
        if (top <= start) { v = start; break; }
        // Genuinely random, not a function of the index: a rule meant to
        // produce noise that repeated identically per index would not be
        // producing noise. This is the one mode a run cannot reproduce from
        // the send index alone.
        const quint64 span = quint64(top - start) + 1;
        v = start + qint64(QRandomGenerator::global()->generate64() % span);
        break;
    }
    }

    if (bits > 0) {
        if (v < 0)      { v = 0; }
        if (v > width)  { v &= width; }
    }
    return v;
}

QString Rule::describe(int bits) const
{
    switch (mode) {
    case Increment: {
        const qint64 top = (max > start) ? max : maxForBits(bits);
        return QStringLiteral("%1: %2 then +%3, wrap at %4")
            .arg(field).arg(start).arg(step == 0 ? 1 : step).arg(top);
    }
    case Sweep:
        return QStringLiteral("%1: sweep %2..%3 step %4")
            .arg(field).arg(start).arg(max > start ? max : start)
            .arg(step == 0 ? 1 : step);
    case Random:
        return QStringLiteral("%1: random %2..%3")
            .arg(field).arg(start).arg(max > start ? max : maxForBits(bits));
    case Live:
        return step == 0
            ? QStringLiteral("%1: from live ARP/LSRP").arg(field)
            : QStringLiteral("%1: from live ARP/LSRP, +%2 per send until it moves")
                  .arg(field).arg(step);
    }
    return field;
}

QString modeName(Rule::Mode m)
{
    switch (m) {
    case Rule::Increment: return QStringLiteral("increment");
    case Rule::Sweep:     return QStringLiteral("sweep");
    case Rule::Random:    return QStringLiteral("random");
    case Rule::Live:      return QStringLiteral("live");
    }
    return QStringLiteral("increment");
}

Rule::Mode modeFromName(const QString &s)
{
    const QString t = s.trimmed().toLower();
    if (t == QLatin1String("sweep"))  { return Rule::Sweep; }
    if (t == QLatin1String("random")) { return Rule::Random; }
    if (t == QLatin1String("live"))   { return Rule::Live; }
    return Rule::Increment;
}

QJsonArray toJson(const QVector<Rule> &rules)
{
    QJsonArray arr;
    for (const Rule &r : rules) {
        QJsonObject o;
        o["field"]   = r.field;
        o["mode"]    = modeName(r.mode);
        o["start"]   = double(r.start);
        o["step"]    = double(r.step);
        o["max"]     = double(r.max);
        o["enabled"] = r.enabled;
        arr.append(o);
    }
    return arr;
}

QVector<Rule> fromJson(const QJsonArray &arr)
{
    QVector<Rule> out;
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        const QString field = o.value("field").toString();
        if (field.isEmpty()) { continue; }
        Rule r;
        r.field   = field;
        r.mode    = modeFromName(o.value("mode").toString());
        r.start   = qint64(o.value("start").toDouble());
        r.step    = qint64(o.value("step").toDouble(1));
        r.max     = qint64(o.value("max").toDouble());
        r.enabled = o.value("enabled").toBool(true);
        out.push_back(r);
    }
    return out;
}

}  // namespace PacketVary
