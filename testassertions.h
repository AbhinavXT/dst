#ifndef TESTASSERTIONS_H
#define TESTASSERTIONS_H

// =============================================================================
//  TestAssertions
//  -----------------------------------------------------------------------------
//  Watches live or recorded traffic for the conditions a test case asserts,
//  and records what was observed.
//
//  WHY
//    SIF 0533 has 1500 test cases and 699 hand-filled Pass/Fail cells, and
//    the observable ones are all statements about field values — "SIG_OV
//    shall be set 1", "the status of following bits shall be set to 1 if
//    failed", "simulate with frame numbers ending 03, 11, 19...". Today
//    somebody watches the screen and says they saw it. Nothing links the
//    tick in the document to the frame that justified it.
//
//    This turns each of those statements into a query the console evaluates
//    continuously, and keeps the frame that satisfied it.
//
//  WHAT AN ASSERTION IS
//    A LogQuery plus an optional coverage requirement. The query is the
//    condition; coverage is "and I want to have seen it across all of these
//    distinct values", which is exactly what the document's simulate-with
//    lists ask for. 32.9.3 is not satisfied by one frame ending 03 — it
//    wants 03, 11, 19, ... 99, and checking thirteen of those by eye is
//    where mistakes live.
//
//  OBSERVED, NOT PASSED
//    The engine reports OBSERVED / NOT OBSERVED, never Pass/Fail. Whether a
//    test passes is a judgement about whether the system did the right
//    thing, made by the people signing the document. A tool that printed
//    "PASS" would be claiming an authority it does not have, and on a
//    signalling acceptance test that claim would be worth nothing and cost
//    a great deal. What it can honestly say is "this condition was seen, at
//    this time, in this frame" — which is the evidence the tick rests on.
// =============================================================================

#include <QDateTime>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

#include "logentry.h"
#include "logquery.h"

class ColorRules;

// How an assertion is evaluated.
enum class AssertKind {
    // A single condition seen anywhere in the traffic.
    Simple,
    // An ordered sequence of conditions, each with its own deadline.
    //
    // This is the general form, and it exists because most SIF cases have a
    // PRIOR STATE as well as a trigger and a result: "while in Full
    // Supervision, if no cab is active, enter Stand_By" is three steps, not
    // two, and the same trigger and result appear again under Limited
    // Supervision and Staff Responsible as separate numbered cases. Only
    // the first step differs.
    Sequence,
    // "when GIVEN holds, THEN must follow within a window" — a condition
    // that spans two DIFFERENT packets.
    //
    // This is the shape most acceptance cases actually have, and it cannot
    // be written as one query: the cause and the effect arrive in separate
    // message types, seconds apart. "Cab input goes inactive (DIP1) so the
    // loco enters Stand_By (LSRP/ARP)" needs both halves and the causal
    // order between them.
    Implication,
};

struct TestAssertion {
    QString id;            // SIF operation id, e.g. "32.9.3"
    QString title;         // the clause text, abbreviated
    AssertKind kind = AssertKind::Simple;

    // Sequence: one step per condition that must occur, in order.
    struct Step {
        QString  matchText;
        QString  label;       // shown in the report, e.g. "in Full Supervision"
        qint64   withinMs = 0;// deadline after the PREVIOUS step; 0 = no limit
        LogQuery query;

        // all_of: several conditions that must ALL hold, in any order,
        // within this step's deadline.
        //
        // A trigger with multiple simultaneous consequences is a distinct
        // shape from a sequence, and collapsing it into one would be wrong
        // in both directions. A foreign tag must produce the indication AND
        // the mode change AND the brake — the order they arrive in is an
        // implementation detail, but every one of them is required, and a
        // case that reported success when only two of three happened would
        // be worse than no check at all.
        struct Part {
            QString  matchText;
            QString  label;
            LogQuery query;
            bool     seen = false;
            QString  evidence;
        };
        QVector<Part> parts;

        bool isAllOf() const { return !parts.isEmpty(); }
        bool partsComplete() const
        {
            for (const Part &p : parts) if (!p.seen) return false;
            return true;
        }
        QStringList missingParts() const
        {
            QStringList out;
            for (const Part &p : parts) if (!p.seen) out << p.label;
            return out;
        }
        void resetParts()
        {
            for (Part &p : parts) { p.seen = false; p.evidence.clear(); }
        }
    };
    QVector<Step> steps;

