#include "rejectrules.h"

#include "schema/schemadecoder.h"

#include <QDebug>
#include <QDomDocument>
#include <QFile>

bool RejectRules::load(const QString &path, QString *error)
{
    auto fail = [&](const QString &why) {
        if (error) { *error = why; }
        return false;
    };

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return fail(QStringLiteral("cannot open %1").arg(path));
    }

    QDomDocument doc;
    QString msg;
    int line = 0, col = 0;
    if (!doc.setContent(&f, &msg, &line, &col)) {
        return fail(QStringLiteral("XML error at %1:%2 — %3").arg(line).arg(col).arg(msg));
    }

    const QDomElement root = doc.documentElement();
    if (root.tagName() != QLatin1String("rejectrules")) {
        return fail(QStringLiteral("root element is <%1>, expected <rejectrules>")
                        .arg(root.tagName()));
    }

    // Built into a local and swapped in only once the whole file parses.
    // A partially applied rule set would quietly stop reporting conditions
    // the operator still believes are being checked, which is worse than
    // refusing the file outright.
    QVector<Rule> parsed;
    for (QDomElement e = root.firstChildElement(QStringLiteral("rule"));
         !e.isNull(); e = e.nextSiblingElement(QStringLiteral("rule"))) {
        Rule r;
        r.clause = e.attribute(QStringLiteral("clause")).trimmed();
        r.field  = e.attribute(QStringLiteral("field")).trimmed();
        r.op     = e.attribute(QStringLiteral("op"), QStringLiteral("eq")).trimmed();
        r.doc    = e.attribute(QStringLiteral("doc")).trimmed();
        r.bits   = e.attribute(QStringLiteral("bits")).toInt();
        r.note   = e.attribute(QStringLiteral("note")).trimmed();
        r.when   = e.attribute(QStringLiteral("when")).trimmed();

        if (r.field.isEmpty()) {
            return fail(QStringLiteral("rule %1 names no field").arg(r.clause));
        }

        // Same grammar as the schema's when= (one condition language), and
        // refused the same way: a guard that does not parse would otherwise
        // evaluate as "holds" and let the rule fire where it should not.
        if (!r.when.isEmpty()) {
            const QString why = Schema::Decoder::checkCondition(r.when);
            if (!why.isEmpty()) {
                return fail(QStringLiteral("rule %1 (%2) has a bad when=\"%3\": %4")
                                .arg(r.clause, r.field, r.when, why));
            }
        }

        bool ok = false;
        if (r.op == QLatin1String("eq")) {
            r.value = e.attribute(QStringLiteral("value")).toLongLong(&ok);
            if (!ok) {
                return fail(QStringLiteral("rule %1 (%2) has no numeric value")
                                .arg(r.clause, r.field));
            }
            // A value the field cannot hold can never fire, and a rule that
            // can never fire reads on screen as a condition being checked
            // when it is not.
            if (r.bits > 0 && r.bits < 63
                && (r.value < 0 || r.value >= (qint64(1) << r.bits))) {
                return fail(QStringLiteral(
                                "rule %1 tests %2 == %3, which does not fit %4 bits")
                                .arg(r.clause, r.field)
                                .arg(r.value).arg(r.bits));
            }
        } else if (r.op == QLatin1String("ne_field")) {
            // Fires when two fields disagree. The identity rules need it:
            // "this SLRP is addressed to a different loco" is a comparison
            // against something the frame does not carry, supplied alongside
            // it. Keeping it a plain field comparison means the engine needs
            // no notion of identity at all.
            r.other = e.attribute(QStringLiteral("other")).trimmed();
            if (r.other.isEmpty()) {
                return fail(QStringLiteral("rule %1 (%2) has op ne_field but "
                                           "names no other field")
                                .arg(r.clause, r.field));
            }
        } else if (r.op == QLatin1String("outside")) {
            bool okMin = false, okMax = false;
            r.min = e.attribute(QStringLiteral("min")).toLongLong(&okMin);
            r.max = e.attribute(QStringLiteral("max")).toLongLong(&okMax);
            if (!okMin || !okMax || r.min > r.max) {
                return fail(QStringLiteral("rule %1 (%2) has no usable min/max")
                                .arg(r.clause, r.field));
            }
        } else {
            return fail(QStringLiteral("rule %1 (%2) has unknown op \"%3\"")
                            .arg(r.clause, r.field, r.op));
        }

        parsed.push_back(r);
    }

    if (parsed.isEmpty()) {
        return fail(QStringLiteral("%1 holds no <rule> elements").arg(path));
    }

    m_rules = parsed;
    m_path  = path;
    return true;
}

