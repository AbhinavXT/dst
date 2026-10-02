#include "exporter.h"
#include "namemap.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QStringList>
#include <QTextStream>

#include <functional>

namespace {

// Map enum to short stable strings used in both formats. We don't reuse
// LogModel's display strings because those have padding, are translatable,
// and aren't stable for downstream consumers.
const char *severityKey(Severity s) {
    switch (s) {
    case Severity::Info:  return "info";
    case Severity::Warn:  return "warn";
    case Severity::Error: return "error";
    }
    return "info";
}
const char *directionKey(LogDirection d) {
    switch (d) {
    case LogDirection::None: return "";
    case LogDirection::In:   return "in";
    case LogDirection::Out:  return "out";
    }
    return "";
}

// ISO-8601 with millisecond precision, local time + offset. Same form a
// log analysis pipeline can re-parse without effort.
QString isoTime(qint64 epochMs)
{
    return QDateTime::fromMSecsSinceEpoch(epochMs)
        .toString(Qt::ISODateWithMs);
}

// Hex dump a byte array as space-separated lowercase pairs:
//   "21 65 03 0d 00 01 00 ..."
QString hexBytes(const QByteArray &b)
{
    QString out;
    out.reserve(b.size() * 3);
    for (int i = 0; i < b.size(); ++i) {
        if (i > 0) out += ' ';
        out += QString("%1").arg(static_cast<quint8>(b[i]),
                                  2, 16, QChar('0'));
    }
    return out;
}

}  // anonymous namespace


Exporter::Exporter(const QString &outputPath,
                   Format format,
                   QVector<LogEntryPtr> entries,
                   const NameMap *names,
                   ColumnFlags columns,
                   QObject *parent)
    : QThread(parent)
    , m_path(outputPath)
    , m_format(format)
    , m_entries(std::move(entries))
    , m_names(names)
    , m_columns(columns)
{
}

void Exporter::run()
{
    // Make sure the parent directory exists. Operators tend to type
    // paths like "C:/exports/today/log.csv" and expect us to do the
    // mkdir for them.
    QFileInfo fi(m_path);
    QDir parent(fi.absolutePath());
    if (!parent.exists()) {
        if (!parent.mkpath(".")) {
            emit exportFailed(m_path, tr("Cannot create directory %1")
                                          .arg(parent.absolutePath()));
            return;
        }
    }

    bool ok = false;
    switch (m_format) {
    case CSV:  ok = writeCsv (m_path); break;
    case JSON: ok = writeJson(m_path); break;
    }

    if (ok) {
        emit exportFinished(m_path, m_entries.size());
    }
    // writeXxx already emitted exportFailed on its way out if it returned
    // false; no further action needed here.
}

bool Exporter::writeCsv(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit exportFailed(path, f.errorString());
        return false;
    }

    // UTF-8 BOM for Excel.
    f.write("\xEF\xBB\xBF", 3);

    QTextStream out(&f);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
