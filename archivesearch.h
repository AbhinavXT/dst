#ifndef ARCHIVESEARCH_H
#define ARCHIVESEARCH_H

// =============================================================================
//  ArchiveSearch
//  -----------------------------------------------------------------------------
//  Run a LogQuery across recorded .dlr sessions on disk, without loading
//  them into the live models.
//
//  WHY
//    Find, filter and cross-tab search all operate on the in-memory ring —
//    at most a few hundred thousand rows, minutes to hours of traffic. The
//    question that actually settles an incident is usually "did this happen
//    before?", and the answer lives in last Tuesday's archive. Today that
//    means leaving the app and grepping, which works for the .log text and
//    not at all for anything needing severity, direction, or a re-decode.
//
//    Searching the .dlr rather than the .log is the point: records are
//    re-classified with the CURRENT colour rules as they are read, so a
//    rule you fixed this morning applies retroactively to every archived
//    session. Grepping the text would search last week's interpretation.
//
//  STREAMING, NOT LOADING
//    Files are read one record at a time through SessionReader and
//    discarded unless they match, so the memory cost is one record plus the
//    hits — a hundred gigabytes of archive searches in bounded memory. Only
//    the hit cap bounds the result set.
//
//  THREADING
//    The scan runs on a worker thread and takes its OWN COPY of the colour
//    rules. Classification reads them for every record; borrowing a pointer
//    to a MainWindow member would be the same latent cross-thread bug that
//    the log writer's NameMap had, and a schema or rules reload mid-search
//    would be enough to trip it.
// =============================================================================

#include <QDate>
#include <QStringList>
#include <QThread>
#include <QVector>

#include <functional>

#include "colorrules.h"
#include "logentry.h"
#include "logquery.h"

struct ArchiveHit {
    QString  filePath;
    QString  tabKey;
    qint64   epochMs  = 0;
    QString  text;
    Severity severity = Severity::Info;
};

struct ArchiveScanResult {
    QVector<ArchiveHit> hits;
    int  filesScanned   = 0;
    int  filesUnreadable = 0;
    qint64 recordsScanned = 0;
    bool hitCap    = false;
    bool cancelled = false;
    // Per-file problems (truncation, CRC), collected rather than fatal: a
    // partially readable archive is still evidence.
    QStringList warnings;
};

// Every .dlr under `root`, optionally restricted to the date-named
// subdirectories in [from, to]. Invalid dates mean "no bound".
//
// The date filter reads the DIRECTORY name (yyyy-MM-dd), not file
// timestamps: a copied or restored archive keeps its directory but not
// necessarily its mtimes, and the directory is what the writer used.
QStringList findSessionFiles(const QString &root,
                             const QDate &from = QDate(),
                             const QDate &to   = QDate());

// Scan the given files. `cancelled` is polled between records; `progress`
// is called per file with (filesDone, totalFiles). Both may be empty.
ArchiveScanResult scanArchive(const QStringList &files,
                              const LogQuery &query,
                              const ColorRules *rules,
                              int maxHits = 5000,
                              std::function<bool()> cancelled = {},
                              std::function<void(int, int)> progress = {});

class ArchiveSearcher : public QThread
{
    Q_OBJECT

public:
    ArchiveSearcher(const QStringList &files,
                    const QString &queryText,
                    const ColorRules &rules,     // copied, deliberately
                    int maxHits,
                    QObject *parent = nullptr);

    void cancel() { m_cancel.store(true, std::memory_order_relaxed); }

signals:
    void progress(int filesDone, int totalFiles);
    void finished(ArchiveScanResult result);

protected:
    void run() override;

private:
    QStringList m_files;
    QString     m_queryText;
    ColorRules  m_rules;                 // owned copy
    int         m_maxHits;
    std::atomic<bool> m_cancel{false};
};

#endif // ARCHIVESEARCH_H
