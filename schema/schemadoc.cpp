#include "schemadoc.h"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace SchemaDoc {

const Attr *Node::attr(const QString &name) const
{
    for (const Attr &a : attrs) {
        if (a.name == name) {
            return &a;
        }
    }
    return nullptr;
}

QString Node::value(const QString &name) const
{
    const Attr *a = attr(name);
    return a ? a->value : QString();
}

QString escape(const QString &value, char quote)
{
    QString out;
    out.reserve(value.size());
    for (const QChar c : value) {
        if (c == QLatin1Char('&')) {
            out += QLatin1String("&amp;");
        } else if (c == QLatin1Char('<')) {
            out += QLatin1String("&lt;");
        } else if (c == QLatin1Char('>')) {
            out += QLatin1String("&gt;");
        } else if (c == QLatin1Char('"') && quote == '"') {
            out += QLatin1String("&quot;");
        } else if (c == QLatin1Char('\'') && quote == '\'') {
            out += QLatin1String("&apos;");
        } else {
            out += c;
        }
    }
    return out;
}

QString unescape(const QByteArray &raw)
{
    const QString s = QString::fromUtf8(raw);
    if (!s.contains(QLatin1Char('&'))) {
        return s;
    }
    QString out;
    out.reserve(s.size());
    for (int i = 0; i < s.size(); ++i) {
        if (s.at(i) != QLatin1Char('&')) {
            out += s.at(i);
            continue;
        }
        const int semi = s.indexOf(QLatin1Char(';'), i);
        if (semi < 0 || semi - i > 10) {
            out += s.at(i);
            continue;
        }
        const QString ent = s.mid(i + 1, semi - i - 1);
        QString rep;
        if (ent == QLatin1String("amp")) rep = QStringLiteral("&");
        else if (ent == QLatin1String("lt")) rep = QStringLiteral("<");
        else if (ent == QLatin1String("gt")) rep = QStringLiteral(">");
        else if (ent == QLatin1String("quot")) rep = QStringLiteral("\"");
        else if (ent == QLatin1String("apos")) rep = QStringLiteral("'");
        else if (ent.startsWith(QLatin1Char('#'))) {
            bool ok = false;
            const uint code = ent.startsWith(QLatin1String("#x")) ? ent.mid(2).toUInt(&ok, 16)
                                                                   : ent.mid(1).toUInt(&ok, 10);
            // Not QString::fromUcs4: its argument type differs between Qt 5 and 6.
            if (ok && code > 0 && code <= 0xFFFF) {
                rep = QString(QChar(ushort(code)));
            } else if (ok && code > 0xFFFF && code <= 0x10FFFF) {
                const QChar pair[2] = { QChar(QChar::highSurrogate(code)), QChar(QChar::lowSurrogate(code)) };
                rep = QString(pair, 2);
            }
        }
        if (rep.isNull()) {
            out += s.at(i);
            continue;
        }
        out += rep;
        i = semi;
    }
    return out;
}

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool isNameChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'
           || c == ':' || c == '.' || (static_cast<unsigned char>(c) >= 0x80);
}

}  // namespace

