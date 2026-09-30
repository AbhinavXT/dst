#include "testutil.h"

#include "framediff.h"
#include "framediffwindow.h"

#include <QCheckBox>
#include <QTableWidget>

#include <QByteArray>
#include <QString>

// =============================================================================
//  Frame diff — field-level comparison of two frames.
//
//  The point of the feature is that a byte diff of a bit-packed frame is
//  useless: one changed 9-bit field smears across two bytes, and a field that
//  moved because a conditional branch fired changes every byte after it. So
//  the comparison is over decoded FIELDS, and the two field lists are aligned
//  before being compared — a row that exists on one side only must be reported
//  as such rather than pushing every later row out of step and reporting the
//  whole frame as changed.
//
//  Real frames from replay/ are used for the end-to-end checks so the
//  alignment is exercised against real conditional branches (LSRP's
//  Loco_Health group rotates with FRAME_NUM) rather than only synthetic rows.
// =============================================================================

namespace {

FieldRow fr(const QString &f, const QString &v) { return FieldRow{ f, v }; }

using FrameDiff::Kind;

// The row for a field name, or a Kind that cannot occur so a miss is visible.
Kind kindOf(const QVector<FrameDiff::Row> &rows, const QString &field)
{
    for (const FrameDiff::Row &r : rows) {
        if (r.field == field) { return r.kind; }
    }
    return static_cast<Kind>(-1);
}

int countOf(const QVector<FrameDiff::Row> &rows, Kind k)
{
    int n = 0;
    for (const FrameDiff::Row &r : rows) { if (r.kind == k) { ++n; } }
    return n;
}

// Two real LSRP frames from replay/, two seconds apart: same layout, a handful
// of moving fields.
const QString kLsrpA = QStringLiteral(
    "@lsrp_1_1 2026-06-26T16:29:16 1112 02 07 0A 00 27 00 00 00 0F 02 A3 AE 7D D0 "
    "00 01 40 9D 5C 81 20 90 7D 0C 90 40 50 0D 00 00 00 78 34 A8 37 56 19 1D 46");
const QString kLsrpB = QStringLiteral(
    "@lsrp_1_1 2026-06-26T16:29:18 1139 02 07 0A 00 27 00 00 00 0F 02 A3 AE 7D F0 "
    "00 01 40 9D 63 41 40 A0 7D 0B 50 C0 50 0D 20 20 10 A4 00 79 73 87 2A 50 E2");

}  // namespace

