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
    int           closeStart = 0; // the end tag's '<' (== tagEnd if self-closing)
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

    // ---- structure (session 308, phase 2) ---------------------------------
    // Where a new element goes, relative to an anchor element.
    // BeforeBlock: before the anchor and the comment lines right above it
    // (phase 4: a new <enums> section goes above the first packet's comment).
    enum class Place { Before, After, FirstChild, LastChild, BeforeBlock };

    // The whole lines an element owns: from the start of its first line to
    // just past the line break of its last, with a comment on the same line
    // after it. False (*why) when another element shares one of those lines,
    // which a line edit would cut through.
    bool lineSpan(int node, int *from, int *to, QString *why = nullptr) const;
    // lineSpan() widened upward over the comment lines right above it (a
    // packet's or struct's description goes where it goes).
    bool blockSpan(int node, int *from, int *to, QString *why = nullptr) const;
    // The text with node's block moved to just before (after == false) or
    // after `target`'s block. *at receives its new start.
    QByteArray withMovedBlock(int node, int target, bool after, int *at, QString *why) const;
    // The text without node's block (comment above included).
    QByteArray withoutBlock(int node, QString *why) const;
    // The whitespace before an element on its line.
    QByteArray indentOf(int node) const;
    // "\r\n" when the file's lines end that way, else "\n".
    QByteArray lineEnd() const;

    // The text with `line` (one element, no indent, no line break) added as
    // a line of its own. A `line` with '\n's is a block (lines indented
    // relative to its first), each line put under the indent. *at receives the byte offset of its '<' in the new
    // text. `columnsFrom` (an element, or -1) lines its attributes up with
    // that element's: the first under its first, the rest under the same name.
    QByteArray withInserted(int anchor, Place place, const QString &line, int columnsFrom,
                            int *at, QString *why) const;
    // The text without the element's lines (its children go with it).
    QByteArray withoutNode(int node, QString *why) const;
    // ---- phase 4 ---------------------------------------------------------
    // The start of the whole-line comments right above `node` when they are
    // its own, else -1. They are not its own when the element is followed
    // straight away by a sibling: then they head that run of fields (a
    // section label), and stay. A blank line, an end tag, another comment
    // or the end of the file after it: they are its own.
    int ownCommentStart(int node) const;
    // withoutNode(), with its own comment (*tookComment says whether there was one).
    QByteArray withoutMember(int node, QString *why, bool *tookComment = nullptr) const;
    // The element's lines taken out and put at `anchor`/`place`, re-indented
    // for where they land (a field into or out of a condition). `anchor` is
    // an element of THIS document, not inside `node`. *at receives the
    // element's '<' in the new text.
    QByteArray withRelocated(int node, int anchor, Place place, int *at, QString *why) const;
    // A one-line empty element (an enum value, a flag, an event) rewritten as
    // `line`, its attributes in `columnsFrom`'s columns (an element, or -1
    // for its own). Its index stays the same.
    QByteArray withReplacedElement(int node, const QString &line, int columnsFrom, QString *why) const;
    // The element's lines swapped with its previous (delta -1) or next (+1)
    // sibling element's. *at receives its new start offset.
    QByteArray withMoved(int node, int delta, int *at, QString *why) const;
    // The element's lines again, right after it. *at receives the copy's start.
    QByteArray withDuplicated(int node, int *at, QString *why) const;

    // The element whose '<' is at `offset`, or -1.
    int nodeAt(int offset) const;
    // Child positions from the root: a selection that survives an edit
    // elsewhere. resolve() clamps to what still exists (the last sibling,
    // else the parent).
    QVector<int> pathOf(int node) const;
    int resolve(const QVector<int> &path) const;

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
