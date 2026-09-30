#include "framediff.h"

#include <QRegularExpression>
#include <QPair>
#include <QSet>
#include <QHash>
#include <QStringList>

namespace FrameDiff {

namespace {

// Field names repeat within one frame (every subpacket has a FRAME_OFFSET,
// every repeat element the same members), so the alignment key has to carry
// which occurrence this is or the LCS happily pairs subpacket 1's field with
// subpacket 4's.
QVector<QString> alignmentKeys(const QVector<FieldRow> &rows)
{
    QVector<QString> keys;
    keys.reserve(rows.size());
    QHash<QString, int> seen;
    for (const FieldRow &r : rows) {
        const int n = seen[r.field]++;
        keys.push_back(n == 0 ? r.field : QStringLiteral("%1#%2").arg(r.field).arg(n));
    }
    return keys;
}

// Standard LCS table. Frames decode to a few hundred rows at most, so the
// O(n*m) table is small; the cap below is a guard against a pathological
// input (a corrupt length field yielding thousands of rows) turning a UI
// action into a freeze, not an expected case.
const int kMaxRowsForLcs = 2000;

QVector<QPair<int, int>> lcsPairs(const QVector<QString> &a, const QVector<QString> &b)
{
    const int n = a.size(), m = b.size();
    QVector<QVector<int>> dp(n + 1, QVector<int>(m + 1, 0));
    for (int i = n - 1; i >= 0; --i) {
        for (int j = m - 1; j >= 0; --j) {
            dp[i][j] = (a[i] == b[j]) ? dp[i + 1][j + 1] + 1
                                      : qMax(dp[i + 1][j], dp[i][j + 1]);
        }
    }
    QVector<QPair<int, int>> pairs;
    int i = 0, j = 0;
    while (i < n && j < m) {
        if (a[i] == b[j]) { pairs.push_back({i, j}); ++i; ++j; }
        else if (dp[i + 1][j] >= dp[i][j + 1]) { ++i; }
        else { ++j; }
    }
    return pairs;
}

}  // namespace

QVector<Row> compare(const QVector<FieldRow> &left, const QVector<FieldRow> &right)
{
    QVector<Row> out;

    // Too big to align properly: fall back to positional comparison rather
    // than refusing to answer. Says the same thing for parallel frames, and
    // parallel is what a frame that large will be.
    if (left.size() > kMaxRowsForLcs || right.size() > kMaxRowsForLcs) {
        const int n = qMax(left.size(), right.size());
        for (int i = 0; i < n; ++i) {
            Row r;
            if (i < left.size() && i < right.size()) {
                r.field = left[i].field;
                r.left  = left[i].value;
                r.right = right[i].value;
                r.kind  = (left[i].field == right[i].field && r.left == r.right)
                              ? Kind::Same : Kind::Changed;
            } else if (i < left.size()) {
                r.field = left[i].field;  r.left = left[i].value;  r.kind = Kind::OnlyLeft;
            } else {
                r.field = right[i].field; r.right = right[i].value; r.kind = Kind::OnlyRight;
            }
            out.push_back(r);
        }
        return out;
    }

    const QVector<QString> ka = alignmentKeys(left);
    const QVector<QString> kb = alignmentKeys(right);
    const QVector<QPair<int, int>> pairs = lcsPairs(ka, kb);

    int i = 0, j = 0;
    auto emitLeft = [&](int idx) {
        out.push_back({ left[idx].field, left[idx].value, QString(), Kind::OnlyLeft });
    };
    auto emitRight = [&](int idx) {
        out.push_back({ right[idx].field, QString(), right[idx].value, Kind::OnlyRight });
    };

    for (const auto &p : pairs) {
        while (i < p.first)  { emitLeft(i++); }
        while (j < p.second) { emitRight(j++); }
        const bool same = (left[i].value == right[j].value);
        out.push_back({ left[i].field, left[i].value, right[j].value,
                        same ? Kind::Same : Kind::Changed });
        ++i; ++j;
    }
    while (i < left.size())  { emitLeft(i++); }
    while (j < right.size()) { emitRight(j++); }
    return out;
}

Summary summarize(const QVector<Row> &rows)
{
    Summary s;
    for (const Row &r : rows) {
        switch (r.kind) {
        case Kind::Same:      ++s.same;      break;
        case Kind::Changed:   ++s.changed;   break;
        case Kind::OnlyLeft:  ++s.onlyLeft;  break;
        case Kind::OnlyRight: ++s.onlyRight; break;
        }
    }
    return s;
}

QVector<int> differingBytes(const QByteArray &a, const QByteArray &b)
{
    QVector<int> out;
    const int n = qMin(a.size(), b.size());
    for (int i = 0; i < n; ++i) {
        if (a[i] != b[i]) { out.push_back(i); }
    }
    // Past the shorter frame every remaining byte differs by definition —
    // there is nothing on the other side to equal.
    for (int i = n; i < qMax(a.size(), b.size()); ++i) { out.push_back(i); }
    return out;
}

CaptureLine parseInput(const QString &text, const QString &typeToken, QString *err)
{
    auto fail = [&](const QString &m) {
        if (err) { *err = m; }
        return CaptureLine();
    };

    const QString t = text.trimmed();
    if (t.isEmpty()) { return fail(QStringLiteral("empty")); }

    // A whole capture line already carries its type and is the exact thing
    // the log holds, so it is passed through untouched.
    if (t.startsWith(QLatin1Char('@'))) {
        const CaptureLine c = CaptureDecoder::parseLine(t);
        if (!c.valid) { return fail(QStringLiteral("not a usable capture line")); }
        if (err) { err->clear(); }
        return c;
    }

    // Bare hex: strip the separator styles a paste arrives in.
    QString hex = t;
    hex.remove(QRegularExpression(QStringLiteral("0[xX]")));
    hex.remove(QRegularExpression(QStringLiteral("[\\s,;:_]")));
    if (hex.isEmpty()) { return fail(QStringLiteral("no bytes")); }
    if (hex.contains(QRegularExpression(QStringLiteral("[^0-9A-Fa-f]")))) {
        return fail(QStringLiteral("contains non-hex characters"));
    }
    if (hex.size() % 2 != 0) { return fail(QStringLiteral("odd number of hex digits")); }
    if (typeToken.isEmpty()) {
        return fail(QStringLiteral("pick a packet type for bare hex"));
    }

    // Rebuild it as a capture line so there is ONE decode path — the same one
    // the log view uses — rather than a second, subtly different one here.
    const QString line = QStringLiteral("@%1_1_1 2000-01-01T00:00:00 0 %2")
                             .arg(typeToken, hex);
    const CaptureLine c = CaptureDecoder::parseLine(line);
    if (!c.valid) { return fail(QStringLiteral("could not decode as %1").arg(typeToken)); }
    if (err) { err->clear(); }
    return c;
}

}  // namespace FrameDiff

