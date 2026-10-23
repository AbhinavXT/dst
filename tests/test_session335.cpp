#include "testutil.h"

#include "capturedecoder.h"
#include "colorrules.h"
#include "rejectrules.h"
#include "rowwhy.h"

#include <QDir>
#include <QFile>
#include <QHash>

// =============================================================================
//  Session 335 — "Why is this row coloured?" in sentences: what a receiver
//  would do with the frame (reject rules and their notes), a failed CRC, and
//  which colour rule set the severity; the rule-by-rule list under Details.
//  Real frames: the first in replay/ that a reject rule fires on, and the
//  first whose CRC fails. Colour rules: the shipped color_rules.json.
// =============================================================================

namespace {
// The first capture line in replay/ for which `want` holds.
template <typename Pred>
QString firstInReplay(Pred want)
{
    QDir dir(QStringLiteral(DL_SRC_DIR "/replay"));
    for (const QString &name : dir.entryList({ QStringLiteral("*.cap") }, QDir::Files, QDir::Name)) {
        QFile f(dir.filePath(name));
        if (!f.open(QIODevice::ReadOnly)) continue;
        while (!f.atEnd()) {
            const QString l = QString::fromLatin1(f.readLine()).trimmed();
            if (!l.startsWith(QLatin1Char('@'))) continue;
            const CaptureLine cap = CaptureDecoder::parseLine(l);
            if (cap.valid && !cap.bytes.isEmpty() && want(cap)) return l;
        }
    }
    return QString();
}
}  // namespace

TEST_SUITE(session335)
{
    ColorRules rules;
    CHECK(rules.loadFromFile(QStringLiteral(DL_SRC_DIR "/color_rules.json")), "fixture: the shipped colour rules");

    // ---- text rows: the colour rule, in words, with its note -----------------------
    {
        const QString why = RowWhy::explain(QStringLiteral("Link 2: PACKET REJECTED"), rules);
        CHECK(why.contains(QStringLiteral("shown as an error because its text matches colour rule #")),
              "an error row: which rule made it one");
        const QString noErr = RowWhy::explain(QStringLiteral("VCC status: No Error"), rules);
        CHECK(noErr.contains(QStringLiteral("shown as information")) && noErr.contains(QStringLiteral("must precede")),
              "and the rule's own _note comes with it");
        CHECK(rules.ruleNote(0) == QStringLiteral("must precede \\bError\\b") && rules.ruleNote(-1).isEmpty()
                  && rules.ruleNote(rules.ruleCount()).isEmpty(),
              "ruleNote reads _note; out of range: empty");
        CHECK(RowWhy::explain(QStringLiteral("plain words"), rules)
                  .contains(QStringLiteral("No colour rule sets its severity")),
              "nothing matched: it says so");
    }

    // ---- a frame a receiver would not process ---------------------------------------
    {
        const QString line = firstInReplay([](const CaptureLine &cap) {
            QHash<QString, qint64> raw;
            CaptureDecoder::describe(cap, nullptr, 0, &raw);
            return !kavachRejectRules().evaluate(raw, cap.typeToken).isEmpty();
        });
        CHECK(!line.isEmpty(), "fixture: a real frame a reject rule fires on");
        if (!line.isEmpty()) {
            const QString why = RowWhy::explain(line, rules);
            QHash<QString, qint64> raw;
            const CaptureLine cap = CaptureDecoder::parseLine(line);
            CaptureDecoder::describe(cap, nullptr, 0, &raw);
            const auto f = kavachRejectRules().evaluate(raw, cap.typeToken);
            CHECK(why.startsWith(QStringLiteral("A receiver would not process this %1 frame:").arg(cap.typeToken.toUpper()))
                      && why.contains(f.first().text),
                  QByteArray("first, what a receiver would do, with the finding: ") + why.left(200).toUtf8());
            CHECK(!why.contains(QStringLiteral("invalid"), Qt::CaseInsensitive), "never \"invalid\"");
        }
    }

    // ---- a frame whose CRC fails ------------------------------------------------------
    {
        const QString line = firstInReplay([](const CaptureLine &cap) { return cap.crcChecked && !cap.crcOk; });
        CHECK(!line.isEmpty(), "fixture: a real frame whose CRC fails");
        if (!line.isEmpty())
            CHECK(RowWhy::explain(line, rules).contains(QStringLiteral("Its CRC does not match its bytes")),
                  "a failed CRC is named");
        const QString good = firstInReplay([](const CaptureLine &cap) { return cap.crcChecked && cap.crcOk; });
        CHECK(!good.isEmpty() && !RowWhy::explain(good, rules).contains(QStringLiteral("CRC")),
              "a frame whose CRC passes: not mentioned");
    }
}