bool Document::parse(const QByteArray &text, QString *error)
{
    m_text = text;
    m_nodes.clear();
    const char *b = text.constData();
    const int n = text.size();
    int line = 1;
    int lineAt = 0;   // offset `line` was counted up to
    auto lineOf = [&](int pos) {
        for (; lineAt < pos; ++lineAt) {
            if (b[lineAt] == '\n') ++line;
        }
        return line;
    };
    auto fail = [&](int pos, const QString &why) {
        if (error) *error = QStringLiteral("line %1: %2").arg(lineOf(pos)).arg(why);
        m_nodes.clear();
        return false;
    };

    QVector<int> stack;
    QString pendingComment;
    int i = 0;
    while (i < n) {
        if (b[i] != '<') {
            if (!isSpace(b[i])) pendingComment.clear();   // text between: the comment was about it
            ++i;
            continue;
        }
        if (text.mid(i, 4) == "<!--") {
            const int close = text.indexOf("-->", i + 4);
            if (close < 0) return fail(i, QStringLiteral("unterminated comment"));
            const QString c = QString::fromUtf8(text.mid(i + 4, close - i - 4)).trimmed();
            pendingComment = pendingComment.isEmpty() ? c : pendingComment + QLatin1Char('\n') + c;
            i = close + 3;
            continue;
        }
        if (text.mid(i, 2) == "<?") {
            const int close = text.indexOf("?>", i + 2);
            if (close < 0) return fail(i, QStringLiteral("unterminated <?"));
            i = close + 2;
            continue;
        }
        if (text.mid(i, 9) == "<![CDATA[") {
            const int close = text.indexOf("]]>", i + 9);
            if (close < 0) return fail(i, QStringLiteral("unterminated CDATA"));
            i = close + 3;
            continue;
        }
        if (text.mid(i, 2) == "<!") {
            const int close = text.indexOf('>', i + 2);
            if (close < 0) return fail(i, QStringLiteral("unterminated <!"));
            i = close + 1;
            continue;
        }
        if (i + 1 < n && b[i + 1] == '/') {
            int j = i + 2;
            while (j < n && isNameChar(b[j])) ++j;
            const QString tag = QString::fromUtf8(text.mid(i + 2, j - i - 2));
            const int close = text.indexOf('>', j);
            if (close < 0) return fail(i, QStringLiteral("unterminated </%1").arg(tag));
            if (stack.isEmpty() || m_nodes.at(stack.last()).tag != tag) {
                return fail(i, QStringLiteral("</%1> does not close %2").arg(
                                   tag, stack.isEmpty() ? QStringLiteral("anything")
                                                        : QStringLiteral("<%1>").arg(m_nodes.at(stack.last()).tag)));
            }
            m_nodes[stack.last()].end = close + 1;
            m_nodes[stack.last()].closeStart = i;
            stack.removeLast();
            pendingComment.clear();
            i = close + 1;
            continue;
        }
        // A start tag.
        Node node;
        node.start = i;
        node.line = lineOf(i);
        int j = i + 1;
        while (j < n && isNameChar(b[j])) ++j;
        node.tag = QString::fromUtf8(text.mid(i + 1, j - i - 1));
        if (node.tag.isEmpty()) return fail(i, QStringLiteral("'<' that starts no element"));
        for (;;) {
            while (j < n && isSpace(b[j])) ++j;
            if (j >= n) return fail(i, QStringLiteral("unterminated <%1").arg(node.tag));
            if (b[j] == '>') {
                ++j;
                break;
            }
            if (b[j] == '/' && j + 1 < n && b[j + 1] == '>') {
                node.selfClosing = true;
                j += 2;
                break;
            }
            Attr a;
            a.start = j;
            while (j < n && isNameChar(b[j])) ++j;
            a.name = QString::fromUtf8(text.mid(a.start, j - a.start));
            if (a.name.isEmpty()) return fail(j, QStringLiteral("unexpected '%1' in <%2>").arg(QChar(b[j]), node.tag));
            while (j < n && isSpace(b[j])) ++j;
            if (j >= n || b[j] != '=') return fail(j, QStringLiteral("attribute %1 has no value").arg(a.name));
            ++j;
            while (j < n && isSpace(b[j])) ++j;
            if (j >= n || (b[j] != '"' && b[j] != '\'')) {
                return fail(j, QStringLiteral("attribute %1's value is not quoted").arg(a.name));
            }
            a.quote = b[j];
            a.valueStart = j + 1;
            const int close = text.indexOf(a.quote, a.valueStart);
            if (close < 0) return fail(j, QStringLiteral("attribute %1's value is not closed").arg(a.name));
            a.valueEnd = close;
            a.value = unescape(text.mid(a.valueStart, close - a.valueStart));
            node.attrs.push_back(a);
            j = close + 1;
        }
        node.tagEnd = j;
        node.end = j;
        node.closeStart = j;
        node.comment = pendingComment;
        pendingComment.clear();
        node.parent = stack.isEmpty() ? -1 : stack.last();
        node.depth = stack.size();
        if (node.parent < 0 && !m_nodes.isEmpty()) return fail(i, QStringLiteral("a second root element <%1>").arg(node.tag));
        const int index = m_nodes.size();
        if (node.parent >= 0) m_nodes[node.parent].children.push_back(index);
        m_nodes.push_back(node);
        if (!node.selfClosing) stack.push_back(index);
        i = j;
    }
    if (!stack.isEmpty()) return fail(n, QStringLiteral("<%1> is never closed").arg(m_nodes.at(stack.last()).tag));
    if (m_nodes.isEmpty()) return fail(0, QStringLiteral("no element"));
    return true;
}

