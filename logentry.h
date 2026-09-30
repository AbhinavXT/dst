#ifndef LOGENTRY_H
#define LOGENTRY_H

// =============================================================================
//  LogEntry
//  -----------------------------------------------------------------------------
//  The single in-memory representation of one received message, carried
//  end-to-end from the UDP receiver, through the dispatcher, into the per-tab
//  model, and out to disk via SaveData.
//
//  The original code shuffled three different shapes around (STRUCT_MQBUF,
//  STRUCT_DLC_PAYLOAD, and HTML-tagged QString rows in the model). Every
//  conversion was a place a bug could hide. After this refactor there is
//  exactly one shape, and the conversions happen at the edges only:
//      bytes off the wire  →  LogEntry  (UDPCommunication)
//      LogEntry            →  bytes on disk (SaveData)
//      LogEntry            →  pixels    (LogModel + delegate)
//
//  Constraints honored:
//    – `header` is the EXACT wire header (STRUCT_MESSAGE_HEADER) byte-for-byte,
//      already byte-swapped to host order. This is the user's hard requirement.
//    – `rawBytes` is the entire datagram (header + payload), preserved so
//      Patch B can offer a raw hex/byte side panel.
// =============================================================================

#include <QByteArray>
#include <QColor>
#include <QDateTime>
#include <QVector>
#include <QMetaType>
#include <QSharedPointer>
#include <QString>

#include "Structures.h"

// Severity tags (computed once at ingest time by ColorRules; cheap to filter
// on later). Kept as a plain enum so it can be a model role value directly.
enum class Severity : quint8 {
    Info  = 0,
    Warn  = 1,
    Error = 2
};

// Direction tags (likewise computed once). The original code reasoned about
// IN/OUT via QString::contains — now it's a typed field on the row.
enum class Direction : quint8 {
    None = 0,
    In   = 1,
    Out  = 2
};

struct LogEntry
{
    // Wall-clock time of receipt, milliseconds since Unix epoch. Used by the
    // model's "Time" column and by future time-range filters.
    qint64 epochMs = 0;

    // EXACT wire header, in host byte order. Never reformat or repack — this
    // is what gets handed to disk-side tooling and to the raw-bytes panel.
    STRUCT_MESSAGE_HEADER header{};

    // The decoded payload as a string. We assume UTF-8 because the existing
    // backend sends ASCII diagnostic text. If a future Kavach payload is not
    // text, rawBytes is still authoritative.
    QString text;

    // The complete datagram (header + payload) as it arrived, byte-for-byte.
    // Used by the "show raw bytes" feature in Patch B and by anyone who needs
    // to re-derive anything we might have parsed wrong.
    QByteArray rawBytes;

    // Derived tags. Computed once at insert time by ColorRules::classify().
    Severity  severity  = Severity::Info;
    Direction direction = Direction::None;

    // Colors from the matched ColorRule, copied verbatim. Patch C: each
    // entry now stores BOTH light-theme and dark-theme variants. The
    // model picks one set or the other at paint time based on the
    // currently-active theme. Any of the four can be invalid (the QColor
    // default), in which case the model falls back to a severity-derived
    // color or the system default.
    QColor    fgLight;
    QColor    bgLight;
    QColor    fgDark;
    QColor    bgDark;

    // The "21_1" routing key, and the "HH:mm:ss.zzz" display time.
    //
    // Both are built ONCE, by cacheDerived(), at ingest. They used to be
    // recomputed inside LogModel::data() — which the view calls per visible
    // cell, per repaint. tabKey() allocated a QString and ran two arg()
    // substitutions; the Friendly column then fed that into a hash lookup;
    // and formatTime() constructed a QDateTime and did a full timezone
    // conversion. An entry is built once and painted hundreds of times, so
    // this belongs on the ingest side.
    //
    // NOTE: both halves are DECIMAL. An older comment here claimed source_id
    // was rendered as lowercase hex; it never was, and NameMap normalises
    // its CSV keys to decimal to match, so a friendly_names.csv written
    // against the comment rather than the code would silently never match.
    QString cachedTabKey;
    QString cachedTime;

    // Operator bookmark. Lives on the entry purely so painting is a field
    // read rather than a hash lookup per cell per repaint; the durable
    // record is BookmarkStore, which survives eviction and restart.
    bool bookmarked = false;

