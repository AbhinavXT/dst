#ifndef SCHEMAEDIT_H
#define SCHEMAEDIT_H
// =============================================================================
//  schemaedit.{h,cpp} -- what the Schema Editor knows about kavach.xml, and
//  its staged edits (session 308).
//
//  SPECS
//    For each element (field, pad, repeat, enum, map, flag, crc, ...) the
//    attributes the decoder reads, what kind of value each takes, and a
//    one-line meaning. The form is built from these; an attribute in the
//    file that has no spec is still shown, as plain text.
//
//  SESSION
//    The file as opened, and the text with the edits staged so far. Each
//    edit is checked before it is staged: the value must suit its kind, a
//    reference must name something that exists, and the whole edited text
//    must still load in Schema::Decoder. An edit that fails any of these is
//    refused, with the reason, and nothing is staged. Undo and redo walk
//    the staged edits. save() writes them: a timestamped copy of the old
//    file first, then the new text atomically. It refuses the built-in
//    schema, and a file that changed on disk since it was opened.
//
//  RENAMES
//    Renaming a field also rewrites the when=/test= conditions, count= and
//    <row tmpl="{name}"> in the same packet or struct that name it; renaming
//    an enum, struct, flag table, event table or CRC rewrites the enum=,
//    struct=, table= or crc= that point at it. Uses of the old name
//    elsewhere are listed, not changed. DLConsole's own code, the Python
//    validators and fielddocs.json are not changed either: the dialog says
//    so.
// =============================================================================
#include "schemadoc.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QPair>
#include <QVector>

#include <climits>

namespace SchemaEdit {

enum class Kind {
    Text,
    Int,            // a whole number (may be negative)
    Bool,           // "true" / "false"
    Choice,         // one of `choices` (or empty)
    Condition,      // the decoder's condition language
    Count,          // a whole number, or the name of an earlier field
    EnumRef,        // <enum name=>
    StructRef,      // <struct name=>
    FlagTableRef,   // <flagtable name=>
    EventTableRef,  // <eventtable name=>
    CrcRef,         // <crcs><crc name=>
};

struct AttrSpec {
    QString     name;
    Kind        kind = Kind::Text;
    QString     label;      // short, for the form
    QString     hint;       // one line: what it does
    QStringList choices;    // Kind::Choice
    int         min = INT_MIN;   // Kind::Int
    int         max = INT_MAX;
};

// The attributes `tag` takes, in the order the form shows them. `parentTag`
// tells a packet's embedded <crc> from a <crcs> definition.
QVector<AttrSpec> specsFor(const QString &tag, const QString &parentTag = QString());
const AttrSpec *specFor(const QVector<AttrSpec> &specs, const QString &name);

// "Field", "Pad", "Repeat", ... for the tree and the form's title.
QString title(const QString &tag);
// A plain sentence on what the element is.
QString describe(const QString &tag, const QString &parentTag = QString());

// The element's size on the wire in bits, when it is fixed; -1 when it
// depends on the frame (conditions, counted repeats, sub-packets, ...).
int fixedBits(const SchemaDoc::Document &doc, int node);
// The bit offset of each child of `container` from the start of its body,
// -1 once it can no longer be known without a frame.
QVector<int> childOffsets(const SchemaDoc::Document &doc, int container);

// One attribute value that a rename rewrites.
struct Reference {
    int     node = -1;
    QString attr;
    QString before;
    QString after;
};
struct RenamePlan {
    QVector<Reference> changes;    // rewritten with the rename
    QStringList        elsewhere;  // uses of the old name not changed, as "SLRP: <field when="…">"
    QString            warning;    // e.g. a packet name the program looks up
};
RenamePlan planRename(const SchemaDoc::Document &doc, int node, const QString &newName);

// An attribute the element cannot do without (no remove button): name=,
// a field's bits= (unless it has a type=), a case's type= and struct=, ...
bool isRequired(const SchemaDoc::Document &doc, int node, const QString &attr);

// Why `value` cannot be `spec`'s value in `doc`, or empty.
QString valueProblem(const SchemaDoc::Document &doc, const AttrSpec &spec, const QString &value);

// A new element: its tag and its attributes, in the order written.
struct NewElement {
    QString                          tag;
    QVector<QPair<QString, QString>> attrs;
};

// One row of a table to add or change (phase 4: Paste).
struct TableRow {
    QHash<QString, QString> cells;    // column (entryColumns()) -> text
    int                     existing = -1;   // the entry it changes, or -1: a new one
    int                     line = 0;        // in the pasted text, 1-based
    QString                 note;            // shown in the preview: "new", "label A → B", ...
    bool                    skip = false;    // nothing to do (same as now, or a repeat)
};

struct NewContainer {
    QString                          tag;     // packet, struct, enum, flagtable, eventtable, crc
    QString                          name;
    QVector<QPair<QString, QString>> attrs;   // after name=, in order
    // A struct: also decode it as sub-packet `caseType` of this packet.
    QString                          casePacket;
    int                              caseType = -1;
};

class Session
{
public:
    // Reads `path` (":/…" = the built-in copy, read-only).
    bool open(const QString &path, QString *error);
    bool openText(const QByteArray &text, const QString &path, QString *error);