#endif

    // Build header + per-row column producer pairs in lockstep, so they
    // can't go out of sync.
    //
    // Each entry is (header_string, value_extractor). The value extractor
    // is a small lambda that returns the column's value for one
    // LogEntry. We assemble these once based on m_columns flags, then
    // walk entries and join.
    //
    // Note: ColRawBytes intentionally not exposed to CSV — RFC 4180
    // doesn't have an obvious "long binary blob" idiom and operators
    // who want bytes should use JSON. The flag is silently ignored
    // here.
    using Producer = std::function<QString(const LogEntryPtr&)>;
    QVector<QPair<QString, Producer>> cols;

    if (m_columns & ColTime) {
        cols.append({"time_iso",
                     [](const LogEntryPtr &e) { return isoTime(e->epochMs); }});
    }
    if (m_columns & ColSource) {
        cols.append({"source",
                     [](const LogEntryPtr &e) {
                         return QString("%1_%2")
                             .arg(int(e->header.source_id))
                             .arg(int(e->header.kvchId));
                     }});
    }
    if (m_columns & ColFriendly) {
        const NameMap *names = m_names;
        cols.append({"friendly",
                     [names](const LogEntryPtr &e) {
                         const QString src = QString("%1_%2")
                             .arg(int(e->header.source_id))
                             .arg(int(e->header.kvchId));
                         return names ? names->lookupByKey(src) : src;
                     }});
    }
    if (m_columns & ColDirection) {
        cols.append({"direction",
                     [](const LogEntryPtr &e) {
                         return QString::fromLatin1(directionKey(e->direction));
                     }});
    }
    if (m_columns & ColSeverity) {
        cols.append({"severity",
                     [](const LogEntryPtr &e) {
                         return QString::fromLatin1(severityKey(e->severity));
                     }});
    }
    if (m_columns & ColMessage) {
        cols.append({"message",
                     [](const LogEntryPtr &e) { return e->text; }});
    }

    if (cols.isEmpty()) {
        // User unticked everything. Don't write a malformed file with
        // just a header and no data — write an empty UTF-8 BOM file
        // and bail with a friendly error.
        f.close();
        emit exportFailed(path, tr("No columns selected for export."));
        return false;
    }

    // Header row.
    QStringList headerParts;
    headerParts.reserve(cols.size());
    for (const auto &c : cols) headerParts.append(c.first);
    out << headerParts.join(',') << '\n';

    // Data rows.
    for (const LogEntryPtr &e : m_entries) {
        if (!e) continue;
        QStringList rowParts;
        rowParts.reserve(cols.size());
        for (const auto &c : cols) {
            rowParts.append(csvEscape(c.second(e)));
        }
        out << rowParts.join(',') << '\n';
    }
    out.flush();
    f.close();
    return true;
}

bool Exporter::writeJson(const QString &path)
{
    QJsonArray arr;
    // QJsonArray::reserve was added in Qt 5.15; older versions just grow.
    // The cost is negligible for our scale (max 200k entries) so we don't
    // bother with a version-conditional.

    // Quick check: if everything is off, refuse rather than write `[]`
    // which may surprise an operator who untoggled by accident.
    if ((m_columns & ColAll) == 0) {
        emit exportFailed(path, tr("No columns selected for export."));
        return false;
    }

    for (const LogEntryPtr &e : m_entries) {
        if (!e) continue;
        const QString src = QString("%1_%2")
                                .arg(int(e->header.source_id))
                                .arg(int(e->header.kvchId));
        const QString friendly = m_names ? m_names->lookupByKey(src) : src;

        QJsonObject o;
        // Each field is included only if its column flag is set. This
        // means the output JSON has different shapes for different
        // export configs — that's fine, callers can use Object.keys()
        // to introspect what's there.
        if (m_columns & ColTime) {
            // double has 53-bit mantissa — exact representation of
            // ms-since-epoch out to ~year 285,000. Safe.
            o["time_ms"]  = double(e->epochMs);
            o["time_iso"] = isoTime(e->epochMs);
        }
        if (m_columns & ColSource) {
            o["source_id"] = int(e->header.source_id);
            o["kvch_id"]   = int(e->header.kvchId);
        }
        if (m_columns & ColFriendly) {
            o["friendly"] = friendly;
        }
        if (m_columns & ColDirection) {
            o["direction"] = directionKey(e->direction);
        }
        if (m_columns & ColSeverity) {
            o["severity"] = severityKey(e->severity);
        }
        if (m_columns & ColMessage) {
            o["message"] = e->text;
        }
        if (m_columns & ColRawBytes) {
            o["raw_bytes_hex"] = hexBytes(e->rawBytes);
        }
        arr.append(o);
    }

    QJsonDocument doc(arr);
    const QByteArray bytes = doc.toJson(QJsonDocument::Indented);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit exportFailed(path, f.errorString());
        return false;
    }
    if (f.write(bytes) != bytes.size()) {
        emit exportFailed(path, f.errorString());
        f.close();
        return false;
    }
    f.close();
    return true;
}

QString Exporter::csvEscape(const QString &field)
{
    // Spec: a field containing a comma, double-quote, CR, or LF must be
    // wrapped in double quotes, with each internal double-quote doubled.
    bool needsQuote = false;
    for (QChar c : field) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            needsQuote = true;
            break;
        }
    }
    if (!needsQuote) return field;

    QString out;
    out.reserve(field.size() + 4);
    out += '"';
    for (QChar c : field) {
        if (c == '"') out += '"';   // double up
        out += c;
    }
    out += '"';
    return out;
}
