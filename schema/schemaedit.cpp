#include "schemaedit.h"

#include "schemadecoder.h"
#include "settings.h"

#include <QDateTime>
#include <QFile>
#include <QObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

namespace SchemaEdit {

namespace {

AttrSpec spec(const char *name, Kind kind, const QString &label, const QString &hint,
              int min = INT_MIN, int max = INT_MAX)
{
    AttrSpec s;
    s.name = QString::fromLatin1(name);
    s.kind = kind;
    s.label = label;
    s.hint = hint;
    s.min = min;
    s.max = max;
    return s;
}

AttrSpec choice(const char *name, const QString &label, const QString &hint, const QStringList &choices)
{
    AttrSpec s = spec(name, Kind::Choice, label, hint);
    s.choices = choices;
    return s;
}

QString tr(const char *text) { return QObject::tr(text); }

}  // namespace

QVector<AttrSpec> specsFor(const QString &tag, const QString &parentTag)
{
    const AttrSpec name = spec("name", Kind::Text, tr("Name"), tr("What the decoder shows; conditions and counts refer to it"));
    if (tag == QLatin1String("field")) {
        return {
            name,
            spec("bits", Kind::Int, tr("Bits"), tr("Width on the wire"), 1, 4096),
            choice("type", tr("Type"), tr("Empty = an integer of Bits; float / double = IEEE-754; char = Count bytes of text"),
                   { QString(), QStringLiteral("float"), QStringLiteral("double"), QStringLiteral("char") }),
            spec("count", Kind::Int, tr("Characters"), tr("type=\"char\": how many bytes of text"), 1, 4096),
            spec("signed", Kind::Bool, tr("Signed"), tr("Two's-complement signed value")),
            spec("unit", Kind::Text, tr("Unit"), tr("Shown after the value (display only)")),
            spec("enum", Kind::EnumRef, tr("Value names"), tr("Show the number through this <enum>")),
            spec("hex", Kind::Int, tr("Hex digits"), tr("Show as 0x… with this many digits"), 1, 32),
            spec("hexup", Kind::Int, tr("HEX digits"), tr("As Hex digits, upper case"), 1, 32),
            spec("prec", Kind::Int, tr("Decimals"), tr("float / double: digits after the point"), 0, 17),
            spec("id", Kind::Text, tr("Alias"), tr("A second name later conditions and counts may use")),
            spec("when", Kind::Condition, tr("Only when"), tr("Read the field only when this holds, e.g. cls==1")),
            spec("hide", Kind::Bool, tr("Hidden"), tr("Read (conditions can use it) but show no row")),
            spec("tmpl", Kind::Text, tr("Template"), tr("Inside a repeat or group: how the value reads, %s = the value")),
            spec("format", Kind::Text, tr("Formatter"), tr("A composite formatter registered in code (dm, ipv4, sigInfo, ...)")),
            spec("meaning", Kind::Text, tr("Meaning hook"), tr("A value-meaning hook registered in code, with Event id")),
            spec("eid", Kind::Int, tr("Event id"), tr("The id passed to the meaning hook")),
            choice("role", tr("Location role"), tr("How the field composes an absolute track location"),
                   { QString(), QStringLiteral("seglen"), QStringLiteral("start"), QStringLiteral("len"), QStringLiteral("taghop") }),
            spec("ui-group", Kind::Text, tr("Config group"), tr("Loco Configuration: the group the row is in")),
            spec("ui-note", Kind::Text, tr("Config note"), tr("Loco Configuration: the note beside the row")),
            spec("ui-format", Kind::Text, tr("Config format"), tr("Loco Configuration: how the value is typed (ipv4)")),
            spec("ui-default", Kind::Text, tr("Config default"), tr("Loco Configuration: a new configuration's value")),
            spec("c-name", Kind::Text, tr("C name"), tr("The member's name in the C struct, when it differs")),
        };
    }
    if (tag == QLatin1String("pad")) {
        return { spec("bits", Kind::Int, tr("Bits"), tr("Read and discarded, no row shown"), 1, 4096),
                 spec("fill", Kind::Text, tr("Fill"), tr("What the encoder writes there (a number, or frame_len)")) };
    }
    if (tag == QLatin1String("repeat")) {
        return { name,
                 spec("count", Kind::Count, tr("Count"), tr("A number, or the name of an earlier field holding it")),
                 spec("cmin", Kind::Int, tr("Count min"), tr("Packet Maker: the smallest count offered")),
                 spec("cmax", Kind::Int, tr("Count max"), tr("Packet Maker: the largest count offered")),
                 spec("index0", Kind::Bool, tr("Count from 0"), tr("Entries are name[0], name[1], ... instead of from 1")),
                 spec("labels", Kind::Text, tr("Entry names"), tr("Space-separated names for the entries, in order")),
                 spec("sep", Kind::Text, tr("Separator"), tr("Between an entry's tokens (default two spaces)")),
                 choice("expand", tr("Expand"), tr("true = each entry's fields are rows of their own"),
                        { QString(), QStringLiteral("true") }) };
    }
    if (tag == QLatin1String("group")) {
        return { name, spec("sep", Kind::Text, tr("Separator"), tr("Between the fields' tokens on the one row")) };
    }
    if (tag == QLatin1String("when")) {
        return { spec("test", Kind::Condition, tr("Condition"), tr("What inside is read only when this holds")) };
    }
    if (tag == QLatin1String("flags")) {
        return { name,
                 spec("table", Kind::FlagTableRef, tr("Flag table"), tr("The <flagtable> naming each bit")),
                 spec("bits", Kind::Int, tr("Bits"), tr("Width on the wire"), 1, 4096),
                 choice("order", tr("Bit order"), tr("lsb = the table's first flag is the lowest bit"),
                        { QString(), QStringLiteral("lsb"), QStringLiteral("msb") }),
                 choice("expand", tr("Expand"), tr("all = one row per flag"), { QString(), QStringLiteral("all") }),
                 spec("pad", Kind::Int, tr("Pad bits"), tr("Unused bits after the flags"), 0, 4096) };
    }
    if (tag == QLatin1String("note")) {
        return { name, spec("text", Kind::Text, tr("Text"), tr("A label row; reads no bits")),
                 spec("c-member", Kind::Text, tr("C member"), tr("The C struct member this section is")) };
    }
    if (tag == QLatin1String("row")) {
        return { name, spec("tmpl", Kind::Text, tr("Template"), tr("A computed row: {name} or {name:02d} from earlier fields")) };
    }
    if (tag == QLatin1String("eventstream")) {
        return { name,
                 spec("count_bits", Kind::Int, tr("Count bits"), tr("Width of the record count"), 1, 64),
                 spec("id_bits", Kind::Int, tr("Id bits"), tr("Width of each record's id"), 1, 64),
                 spec("table", Kind::EventTableRef, tr("Event table"), tr("Each id's size, sign and name")),
                 spec("meaning", Kind::Text, tr("Meaning hook"), tr("A value-meaning hook registered in code")) };
    }
    if (tag == QLatin1String("subpackets")) {
        return { spec("type_bits", Kind::Int, tr("Type bits"), tr("Width of each sub-packet's type"), 1, 64),
                 spec("len_bits", Kind::Int, tr("Length bits"), tr("Width of each sub-packet's length"), 1, 64),
                 choice("len_unit", tr("Length unit"), tr("What the length counts"), { QString(), QStringLiteral("bytes"), QStringLiteral("bits") }),
                 spec("reserve_tail", Kind::Int, tr("Tail bytes"), tr("Bytes kept at the end (MAC_CODE ...)"), 0, 4096) };
    }
    if (tag == QLatin1String("case")) {
        return { spec("type", Kind::Int, tr("Type"), tr("The sub-packet type number"), 0, INT_MAX),
                 spec("struct", Kind::StructRef, tr("Struct"), tr("The <struct> that decodes it")) };
    }
    if (tag == QLatin1String("packet")) {
        return { name,
                 spec("match", Kind::Text, tr("Match"), tr("captype==token[,token]: which captures it decodes")),
                 choice("wire", tr("Bit order"), tr("msb-first (radio, Annexure C) or lsb-first (Annexure D, logs)"),
                        { QStringLiteral("msb-first"), QStringLiteral("lsb-first") }),
                 spec("crc", Kind::CrcRef, tr("CRC"), tr("The <crc> definition its trailer is checked with")),
                 spec("trailer_bytes", Kind::Int, tr("Trailer bytes"), tr("Bytes at the end that are not body (default 4)"), 0, 4096),
                 spec("body_offset", Kind::Int, tr("Body offset"), tr("Bit the body starts at (past a header)"), 0, INT_MAX) };
    }
    if (tag == QLatin1String("struct")) {
        return { spec("name", Kind::Text, tr("Name"), tr("Sub-packet cases refer to it")) };
    }
    if (tag == QLatin1String("enum")) {
        return { spec("name", Kind::Text, tr("Name"), tr("Fields refer to it with enum=")) };
    }
    if (tag == QLatin1String("map")) {
        return { spec("v", Kind::Int, tr("Value"), tr("The exact value this names")),
                 spec("from", Kind::Int, tr("From"), tr("A range: first value")),
                 spec("to", Kind::Int, tr("To"), tr("A range: last value")),
                 spec("label", Kind::Text, tr("Label"), tr("The text; %v = the value, %f = the formula's result")),
                 spec("formula", Kind::Text, tr("Formula"), tr("v*K, v+K or v-K")),
                 spec("unit", Kind::Text, tr("Unit"), tr("After the formula's result")),
                 spec("default", Kind::Bool, tr("Otherwise"), tr("Used for any value no other map names")) };
    }
    if (tag == QLatin1String("crc") && parentTag == QLatin1String("crcs")) {
        return { name,
                 choice("type", tr("Type"), tr("Width of the CRC"), { QStringLiteral("crc32"), QStringLiteral("crc30") }),
                 spec("poly", Kind::Text, tr("Polynomial"), tr("e.g. 0x04C11DB7")),
                 spec("init", Kind::Text, tr("Initial"), tr("e.g. 0xFFFFFFFF")),
                 spec("reflect_in", Kind::Bool, tr("Reflect in"), tr("Bytes are bit-reversed going in")),
                 spec("reflect_out", Kind::Bool, tr("Reflect out"), tr("The result is bit-reversed")),
                 spec("xor_out", Kind::Text, tr("XOR out"), tr("XORed into the result")),
                 spec("trailer_bytes", Kind::Int, tr("Trailer bytes"), tr("Size of the stored CRC"), 0, 64),
                 spec("trailer_bits", Kind::Int, tr("Trailer bits"), tr("Size of the stored CRC, in bits"), 0, 512),
                 choice("trailer_endian", tr("Trailer order"), tr("Byte order of the stored CRC"), { QString(), QStringLiteral("be"), QStringLiteral("le") }) };
    }
    if (tag == QLatin1String("crc")) {
        return { name,
                 spec("algo", Kind::Text, tr("Algorithm"), tr("A CRC registered in code (jamcrc, rfidcrc30)")),
                 spec("from", Kind::Int, tr("From byte"), tr("First byte covered"), 0, INT_MAX),
                 spec("len", Kind::Int, tr("Length"), tr("Bytes covered"), 0, INT_MAX),
                 spec("bits", Kind::Int, tr("Bits"), tr("Width of the stored CRC (default 32)"), 1, 64),
                 spec("at", Kind::Int, tr("At bit"), tr("Read the stored CRC here, not at the cursor"), 0, INT_MAX) };
    }
    if (tag == QLatin1String("eventtable") || tag == QLatin1String("flagtable")) {
        return { name };
    }
    if (tag == QLatin1String("event")) {
        return { spec("id", Kind::Int, tr("Id"), tr("The record id in the stream"), 0, INT_MAX),
                 spec("bytes", Kind::Int, tr("Bytes"), tr("Size of its value"), 1, 8),
                 spec("name", Kind::Text, tr("Name"), tr("The row's name")),
                 spec("signed", Kind::Bool, tr("Signed"), tr("Two's-complement signed value")) };
    }
    if (tag == QLatin1String("flag")) {
        return { spec("name", Kind::Text, tr("Name"), tr("The bit's name, in table order")) };
    }
    if (tag == QLatin1String("schema")) {
        return { spec("version", Kind::Int, tr("Version"), tr("The schema grammar version"), 1, 1000) };
    }
    return {};
}

const AttrSpec *specFor(const QVector<AttrSpec> &specs, const QString &name)
{
    for (const AttrSpec &s : specs) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

QString title(const QString &tag)
{
    static const QHash<QString, const char *> titles = {
        { QStringLiteral("field"), "Field" },         { QStringLiteral("pad"), "Pad" },
        { QStringLiteral("repeat"), "Repeat" },       { QStringLiteral("group"), "Group" },
        { QStringLiteral("when"), "Condition" },      { QStringLiteral("flags"), "Flags" },
        { QStringLiteral("note"), "Note" },           { QStringLiteral("row"), "Computed row" },
        { QStringLiteral("eventstream"), "Event stream" }, { QStringLiteral("subpackets"), "Sub-packets" },
        { QStringLiteral("case"), "Sub-packet case" }, { QStringLiteral("packet"), "Packet" },
        { QStringLiteral("struct"), "Struct" },       { QStringLiteral("enum"), "Enum" },
        { QStringLiteral("map"), "Value" },           { QStringLiteral("crc"), "CRC" },
        { QStringLiteral("eventtable"), "Event table" }, { QStringLiteral("event"), "Event" },
        { QStringLiteral("flagtable"), "Flag table" }, { QStringLiteral("flag"), "Flag" },
        { QStringLiteral("align"), "Align" },         { QStringLiteral("schema"), "Schema" },
    };
    const auto it = titles.constFind(tag);
    return it == titles.constEnd() ? tag : QObject::tr(it.value());
}

QString describe(const QString &tag, const QString &parentTag)
{
    if (tag == QLatin1String("field")) return tr("Reads a value from the frame and shows it as a row.");
    if (tag == QLatin1String("pad")) return tr("Bits that are read and discarded.");
    if (tag == QLatin1String("repeat")) return tr("Its fields, read Count times, one entry each.");
    if (tag == QLatin1String("group")) return tr("Its fields, read in turn and shown as one row.");
    if (tag == QLatin1String("when")) return tr("What is inside is read only when the condition holds.");
    if (tag == QLatin1String("flags")) return tr("A run of bits, each named by a flag table.");
    if (tag == QLatin1String("note")) return tr("A label row between sections; reads no bits.");
    if (tag == QLatin1String("row")) return tr("A row computed from earlier fields; reads no bits.");
    if (tag == QLatin1String("eventstream")) return tr("A count, then records of (id, value) sized by an event table.");
    if (tag == QLatin1String("subpackets")) return tr("Sub-packets of (type, length, body), each decoded by a struct.");
    if (tag == QLatin1String("case")) return tr("Which struct decodes one sub-packet type.");
    if (tag == QLatin1String("packet")) return tr("A whole frame: its fields in wire order, top to bottom.");
    if (tag == QLatin1String("struct")) return tr("A sub-packet body, decoded when a packet's case names it.");
    if (tag == QLatin1String("enum")) return tr("Names for a field's values.");
    if (tag == QLatin1String("map")) return tr("One value (or range) and its name.");
    if (tag == QLatin1String("crc") && parentTag == QLatin1String("crcs")) return tr("A CRC definition packets refer to.");
    if (tag == QLatin1String("crc")) return tr("A stored CRC inside the frame, checked and shown PASS / FAIL.");
    if (tag == QLatin1String("eventtable")) return tr("The size, sign and name of each event id.");
    if (tag == QLatin1String("event")) return tr("One event id.");
    if (tag == QLatin1String("flagtable")) return tr("Names for the bits of a <flags>, in order.");
    if (tag == QLatin1String("flag")) return tr("One bit's name.");
    if (tag == QLatin1String("align")) return tr("Skips to the next byte boundary.");
    return QString();
}

// -----------------------------------------------------------------------------
//  Sizes
// -----------------------------------------------------------------------------

namespace {

bool isWhole(const QString &s, qint64 *v = nullptr)
{
    bool ok = false;
    const qint64 n = s.trimmed().toLongLong(&ok);
    if (ok && v) *v = n;
    return ok;
}

int sumChildren(const SchemaDoc::Document &doc, int node)
{
    int total = 0;
    for (int c : doc.node(node).children) {
        const QString tag = doc.node(c).tag;
        if (tag == QLatin1String("align")) return -1;
        const int b = fixedBits(doc, c);
        if (b < 0) return -1;
        total += b;
    }
    return total;
}

}  // namespace

int fixedBits(const SchemaDoc::Document &doc, int node)
{
    const SchemaDoc::Node &n = doc.node(node);
    const QString &tag = n.tag;
    if (tag == QLatin1String("field")) {
        const QString t = n.value(QStringLiteral("type"));
        if (t == QLatin1String("float")) return 32;
        if (t == QLatin1String("double")) return 64;
        if (t == QLatin1String("char")) return 8 * n.value(QStringLiteral("count")).toInt();
        return n.value(QStringLiteral("bits")).toInt();
    }
    if (tag == QLatin1String("pad") || tag == QLatin1String("flags")) {
        return n.value(QStringLiteral("bits")).toInt() + n.value(QStringLiteral("pad")).toInt();
    }
    if (tag == QLatin1String("note") || tag == QLatin1String("row")) return 0;
    if (tag == QLatin1String("crc")) {
        if (n.has(QStringLiteral("at"))) return 0;
        return n.has(QStringLiteral("bits")) ? n.value(QStringLiteral("bits")).toInt() : 32;
    }
    if (tag == QLatin1String("group") || tag == QLatin1String("struct") || tag == QLatin1String("packet")) {
        return sumChildren(doc, node);
    }
    if (tag == QLatin1String("repeat")) {
        qint64 count = 0;
        if (!isWhole(n.value(QStringLiteral("count")), &count)) return -1;
        const int entry = sumChildren(doc, node);
        return entry < 0 ? -1 : int(count * entry);
    }
    return -1;   // when, align, eventstream, subpackets, ...
}

QVector<int> childOffsets(const SchemaDoc::Document &doc, int container)
{
    QVector<int> out;
    int at = 0;
    for (int c : doc.node(container).children) {
        out.push_back(at);
        if (at < 0) continue;
        if (doc.node(c).tag == QLatin1String("align")) {
            at = (at + 7) / 8 * 8;
            continue;
        }
        const int b = fixedBits(doc, c);
        at = b < 0 ? -1 : at + b;
    }
    return out;
}

// -----------------------------------------------------------------------------
//  Renames
// -----------------------------------------------------------------------------

namespace {

// Where a node is, for the "also used in" list: "SLRP: <field name="X">".
QString where(const SchemaDoc::Document &doc, int node)
{
    const int c = doc.container(node);
    const QString owner = c >= 0 ? doc.node(c).value(QStringLiteral("name")) : QString();
    const SchemaDoc::Node &n = doc.node(node);
    const QString nm = n.value(QStringLiteral("name"));
    const QString self = nm.isEmpty() ? QStringLiteral("<%1>").arg(n.tag) : QStringLiteral("<%1 %2>").arg(n.tag, nm);
    return owner.isEmpty() || c == node ? self : QStringLiteral("%1: %2").arg(owner, self);
}

bool inside(const SchemaDoc::Document &doc, int node, int ancestor)
{
    for (int k = node; k >= 0; k = doc.node(k).parent) {
        if (k == ancestor) return true;
    }
    return false;
}

// The condition with its tested name replaced, or empty if it does not test `from`.
QString renamedCondition(const QString &cond, const QString &from, const QString &to)
{
    QString name;
    if (!Schema::Decoder::checkCondition(cond, &name).isEmpty() || name != from) return QString();
    const int at = cond.indexOf(from);
    if (at < 0) return QString();
    return cond.left(at) + to + cond.mid(at + from.size());
}

QString renamedTemplate(const QString &tmpl, const QString &from, const QString &to)
{
    const QRegularExpression re(QStringLiteral("\\{%1([:}])").arg(QRegularExpression::escape(from)));
    if (!re.match(tmpl).hasMatch()) return QString();
    QString out = tmpl;
    out.replace(re, QStringLiteral("{%1\\1").arg(to));
    return out;
}

}  // namespace

RenamePlan planRename(const SchemaDoc::Document &doc, int node, const QString &newName)
{
    RenamePlan plan;
    const SchemaDoc::Node &n = doc.node(node);
    const QString old = n.value(QStringLiteral("name"));
    if (old.isEmpty() || old == newName) return plan;
    const QString parentTag = n.parent >= 0 ? doc.node(n.parent).tag : QString();

    auto pointers = [&](const QString &tag, const QString &attr) {
        for (int i = 0; i < doc.count(); ++i) {
            const SchemaDoc::Node &k = doc.node(i);
            if ((tag.isEmpty() || k.tag == tag) && k.value(attr) == old && k.has(attr)) {
                plan.changes.push_back({ i, attr, old, newName });
            }
        }
    };
    if (n.tag == QLatin1String("enum")) {
        pointers(QStringLiteral("field"), QStringLiteral("enum"));
        return plan;
    }
    if (n.tag == QLatin1String("struct")) {
        pointers(QString(), QStringLiteral("struct"));
        return plan;
    }
    if (n.tag == QLatin1String("flagtable")) {
        pointers(QStringLiteral("flags"), QStringLiteral("table"));
        return plan;
    }
    if (n.tag == QLatin1String("eventtable")) {
        pointers(QStringLiteral("eventstream"), QStringLiteral("table"));
        return plan;
    }
    if (n.tag == QLatin1String("crc") && parentTag == QLatin1String("crcs")) {
        pointers(QStringLiteral("packet"), QStringLiteral("crc"));
        return plan;
    }
    if (n.tag == QLatin1String("packet")) {
        plan.warning = QObject::tr("DLConsole's own views look some packets up by name (LINFO, DMI, RFID, ...): "
                                   "a renamed packet may stop showing there.");
        return plan;
    }
    // A field, repeat, group, flags, ...: the name conditions and counts use.
    const int owner = doc.container(node);
    for (int i = 0; i < doc.count(); ++i) {
        const SchemaDoc::Node &k = doc.node(i);
        const bool same = owner >= 0 && inside(doc, i, owner);
        for (const char *a : { "when", "test" }) {
            const QString attr = QString::fromLatin1(a);
            if (!k.has(attr)) continue;
            const QString to = renamedCondition(k.value(attr), old, newName);
            if (to.isEmpty()) continue;
            if (same) plan.changes.push_back({ i, attr, k.value(attr), to });
            else plan.elsewhere << QStringLiteral("%1 %2=\"%3\"").arg(where(doc, i), attr, k.value(attr));
        }
        if (k.tag == QLatin1String("repeat") && k.value(QStringLiteral("count")) == old) {
            if (same) plan.changes.push_back({ i, QStringLiteral("count"), old, newName });
            else plan.elsewhere << QStringLiteral("%1 count=\"%2\"").arg(where(doc, i), old);
        }
        if (k.tag == QLatin1String("row") && k.has(QStringLiteral("tmpl"))) {
            const QString to = renamedTemplate(k.value(QStringLiteral("tmpl")), old, newName);
            if (to.isEmpty()) continue;
            if (same) plan.changes.push_back({ i, QStringLiteral("tmpl"), k.value(QStringLiteral("tmpl")), to });
            else plan.elsewhere << QStringLiteral("%1 tmpl=\"%2\"").arg(where(doc, i), k.value(QStringLiteral("tmpl")));
        }
    }
    return plan;
}

// -----------------------------------------------------------------------------
//  Values
// -----------------------------------------------------------------------------

namespace {

QSet<QString> namesOf(const SchemaDoc::Document &doc, const QString &tag, const QString &parentTag = QString())
{
    QSet<QString> out;
    for (int i : doc.all(tag)) {
        const SchemaDoc::Node &n = doc.node(i);
        if (!parentTag.isEmpty() && (n.parent < 0 || doc.node(n.parent).tag != parentTag)) continue;
        out.insert(n.value(QStringLiteral("name")));
    }
    return out;
}

}  // namespace

QString valueProblem(const SchemaDoc::Document &doc, const AttrSpec &spec, const QString &value)
{
    if (value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r'))) {
        return QObject::tr("%1 is one line").arg(spec.label);
    }
    switch (spec.kind) {
    case Kind::Text:
        return QString();
    case Kind::Int: {
        qint64 v = 0;
        if (!isWhole(value, &v)) return QObject::tr("%1 is a whole number").arg(spec.label);
        if (v < spec.min || v > spec.max) {
            return QObject::tr("%1 is %2 to %3").arg(spec.label).arg(spec.min).arg(spec.max);
        }
        return QString();
    }
    case Kind::Bool:
        return value == QLatin1String("true") || value == QLatin1String("false")
                   ? QString() : QObject::tr("%1 is true or false").arg(spec.label);
    case Kind::Choice:
        return spec.choices.contains(value) ? QString()
                                            : QObject::tr("%1 is one of: %2").arg(spec.label, spec.choices.join(QStringLiteral(", ")));
    case Kind::Condition: {
        const QString why = Schema::Decoder::checkCondition(value);
        return why.isEmpty() ? QString() : QObject::tr("%1: %2").arg(spec.label, why);
    }
    case Kind::Count:
        if (isWhole(value)) return QString();
        return value.trimmed().isEmpty() || value.contains(QLatin1Char(' '))
                   ? QObject::tr("%1 is a number or a field's name").arg(spec.label) : QString();
    case Kind::EnumRef:
        return namesOf(doc, QStringLiteral("enum")).contains(value) ? QString()
                                                                    : QObject::tr("There is no enum %1").arg(value);
    case Kind::StructRef:
        return namesOf(doc, QStringLiteral("struct")).contains(value) ? QString()
                                                                      : QObject::tr("There is no struct %1").arg(value);
    case Kind::FlagTableRef:
        return namesOf(doc, QStringLiteral("flagtable")).contains(value) ? QString()
                                                                         : QObject::tr("There is no flag table %1").arg(value);
    case Kind::EventTableRef:
        return namesOf(doc, QStringLiteral("eventtable")).contains(value) ? QString()
                                                                          : QObject::tr("There is no event table %1").arg(value);
    case Kind::CrcRef:
        return namesOf(doc, QStringLiteral("crc"), QStringLiteral("crcs")).contains(value)
                   ? QString() : QObject::tr("There is no CRC definition %1").arg(value);
    }
    return QString();
}

bool isRequired(const SchemaDoc::Document &doc, int node, const QString &attr)
{
    const SchemaDoc::Node &n = doc.node(node);
    const QString &t = n.tag;
    if (attr == QLatin1String("name")) {
        return t != QLatin1String("pad") && t != QLatin1String("when") && t != QLatin1String("map")
               && t != QLatin1String("subpackets") && t != QLatin1String("case") && t != QLatin1String("schema");
    }
    if (t == QLatin1String("field") && attr == QLatin1String("bits")) return !n.has(QStringLiteral("type"));
    if (t == QLatin1String("field") && attr == QLatin1String("count")) return n.value(QStringLiteral("type")) == QLatin1String("char");
    if (t == QLatin1String("pad")) return attr == QLatin1String("bits");
    if (t == QLatin1String("case")) return attr == QLatin1String("type") || attr == QLatin1String("struct");
    if (t == QLatin1String("packet")) return attr == QLatin1String("match") || attr == QLatin1String("wire");
    if (t == QLatin1String("repeat")) return attr == QLatin1String("count");
    if (t == QLatin1String("when")) return attr == QLatin1String("test");
    if (t == QLatin1String("flags")) return attr == QLatin1String("table") || attr == QLatin1String("bits");
    if (t == QLatin1String("event")) return attr == QLatin1String("id") || attr == QLatin1String("bytes");
    if (t == QLatin1String("eventstream")) return attr == QLatin1String("table");
    if (t == QLatin1String("schema")) return attr == QLatin1String("version");
    return false;
}

QString loadProblem(const QByteArray &text)
{
    Schema::Decoder probe;
    QString why;
    return probe.loadData(text, &why) ? QString() : why;
}

// -----------------------------------------------------------------------------
//  Session
// -----------------------------------------------------------------------------

bool Session::open(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QObject::tr("Cannot open %1: %2").arg(path, f.errorString());
        return false;
    }
    return openText(f.readAll(), path, error);
}

bool Session::openText(const QByteArray &text, const QString &path, QString *error)
{
    SchemaDoc::Document doc;
    QString why;
    if (!doc.parse(text, &why)) {
        if (error) *error = QObject::tr("%1 is not readable XML: %2").arg(path, why);
        return false;
    }
    m_path = path;
    m_original = text;
    m_doc = doc;
    m_done.clear();
    m_undone.clear();
    return true;
}

QStringList Session::changes() const
{
    QStringList out;
    for (const Step &s : m_done) out << s.what;
    return out;
}

QString Session::undoText() const { return m_done.isEmpty() ? QString() : m_done.last().what; }
QString Session::redoText() const { return m_undone.isEmpty() ? QString() : m_undone.last().what; }

QString Session::check(const QByteArray &text, SchemaDoc::Document *parsed) const
{
    QString err;
    if (!parsed->parse(text, &err)) return QObject::tr("The edit would break the XML (%1); not made").arg(err);
    err = loadProblem(text);
    if (!err.isEmpty()) return QObject::tr("The decoder would refuse the schema: %1. Not made.").arg(err);
    // The decoder checks conditions, not these: a count, enum, struct, table
    // or CRC naming nothing would decode silently wrong.
    const QStringList before = dangling(m_doc);
    QStringList fresh;
    for (const QString &d : dangling(*parsed)) {
        if (!before.contains(d)) fresh << d;
    }
    if (!fresh.isEmpty()) {
        return QObject::tr("It would leave %1 naming nothing. Not made.").arg(fresh.mid(0, 3).join(QStringLiteral("; ")));
    }
    const QStringList had = clashes(m_doc);
    for (const QString &c : clashes(*parsed)) {
        if (!had.contains(c)) return QObject::tr("It would make %1. Not made.").arg(c);
    }
    return QString();
}

bool Session::stage(const QByteArray &text, const QString &what, QString *why, const QString &key)
{
    if (readOnly()) {
        if (why) *why = QObject::tr("The built-in kavach.xml cannot be changed: make an editable copy first");
        return false;
    }
    if (text.isEmpty()) return false;   // the caller's edit failed, *why already says why
    if (text == m_doc.text()) {
        if (why) *why = QObject::tr("No change");
        return false;
    }
    SchemaDoc::Document next;
    const QString err = check(text, &next);
    if (!err.isEmpty()) {
        if (why) *why = err;
        return false;
    }
    const bool merge = !key.isEmpty() && !m_done.isEmpty() && m_done.last().key == key && m_undone.isEmpty();
    if (merge && m_done.last().text == text) {
        m_done.removeLast();             // back where the run started
    } else if (merge) {
        m_done.last().what = what;
    } else {
        Step step;
        step.text = m_doc.text();
        step.what = what;
        step.key = key;
        m_done.push_back(step);
    }
    m_undone.clear();
    m_doc = next;
    return true;
}

bool Session::undo()
{
    if (m_done.isEmpty()) return false;
    const Step s = m_done.takeLast();
    Step back;
    back.text = m_doc.text();
    back.what = s.what;
    m_undone.push_back(back);
    m_doc.parse(s.text);
    return true;
}

bool Session::redo()
{
    if (m_undone.isEmpty()) return false;
    const Step s = m_undone.takeLast();
    Step again;
    again.text = m_doc.text();
    again.what = s.what;
    m_done.push_back(again);
    m_doc.parse(s.text);
    return true;
}

void Session::discard()
{
    m_done.clear();
    m_undone.clear();
    m_doc.parse(m_original);
}

static QString label(const SchemaDoc::Document &doc, int node)
{
    const SchemaDoc::Node &n = doc.node(node);
    const QString nm = n.value(QStringLiteral("name"));
    const int c = doc.container(node);
    QString self = nm.isEmpty() ? title(n.tag) : nm;
    if (c >= 0 && c != node) self = doc.node(c).value(QStringLiteral("name")) + QStringLiteral(" › ") + self;
    return self;
}

bool Session::setAttribute(int node, const QString &name, const QString &value, QString *why)
{
    if (node < 0 || node >= m_doc.count()) return false;
    const SchemaDoc::Node &n = m_doc.node(node);
    const QString parentTag = n.parent >= 0 ? m_doc.node(n.parent).tag : QString();
    const QVector<AttrSpec> specs = specsFor(n.tag, parentTag);
    if (const AttrSpec *s = specFor(specs, name)) {
        const QString p = valueProblem(m_doc, *s, value);
        if (!p.isEmpty()) {
            if (why) *why = p;
            return false;
        }
    } else if (value.contains(QLatin1Char('\n'))) {
        if (why) *why = QObject::tr("A value is one line");
        return false;
    }
    if (name == QLatin1String("name") && n.has(name)) {
        return rename(node, value, false, why);
    }
    const QString key = QStringLiteral("%1:%2").arg(node).arg(name);
    const bool merge = !m_done.isEmpty() && m_done.last().key == key && m_undone.isEmpty();
    const bool had = merge ? m_done.last().had : n.has(name);
    const QString before = merge ? m_done.last().before : n.value(name);
    const QString what = had
        ? QObject::tr("%1: %2 %3 → %4").arg(label(m_doc, node), name, before.isEmpty() ? QStringLiteral("\"\"") : before, value)
        : QObject::tr("%1: + %2=\"%3\"").arg(label(m_doc, node), name, value);
    if (!stage(m_doc.withAttribute(node, name, value), what, why, key)) return false;
    if (!merge && !m_done.isEmpty() && m_done.last().key == key) {
        m_done.last().had = had;
        m_done.last().before = before;
    }
    return true;
}

bool Session::removeAttribute(int node, const QString &name, QString *why)
{
    if (node < 0 || node >= m_doc.count()) return false;
    const SchemaDoc::Node &n = m_doc.node(node);
    if (!n.has(name)) {
        if (why) *why = QObject::tr("It has no %1").arg(name);
        return false;
    }
    if (name == QLatin1String("name")) {
        if (why) *why = QObject::tr("The name stays; rename it instead");
        return false;
    }
    return stage(m_doc.withoutAttribute(node, name),
                 QObject::tr("%1: − %2=\"%3\"").arg(label(m_doc, node), name, n.value(name)), why);
}

bool Session::rename(int node, const QString &newName, bool withReferences, QString *why)
{
    if (node < 0 || node >= m_doc.count()) return false;
    const SchemaDoc::Node &n = m_doc.node(node);
    const QString old = n.value(QStringLiteral("name"));
    if (newName.trimmed().isEmpty()) {
        if (why) *why = QObject::tr("The name cannot be empty");
        return false;
    }
    if (newName != newName.trimmed()) {
        if (why) *why = QObject::tr("The name has spaces at its ends");
        return false;
    }
    // Names that are looked up must stay unique among their kind.
    static const QSet<QString> unique = { QStringLiteral("packet"), QStringLiteral("struct"), QStringLiteral("enum"),
                                          QStringLiteral("flagtable"), QStringLiteral("eventtable") };
    const QString parentTag = n.parent >= 0 ? m_doc.node(n.parent).tag : QString();
    const bool crcDef = n.tag == QLatin1String("crc") && parentTag == QLatin1String("crcs");
    if (unique.contains(n.tag) || crcDef) {
        for (int i : m_doc.all(n.tag)) {
            if (i != node && m_doc.node(i).value(QStringLiteral("name")) == newName
                && (!crcDef || m_doc.node(m_doc.node(i).parent).tag == QLatin1String("crcs"))) {
                if (why) *why = QObject::tr("There is already a %1 named %2").arg(title(n.tag).toLower(), newName);
                return false;
            }
        }
    }
    // Apply the references back to front so earlier offsets stay good: each
    // edit is made on a fresh parse of the text so far.
    SchemaDoc::Document work = m_doc;
    QVector<Reference> refs;
    if (withReferences) refs = planRename(m_doc, node, newName).changes;
    refs.push_back({ node, QStringLiteral("name"), old, newName });
    std::sort(refs.begin(), refs.end(), [&](const Reference &a, const Reference &b) {
        return m_doc.node(a.node).start > m_doc.node(b.node).start;
    });
    for (const Reference &r : refs) {
        const QByteArray t = work.withAttribute(r.node, r.attr, r.after);
        work.parse(t);
    }
    QString what = QObject::tr("%1 renamed %2").arg(label(m_doc, node), newName);
    if (refs.size() > 1) what += QObject::tr(" (and %n reference(s))", nullptr, int(refs.size()) - 1);
    return stage(work.text(), what, why);
}

// -----------------------------------------------------------------------------
//  Structure
// -----------------------------------------------------------------------------

QString elementLine(const NewElement &element)
{
    QString out = QLatin1Char('<') + element.tag;
    for (const auto &a : element.attrs) {
        out += QStringLiteral(" %1=\"%2\"").arg(a.first, SchemaDoc::escape(a.second, '"'));
    }
    return out + QStringLiteral("/>");
}

bool allowedIn(const QString &tag, const QString &parentTag)
{
    static const QSet<QString> body = { QStringLiteral("packet"), QStringLiteral("struct"), QStringLiteral("when"),
                                        QStringLiteral("repeat"), QStringLiteral("group") };
    if (tag == QLatin1String("field") || tag == QLatin1String("pad")) return body.contains(parentTag);
    if (tag == QLatin1String("note")) {
        return parentTag == QLatin1String("packet") || parentTag == QLatin1String("struct") || parentTag == QLatin1String("when");
    }
    if (tag == QLatin1String("case")) return parentTag == QLatin1String("subpackets");
    if (tag == QLatin1String("map")) return parentTag == QLatin1String("enum");
    if (tag == QLatin1String("flag")) return parentTag == QLatin1String("flagtable");
    if (tag == QLatin1String("event")) return parentTag == QLatin1String("eventtable");
    return false;
}

int parentFor(const SchemaDoc::Document &doc, int anchor, SchemaDoc::Document::Place place)
{
    if (anchor < 0 || anchor >= doc.count()) return -1;
    return place == SchemaDoc::Document::Place::Before || place == SchemaDoc::Document::Place::After
               ? doc.node(anchor).parent : anchor;
}

QString memberProblem(const SchemaDoc::Document &doc, int node)
{
    if (node <= 0 || node >= doc.count()) return QObject::tr("Select an element");
    const int c = doc.container(node);
    if (c < 0 || (c == node && !isContainer(doc, node))) {
        return QObject::tr("<%1> is a section of the file; it stays").arg(doc.node(node).tag);
    }
    return QString();
}

bool isContainer(const SchemaDoc::Document &doc, int node)
{
    if (node <= 0 || node >= doc.count() || doc.container(node) != node) return false;
    return containerTags().contains(doc.node(node).tag);
}

QStringList containerTags()
{
    return { QStringLiteral("packet"), QStringLiteral("struct"), QStringLiteral("enum"), QStringLiteral("flagtable"),
             QStringLiteral("eventtable"), QStringLiteral("crc") };
}

QStringList packetsWithSubpackets(const SchemaDoc::Document &doc)
{
    QStringList out;
    for (int p : doc.all(QStringLiteral("packet"))) {
        if (doc.child(p, QStringLiteral("subpackets")) >= 0) out << doc.node(p).value(QStringLiteral("name"));
    }
    return out;
}

int freeCaseType(const SchemaDoc::Document &doc, const QString &packet)
{
    for (int p : doc.all(QStringLiteral("packet"))) {
        if (doc.node(p).value(QStringLiteral("name")) != packet) continue;
        const int sp = doc.child(p, QStringLiteral("subpackets"));
        if (sp < 0) return -1;
        QSet<int> used;
        for (int c : doc.node(sp).children) used.insert(doc.node(c).value(QStringLiteral("type")).toInt());
        int t = 0;
        while (used.contains(t)) ++t;
        return t;
    }
    return -1;
}

QStringList caseUses(const SchemaDoc::Document &doc, const QString &structName)
{
    QStringList out;
    for (int c : doc.all(QStringLiteral("case"))) {
        if (doc.node(c).value(QStringLiteral("struct")) != structName) continue;
        out << QObject::tr("%1 type %2").arg(doc.node(doc.container(c)).value(QStringLiteral("name")),
                                            doc.node(c).value(QStringLiteral("type")));
    }
    return out;
}

QStringList clashes(const SchemaDoc::Document &doc)
{
    QStringList out;
    QHash<QString, QString> token;   // captype -> packet
    QHash<QString, int> seen;
    for (int i = 0; i < doc.count(); ++i) {
        const SchemaDoc::Node &n = doc.node(i);
        if (isContainer(doc, i)) {
            const QString key = n.tag + QLatin1Char(' ') + n.value(QStringLiteral("name"));
            if (seen[key]++ == 1) out << QObject::tr("two %1s named %2").arg(title(n.tag).toLower(), n.value(QStringLiteral("name")));
        }
        if (n.tag == QLatin1String("packet")) {
            for (const QString &t : Schema::captypeTokens(n.value(QStringLiteral("match")))) {
                if (token.contains(t)) {
                    out << QObject::tr("captype %1 answered by %2 and %3 (only the first decodes)").arg(t, token.value(t), n.value(QStringLiteral("name")));
                } else {
                    token.insert(t, n.value(QStringLiteral("name")));
                }
            }
        }
        if (n.tag == QLatin1String("subpackets")) {
            QSet<QString> types;
            for (int c : n.children) {
                const QString t = doc.node(c).value(QStringLiteral("type"));
                if (types.contains(t)) {
                    out << QObject::tr("%1: two sub-packet cases of type %2").arg(doc.node(doc.container(i)).value(QStringLiteral("name")), t);
                }
                types.insert(t);
            }
        }
    }
    return out;
}

QStringList dangling(const SchemaDoc::Document &doc)
{
    QSet<QString> declared;   // what conditions and counts may name
    QHash<QString, QSet<QString>> defs;   // tag -> names
    for (int i = 0; i < doc.count(); ++i) {
        const SchemaDoc::Node &n = doc.node(i);
        const int c = doc.container(i);
        const QString ct = c >= 0 ? doc.node(c).tag : QString();
        if ((ct == QLatin1String("packet") || ct == QLatin1String("struct")) && c != i) {
            if (n.has(QStringLiteral("name"))) declared.insert(n.value(QStringLiteral("name")));
            if (n.has(QStringLiteral("id"))) declared.insert(n.value(QStringLiteral("id")));
        }
        if (n.parent >= 0 && c == i) {
            const QString key = n.tag == QLatin1String("crc") ? QStringLiteral("crcdef") : n.tag;
            defs[key].insert(n.value(QStringLiteral("name")));
        }
    }
    QStringList out;
    for (int i = 0; i < doc.count(); ++i) {
        const SchemaDoc::Node &n = doc.node(i);
        auto bad = [&](const QString &attr, const QString &what) {
            out << QObject::tr("%1 %2=\"%3\" (no such %4)").arg(
                       doc.container(i) >= 0 ? doc.node(doc.container(i)).value(QStringLiteral("name")) + QStringLiteral(": <") + n.tag + QLatin1Char('>')
                                            : QLatin1Char('<') + n.tag + QLatin1Char('>'),
                       attr, n.value(attr), what);
        };
        if (n.tag == QLatin1String("repeat") && n.has(QStringLiteral("count"))) {
            bool num = false;
            n.value(QStringLiteral("count")).toLongLong(&num);
            if (!num && !declared.contains(n.value(QStringLiteral("count")))) bad(QStringLiteral("count"), QObject::tr("field"));
        }
        if (n.tag == QLatin1String("field") && n.has(QStringLiteral("enum")) && !defs.value(QStringLiteral("enum")).contains(n.value(QStringLiteral("enum")))) {
            bad(QStringLiteral("enum"), QObject::tr("enum"));
        }
        if (n.tag == QLatin1String("case") && !defs.value(QStringLiteral("struct")).contains(n.value(QStringLiteral("struct")))) {
            bad(QStringLiteral("struct"), QObject::tr("struct"));
        }
        if (n.tag == QLatin1String("flags") && !defs.value(QStringLiteral("flagtable")).contains(n.value(QStringLiteral("table")))) {
            bad(QStringLiteral("table"), QObject::tr("flag table"));
        }
        if (n.tag == QLatin1String("eventstream") && !defs.value(QStringLiteral("eventtable")).contains(n.value(QStringLiteral("table")))) {
            bad(QStringLiteral("table"), QObject::tr("event table"));
        }
        if (n.tag == QLatin1String("packet") && n.has(QStringLiteral("crc")) && !defs.value(QStringLiteral("crcdef")).contains(n.value(QStringLiteral("crc")))) {
            bad(QStringLiteral("crc"), QObject::tr("CRC definition"));
        }
    }
    return out;
}

bool Session::previewInsert(int anchor, SchemaDoc::Document::Place place, const NewElement &element,
                            QByteArray *text, int *node, QString *why) const
{
    using Place = SchemaDoc::Document::Place;
    const int parent = parentFor(m_doc, anchor, place);
    if (parent < 0) {
        if (why) *why = QObject::tr("Choose where it goes");
        return false;
    }
    const QString parentTag = m_doc.node(parent).tag;
    if (!allowedIn(element.tag, parentTag)) {
        if (why) *why = QObject::tr("A %1 does not go inside a %2").arg(title(element.tag).toLower(), title(parentTag).toLower());
        return false;
    }
    const QVector<AttrSpec> specs = specsFor(element.tag, parentTag);
    for (const auto &a : element.attrs) {
        if (const AttrSpec *sp = specFor(specs, a.first)) {
            const QString p = valueProblem(m_doc, *sp, a.second);
            if (!p.isEmpty()) {
                if (why) *why = p;
                return false;
            }
        }
    }
    // A field needs a name and a note its text; the rest is isRequired()'s, below.
    if (element.tag == QLatin1String("field") || element.tag == QLatin1String("note")) {
        const QString key = element.tag == QLatin1String("note") ? QStringLiteral("text") : QStringLiteral("name");
        bool given = false;
        for (const auto &a : element.attrs) given = given || (a.first == key && !a.second.trimmed().isEmpty());
        if (!given) {
            if (why) *why = element.tag == QLatin1String("note") ? QObject::tr("Give the note a text") : QObject::tr("Give it a name");
            return false;
        }
    }
    // Line it up with its neighbour: the anchor, or the nearest child.
    int columns = -1;
    if (place == Place::Before || place == Place::After) columns = anchor;
    else if (!m_doc.node(anchor).children.isEmpty()) {
        columns = place == Place::FirstChild ? m_doc.node(anchor).children.first() : m_doc.node(anchor).children.last();
    }
    int at = -1;
    QString err;
    const QByteArray t = m_doc.withInserted(anchor, place, elementLine(element), columns, &at, &err);
    if (t.isEmpty()) {
        if (why) *why = err;
        return false;
    }
    SchemaDoc::Document next;
    err = check(t, &next);
    if (!err.isEmpty()) {
        if (why) *why = err;
        return false;
    }
    const int added = next.nodeAt(at);
    for (const AttrSpec &sp : specsFor(element.tag, parentTag)) {
        if (added >= 0 && isRequired(next, added, sp.name) && !next.node(added).has(sp.name)) {
            if (why) *why = QObject::tr("It needs %1").arg(sp.label);
            return false;
        }
    }
    if (text) *text = t;
    if (node) *node = added;
    return true;
}

bool Session::insert(int anchor, SchemaDoc::Document::Place place, const NewElement &element, int *node, QString *why)
{
    if (readOnly()) {
        if (why) *why = QObject::tr("The built-in kavach.xml cannot be changed: make an editable copy first");
        return false;
    }
    QByteArray t;
    int added = -1;
    if (!previewInsert(anchor, place, element, &t, &added, why)) return false;
    const int parent = parentFor(m_doc, anchor, place);
    QString name;
    for (const auto &a : element.attrs) {
        if (a.first == QLatin1String("name") || (name.isEmpty() && a.first == QLatin1String("text"))) name = a.second;
    }
    if (element.tag == QLatin1String("pad")) {
        for (const auto &a : element.attrs) if (a.first == QLatin1String("bits")) name = QObject::tr("%1 bits").arg(a.second);
    }
    const QString what = QObject::tr("%1: + %2 %3").arg(label(m_doc, parent), title(element.tag).toLower(), name);
    if (!stage(t, what, why)) return false;
    if (node) *node = added;
    return true;
}

bool Session::remove(int node, QString *why)
{
    const QString p = memberProblem(m_doc, node);
    if (!p.isEmpty()) {
        if (why) *why = p;
        return false;
    }
    QString err;
    // A container goes with the comment that describes it.
    const QByteArray t = isContainer(m_doc, node) ? m_doc.withoutBlock(node, &err) : m_doc.withoutNode(node, &err);
    if (t.isEmpty()) {
        if (why) *why = err;
        return false;
    }
    const int kids = [&]() {
        int k = 0;
        for (int i = node + 1; i < m_doc.count() && m_doc.node(i).start < m_doc.node(node).end; ++i) ++k;
        return k;
    }();
    QString what = QObject::tr("%1 deleted").arg(label(m_doc, node));
    if (kids > 0) what += QObject::tr(" (with the %n element(s) inside it)", nullptr, kids);
    return stage(t, what, why);
}

bool Session::move(int node, int delta, int *moved, QString *why)
{
    const QString p = memberProblem(m_doc, node);
    if (!p.isEmpty()) {
        if (why) *why = p;
        return false;
    }
    int at = -1;
    QString err;
    QByteArray t;
    if (isContainer(m_doc, node)) {
        // Past the next one of its kind (packets sit on both sides of the
        // structs), its comment with it.
        const QVector<int> &sib = m_doc.node(m_doc.node(node).parent).children;
        int target = -1;
        for (int k = sib.indexOf(node) + delta; k >= 0 && k < sib.size(); k += delta) {
            if (m_doc.node(sib.at(k)).tag == m_doc.node(node).tag) {
                target = sib.at(k);
                break;
            }
        }
        if (target < 0) {
            if (why) *why = delta < 0 ? QObject::tr("It is already the first %1").arg(title(m_doc.node(node).tag).toLower())
                                      : QObject::tr("It is already the last %1").arg(title(m_doc.node(node).tag).toLower());
            return false;
        }
        t = m_doc.withMovedBlock(node, target, delta > 0, &at, &err);
    } else {
        t = m_doc.withMoved(node, delta, &at, &err);
    }
    if (t.isEmpty()) {
        if (why) *why = err;
        return false;
    }
    const QString what = delta < 0 ? QObject::tr("%1 moved up").arg(label(m_doc, node))
                                   : QObject::tr("%1 moved down").arg(label(m_doc, node));
    if (!stage(t, what, why)) return false;
    if (moved) *moved = m_doc.nodeAt(at);
    return true;
}

bool Session::duplicate(int node, int *copy, QString *why)
{
    const QString p = memberProblem(m_doc, node);
    if (!p.isEmpty()) {
        if (why) *why = p;
        return false;
    }
    int at = -1;
    QString err;
    QByteArray t = m_doc.withDuplicated(node, &at, &err);
    if (t.isEmpty()) {
        if (why) *why = err;
        return false;
    }
    // A named copy gets a name of its own: X_2, X_3, ...
    const SchemaDoc::Node &n = m_doc.node(node);
    if (n.has(QStringLiteral("name"))) {
        SchemaDoc::Document d;
        d.parse(t);
        const int c = d.nodeAt(at);
        const bool whole = isContainer(m_doc, node);
        const int owner = d.container(c);
        QSet<QString> taken;
        for (int i = 0; i < d.count(); ++i) {
            if (whole ? (isContainer(d, i) && d.node(i).tag == n.tag) : d.container(i) == owner) {
                taken.insert(d.node(i).value(QStringLiteral("name")));
            }
        }
        const QString base = n.value(QStringLiteral("name"));
        int k = 2;
        while (taken.contains(QStringLiteral("%1_%2").arg(base).arg(k))) ++k;
        const QString copyName = QStringLiteral("%1_%2").arg(base).arg(k);
        t = d.withAttribute(c, QStringLiteral("name"), copyName);
        // A copied packet answers a captype of its own until it is given one:
        // two packets on one captype, and only the first would ever decode.
        if (n.tag == QLatin1String("packet") && whole) {
            d.parse(t);
            t = d.withAttribute(c, QStringLiteral("match"), QStringLiteral("captype==") + copyName.toLower());
        }
    }
    if (!stage(t, QObject::tr("%1 duplicated").arg(label(m_doc, node)), why)) return false;
    if (copy) *copy = m_doc.nodeAt(at);
    return true;
}

namespace {

QString startTagOf(const QString &tag, const QString &name, const QVector<QPair<QString, QString>> &attrs, bool empty)
{
    QString out = QStringLiteral("<%1 name=\"%2\"").arg(tag, SchemaDoc::escape(name, '"'));
    for (const auto &a : attrs) out += QStringLiteral(" %1=\"%2\"").arg(a.first, SchemaDoc::escape(a.second, '"'));
    return out + (empty ? QStringLiteral("/>") : QStringLiteral(">"));
}

}  // namespace

bool Session::previewContainer(const NewContainer &nc, QByteArray *text, int *node, QString *why) const
{
    using Place = SchemaDoc::Document::Place;
    auto fail = [why](const QString &w) {
        if (why) *why = w;
        return false;
    };
    if (!containerTags().contains(nc.tag)) return fail(QObject::tr("Not something the editor makes"));
    if (nc.name.trimmed().isEmpty()) return fail(QObject::tr("Give it a name"));
    if (nc.name != nc.name.trimmed()) return fail(QObject::tr("The name has spaces at its ends"));
    for (int i = 0; i < m_doc.count(); ++i) {
        if (isContainer(m_doc, i) && m_doc.node(i).tag == nc.tag && m_doc.node(i).value(QStringLiteral("name")) == nc.name) {
            return fail(QObject::tr("There is already a %1 named %2").arg(title(nc.tag).toLower(), nc.name));
        }
    }
    const QString parentTag = nc.tag == QLatin1String("enum") ? QStringLiteral("enums")
                            : nc.tag == QLatin1String("flagtable") ? QStringLiteral("flagtables")
                            : nc.tag == QLatin1String("eventtable") ? QStringLiteral("eventtables")
                            : nc.tag == QLatin1String("crc") ? QStringLiteral("crcs") : QStringLiteral("schema");
    const QVector<AttrSpec> specs = specsFor(nc.tag, parentTag);
    for (const auto &a : nc.attrs) {
        if (const AttrSpec *sp = specFor(specs, a.first)) {
            const QString p = valueProblem(m_doc, *sp, a.second);
            if (!p.isEmpty()) return fail(p);
        }
        if (nc.tag == QLatin1String("packet") && a.first == QLatin1String("match")
            && Schema::captypeTokens(a.second).isEmpty()) {
            return fail(QObject::tr("Captures: captype==token, or several tokens separated by commas"));
        }
    }
    // Where: after the last one of its kind (a struct after the last struct,
    // else after the last packet), or last in its section.
    int anchor = -1;
    Place place = Place::After;
    QVector<int> same;
    for (int i = 0; i < m_doc.count(); ++i) {
        if (isContainer(m_doc, i) && m_doc.node(i).tag == nc.tag) same.push_back(i);
    }
    if (!same.isEmpty()) {
        anchor = same.last();
    } else if (parentTag == QLatin1String("schema")) {
        const QVector<int> packets = m_doc.all(QStringLiteral("packet"));
        if (!packets.isEmpty()) anchor = packets.last();
    } else {
        anchor = m_doc.child(m_doc.root(), parentTag);
        place = Place::LastChild;
        if (anchor < 0) return fail(QObject::tr("kavach.xml has no <%1> section; add one by hand").arg(parentTag));
    }
    if (anchor < 0) return fail(QObject::tr("Nowhere to put it"));
    const bool empty = nc.tag == QLatin1String("crc");
    QString block = startTagOf(nc.tag, nc.name, nc.attrs, empty);
    if (!empty) block = QStringLiteral("\n") + block + QStringLiteral("\n</%1>").arg(nc.tag);
    int at = -1;
    QString err;
    QByteArray t = m_doc.withInserted(anchor, place, block, empty && place == Place::After ? anchor : -1, &at, &err);
    if (t.isEmpty()) return fail(err);
    SchemaDoc::Document next;
    next.parse(t);
    if (nc.tag == QLatin1String("struct") && !nc.casePacket.isEmpty()) {
        int sub = -1;
        for (int p : next.all(QStringLiteral("packet"))) {
            if (next.node(p).value(QStringLiteral("name")) == nc.casePacket) sub = next.child(p, QStringLiteral("subpackets"));
        }
        if (sub < 0) return fail(QObject::tr("%1 has no sub-packets").arg(nc.casePacket));
        if (nc.caseType < 0) return fail(QObject::tr("Give the sub-packet a type"));
        const int caseAnchor = next.node(sub).children.isEmpty() ? sub : next.node(sub).children.last();
        const Place cp = next.node(sub).children.isEmpty() ? Place::LastChild : Place::After;
        const QString line = QStringLiteral("<case type=\"%1\" struct=\"%2\"/>").arg(nc.caseType).arg(SchemaDoc::escape(nc.name, '"'));
        int caseAt = -1;
        const QByteArray t2 = next.withInserted(caseAnchor, cp, line, cp == Place::After ? caseAnchor : -1, &caseAt, &err);
        if (t2.isEmpty()) return fail(err);
        if (caseAt < at) {
            // The case line sits before the struct (SLRP comes first): the struct moved down.
            int lineLen = t2.size() - t.size();
            at += lineLen;
        }
        t = t2;
        next.parse(t);
    }
    err = check(t, &next);
    if (!err.isEmpty()) return fail(err);
    if (text) *text = t;
    if (node) *node = next.nodeAt(at);
    return true;
}

bool Session::addContainer(const NewContainer &nc, int *node, QString *why)
{
    if (readOnly()) {
        if (why) *why = QObject::tr("The built-in kavach.xml cannot be changed: make an editable copy first");
        return false;
    }
    QByteArray t;
    int added = -1;
    if (!previewContainer(nc, &t, &added, why)) return false;
    QString what = QObject::tr("+ %1 %2").arg(title(nc.tag).toLower(), nc.name);
    if (!nc.casePacket.isEmpty()) what += QObject::tr(" (sub-packet type %1 of %2)").arg(nc.caseType).arg(nc.casePacket);
    if (!stage(t, what, why)) return false;
    if (node) *node = added;
    return true;
}

bool Session::registerCase(const QString &structName, const QString &packet, int type, int *node, QString *why)
{
    int sub = -1;
    for (int p : m_doc.all(QStringLiteral("packet"))) {
        if (m_doc.node(p).value(QStringLiteral("name")) == packet) sub = m_doc.child(p, QStringLiteral("subpackets"));
    }
    if (sub < 0) {
        if (why) *why = QObject::tr("%1 has no sub-packets").arg(packet);
        return false;
    }
    NewElement e;
    e.tag = QStringLiteral("case");
    e.attrs = { qMakePair(QStringLiteral("type"), QString::number(type)), qMakePair(QStringLiteral("struct"), structName) };
    const auto place = m_doc.node(sub).children.isEmpty() ? SchemaDoc::Document::Place::LastChild
                                                          : SchemaDoc::Document::Place::After;
    const int anchor = m_doc.node(sub).children.isEmpty() ? sub : m_doc.node(sub).children.last();
    return insert(anchor, place, e, node, why);
}

bool Session::save(QString *backup, QString *error)
{
    if (readOnly()) {
        if (error) *error = QObject::tr("The built-in kavach.xml cannot be changed: make an editable copy first");
        return false;
    }
    if (!dirty()) {
        if (error) *error = QObject::tr("Nothing to save");
        return false;
    }
    QFile in(m_path);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error) *error = QObject::tr("Cannot open %1: %2").arg(m_path, in.errorString());
        return false;
    }
    if (in.readAll() != m_original) {
        if (error) *error = QObject::tr("%1 changed on disk since the editor opened it. Nothing was written; "
                                        "reopen it to edit the new version.").arg(m_path);
        return false;
    }
    in.close();
    const QString why = loadProblem(m_doc.text());
    if (!why.isEmpty()) {
        if (error) *error = QObject::tr("The decoder would refuse the schema: %1").arg(why);
        return false;
    }
    const QString b = writeWithBackup(m_path, m_doc.text(), error);
    if (b.isEmpty()) return false;
    if (backup) *backup = b;
    m_original = m_doc.text();
    m_done.clear();
    m_undone.clear();
    return true;
}

QString writeWithBackup(const QString &path, const QByteArray &text, QString *error)
{
    const QString backup = path + QStringLiteral(".bak-")
                           + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QFile::remove(backup);
    if (!QFile::copy(path, backup)) {
        if (error) *error = QObject::tr("Could not back up %1 to %2; nothing was changed").arg(path, backup);
        return QString();
    }
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(text) != text.size() || !out.commit()) {
        if (error) *error = QObject::tr("Could not write %1: %2").arg(path, out.errorString());
        return QString();
    }
    return backup;
}

bool makeEditableCopy(const QString &dest, QString *error)
{
    QFile in(QStringLiteral(":/schema/kavach.xml"));
    QSaveFile out(dest);
    if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly) || out.write(in.readAll()) < 0
        || !out.commit()) {
        if (error) *error = QObject::tr("Could not write %1: %2").arg(dest, out.errorString());
        return false;
    }
    Settings::setSchemaPath(dest);
    return true;
}

}  // namespace SchemaEdit