int Document::child(int parent, const QString &tag, const QString &name) const
{
    if (parent < 0 || parent >= m_nodes.size()) return -1;
    for (int c : m_nodes.at(parent).children) {
        const Node &k = m_nodes.at(c);
        if (k.tag == tag && (name.isNull() || k.value(QStringLiteral("name")) == name)) return c;
    }
    return -1;
}

QVector<int> Document::all(const QString &tag) const
{
    QVector<int> out;
    for (int i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes.at(i).tag == tag) out.push_back(i);
    }
    return out;
}

int Document::container(int i) const
{
    if (i <= 0 || i >= m_nodes.size()) return -1;
    QVector<int> path;
    for (int k = i; k > 0; k = m_nodes.at(k).parent) path.prepend(k);
    // path[0] is a child of <schema>.
    static const QSet<QString> sections = { QStringLiteral("enums"), QStringLiteral("crcs"),
                                            QStringLiteral("flagtables"), QStringLiteral("eventtables") };
    if (sections.contains(m_nodes.at(path.first()).tag)) {
        return path.size() > 1 ? path.at(1) : path.first();
    }
    return path.first();
}

QByteArray Document::withAttribute(int node, const QString &name, const QString &value) const
{
    const Node &nd = m_nodes.at(node);
    if (const Attr *a = nd.attr(name)) {
        const QByteArray enc = escape(value, a->quote).toUtf8();
        const int delta = enc.size() - (a->valueEnd - a->valueStart);
        QByteArray out = m_text.left(a->valueStart) + enc;
        // Padding after the closing quote: keep the next attribute's column
        // when the file had more than one space there (an aligned block).
        int k = a->valueEnd + 1;
        int pad = 0;
        while (k + pad < nd.tagEnd && m_text.at(k + pad) == ' ') ++pad;
        const char next = k + pad < nd.tagEnd ? m_text.at(k + pad) : '>';
        const bool attrFollows = next != '/' && next != '>' && next != '\r' && next != '\n';
        if (pad > 1 && attrFollows) {
            const int want = std::max(1, pad - delta);
            out += m_text.mid(a->valueEnd, 1) + QByteArray(want, ' ') + m_text.mid(k + pad);
        } else {
            out += m_text.mid(a->valueEnd);
        }
        return out;
    }
    // Added at the end of the start tag, before "/>" or ">", after the last
    // attribute (whatever whitespace followed it is kept after the new one).
    int at = nd.tagEnd - (nd.selfClosing ? 2 : 1);
    if (!nd.attrs.isEmpty()) {
        at = nd.attrs.last().valueEnd + 1;
    } else {
        while (at > nd.start && isSpace(m_text.at(at - 1))) --at;
    }
    const QByteArray piece = " " + name.toUtf8() + "=\"" + escape(value, '"').toUtf8() + "\"";
    return m_text.left(at) + piece + m_text.mid(at);
}

QByteArray Document::withoutAttribute(int node, const QString &name) const
{
    const Node &nd = m_nodes.at(node);
    const Attr *a = nd.attr(name);
    if (!a) return m_text;
    // Take the whitespace before it with it, but never the line break: an
    // attribute that starts a continuation line goes with its indent.
    int from = a->start;
    while (from > nd.start && (m_text.at(from - 1) == ' ' || m_text.at(from - 1) == '\t')) --from;
    int to = a->valueEnd + 1;
    if (from > 0 && m_text.at(from - 1) == '\n') {
        while (to < nd.tagEnd && (m_text.at(to) == ' ' || m_text.at(to) == '\t')) ++to;
    }
    return m_text.left(from) + m_text.mid(to);
}

QString Document::startTag(int node) const
{
    const Node &nd = m_nodes.at(node);
    return QString::fromUtf8(m_text.mid(nd.start, nd.tagEnd - nd.start));
}


// -----------------------------------------------------------------------------
//  Structure
// -----------------------------------------------------------------------------

QByteArray Document::lineEnd() const
{
    const int nl = m_text.indexOf('\n');
    return nl > 0 && m_text.at(nl - 1) == '\r' ? QByteArrayLiteral("\r\n") : QByteArrayLiteral("\n");
}