TEST_SUITE(framediff)
{
    // ---- identical lists ---------------------------------------------------
    {
        QVector<FieldRow> a{ fr("PKT_TYPE", "10"), fr("SPEED", "40") };
        const auto rows = FrameDiff::compare(a, a);
        const auto sum  = FrameDiff::summarize(rows);
        CHECK(sum.identical(), "identical field lists report no differences");
        CHECK(sum.same == 2, "and every row is counted as same");
    }

    // ---- one changed value -------------------------------------------------
    {
        QVector<FieldRow> a{ fr("PKT_TYPE", "10"), fr("SPEED", "40"), fr("DIR", "1") };
        QVector<FieldRow> b{ fr("PKT_TYPE", "10"), fr("SPEED", "45"), fr("DIR", "1") };
        const auto rows = FrameDiff::compare(a, b);
        const auto sum  = FrameDiff::summarize(rows);
        CHECK(sum.changed == 1 && sum.same == 2, "exactly the changed field is flagged");
        CHECK(kindOf(rows, "SPEED") == Kind::Changed, "and it is the right one");
        for (const FrameDiff::Row &r : rows) {
            if (r.field == "SPEED") {
                CHECK(r.left == "40" && r.right == "45", "both values are carried");
            }
        }
    }

    // ---- a row present on one side only ------------------------------------
    // The case that makes naive positional comparison useless: everything
    // after the extra row would otherwise be reported as changed.
    {
        QVector<FieldRow> a{ fr("A", "1"), fr("B", "2"),               fr("D", "4") };
        QVector<FieldRow> b{ fr("A", "1"), fr("B", "2"), fr("C", "3"), fr("D", "4") };
        const auto rows = FrameDiff::compare(a, b);
        const auto sum  = FrameDiff::summarize(rows);
        CHECK(sum.onlyRight == 1, "the extra row is reported as right-only");
        CHECK(sum.changed == 0, "and nothing after it is dragged out of step");
        CHECK(sum.same == 3, "the shared rows still line up");
        CHECK(kindOf(rows, "C") == Kind::OnlyRight, "the extra row is named");
    }
    {
        QVector<FieldRow> a{ fr("A", "1"), fr("X", "9"), fr("B", "2") };
        QVector<FieldRow> b{ fr("A", "1"),               fr("B", "2") };
        const auto rows = FrameDiff::compare(a, b);
        CHECK(FrameDiff::summarize(rows).onlyLeft == 1, "left-only rows too");
        CHECK(kindOf(rows, "X") == Kind::OnlyLeft, "and are named");
    }

    // ---- repeated field names --------------------------------------------
    // Subpackets repeat FRAME_OFFSET; aligning on the bare name would pair
    // subpacket 1 with subpacket 3 and report both as changed.
    {
        QVector<FieldRow> a{ fr("OFF", "1"), fr("OFF", "2"), fr("OFF", "3") };
        QVector<FieldRow> b{ fr("OFF", "1"), fr("OFF", "9"), fr("OFF", "3") };
        const auto rows = FrameDiff::compare(a, b);
        const auto sum  = FrameDiff::summarize(rows);
        CHECK(sum.changed == 1, "only the middle occurrence differs");
        CHECK(sum.same == 2, "the first and third pair up with their own");
    }

    // ---- empty sides -------------------------------------------------------
    {
        QVector<FieldRow> a{ fr("A", "1") };
        CHECK(FrameDiff::summarize(FrameDiff::compare(a, {})).onlyLeft == 1,
              "an empty right side leaves every row left-only");
        CHECK(FrameDiff::summarize(FrameDiff::compare({}, a)).onlyRight == 1,
              "and the other way round");
        CHECK(FrameDiff::compare({}, {}).isEmpty(), "two empty sides diff to nothing");
    }

    // ---- byte-level ---------------------------------------------------------
    {
        const QByteArray x = QByteArray::fromHex("00112233");
        const QByteArray y = QByteArray::fromHex("00FF2233");
        const auto d = FrameDiff::differingBytes(x, y);
        CHECK(d.size() == 1 && d.first() == 1, "the differing byte offset is found");
        CHECK(FrameDiff::differingBytes(x, x).isEmpty(), "identical buffers differ nowhere");

        // A length difference: every byte past the shorter frame differs,
        // because there is nothing on the other side to equal.
        const QByteArray shortY = QByteArray::fromHex("0011");
        CHECK(FrameDiff::differingBytes(x, shortY).size() == 2,
              "bytes past the shorter frame count as differing");
    }

    // ---- input parsing ------------------------------------------------------
    {
        QString err;
        CHECK(FrameDiff::parseInput(kLsrpA, QString(), &err).valid,
              "a capture line parses on its own");
        CHECK(err.isEmpty(), "with no error");

        CHECK(!FrameDiff::parseInput(QStringLiteral("91 99 C5"), QString(), &err).valid,
              "bare hex without a type is refused");
        CHECK(!err.isEmpty(), "and says why");

        const CaptureLine c = FrameDiff::parseInput(
            QStringLiteral("0x91,0x99,0xC5 DE"), QStringLiteral("slrp"), &err);
        CHECK(c.valid, "bare hex with a type is accepted");
        CHECK(c.bytes == QByteArray::fromHex("9199C5DE"),
              "and separators / 0x prefixes are stripped");

        CHECK(!FrameDiff::parseInput(QStringLiteral("91 99 C"), QStringLiteral("slrp"),
                                     &err).valid,
              "an odd number of hex digits is refused");
        CHECK(!FrameDiff::parseInput(QStringLiteral("zz zz"), QStringLiteral("slrp"),
                                     &err).valid,
              "non-hex is refused");
        CHECK(!FrameDiff::parseInput(QString(), QString(), &err).valid,
              "empty input is refused");
    }

    // ---- the window renders what compare() says ----------------------------
    // The core being right does not mean the window shows it: "show only
    // differences" is on by default, so the table must be shorter than the
    // full field list and must still contain the changed rows.
    {
        FrameDiffWindow w;
        w.setSide(0, kLsrpA);
        w.setSide(1, kLsrpB);

        QTableWidget *table = w.findChild<QTableWidget *>();
        CHECK(table != nullptr, "the window has a diff table");

        QString err;
        const CaptureLine a = FrameDiff::parseInput(kLsrpA, QString(), &err);
        const CaptureLine b = FrameDiff::parseInput(kLsrpB, QString(), &err);
        const auto rows = FrameDiff::compare(CaptureDecoder::describe(a),
                                             CaptureDecoder::describe(b));
        const auto sum = FrameDiff::summarize(rows);

        const int shown = sum.changed + sum.onlyLeft + sum.onlyRight;
        CHECK(table && table->rowCount() == shown,
              "only the differing rows are shown by default");

        QCheckBox *only = w.findChild<QCheckBox *>();
        CHECK(only != nullptr && only->isChecked(),
              "'show only differences' is the default");
        if (only) {
            only->setChecked(false);
            CHECK(table && table->rowCount() == rows.size(),
                  "unticking it shows every field");
        }

        // Two identical frames: nothing to show, and no crash on an empty diff.
        w.setSide(1, kLsrpA);
        if (only) { only->setChecked(true); }
        CHECK(table && table->rowCount() == 0,
              "two identical frames leave the table empty");
    }

    // ---- end to end on two real frames -------------------------------------
    {
        QString err;
        const CaptureLine a = FrameDiff::parseInput(kLsrpA, QString(), &err);
        const CaptureLine b = FrameDiff::parseInput(kLsrpB, QString(), &err);
        CHECK(a.valid && b.valid, "both real frames parse");

        const auto rows = FrameDiff::compare(CaptureDecoder::describe(a),
                                             CaptureDecoder::describe(b));
        const auto sum = FrameDiff::summarize(rows);
        CHECK(!rows.isEmpty(), "two real frames produce a diff");
        CHECK(sum.changed > 0, "two frames two seconds apart have moving fields");
        CHECK(sum.same > 0, "and unchanged ones — not everything is reported changed");
        CHECK(countOf(rows, Kind::Changed) == sum.changed, "the counts agree");

        // Same frame against itself: the strongest statement the alignment can
        // make, and it exercises the real conditional branches.
        const auto self = FrameDiff::compare(CaptureDecoder::describe(a),
                                             CaptureDecoder::describe(a));
        CHECK(FrameDiff::summarize(self).identical(),
              "a real frame diffed against itself shows no differences");
    }
}

