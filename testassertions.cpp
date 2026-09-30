#include "testassertions.h"

#include "fieldindex.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <functional>

QSet<qint64> TestAssertion::coverMissing() const
{
    QSet<qint64> missing = coverWanted;
    return missing.subtract(coverSeen);
}

TestAssertionEngine::TestAssertionEngine(QObject *parent)
    : QObject(parent)
{
}

bool TestAssertionEngine::load(const QString &path, QString *err)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = QStringLiteral("cannot open %1: %2")
                            .arg(path, f.errorString());
        return false;
    }
    const QByteArray data = f.readAll();
    f.close();
    return loadFromJson(data, err);
}

bool TestAssertionEngine::loadFromJson(const QByteArray &json, QString *err)
{
    m_asserts.clear();

    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &perr);
    if (perr.error != QJsonParseError::NoError) {
        if (err) *err = QStringLiteral("JSON error at offset %1: %2")
                            .arg(perr.offset).arg(perr.errorString());
        return false;
    }
    if (!doc.isArray()) {
        if (err) *err = QStringLiteral("expected a JSON array of test cases");
        return false;
    }

    // Expand templates first. A case carrying "expand" is a SHAPE, not a
    // case: the same trigger and result under Full Supervision, Limited
    // Supervision and Staff Responsible are three separately numbered SIF
    // clauses that differ only in the prior mode. Writing them out three
    // times invites the copy that gets edited in two places out of three.
    QVector<QJsonObject> objects;
    for (const QJsonValue &v : doc.array()) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();

        const QJsonArray expand = o.value("expand").toArray();
        if (expand.isEmpty()) { objects.append(o); continue; }

        for (const QJsonValue &ev : expand) {
            const QJsonObject vars = ev.toObject();
            // Substitute {name} placeholders throughout the object's
            // strings, including inside nested sequence steps.
            std::function<QJsonValue(const QJsonValue &)> subst =
                [&](const QJsonValue &in) -> QJsonValue {
                    if (in.isString()) {
                        QString s = in.toString();
                        for (auto it = vars.begin(); it != vars.end(); ++it) {
                            s.replace(QStringLiteral("{%1}").arg(it.key()),
                                      it.value().isString()
                                          ? it.value().toString()
                                          : QString::number(it.value().toDouble()));
                        }
                        return s;
                    }
                    if (in.isObject()) {
                        QJsonObject out;
                        const QJsonObject io = in.toObject();
                        for (auto it = io.begin(); it != io.end(); ++it) {
                            out.insert(it.key(), subst(it.value()));
                        }
                        return out;
                    }
                    if (in.isArray()) {
                        QJsonArray out;
                        for (const QJsonValue &x : in.toArray()) out.append(subst(x));
                        return out;
                    }
                    return in;
                };
            QJsonObject inst = subst(o).toObject();
            inst.remove("expand");
            objects.append(inst);
        }
    }

    for (const QJsonObject &o : objects) {
        TestAssertion a;
        a.id        = o.value("id").toString();
        a.givenText = o.value("given").toString();
        a.thenText  = o.value("then").toString();
        if (o.contains("within_ms")) a.withinMs = qint64(o.value("within_ms").toDouble());
        a.title     = o.value("title").toString();
        a.queryText = o.value("query").toString();
        a.note      = o.value("note").toString();
        a.coverField = o.value("cover_field").toString();

        for (const QJsonValue &cv : o.value("cover_values").toArray()) {
            a.coverWanted.insert(qint64(cv.toDouble()));
        }

        // A given/then pair is just a two-step sequence; normalising it
        // here means one evaluator rather than two that can drift.
        QJsonArray seq = o.value("sequence").toArray();
        if (seq.isEmpty() && !a.givenText.isEmpty() && !a.thenText.isEmpty()) {
            QJsonObject g; g.insert("match", a.givenText); g.insert("label", "given");
            QJsonObject t; t.insert("match", a.thenText);  t.insert("label", "then");
            t.insert("within_ms", double(a.withinMs));
            seq.append(g); seq.append(t);
        }

        if (a.id.isEmpty()) continue;
        if (seq.isEmpty() && a.queryText.isEmpty()) continue;

        // A bad query is KEPT, with its error recorded. Dropping it would
        // remove the case from the run silently, and a test case that
        // quietly is not being checked is worse than one that is visibly
        // broken.
        if (!seq.isEmpty()) {
            a.kind = AssertKind::Sequence;
            QStringList shown;
            int n = 0;
            for (const QJsonValue &sv : seq) {
                const QJsonObject so = sv.toObject();
                TestAssertion::Step st;
                st.matchText = so.value("match").toString();
                st.label     = so.value("label").toString();
                st.withinMs  = qint64(so.value("within_ms").toDouble());
                if (st.label.isEmpty()) {
                    st.label = QStringLiteral("step %1").arg(n + 1);
                }

                const QJsonArray allOf = so.value("all_of").toArray();
                for (const QJsonValue &pv : allOf) {
                    const QJsonObject po = pv.toObject();
                    TestAssertion::Step::Part part;
                    part.matchText = po.value("match").toString();
                    part.label     = po.value("label").toString();
                    if (part.label.isEmpty()) part.label = part.matchText;
                    if (!part.query.parse(part.matchText)) {
                        a.queryOk = false;
                        a.queryError = QStringLiteral("%1 / %2: %3")
                                           .arg(st.label, part.label,
                                                part.query.errorString());
                    }
                    st.parts.append(part);
                }

                if (!st.isAllOf()) {
                    if (!st.query.parse(st.matchText)) {
                        a.queryOk = false;
                        a.queryError = QStringLiteral("%1: %2")
                                           .arg(st.label, st.query.errorString());
                    }
                } else if (!st.matchText.isEmpty()) {
                    // Both given would be ambiguous about what must hold.
                    a.queryOk = false;
                    a.queryError = QStringLiteral("%1: use either 'match' or "
                                                  "'all_of', not both").arg(st.label);
                }

                if (st.isAllOf()) {
                    QStringList ps;
                    for (const auto &p : st.parts) ps << p.label;
                    shown << QStringLiteral("%1 [all of: %2]%3")
                                 .arg(st.label, ps.join(QStringLiteral(" + ")))
                                 .arg(st.withinMs > 0
                                          ? QStringLiteral(" within %1 ms").arg(st.withinMs)
                                          : QString());
                } else {
                    shown << (st.withinMs > 0
                                  ? QStringLiteral("%1 [%2] within %3 ms")
                                        .arg(st.label, st.matchText).arg(st.withinMs)
                                  : QStringLiteral("%1 [%2]")
                                        .arg(st.label, st.matchText));
                }
                a.steps.append(st);
                ++n;
            }
            a.queryText = shown.join(QStringLiteral("  →  "));
        } else {
            a.queryOk = a.query.parse(a.queryText);
            if (!a.queryOk) a.queryError = a.query.errorString();
        }

        m_asserts.append(a);
    }

    if (m_asserts.isEmpty()) {
        if (err) *err = QStringLiteral("no usable test cases in the file");
        return false;
    }
    return true;
}