QByteArray Document::indentOf(int node) const
{
    const int s = m_nodes.at(node).start;
    int b = s;
    while (b > 0 && (m_text.at(b - 1) == ' ' || m_text.at(b - 1) == '\t')) --b;
    return m_text.mid(b, s - b);
}

bool Document::lineSpan(int node, int *from, int *to, QString *why) const
{
    const Node &n = m_nodes.at(node);
    int b = n.start;
    while (b > 0 && (m_text.at(b - 1) == ' ' || m_text.at(b - 1) == '\t')) --b;
    if (b > 0 && m_text.at(b - 1) != '\n') {
        if (why) *why = QStringLiteral("line %1: <%2> shares its line with something before it; edit the file by hand").arg(n.line).arg(n.tag);
        return false;
    }
    int e = n.end;
    while (e < m_text.size() && (m_text.at(e) == ' ' || m_text.at(e) == '\t')) ++e;
    if (m_text.mid(e, 4) == "<!--") {   // a comment on the same line goes with it
        const int c = m_text.indexOf("-->", e);
        const int nl = m_text.indexOf('\n', e);
        if (c >= 0 && (nl < 0 || c < nl)) {
            e = c + 3;
            while (e < m_text.size() && (m_text.at(e) == ' ' || m_text.at(e) == '\t')) ++e;
        }
    }
    if (e < m_text.size() && m_text.at(e) == '\r') ++e;
    if (e < m_text.size() && m_text.at(e) != '\n') {
        if (why) *why = QStringLiteral("line %1: <%2> shares its line with something after it; edit the file by hand").arg(n.line).arg(n.tag);
        return false;
    }
    if (e < m_text.size()) ++e;
    *from = b;
    *to = e;
    return true;
}

namespace {

// Column (from the line start) of each attribute of a one-line start tag,
// by name, and of the first one under "".
QHash<QString, int> attrColumns(const QByteArray &text, const Node &n)
{
    QHash<QString, int> cols;
    int ls = n.start;
    while (ls > 0 && text.at(ls - 1) != '\n') --ls;
    if (text.mid(n.start, n.tagEnd - n.start).contains('\n')) return cols;
    for (int k = 0; k < n.attrs.size(); ++k) {
        const int c = QString::fromUtf8(text.mid(ls, n.attrs.at(k).start - ls)).size();
        if (k == 0) cols.insert(QString(), c);
        cols.insert(n.attrs.at(k).name, c);
    }
    return cols;
}

// `line` (an element) with its attributes moved to the columns given.
QString aligned(const QString &indent, const QString &line, const QHash<QString, int> &cols)
{
    if (cols.isEmpty()) return indent + line;
    Document probe;
    if (!probe.parse(line.toUtf8()) || probe.count() != 1) return indent + line;
    const Node &n = probe.node(0);
    QString out = indent + QLatin1Char('<') + n.tag;
    for (int k = 0; k < n.attrs.size(); ++k) {
        const Attr &a = n.attrs.at(k);
        const int want = k == 0 ? cols.value(QString(), -1) : cols.value(a.name, -1);
        const int pad = want > out.size() ? want - out.size() : 1;
        out += QString(pad, QLatin1Char(' ')) + QString::fromUtf8(probe.text().mid(a.start, a.valueEnd + 1 - a.start));
    }
    out += QString::fromUtf8(probe.text().mid(n.attrs.isEmpty() ? n.tag.size() + 1 : n.attrs.last().valueEnd + 1));
    return out;
}

}  // namespace