// ---------------------------------------------------------------------------
//  N-way
// ---------------------------------------------------------------------------

QVector<FrameDiff::MultiRow>
FrameDiff::compareMany(const QVector<QVector<FieldRow>> &frames)
{
    QVector<MultiRow> out;
    if (frames.isEmpty()) { return out; }

    // Key = field name plus how many times that name has already appeared in
    // the same frame, so repeated fields line up nth-with-nth.
    auto keysOf = [](const QVector<FieldRow> &rows) {
        QVector<QPair<QString, QString>> kv;      // key -> value
        QHash<QString, int> seen;
        kv.reserve(rows.size());
        for (const FieldRow &r : rows) {
            const int n = seen[r.field]++;
            kv.push_back({ QStringLiteral("%1#%2").arg(r.field).arg(n), r.value });
        }
        return kv;
    };

    QVector<QHash<QString, QString>> byKey;
    byKey.reserve(frames.size());
    QVector<QString> order;                        // first-seen order, all frames
    QSet<QString> known;
    QHash<QString, QString> labelOf;               // key -> field name to show

    for (const QVector<FieldRow> &rows : frames) {
        QHash<QString, QString> map;
        const auto kv = keysOf(rows);
        for (int i = 0; i < kv.size(); ++i) {
            map.insert(kv[i].first, kv[i].second);
            if (!known.contains(kv[i].first)) {
                known.insert(kv[i].first);
                order.push_back(kv[i].first);
                labelOf.insert(kv[i].first, rows[i].field);
            }
        }
        byKey.push_back(map);
    }

    out.reserve(order.size());
    for (const QString &key : order) {
        MultiRow mr;
        mr.field = labelOf.value(key);
        mr.values.reserve(frames.size());
        mr.present.reserve(frames.size());

        QString first;
        bool haveFirst = false;
        for (const QHash<QString, QString> &map : byKey) {
            const bool has = map.contains(key);
            mr.present.push_back(has);
            mr.values.push_back(has ? map.value(key) : QString());
            if (!has) { mr.everywhere = false; continue; }
            if (!haveFirst) { first = map.value(key); haveFirst = true; }
            else if (map.value(key) != first) { mr.allSame = false; }
        }
        out.push_back(mr);
    }
    return out;
}

FrameDiff::MultiSummary
FrameDiff::summarizeMany(const QVector<MultiRow> &rows)
{
    MultiSummary s;
    for (const MultiRow &r : rows) {
        s.frames = qMax(s.frames, r.values.size());
        if (!r.everywhere)   { ++s.partial; }
        else if (r.allSame)  { ++s.same; }
        else                 { ++s.changed; }
    }
    return s;
}

QVector<int> FrameDiff::oddOnesOut(const MultiRow &row)
{
    QVector<int> odd;
    if (row.allSame) { return odd; }

    QHash<QString, int> tally;
    int counted = 0;
    for (int i = 0; i < row.values.size(); ++i) {
        if (!row.present.value(i)) { continue; }
        ++tally[row.values.at(i)];
        ++counted;
    }

    // A majority, not a plurality: with values 1, 1, 2, 2 there is no odd one
    // out, and saying there is would be inventing an answer.
    QString majority;
    for (auto it = tally.constBegin(); it != tally.constEnd(); ++it) {
        if (it.value() * 2 > counted) { majority = it.key(); break; }
    }
    if (majority.isNull()) { return odd; }

    for (int i = 0; i < row.values.size(); ++i) {
        if (row.present.value(i) && row.values.at(i) != majority) { odd.push_back(i); }
    }
    return odd;
}