void TestAssertionEngine::observe(const LogEntryPtr &entry)
{
    if (!entry) return;

    // Resolve anything whose window has closed BEFORE this entry is
    // considered, using message time rather than wall-clock so a replayed
    // or archived session evaluates identically to a live one.
    expireArmed(entry->epochMs);

    for (TestAssertion &a : m_asserts) {
        if (!a.queryOk) continue;

        if (a.kind == AssertKind::Sequence) {
            const int next = a.stepAt + 1;
            if (next >= a.steps.size()) continue;

            // An all_of step accumulates: each part may arrive in its own
            // packet, in any order, and the step only completes when every
            // one has been seen.
            if (a.steps[next].isAllOf()) {
                bool changed = false;
                for (auto &part : a.steps[next].parts) {
                    if (part.seen) continue;
                    if (!part.query.match(*entry, nullptr)) continue;
                    part.seen = true;
                    part.evidence = entry->text.left(90);
                    changed = true;
                }
                if (!changed || !a.steps[next].partsComplete()) {
                    // Start the clock on the first part that arrives, so a
                    // partially-satisfied step can still time out.
                    if (changed && a.stepMs == 0) a.stepMs = entry->epochMs;
                    continue;
                }
                // Every part seen: fall through and treat as an advance.
            } else if (!a.steps[next].query.match(*entry, nullptr)) {
                // Not advancing; the restart check below still applies.
                if (a.stepAt > 0 && a.steps[0].query.match(*entry, nullptr)) {
                    for (auto &st : a.steps) st.resetParts();
                    a.stepAt = 0;
                    a.stepMs = entry->epochMs;
                    a.stepEvidence.clear();
                    a.stepEvidence << QStringLiteral("%1: %2")
                                          .arg(a.steps[0].label, entry->text.left(90));
                }
                continue;
            }

            {
                const bool wasObserved = a.observed();
                if (a.stepMs > 0) {
                    // Longest gap between consecutive steps, for tuning the
                    // deadline: a run that consistently lands at 8s against
                    // a 5s window should show as data, not as mystery
                    // violations.
                    const qint64 gap = entry->epochMs - a.stepMs;
                    if (gap > a.slowestMs) a.slowestMs = gap;
                }
                a.stepAt = next;
                a.stepMs = entry->epochMs;
                if (a.steps[next].isAllOf()) {
                    for (const auto &p : a.steps[next].parts) {
                        a.stepEvidence << QStringLiteral("%1 / %2: %3")
                                              .arg(a.steps[next].label, p.label,
                                                   p.evidence);
                    }
                } else {
                    a.stepEvidence << QStringLiteral("%1: %2")
                                          .arg(a.steps[next].label,
                                               entry->text.left(90));
                }

                if (a.stepAt == a.steps.size() - 1) {
                    ++a.satisfied;
                    ++a.hits;
                    a.lastMs = entry->epochMs;
                    if (a.firstMs == 0) {
                        a.firstMs   = entry->epochMs;
                        a.firstText = a.stepEvidence.join(QStringLiteral("  |  "));
                    }
                    if (!a.coverField.isEmpty()) {
                        bool ok = false;
                        const double v = FieldIndex::number(*entry, a.coverField, &ok);
                        if (ok) a.coverSeen.insert(qint64(v));
                    }
                    a.stepAt = -1;              // ready for the next occurrence
                    a.stepEvidence.clear();
                    for (auto &st : a.steps) st.resetParts();
                    if (!wasObserved && a.observed()) {
                        emit assertionObserved(a.id, a.title);
                    }
                }
                continue;
            }

            continue;
        }

        if (a.kind == AssertKind::Implication) {
            // Consequent first: a single frame that satisfies both halves
            // should close the obligation rather than immediately re-arm
            // it and appear to be a violation.
            if (a.armedMs != 0 && a.thenQuery.match(*entry, nullptr)) {
                const bool wasObserved = a.observed();
                const qint64 gap = entry->epochMs - a.armedMs;
                if (gap > a.slowestMs) a.slowestMs = gap;
                ++a.satisfied;
                ++a.hits;
                a.lastMs = entry->epochMs;
                if (a.firstMs == 0) {
                    a.firstMs   = a.armedMs;
                    a.firstText = QStringLiteral("given: %1  →  then: %2")
                                      .arg(a.armedText, entry->text.left(120));
                }
                if (!a.coverField.isEmpty()) {
                    bool ok = false;
                    const double v = FieldIndex::number(*entry, a.coverField, &ok);
                    if (ok) a.coverSeen.insert(qint64(v));
                }
                a.armedMs = 0;
                a.armedText.clear();
                if (!wasObserved && a.observed()) {
                    emit assertionObserved(a.id, a.title);
                }
                continue;
            }
            if (a.givenQuery.match(*entry, nullptr)) {
                // Re-arming while already armed is not a violation: the
                // precondition simply persists (cab input stays inactive
                // across many DIP frames). Keep the EARLIEST arm time so
                // the window measures from when the condition began.
                if (a.armedMs == 0) {
                    a.armedMs   = entry->epochMs;
                    a.armedText = entry->text.left(120);
                }
            }
            continue;
        }

        if (!a.query.match(*entry, nullptr)) continue;

        const bool wasObserved = a.observed();

        ++a.hits;
        a.lastMs = entry->epochMs;
        if (a.firstMs == 0) {
            a.firstMs   = entry->epochMs;
            a.firstText = entry->text.left(200);
        }

        // Coverage is collected only from frames that MATCHED. Counting a
        // frame number seen while the condition was absent would report the
        // simulate-with list as exercised when it was not.
        if (!a.coverField.isEmpty()) {
            bool ok = false;
            const double v = FieldIndex::number(*entry, a.coverField, &ok);
            if (ok) a.coverSeen.insert(qint64(v));
        }

        if (!wasObserved && a.observed()) {
            emit assertionObserved(a.id, a.title);
        }
    }
}