QByteArray Document::withInserted(int anchor, Place place, const QString &line, int columnsFrom,
                                  int *at, QString *why) const
{
    const Node &a = m_nodes.at(anchor);
    int pos = -1;
    QByteArray indent;
    if (place == Place::Before || place == Place::After) {
        if (a.parent < 0) {
            if (why) *why = QStringLiteral("Nothing goes beside the root element");
            return QByteArray();
        }
        int from = 0, to = 0;
        if (!lineSpan(anchor, &from, &to, why)) return QByteArray();
        pos = place == Place::Before ? from : to;
        indent = indentOf(anchor);
    } else if (place == Place::LastChild && !a.children.isEmpty()) {
        return withInserted(a.children.last(), Place::After, line, columnsFrom, at, why);
    } else if (place == Place::FirstChild && !a.children.isEmpty()) {
        return withInserted(a.children.first(), Place::Before, line, columnsFrom, at, why);
    } else {
        // An element with no children yet: between its start and end tags.
        if (a.selfClosing) {
            if (why) *why = QStringLiteral("<%1> is written as one empty tag; give it an end tag by hand first").arg(a.tag);
            return QByteArray();
        }
        int ls = a.closeStart;
        while (ls > 0 && (m_text.at(ls - 1) == ' ' || m_text.at(ls - 1) == '\t')) --ls;
        if (ls == 0 || m_text.at(ls - 1) != '\n') {
            if (why) *why = QStringLiteral("</%1> is not on a line of its own; edit the file by hand").arg(a.tag);
            return QByteArray();
        }
        pos = ls;
        indent = indentOf(anchor) + "  ";
    }
    QHash<QString, int> cols;
    if (columnsFrom >= 0 && columnsFrom < m_nodes.size()) cols = attrColumns(m_text, m_nodes.at(columnsFrom));
    const QByteArray text = aligned(QString::fromUtf8(indent), line, cols).toUtf8() + lineEnd();
    if (pos > m_text.size()) pos = m_text.size();
    QByteArray out = m_text.left(pos);
    if (pos == m_text.size() && !out.isEmpty() && !out.endsWith('\n')) out += lineEnd();
    if (at) *at = out.size() + indent.size();
    return out + text + m_text.mid(pos);
}

QByteArray Document::withoutNode(int node, QString *why) const
{
    if (node <= 0) {
        if (why) *why = QStringLiteral("The root element stays");
        return QByteArray();
    }
    int from = 0, to = 0;
    if (!lineSpan(node, &from, &to, why)) return QByteArray();
    return m_text.left(from) + m_text.mid(to);
}

QByteArray Document::withMoved(int node, int delta, int *at, QString *why) const
{
    const Node &n = m_nodes.at(node);
    if (n.parent < 0) return QByteArray();
    const QVector<int> &sib = m_nodes.at(n.parent).children;
    const int k = sib.indexOf(node);
    const int other = k + delta;
    if (other < 0 || other >= sib.size()) {
        if (why) *why = delta < 0 ? QStringLiteral("It is already first") : QStringLiteral("It is already last");
        return QByteArray();
    }
    const int first = delta < 0 ? sib.at(other) : node;
    const int second = delta < 0 ? node : sib.at(other);
    int f1 = 0, t1 = 0, f2 = 0, t2 = 0;
    if (!lineSpan(first, &f1, &t1, why) || !lineSpan(second, &f2, &t2, why)) return QByteArray();
    // first's lines, whatever lies between (comments, blank lines), second's lines.
    const QByteArray a = m_text.mid(f1, t1 - f1), mid = m_text.mid(t1, f2 - t1), b = m_text.mid(f2, t2 - f2);
    QByteArray out = m_text.left(f1) + b + mid + a + m_text.mid(t2);
    // The moved element's start in the new text.
    const int self0 = delta < 0 ? f2 : f1;            // its lines' start, before
    const int selfLineStart = delta < 0 ? f1 : f1 + b.size() + mid.size();
    if (at) *at = selfLineStart + (n.start - self0);
    return out;
}

QByteArray Document::withDuplicated(int node, int *at, QString *why) const
{
    int from = 0, to = 0;
    if (!lineSpan(node, &from, &to, why)) return QByteArray();
    QByteArray copy = m_text.mid(from, to - from);
    if (!copy.endsWith('\n')) copy = lineEnd() + copy;
    if (at) *at = to + (m_nodes.at(node).start - from) + (m_text.mid(from, to - from).endsWith('\n') ? 0 : lineEnd().size());
    return m_text.left(to) + copy + m_text.mid(to);
}

int Document::nodeAt(int offset) const
{
    for (int i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes.at(i).start == offset) return i;
    }
    return -1;
}

QVector<int> Document::pathOf(int node) const
{
    QVector<int> path;
    for (int k = node; k > 0 && k < m_nodes.size(); k = m_nodes.at(k).parent) {
        path.prepend(m_nodes.at(m_nodes.at(k).parent).children.indexOf(k));
    }
    return path;
}