    QString path() const { return m_path; }
    bool readOnly() const { return m_path.startsWith(QLatin1Char(':')); }

    const SchemaDoc::Document &doc() const { return m_doc; }
    QByteArray original() const { return m_original; }
    QByteArray text() const { return m_doc.text(); }

    bool dirty() const { return m_doc.text() != m_original; }
    int  changeCount() const { return m_done.size(); }
    QStringList changes() const;          // the staged edits, oldest first
    bool canUndo() const { return !m_done.isEmpty(); }
    bool canRedo() const { return !m_undone.isEmpty(); }
    QString undoText() const;
    QString redoText() const;
    bool undo();
    bool redo();
    void discard();

    // Edits. False + *why = refused, nothing staged.
    bool setAttribute(int node, const QString &name, const QString &value, QString *why);
    bool removeAttribute(int node, const QString &name, QString *why);
    // name= with the references planRename() finds (or none).
    bool rename(int node, const QString &newName, bool withReferences, QString *why);

    // Structure. *node receives the new (or moved) element's index.
    // previewInsert() is insert() without staging: the dialog's preview.
    bool previewInsert(int anchor, SchemaDoc::Document::Place place, const NewElement &element,
                       QByteArray *text, int *node, QString *why) const;
    bool insert(int anchor, SchemaDoc::Document::Place place, const NewElement &element, int *node, QString *why);
    bool remove(int node, QString *why);
    bool move(int node, int delta, int *moved, QString *why);
    bool duplicate(int node, int *copy, QString *why);
    // Containers. remove(), move() and duplicate() take them too.
    bool previewContainer(const NewContainer &container, QByteArray *text, int *node, QString *why) const;
    bool addContainer(const NewContainer &container, int *node, QString *why);
    // A <case type= struct=> in the packet's <subpackets>.
    bool registerCase(const QString &structName, const QString &packet, int type, int *node, QString *why);

    // Phase 4: enum values, flags and events, as the grid edits them.
    // Several attributes of one element in one step; an empty value removes
    // the attribute.
    bool setAttributes(int node, const QVector<QPair<QString, QString>> &values, const QString &what, QString *why);
    // `text` typed into `column` (entryColumns()) of an entry's row.
    bool setEntryCell(int entry, const QString &column, const QString &text, QString *why);
    // A new row of `table` from the cells typed so far (column -> text).
    // previewEntry() is addEntry() without staging: a row the grid keeps
    // pending while it is not yet enough (*why says what is missing).
    bool previewEntry(int table, const QHash<QString, QString> &cells, NewElement *element, QString *why) const;
    bool addEntry(int table, const QHash<QString, QString> &cells, int *node, QString *why);
    // Pasted rows (parseRows()), one step. `replace`: the table's entries
    // are replaced by them.
    bool applyRows(int table, const QVector<TableRow> &rows, bool replace, QString *why);
    // A member into the condition, repeat or group above (else below) it,
    // or out of the one it is in. *moved receives it.
    bool moveIn(int node, int *moved, QString *why);
    bool moveOut(int node, int *moved, QString *why);

    QStringList diff(int context = 2) const { return SchemaDoc::lineDiff(m_original, m_doc.text(), context); }

    // Writes the staged text: backup beside it, then atomically. *backup
    // gets the backup's path.
    bool save(QString *backup, QString *error);

private:
    // `key` (node:attribute) merges a run of edits to one attribute into
    // one step: a spin box's clicks are one change, not ten.
    bool stage(const QByteArray &text, const QString &what, QString *why, const QString &key = QString());
    // Why `text` may not be staged (not XML, decoder refuses, a new dangling
    // reference), or empty. *parsed gets the parse.
    QString check(const QByteArray &text, SchemaDoc::Document *parsed) const;

