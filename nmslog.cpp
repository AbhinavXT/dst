#include "nmslog.h"

#include "fieldplot.h"   // captureTypeOf

#include <QDateTime>

namespace NmsLog {

namespace {

const QString kPcTime = QStringLiteral("PC time");
const QString kHex    = QStringLiteral("Frame (hex)");

QString csvField(const QString &s)
{
    if (!s.contains(QLatin1Char(',')) && !s.contains(QLatin1Char('"'))
        && !s.contains(QLatin1Char('\n')) && !s.contains(QLatin1Char('\r'))) return s;
    QString q = s;
    q.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + q + QLatin1Char('"');
}

// A whole number goes to the sheet as one, so Excel sorts and sums it.
// Leading zeros and "0X1A" stay text: they are codes, not quantities.
QVariant sheetCell(const QString &s)
{
    if (s.isEmpty()) return QVariant();
    bool ok = false;
    const qlonglong v = s.toLongLong(&ok);
    if (ok && QString::number(v) == s) return v;
    return s;
}

}  // namespace

QString token(Kind k)
{
    switch (k) {
    case Kind::Health: return QStringLiteral("nmshlth");
    case Kind::Fault:  return QStringLiteral("nmsflt");
    case Kind::Rssi:   return QStringLiteral("nmsrssi");
    }
    return QString();
}

QString title(Kind k)
{
    switch (k) {
    case Kind::Health: return QStringLiteral("NMS health log");
    case Kind::Fault:  return QStringLiteral("NMS fault log");
    case Kind::Rssi:   return QStringLiteral("NMS RSSI log");
    }
    return QString();
}

QString fileStem(Kind k)
{
    switch (k) {
    case Kind::Health: return QStringLiteral("nms_health");
    case Kind::Fault:  return QStringLiteral("nms_fault");
    case Kind::Rssi:   return QStringLiteral("nms_rssi");
    }
    return QString();
}

Table::Table()
{
    columnOf(kPcTime, Group::Lead);
}

QString Table::header(int col) const
{
    return col == order.size() ? kHex : columns.value(order.value(col, -1));
}

QString Table::cell(int row, int col) const
{
    if (row < 0 || row >= rows.size()) return QString();
    if (col == order.size()) return hex.at(row);
    return rows.at(row).value(order.value(col, -1));   // a column added after this row: empty
}

int Table::columnOf(const QString &name, Group g)
{
    const auto it = index.constFind(name);
    if (it != index.constEnd()) return order.at(it.value());
    const int id = columns.size();
    columns << name;
    groups << g;
    int at = 0;
    while (at < order.size() && groups.at(order.at(at)) <= g) ++at;
    order.insert(at, id);
    for (int p = at; p < order.size(); ++p) index.insert(columns.at(order.at(p)), p);
    return id;
}

bool append(Table &t, Kind kind, const LogEntry &entry, bool *newColumns)
{
    if (newColumns) *newColumns = false;
    if (captureTypeOf(entry.text) != token(kind)) return false;
    const CaptureLine cap = CaptureDecoder::parseLine(entry.text);
    if (!cap.valid || cap.bytes.isEmpty()) return false;

    const int before = t.columns.size();
    QStringList row;
    auto put = [&](const QString &name, const QString &value, Group g) {
        const int c = t.columnOf(name, g);
        while (row.size() <= c) row << QString();   // no QList::resize in Qt 5
        row[c] = value;
    };
    put(kPcTime, QDateTime::fromMSecsSinceEpoch(entry.epochMs).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
        Group::Lead);

    QHash<QString, int> seen;   // a name twice in one frame gets "(2)", as the encoder keys it
    for (const FieldRow &r : CaptureDecoder::describe(cap)) {
        const QString name = r.field.trimmed();
        if (name.isEmpty() || name == QLatin1String("type")) continue;   // the window is one type
        const int n = ++seen[name];
        // describe() indents the packet's own rows; the rest is the capture
        // line and the NMS header, of which the RTC and seq say when.
        const Group g = r.field.startsWith(QLatin1Char(' ')) ? Group::Body
                      : (name == QLatin1String("rtc") || name == QLatin1String("seq")) ? Group::Lead
                      : Group::Details;
        put(n == 1 ? name : QStringLiteral("%1 (%2)").arg(name).arg(n), r.value.trimmed(), g);
    }
    while (row.size() < t.columns.size()) row << QString();
    t.rows << row;
    t.hex << QString::fromLatin1(cap.bytes.toHex(' ').toUpper());
    t.epochMs << entry.epochMs;
    if (newColumns) *newColumns = t.columns.size() != before;
    return true;
}

QString toCsv(const Table &t)
{
    QString out;
    const int cols = t.columnCount();
    QStringList line;
    for (int c = 0; c < cols; ++c) line << csvField(t.header(c));
    out += line.join(QLatin1Char(',')) + QStringLiteral("\r\n");
    for (int r = 0; r < t.rows.size(); ++r) {
        line.clear();
        for (int c = 0; c < cols; ++c) line << csvField(t.cell(r, c));
        out += line.join(QLatin1Char(',')) + QStringLiteral("\r\n");
    }
    return out;
}

QVector<XlsxBook::Sheet> toSheets(const Table &t, Kind kind)
{
    XlsxBook::Sheet sh;
    sh.name = token(kind);
    const int cols = t.columnCount();
    XlsxBook::Row head;
    for (int c = 0; c < cols; ++c) head << t.header(c);
    sh.rows << head;
    for (int r = 0; r < t.rows.size(); ++r) {
        XlsxBook::Row row;
        for (int c = 0; c < cols; ++c) row << sheetCell(t.cell(r, c));
        while (!row.isEmpty() && !row.last().isValid()) row.removeLast();
        sh.rows << row;
    }
    return { sh };
}

}  // namespace NmsLog
