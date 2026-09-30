#include "archivesearch.h"

#include "messagedispatcher.h"
#include "sessionfile.h"
#include "sessionreader.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

QStringList findSessionFiles(const QString &root,
                             const QDate &from,
                             const QDate &to)
{
    QStringList out;
    if (root.isEmpty()) return out;

    QDir rootDir(root);
    if (!rootDir.exists()) return out;

    const QStringList dayDirs =
        rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    for (const QString &day : dayDirs) {
        // A directory whose name is not a date is not ours; skip rather
        // than guess. The writer always names them yyyy-MM-dd.
        const QDate d = QDate::fromString(day, QStringLiteral("yyyy-MM-dd"));
        if (!d.isValid()) continue;
        if (from.isValid() && d < from) continue;
        if (to.isValid()   && d > to)   continue;

        QDir dd(rootDir.filePath(day));
        const QStringList files =
            dd.entryList({ QStringLiteral("*%1").arg(SessionFile::kExtension) },
                         QDir::Files, QDir::Name);
        for (const QString &f : files) out << dd.filePath(f);
    }

    // Loose .dlr files directly under the root — an archive someone copied
    // out of its day directory is still searchable, and silently ignoring
    // it would be the wrong kind of tidy.
    const QStringList loose =
        rootDir.entryList({ QStringLiteral("*%1").arg(SessionFile::kExtension) },
                          QDir::Files, QDir::Name);
    for (const QString &f : loose) out << rootDir.filePath(f);

    return out;
}

ArchiveScanResult scanArchive(const QStringList &files,
                              const LogQuery &query,
                              const ColorRules *rules,
                              int maxHits,
                              std::function<bool()> cancelled,
                              std::function<void(int, int)> progress)
{
    ArchiveScanResult res;
    if (maxHits <= 0) maxHits = 1;

    int fileIndex = 0;
    for (const QString &path : files) {
        ++fileIndex;
        if (progress) progress(fileIndex, files.size());

        if (cancelled && cancelled()) { res.cancelled = true; break; }

        SessionReader reader;
        if (!reader.open(path)) {
            ++res.filesUnreadable;
            res.warnings << QStringLiteral("%1: %2")
                                .arg(QFileInfo(path).fileName(),
                                     reader.errorString());
            continue;
        }
        ++res.filesScanned;

        const QString key = reader.tabKey();

        while (reader.next()) {
            if (cancelled && cancelled()) { res.cancelled = true; break; }
            ++res.recordsScanned;

            // Rebuild through the same canonical conversion live ingest
            // uses, so an archived record is classified exactly as it would
            // be if it arrived now — with the CURRENT rules, which is the
            // whole reason to search the .dlr instead of the .log.
            LogEntryPtr e = MessageDispatcher::buildEntry(
                reader.wire(), reader.arrivalMs(), rules);
            if (!e) continue;

            if (!query.match(*e, nullptr)) continue;

            ArchiveHit h;
            h.filePath = path;
            h.tabKey   = key.isEmpty() ? e->tabKey() : key;
            h.epochMs  = e->epochMs;
            h.text     = e->text;
            h.severity = e->severity;
            res.hits.append(h);

            if (res.hits.size() >= maxHits) { res.hitCap = true; break; }
        }

        // Truncation is normal for a file the app was killed while writing;
        // report it without treating the file as a failure, since every
        // record before the tear was read successfully.
        switch (reader.status()) {
        case SessionReader::TruncatedTail:
            res.warnings << QStringLiteral("%1: ends mid-record (interrupted "
                                           "while writing)")
                                .arg(QFileInfo(path).fileName());
            break;
        case SessionReader::Corrupt:
            res.warnings << QStringLiteral("%1: %2")
                                .arg(QFileInfo(path).fileName(),
                                     reader.errorString());
            break;
        default:
            break;
        }

        if (res.hitCap || res.cancelled) break;
    }

    // Interleave by time across files. Each file is internally ordered, but
    // sources are separate files, so without this the results would read as
    // "everything source A said, then everything B said" — which is exactly
    // the grouping the merged view exists to undo.
    std::stable_sort(res.hits.begin(), res.hits.end(),
                     [](const ArchiveHit &a, const ArchiveHit &b) {
                         return a.epochMs < b.epochMs;
                     });
    return res;
}

ArchiveSearcher::ArchiveSearcher(const QStringList &files,
                                 const QString &queryText,
                                 const ColorRules &rules,
                                 int maxHits,
                                 QObject *parent)
    : QThread(parent)
    , m_files(files)
    , m_queryText(queryText)
    , m_rules(rules)          // copy; see the header
    , m_maxHits(maxHits)
{
}

void ArchiveSearcher::run()
{
    // Parsed here rather than passed in, so the parse happens on this
    // thread along with everything else it feeds.
    LogQuery query;
    if (!query.parse(m_queryText)) {
        ArchiveScanResult bad;
        bad.warnings << QStringLiteral("query error: %1").arg(query.errorString());
        emit finished(bad);
        return;
    }

    ArchiveScanResult res = scanArchive(
        m_files, query, &m_rules, m_maxHits,
        [this]() { return m_cancel.load(std::memory_order_relaxed); },
        [this](int done, int total) { emit progress(done, total); });

    emit finished(res);
}
