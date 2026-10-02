#include "logquery.h"

#include "capturedecoder.h"
#include "fieldcatalog.h"
#include "fieldindex.h"
#include "namemap.h"
#include "fieldplot.h"
#include "schema/schemadecoder.h"

#include <QDate>
#include <QTime>

#include <limits>

namespace {

// Whether the term in `buf` carries on past the whitespace at s[j].
// 0 = no, 1 = join with nothing between, 2 = join keeping one space.
//
// People type `after: 14:02`, `len > 64` and `after:2026-08-08 14:02`.
// Splitting those at the space produced either an error ("'after:' has no
// value") or — worse — a query that parsed and meant something else: the
// "14:02" of a date-and-time became a separate TEXT term, ANDed on, and the
// filter quietly asked a different question.
int joinAcrossSpace(const QString &buf, const QString &s, int j)
{
    if (buf.isEmpty() || buf.startsWith(QLatin1Char('"'))
        || buf.startsWith(QLatin1Char('/'))) {
        return 0;
    }
    const QString next = s.mid(j);
    if (next.startsWith(QLatin1Char('(')) || next.startsWith(QLatin1Char(')'))) {
        return 0;
    }
    // A field prefix still waiting for its value.
    if (buf.endsWith(QLatin1Char(':'))
        && LogQuery::knownFields().contains(buf.chopped(1).toLower())) {
        return 1;
    }
    // An operator on either side of the gap.
    static const QRegularExpression opEnd(QStringLiteral("(>|<|=|~|\\.\\.)$"));
    static const QRegularExpression opStart(
        QStringLiteral("^(>=|<=|!=|==|=|>|<|~|\\.\\.)"));
    if (opEnd.match(buf).hasMatch() || opStart.match(next).hasMatch()) return 1;
    // A date on a time field, its time-of-day after the space.
    static const QRegularExpression dateEnd(QStringLiteral(
        "(^|[:=<>~.])(\\d{4}[-/.]\\d{1,2}[-/.]\\d{1,2}|\\d{1,2}[-/.]\\d{1,2}[-/.]\\d{4})$"));
    static const QRegularExpression timeStart(QStringLiteral("^\\d{1,2}:\\d"));
    const QString low = buf.toLower();
    const bool timeField = low.startsWith(QLatin1String("after:"))
                        || low.startsWith(QLatin1String("before:"))
                        || low.startsWith(QLatin1String("time"));
    if (timeField && dateEnd.match(buf).hasMatch() && timeStart.match(next).hasMatch()) {
        return 2;
    }
    return 0;
}

// Whether `name` is a decoded field the catalogue or the active schema
// knows. Same prefix rule as FieldIndex::value(): "Loco_Health" names
// "Loco_Health (faults 6-11)".
bool isDecodedFieldName(const QString &name)
{
    if (FieldCatalog::instance().find(name)) return true;
    const QString want = name.toLower();
    for (const QString &f : kavachSchema().allFieldNames()) {
        const QString have = f.trimmed().toLower();
        if (have == want) return true;
        if (have.size() > want.size() && have.startsWith(want)
            && (have.at(want.size()) == QLatin1Char(' ')
                || have.at(want.size()) == QLatin1Char('('))) {
            return true;
        }
    }
    return false;
}

// Time of day in ms, in UTC or local time.
//
// Per row, inside a proxy filter, so the local-time conversion is cached:
// a zone's UTC offset only changes at a transition, and transitions fall on
// quarter-hour boundaries, so one lookup serves a whole 15-minute slot.
// thread_local because archive search matches on a worker thread.
qint64 timeOfDayMs(qint64 epochMs, bool utc)
{
    constexpr qint64 day = 86400000LL;
    qint64 offsetMs = 0;
    if (!utc) {
        constexpr qint64 slotMs = 15 * 60 * 1000LL;
        thread_local qint64 cachedSlot = std::numeric_limits<qint64>::min();
        thread_local qint64 cachedOffset = 0;
        const qint64 slot = epochMs >= 0 ? epochMs / slotMs
                                         : (epochMs - slotMs + 1) / slotMs;
        if (slot != cachedSlot) {
            cachedSlot   = slot;
            cachedOffset = QDateTime::fromMSecsSinceEpoch(epochMs).offsetFromUtc() * 1000LL;
        }
        offsetMs = cachedOffset;
    }
    return ((epochMs + offsetMs) % day + day) % day;
}

} // namespace

// ============================== tokenizer ==================================

QVector<LogQuery::Token> LogQuery::tokenize(const QString &s,
                                            QString *err, int *errPos) const
{
    QVector<Token> out;
    int i = 0;
    const int n = s.size();

    while (i < n) {
        while (i < n && s.at(i).isSpace()) ++i;
        if (i >= n) break;

        const int start = i;
        const QChar c = s.at(i);

        if (c == '(') { out.push_back({ Token::LParen, QString(), i }); ++i; continue; }
        if (c == ')') { out.push_back({ Token::RParen, QString(), i }); ++i; continue; }

        // "!=" is a comparison that lost its left side to a space, not NOT.
        if ((c == '!' && !(i + 1 < n && s.at(i + 1) == '=')) || c == '-') {
            // '-' is only negation when it starts a token AND something
            // follows it. A bare '-' or one inside a word (a timestamp, a
            // hyphenated name) is ordinary text.
            if (i + 1 < n && !s.at(i + 1).isSpace()) {
                out.push_back({ Token::Not, QString(), i });
                ++i;
                continue;
            }
        }
        if (c == '&' && i + 1 < n && s.at(i + 1) == '&') {
            out.push_back({ Token::And, QString(), i }); i += 2; continue;
        }
        if (c == '|' && i + 1 < n && s.at(i + 1) == '|') {
            out.push_back({ Token::Or, QString(), i }); i += 2; continue;
        }

        // A term runs to the next whitespace or paren, except that quotes
        // and slashes swallow whatever is between them — otherwise a phrase
        // with a space or a regex with a bracket would be split apart.
        QString buf;
        while (i < n) {
            const QChar ch = s.at(i);
            if (ch == '(' || ch == ')') break;
            if (ch.isSpace()) {
                int j = i;
                while (j < n && s.at(j).isSpace()) ++j;
                const int join = j < n ? joinAcrossSpace(buf, s, j) : 0;
                if (join == 0) break;
                if (join == 2) buf += QLatin1Char(' ');
                i = j;
                continue;
            }

            if (ch == '"' || ch == '/') {
                const QChar closer = ch;
                buf += ch;
                ++i;
                bool closed = false;
                while (i < n) {
                    if (s.at(i) == '\\' && i + 1 < n) {
                        // A backslash only ESCAPES the delimiter. Everything
                        // else keeps its backslash: regex bodies are full of
                        // \s, \d and \b, and swallowing those silently
                        // rewrote the pattern into something that still
                        // compiled but matched the wrong thing.
                        if (s.at(i + 1) == closer) {
                            buf += closer;
                        } else {
                            buf += s.at(i);
                            buf += s.at(i + 1);
                        }
                        i += 2;
                        continue;
                    }
                    if (s.at(i) == closer) { buf += closer; ++i; closed = true; break; }
                    buf += s.at(i);
                    ++i;
                }
                if (!closed) {
                    if (err) {
                        *err = QStringLiteral("unterminated %1")
                                   .arg(closer == '"' ? "quote" : "regex");
                    }
                    if (errPos) *errPos = start;
                    return {};
                }
                continue;
            }
            buf += ch;
            ++i;
        }

        // Bare keywords are operators only when unquoted and standing alone.
        const QString up = buf.toUpper();
        if      (up == "AND") out.push_back({ Token::And, QString(), start });
        else if (up == "OR")  out.push_back({ Token::Or,  QString(), start });
        else if (up == "NOT") out.push_back({ Token::Not, QString(), start });
        else                  out.push_back({ Token::Term, buf,      start });
    }

    out.push_back({ Token::End, QString(), n });
    return out;
}

