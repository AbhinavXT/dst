#ifndef LOGQUERY_H
#define LOGQUERY_H

// =============================================================================
//  LogQuery
//  -----------------------------------------------------------------------------
//  A small boolean query language over LogEntry, shared by every place that
//  narrows a set of log rows: the per-tab filter bar, cross-tab search, and
//  (later) archive search.
//
//  WHY A LANGUAGE
//    The filter bar supports one text term against one column, ANDed with
//    the severity/direction chips. That covers "show me errors mentioning
//    RAD" and stops there. The questions operators actually arrive with are
//    compound — "errors from 33_1 in this nine-minute window that aren't
//    the routine No-Error line" — and every one of those currently means
//    filtering, eyeballing, re-filtering.
//
//    Writing the evaluator once and reusing it everywhere also means the
//    filter bar and the search window can never disagree about what a query
//    means, which they would within a release if each grew its own matching
//    code.
//
//  GRAMMAR
//      expr    := orExpr
//      orExpr  := andExpr ( ("OR" | "||") andExpr )*
//      andExpr := notExpr ( ("AND" | "&&")? notExpr )*     // AND is implicit
//      notExpr := ("NOT" | "!" | "-")? primary
//      primary := "(" expr ")" | term
//      term    := [field ":"] value
//      value   := bareword | "quoted phrase" | /regex/
//
//    Adjacency means AND, so `error RAD` is `error AND RAD` — the common
//    case stays as short as it is in the current filter box, and a plain
//    string with no operators behaves exactly like the old free-text
//    filter. That backwards compatibility is deliberate: the query mode has
//    to be strictly better than what it replaces or people won't use it.
//
//  FIELDS
//      msg:  text:            the decoded message text (default)
//      src:  source:          tab key, e.g. 33_1
//      name: friendly:        friendly name from friendly_names.csv
//      sev:  severity:        info | warn | error
//      dir:  direction:       in | out | none
//      hex:                   substring match on the raw datagram, hex.
//                             Separators are ignored, so hex:0a1b and
//                             hex:0a:1b are the same. A SPACE separates
//                             terms like anywhere else, so a space-delimited
//                             run must be quoted: hex:"0a 1b" — which is the
//                             form you get pasting out of the hex panel.
//      len:                   payload length, with = != > >= < <=
//      after:  before:        time bounds, see below
//      field:                 a SCHEMA-DECODED field, see below
//
//  DECODED FIELDS
//      field:SIG_OV               the field exists and is non-zero
//      field:SIG_OV=1             numeric equality
//      field:TRAIN_SPEED>60       also >= < <= !=
//      field:FRAME_NUM&7=3        bitwise mask, for multiplexed fields
//      field:Loco_Health~RADIO1   substring of the RENDERED value, which is
//                                 how enum and flag fields are matched
//
//    Names are case-insensitive, and a prefix matches: field:Loco_Health
//    finds "Loco_Health (faults 6-11)" without the caller needing to know
//    which group the frame carried.
//
//    Decoding is lazy and cached per entry (see fieldindex.h). A query with
//    no field: term costs nothing extra.
//
//    A term with no field searches message, source and friendly together —
//    the same thing the find bar's "Any column" does.
//
//  TIME
//      after:2026-08-08T14:02:33     full ISO
//      after:2026-08-08              date, at 00:00 local
//      after:14:02:33                time today (seconds optional)
//      after:-15m                    relative to now: s / m / h / d
//
//  MATCHING
//    Bare and quoted values are case-insensitive substring matches.
//    /slashes/ make it a regular expression. sev:, dir: and len: compare
//    rather than match. An unparseable query is an ERROR, reported through
//    errorString() — it never silently degrades to "matches nothing",
//    because a filter that quietly hides everything looks identical to a
//    quiet period on the bus.
//
//  THREADING
//    parse() builds an immutable tree; match() is const and re-entrant.
//    Regexes are compiled and optimized once at parse time, since match()
//    runs per row inside a proxy filter.
// =============================================================================

#include <QDateTime>
#include <QRegularExpression>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QVector>

#include "logentry.h"

class NameMap;

