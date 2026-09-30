#include "testutil.h"

#include "fieldsweep.h"

#include <QSet>

// =============================================================================
//  Field sweep — the plan, and what an answer means.
//
//  Both halves are pure, which is the point: the values sent to a live target
//  and the meaning assigned to what came back are the two things that must not
//  be wrong, and neither should need a socket or a window to check.
//
//  The plan is where the damage would be. A wrong boundary value tests nothing
//  and reports a pass; a value that does not fit the field is silently
//  truncated on the way to the wire, so the value tested is not the value in
//  the results table; and a range that quietly expands to 131072 sends turns a
//  button press into a night.
// =============================================================================

namespace {

using namespace FieldSweep;

bool hasValue(const QVector<Step> &plan, qint64 v)
{
    for (const Step &s : plan) { if (s.value == v) { return true; } }
    return false;
}

Spec boundarySpec(int bits, bool isSigned)
{
    Spec s;
    s.field = QStringLiteral("TEST_FIELD");
    s.mode = Mode::Boundary;
    s.bits = bits;
    s.isSigned = isSigned;
    return s;
}

}  // namespace

TEST_SUITE(fieldsweep)
{
    QString err;

    // ---- field limits ------------------------------------------------------
    CHECK(maxValue(4, false) == 15, "4-bit unsigned tops out at 15");
    CHECK(minValue(4, false) == 0,  "and bottoms out at 0");
    CHECK(maxValue(4, true) == 7,   "4-bit signed tops out at 7");
    CHECK(minValue(4, true) == -8,  "and bottoms out at -8");
    CHECK(maxValue(0, false) == 0,  "an unknown width has no range");
    CHECK(maxValue(17, false) == 131071, "17-bit FRAME_NUM tops out at 131071");

    // ---- boundary plans ----------------------------------------------------
    {
        const auto plan = FieldSweep::plan(boundarySpec(8, false), &err);
        CHECK(!plan.isEmpty() && err.isEmpty(), "an 8-bit field plans boundaries");
        CHECK(hasValue(plan, 0),   "zero is included");
        CHECK(hasValue(plan, 1),   "and one");
        CHECK(hasValue(plan, 254), "and max-1 — where off-by-one lives");
        CHECK(hasValue(plan, 255), "and max");
        for (const Step &s : plan) {
            CHECK(s.value >= 0 && s.value <= 255, "every value fits the field");
        }
    }
    {
        const auto plan = FieldSweep::plan(boundarySpec(8, true), &err);
        CHECK(hasValue(plan, -128), "signed: the negative limit is included");
        CHECK(hasValue(plan, -1),   "and -1, the all-ones pattern");
        CHECK(hasValue(plan, 127),  "and the positive limit");
        CHECK(!hasValue(plan, 255), "and nothing outside the signed range");
    }
    {
        // A 1-bit field's min, zero, max-1 and max collapse onto two values.
        // Sending the same value twice teaches nothing and makes the results
        // table overstate coverage.
        const auto plan = FieldSweep::plan(boundarySpec(1, false), &err);
        CHECK(plan.size() == 2, "a 1-bit field plans exactly two values");
        CHECK(hasValue(plan, 0) && hasValue(plan, 1), "which are 0 and 1");
    }
    {
        FieldSweep::plan(boundarySpec(0, false), &err);
        CHECK(!err.isEmpty(), "a field of unknown width cannot be swept by boundary");
    }

    // ---- ranges ------------------------------------------------------------
    {
        Spec s = boundarySpec(8, false);
        s.mode = Mode::Range; s.from = 0; s.to = 10; s.step = 2;
        const auto plan = FieldSweep::plan(s, &err);
        CHECK(plan.size() == 6, "0..10 by 2 is six values");
        CHECK(plan.first().value == 0 && plan.last().value == 10, "ends included");
    }
    {
        Spec s = boundarySpec(8, false);
        s.mode = Mode::Range; s.from = 10; s.to = 0; s.step = -5;
        const auto plan = FieldSweep::plan(s, &err);
        CHECK(plan.size() == 3, "a descending range works");
        CHECK(plan.first().value == 10 && plan.last().value == 0, "and ends correctly");
    }
    {
        Spec s = boundarySpec(8, false);
        s.mode = Mode::Range; s.from = 0; s.to = 10; s.step = 0;
        FieldSweep::plan(s, &err);
        CHECK(!err.isEmpty(), "a zero step is refused rather than looping for ever");
    }
    {
        Spec s = boundarySpec(8, false);
        s.mode = Mode::Range; s.from = 0; s.to = 10; s.step = -1;
        FieldSweep::plan(s, &err);
        CHECK(!err.isEmpty(), "a step running away from the end is refused");
    }
    {
        // The one that would turn a button press into a night on the bench.
        Spec s = boundarySpec(17, false);
        s.mode = Mode::Range; s.from = 0; s.to = 131071; s.step = 1;
        const auto plan = FieldSweep::plan(s, &err);
        CHECK(plan.isEmpty() && err.contains(QLatin1String("131072")),
              "an oversized range is refused, and says how many values it was");
    }
    {
        // A value outside the field is silently truncated on the way to the
        // wire, so the value tested would not be the value reported.
        Spec s = boundarySpec(4, false);
        s.mode = Mode::Range; s.from = 0; s.to = 20; s.step = 5;
        FieldSweep::plan(s, &err);
        CHECK(err.contains(QLatin1String("4 bits")),
              "values that do not fit the field are refused");
    }

    // ---- explicit lists ----------------------------------------------------
    {
        Spec s = boundarySpec(8, false);
        s.mode = Mode::List; s.list = { 0, 7, 7, 255 };
        const auto plan = FieldSweep::plan(s, &err);
        CHECK(plan.size() == 3, "a repeated value in the list is sent once");
        Spec empty = s; empty.list.clear();
        FieldSweep::plan(empty, &err);
        CHECK(!err.isEmpty(), "an empty list is refused");
    }

    // ---- enum codes --------------------------------------------------------
    {
        Spec s = boundarySpec(4, false);
        s.mode = Mode::EnumCodes;
        s.enumChoices = { {0, QStringLiteral("none")},
                          {1, QStringLiteral("normal")},
                          {2, QStringLiteral("emergency")} };
        const auto plan = FieldSweep::plan(s, &err);
        CHECK(plan.size() == 4, "three declared codes plus one undefined");
        CHECK(hasValue(plan, 3), "the undefined code is the first unused value");
        bool labelled = false;
        for (const Step &st : plan) {
            if (st.value == 3) { labelled = st.note.contains(QLatin1String("undefined")); }
        }
        CHECK(labelled, "and it is labelled as such — it is the point of this mode");

        Spec none = s; none.enumChoices.clear();
        FieldSweep::plan(none, &err);
        CHECK(!err.isEmpty(), "a field with no enum cannot be swept by enum");
    }

    // ---- no field ----------------------------------------------------------
    {
        Spec s; s.mode = Mode::Boundary; s.bits = 8;
        FieldSweep::plan(s, &err);
        CHECK(!err.isEmpty(), "a spec with no field is refused");
    }

    // ---- classification ----------------------------------------------------
    {
        const QSet<QString> replies = { QStringLiteral("aap") };

        CHECK(classify({}, replies, 500) == Verdict::Silent,
              "nothing observed is silence");

        QVector<Observation> reply{ { 120, QStringLiteral("aap"), false } };
        CHECK(classify(reply, replies, 500) == Verdict::Reply, "an expected type is a reply");

        QVector<Observation> late{ { 900, QStringLiteral("aap"), false } };
        CHECK(classify(late, replies, 500) == Verdict::Silent,
              "an answer after the window belongs to no step, so the step is silent");

        QVector<Observation> fault{ { 50, QStringLiteral("nmsflt"), true } };
        CHECK(classify(fault, replies, 500) == Verdict::FaultOnly,
              "a fault alone is its own verdict — the target understood and rejected");

        QVector<Observation> other{ { 50, QStringLiteral("rfid"), false } };
        CHECK(classify(other, replies, 500) == Verdict::Other,
              "unrelated traffic is neither a reply nor silence");

        // A reply outranks a fault in the same window: the target answered.
        QVector<Observation> both{ { 50, QStringLiteral("nmsflt"), true },
                                   { 60, QStringLiteral("aap"), false } };
        CHECK(classify(both, replies, 500) == Verdict::Reply,
              "a reply alongside a fault still counts as a reply");

        // With no reply types configured, traffic is 'other' rather than
        // silently scored as an answer.
        CHECK(classify(reply, {}, 500) == Verdict::Other,
              "with no reply types set, traffic is only 'other'");

        CHECK(!verdictName(Verdict::Silent).isEmpty(), "verdicts have names for the table");
    }
}