// ============================== parser =====================================

bool LogQuery::parse(const QString &text)
{
    m_root.reset();
    m_error.clear();
    m_errorOffset = -1;
    m_source = text;
    m_usesDataEnd = false;

    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return true;         // empty query: matches all

    QString err;
    int errPos = -1;
    const QVector<Token> toks = tokenize(trimmed, &err, &errPos);
    if (!err.isEmpty()) {
        m_error = err;
        m_errorOffset = errPos;
        return false;
    }

    int i = 0;
    NodePtr root = parseOr(toks, &i);
    if (!m_error.isEmpty()) return false;

    if (toks.at(i).kind != Token::End) {
        m_error = QStringLiteral("unexpected ')'");
        m_errorOffset = toks.at(i).pos;
        return false;
    }

    m_root = root;
    return true;
}

LogQuery::NodePtr LogQuery::parseOr(const QVector<Token> &t, int *i)
{
    NodePtr left = parseAnd(t, i);
    if (!m_error.isEmpty()) return {};

    while (t.at(*i).kind == Token::Or) {
        ++(*i);
        NodePtr right = parseAnd(t, i);
        if (!m_error.isEmpty()) return {};
        auto node = NodePtr::create();
        node->op = Op::Or;
        node->kids << left << right;
        left = node;
    }
    return left;
}

LogQuery::NodePtr LogQuery::parseAnd(const QVector<Token> &t, int *i)
{
    NodePtr left = parseNot(t, i);
    if (!m_error.isEmpty()) return {};

    while (true) {
        const Token::Kind k = t.at(*i).kind;
        // Explicit AND, or adjacency: a term/paren/NOT following another
        // operand is an implicit AND.
        if (k == Token::And) {
            ++(*i);
        } else if (k != Token::Term && k != Token::LParen && k != Token::Not) {
            break;
        }
        NodePtr right = parseNot(t, i);
        if (!m_error.isEmpty()) return {};
        auto node = NodePtr::create();
        node->op = Op::And;
        node->kids << left << right;
        left = node;
    }
    return left;
}

LogQuery::NodePtr LogQuery::parseNot(const QVector<Token> &t, int *i)
{
    if (t.at(*i).kind == Token::Not) {
        ++(*i);
        NodePtr child = parseNot(t, i);       // NOT NOT x is legal
        if (!m_error.isEmpty()) return {};
        auto node = NodePtr::create();
        node->op = Op::Not;
        node->kids << child;
        return node;
    }
    return parsePrim(t, i);
}

LogQuery::NodePtr LogQuery::parsePrim(const QVector<Token> &t, int *i)
{
    const Token &tok = t.at(*i);

    if (tok.kind == Token::LParen) {
        ++(*i);
        NodePtr inner = parseOr(t, i);
        if (!m_error.isEmpty()) return {};
        if (t.at(*i).kind != Token::RParen) {
            m_error = QStringLiteral("missing ')'");
            m_errorOffset = t.at(*i).pos;
            return {};
        }
        ++(*i);
        return inner;
    }

    if (tok.kind == Token::Term) {
        ++(*i);
        return makeTerm(tok.text, tok.pos);
    }

    m_error = (tok.kind == Token::End)
                  ? QStringLiteral("query ends after an operator")
                  : QStringLiteral("expected a search term");
    m_errorOffset = tok.pos;
    return {};
}

HexPattern parseHexPattern(const QString &pattern)
{
    HexPattern out;

    QString digits;
    digits.reserve(pattern.size());
    for (int i = 0; i < pattern.size(); ++i) {
        const QChar ch = pattern.at(i);
        // The separators an operator actually produces: spaces from the hex
        // panel, colons from Wireshark, dashes and commas from a spreadsheet,
        // underscores and dots from hand-typing.
        if (ch.isSpace() || ch == QLatin1Char(':') || ch == QLatin1Char('-')
            || ch == QLatin1Char(',') || ch == QLatin1Char('.')
            || ch == QLatin1Char('_')) {
            continue;
        }
        // Not isxdigit(): it takes an int that must be representable as
        // unsigned char, and a non-Latin1 QChar folds to something that is
        // not. The test is spelled out instead of narrowed first.
        const bool isHexDigit = (ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))
                             || (ch >= QLatin1Char('a') && ch <= QLatin1Char('f'))
                             || (ch >= QLatin1Char('A') && ch <= QLatin1Char('F'));
        if (!isHexDigit) {
            out.error = QStringLiteral("'%1' is not a hex digit").arg(ch);
            out.errorOffset = i;
            return out;
        }
        digits += ch;
    }

    if (digits.isEmpty()) {
        out.error = QStringLiteral("no hex digits to search for");
        return out;
    }
    if (digits.size() % 2 != 0) {
        // Naming the count matters: the usual cause is a byte typed as one
        // digit ("0a 1 2c"), and "odd number of digits" sends the operator
        // looking for the one they dropped rather than retyping the lot.
        out.error = QStringLiteral("bytes come in pairs of digits — %1 digits is one short")
                        .arg(digits.size());
        out.errorOffset = pattern.size() - 1;
        return out;
    }

    out.bytes = QByteArray::fromHex(digits.toLatin1());
    out.ok    = true;
    return out;
}

