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
    if (name.startsWith(QLatin1String("fault[")) && name.endsWith(QLatin1String("code_type"))) {
        if (value.endsWith(QLatin1String("(Faulty)"))) return mark(Mark::Fail, QStringLiteral("The fault is reported active"));
        if (value.endsWith(QLatin1String("(Recovered)"))) return mark(Mark::Ok, QStringLiteral("The fault is reported recovered"));
        return Mark::None;
    }
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
    // @dllog (session 267).
    if (is("log_type") && has("(Dangerous)"))
        return mark(Mark::Warn, QStringLiteral("A dangerous event was logged"));
    if ((is("BIU_ISOLATED") || is("KAVACH_CONNECTION")) && has("Isolated"))
        return mark(Mark::Warn, QStringLiteral("Reported isolated"));
    return Mark::None;
}

QVector<int> dlCounts(const Table &t)
{
    QVector<int> n(4, 0);
    const int col = t.index.value(QStringLiteral("counter_value"), -1);
    if (col < 0) return n;
    for (int row = 0; row < t.rows.size(); ++row) {
        bool ok = false;
        const int v = t.cell(row, col).section(QLatin1Char(' '), 0, 0).toInt(&ok);   // "1  (SOS counter)"
        ++n[ok && v >= 0 && v <= 2 ? v : 3];
    }
    return n;
}

QString dlCountTotals(const Table &t)
{
    const QVector<int> n = dlCounts(t);
    QString text = QStringLiteral("Isolation %1 \u00B7 SOS %2 \u00B7 OV trip %3").arg(n[0]).arg(n[1]).arg(n[2]);
    if (n[3] > 0) text += QStringLiteral(" \u00B7 other %1").arg(n[3]);
    return text;
}

QString token(Kind k)
{
    switch (k) {
    case Kind::Health: return QStringLiteral("nmshlth");
    case Kind::Fault:  return QStringLiteral("nmsflt");
    case Kind::Rssi:   return QStringLiteral("nmsrssi");
    case Kind::DlLog:  return QStringLiteral("dllog");
    case Kind::DlCount: return QStringLiteral("dlcount");
    }
    return QString();
}

QString title(Kind k)
{
    switch (k) {
    case Kind::Health: return QStringLiteral("NMS health log");
    case Kind::Fault:  return QStringLiteral("NMS fault log");
    case Kind::Rssi:   return QStringLiteral("NMS RSSI log");
    case Kind::DlLog:  return QStringLiteral("DL log");
    case Kind::DlCount: return QStringLiteral("DL counts");
    }
    return QString();
}