void TestAssertionEngine::expireArmed(qint64 nowMs)
{
    for (TestAssertion &a : m_asserts) {
        if (a.kind == AssertKind::Sequence) {
            if (a.stepAt < 0 || a.stepAt >= a.steps.size() - 1) continue;
            const qint64 limit = a.steps[a.stepAt + 1].withinMs;
            if (limit <= 0) continue;               // no deadline on this step
            if (nowMs - a.stepMs <= limit) continue;

            ++a.violated;
            if (a.worstMs == 0) {
                a.worstMs = a.stepMs;
                a.violationText = a.stepEvidence.join(QStringLiteral("  |  "));
            }
            // Name what was missing. With several required consequences,
            // "the step did not complete" is nearly useless — "the mode
            // changed and the indication appeared but the brake never did"
            // is the finding.
            TestAssertion::Step &pending = a.steps[a.stepAt + 1];
            QString missing;
            if (pending.isAllOf()) {
                const QStringList m = pending.missingParts();
                QStringList got;
                for (const auto &p : pending.parts) if (p.seen) got << p.label;
                missing = QStringLiteral("\nsatisfied: %1\nMISSING:   %2")
                              .arg(got.isEmpty() ? QStringLiteral("(none)")
                                                 : got.join(QStringLiteral(", ")),
                                   m.join(QStringLiteral(", ")));
            }
            const QString detail =
                QStringLiteral("reached '%1' at %2 but '%3' did not complete "
                               "within %4 ms%5\n%6")
                    .arg(a.steps[a.stepAt].label)
                    .arg(QDateTime::fromMSecsSinceEpoch(a.stepMs, Qt::UTC)
                             .toString(Qt::ISODateWithMs))
                    .arg(pending.label).arg(limit).arg(missing)
                    .arg(a.stepEvidence.join(QStringLiteral("\n")));
            pending.resetParts();
            a.stepAt = -1;
            a.stepEvidence.clear();
            emit assertionViolated(a.id, a.title, detail);
            continue;
        }
        if (a.kind != AssertKind::Implication) continue;
        if (a.armedMs == 0) continue;
        // withinMs == 0 means "any time later in the run", so such an
        // assertion never expires mid-run.
        if (a.withinMs <= 0) continue;
        if (nowMs - a.armedMs <= a.withinMs) continue;

        ++a.violated;
        if (a.worstMs == 0) {
            a.worstMs = a.armedMs;
            a.violationText = a.armedText;
        }
        const QString detail =
            QStringLiteral("precondition at %1 was not followed by the "
                           "expected condition within %2 ms\n%3")
                .arg(QDateTime::fromMSecsSinceEpoch(a.armedMs, Qt::UTC)
                         .toString(Qt::ISODateWithMs))
                .arg(a.withinMs)
                .arg(a.armedText);
        a.armedMs = 0;
        a.armedText.clear();
        emit assertionViolated(a.id, a.title, detail);
    }
}

