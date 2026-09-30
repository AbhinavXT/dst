#include "fieldsweep.h"
#include <limits>

#include <QSet>

namespace FieldSweep {

qint64 maxValue(int bits, bool isSigned)
{
    if (bits <= 0) { return 0; }
    if (bits >= 64) { return std::numeric_limits<qint64>::max(); }
    return isSigned ? ((qint64(1) << (bits - 1)) - 1)
                    : ((qint64(1) << bits) - 1);
}

qint64 minValue(int bits, bool isSigned)
{
    if (bits <= 0 || !isSigned) { return 0; }
    if (bits >= 64) { return std::numeric_limits<qint64>::min(); }
    return -(qint64(1) << (bits - 1));
}

QVector<Step> plan(const Spec &s, QString *err)
{
    auto fail = [&](const QString &m) {
        if (err) { *err = m; }
        return QVector<Step>();
    };
    if (err) { err->clear(); }
    if (s.field.isEmpty()) { return fail(QStringLiteral("no field chosen")); }

    QVector<Step> out;
    // Values repeat across the interesting cases of a narrow field -- for a
    // 1-bit field min, 0, 1 and max are the same two values -- and sending the
    // same value twice teaches nothing while making the results table lie
    // about coverage.
    QSet<qint64> seen;
    auto add = [&](qint64 v, const QString &note) {
        if (seen.contains(v)) { return; }
        seen.insert(v);
        out.push_back({ v, note });
    };

    switch (s.mode) {
    case Mode::Boundary: {
        if (s.bits <= 0) { return fail(QStringLiteral("field width is unknown")); }
        const qint64 lo = minValue(s.bits, s.isSigned);
        const qint64 hi = maxValue(s.bits, s.isSigned);
        add(lo, QStringLiteral("min"));
        if (lo != hi) { add(lo + 1, QStringLiteral("min+1")); }
        if (s.isSigned) {
            add(-1, QStringLiteral("-1 (all ones)"));
            add(0,  QStringLiteral("zero"));
            add(1,  QStringLiteral("one"));
        }
        add((lo + hi) / 2, QStringLiteral("midpoint"));
        if (hi - 1 > lo) { add(hi - 1, QStringLiteral("max-1")); }
        add(hi, QStringLiteral("max"));
        break;
    }
    case Mode::Range: {
        if (s.step == 0) { return fail(QStringLiteral("step cannot be zero")); }
        if ((s.to - s.from) / s.step < 0) {
            return fail(QStringLiteral("step runs away from the end value"));
        }
        // Count before generating: a 17-bit field swept by 1 is 131072 sends,
        // which at any sane interval is hours. Refuse with the number rather
        // than queueing it.
        const qint64 count = (s.to - s.from) / s.step + 1;
        if (count > s.maxValues) {
            return fail(QStringLiteral("%1 values — more than the %2 allowed; "
                                       "widen the step or narrow the range")
                            .arg(count).arg(s.maxValues));
        }
        for (qint64 v = s.from; s.step > 0 ? v <= s.to : v >= s.to; v += s.step) {
            add(v, QString());
        }
        break;
    }
    case Mode::List: {
        if (s.list.isEmpty()) { return fail(QStringLiteral("no values listed")); }
        if (s.list.size() > s.maxValues) {
            return fail(QStringLiteral("%1 values — more than the %2 allowed")
                            .arg(s.list.size()).arg(s.maxValues));
        }
        for (qint64 v : s.list) { add(v, QString()); }
        break;
    }
    case Mode::EnumCodes: {
        if (s.enumChoices.isEmpty()) {
            return fail(QStringLiteral("that field has no declared enum values"));
        }
        for (const auto &c : s.enumChoices) { add(c.first, c.second); }
        // The undefined code is the point of this mode: a target that accepts
        // a value the spec never defined is the bug worth finding, and it is
        // not reachable by sweeping only the declared ones.
        if (s.bits > 0) {
            const qint64 hi = maxValue(s.bits, s.isSigned);
            for (qint64 v = 0; v <= hi; ++v) {
                if (!seen.contains(v)) {
                    add(v, QStringLiteral("undefined code"));
                    break;
                }
            }
        }
        break;
    }
    }

    if (out.isEmpty()) { return fail(QStringLiteral("nothing to send")); }

    // Anything outside the field's width would be silently truncated on the
    // way to the wire, so the value sent would not be the value tested.
    if (s.bits > 0) {
        const qint64 lo = minValue(s.bits, s.isSigned);
        const qint64 hi = maxValue(s.bits, s.isSigned);
        for (const Step &st : out) {
            if (st.value < lo || st.value > hi) {
                return fail(QStringLiteral("%1 does not fit in %2 bits (%3..%4)")
                                .arg(st.value).arg(s.bits).arg(lo).arg(hi));
            }
        }
    }
    return out;
}

Verdict classify(const QVector<Observation> &obs, const QSet<QString> &replyTypes,
                 int windowMs)
{
    bool sawReply = false, sawFault = false, sawAnything = false;
    for (const Observation &o : obs) {
        if (o.msAfterSend < 0 || o.msAfterSend > windowMs) { continue; }
        sawAnything = true;
        if (!replyTypes.isEmpty() && replyTypes.contains(o.captype.toLower())) {
            sawReply = true;
        }
        if (o.isFault) { sawFault = true; }
    }
    if (sawReply)    { return Verdict::Reply; }
    if (sawFault)    { return Verdict::FaultOnly; }
    if (sawAnything) { return Verdict::Other; }
    return Verdict::Silent;
}

QString verdictName(Verdict v)
{
    switch (v) {
    case Verdict::Pending:   return QStringLiteral("waiting…");
    case Verdict::Reply:     return QStringLiteral("reply");
    case Verdict::FaultOnly: return QStringLiteral("fault only");
    case Verdict::Other:     return QStringLiteral("other traffic");
    case Verdict::Silent:    return QStringLiteral("silent");
    }
    return QString();
}

}  // namespace FieldSweep