LogQuery::NodePtr LogQuery::fail(const QString &why, int pos)
{
    // First error wins: a nested term (one item of a list, one end of a
    // range) reports its own problem, and the caller unwinding must not
    // overwrite it with a vaguer one.
    if (m_error.isEmpty()) {
        m_error       = why;
        m_errorOffset = pos;
    }
    return {};
}

LogQuery::NodePtr LogQuery::combine(Op op, const QVector<NodePtr> &kids)
{
    if (kids.size() == 1 && op != Op::Not) return kids.first();
    auto node = NodePtr::create();
    node->op   = op;
    node->kids = kids;
    return node;
}

// NAME op VALUE without a colon: len>64, time>=9:05, sev!=info,
// TRAIN_SPEED>60. Rewritten into the colon form and parsed by makeTerm, so
// the two spellings cannot drift apart.
LogQuery::NodePtr LogQuery::makeComparison(const QString &name,
                                           const QString &mask,
                                           const QString &op,
                                           const QString &rhs,
                                           const QString &raw, int pos)
{
    const QString key = name.toLower();
    const bool eq = (op == QLatin1String("=") || op == QLatin1String("=="));

    if (knownFields().contains(key) && key != QLatin1String("field")) {
        if (!mask.isEmpty()) {
            return fail(QStringLiteral("a mask (&) applies only to decoded "
                                       "fields, not %1").arg(key), pos);
        }
        if (key == QLatin1String("len")) {
            return makeTerm(QStringLiteral("len:") + (eq ? QStringLiteral("=") : op) + rhs, pos);
        }
        if (key == QLatin1String("time")) {
            return makeTerm(QStringLiteral("time:") + (eq ? QString() : op) + rhs, pos);
        }
        if (key == QLatin1String("after") || key == QLatin1String("before")
            || key == QLatin1String("last")) {
            if (!eq) {
                return fail(QStringLiteral("%1 takes a value after ':' "
                                           "(e.g. %1:14:02); use time%2 to "
                                           "compare").arg(key, op), pos);
            }
            return makeTerm(key + QLatin1Char(':') + rhs, pos);
        }
        // The text-like fields: = and ~ both mean "matches", != negates.
        if (op == QLatin1String("!=")) {
            NodePtr k = makeTerm(key + QLatin1Char(':') + rhs, pos);
            return k ? combine(Op::Not, { k }) : k;
        }
        if (eq || op == QLatin1String("~")) return makeTerm(key + QLatin1Char(':') + rhs, pos);
        return fail(QStringLiteral("%1 cannot be compared with '%2'; "
                                   "use =, != or ~").arg(key, op), pos);
    }

    if (isDecodedFieldName(name)) return makeTerm(QStringLiteral("field:") + raw, pos);

    // Refused rather than searched for as text: "TRAN_SPEED>60" run as a
    // text search would match nothing, and look like a quiet bus.
    return fail(QStringLiteral("unknown field '%1' — write field:%1 to force "
                               "a decoded field, or quote the term to search "
                               "text: \"%2\"").arg(name, raw), pos);
}

