#ifndef EXPORTER_H
#define EXPORTER_H

// =============================================================================
//  Exporter
//  -----------------------------------------------------------------------------
//  Worker thread that writes the contents of a tab to either CSV or JSON.
//
//  The caller (MainWindow) snapshots the entries from the proxy model
//  (so the export honors any active filter) and hands the QVector to
//  the exporter. From that point on the GUI thread is free; the
//  exporter does its own file I/O on its own thread and emits
//  exportFinished() on completion.
//
//  CSV format:
//    – RFC 4180 quoting: any field containing a comma, double-quote,
//      newline, or carriage return is wrapped in double quotes; embedded
//      double quotes are escaped by doubling.
//    – UTF-8 with BOM. Excel on Windows opens UTF-8 files as CP1252
//      unless there's a BOM, so non-ASCII text gets garbled. The BOM
//      fixes that for Excel without breaking other tools.
//    – Columns: time_iso, source, friendly, direction, severity, message.
//      Same shape as the on-screen table (minus the time column, which is
//      always included here regardless of UI hide state).
//
//  JSON format:
//    – Pretty-printed array of objects.
//    – Fields: time_ms, time_iso, source_id, kvch_id, friendly,
//      direction, severity, message, raw_bytes_hex.
//    – raw_bytes_hex is the full datagram (header + payload) as a
//      space-separated hex string, e.g. "21 65 03 0d 00 01 00 ...".
//      That gives the consumer everything they'd need to reconstruct
//      the wire packet byte-for-byte.
//
//  Threading:
//    Mirrors SaveData's pattern. Subclass QThread, override run(), emit
//    exportFinished on success, exportFailed on error. Caller wires
//    QThread::finished → deleteLater for cleanup.
// =============================================================================

#include <QString>
#include <QThread>
#include <QVector>

#include "logentry.h"

class NameMap;

class Exporter : public QThread
{
    Q_OBJECT

public:
    enum Format { CSV, JSON };

    // Per-column include flags. The dialog builds these from checkboxes;
    // the writers consult before emitting each column. The default
    // (ColAll) reproduces pre-feature behavior exactly.
    enum ColumnFlag : unsigned {
        ColTime      = 1u << 0,   // CSV: time_iso ; JSON: time_iso + time_ms
        ColSource    = 1u << 1,   // CSV: source ; JSON: source_id + kvch_id
        ColFriendly  = 1u << 2,
        ColDirection = 1u << 3,
        ColSeverity  = 1u << 4,
        ColMessage   = 1u << 5,
        ColRawBytes  = 1u << 6,   // JSON only: raw_bytes_hex; CSV ignores

        ColAll = ColTime | ColSource | ColFriendly | ColDirection
               | ColSeverity | ColMessage | ColRawBytes
    };
    using ColumnFlags = unsigned;

    Exporter(const QString &outputPath,
             Format format,
             QVector<LogEntryPtr> entries,
             const NameMap *names,             // borrowed; nullable
             ColumnFlags columns = ColAll,     // default = include everything
             QObject *parent = nullptr);

signals:
    void exportFinished(QString outputPath, int rows);
    void exportFailed  (QString outputPath, QString reason);

protected:
    void run() override;

private:
    bool writeCsv (const QString &path);
    bool writeJson(const QString &path);

    static QString csvEscape(const QString &field);

    QString               m_path;
    Format                m_format;
    QVector<LogEntryPtr>  m_entries;
    const NameMap        *m_names;
    ColumnFlags           m_columns;
};

#endif // EXPORTER_H
