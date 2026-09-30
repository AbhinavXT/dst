#include "testutil.h"

#include "capturedecoder.h"

#include <QDebug>
#include "rejectrules.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

// =============================================================================
//  Reject rules — why a receiver would not process a packet.
//
//  From the SIF-0533 v4.22 functional test cases. The risk in this feature is
//  not that it misses a rule; it is that it asserts a WRONG one. A console
//  that says "PKT_DIR = 0, unidentified [31.16.1]" is making a claim about
//  the document, and an operator will act on it.
//
//  So the tests are mostly about refusing: rules that cannot fire, files that
//  do not parse, and the difference between "nothing matched" and "valid".
// =============================================================================

namespace {

QString writeRules(const QTemporaryDir &dir, const QString &body)
{
    const QString path = dir.filePath(QStringLiteral("r.xml"));
    QFile f(path);
    f.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream(&f) << body;
    f.close();
    return path;
}

}  // namespace

TEST_SUITE(rejectrules)
{
    // ---- the shipped file ---------------------------------------------------
    {
        RejectRules r;
        QString err;
        CHECK(r.load(QStringLiteral(":/schema/rejectrules.xml"), &err),
              "the shipped rule set loads");
        CHECK(err.isEmpty(), "with no complaint");
        CHECK(r.count() >= 20, "and holds the section-A rules");
    }

    RejectRules rules;
    CHECK(rules.load(QStringLiteral(":/schema/rejectrules.xml")), "loaded");

    // ---- the conditions that prompted this ---------------------------------
    {
        QHash<QString, qint64> v{ { QStringLiteral("PKT_DIR"), 0 } };
        const auto f = rules.evaluate(v);
        CHECK(f.size() == 1, "PKT_DIR = 0 fires exactly one rule");
        CHECK(!f.isEmpty() && f.first().rule.clause == QStringLiteral("31.16.1"),
              "and cites the clause it came from, so the claim can be checked "
              "against the document");
        CHECK(!f.isEmpty() && f.first().text.contains(QStringLiteral("PKT_DIR = 0")),
              "and says what the frame actually carried");
    }
    {
        QHash<QString, qint64> v{ { QStringLiteral("PKT_DIR"), 3 } };
        CHECK(rules.evaluate(v).size() == 1, "PKT_DIR = 3 (spare) fires too");
    }
    {
        QHash<QString, qint64> v{ { QStringLiteral("PKT_DIR"), 1 } };
        CHECK(rules.evaluate(v).isEmpty(), "and nominal does not");
    }

    // ---- FRAME_NUM is a range, not two values ------------------------------
    //
    // The document tests 0 and 86401 because they are the boundaries. The
    // field is seconds since midnight plus one, so anything outside 1..86400
    // is the same fault — encoding only the two tested values would pass
    // 90000 silently, which is the failure this feature exists to prevent.
    {
        auto fires = [&rules](qint64 n) {
            return !rules.evaluate({ { QStringLiteral("FRAME_NUM"), n } }).isEmpty();
        };
        CHECK(fires(0),      "FRAME_NUM 0 is out of range");
        CHECK(fires(86401),  "and so is 86401");
        CHECK(fires(90000),  "and so is 90000, which the document never tests");
        CHECK(!fires(1),     "1 is the first valid frame");
        CHECK(!fires(86400), "86400 is the last");
        CHECK(!fires(43200), "and midday is unremarkable");
    }

    // ---- every rule that fires, not the first one --------------------------
    //
    // A malformed frame usually trips several. Reporting one, being fixed,
    // and running again to find the next is the loop this removes.
    {
        QHash<QString, qint64> v{
            { QStringLiteral("PKT_DIR"),        0 },
            { QStringLiteral("FRAME_NUM"),      0 },
            { QStringLiteral("AUTHORITY_TYPE"), 0 },
        };
        const auto f = rules.evaluate(v);
        CHECK(f.size() == 3, "three faults are reported as three");
    }

    // ---- absent fields are not findings ------------------------------------
    //
    // An SLRP carrying only a movement authority has no TSR fields at all.
    // Reporting them as missing on every such frame would bury the real ones.
    {
        CHECK(rules.evaluate({}).isEmpty(),
              "a frame that carries none of the tested fields reports nothing");
        QHash<QString, qint64> v{ { QStringLiteral("PKT_DIR"), 1 } };
        CHECK(rules.evaluate(v).isEmpty(),
              "and a clean frame reports nothing — which means NO RULE "
              "MATCHED, not that the packet is valid");
    }

    QTemporaryDir dir;

    // ---- the aspect values, once read as binary ----------------------------
    //
    // The document writes every aspect in binary and drops leading zeros on
    // the short ones: 0, 1, 10, 100 … 1111, then 010000, 011000, 100000. An
    // earlier reading took the four-character ones for decimal and produced
    // 1001, which does not fit a 6-bit field at all.
    {
        auto fires = [&rules](const char *f, qint64 v) {
            return !rules.evaluate({ { QString::fromLatin1(f), v } }).isEmpty();
        };
        CHECK(fires("CUR_SIG_ASPECT", 9),
              "aspect 9 is spare in the document and in kavach.xml, which has "
              "no v=9 and falls through to Spare");
        CHECK(fires("NEXT_SIG_ASPECT", 9), "on both aspect fields");
        CHECK(fires("CUR_SIG_ASPECT", 16) && fires("CUR_SIG_ASPECT", 31),
              "and the six-character spare values still fire");

        CHECK(!fires("CUR_SIG_ASPECT", 11), "Green is not reported");
        CHECK(!fires("CUR_SIG_ASPECT", 24), "nor Stop Board");
        CHECK(!fires("CUR_SIG_ASPECT", 40), "nor a stencil route");

        // 14 is deliberately absent. The document calls it spare; kavach.xml
        // calls it AG Marker OFF, a real aspect. A rule here would report a
        // legitimate aspect on live traffic, which is the one failure that
        // teaches an operator to ignore the feature.
        CHECK(!fires("CUR_SIG_ASPECT", 14),
              "aspect 14 is NOT reported while the document and the schema "
              "disagree about whether it is spare");
    }

    // ---- a condition that hid behind the packet it was tested on ------------
    {
        CHECK(!rules.evaluate({ { QStringLiteral("TSR_STATUS"), 3 } }).isEmpty(),
              "reserved TSR status is reported — the row names PKT_TYPE 0111 "
              "first, so a first pass read it as a packet-type rule");
        CHECK(rules.evaluate({ { QStringLiteral("TSR_STATUS"), 2 } }).isEmpty(),
              "and a latest TSR is unremarkable");
    }

    // ---- guarded rules: the same value, two answers ------------------------
    //
    // FRAME_OFFSET 14 is why the rule format needed conditions at all. The
    // document appears to contradict itself — 31.19.3 sends 1110 and expects
    // it processed, 31.19.5 says process only below 14 — until you notice
    // they describe different sections. Neither clause can be encoded as a
    // value rule without losing the thing that separates them.
    {
        auto fires = [&rules](qint64 section, qint64 offset) {
            return !rules.evaluate({
                { QStringLiteral("TRAIN_SECTION_TYPE"), section },
                { QStringLiteral("FRAME_OFFSET"),       offset  } }).isEmpty();
        };
        CHECK(!fires(0, 14), "14 stands in a station section");
        CHECK(!fires(1, 14), "and in an absolute-block section");
        CHECK(fires(2, 14),  "but is reported in autoblock");
        CHECK(!fires(0, 13), "13 is unremarkable wherever the train is");
        CHECK(!fires(2, 13), "including where 14 would be reported");

        // 15 is unconditional, so the guard must not swallow it.
        CHECK(fires(0, 15),
              "and the reserved offset 15 is still reported in a station "
              "section, where 14 is not — the guard belongs to one rule");
    }

    // A guarded finding says the guard out loud: "FRAME_OFFSET = 14" alone
    // would look wrong to anyone who knows 14 is fine in a station section.
    {
        const auto f = rules.evaluate({
            { QStringLiteral("TRAIN_SECTION_TYPE"), 2 },
            { QStringLiteral("FRAME_OFFSET"),       14 } });
        CHECK(!f.isEmpty() && f.first().text.contains(QStringLiteral("when ")),
              "a conditional finding states the condition it rests on");
    }

    // ---- a rule that could never fire is refused ---------------------------
    //
    // Six rules in the document test values that do not fit the field they
    // test — SOURCE_STN_ILC_IBS_ID as 65536 in a 16-bit field. Those exercise
    // the generating tool, not the wire. Loading one would put a condition on
    // screen that looks checked and never is.
    {
        RejectRules r;
        QString err;
        const QString p = writeRules(dir, QStringLiteral(
            "<rejectrules>\n"
            "  <rule clause=\"31.10\" field=\"SOURCE_STN_ILC_IBS_ID\" op=\"eq\"\n"
            "        value=\"65536\" bits=\"16\"/>\n"
            "</rejectrules>\n"));
        CHECK(!r.load(p, &err), "a value too wide for its field is refused");
        CHECK(err.contains(QStringLiteral("16 bits")),
              "and the message says which rule and why, so it can be fixed");
    }

    // ---- a guard that does not parse is refused (session 65) ---------------
    // Before 65 a when= with no operator evaluated as "holds", so the rule
    // fired on frames the guard was written to exclude.
    {
        RejectRules r;
        QString err;
        const QString p = writeRules(dir, QStringLiteral(
            "<rejectrules>\n"
            "  <rule clause=\"1.1\" field=\"PKT_DIR\" op=\"eq\" value=\"0\"\n"
            "        when=\"TRAIN_SECTION_TYPE\"/>\n"
            "</rejectrules>\n"));
        CHECK(!r.load(p, &err), "a when= guard with no operator is refused");
        CHECK(err.contains(QStringLiteral("no operator")),
              "and the message says why");
    }

    // ---- a bad file leaves the working rules in place ----------------------
    //
    // Half a rule set is worse than a stale one: it silently stops reporting
    // conditions the operator still believes are being checked.
    {
        const int before = rules.count();
        QString err;
        CHECK(!rules.load(dir.filePath(QStringLiteral("nope.xml")), &err),
              "a missing file fails");
        CHECK(rules.count() == before,
              "and the rules already loaded are untouched");

        const QString bad = writeRules(dir, QStringLiteral(
            "<rejectrules>\n"
            "  <rule clause=\"9.9\" field=\"PKT_DIR\" op=\"eq\" value=\"0\"/>\n"
            "  <rule clause=\"9.9\" field=\"PKT_DIR\" op=\"sideways\" value=\"1\"/>\n"
            "</rejectrules>\n"));
        CHECK(!rules.load(bad, &err), "an unknown operator is refused");
        CHECK(rules.count() == before,
              "and the good rule beside it is not half-applied");
    }

    // ---- other malformed files ---------------------------------------------
    {
        RejectRules r;
        QString err;
        CHECK(!r.load(writeRules(dir, QStringLiteral("<notrules/>")), &err),
              "the wrong root element is refused");
        CHECK(!r.load(writeRules(dir, QStringLiteral("<rejectrules/>")), &err),
              "and so is a file with no rules — an empty rule set would "
              "report nothing and look like a clean packet");
        CHECK(!r.load(writeRules(dir, QStringLiteral(
                  "<rejectrules><rule clause=\"1\" op=\"eq\" value=\"0\"/></rejectrules>")),
              &err),
              "a rule naming no field is refused");
        CHECK(!r.load(writeRules(dir, QStringLiteral("<rejectrules>")), &err),
              "and so is a file that is not XML");
    }

    // ---- the wording is a report, not a verdict ----------------------------
    {
        const auto f = rules.evaluate({ { QStringLiteral("PKT_DIR"), 0 } });
        CHECK(!f.isEmpty(), "there is a finding to word");
        const QString t = f.isEmpty() ? QString() : f.first().text;
        CHECK(!t.contains(QStringLiteral("invalid"), Qt::CaseInsensitive)
                  && !t.contains(QStringLiteral("fail"), Qt::CaseInsensitive)
                  && !t.contains(QStringLiteral("PASS"), Qt::CaseSensitive),
              "no verdict language — the tooling reports the condition and "
              "the clause, and the signatory decides");
    }
}