LogQuery::NodePtr LogQuery::makeTerm(const QString &raw, int pos)
{
    QString field, value = raw;
    const bool literalTerm = raw.startsWith('"') || raw.startsWith('/');

    // Split on the first colon — but only when it looks like a field
    // prefix. A bare "14:02" or an unprefixed "a:b" inside quotes must stay
    // a literal, so the colon only counts if what precedes it is a known
    // field name.
    const int colon = raw.indexOf(':');
    if (colon > 0 && !literalTerm) {
        const QString maybe = raw.left(colon).toLower();
        if (knownFields().contains(maybe)) {
            field = maybe;
            value = raw.mid(colon + 1);
        }
    }

    if (field.isEmpty() && !literalTerm) {
        static const QRegularExpression cmpRe(QStringLiteral(
            "^([A-Za-z_][A-Za-z0-9_]*)(&[^=!<>~]*)?(>=|<=|!=|==|=|>|<|~)(.*)$"));
        const QRegularExpressionMatch mm = cmpRe.match(raw);
        if (mm.hasMatch()) {
            return makeComparison(mm.captured(1), mm.captured(2),
                                  mm.captured(3), mm.captured(4), raw, pos);
        }
    }

    if (value.isEmpty()) {
        return fail(QStringLiteral("'%1:' has no value").arg(field), pos);
    }

    const bool literalValue = value.startsWith('"') || value.startsWith('/');

    if (field == QLatin1String("after") || field == QLatin1String("before")
        || field == QLatin1String("time") || field == QLatin1String("last")) {
        if (value.startsWith('/')) {
            return fail(QStringLiteral("%1: takes a time, not a /regex/").arg(field), pos);
        }
        if (value.size() >= 2 && value.startsWith('"') && value.endsWith('"')) {
            value = value.mid(1, value.size() - 2);
        }
        return makeTimeTerm(field, value, pos);
    }

    if (!literalValue) {
        // Lists: any of. Not on msg: or bare text, where a comma is far
        // more likely to be part of what is being searched for.
        static const QStringList listFields = {
            "src", "source", "name", "friendly", "sev", "severity",
            "dir", "direction", "len" };
        if (listFields.contains(field) && value.contains(QLatin1Char(','))) {
            QVector<NodePtr> kids;
            for (const QString &item : value.split(QLatin1Char(','))) {
                if (item.trimmed().isEmpty()) {
                    return fail(QStringLiteral("%1: list has an empty item").arg(field), pos);
                }
                NodePtr k = makeTerm(field + QLatin1Char(':') + item.trimmed(), pos);
                if (!k) return {};
                kids << k;
            }
            return combine(Op::Or, kids);
        }
        if (field == QLatin1String("len") && value.contains(QLatin1String(".."))) {
            const int dd = value.indexOf(QLatin1String(".."));
            const QString lo = value.left(dd).trimmed(), hi = value.mid(dd + 2).trimmed();
            if (lo.isEmpty() && hi.isEmpty()) {
                return fail(QStringLiteral("len: range needs at least one end"), pos);
            }
            QVector<NodePtr> kids;
            if (!lo.isEmpty()) kids << makeTerm(QStringLiteral("len:>=") + lo, pos);
            if (!hi.isEmpty()) kids << makeTerm(QStringLiteral("len:<=") + hi, pos);
            for (const NodePtr &k : kids) if (!k) return {};
            return combine(Op::And, kids);
        }
    }

    auto node = NodePtr::create();
    node->op = Op::Term;

    // Strip quoting / regex delimiters.
    if (value.size() >= 2 && value.startsWith('/') && value.endsWith('/')) {
        node->useRegex = true;
        const QString pat = value.mid(1, value.size() - 2);
        node->regex = QRegularExpression(
            pat, QRegularExpression::CaseInsensitiveOption);
        if (!node->regex.isValid()) {
            m_error = QStringLiteral("bad regular expression: %1")
                          .arg(node->regex.errorString());
            m_errorOffset = pos;
            return {};
        }
        node->regex.optimize();
    } else if (value.size() >= 2 && value.startsWith('"') && value.endsWith('"')) {
        value = value.mid(1, value.size() - 2);
    }

    node->literal = value.toLower();

    if      (field.isEmpty())                          node->field = Field::Any;
    else if (field == "msg"  || field == "text")       node->field = Field::Message;
    else if (field == "src"  || field == "source")     node->field = Field::Source;
    else if (field == "name" || field == "friendly")   node->field = Field::Friendly;
    else if (field == "hex")                           node->field = Field::Hex;
    else if (field == "sev"  || field == "severity")   node->field = Field::Severity;
    else if (field == "dir"  || field == "direction")  node->field = Field::Direction;
    else if (field == "len")                           node->field = Field::Length;
    else if (field == "field")                         node->field = Field::Decoded;

    switch (node->field) {
    case Field::Severity: {
        const QString v = node->literal;
        if      (v.startsWith("err"))  node->sev = Severity::Error;
        else if (v.startsWith("warn")) node->sev = Severity::Warn;
        else if (v.startsWith("info")) node->sev = Severity::Info;
        else {
            m_error = QStringLiteral("sev: expects info, warn or error");
            m_errorOffset = pos;
            return {};
        }
        break;
    }
    case Field::Direction: {
        const QString v = node->literal;
        if      (v == "in")   node->dir = Direction::In;
        else if (v == "out")  node->dir = Direction::Out;
        else if (v == "none") node->dir = Direction::None;
        else {
            m_error = QStringLiteral("dir: expects in, out or none");
            m_errorOffset = pos;
            return {};
        }
        break;
    }
    case Field::Length: {
        QString v = node->literal;
        node->cmp = Cmp::Eq;
        if      (v.startsWith(">=")) { node->cmp = Cmp::Ge; v = v.mid(2); }
        else if (v.startsWith("<=")) { node->cmp = Cmp::Le; v = v.mid(2); }
        else if (v.startsWith("!=")) { node->cmp = Cmp::Ne; v = v.mid(2); }
        else if (v.startsWith(">"))  { node->cmp = Cmp::Gt; v = v.mid(1); }
        else if (v.startsWith("<"))  { node->cmp = Cmp::Lt; v = v.mid(1); }
        else if (v.startsWith("="))  { node->cmp = Cmp::Eq; v = v.mid(1); }
        bool ok = false;
        node->number = v.toLongLong(&ok);
        if (!ok) {
            m_error = QStringLiteral("len: expects a number, optionally "
                                     "prefixed with > >= < <= = !=");
            m_errorOffset = pos;
            return {};
        }
        break;
    }
    case Field::Decoded: {
        // Grammar: NAME [ & MASK ] [ op VALUE ], op being = == != > >= < <=
        // or ~ for substring. Parsed here rather than at match time so a
        // malformed term is an error the user sees, not a silent no-match.
        QString spec = node->literal;      // already lower-cased and unquoted

        // Lists and ranges: NAME=a,b is any of, NAME=a..b is between
        // (inclusive), and != negates either. Expanded into ordinary terms
        // so each value still gets the symbol resolution below.
        static const QRegularExpression setRe(
            QStringLiteral("^([^=!<>~]+?)\\s*(==|!=|=)\\s*(.*)$"));
        const QRegularExpressionMatch sm = setRe.match(spec);
        if (sm.hasMatch() && (sm.captured(3).contains(QLatin1Char(','))
                              || sm.captured(3).contains(QLatin1String("..")))) {
            const QString lhs = QStringLiteral("field:") + sm.captured(1).trimmed();
            const QString rhs = sm.captured(3);
            const bool    neg = sm.captured(2) == QLatin1String("!=");
            QVector<NodePtr> kids;
            Op op = Op::Or;
            if (rhs.contains(QLatin1String(".."))) {
                op = Op::And;
                const int dd = rhs.indexOf(QLatin1String(".."));
                const QString lo = rhs.left(dd).trimmed(), hi = rhs.mid(dd + 2).trimmed();
                if (lo.isEmpty() && hi.isEmpty()) {
                    return fail(QStringLiteral("field: range needs at least one end"), pos);
                }
                if (!lo.isEmpty()) kids << makeTerm(lhs + QStringLiteral(">=") + lo, pos);
                if (!hi.isEmpty()) kids << makeTerm(lhs + QStringLiteral("<=") + hi, pos);
            } else {
                for (const QString &item : rhs.split(QLatin1Char(','))) {
                    if (item.trimmed().isEmpty()) {
                        return fail(QStringLiteral("field: list has an empty item"), pos);
                    }
                    kids << makeTerm(lhs + QLatin1Char('=') + item.trimmed(), pos);
                }
            }
            for (const NodePtr &k : kids) if (!k) return {};
            const NodePtr all = combine(op, kids);
            return neg ? combine(Op::Not, { all }) : all;
        }

        const int amp = spec.indexOf(QLatin1Char('&'));
        if (amp >= 0) {
            // The mask ends where the comparison starts.
            int opAt = -1;
            for (int i = amp + 1; i < spec.size(); ++i) {
                const QChar ch = spec.at(i);
                if (ch == '=' || ch == '!' || ch == '>' || ch == '<' || ch == '~') {
                    opAt = i; break;
                }
            }
            const QString maskStr =
                (opAt < 0 ? spec.mid(amp + 1) : spec.mid(amp + 1, opAt - amp - 1)).trimmed();
            bool mok = false;
            node->mask = maskStr.toLongLong(&mok, 0);
            if (!mok) {
                m_error = QStringLiteral("field: mask '%1' is not a number")
                              .arg(maskStr);
                m_errorOffset = pos;
                return {};
            }
            node->hasMask = true;
            spec = spec.left(amp) + (opAt < 0 ? QString() : spec.mid(opAt));
        }

        // Comparison operator, longest first so ">=" is not read as ">".
        struct { const char *tok; Cmp cmp; bool sub; } opsTbl[] = {
            { ">=", Cmp::Ge, false }, { "<=", Cmp::Le, false },
            { "!=", Cmp::Ne, false }, { "==", Cmp::Eq, false },
            { "~",  Cmp::Eq, true  }, { "=",  Cmp::Eq, false },
            { ">",  Cmp::Gt, false }, { "<",  Cmp::Lt, false },
        };
        int opAt = -1, opLen = 0;
        for (const auto &o : opsTbl) {
            const int k = spec.indexOf(QLatin1String(o.tok));
            if (k < 0) continue;
            if (opAt < 0 || k < opAt) {
                opAt = k; opLen = int(qstrlen(o.tok));
                node->cmp = o.cmp; node->substring = o.sub;
            }
        }

        if (opAt < 0) {
            node->fieldName  = spec.trimmed();
            node->existsOnly = true;
        } else {
            node->fieldName = spec.left(opAt).trimmed();
            const QString rhs = spec.mid(opAt + opLen).trimmed();

            // Symbolic value, e.g. loco_mode=staff_responsible. Resolved
            // here so a mistyped symbol is a PARSE error the author sees,
            // rather than a query that runs and quietly matches nothing.
            if (!node->substring) {
                bool symOk = false;
                const qint64 sym =
                    FieldCatalog::instance().resolveValue(node->fieldName, rhs, &symOk);
                if (symOk) {
                    node->number = sym;
                    const FieldCatalog::Entry *ce =
                        FieldCatalog::instance().find(node->fieldName);
                    node->fieldPacket = ce ? ce->packet : QString();
                    node->fieldName   = ce ? ce->field : node->fieldName;
                    break;
                }
                // Known field, unknown symbol, non-numeric text: almost
                // certainly a typo in a symbol name, and worth refusing.
                const FieldCatalog::Entry *ce =
                    FieldCatalog::instance().find(node->fieldName);
                bool numeric = false;
                rhs.toDouble(&numeric);
                if (ce && !ce->values.isEmpty() && !numeric) {
                    QStringList known = ce->values.keys();
                    known.sort();
                    m_error = QStringLiteral("'%1' is not a value of %2 "
                                             "(known: %3)")
                                  .arg(rhs, node->fieldName,
                                       known.join(QStringLiteral(", ")));
                    m_errorOffset = pos;
                    return {};
                }
            }
            if (node->substring) {
                node->literal = rhs;
            } else {
                bool nok = false;
                node->number = qint64(rhs.toDouble(&nok));
                if (!nok) node->number = rhs.toLongLong(&nok, 0);
                if (!nok) {
                    // A non-numeric right-hand side is almost always meant
                    // as a value match on an enum, so treat it as one
                    // rather than rejecting a reasonable query.
                    node->substring = true;
                    node->literal   = rhs;
                }
            }
        }
        if (node->fieldName.isEmpty()) {
            m_error = QStringLiteral("field: needs a field name");
            m_errorOffset = pos;
            return {};
        }
        // Resolve the alias for the remaining forms (bare, numeric,
        // substring). Unknown names pass through unchanged.
        if (node->fieldPacket.isEmpty()) {
            const FieldCatalog::Entry *ce =
                FieldCatalog::instance().find(node->fieldName);
            if (ce) {
                node->fieldPacket = ce->packet;
                node->fieldName   = ce->field;
            }
        }
        break;
    }

    case Field::Hex: {
        // Accept "0a1b", "0a 1b", "0A:1B" — operators copy bytes out of the
        // hex panel with whatever separators it used. The rules live in
        // parseHexPattern so the Find window's Hex mode cannot drift from
        // what `hex:` accepts here.
        const HexPattern hp = parseHexPattern(node->literal);
        if (!hp.ok) {
            m_error = QStringLiteral("hex: %1").arg(hp.error);
            // Offsets are relative to the literal; move them onto the term.
            m_errorOffset = hp.errorOffset >= 0 ? pos + hp.errorOffset : pos;
            return {};
        }
        node->hexBytes = hp.bytes;
        break;
    }
    default:
        break;
    }

    return node;
}