QVector<RejectRules::Finding> RejectRules::evaluate(
    const QHash<QString, qint64> &values) const
{
    QVector<Finding> out;
    for (const Rule &r : m_rules) {
        // A field the frame does not carry is not a finding. Sub-packet
        // fields are absent from most frames by design — an SLRP carrying
        // only a movement authority has no TSR fields at all, and reporting
        // "TSR_STATUS missing" on every such frame would bury the real ones.
        auto it = values.constFind(r.field);
        if (it == values.constEnd()) { continue; }

        // The guard is checked before the value, and against the same
        // decoded map. A guard naming a field the frame does not carry
        // evaluates as that field being zero, which is the schema's own
        // behaviour — the rules deliberately do not invent a second one.
        if (!r.when.isEmpty()
            && !Schema::Decoder::conditionHolds(r.when, values)) {
            continue;
        }

        const qint64 v = it.value();
        bool fires = false;
        if (r.op == QLatin1String("eq")) {
            fires = (v == r.value);
        } else if (r.op == QLatin1String("outside")) {
            fires = (v < r.min || v > r.max);
        } else if (r.op == QLatin1String("ne_field")) {
            // A rule comparing against a field that is not present does not
            // fire. That is what keeps the identity rules quiet until the
            // loco is known, rather than reporting every frame as addressed
            // elsewhere because "ours" read as zero.
            auto o = values.constFind(r.other);
            if (o == values.constEnd()) { continue; }
            fires = (v != o.value());
        }
        if (!fires) { continue; }

        Finding f;
        f.rule   = r;
        f.actual = v;
        f.text   = describe(f);
        out.push_back(f);
    }
    return out;
}

QString RejectRules::describe(const Finding &f)
{
    // "would not be processed", not "invalid" and not "fail". The console
    // reports what the equipment at the other end would do and cites the
    // clause; the verdict belongs to whoever signs the test sheet.
    QString s = QStringLiteral("%1 = %2").arg(f.rule.field).arg(f.actual);
    if (f.rule.op == QLatin1String("ne_field")) {
        s += QStringLiteral(", not %1").arg(f.rule.other);
    }
    if (!f.rule.when.isEmpty()) {
        // The guard is part of the claim. "FRAME_OFFSET = 14" alone would
        // look wrong to anyone who knows 14 is fine in a station section.
        s += QStringLiteral(" (when %1)").arg(f.rule.when);
    }
    if (!f.rule.note.isEmpty()) {
        s += QStringLiteral(" — %1").arg(f.rule.note);
    }
    s += QStringLiteral("  [%1]").arg(f.rule.clause);
    return s;
}

RejectRules &kavachRejectRules()
{
    // Loaded once, beside the schema, and on the same terms: a failure is a
    // packaging error worth a warning, not a reason to refuse to start. The
    // console then simply reports no reject conditions, which is honest —
    // it has none to check against.
    static RejectRules rules = [] {
        RejectRules r;
        QString err;
        if (!r.load(QStringLiteral(":/schema/rejectrules.xml"), &err)) {
            qWarning("rejectrules.xml load failed: %s", qPrintable(err));
        }
        return r;
    }();
    return rules;
}
