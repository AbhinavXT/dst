#ifndef COLORRULES_H
#define COLORRULES_H

// =============================================================================
//  ColorRules
//  -----------------------------------------------------------------------------
//  Classifies a payload and returns a RuleMatch structure carrying both
//  semantic tags (severity, direction) and the colors to render with.
//
//  Patch C extension: each rule may now declare BOTH a light-theme and a
//  dark-theme color pair. The model picks one set or the other at paint
//  time based on the active theme. Backwards-compatible: rules with no
//  _dark variants reuse their light colors in dark mode (which usually
//  looks bad — operators are encouraged to set dark variants explicitly).
//
//  JSON format:
//
//      [
//        { "pattern": "\\bError\\b",
//          "fg":      "red",          "bg":      "#ffe0e0",
//          "fg_dark": "#ff8080",      "bg_dark": "#5a2020",
//          "severity": "error" },
//
//        { "pattern": "\\bIN\\b",
//          "fg":      "blue",         "bg":      "#e0f0ff",
//          "fg_dark": "#7fb3ff",      "bg_dark": "#1e2a3c",
//          "direction": "in" }
//      ]
//
//  All four color fields (fg, bg, fg_dark, bg_dark) are optional. Empty
//  string or omitted = "no preference, use the default for that role".
//
//  Priority: order in the JSON IS the priority, but "first match wins"
//  applies PER ATTRIBUTE, not to the rule as a whole. Colors come from the
//  first matching rule; severity comes from the first matching rule that
//  DECLARES a severity; direction from the first that declares a direction.
//
//  This matters. With whole-rule first-match, a decorative rule like
//      { "pattern": "\\bRAD\\b.*\\b(IN|OUT)\\b", "fg": "darkblue" }
//  sitting above the \bIN\b / \bOUT\b rules would swallow every RAD frame
//  and return Direction::None for it — so the Dir column and the direction
//  field in every saved log line came out blank for exactly the traffic
//  where direction matters most. Per-attribute resolution means a rule that
//  only wants to set colors no longer silently clears the tags.
//
//  Omitting "severity"/"direction" means "don't declare" (fall through).
//  Writing "severity": "info" or "direction": "none" is an explicit
//  declaration and DOES stop the search.
//
//  "case_sensitive": true makes a rule's regex case-sensitive. Default is
//  false for backwards compatibility, but direction rules should set it —
//  a case-insensitive \bIN\b matches the English word "in", so
//  "timeout in link 1" was being tagged as an inbound message.
// =============================================================================

#include <QColor>
#include <QRegularExpression>
#include <QString>
#include <QVector>

#include "logentry.h"     // for Severity, Direction enums

class ColorRules
{
public:
    // Result of classifying one payload string. Carries BOTH theme
    // variants — the caller stores them on the LogEntry and picks the
    // one matching the active theme at paint time.
    struct RuleMatch {
        bool      matched   = false;
        QColor    fgLight;
        QColor    bgLight;
        QColor    fgDark;
        QColor    bgDark;
        Severity  severity  = Severity::Info;
        Direction direction = Direction::None;

        // Which rule supplied each attribute, as an index into the rule
        // list. -1 means "nothing matched; this is the default".
        //
        // Provenance is recorded because attributes resolve INDEPENDENTLY:
        // colours can come from rule 0 while severity comes from rule 3 and
        // direction from rule 5. Reporting only "rule 0 matched" would be
        // actively misleading, and that ambiguity is exactly what hid the
        // shipped colour_rules.json bug where decorative RAD/CAN rules
        // pinned severity to info and swallowed every real error. With
        // these fields a user can see "severity: rule 1" and go look at
        // rule 1.
        int colorRule     = -1;
        int severityRule  = -1;
        int directionRule = -1;
    };

    // Human-readable explanation of how `text` was classified: which rules
    // supplied which attributes, and which rules matched but contributed
    // nothing. Used by the UI's "why is this row coloured?" affordance.
    QString explainClassification(const QString &text) const;

    // Source pattern of a rule, for display. Empty if out of range.
    QString rulePattern(int index) const;

    // Load rules from `path`. Returns true on success. On failure the
    // built-in defaults are loaded instead and the function returns false.
    // After this call ruleCount() is always >= 1.
    bool loadFromFile(const QString &path);

    // Classify a payload string. The first matching rule wins; if none
    // match, RuleMatch::matched is false and the caller should treat the
    // entry as untagged.
    RuleMatch classify(const QString &text) const;

    int ruleCount() const { return m_rules.size(); }

private:
    struct Rule {
        QRegularExpression pattern;
        QColor             fgLight;
        QColor             bgLight;
        QColor             fgDark;
        QColor             bgDark;
        Severity           severity     = Severity::Info;
        Direction          direction    = Direction::None;
        // Tri-state: distinguishes "this rule says Info/None" from "this
        // rule says nothing about it". Without this the two are the same
        // value and every rule looks like it declares both.
        bool               hasSeverity  = false;
        bool               hasDirection = false;
    };

    void loadDefaults();
    // Each returns false via the `has` out-param when the attribute was
    // absent or empty, i.e. the rule declines to declare it.
    static Severity  parseSeverity (const QString &s, bool *has);
    static Direction parseDirection(const QString &s, bool *has);

    QVector<Rule> m_rules;
};

#endif // COLORRULES_H