// ============================== evaluation =================================

bool LogQuery::match(const LogEntry &e, const NameMap *names) const
{
    if (m_root.isNull()) return true;
    return evalNode(*m_root, e, names);
}

bool LogQuery::evalNode(const Node &n, const LogEntry &e,
                        const NameMap *names) const
{
    switch (n.op) {
    case Op::And:
        for (const NodePtr &k : n.kids) {
            if (!k || !evalNode(*k, e, names)) return false;   // short-circuit
        }
        return true;
    case Op::Or:
        for (const NodePtr &k : n.kids) {
            if (k && evalNode(*k, e, names)) return true;
        }
        return false;
    case Op::Not:
        return n.kids.isEmpty() || !n.kids.at(0)
                   || !evalNode(*n.kids.at(0), e, names);
    case Op::Term:
        break;
    }

    auto textMatch = [&n](const QString &hay) {
        if (n.useRegex) return n.regex.match(hay).hasMatch();
        return hay.contains(n.literal, Qt::CaseInsensitive);
    };

    switch (n.field) {
    case Field::Message:  return textMatch(e.text);
    case Field::Source:   return textMatch(e.tabKey());
    case Field::Friendly:
        return textMatch(names ? names->lookupByKey(e.tabKey()) : e.tabKey());

    case Field::Any: {
        if (textMatch(e.text))      return true;
        if (textMatch(e.tabKey()))  return true;
        if (names && textMatch(names->lookupByKey(e.tabKey()))) return true;
        return false;
    }

    case Field::Severity:  return e.severity  == n.sev;
    case Field::Direction: return e.direction == n.dir;

    case Field::Hex:
        return e.rawBytes.contains(n.hexBytes);

    case Field::Length: {
        // Payload length, excluding the fixed header — that's the number
        // shown everywhere else in the UI.
        const qint64 len = e.header.message_len;
        switch (n.cmp) {
        case Cmp::Eq: return len == n.number;
        case Cmp::Ne: return len != n.number;
        case Cmp::Gt: return len >  n.number;
        case Cmp::Ge: return len >= n.number;
        case Cmp::Lt: return len <  n.number;
        case Cmp::Le: return len <= n.number;
        }
        return false;
    }

    case Field::Decoded: {
        // Scope to the packet the catalogue says carries this field. Two
        // packets can decode a field of the same name, and a case written
        // for one must not be satisfied by the other.
        if (!n.fieldPacket.isEmpty()
            && FieldIndex::captype(e) != n.fieldPacket) {
            return false;
        }
        const QString raw = FieldIndex::value(e, n.fieldName);
        if (raw.isNull()) return false;          // field absent on this entry

        if (n.existsOnly) {
            // "field:NAME" means present AND meaningful. A field that
            // decodes to 0 or "(none)" is the healthy/absent state
            // everywhere in this protocol, so reporting it as a hit would
            // make the bare form useless — every frame has SIG_OV.
            bool ok = false;
            const double v = parseFieldNumber(raw, &ok);
            if (ok) return v != 0.0;
            return raw != QLatin1String("(none)") && !raw.trimmed().isEmpty();
        }

        if (n.substring) return raw.contains(n.literal, Qt::CaseInsensitive);

        bool ok = false;
        double v = parseFieldNumber(raw, &ok);
        if (!ok) return false;                   // non-numeric vs a number
        qint64 iv = qint64(v);
        if (n.hasMask) iv &= n.mask;

        switch (n.cmp) {
        case Cmp::Eq: return iv == n.number;
        case Cmp::Ne: return iv != n.number;
        case Cmp::Gt: return iv >  n.number;
        case Cmp::Ge: return iv >= n.number;
        case Cmp::Lt: return iv <  n.number;
        case Cmp::Le: return iv <= n.number;
        }
        return false;
    }

    case Field::Time: {
        const qint64 v = n.clock ? timeOfDayMs(e.epochMs, m_utc) : e.epochMs;
        if (n.clock && n.hasLo && n.hasHi && n.lo > n.hi) {
            return v >= n.lo || v <= n.hi;          // wraps midnight
        }
        return (!n.hasLo || v >= n.lo) && (!n.hasHi || v <= n.hi);
    }
    }
    return false;
}

