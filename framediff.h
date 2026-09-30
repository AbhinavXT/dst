#ifndef FRAMEDIFF_H
#define FRAMEDIFF_H
// =====================================================================
//  framediff.{h,cpp} -- compare two frames FIELD BY FIELD.
//
//  A byte diff of two bit-packed Kavach frames is close to useless: one
//  changed 9-bit field smears across two bytes and shifts nothing else,
//  so the bytes say "two bytes differ" when the answer wanted is
//  "TRAIN_SPEED went from 40 to 45". This compares the decoded field
//  lists instead, and reports the byte offsets separately as a sanity
//  check rather than as the headline.
//
//  The two field lists are NOT necessarily parallel. A conditional
//  branch (<when> in the schema), a different subpacket mix, or a
//  different repeat count all change which rows exist, so the lists are
//  aligned by an LCS over field names before being compared -- rows
//  present on one side only are reported as such instead of dragging
//  every later row out of step and reporting the whole frame as changed.
//
//  Nothing here touches Qt widgets: the window is a thin renderer over
//  compare(), and the interesting cases are all reachable from a test.
// =====================================================================
#include <QByteArray>
#include <QString>
#include <QVector>

#include "capturedecoder.h"       // FieldRow, CaptureLine

namespace FrameDiff {

enum class Kind {
    Same,        // same field, same value
    Changed,     // same field, different value
    OnlyLeft,    // the right frame has no such row here
    OnlyRight
};

struct Row {
    QString field;
    QString left;
    QString right;
    Kind    kind = Kind::Same;
};

struct Summary {
    int same      = 0;
    int changed   = 0;
    int onlyLeft  = 0;
    int onlyRight = 0;
    int total() const { return same + changed + onlyLeft + onlyRight; }
    bool identical() const { return changed == 0 && onlyLeft == 0 && onlyRight == 0; }
};

// Align and compare. Order follows the left frame, with right-only rows
// inserted where they fall.
QVector<Row> compare(const QVector<FieldRow> &left, const QVector<FieldRow> &right);

Summary summarize(const QVector<Row> &rows);

// ---- more than two --------------------------------------------------------
//
// Three frames answer a question two cannot: "which of these is the odd one
// out", and "does this field change every frame or only at the transition".
// Comparing them in pairs means holding the third in your head.
//
// Alignment here is by field NAME AND OCCURRENCE, not by the LCS the pairwise
// path uses. LCS does not generalise to N lists — the usual dodge is to align
// everything against the first frame, which makes the answer depend on which
// frame you happened to paste first, and that is not a property a diff should
// have. Name+ordinal is order-independent: the nth ARP_HEALTH of one frame
// lines up with the nth of every other, and a name that appears in some
// frames and not others shows as absent there rather than dragging the rest
// out of step.
//
// compare() is left alone, so a two-frame diff keeps the alignment that
// handles a <when> branch shifting the rows after it.
struct MultiRow {
    QString          field;
    QVector<QString> values;    // one per frame, in the order given
    QVector<bool>    present;   // false where that frame has no such row
    bool everywhere = true;     // present in every frame
    bool allSame    = true;     // same value across the frames that have it
};

struct MultiSummary {
    int same    = 0;   // present everywhere, one value
    int changed = 0;   // present everywhere, more than one value
    int partial = 0;   // missing from at least one frame
    int frames  = 0;
    int total() const { return same + changed + partial; }
    bool identical() const { return changed == 0 && partial == 0; }
};

QVector<MultiRow>  compareMany(const QVector<QVector<FieldRow>> &frames);
MultiSummary       summarizeMany(const QVector<MultiRow> &rows);

// Which frames hold the minority value for a row — the "odd one out" the
// third frame was added to find. Empty when the row is the same everywhere
// or when no single value has a majority.
QVector<int> oddOnesOut(const MultiRow &row);

// Byte offsets at which the two buffers differ, plus the offsets that exist
// in only one of them (a length difference is reported as differing bytes
// from the shorter length onwards).
QVector<int> differingBytes(const QByteArray &a, const QByteArray &b);

// Accept what an operator will actually paste: a whole capture line
// ("@slrp_1_1 <ts> <seq> <hex…>"), or bare hex in any separator style, in
// which case `typeToken` (may be empty) says how to decode it.
// `err` is set and the result is invalid when the text is not usable.
CaptureLine parseInput(const QString &text, const QString &typeToken, QString *err);

}  // namespace FrameDiff

#endif  // FRAMEDIFF_H