void TestAssertionEngine::resetObservations()
{
    for (TestAssertion &a : m_asserts) {
        a.hits = 0;
        a.firstMs = a.lastMs = 0;
        a.firstText.clear();
        a.coverSeen.clear();
        a.armedMs = 0;
        a.armedText.clear();
        a.satisfied = a.violated = 0;
        a.stepAt = -1;
        a.stepMs = 0;
        a.stepEvidence.clear();
        for (auto &st : a.steps) st.resetParts();
        a.worstMs = a.slowestMs = 0;
        a.violationText.clear();
    }
}

int TestAssertionEngine::observedCount() const
{
    int n = 0;
    for (const TestAssertion &a : m_asserts) if (a.observed()) ++n;
    return n;
}

QString TestAssertionEngine::buildReportHtml(const QString &title,
                                             qint64 nowMs) const
{
    auto esc = [](const QString &s) { return s.toHtmlEscaped(); };
    auto stamp = [](qint64 ms) {
        return ms > 0 ? QDateTime::fromMSecsSinceEpoch(ms, Qt::UTC)
                            .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) + "Z"
                      : QStringLiteral("—");
    };

    int observed = 0, broken = 0, violations = 0;
    for (const TestAssertion &a : m_asserts) {
        if (a.observed())     ++observed;
        if (!a.queryOk)       ++broken;
        if (a.hasViolation()) ++violations;
    }

    QString h;
    h += QStringLiteral("<!DOCTYPE html><html><head><meta charset='utf-8'>");
    h += QStringLiteral("<title>%1</title>").arg(esc(title));
    h += QStringLiteral(
        "<style>body{font-family:sans-serif;font-size:12px;margin:24px;}"
        "h1{font-size:18px;margin-bottom:2px;}"
        ".meta{color:#666;margin-bottom:14px;}"
        ".warn{background:#fff8e6;border:1px solid #e8c37a;padding:8px;"
        "margin-bottom:14px;}"
        "table{border-collapse:collapse;width:100%;}"
        "th,td{border:1px solid #ccc;padding:4px 8px;text-align:left;"
        "vertical-align:top;}th{background:#f0f0f0;}"
        "tr.seen td{background:#f2fbf4;} tr.miss td{background:#fbf4f4;}"
        "tr.viol td{background:#ffe9e9;}"
        ".ok{color:#0b6b2f;font-weight:bold;} .no{color:#b00000;font-weight:bold;}"
        ".vi{color:#7a0000;font-weight:bold;}"
        ".err{color:#b00000;} code{font-size:11px;color:#333;}"
        "</style></head><body>");

    h += QStringLiteral("<h1>%1</h1>").arg(esc(title));
    h += QStringLiteral("<div class='meta'>Generated %1 &middot; "
                        "%2 of %3 conditions observed</div>")
             .arg(stamp(nowMs)).arg(observed).arg(m_asserts.size());

    // The disclaimer is part of the artefact, not decoration. This report
    // is evidence for a signature, and it must not be mistaken for one.
    h += QStringLiteral(
        "<div class='warn'><b>This is an observation record, not a verdict.</b> "
        "It states which conditions were seen in the captured traffic and "
        "when. Whether a test case passes remains the judgement of the "
        "signatories; conditions that cannot be observed in packet data "
        "(DMI indications, brake application, operator actions) are not "
        "covered here at all.</div>");

    // Violations lead. An unanswered precondition is the one outcome that
    // says the system did something unexpected, as opposed to merely not
    // having been exercised — burying it below a table of green rows would
    // invert its importance.
    if (violations > 0) {
        h += QStringLiteral("<div class='warn'><b>%1 condition(s) were "
                            "triggered but not answered.</b> The precondition "
                            "occurred and the expected response did not follow "
                            "within the allowed window. These are findings, "
                            "not untested cases.</div>").arg(violations);
    }

    if (broken > 0) {
        h += QStringLiteral("<div class='warn'><b>%1 test case(s) have an "
                            "unusable query</b> and were never evaluated — "
                            "see the rows marked in red.</div>").arg(broken);
    }

    h += QStringLiteral(
        "<table><tr><th>Operation</th><th>Condition</th><th>Observed</th>"
        "<th>Hits</th><th>First seen</th><th>Coverage</th><th>Evidence</th></tr>");

    for (const TestAssertion &a : m_asserts) {
        h += QStringLiteral("<tr class='%1'>")
                 .arg(a.hasViolation() ? "viol" : (a.observed() ? "seen" : "miss"));
        h += QStringLiteral("<td><b>%1</b><br><span style='color:#666'>%2</span></td>")
                 .arg(esc(a.id), esc(a.title));
        h += QStringLiteral("<td><code>%1</code>%2</td>")
                 .arg(esc(a.queryText),
                      a.queryOk ? QString()
                                : QStringLiteral("<br><span class='err'>query error: %1</span>")
                                      .arg(esc(a.queryError)));
        // Three outcomes, not two. "Not answered" must not be reported as
        // "not observed": one means the system misbehaved, the other means
        // nobody tried it.
        QString state, cls;
        if (a.hasViolation())      { state = QStringLiteral("NOT ANSWERED"); cls = "vi"; }
        else if (a.observed())     { state = QStringLiteral("OBSERVED");     cls = "ok"; }
        else                       { state = QStringLiteral("not observed"); cls = "no"; }
        h += QStringLiteral("<td class='%1'>%2</td>").arg(cls, state);

        if (a.kind == AssertKind::Implication || a.kind == AssertKind::Sequence) {
            h += QStringLiteral("<td>%1 completed<br>%2 broken%3</td>")
                     .arg(a.satisfied).arg(a.violated)
                     .arg(a.slowestMs > 0
                              ? QStringLiteral("<br><span style='color:#666'>"
                                               "slowest %1 ms</span>").arg(a.slowestMs)
                              : QString());
        } else {
            h += QStringLiteral("<td>%1</td>").arg(a.hits);
        }
        h += QStringLiteral("<td>%1</td>").arg(stamp(a.firstMs));

        if (a.coverWanted.isEmpty()) {
            h += QStringLiteral("<td>—</td>");
        } else {
            QList<qint64> missing = a.coverMissing().values();
            std::sort(missing.begin(), missing.end());
            QStringList ms;
            for (qint64 m : missing) ms << QString::number(m);
            h += QStringLiteral("<td>%1 of %2 %3%4</td>")
                     .arg(a.coverWanted.size() - missing.size())
                     .arg(a.coverWanted.size())
                     .arg(esc(a.coverField))
                     .arg(missing.isEmpty()
                              ? QString()
                              : QStringLiteral("<br><span class='err'>missing: %1</span>")
                                    .arg(ms.join(", ")));
        }
        h += QStringLiteral("<td><code>%1</code>%2</td>")
                 .arg(esc(a.firstText),
                      a.violationText.isEmpty()
                          ? QString()
                          : QStringLiteral("<br><span class='vi'>unanswered: </span>"
                                           "<code>%1</code>").arg(esc(a.violationText)));
        h += QStringLiteral("</tr>");
    }
    h += QStringLiteral("</table></body></html>");
    return h;
}

QString TestAssertionEngine::buildReportCsv() const
{
    auto q = [](const QString &s) {
        QString v = s; v.replace('"', "\"\"");
        return QStringLiteral("\"%1\"").arg(v);
    };
    QStringList lines;
    lines << "operation,title,observed,hits,first_seen_utc,coverage,query";
    for (const TestAssertion &a : m_asserts) {
        const QString cov = a.coverWanted.isEmpty()
            ? QString()
            : QStringLiteral("%1/%2")
                  .arg(a.coverWanted.size() - a.coverMissing().size())
                  .arg(a.coverWanted.size());
        lines << QStringList{
            q(a.id), q(a.title),
            a.hasViolation() ? "NOT ANSWERED"
                             : (a.observed() ? "OBSERVED" : "not observed"),
            QString::number(a.kind == AssertKind::Simple ? a.hits : a.satisfied),
            a.firstMs > 0 ? QDateTime::fromMSecsSinceEpoch(a.firstMs, Qt::UTC)
                                .toString(Qt::ISODate) : QString(),
            q(cov), q(a.queryText)
        }.join(QLatin1Char(','));
    }
    return lines.join(QLatin1Char('\n'));
}