// ============================== helpers ====================================

LogQuery::NodePtr LogQuery::makeTimeTerm(const QString &field,
                                         const QString &value, int pos)
{
    auto node = NodePtr::create();
    node->op    = Op::Term;
    node->field = Field::Time;

    QString v = value.trimmed();
    QString why;

    if (field == QLatin1String("last")) {
        m_usesDataEnd = true;
        qint64 d = 0;
        if (!parseDuration(v, &d)) {
            return fail(QStringLiteral("last: expects a duration such as "
                                       "30s, 15m, 2h or 1h30m"), pos);
        }
        const qint64 end = m_dataEnd > 0 ? m_dataEnd
                                         : QDateTime::currentMSecsSinceEpoch();
        node->hasLo = true;
        node->lo    = end - d;
        return node;
    }

    auto point = [&](const QString &t, TimePoint *p) -> bool {
        why.clear();
        if (parseTimePoint(t, p, &why)) return true;
        fail(why.isEmpty()
                 ? QStringLiteral("could not read '%1' as a time (try 14:02, "
                                  "9:05:30, 2026-08-08 14:02, 08-08-2026 or "
                                  "-15m)").arg(t)
                 : QStringLiteral("could not read '%1' as a time: %2").arg(t, why),
             pos);
        return false;
    };

    if (field == QLatin1String("after") || field == QLatin1String("before")) {
        TimePoint p;
        if (!point(v, &p)) return {};
        node->clock = p.clock;
        if (field == QLatin1String("after")) { node->hasLo = true; node->lo = p.start; }
        else                                 { node->hasHi = true; node->hi = p.end;   }
        return node;
    }

    // time: [op] point | a..b | clock-clock
    QString op;
    for (const char *o : { ">=", "<=", "!=", "==", ">", "<", "=" }) {
        if (v.startsWith(QLatin1String(o))) {
            op = QLatin1String(o);
            v  = v.mid(op.size()).trimmed();
            break;
        }
    }
    if (v.isEmpty()) return fail(QStringLiteral("'time:' has no value"), pos);

    QString loText, hiText;
    bool isRange = false;
    const int dd = v.indexOf(QLatin1String(".."));
    if (dd >= 0) {
        isRange = true;
        loText  = v.left(dd).trimmed();
        hiText  = v.mid(dd + 2).trimmed();
    } else {
        // 14:02-14:09. Only between two clock times: with dates in play the
        // dash is part of the date.
        static const QRegularExpression clockDash(QStringLiteral(
            "^(\\d{1,2}:[\\d:.,]+)\\s*-\\s*(\\d{1,2}:[\\d:.,]+)$"));
        const QRegularExpressionMatch cm = clockDash.match(v);
        if (cm.hasMatch()) {
            isRange = true;
            loText  = cm.captured(1);
            hiText  = cm.captured(2);
        }
    }

    if (isRange) {
        if (!op.isEmpty()) {
            return fail(QStringLiteral("a time range takes no comparison operator"), pos);
        }
        if (loText.isEmpty() && hiText.isEmpty()) {
            return fail(QStringLiteral("time: range needs at least one end"), pos);
        }
        TimePoint a, b;
        if (!loText.isEmpty() && !point(loText, &a)) return {};
        if (!hiText.isEmpty() && !point(hiText, &b)) return {};

        if (!loText.isEmpty() && !hiText.isEmpty() && a.clock != b.clock) {
            if (a.clock) {
                return fail(QStringLiteral("time: give the start of the range "
                                           "a date too, or neither end"), pos);
            }
            // "2026-08-08 23:50..00:10": the end takes the start's date,
            // rolling over midnight when it has to.
            const QDateTime startDt = m_utc ? QDateTime::fromMSecsSinceEpoch(a.start, Qt::UTC)
                                            : QDateTime::fromMSecsSinceEpoch(a.start);
            QDateTime endDt(startDt.date(), QTime::fromMSecsSinceStartOfDay(int(b.start)),
                            m_utc ? Qt::UTC : Qt::LocalTime);
            if (endDt.toMSecsSinceEpoch() < a.start) endDt = endDt.addDays(1);
            const qint64 span = b.end - b.start;
            b.clock = false;
            b.start = endDt.toMSecsSinceEpoch();
            b.end   = b.start + span;
        }

        node->clock = !loText.isEmpty() ? a.clock : b.clock;
        if (!loText.isEmpty()) { node->hasLo = true; node->lo = a.start; }
        if (!hiText.isEmpty()) { node->hasHi = true; node->hi = b.end;   }
        // A clock window that "ends before it starts" wraps midnight; a
        // dated one is a mistake.
        if (!node->clock && node->hasLo && node->hasHi && node->lo > node->hi) {
            return fail(QStringLiteral("time: range ends before it starts"), pos);
        }
        return node;
    }

    TimePoint p;
    if (!point(v, &p)) return {};
    node->clock = p.clock;
    if      (op == QLatin1String(">=")) { node->hasLo = true; node->lo = p.start;     }
    else if (op == QLatin1String(">"))  { node->hasLo = true; node->lo = p.end + 1;   }
    else if (op == QLatin1String("<=")) { node->hasHi = true; node->hi = p.end;       }
    else if (op == QLatin1String("<"))  { node->hasHi = true; node->hi = p.start - 1; }
    else {
        // time:14:02 is the whole of what was typed: that minute.
        node->hasLo = node->hasHi = true;
        node->lo = p.start;
        node->hi = p.end;
    }
    return op == QLatin1String("!=") ? combine(Op::Not, { node }) : node;
}