    struct Step {
        QByteArray text;
        QString    what;
        QString    key;
        bool       had = false;   // the attribute was there before the step
        QString    before;        // and its value
    };
    QString            m_path;
    QByteArray         m_original;
    SchemaDoc::Document m_doc;
    QVector<Step>      m_done;     // text BEFORE each staged edit, and what it was
    QVector<Step>      m_undone;   // text AFTER an undone edit
};

// ---- structure (phase 2) ----------------------------------------------------

// The element as one line: <field name="X" bits="4"/>.
QString elementLine(const NewElement &element);
// The tags that may go inside `parentTag`, of those the editor adds.
bool allowedIn(const QString &tag, const QString &parentTag);
// The element a Place puts `tag` into: anchor's parent for Before/After,
// the anchor itself for First/LastChild.
int parentFor(const SchemaDoc::Document &doc, int anchor, SchemaDoc::Document::Place place);
// Can this element be deleted, moved or duplicated (a member of a packet,
// struct, enum or table — not the packets, structs and sections themselves)?
QString memberProblem(const SchemaDoc::Document &doc, int node);
// ---- containers (phase 3) ------------------------------------------------------

// A packet, struct, enum, flag table, event table or CRC definition.
bool isContainer(const SchemaDoc::Document &doc, int node);
// The tags a "New" makes, in the order the menu offers them.
QStringList containerTags();

// The packets with a <subpackets>, by name.
QStringList packetsWithSubpackets(const SchemaDoc::Document &doc);
// The smallest sub-packet type the packet does not use yet.
int freeCaseType(const SchemaDoc::Document &doc, const QString &packet);
// The sub-packet cases that decode a struct: "SLRP type 5".
QStringList caseUses(const SchemaDoc::Document &doc, const QString &structName);

// Two packets answering one captype, two of a kind with one name, two
// cases of one type. One line each; check() refuses new ones.
QStringList clashes(const SchemaDoc::Document &doc);

// Uses of names that nothing defines: count= with no such field, enum=,
// struct=, table= and crc= with no such definition. One line each.
QStringList dangling(const SchemaDoc::Document &doc);

// ---- enum values, flags and events (phase 4) ---------------------------------

// enum, flagtable or eventtable: a container the grid edits.
bool isTable(const QString &tag);
// The grid's columns for a table: enum "value", "from", "to", "label",
// "formula", "unit"; flagtable "bit", "name"; eventtable "id", "name",
// "bytes", "signed".
QStringList entryColumns(const QString &tableTag);
QString entryHeader(const QString &column);
// Bit is the flag's place in the table: it cannot be typed.
bool entryEditable(const QString &column);
// What the grid shows in `column` for an entry: an enum's "otherwise" row
// in "value", a flag's bit, "true" for a signed event.
QString entryText(const SchemaDoc::Document &doc, int entry, const QString &column);
// The value a new row starts from: one past the highest enum value (or
// range end) or event id; empty for a flag table.
QString nextValue(const SchemaDoc::Document &doc, int table);
// An entry for undo and the status line: "dmiTrainType › 3", "dmiAlarm › bit 4".
QString entryLabel(const SchemaDoc::Document &doc, int entry);
// Entries the decoder would never show or would read wrongly: an enum value
// named twice (the first wins), a range named wholly above, a range that
// runs backwards or has no end, two "otherwise", an event id twice (the
// later one replaces the first), a flag with no name. check() refuses new ones.
QStringList entryProblems(const SchemaDoc::Document &doc);
// The <flags> that read a flag table, "DLLOG alarm_code (71 bits)", and
// the fewest bits any of them reads (-1: none reads it).
QStringList flagReaders(const SchemaDoc::Document &doc, const QString &table, int *fewest = nullptr);
// Text pasted into a table (a SIF annexure, two Excel columns), as rows:
//   enum:   "3  Spare", "3<TAB>Spare", "1..50 label", "otherwise  rsvd(%v)";
//           a line with no value takes the one after the line before;
//   flags:  one name per line, or "bit<TAB>name";
//   events: "id name [bytes] [signed]", or tab-separated in that order.
// A value already in the table changes that entry; `replace` makes every
// row new. *problems: lines not understood, and repeats.
QVector<TableRow> parseRows(const SchemaDoc::Document &doc, int table, const QString &text, bool replace,
                            QStringList *problems);

// ---- moving between levels (phase 4) -------------------------------------------

// Can `tag` sit inside `parentTag` once moved there? A repeat or group
// reads only fields and pads; a condition, packet or struct reads any member.
bool movableInto(const QString &tag, const QString &parentTag);
// Where moveIn() / moveOut() would put it, or false with *why.
bool planMoveIn(const SchemaDoc::Document &doc, int node, int *anchor, SchemaDoc::Document::Place *place, QString *why);
bool planMoveOut(const SchemaDoc::Document &doc, int node, int *anchor, SchemaDoc::Document::Place *place, QString *why);

// Why the decoder would not load `text`, or empty.
QString loadProblem(const QByteArray &text);

// `text` over `path`, a copy of the old file beside it first
// (`<path>.bak-yyyyMMdd-HHmmss`). Returns the backup's path, or empty with *error.
QString writeWithBackup(const QString &path, const QByteArray &text, QString *error);

// The built-in kavach.xml copied to `dest` and set as the external schema
// (Settings::setSchemaPath). False with *error.
bool makeEditableCopy(const QString &dest, QString *error);

}  // namespace SchemaEdit

#endif  // SCHEMAEDIT_H
