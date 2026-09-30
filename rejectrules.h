#ifndef REJECTRULES_H
#define REJECTRULES_H
// =============================================================================
//  rejectrules.{h,cpp} — why a receiver would not process this packet.
//
//  The decoder answers "what does this frame say". This answers the question
//  an operator actually asks next: would the equipment at the other end act
//  on it, and if not, which clause says so.
//
//  Rules live in schema/rejectrules.xml, outside the binary, for the same
//  reason kavach.xml does: a clause read wrongly is fixed by editing a file
//  and reloading, not by shipping a build. Each rule carries its FRS clause,
//  so every reason printed can be checked against the document.
//
//  TWO THINGS THIS DELIBERATELY DOES NOT DO.
//
//  It does not say a packet is VALID. Silence here means no rule in the file
//  matched, which is not the same claim — the file holds one section of the
//  document. Findings() returning empty must be reported as "no reject
//  condition matched", never as "valid".
//
//  It does not stop at the first match. A malformed frame usually trips
//  several rules, and knowing all of them is the point: fixing the one the
//  console happened to mention first, rebuilding, and running again to find
//  the next is the loop this is meant to remove.
// =============================================================================
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

class RejectRules
{
public:
    struct Rule {
        QString clause;     // "31.16.1" — the FRS clause this came from
        QString field;      // decoded field name it tests
        QString op;         // "eq" | "outside" | "ne_field" | "eq_field"
        QString other;      // for ne_field: the field to compare against
        qint64  value = 0;  // for eq
        qint64  min   = 0;  // for outside
        qint64  max   = 0;
        // Guard, in the schema's own condition language ("A==1",
        // "A in 3..7", "A not in 0..0"). Empty means unconditional.
        //
        // Needed because several clauses are not value rules at all.
        // FRAME_OFFSET 14 is the case in point: the same value is accepted
        // or rejected depending on the section the train is in, so 31.19.3
        // and 31.19.5 are both right and neither can be encoded without
        // the condition that separates them.
        QString when;

        QString doc;        // the document's own spelling, e.g. "‘11’"
        int     bits  = 0;
        QString note;       // plain words for the operator
        // Session 92: capture types the rule applies to ("arprecv"; comma
        // list). Empty = every frame carrying the field. Needed where the
        // same packet layout arrives by two routes that must be judged
        // differently: an ARP the loco SENDS carries its own ID by design,
        // one it RECEIVES must not.
        QStringList captypes;
    };

    struct Finding {
        Rule    rule;
        qint64  actual = 0;   // what the frame carried
        QString text;         // one line, ready to show
    };

    // Load rules. Returns false and sets `error` on a malformed file; the
    // previous rule set is then left untouched, because half a rule set is
    // more dangerous than a stale one — it would silently stop reporting
    // conditions the operator believes are still being checked.
    bool load(const QString &path, QString *error = nullptr);

    // Every rule that fires, in file order. Empty means no rule matched.
    // `captype` is the frame's capture type ("arprecv"); rules restricted to
    // other types are skipped. Empty skips every restricted rule.
    QVector<Finding> evaluate(const QHash<QString, qint64> &values,
                              const QString &captype = QString()) const;

    int  count() const { return m_rules.size(); }
    bool isEmpty() const { return m_rules.isEmpty(); }
    const QVector<Rule> &rules() const { return m_rules; }
    QString sourcePath() const { return m_path; }

    // One line for a finding: what fired, what the frame carried, and the
    // clause. Phrased as what the RECEIVER would do, not as a verdict on the
    // packet — the tooling reports, signatories decide.
    static QString describe(const Finding &f);

private:
    QVector<Rule> m_rules;
    QString       m_path;
};

// The rule set the application uses, loaded beside the active schema.
RejectRules &kavachRejectRules();

#endif  // REJECTRULES_H