bool LogQuery::parseTimePoint(const QString &text, TimePoint *out,
                              QString *why) const
{
    const QString v = text.trimmed().toLower();
    if (v.isEmpty()) return false;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (v == QLatin1String("now")) {
        out->clock = false;
        out->start = out->end = now;
        return true;
    }
    // Relative: -15m, -1h30m. Anchored to "now", which is what someone
    // means by "the last fifteen minutes" of a live session; last: is the
    // form anchored to the data.
    if (v.startsWith(QLatin1Char('-'))) {
        qint64 d = 0;
        if (!parseDuration(v.mid(1), &d)) return false;
        out->clock = false;
        out->start = out->end = now - d;
        return true;
    }

    // "after:15m" — a duration without its sign. Said so, rather than the
    // generic "could not read", because the fix is one character.
    {
        qint64 d = 0;
        if (parseDuration(v, &d)) {
            *why = QStringLiteral("a duration needs its sign: -%1 counts back "
                                  "from now; last:%1 counts back from the "
                                  "newest row").arg(v);
            return false;
        }
    }

    // Date: year-first (2026-08-08) or day-first (08-08-2026, the way it
    // is written here), with - / or . between. Never month-first: 08-09
    // would be ambiguous, and a date read the wrong way round filters a
    // different day without any sign of it.
    static const QRegularExpression dateRe(QStringLiteral(
        "^(?:(\\d{4})[-/.](\\d{1,2})[-/.](\\d{1,2})|(\\d{1,2})[-/.](\\d{1,2})[-/.](\\d{4}))"));
    QDate date;
    QString rest = v;
    const QRegularExpressionMatch dm = dateRe.match(v);
    if (dm.hasMatch()) {
        const bool yearFirst = !dm.captured(1).isEmpty();
        const int y  = (yearFirst ? dm.captured(1) : dm.captured(6)).toInt();
        const int mo = (yearFirst ? dm.captured(2) : dm.captured(5)).toInt();
        const int d  = (yearFirst ? dm.captured(3) : dm.captured(4)).toInt();
        date = QDate(y, mo, d);
        if (!date.isValid()) {
            *why = QStringLiteral("%1 is not a date (year-first 2026-08-25 or "
                                  "day-first 25-08-2026)").arg(dm.captured(0));
            return false;
        }
        rest = v.mid(dm.capturedLength(0));
        if (!rest.isEmpty()) {
            const QChar sep = rest.at(0);
            if (sep != QLatin1Char('t') && sep != QLatin1Char(' ')
                && sep != QLatin1Char('_') && sep != QLatin1Char(',')) {
                return false;
            }
            rest = rest.mid(1).trimmed();
            if (rest.isEmpty()) return false;
        }
    }

    // Time of day, hours and minutes at least, an optional zone after it.
    enum class Prec { Day, Minute, Second, Milli } prec = Prec::Day;
    int h = 0, mi = 0, se = 0, ms = 0, tzSecs = 0;
    bool hasTz = false;
    if (!rest.isEmpty()) {
        static const QRegularExpression timeRe(QStringLiteral(
            "^(\\d{1,2}):(\\d{1,2})(?::(\\d{1,2})(?:[.,](\\d{1,9}))?)?\\s*(z|[+-]\\d{2}:?\\d{2})?$"));
        const QRegularExpressionMatch tm = timeRe.match(rest);
        if (!tm.hasMatch()) return false;
        h  = tm.captured(1).toInt();
        mi = tm.captured(2).toInt();
        prec = Prec::Minute;
        if (!tm.captured(3).isEmpty()) { se = tm.captured(3).toInt(); prec = Prec::Second; }
        if (!tm.captured(4).isEmpty()) {
            ms   = tm.captured(4).leftJustified(3, QLatin1Char('0')).left(3).toInt();
            prec = Prec::Milli;
        }
        if (h > 23 || mi > 59 || se > 59) {
            *why = QStringLiteral("%1 is not a time of day").arg(rest);
            return false;
        }
        const QString tz = tm.captured(5);
        if (!tz.isEmpty()) {
            hasTz = true;
            if (tz != QLatin1String("z")) {
                QString hhmm = tz.mid(1);
                hhmm.remove(QLatin1Char(':'));
                tzSecs = (tz.startsWith(QLatin1Char('-')) ? -1 : 1)
                       * (hhmm.left(2).toInt() * 3600 + hhmm.mid(2).toInt() * 60);
            }
        }
    }

    // An upper bound covers the whole unit typed: before:14:09 includes
    // 14:09:45, because that row's Time column reads 14:09.
    const qint64 span = prec == Prec::Minute ? 59999
                      : prec == Prec::Second ? 999 : 0;

    if (!date.isValid()) {
        if (hasTz) {
            *why = QStringLiteral("a time zone needs a date with it");
            return false;
        }
        out->clock = true;
        out->start = ((h * 60LL + mi) * 60 + se) * 1000 + ms;
        out->end   = out->start + span;
        return true;
    }

    const QDateTime dt = hasTz
        ? QDateTime(date, QTime(h, mi, se, ms), Qt::OffsetFromUTC, tzSecs)
        : QDateTime(date, QTime(h, mi, se, ms), m_utc ? Qt::UTC : Qt::LocalTime);
    out->clock = false;
    out->start = dt.toMSecsSinceEpoch();
    out->end   = prec == Prec::Day ? dt.addDays(1).toMSecsSinceEpoch() - 1
                                   : out->start + span;
    return true;
}