int Document::resolve(const QVector<int> &path) const
{
    if (m_nodes.isEmpty()) return -1;
    int at = 0;
    for (int p : path) {
        const QVector<int> &kids = m_nodes.at(at).children;
        if (kids.isEmpty()) return at;
        at = kids.at(qBound(0, p, int(kids.size()) - 1));
    }
    return at;
}

// -----------------------------------------------------------------------------

QStringList lineDiff(const QByteArray &before, const QByteArray &after, int context)
{
    auto split = [](const QByteArray &t) {
        QStringList out;
        for (QByteArray l : t.split('\n')) {
            if (l.endsWith('\r')) l.chop(1);
            out << QString::fromUtf8(l);
        }
        return out;
    };
    const QStringList a = split(before), b = split(after);
    int pre = 0;
    while (pre < a.size() && pre < b.size() && a.at(pre) == b.at(pre)) ++pre;
    int suf = 0;
    while (suf < a.size() - pre && suf < b.size() - pre && a.at(a.size() - 1 - suf) == b.at(b.size() - 1 - suf)) ++suf;
    const int n = a.size() - pre - suf, m = b.size() - pre - suf;
    if (n == 0 && m == 0) return {};

    // ops over the middle: ' ' keep, '-' a only, '+' b only.
    struct Op { char kind; int ai; int bi; };
    QVector<Op> ops;
    if (qint64(n) * m <= 4000000) {
        QVector<int> L((n + 1) * (m + 1), 0);
        auto at = [&](int i, int j) -> int & { return L[i * (m + 1) + j]; };
        for (int i = n - 1; i >= 0; --i) {
            for (int j = m - 1; j >= 0; --j) {
                at(i, j) = a.at(pre + i) == b.at(pre + j) ? at(i + 1, j + 1) + 1 : std::max(at(i + 1, j), at(i, j + 1));
            }
        }
        int i = 0, j = 0;
        while (i < n || j < m) {
            if (i < n && j < m && a.at(pre + i) == b.at(pre + j)) {
                ops.push_back({ ' ', pre + i++, pre + j++ });
            } else if (i < n && (j == m || at(i + 1, j) >= at(i, j + 1))) {
                ops.push_back({ '-', pre + i++, -1 });   // removed lines before the added ones
            } else {
                ops.push_back({ '+', -1, pre + j++ });
            }
        }
    } else if (n == m) {
        for (int i = 0; i < n; ++i) {
            if (a.at(pre + i) == b.at(pre + i)) {
                ops.push_back({ ' ', pre + i, pre + i });
            } else {
                ops.push_back({ '-', pre + i, -1 });
                ops.push_back({ '+', -1, pre + i });
            }
        }
    } else {
        for (int i = 0; i < n; ++i) ops.push_back({ '-', pre + i, -1 });
        for (int j = 0; j < m; ++j) ops.push_back({ '+', -1, pre + j });
    }

    // The leading/trailing context lines join the ops, then hunks are cut.
    QVector<Op> all;
    for (int k = 0; k < pre; ++k) all.push_back({ ' ', k, k });
    all += ops;
    for (int k = 0; k < suf; ++k) all.push_back({ ' ', int(a.size()) - suf + k, int(b.size()) - suf + k });

    QVector<bool> show(all.size(), false);
    for (int k = 0; k < all.size(); ++k) {
        if (all.at(k).kind == ' ') continue;
        for (int d = std::max(0, k - context); d <= std::min(int(all.size()) - 1, k + context); ++d) show[d] = true;
    }
    QStringList out;
    bool inHunk = false;
    for (int k = 0; k < all.size(); ++k) {
        if (!show.at(k)) {
            inHunk = false;
            continue;
        }
        const Op &op = all.at(k);
        if (!inHunk) {
            const int lineNo = (op.bi >= 0 ? op.bi : op.ai) + 1;
            out << QStringLiteral("@@ line %1").arg(lineNo);
            inHunk = true;
        }
        if (op.kind == ' ') out << QStringLiteral("  ") + a.at(op.ai);
        else if (op.kind == '-') out << QStringLiteral("- ") + a.at(op.ai);
        else out << QStringLiteral("+ ") + b.at(op.bi);
    }
    return out;
}

}  // namespace SchemaDoc
