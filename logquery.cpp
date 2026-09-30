#include "logquery.h"

#include "fieldcatalog.h"
#include "fieldindex.h"
#include "namemap.h"
#include "fieldplot.h"

#include <QDate>
#include <QTime>

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

        if (c == '!' || c == '-') {
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
            if (ch.isSpace() || ch == '(' || ch == ')') break;

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

LogQuery::NodePtr LogQuery::makeTerm(const QString &raw, int pos)
{
    auto node = NodePtr::create();
    node->op = Op::Term;

    QString field, value = raw;

    // Split on the first colon — but only when it looks like a field
    // prefix. A bare "14:02" or an unprefixed "a:b" inside quotes must stay
    // a literal, so the colon only counts if what precedes it is a known
    // field name.
    const int colon = raw.indexOf(':');
    if (colon > 0 && !raw.startsWith('"') && !raw.startsWith('/')) {
        const QString maybe = raw.left(colon).toLower();
        if (knownFields().contains(maybe)) {
            field = maybe;
            value = raw.mid(colon + 1);
        }
    }

    if (value.isEmpty()) {
        m_error = QStringLiteral("'%1:' has no value").arg(field);
        m_errorOffset = pos;
        return {};
    }

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
    else if (field == "after")                         node->field = Field::After;
    else if (field == "before")                        node->field = Field::Before;

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
    case Field::After:
    case Field::Before: {
        qint64 ms = 0;
        if (!parseTimeSpec(node->literal, &ms)) {
            m_error = QStringLiteral("could not read '%1' as a time "
                                     "(try 14:02:33, 2026-08-08T14:02, or -15m)")
                          .arg(node->literal);
            m_errorOffset = pos;
            return {};
        }
        node->number = ms;
        break;
    }
    case Field::Decoded: {
        // Grammar: NAME [ & MASK ] [ op VALUE ], op being = == != > >= < <=
        // or ~ for substring. Parsed here rather than at match time so a
        // malformed term is an error the user sees, not a silent no-match.
        QString spec = node->literal;      // already lower-cased and unquoted

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

    case Field::After:  return e.epochMs >= n.number;
    case Field::Before: return e.epochMs <= n.number;
    }
    return false;
}

// ============================== helpers ====================================

bool LogQuery::parseTimeSpec(const QString &s, qint64 *outMs)
{
    const QString v = s.trimmed();
    if (v.isEmpty()) return false;

    // Relative: -15m, -2h, -30s, -1d. Anchored to "now", which is what
    // someone means by "the last fifteen minutes".
    if (v.startsWith('-')) {
        const QChar unit = v.at(v.size() - 1).toLower();
        bool ok = false;
        const qint64 qty = v.mid(1, v.size() - 2).toLongLong(&ok);
        if (!ok || qty < 0) return false;
        qint64 mult = 0;
        switch (unit.toLatin1()) {
        case 's': mult = 1000LL;             break;
        case 'm': mult = 60LL * 1000;        break;
        case 'h': mult = 3600LL * 1000;      break;
        case 'd': mult = 86400LL * 1000;     break;
        default: return false;
        }
        *outMs = QDateTime::currentMSecsSinceEpoch() - qty * mult;
        return true;
    }

    // Full ISO, with or without the 'T'.
    QDateTime dt = QDateTime::fromString(v, Qt::ISODateWithMs);
    if (!dt.isValid()) dt = QDateTime::fromString(v, Qt::ISODate);
    if (!dt.isValid()) {
        QString alt = v;
        alt.replace(' ', 'T');
        dt = QDateTime::fromString(alt, Qt::ISODate);
    }
    if (dt.isValid()) { *outMs = dt.toMSecsSinceEpoch(); return true; }

    // Date only.
    const QDate d = QDate::fromString(v, QStringLiteral("yyyy-MM-dd"));
    if (d.isValid()) {
        *outMs = QDateTime(d, QTime(0, 0)).toMSecsSinceEpoch();
        return true;
    }

    // Time only -> today. Tried longest-first so 14:02:33.500 isn't
    // truncated by the shorter patterns.
    static const char *timeFormats[] = {
        "HH:mm:ss.zzz", "HH:mm:ss", "HH:mm"
    };
    for (const char *fmt : timeFormats) {
        const QTime t = QTime::fromString(v, QLatin1String(fmt));
        if (t.isValid()) {
            *outMs = QDateTime(QDate::currentDate(), t).toMSecsSinceEpoch();
            return true;
        }
    }
    return false;
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
             "hex", "len", "field", "after", "before" };
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
    case Field::After:     return QStringLiteral("after");
    case Field::Before:    return QStringLiteral("before");
    case Field::Decoded:   return QStringLiteral("field");
    }
    return QString();
}

QString LogQuery::describe(const Node &n, int indent)
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
    if (n.field == Field::After || n.field == Field::Before) {
        what = QDateTime::fromMSecsSinceEpoch(n.number)
                   .toString(Qt::ISODateWithMs);
    }
    return pad + fieldName(n.field) + " " + what + "\n";
}

QString LogQuery::explain() const
{
    if (!m_error.isEmpty()) return QStringLiteral("error: %1").arg(m_error);
    if (m_root.isNull())    return QStringLiteral("(empty — matches everything)");
    return describe(*m_root, 0);
}
