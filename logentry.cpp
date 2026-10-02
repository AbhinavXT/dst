#include "logentry.h"

#include <QRegularExpression>
#include <QVector>

#include <cctype>

#include <QStringList>

// Is this message already a frame written out in hex?
//
// The test is deliberately strict, because the cost of a false positive is
// handing the Decode Workbench the wrong bytes with no sign that anything
// went wrong. Every whitespace-separated token must be exactly two hex
// digits, and there must be enough of them to be a frame rather than a
// number that happens to look like one — "DE AD" in a sentence is not a
// packet, and no packet worth decoding is two bytes long.
static bool looksLikeHexDump(const QString &text)
{
    const QVector<QStringRef> tokens =
        text.splitRef(QRegularExpression(QStringLiteral("\\s+")),
                      Qt::SkipEmptyParts);
    if (tokens.size() < 8) { return false; }

    for (const QStringRef &t : tokens) {
        if (t.size() != 2) { return false; }
        for (int i = 0; i < 2; ++i) {
            if (!isxdigit(t.at(i).toLatin1())) { return false; }
        }
    }
    return true;
}

QString entryBufferText(const LogEntryPtr &entry)
{
    if (!entry) { return QString(); }

    const QString text = entry->text.trimmed();
    if (text.startsWith(QLatin1Char('@'))) { return text; }

    // A message that IS a hex dump wins over rawBytes.
    //
    // rawBytes is the datagram this row arrived in: the transport header
    // followed by the payload, kept verbatim. When the payload is binary
    // that is exactly what someone wants to decode. When the payload is a
    // frame the backend already printed as text, it is not — handing over
    // rawBytes there gives the envelope plus the ASCII CODES of the hex
    // digits, so "02 07 0D…" arrived at the workbench as
    // "…00 02 00 30 32 20 30 37…" and decoded as nonsense.
    //
    // The frame the operator is pointing at is the one in the message.
    if (looksLikeHexDump(text)) { return text.toUpper(); }

    if (!entry->rawBytes.isEmpty()) {
        return QString::fromLatin1(entry->rawBytes.toHex(' ')).toUpper();
    }
    return QString();
}

QString formatEntriesForClipboard(const QVector<LogEntryPtr> &entries,
                                  bool includeHeader,
                                  bool utc)
{
    auto sevWord = [](Severity s) {
        return s == Severity::Error ? QStringLiteral("ERROR")
             : s == Severity::Warn  ? QStringLiteral("WARN")
                                    : QStringLiteral("INFO");
    };
    auto dirWord = [](LogDirection d) {
        return d == LogDirection::In  ? QStringLiteral("IN")
             : d == LogDirection::Out ? QStringLiteral("OUT")
                                   : QString();
    };

    QStringList lines;
    if (includeHeader) {
        lines << QStringLiteral("Time\tSource\tDirection\tSeverity\tMessage");
    }
    for (const LogEntryPtr &e : entries) {
        if (!e) continue;
        const QDateTime dt = utc
            ? QDateTime::fromMSecsSinceEpoch(e->epochMs, Qt::UTC)
            : QDateTime::fromMSecsSinceEpoch(e->epochMs);

        // Tabs and newlines inside a message would break the row structure
        // of whatever this is pasted into, so they are flattened to spaces.
        // The text is already newline-flattened at ingest; this guards the
        // paths that are not (replayed rows, hand-built entries).
        QString text = e->text;
        text.replace(QLatin1Char('\t'), QLatin1Char(' '));
        text.replace(QLatin1Char('\n'), QLatin1Char(' '));
        text.replace(QLatin1Char('\r'), QLatin1Char(' '));

        lines << QStringLiteral("%1\t%2\t%3\t%4\t%5")
                     .arg(dt.toString(Qt::ISODateWithMs),
                          e->tabKey(),
                          dirWord(e->direction),
                          sevWord(e->severity),
                          text);
    }
    return lines.join(QLatin1Char('\n'));
}

QString formatMessagesForClipboard(const QVector<LogEntryPtr> &entries)
{
    QStringList lines;
    lines.reserve(entries.size());
    for (const LogEntryPtr &e : entries) {
        if (!e) { continue; }
        QString text = e->text;
        text.replace(QLatin1Char('\t'), QLatin1Char(' '));
        text.replace(QLatin1Char('\n'), QLatin1Char(' '));
        text.replace(QLatin1Char('\r'), QLatin1Char(' '));
        lines << text;
    }
    return lines.join(QLatin1Char('\n'));
}
