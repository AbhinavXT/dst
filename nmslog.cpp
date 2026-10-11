#include "nmslog.h"

#include "fieldplot.h"   // captureTypeOf

#include <QDateTime>

namespace NmsLog {

namespace {

const QString kPcTime = QStringLiteral("PC time");
const QString kHex    = QStringLiteral("Frame (hex)");

// The capture line's columns, in the order they are shown.
int leadRank(const QString &name)
{
    static const QStringList lead = { kPcTime, QStringLiteral("rtc"), QStringLiteral("seq"),
                                      QStringLiteral("loco / ctrl"), QStringLiteral("frame len"),
                                      QStringLiteral("CRC") };
    const int i = lead.indexOf(name);
    return i < 0 ? lead.size() : i;
}

int sortKey(const QString &name, Group g)
{
    return int(g) * 100 + (g == Group::Lead ? leadRank(name) : 0);
}

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

Mark markOf(const QString &column, const QString &value, QString *why)
{
    auto mark = [&](Mark m, const QString &text) {
        if (why) *why = text;
        return m;
    };
    if (why) why->clear();
    if (value.isEmpty()) return Mark::None;
    // "fault[2] fault_id", "RADIO_1_HEALTH (2)": the name without its repeat.
    const QString name = column.section(QStringLiteral(" ("), 0, 0);
    auto is = [&](const char *n) { return name == QLatin1String(n); };
    auto has = [&](const char *t) { return value.contains(QLatin1String(t), Qt::CaseInsensitive); };

    if (is("CRC"))
        return value == QLatin1String("FAIL") ? mark(Mark::Fail, QStringLiteral("The frame's CRC did not match"))
                                              : Mark::None;
    if (name.startsWith(QLatin1String("fault[")) && name.endsWith(QLatin1String("fault_id")))
        return mark(Mark::Warn, QStringLiteral("A fault the subsystem reported"));

    // Equipment reported down.
    if ((is("RADIO_1_HEALTH") || is("RADIO_2_HEALTH")) && !has("(OK)"))
        return mark(Mark::Fail, QStringLiteral("The radio reported not OK"));
    if ((is("DMI_1_LINK_STATUS") || is("DMI_2_LINK_STATUS") || is("RFID_READER_1_LINK_STATUS")
         || is("RFID_READER_2_LINK_STATUS")) && has("NOT OK"))
        return mark(Mark::Fail, QStringLiteral("The link reported NOT OK"));
    if (is("ONBOARD_KAVACH_MODULES_HEALTH") && has("NOT OK"))
        return mark(Mark::Fail, QStringLiteral("A module reported NOT OK"));
    if (is("REMAINING_KEY_NUMBERS")) {
        if (has("No keys")) return mark(Mark::Fail, QStringLiteral("No authentication key sets left"));
        const int left = value.section(QLatin1Char(' '), 0, 0).toInt();
        if (left < 5) return mark(Mark::Warn, QStringLiteral("Fewer than 5 key sets left"));
        return Mark::None;
    }
    if (is("BOOTUP_SEQUENCE_ERROR")) return mark(Mark::Fail, QStringLiteral("A boot-up sequence error was reported"));
    if (is("BRAKE_INTERFACE_ERROR")) return mark(Mark::Fail, QStringLiteral("A brake interface error was reported"));

    // Events worth reading.
    if (is("ACTIVE_GPS_NUMBER") && has("No Active GPS"))
        return mark(Mark::Warn, QStringLiteral("No GPS active"));
    if ((is("GPS_1_VIEW_STATUS") || is("GPS_2_VIEW_STATUS")) && (has("(V)") || has("No Data")))
        return mark(Mark::Warn, QStringLiteral("No valid GPS fix (A is a valid fix)"));
    if ((is("GPS_1_LINK_STATUS") || is("GPS_2_LINK_STATUS")) && has("fail"))
        return mark(Mark::Warn, QStringLiteral("The GPS link or PPS reported a failure"));
    if (is("BRAKE_APPLICATION_REASON") && !has("Not used"))
        return mark(Mark::Warn, QStringLiteral("A brake application was reported"));
    if (is("LOCO_SELF_SOS") || is("STATION_GENERAL_SOS") || is("STATION_LOCO_SPECIFIC_SOS"))
        return mark(Mark::Warn, QStringLiteral("An SoS event was reported"));
    if (is("COLLISION_DETECTION"))
        return mark(Mark::Warn, QStringLiteral("A collision detection was reported"));
    if (is("DUPLICATE_MISSING_RFID") || is("MISSING_LINKED_RFID") || is("CONFLICT_ROUTE_RFID"))
        return mark(Mark::Warn, QStringLiteral("An RFID tag event was reported"));
    if (is("EB_BYPASSED") && has("Bypassed"))
        return mark(Mark::Warn, QStringLiteral("EB reported bypassed"));
    if ((is("BIU_ISOLATED") || is("KAVACH_CONNECTION")) && has("Isolated"))
        return mark(Mark::Warn, QStringLiteral("Reported isolated"));
    return Mark::None;
}

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

bool Table::unchanging(int col) const
{
    if (col < 0 || col >= order.size()) return false;
    return !carry.varies.contains(order.at(col));
}

Group Table::groupAt(int col) const
{
    return col >= 0 && col < order.size() ? groups.at(order.at(col)) : Group::Lead;
}

int Table::columnOf(const QString &name, Group g)
{
    const auto it = index.constFind(name);
    if (it != index.constEnd()) return order.at(it.value());
    const int id = columns.size();
    columns << name;
    groups << g;
    const int key = sortKey(name, g);
    int at = 0;
    while (at < order.size() && sortKey(columns.at(order.at(at)), groups.at(order.at(at))) <= key) ++at;
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
        // line, then the NMS header.
        const Group g = r.field.startsWith(QLatin1Char(' ')) ? Group::Body
                      : leadRank(name) < leadRank(QString()) ? Group::Lead
                      : Group::Header;
        put(n == 1 ? name : QStringLiteral("%1 (%2)").arg(name).arg(n), r.value.trimmed(), g);
    }
    while (row.size() < t.columns.size()) row << QString();
    bool changed = false;   // the first packet's events are all new: changed
    for (int c = 0; c < row.size(); ++c) {
        if (row.at(c).isEmpty()) continue;
        if (t.groups.at(c) == Group::Body) {
            auto it = t.carry.lastValue.find(c);
            if (it == t.carry.lastValue.end()) { t.carry.lastValue.insert(c, row.at(c)); changed = true; }
            else if (it.value() != row.at(c)) { it.value() = row.at(c); changed = true; }
        } else if (t.columns.at(c) == QLatin1String("CRC") && row.at(c) == QLatin1String("FAIL")) {
            changed = true;
        }
    }

