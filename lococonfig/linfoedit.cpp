#include "linfoedit.h"

#include <QHash>
#include <QList>
#include <QRegularExpression>

namespace LinfoEdit {

namespace {

// The lines of a file, each WITHOUT its '\n' (a CRLF file keeps its '\r').
QList<QByteArray> splitLines(const QByteArray &text)
{
    return text.split('\n');
}

QByteArray joinLines(const QList<QByteArray> &lines)
{
    QByteArray out;
    for (int i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            out += '\n';
        }
        out += lines.at(i);
    }
    return out;
}

QString attr(const QString &line, const QString &name)
{
    const QRegularExpression re(QStringLiteral("\\b%1=\"([^\"]*)\"").arg(name));
    const QRegularExpressionMatch m = re.match(line);
    return m.hasMatch() ? m.captured(1) : QString();
}

// Where each LINFO field's line is, by key, the way Layout keys them.
struct Scan {
    QString error;
    int     packetLine = -1;
    int     endLine = -1;
    QHash<QString, int> lineOf;      // key -> line index
};

Scan scan(const QList<QByteArray> &lines)
{
    Scan s;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i).contains("<packet name=\"LINFO\"")) {
            s.packetLine = i;
            break;
        }
    }
    if (s.packetLine < 0) {
        s.error = QStringLiteral("kavach.xml has no <packet name=\"LINFO\"> line");
        return s;
    }
    QString section;
    for (int i = s.packetLine + 1; i < lines.size(); ++i) {
        const QString line = QString::fromUtf8(lines.at(i)).trimmed();
        if (line.startsWith(QLatin1String("</packet>"))) {
            s.endLine = i;
            break;
        }
        const bool isField = line.startsWith(QLatin1String("<field"));
        const bool isNote = line.startsWith(QLatin1String("<note"));
        if ((isField || isNote) && !line.endsWith(QLatin1String("/>"))) {
            // A field written over several lines: a line edit could cut it
            // in half. Refuse; the file is edited by hand then.
            s.error = QStringLiteral("LINFO line %1 is not one complete element; edit kavach.xml by hand")
                          .arg(i + 1);
            return s;
        }
        if (isNote) {
            section = attr(line, QStringLiteral("text")).section(QLatin1Char(' '), 0, 0).trimmed();
            if (section == QLatin1String(LocoInfo::kTopLevelSection)) {
                section.clear();
            }
        } else if (isField) {
            const QString name = attr(line, QStringLiteral("name"));
            s.lineOf.insert(section.isEmpty() ? name : section + QLatin1Char('.') + name, i);
        }
    }
    if (s.endLine < 0) {
        s.error = QStringLiteral("LINFO has no </packet>");
    }
    return s;
}

QString esc(const QString &text) { return text.toHtmlEscaped(); }

}  // namespace

int sizeOf(const NewField &field)
{
    switch (field.kind) {
    case LocoInfo::Kind::U8:    return 1;
    case LocoInfo::Kind::U16:   return 2;
    case LocoInfo::Kind::U32:   return 4;
    case LocoInfo::Kind::Float: return 4;
    case LocoInfo::Kind::Char:  return field.charCount;
    }
    return 0;
}

static QString keyFor(const LocoInfo::Layout &layout, const NewField &field)
{
    const LocoInfo::Field *after = layout.field(field.afterKey);
    if (after == nullptr || after->section.isEmpty()) {
        return field.name;
    }
    return after->section + QLatin1Char('.') + field.name;
}