    // How far through the sequence we currently are, and when that step
    // was reached. -1 means nothing matched yet.
    int    stepAt    = -1;
    qint64 stepMs    = 0;
    QStringList stepEvidence;

    QString queryText;     // Simple: the condition
    QString givenText;     // Implication: the precondition
    QString thenText;      // Implication: what must follow

    // How long the consequent has to appear. Chosen per case because the
    // systems differ: a mode change is near-immediate, a link timeout is
    // seconds. Zero means "any time later in the run", which is weaker but
    // still meaningful.
    qint64  withinMs = 5000;
    QString note;          // free text carried into the report

    // Optional coverage: the distinct values of `coverField` that must all
    // have been seen while the query matched. Empty means "any single
    // observation satisfies this".
    QString      coverField;
    QSet<qint64> coverWanted;

    // ---- runtime state ----
    LogQuery query;        // Simple
    LogQuery givenQuery;   // Implication
    LogQuery thenQuery;
    bool     queryOk = true;
    QString  queryError;

    // Implication bookkeeping. `armedMs` is when the precondition was last
    // seen with no consequent yet; 0 means not armed.
    qint64  armedMs   = 0;
    QString armedText;
    int     satisfied = 0;   // precondition followed by the consequent
    int     violated  = 0;   // precondition whose window expired
    qint64  worstMs   = 0;   // when the first violation happened
    QString violationText;   // the precondition frame that went unanswered
    qint64  slowestMs = 0;   // longest observed gap, for tuning the window

    int          hits = 0;
    qint64       firstMs = 0;
    qint64       lastMs = 0;
    QString      firstText;      // the frame that first satisfied it
    QSet<qint64> coverSeen;

    bool observed() const
    {
        if (kind == AssertKind::Sequence) {
            return satisfied > 0 && violated == 0 && coverComplete();
        }
        if (kind == AssertKind::Implication) {
            // An implication is only observed when the precondition
            // occurred AND every occurrence was answered. A case that was
            // never triggered is NOT observed — nothing was demonstrated —
            // and one with any unanswered occurrence is a finding, not a
            // pass.
            return satisfied > 0 && violated == 0 && coverComplete();
        }
        return hits > 0 && coverComplete();
    }

    // The precondition happened and the expected response did not follow.
    // Distinct from "not observed", which merely means untested.
    bool hasViolation() const { return violated > 0; }
    // QSet::subtract mutates in place, so it cannot be called on the const
    // member here; check containment instead.
    bool coverComplete() const
    {
        for (qint64 v : coverWanted) if (!coverSeen.contains(v)) return false;
        return true;
    }
    QSet<qint64> coverMissing() const;
};

class TestAssertionEngine : public QObject
{
    Q_OBJECT

public:
    explicit TestAssertionEngine(QObject *parent = nullptr);

    // Load definitions from JSON. Returns false and fills `err` on a parse
    // failure; individual bad assertions are kept with their error recorded
    // rather than dropped, so a typo in one case does not silently remove
    // it from the run.
    bool load(const QString &path, QString *err = nullptr);
    bool loadFromJson(const QByteArray &json, QString *err = nullptr);

    // Evaluate one entry against every assertion. Cheap when nothing
    // matches: a query with no field: term never triggers a decode.
    void observe(const LogEntryPtr &entry);

    // Expire any implication whose window has passed without its
    // consequent. Called from observe() using message time, and separately
    // on a timer so a precondition at the very end of a run is still
    // resolved rather than left dangling.
    void expireArmed(qint64 nowMs);

    void resetObservations();

    const QVector<TestAssertion> &assertions() const { return m_asserts; }
    int observedCount() const;

    // Report of what was and was not seen, keyed by operation id.
    QString buildReportHtml(const QString &title, qint64 nowMs) const;
    QString buildReportCsv() const;

signals:
    // First time an assertion becomes fully observed. The UI surfaces this
    // so the operator knows the condition has been met without watching a
    // table.
    void assertionObserved(const QString &id, const QString &title);

    // A precondition occurred and its consequent never arrived. Surfaced
    // immediately: this is the one outcome an operator wants to know about
    // during the run, not afterwards in a report.
    void assertionViolated(const QString &id, const QString &title,
                           const QString &detail);

private:
    QVector<TestAssertion> m_asserts;
};

#endif // TESTASSERTIONS_H
