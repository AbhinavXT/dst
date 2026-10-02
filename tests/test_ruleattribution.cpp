#include "testutil.h"
#include "colorrules.h"

#include <QFile>
#include <QTemporaryDir>

// Rule attribution. The value of this is not the string formatting — it is
// that the explanation must correctly name WHICH rule supplied each
// attribute when they come from different rules, and must flag rules that
// matched but contributed nothing. That second case is the shape of the
// real colour_rules.json bug found earlier in this project.
TEST_SUITE(ruleattribution)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp dir");

    // ---- defaults: attributes come from DIFFERENT rules ------------------
    {
        ColorRules r;
        r.loadFromFile("/nonexistent");          // built-in defaults

        // "RAD IN Link 1 Error": the decorative RAD rule supplies colours,
        // \bError\b supplies severity, \bIN\b supplies direction. Three
        // different rules for one row — which is exactly why a single
        // "matched rule" number would be misleading.
        const ColorRules::RuleMatch m = r.classify("RAD IN Link 1 Error");
        CHECK(m.matched, "row matched something");
        CHECK(m.severity == Severity::Error,  "severity is error");
        CHECK(m.direction == LogDirection::In,   "direction is in");
        CHECK(m.colorRule >= 0,     "a rule supplied colours");
        CHECK(m.severityRule >= 0,  "a rule supplied severity");
        CHECK(m.directionRule >= 0, "a rule supplied direction");
        CHECK(m.colorRule != m.severityRule,
              "colours and severity really did come from different rules");
        CHECK(m.severityRule != m.directionRule,
              "severity and direction came from different rules");

        const QString why = r.explainClassification("RAD IN Link 1 Error");
        CHECK(why.contains("severity  = error"),  "explains the severity");
        CHECK(why.contains("direction = in"),     "explains the direction");
        CHECK(why.contains(QString("rule #%1").arg(m.severityRule)),
              "names the rule that set severity");
        CHECK(why.contains(QString("rule #%1").arg(m.directionRule)),
              "names the rule that set direction");
        CHECK(why.contains(QString("rule #%1").arg(m.colorRule)),
              "names the rule that set colours");
    }

    // ---- unmatched text reports defaults honestly ------------------------
    {
        ColorRules r;
        r.loadFromFile("/nonexistent");
        const ColorRules::RuleMatch m = r.classify("zzzz nothing matches zzzz");
        CHECK(!m.matched, "nothing matched");
        CHECK(m.colorRule == -1 && m.severityRule == -1 && m.directionRule == -1,
              "no rule is credited");
        const QString why = r.explainClassification("zzzz nothing matches zzzz");
        CHECK(why.contains("No rule matched"), "says so plainly");
        CHECK(why.contains("default"),          "and attributes to defaults");
    }

    // ---- the regression case ---------------------------------------------
    // Recreate the shipped-file bug: a decorative rule that pins severity to
    // info sitting above the real \bError\b rule. The classification is
    // wrong, and the explanation has to make it obvious WHY.
    {
        const QString path = tmp.path() + "/pinned.json";
        QFile f(path); f.open(QIODevice::WriteOnly);
        f.write(R"([
          {"pattern":"\\bRAD\\b.*\\b(IN|OUT)\\b","fg":"blue","severity":"info","direction":"none"},
          {"pattern":"\\bError\\b","fg":"red","severity":"error"},
          {"pattern":"\\bIN\\b","fg":"blue","direction":"in","case_sensitive":true}
        ])");
        f.close();

        ColorRules r;
        CHECK(r.loadFromFile(path), "pinned rules load");

        const ColorRules::RuleMatch m = r.classify("RAD IN Link 1 Error");
        // Rule 0 declares BOTH severity and direction, so it wins both and
        // the genuine error is reported as info. This is the bug.
        CHECK(m.severity == Severity::Info,
              "reproduces the bug: a real error classified as info");
        CHECK(m.severityRule == 0, "severity was supplied by rule #0");
        CHECK(m.directionRule == 0, "direction too");

        const QString why = r.explainClassification("RAD IN Link 1 Error");
        CHECK(why.contains("severity  = info"), "explanation shows the wrong value");
        CHECK(why.contains("rule #0"),          "and names rule #0 as the culprit");
        // And it must show that the correct rule DID match but was ignored —
        // without this the user sees "rule #0 set it" and has no idea a
        // better rule existed further down.
        CHECK(why.contains("Also matched"),
              "flags that other rules matched and contributed nothing");
        CHECK(why.contains("rule #1"),
              "specifically names the \\bError\\b rule that was shadowed");
    }

    // ---- the fixed form ---------------------------------------------------
    // Same rules with the decorative one no longer declaring severity: the
    // error is now classified correctly and attributed to the right rule.
    {
        const QString path = tmp.path() + "/fixed.json";
        QFile f(path); f.open(QIODevice::WriteOnly);
        f.write(R"([
          {"pattern":"\\bRAD\\b.*\\b(IN|OUT)\\b","fg":"blue"},
          {"pattern":"\\bError\\b","fg":"red","severity":"error"},
          {"pattern":"\\bIN\\b","fg":"blue","direction":"in","case_sensitive":true}
        ])");
        f.close();

        ColorRules r;
        CHECK(r.loadFromFile(path), "fixed rules load");
        const ColorRules::RuleMatch m = r.classify("RAD IN Link 1 Error");
        CHECK(m.severity == Severity::Error, "error is now classified correctly");
        CHECK(m.severityRule == 1,  "and attributed to the \\bError\\b rule");
        CHECK(m.directionRule == 2, "direction comes from the \\bIN\\b rule");
        CHECK(m.colorRule == 0,     "colours still come from the decorative rule");
    }

    // ---- rulePattern bounds ----------------------------------------------
    {
        ColorRules r;
        r.loadFromFile("/nonexistent");
        CHECK(!r.rulePattern(0).isEmpty(), "rule 0 has a pattern");
        CHECK(r.rulePattern(-1).isEmpty(), "negative index is empty");
        CHECK(r.rulePattern(9999).isEmpty(), "out-of-range index is empty");
    }
}