// The bytes a `hex:` term (or the Find window's Hex mode) is looking for.
//
// One parser for both, because they are the same question asked in two
// places: an operator who has copied "0A:1B:2C" out of the raw-bytes panel
// should not find that Find and the filter bar disagree about whether that
// is valid. `ok` false means nothing was searched for and `error` says why,
// with `errorOffset` pointing at the offending character where one can be
// named.
//
// STRICT ON PURPOSE. Separators (space, tab, ':', '-', ',', '.', '_') are
// skipped; anything else that is not a hex digit is an error rather than
// something quietly dropped. Dropping it is how "0g1b" becomes a search for
// 0x01, 0xB0 — a different pattern, with no matches and nothing on screen
// saying so.
struct HexPattern
{
    QByteArray bytes;
    QString    error;
    int        errorOffset = -1;
    bool       ok          = false;
};
HexPattern parseHexPattern(const QString &pattern);

class LogQuery
{
public:
    LogQuery() = default;

    // Parse `text`. Returns true on success; on failure the query matches
    // everything (so the caller shows an unfiltered view plus the error)
    // and errorString()/errorOffset() describe the problem.
    bool parse(const QString &text);

    // True when the query is empty — callers usually skip evaluation
    // entirely in that case rather than matching every row against a
    // trivial tree.
    bool isEmpty() const { return m_root.isNull(); }

    bool    isValid()     const { return m_error.isEmpty(); }
    QString errorString() const { return m_error; }
    int     errorOffset() const { return m_errorOffset; }

    // Evaluate against one entry. `names` supplies the friendly name for
    // name:/bare-term matching and may be null.
    bool match(const LogEntry &e, const NameMap *names = nullptr) const;

    // Human-readable dump of the parsed tree. Used by the search window to
    // show how a query was understood, which is the fastest way for someone
    // to spot that their operator precedence isn't what they assumed.
    QString explain() const;

    // The field names accepted, for completer/help UI.
    static QStringList knownFields();

    // AND an extra constraint onto a user-typed query, safely.
    //
    // The user's expression MUST be parenthesised: `a OR b` combined naively
    // with `sev:error` becomes `a OR b AND sev:error`, and since AND binds
    // tighter than OR that quietly means `a OR (b AND sev:error)` — half the
    // query escapes the constraint. Three UI surfaces offer this
    // "errors and warnings only" shorthand, so the rule lives here rather
    // than being re-derived (and eventually mis-derived) in each.
    static QString andConstraint(const QString &userQuery,
                                 const QString &constraint);

private:
    enum class Op   { And, Or, Not, Term };
    enum class Field {
        Any, Message, Source, Friendly, Severity, Direction,
        Hex, Length, After, Before, Decoded
    };
    enum class Cmp  { Eq, Ne, Gt, Ge, Lt, Le };

    struct Node;
    using NodePtr = QSharedPointer<Node>;

    struct Node {
        Op    op = Op::Term;
        QVector<NodePtr> kids;      // And/Or: 2+, Not: 1

        // Term payload
        Field              field = Field::Any;
        QString            literal;        // already lower-cased
        QRegularExpression regex;          // valid only when useRegex
        bool               useRegex = false;
        Cmp                cmp      = Cmp::Eq;
        qint64             number   = 0;   // len:, and epoch ms for time
        Severity           sev      = Severity::Info;
        Direction          dir      = Direction::None;
        QByteArray         hexBytes;

        // Decoded-field terms: the field name, an optional mask, and
        // whether the comparison is numeric or a substring of the rendered
        // value. `existsOnly` means the term was just `field:NAME`.
        QString            fieldName;
        // Packet the catalogue says carries this field. Empty means "any",
        // which is also what an uncatalogued field gets.
        QString            fieldPacket;
        qint64             mask       = 0;
        bool               hasMask    = false;
        bool               substring  = false;
        bool               existsOnly = false;
    };

    // ---- tokenizer ----
    struct Token {
        enum Kind { End, LParen, RParen, And, Or, Not, Term } kind = End;
        QString text;      // for Term: the raw "field:value" chunk
        int     pos  = 0;
    };
    QVector<Token> tokenize(const QString &s, QString *err, int *errPos) const;

    // ---- recursive descent ----
    NodePtr parseOr   (const QVector<Token> &t, int *i);
    NodePtr parseAnd  (const QVector<Token> &t, int *i);
    NodePtr parseNot  (const QVector<Token> &t, int *i);
    NodePtr parsePrim (const QVector<Token> &t, int *i);
    NodePtr makeTerm  (const QString &raw, int pos);

    bool    evalNode  (const Node &n, const LogEntry &e,
                       const NameMap *names) const;
    static QString describe(const Node &n, int indent);

    static bool    parseTimeSpec(const QString &s, qint64 *outMs);
    static QString fieldName(Field f);

    NodePtr m_root;
    QString m_error;
    int     m_errorOffset = -1;
    QString m_source;
};

#endif // LOGQUERY_H