    // ---- decoded-field cache (see fieldindex.h) -----------------------
    //
    // Populated lazily by FieldIndex on the first field-query that touches
    // this entry, and only then: schema decoding is by far the most
    // expensive thing that could be done per message, and almost no message
    // is ever queried by field.
    //
    // `mutable` because querying is conceptually a read. Guarded by nothing:
    // every toucher is on the GUI thread today. The archive-search worker
    // builds its own entries and drops them, so it never shares one of
    // these. If a background thread ever queries a LIVE entry, this needs a
    // lock or a per-thread cache.
    mutable bool                     fieldsDecoded = false;
    mutable QVector<QPair<QString, QString>> decodedFields;

    void cacheDerived(bool utc = false)
    {
        cachedTabKey = QString("%1_%2")
                           .arg(static_cast<int>(header.source_id))
                           .arg(static_cast<int>(header.kvchId));
        // Rendered in whichever zone the UI is currently showing. `utc` is
        // passed in rather than read from Settings here because this runs
        // once per message on the ingest path, and a QSettings lookup per
        // message would be absurd.
        cachedTime = (utc ? QDateTime::fromMSecsSinceEpoch(epochMs, Qt::UTC)
                          : QDateTime::fromMSecsSinceEpoch(epochMs))
                         .toString(QStringLiteral("HH:mm:ss.zzz"));
    }

    QString tabKey() const
    {
        // Fall back to computing it for entries built by paths that predate
        // cacheDerived() (tests, replay import).
        if (!cachedTabKey.isEmpty()) return cachedTabKey;
        return QString("%1_%2")
                .arg(static_cast<int>(header.source_id))
                .arg(static_cast<int>(header.kvchId));
    }
};

// Heap-allocated, ref-counted; that way we never copy the QByteArray when
// the dispatcher hands the entry to multiple subscribers (main tab + a
// detached console mirroring the same source).
using LogEntryPtr = QSharedPointer<LogEntry>;

// Required so a LogEntryPtr can travel across a Qt::QueuedConnection (the
// queued connection is what we use between the UDP thread and the GUI
// thread). The matching qRegisterMetaType<>() call is in main().
Q_DECLARE_METATYPE(LogEntryPtr)

// Render entries as tab-separated text for the clipboard.
//
// Tab-separated rather than the log file's fixed-width columns: the
// destination is a report, a spreadsheet or a ticket, and all three want
// delimited fields. Fixed-width would paste as one mangled column.
//
// Severity is written as the plain word, NOT the glyph the table shows.
// The glyph exists so severity is not colour-only on screen; pasted into a
// spreadsheet it would just be a character nothing can filter on.
//
// `utc` matches whatever the table is currently displaying, so a pasted
// timestamp says the same thing as the row it came from.
QString formatEntriesForClipboard(const QVector<LogEntryPtr> &entries,
                                  bool includeHeader,
                                  bool utc);

// Just the messages, one per line. What Ctrl+C gives.
//
// The columns beside the message — time, source, direction, severity —
// exist to help FIND the row. Once it is found, what gets pasted into a
// decoder, a ticket or a mail is the message itself, and the four columns
// in front of it are four columns to delete by hand. The tab-separated form
// above is still a keystroke away for the times a report wants them.
//
// Tabs and newlines inside a message are flattened for the same reason they
// are there: a message spanning two lines would paste as two rows and be
// counted as two.
QString formatMessagesForClipboard(const QVector<LogEntryPtr> &entries);

// The buffer of one entry, in the form the frame-oriented tools take as
// input (Decode Workbench, Packet Maker, or a paste into anything else).
//
// A capture row already IS such an input: "@slrp_1_1 <ts> <seq> <hex…>",
// and its @tag names the packet type, which is the one thing neither tool
// can infer reliably. So a capture row is passed through whole rather than
// reduced to bytes. Anything else falls back to the raw datagram as
// space-separated upper-case hex — header included, because trimming it is
// a judgement the operator can make and this function cannot.
//
// Empty when the entry is null or carries no bytes at all.
QString entryBufferText(const LogEntryPtr &entry);

#endif // LOGENTRY_H