// =============================================================================
//  End to end: a real capture line to a reported reason.
//
//  The engine tests above feed the evaluator a hand-built map, which proves
//  the rules and proves nothing about the wiring. This runs an actual SLRP
//  capture line through CaptureDecoder::describe() and asks whether the
//  numbers reaching the rules are the numbers the frame carries.
// =============================================================================

TEST_SUITE(rejectrouting)
{
    const QString line = QStringLiteral(
        "@slrp_1_1 2026-06-26T16:29:16 1122 91 31 CF BA 04 1E 80 00 04 40 AF 51 08 01 "
        "80 00 ED 80 10 00 87 2A 03 43 00 10 A1 0D 0C 58 21 43 09 BA 20 14 00 00 34 C6 "
        "52 A8 30 00 00 87 BC 04 99 E0 1A 3E 80 80 F1 02 84 38 0C 00 D0 60 E2 E0 00 03 "
        "43 00 F1 81 93 42 79 00 DE F2");

    const CaptureLine cap = CaptureDecoder::parseLine(line);
    CHECK(cap.valid, "the capture line parses");

    QHash<QString, qint64> raw;
    const QVector<FieldRow> rows = CaptureDecoder::describe(cap, nullptr, 0, &raw);
    CHECK(!rows.isEmpty(), "and decodes to rows");

    // The point of the out-parameter: numbers, not display strings.
    CHECK(!raw.isEmpty(),
          "and hands back the numeric values the rules need — the rows carry "
          "\"2 (Reverse)\" and \"12.5 m\", which no rule can compare against");
    CHECK(raw.contains(QStringLiteral("PKT_DIR")),
          "including the fields the rules actually test");

    // A frame captured off real equipment should trip nothing. If this ever
    // fires, either a rule is wrong or the value plumbing is — and both are
    // worth failing a build over, because a false reject reason on a good
    // frame is the way an operator stops trusting the feature.
    RejectRules rules;
    CHECK(rules.load(QStringLiteral(":/schema/rejectrules.xml")), "rules load");
    const auto found = rules.evaluate(raw);
    if (!found.isEmpty()) {
        for (const auto &f : found) { qWarning("unexpected: %s", qPrintable(f.text)); }
    }
    CHECK(found.isEmpty(),
          "a frame recorded off real equipment trips no reject rule");

    // And the same frame with one field corrupted does trip one, which is
    // what proves the previous check was not passing by disconnection.
    {
        QHash<QString, qint64> bad = raw;
        bad.insert(QStringLiteral("PKT_DIR"), 3);
        const auto f = rules.evaluate(bad);
        CHECK(f.size() == 1, "flip PKT_DIR to the spare value and it fires");
        CHECK(!f.isEmpty() && f.first().rule.clause == QStringLiteral("31.16.4"),
              "citing the clause for it");
    }
}