// =============================================================================
//  More than two frames.
//
//  Three answers a question two cannot: which of these is the odd one out, and
//  does this field change every frame or only at the transition. Comparing in
//  pairs means holding the third in your head.
//
//  Alignment here is by field NAME AND OCCURRENCE, not the LCS the pairwise
//  path uses. LCS does not generalise to N lists, and the usual dodge —
//  aligning everything against the first frame — makes the answer depend on
//  which frame you pasted first. A diff should not have that property.
// =============================================================================

TEST_SUITE(framediffmany)
{
    auto row = [](const QString &f, const QString &v) {
        FieldRow r; r.field = f; r.value = v; return r;
    };

    // Three frames of the same shape; TRAIN_SPEED differs in the middle one.
    const QVector<FieldRow> a{ row("PKT_TYPE", "10"), row("TRAIN_SPEED", "40"),
                               row("MOVEMENT_DIR", "1") };
    const QVector<FieldRow> b{ row("PKT_TYPE", "10"), row("TRAIN_SPEED", "45"),
                               row("MOVEMENT_DIR", "1") };
    const QVector<FieldRow> c{ row("PKT_TYPE", "10"), row("TRAIN_SPEED", "40"),
                               row("MOVEMENT_DIR", "1") };

    {
        const QVector<FrameDiff::MultiRow> rows = FrameDiff::compareMany({ a, b, c });
        CHECK(rows.size() == 3, "one row per distinct field");

        const FrameDiff::MultiSummary s = FrameDiff::summarizeMany(rows);
        CHECK(s.same == 2 && s.changed == 1 && s.partial == 0,
              "two fields agree across all three, one does not");
        CHECK(!s.identical(), "so the frames are not identical");

        // The point of the third frame.
        for (const FrameDiff::MultiRow &r : rows) {
            if (r.field != QLatin1String("TRAIN_SPEED")) { continue; }
            CHECK(r.values == QVector<QString>({ "40", "45", "40" }),
                  "every frame's value is kept, in the order given");
            CHECK(r.everywhere && !r.allSame, "present everywhere, not all equal");
            const QVector<int> odd = FrameDiff::oddOnesOut(r);
            CHECK(odd == QVector<int>({ 1 }), "and B is named as the odd one out");
        }
    }

    // ---- no majority means no odd one out -----------------------------------
    // With 40, 40, 45, 45 there is no minority, and inventing one would be
    // worse than saying nothing.
    {
        const QVector<FieldRow> d{ row("PKT_TYPE", "10"), row("TRAIN_SPEED", "45"),
                                   row("MOVEMENT_DIR", "1") };
        const QVector<FrameDiff::MultiRow> rows = FrameDiff::compareMany({ a, c, b, d });
        for (const FrameDiff::MultiRow &r : rows) {
            if (r.field != QLatin1String("TRAIN_SPEED")) { continue; }
            CHECK(FrameDiff::oddOnesOut(r).isEmpty(),
                  "an even split has no odd one out");
        }
    }

    // ---- a field missing from one frame -------------------------------------
    {
        const QVector<FieldRow> shorter{ row("PKT_TYPE", "10"), row("TRAIN_SPEED", "40") };
        const QVector<FrameDiff::MultiRow> rows = FrameDiff::compareMany({ a, shorter, c });
        const FrameDiff::MultiSummary s = FrameDiff::summarizeMany(rows);
        CHECK(s.partial == 1, "the missing field is counted as partial, not as changed");
        for (const FrameDiff::MultiRow &r : rows) {
            if (r.field != QLatin1String("MOVEMENT_DIR")) { continue; }
            CHECK(r.present == QVector<bool>({ true, false, true }),
                  "and the frame it is missing from is marked absent");
            CHECK(!r.everywhere, "so the row is not everywhere");
            CHECK(r.allSame, "while the frames that do have it still agree");
        }
    }

    // ---- repeated field names line up nth-with-nth --------------------------
    {
        const QVector<FieldRow> p{ row("FAULT", "1"), row("FAULT", "2"), row("FAULT", "3") };
        const QVector<FieldRow> q{ row("FAULT", "1"), row("FAULT", "9"), row("FAULT", "3") };
        const QVector<FrameDiff::MultiRow> rows = FrameDiff::compareMany({ p, q });
        CHECK(rows.size() == 3, "three occurrences stay three rows, not one");
        CHECK(rows[0].allSame && !rows[1].allSame && rows[2].allSame,
              "and it is the second occurrence that differs");
    }

    // ---- order independence --------------------------------------------------
    // The same three frames in a different order must produce the same answer
    // about how many fields differ. Aligning against "the first frame" would
    // not guarantee this.
    {
        const FrameDiff::MultiSummary s1 = FrameDiff::summarizeMany(FrameDiff::compareMany({ a, b, c }));
        const FrameDiff::MultiSummary s2 = FrameDiff::summarizeMany(FrameDiff::compareMany({ c, a, b }));
        CHECK(s1.changed == s2.changed && s1.same == s2.same && s1.partial == s2.partial,
              "reordering the frames does not change what differs");
    }

    // ---- degenerate inputs ---------------------------------------------------
    {
        CHECK(FrameDiff::compareMany({}).isEmpty(), "no frames, no rows");
        CHECK(FrameDiff::compareMany({ a }).size() == 3,
              "one frame lists its fields and calls them all the same");
        const FrameDiff::MultiSummary s = FrameDiff::summarizeMany(FrameDiff::compareMany({ a }));
        CHECK(s.changed == 0, "with nothing to differ from");
    }
}

