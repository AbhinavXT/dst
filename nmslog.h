#ifndef NMSLOG_H
#define NMSLOG_H

// =============================================================================
//  NMS packet logs (session 247) — Tools ▸ Monitor ▸ Equipment ▸
//  NMS health log… / NMS fault log… / NMS RSSI log…
//  -----------------------------------------------------------------------------
//  One table per NMS packet type, one row per packet, as the tab holds them
//  and then as they arrive. The columns are what CaptureDecoder::describe()
//  prints for the frame, in the order first seen:
//    @nmshlth  one column per health event; a packet that did not carry an
//              event leaves its cell empty
//    @nmsflt   reporting_subsystem, fault_count, then fault[1] module /
//              code_type / fault_id, fault[2] …, as many as a packet carried
//    @nmsrssi  its fixed fields
//  First the PC time, the loco's RTC and the capture sequence; then the
//  packet's own fields; then the capture line's loco / ctrl, length and CRC
//  result and the NMS header. Last: the frame in hex. Values are as the decoder shows them ("1  (OK)"); a cell that is a
//  whole number goes to the .xlsx as a number.
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

// Where a column sits: what says when the packet was, then the packet's
// own fields, then the capture line and NMS header details.
enum class Group { Lead, Body, Details };

struct Table {
    QStringList columns;                // by id, in the order first seen
    QVector<Group> groups;              // by id
    QVector<int> order;                 // display position -> id
    QVector<QStringList> rows;          // by id; each as long as `columns` was when it was added
    QVector<QString> hex;               // per row: the frame, the last column shown
    QVector<qint64> epochMs;            // per row: the entry's time, for jumping to it
    QHash<QString, int> index;          // column name -> display position

    Table();
    int  columnCount() const { return columns.size() + 1; }   // + "Frame (hex)"
    QString header(int col) const;      // by display position
    QString cell(int row, int col) const;
    // The column's id; a new one is shown after the last of its group.
    int  columnOf(const QString &name, Group g = Group::Body);
};

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
