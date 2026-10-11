#ifndef NMSLOG_H
#define NMSLOG_H

// =============================================================================
//  NMS packet logs (session 247) — Tools ▸ Monitor ▸ NMS ▸
//  NMS health log… / NMS fault log… / NMS RSSI log…
//  -----------------------------------------------------------------------------
//  One table per NMS packet type, one row per packet, as the tab holds them
//  and then as they arrive. The columns (session 248):
//    PC time, rtc, seq, loco / ctrl, frame len, CRC (PASS / FAIL)
//    the NMS header: nms.start_of_frame … nms.time
//    the packet's own fields, as CaptureDecoder::describe() prints them, in
//    the order first seen:
//      @nmshlth  one column per health event; a packet that did not carry
//                an event leaves its cell empty
//      @nmsflt   reporting_subsystem, fault_count, then fault[1] module /
//                code_type / fault_id, fault[2] …, as many as a packet carried
//      @nmsrssi  its fixed fields
//    last, the frame in hex.
//  Values are as the decoder shows them ("1  (OK)"); a cell that is a whole
//  number goes to the .xlsx as a number.
// =============================================================================

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include "capturedecoder.h"
#include "logentry.h"
#include "xlsxbook.h"

namespace NmsLog {

enum class Kind { Health, Fault, Rssi };

QString token(Kind k);        // "nmshlth", "nmsflt", "nmsrssi"
QString title(Kind k);        // "NMS health log"
QString fileStem(Kind k);     // "nms_health"

// Where a column sits: the capture line (when, which loco, length, CRC; in
// a fixed order), then the NMS header, then the packet's own fields.
enum class Group { Lead, Header, Body };

struct Table {
    QStringList columns;                // by id, in the order first seen
    QVector<Group> groups;              // by id
    QVector<int> order;                 // display position -> id
    QVector<QStringList> rows;          // by id; each as long as `columns` was when it was added
    QVector<QString> hex;               // per row: the frame, the last column shown
    QVector<qint64> epochMs;            // per row: the entry's time, for jumping to it
    // Session 251: per row, did any of the packet's own fields (the events)
    // read differently from the last time that field was carried, or did the
    // CRC fail, or (session 252) is there a gap before it? The first packet
    // always did.
    QVector<bool> changed;
    // Session 252: a gap before the row: packets missing. gapColumn names the
    // cell that shows it ("rtc" or "nms.message_seq"), gapNote says what.
    //   @nmshlth / @nmsrssi  sent about once a second, and their message_seq
    //                        does not count: a gap is 3 s or more on the RTC
    //                        (or the RTC going back)
    //   @nmsflt              sent when there is a fault to report, its
    //                        message_seq counts up by one: a jump, a repeat
    //                        or a restart
    QVector<QString> gapColumn;
    QVector<QString> gapNote;
    int gaps = 0;
    // What the next row is compared with; carried across batches.
    struct Carry {
        QHash<int, QString> lastValue;  // by column id: the event's last value
        qint64 lastRtc = -1;            // seconds since the epoch
        int lastMsgSeq = -1;
    } carry;
    QHash<QString, int> index;          // column name -> display position

    Table();
    int  columnCount() const { return columns.size() + 1; }   // + "Frame (hex)"
    QString header(int col) const;      // by display position
    QString cell(int row, int col) const;
    // The column's id; a new one is shown after the last of its group (the
    // Lead columns in their fixed order).
    int  columnOf(const QString &name, Group g = Group::Body);
};

// Session 250: what a cell reports, for the table to mark it. Fail: the
// packet reports equipment down (a radio, a link, a module, no keys) or its
// CRC did not match. Warn: an event worth reading (a brake application, an
// SoS, a missing tag, no GPS fix, a fault reported). Read from the decoded
// text, so it follows the decoder's labels. `why` says what was reported.
enum class Mark { None, Warn, Fail };
Mark markOf(const QString &column, const QString &value, QString *why = nullptr);

// Adds `entry` as a row when it is a well-formed capture line of `kind`.
// False (and nothing added) otherwise. *newColumns set when the packet
// brought a column the table did not have.
bool append(Table &t, Kind kind, const LogEntry &entry, bool *newColumns = nullptr);

// Header row, then one line per packet; RFC 4180 quoting, CRLF.
QString toCsv(const Table &t);
// One sheet named after the packet type.
QVector<XlsxBook::Sheet> toSheets(const Table &t, Kind kind);

}  // namespace NmsLog

#endif // NMSLOG_H