// The window itself: adding and removing frame columns, and the floor of two.
#include "framediffwindow.h"
#include <QApplication>

TEST_SUITE(framediffwindowcols)
{
    FrameDiffWindow w;
    CHECK(w.frameCount() == 2, "it opens with two frames, as before");

    w.setSide(2, QStringLiteral(""));
    CHECK(w.frameCount() == 3,
          "loading a third frame grows the window rather than being ignored");

    for (int i = 0; i < 10; ++i) { w.setSide(FrameDiffWindow::maxFrames() - 1, QString()); }
    CHECK(w.frameCount() == FrameDiffWindow::maxFrames(),
          "and it stops at the working limit");

    w.setSide(FrameDiffWindow::maxFrames() + 3, QStringLiteral("ignored"));
    CHECK(w.frameCount() == FrameDiffWindow::maxFrames(),
          "an index past the limit is refused rather than growing forever");

    // Two log rows still behave exactly as they did.
    auto mk = [](const QString &text) {
        auto e = QSharedPointer<LogEntry>::create();
        e->header.source_id = 21; e->header.kvchId = 1;
        e->epochMs = 1000; e->severity = Severity::Info; e->text = text;
        e->cacheDerived();
        return e;
    };
    w.setEntries({ mk(QStringLiteral("a")), mk(QStringLiteral("b")) });
    CHECK(w.frameCount() == 2, "two entries shrink it back to two columns");
    CHECK(FrameDiffWindow::maxFrames() >= 3,
          "the limit leaves room for the three-frame case this was added for");
}