bool LogQuery::parseDuration(const QString &text, qint64 *outMs)
{
    // One or more <number><unit>: 15m, 1h30m, 90s, 1.5h. Longest unit
    // spellings first so "min" is not read as "m" followed by junk.
    static const QRegularExpression partRe(QStringLiteral(
        "\\G(\\d+(?:\\.\\d+)?)\\s*(msec|ms|seconds|second|secs|sec|s|minutes|"
        "minute|mins|min|m|hours|hour|hrs|hr|h|days|day|d)"));
    const QString v = text.trimmed().toLower();
    if (v.isEmpty()) return false;

    qint64 total = 0;
    int pos = 0;
    while (pos < v.size()) {
        const QRegularExpressionMatch m = partRe.match(v, pos);
        if (!m.hasMatch()) return false;
        const QString u = m.captured(2);
        qint64 mult;
        if      (u.startsWith(QLatin1String("ms")))  mult = 1;
        else if (u.startsWith(QLatin1Char('s')))      mult = 1000;
        else if (u.startsWith(QLatin1Char('m')))      mult = 60LL * 1000;
        else if (u.startsWith(QLatin1Char('h')))      mult = 3600LL * 1000;
        else                                          mult = 86400LL * 1000;
        total += qint64(m.captured(1).toDouble() * double(mult));
        pos = m.capturedEnd(0);
        while (pos < v.size() && v.at(pos).isSpace()) ++pos;
    }
    *outMs = total;
    return true;
}

QString LogQuery::andConstraint(const QString &userQuery,
                                const QString &constraint)
{
    const QString u = userQuery.trimmed();
    const QString c = constraint.trimmed();
    if (c.isEmpty()) return u;
    if (u.isEmpty()) return c;
    return QStringLiteral("(%1) %2").arg(u, c);
}

QStringList LogQuery::knownFields()
{
    return { "msg", "text", "src", "source", "name", "friendly",
             "sev", "severity", "dir", "direction",
             "hex", "len", "field", "time", "after", "before", "last" };
}

QString LogQuery::fieldName(Field f)
{
    switch (f) {
    case Field::Any:       return QStringLiteral("any");
    case Field::Message:   return QStringLiteral("msg");
    case Field::Source:    return QStringLiteral("src");
    case Field::Friendly:  return QStringLiteral("name");
    case Field::Severity:  return QStringLiteral("sev");
    case Field::Direction: return QStringLiteral("dir");
    case Field::Hex:       return QStringLiteral("hex");
    case Field::Length:    return QStringLiteral("len");
    case Field::Time:      return QStringLiteral("time");
    case Field::Decoded:   return QStringLiteral("field");
    }
    return QString();
}

QString LogQuery::describe(const Node &n, int indent) const
{
    const QString pad(indent * 2, ' ');
    switch (n.op) {
    case Op::And:
    case Op::Or: {
        QString s = pad + (n.op == Op::And ? "AND" : "OR") + "\n";
        for (const NodePtr &k : n.kids) {
            if (k) s += describe(*k, indent + 1);
        }
        return s;
    }
    case Op::Not: {
        QString s = pad + "NOT\n";
        if (!n.kids.isEmpty() && n.kids.at(0)) {
            s += describe(*n.kids.at(0), indent + 1);
        }
        return s;
    }
    case Op::Term:
        break;
    }

    QString what = n.useRegex ? QStringLiteral("regex /%1/").arg(n.regex.pattern())
                              : QStringLiteral("\"%1\"").arg(n.literal);
    if (n.field == Field::Time) {
        auto at = [&](qint64 v) {
            if (n.clock) {
                return QTime::fromMSecsSinceStartOfDay(int(qBound<qint64>(0, v, 86399999)))
                    .toString(QStringLiteral("HH:mm:ss.zzz"));
            }
            return (m_utc ? QDateTime::fromMSecsSinceEpoch(v, Qt::UTC)
                          : QDateTime::fromMSecsSinceEpoch(v))
                .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
        };
        what = (n.hasLo ? at(n.lo) : QStringLiteral("…")) + QStringLiteral(" .. ")
             + (n.hasHi ? at(n.hi) : QStringLiteral("…"));
        if (n.clock) {
            what += (n.hasLo && n.hasHi && n.lo > n.hi)
                        ? QStringLiteral("  (clock time, any date, across midnight)")
                        : QStringLiteral("  (clock time, any date)");
        }
    }
    return pad + fieldName(n.field) + " " + what + "\n";
}

QString LogQuery::explain() const
{
    if (!m_error.isEmpty()) return QStringLiteral("error: %1").arg(m_error);
    if (m_root.isNull())    return QStringLiteral("(empty — matches everything)");
    return describe(*m_root, 0);
}