    // Session 254: which columns have read differently.
    for (int c = 0; c < row.size(); ++c) {
        if (row.at(c).isEmpty() || t.carry.varies.contains(c)) continue;
        auto it = t.carry.firstValue.constFind(c);
        if (it == t.carry.firstValue.constEnd()) t.carry.firstValue.insert(c, row.at(c));
        else if (it.value() != row.at(c)) t.carry.varies.insert(c);
    }

    // Session 252: packets missing before this one?
    QString gapColumn, gapNote;
    auto valueOf = [&](const QString &name) { return row.value(t.columns.indexOf(name)); };
    if (kind == Kind::Fault) {
        bool ok = false;
        const int seq = valueOf(QStringLiteral("nms.message_seq")).toInt(&ok);
        if (ok) {
            const int last = t.carry.lastMsgSeq;
            const int step = (seq - last) & 0xFFFF;
            if (last >= 0 && step != 1) {
                gapColumn = QStringLiteral("nms.message_seq");
                if (step == 0)
                    gapNote = QStringLiteral("message_seq %1 again: the same number as the packet before").arg(seq);
                else if (step <= 1000)
                    gapNote = QStringLiteral("message_seq %1 then %2: %3 @nmsflt packet(s) not in the log")
                                  .arg(last).arg(seq).arg(step - 1);
                else
                    gapNote = QStringLiteral("message_seq %1 then %2: the count started again "
                                             "(the sender restarted, or packets are not in the log)").arg(last).arg(seq);
            }
            t.carry.lastMsgSeq = seq;
        }
    } else {
        const QDateTime rtc = QDateTime::fromString(valueOf(QStringLiteral("rtc")), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        if (rtc.isValid()) {
            const qint64 secs = rtc.toMSecsSinceEpoch() / 1000;
            const qint64 last = t.carry.lastRtc;
            if (last >= 0 && secs - last >= 3) {
                gapColumn = QStringLiteral("rtc");
                gapNote = QStringLiteral("%1 s since the @%2 before (they are sent about once a second)")
                              .arg(secs - last).arg(token(kind));
            } else if (last >= 0 && secs < last) {
                gapColumn = QStringLiteral("rtc");
                gapNote = QStringLiteral("The RTC is %1 s behind the @%2 before").arg(last - secs).arg(token(kind));
            }
            t.carry.lastRtc = secs;
        }
    }
    if (!gapColumn.isEmpty()) { ++t.gaps; changed = true; }
    t.gapColumn << gapColumn;
    t.gapNote << gapNote;
    t.changed << changed;
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