QString problem(const LocoInfo::Layout &layout, const NewField &field)
{
    static const QRegularExpression cName(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    if (field.name.isEmpty()) {
        return QStringLiteral("Give the field a name");
    }
    if (!cName.match(field.name).hasMatch()) {
        return QStringLiteral("\"%1\" is not a C member name (letters, digits and _, not starting with a digit)")
            .arg(field.name);
    }
    if (field.kind == LocoInfo::Kind::Char && (field.charCount < 2 || field.charCount > 255)) {
        return QStringLiteral("A char array needs 2 to 255 characters");
    }
    const LocoInfo::Field *after = layout.field(field.afterKey);
    if (after == nullptr) {
        return QStringLiteral("Choose the field it comes after");
    }
    if (after->isCrc) {
        return QStringLiteral("%1 stays the last member").arg(QLatin1String(LocoInfo::kCrcFieldName));
    }
    const QString key = keyFor(layout, field);
    if (layout.indexOf(key) >= 0) {
        return QStringLiteral("LOCO_INFO already has %1").arg(key);
    }
    if (field.group.contains(QLatin1Char('\n')) || field.note.contains(QLatin1Char('\n'))) {
        return QStringLiteral("The group and the note are one line each");
    }
    if (!field.defaultText.trimmed().isEmpty()) {
        LocoInfo::Field probe;
        probe.key = key;
        probe.name = field.name;
        probe.kind = field.kind;
        probe.size = sizeOf(field);
        QVariant value;
        QString why;
        if (!LocoInfo::parseValueText(probe, field.defaultText.trimmed(), QString(), &value, &why)) {
            return QStringLiteral("Default: %1").arg(why);
        }
    }
    return QString();
}

QString element(const NewField &field)
{
    QString out = QStringLiteral("<field name=\"%1\" ").arg(esc(field.name));
    switch (field.kind) {
    case LocoInfo::Kind::U8:    out += QStringLiteral("bits=\"8\""); break;
    case LocoInfo::Kind::U16:   out += QStringLiteral("bits=\"16\""); break;
    case LocoInfo::Kind::U32:   out += QStringLiteral("bits=\"32\""); break;
    case LocoInfo::Kind::Float: out += QStringLiteral("type=\"float\" prec=\"4\""); break;
    case LocoInfo::Kind::Char:  out += QStringLiteral("type=\"char\" count=\"%1\"").arg(field.charCount); break;
    }
    const bool integer = field.kind == LocoInfo::Kind::U8 || field.kind == LocoInfo::Kind::U16
                         || field.kind == LocoInfo::Kind::U32;
    if (field.hex && integer) {
        out += QStringLiteral(" hexup=\"%1\"").arg(sizeOf(field) * 2);
    }
    if (!field.group.trimmed().isEmpty()) {
        out += QStringLiteral(" ui-group=\"%1\"").arg(esc(field.group.trimmed()));
    }
    if (!field.note.trimmed().isEmpty()) {
        out += QStringLiteral(" ui-note=\"%1\"").arg(esc(field.note.trimmed()));
    }
    if (!field.defaultText.trimmed().isEmpty()) {
        out += QStringLiteral(" ui-default=\"%1\"").arg(esc(field.defaultText.trimmed()));
    }
    out += QStringLiteral("/>");
    return out;
}

Result addField(const QByteArray &schemaText, const LocoInfo::Layout &layout, const NewField &field)
{
    Result r;
    r.oldSize = layout.bodySize();
    r.error = problem(layout, field);
    if (!r.error.isEmpty()) {
        return r;
    }
    QList<QByteArray> lines = splitLines(schemaText);
    const Scan s = scan(lines);
    if (!s.error.isEmpty()) {
        r.error = s.error;
        return r;
    }
    if (!s.lineOf.contains(field.afterKey)) {
        r.error = QStringLiteral("%1 has no line of its own in LINFO").arg(field.afterKey);
        return r;
    }
    const int afterLine = s.lineOf.value(field.afterKey);
    const QString after = QString::fromUtf8(lines.at(afterLine));
    const bool crlf = after.endsWith(QLatin1Char('\r'));

    // The neighbour's indent, and the column its attributes start at, so the
    // new line lines up with the rest.
    int indent = 0;
    while (indent < after.size() && (after.at(indent) == QLatin1Char(' ') || after.at(indent) == QLatin1Char('\t'))) {
        ++indent;
    }
    const QRegularExpressionMatch nm = QRegularExpression(QStringLiteral("name=\"[^\"]*\"\\s*")).match(after);
    const int column = nm.hasMatch() ? nm.capturedEnd() : 0;

    const QString el = element(field);
    const int split = el.indexOf(QLatin1Char('"'), el.indexOf(QLatin1Char('"')) + 1) + 1;   // after name="…"
    QString head = after.left(indent) + el.left(split);
    QString line = head + QString(qMax(1, column - head.size()), QLatin1Char(' ')) + el.mid(split + 1);
    if (crlf) {
        line += QLatin1Char('\r');
    }
    lines.insert(afterLine + 1, line.toUtf8());
    r.text = joinLines(lines);
    r.line = afterLine + 2;
    r.lineText = line.trimmed();

    // Read it back as the editor will.
    LocoInfo::Layout check;
    QString why;
    const QString key = keyFor(layout, field);
    if (!check.loadXml(r.text, QStringLiteral("kavach.xml"), &why)) {
        r.error = QStringLiteral("The edited schema would not load: %1").arg(why);
        return r;
    }
    const LocoInfo::Field *added = check.field(key);
    if (added == nullptr || check.indexOf(key) != layout.indexOf(field.afterKey) + 1
        || added->size != sizeOf(field) || check.bodySize() != r.oldSize + sizeOf(field)) {
        r.error = QStringLiteral("The edit did not give the expected layout; kavach.xml is left as it was");
        return r;
    }
    r.newSize = check.bodySize();
    r.ok = true;
    return r;
}

QString removeProblem(const LocoInfo::Layout &layout, const QString &key)
{
    const LocoInfo::Field *field = layout.field(key);
    if (field == nullptr) {
        return QStringLiteral("LOCO_INFO has no %1").arg(key);
    }
    if (field->isCrc) {
        return QStringLiteral("%1 is computed and must stay the last member").arg(key);
    }
    if (field->name == QLatin1String("vcc_crc")) {
        return QStringLiteral("vcc_crc is shown in the send bar and checked before every send; it is not removed from here");
    }
    return QString();
}

Result removeField(const QByteArray &schemaText, const LocoInfo::Layout &layout, const QString &key)
{
    Result r;
    r.oldSize = layout.bodySize();
    r.error = removeProblem(layout, key);
    if (!r.error.isEmpty()) {
        return r;
    }
    QList<QByteArray> lines = splitLines(schemaText);
    const Scan s = scan(lines);
    if (!s.error.isEmpty()) {
        r.error = s.error;
        return r;
    }
    if (!s.lineOf.contains(key)) {
        r.error = QStringLiteral("%1 has no line of its own in LINFO").arg(key);
        return r;
    }
    const int at = s.lineOf.value(key);
    r.line = at + 1;
    r.lineText = QString::fromUtf8(lines.at(at)).trimmed();
    lines.removeAt(at);
    r.text = joinLines(lines);

    LocoInfo::Layout check;
    QString why;
    if (!check.loadXml(r.text, QStringLiteral("kavach.xml"), &why)) {
        r.error = QStringLiteral("The edited schema would not load: %1").arg(why);
        return r;
    }
    if (check.indexOf(key) >= 0 || check.bodySize() != r.oldSize - layout.field(key)->size) {
        r.error = QStringLiteral("The edit did not give the expected layout; kavach.xml is left as it was");
        return r;
    }
    r.newSize = check.bodySize();
    r.ok = true;
    return r;
}

}  // namespace LinfoEdit
