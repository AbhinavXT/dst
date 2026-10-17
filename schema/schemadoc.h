#ifndef SCHEMADOC_H
#define SCHEMADOC_H
// =============================================================================
//  schemadoc.{h,cpp} -- kavach.xml as text, with every element's place in it
//  (session 308: Tools ▸ Schema ▸ Schema Editor).
//
//  WHY NOT QDomDocument
//    A DOM parse and re-serialise rewrites the whole file: attribute order,
//    quoting, the column alignment the file is written in, comments next to
//    the line they explain. A one-attribute edit would be a 2300-line diff.
//    This reads the file once, keeps the byte offsets of each element and
//    each attribute, and an edit replaces exactly those bytes. Everything
//    else, CRLF included, stays as it was.
//
//  WHAT IT UNDERSTANDS
//    Elements, attributes (either quote), comments, the <?xml?> declaration,
//    CDATA and DOCTYPE (skipped). Attribute values are decoded for display
//    (&amp; &lt; &gt; &quot; &apos; &#N; &#xN;) and escaped again on write.
//    It is not a validating parser: Schema::Decoder::loadData() is the
//    judge of whether an edited text is still a schema.
//
//  IDENTITY
//    An element is its index in document order (nodes()). An attribute edit
//    does not change the element structure, so indices survive it.
// =============================================================================
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace SchemaDoc {

struct Attr {
    QString name;
    QString value;        // decoded
    int     start = 0;    // byte offset of the name
    int     valueStart = 0;   // first byte inside the quotes
    int     valueEnd = 0;     // the closing quote
    char    quote = '"';
};

struct Node {
    QString       tag;
    int           start = 0;      // the '<'
    int           tagEnd = 0;     // one past the start tag's '>'
    int           end = 0;        // one past the end tag (== tagEnd if self-closing)
    bool          selfClosing = false;
    int           line = 0;       // 1-based line of the '<'
    int           parent = -1;
    int           depth = 0;
    QVector<Attr> attrs;
    QVector<int>  children;       // element children, document order
    QString       comment;        // the comment(s) right before it, trimmed

    const Attr *attr(const QString &name) const;
    QString value(const QString &name) const;   // empty if absent
    bool has(const QString &name) const { return attr(name) != nullptr; }
};

class Document
{
public:
    bool parse(const QByteArray &text, QString *error = nullptr);

    const QByteArray &text() const { return m_text; }
    const QVector<Node> &nodes() const { return m_nodes; }
    const Node &node(int i) const { return m_nodes.at(i); }
    int count() const { return m_nodes.size(); }
    int root() const { return m_nodes.isEmpty() ? -1 : 0; }

    // The first child of `parent` with this tag (and name="…" if given).
    int child(int parent, const QString &tag, const QString &name = QString()) const;
    // Every element with this tag, document order.
    QVector<int> all(const QString &tag) const;
    // The packet or struct (or enum, flagtable, ...) a node sits in: the
    // ancestor directly under a top-level section, or under <schema>.
    int container(int i) const;

    // The file's text with one attribute set, added (at the end of the start
    // tag) or removed. Values are escaped. Replacing keeps the next
    // attribute's column when the file had padded it.
    QByteArray withAttribute(int node, const QString &name, const QString &value) const;
    QByteArray withoutAttribute(int node, const QString &name) const;

    // The source text of a node's start tag, for display.
    QString startTag(int node) const;

private:
    QByteArray    m_text;
    QVector<Node> m_nodes;
};

// & < > and the quote character, escaped.
QString escape(const QString &value, char quote = '"');
// Entities decoded.
QString unescape(const QByteArray &raw);

// A plain line diff, for the review dialog: "  " unchanged context (up to
// `context` lines around each change), "- " removed, "+ " added, and
// "@@ line N" before each hunk. Lines keep no '\r'.
QStringList lineDiff(const QByteArray &before, const QByteArray &after, int context = 2);

}  // namespace SchemaDoc

#endif  // SCHEMADOC_H
