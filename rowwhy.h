#ifndef ROWWHY_H
#define ROWWHY_H

// =============================================================================
//  rowwhy.h -- "Why is this row coloured?" in plain words (session 335)
//  ---------------------------------------------------------------------------
//  The row menu's "Why this colour?" (session 44) answered with rule numbers
//  and regular expressions: right for whoever edits color_rules.json, opaque
//  to someone reading a log. This says it in sentences, most important
//  first: what a receiver would do with the frame (reject rules, with their
//  clauses), whether its CRC failed, and which colour rule set its
//  severity. The rule-by-rule attribution stays under Details.
// =============================================================================

#include <QString>

class ColorRules;
class QWidget;

namespace RowWhy {

// Plain lines, one reason each, for `text` (a log row's message).
QString explain(const QString &text, const ColorRules &rules);

// The row menu's dialog: explain() as its text, the rule-by-rule
// attribution under Details. Shared by the log tabs and Compare.
void showDialog(QWidget *parent, const QString &text, const ColorRules &rules);

}  // namespace RowWhy

#endif  // ROWWHY_H