QString fileStem(Kind k)
{
    switch (k) {
    case Kind::Health: return QStringLiteral("nms_health");
    case Kind::Fault:  return QStringLiteral("nms_fault");
    case Kind::Rssi:   return QStringLiteral("nms_rssi");
    case Kind::DlLog:  return QStringLiteral("dl_log");
    case Kind::DlCount: return QStringLiteral("dl_counts");
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
    // By type, not token: @dllog also arrives as @dl_log (session 267).
    if (CaptureDecoder::typeFromToken(captureTypeOf(entry.text))
        != CaptureDecoder::typeFromToken(token(kind))) return false;
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
    QHash<QString, qint64> raw;
    for (const FieldRow &r : CaptureDecoder::describe(cap, nullptr, 0, &raw)) {
        const QString name = r.field.trimmed();
        if (name.isEmpty() || name == QLatin1String("type")) continue;   // the window is one type
        const int n = ++seen[name];
        // describe() indents the packet's own rows; the rest is the capture
        // line, then the NMS header.
        // @dllog's message header is the packet's own first rows: msg.*.
        const Group g = (kind == Kind::DlLog || kind == Kind::DlCount) && name.startsWith(QLatin1String("msg.")) ? Group::Header
                      : r.field.startsWith(QLatin1Char(' ')) ? Group::Body
                      : leadRank(name) < leadRank(QString()) ? Group::Lead
                      : Group::Header;
        QString value = r.value.trimmed();
        if (name.startsWith(QLatin1String("fault[")) && name.endsWith(QLatin1String("code_type"))) {
            const QString st = faultStatus(value.toInt(nullptr, 16));   // "0X01": toInt takes the 0x prefix
            if (!st.isEmpty()) value += QStringLiteral(" (%1)").arg(st);
        }
        put(n == 1 ? name : QStringLiteral("%1 (%2)").arg(name).arg(n), value, g);
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
    } else if (kind != Kind::DlLog && kind != Kind::DlCount) {   // sent on events, seq_num 0: no basis for a gap
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

    // Session 255: the faults this packet lists, against what its subsystem
    // listed last.
    if (kind == Kind::Fault) {
        const int at = t.rowBase + t.rows.size();
        const QVector<ActiveFaultInfo> now = CaptureDecoder::faultsOf(cap);
        const int sub = raw.contains(QStringLiteral("reporting_subsystem"))
                            ? int(raw.value(QStringLiteral("reporting_subsystem")))
                            : now.isEmpty() ? -1 : now.first().subsystem;
        if (sub >= 0) {
            QSet<QString> keys;
            for (const ActiveFaultInfo &f : now) {
                const QString key = QStringLiteral("%1:%2:%3:%4").arg(f.subsystem).arg(f.moduleId).arg(f.codeType).arg(f.faultId);
                // A packet can list a fault twice (loco_1_1_29062026_134509 at
                // 14:20:55 lists 17 / 0X02 / 100 twice): one packet, counted once.
                if (keys.contains(key)) continue;
                keys.insert(key);
                auto it = t.carry.openSpan.constFind(key);
                if (it == t.carry.openSpan.constEnd()) {
                    Table::FaultSpan sp;
                    sp.subsystem = f.subsystemName.isEmpty() ? QString::number(f.subsystem)
                                                             : QStringLiteral("%1 (%2)").arg(f.subsystem).arg(f.subsystemName);
                    sp.module = f.moduleName.isEmpty() ? QString::number(f.moduleId)
                                                       : QStringLiteral("%1 (%2)").arg(f.moduleId).arg(f.moduleName);
                    sp.fault = f.faultName.isEmpty() ? QString::number(f.faultId)
                                                     : QStringLiteral("%1 (%2)").arg(f.faultId).arg(f.faultName);
                    sp.codeType = f.codeType;
                    sp.fromRow = sp.lastRow = at;
                    sp.packets = 1;
                    t.faultSpans << sp;
                    t.carry.openSpan.insert(key, t.faultSpans.size() - 1);
                } else {
                    Table::FaultSpan &sp = t.faultSpans[it.value()];
                    sp.lastRow = at;
                    ++sp.packets;
                }
            }
            for (const QString &key : t.carry.listed.value(sub)) {
                if (keys.contains(key) || !t.carry.openSpan.contains(key)) continue;
                t.faultSpans[t.carry.openSpan.take(key)].toRow = at;
            }
            t.carry.listed[sub] = keys;
        }
    }
    t.gapColumn << gapColumn;
    t.gapNote << gapNote;
    t.changed << changed;
    t.rows << row;
    t.hex << QString::fromLatin1(cap.bytes.toHex(' ').toUpper());
    t.epochMs << entry.epochMs;
    if (newColumns) *newColumns = t.columns.size() != before;
    return true;
}

QString faultStatus(int codeType)
{
    return codeType == 0x01 ? QStringLiteral("Faulty") : codeType == 0x02 ? QStringLiteral("Recovered") : QString();
}

QStringList faultSummaryHeader()
{
    return { QStringLiteral("Module"), QStringLiteral("Fault"), QStringLiteral("Code type"),
             QStringLiteral("Subsystem"), QStringLiteral("First listed (PC time)"),
             QStringLiteral("First listed (rtc)"), QStringLiteral("No longer listed (PC time)"),
             QStringLiteral("Listed for"), QStringLiteral("Packets listing it") };
}

namespace {

QString pcTime(const Table &t, int row)
{
    const int r = row - t.rowBase;
    return r >= 0 && r < t.epochMs.size()
               ? QDateTime::fromMSecsSinceEpoch(t.epochMs.at(r)).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
               : QString();
}

QString span(qint64 ms)
{
    const qint64 s = qMax<qint64>(0, ms / 1000);
    return QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QLatin1Char('0'))
                                     .arg(s % 60, 2, 10, QLatin1Char('0'));
}

}  // namespace

QVector<QStringList> faultSummary(const Table &t)
{
    QVector<QStringList> out;
    const int rtc = t.index.value(QStringLiteral("rtc"), -1);
    for (const Table::FaultSpan &sp : t.faultSpans) {
        const int from = sp.fromRow - t.rowBase;
        const bool open = sp.toRow < 0;
        const int to = (open ? t.rows.size() - 1 : sp.toRow - t.rowBase);
        const qint64 ms = (to >= 0 && to < t.epochMs.size() && from >= 0 && from < t.epochMs.size())
                              ? t.epochMs.at(to) - t.epochMs.at(from) : 0;
        QString code = QStringLiteral("0X%1").arg(sp.codeType, 2, 16, QLatin1Char('0')).toUpper();
        if (!faultStatus(sp.codeType).isEmpty()) code += QStringLiteral(" (%1)").arg(faultStatus(sp.codeType));
        out << QStringList{ sp.module, sp.fault, code,
                            sp.subsystem, pcTime(t, sp.fromRow), rtc >= 0 ? t.cell(from, rtc) : QString(),
                            open ? QStringLiteral("still listed in the last packet") : pcTime(t, sp.toRow),
                            open ? QStringLiteral("at least ") + span(ms) : span(ms), QString::number(sp.packets) };
    }
    return out;
}

QString faultSummaryCsv(const Table &t)
{
    QString out;
    QStringList line;
    for (const QString &h : faultSummaryHeader()) line << csvField(h);
    out += line.join(QLatin1Char(',')) + QStringLiteral("\r\n");
    for (const QStringList &row : faultSummary(t)) {
        line.clear();
        for (const QString &v : row) line << csvField(v);
        out += line.join(QLatin1Char(',')) + QStringLiteral("\r\n");
    }
    return out;
}

QStringList findWords(const QString &query)
{
    QString q = query.toLower();
    q.replace(QLatin1Char('_'), QLatin1Char(' '));
    return q.split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

namespace {

QString folded(const QString &s)
{
    QString f = s.toLower();
    f.replace(QLatin1Char('_'), QLatin1Char(' '));
    return f;
}

bool hasAll(const QString &foldedText, const QStringList &words)
{
    for (const QString &w : words)
        if (!foldedText.contains(w)) return false;
    return true;
}

}  // namespace

bool cellMatches(const QString &cell, const QStringList &words)
{
    return !words.isEmpty() && !cell.isEmpty() && hasAll(folded(cell), words);
}

bool rowMatches(const Table &t, int row, const QStringList &words, int col)
{
    if (words.isEmpty() || row < 0 || row >= t.rows.size() || col < -1) return false;   // < -1: a column not in the table
    if (col >= 0) return cellMatches(t.cell(row, col), words);
    QString all;
    for (int c = 0; c < t.columnCount(); ++c) {
        all += t.cell(row, c);
        all += QLatin1String(" | ");
    }
    return hasAll(folded(all), words);
}

bool rowMarked(const Table &t, int row)
{
    if (row < 0 || row >= t.rows.size()) return false;
    if (!t.gapColumn.value(row).isEmpty()) return true;
    for (int c = 0; c < t.order.size(); ++c)
        if (markOf(t.header(c), t.cell(row, c)) != Mark::None) return true;
    return false;
}

namespace {

QVector<int> allOf(int n)
{
    QVector<int> v(n);
    for (int i = 0; i < n; ++i) v[i] = i;
    return v;
}

}  // namespace

QString toCsv(const Table &t, const QVector<int> &rowsIn, const QVector<int> &colsIn)
{
    const QVector<int> rows = rowsIn.isEmpty() ? allOf(t.rows.size()) : rowsIn;
    const QVector<int> cols = colsIn.isEmpty() ? allOf(t.columnCount()) : colsIn;
    QString out;
    QStringList line;
    for (int c : cols) line << csvField(t.header(c));
    out += line.join(QLatin1Char(',')) + QStringLiteral("\r\n");
    for (int r : rows) {
        line.clear();
        for (int c : cols) line << csvField(t.cell(r, c));
        out += line.join(QLatin1Char(',')) + QStringLiteral("\r\n");
    }
    return out;
}

QVector<XlsxBook::Sheet> toSheets(const Table &t, Kind kind, const QVector<int> &rowsIn, const QVector<int> &colsIn)
{
    const QVector<int> rows = rowsIn.isEmpty() ? allOf(t.rows.size()) : rowsIn;
    const QVector<int> cols = colsIn.isEmpty() ? allOf(t.columnCount()) : colsIn;
    XlsxBook::Sheet sh;
    sh.name = token(kind);
    XlsxBook::Row head;
    for (int c : cols) head << t.header(c);
    sh.rows << head;
    for (int r : rows) {
        XlsxBook::Row row;
        for (int c : cols) row << sheetCell(t.cell(r, c));
        while (!row.isEmpty() && !row.last().isValid()) row.removeLast();
        sh.rows << row;
    }
    return { sh };
}

}  // namespace NmsLog
