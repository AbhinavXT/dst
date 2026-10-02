#include "logwriter.h"
#include "namemap.h"
#include "sessionfile.h"

#include <QDateTime>
#include <QDirIterator>
#include <QStorageInfo>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QTextStream>

namespace {

// Severity tags, padded to width 4 for column alignment in the log file.
// "INFO" / "WARN" / "ERR " / "    " — same width so the columns after
// this one always start at the same offset on every line.
QString severityTag(Severity s)
{
    switch (s) {
    case Severity::Info:  return QStringLiteral("INFO");
    case Severity::Warn:  return QStringLiteral("WARN");
    case Severity::Error: return QStringLiteral("ERR ");
    }
    return QStringLiteral("    ");
}

// Direction tags, padded to width 3 for column alignment.
QString directionTag(LogDirection d)
{
    switch (d) {
    case LogDirection::In:   return QStringLiteral("IN ");
    case LogDirection::Out:  return QStringLiteral("OUT");
    case LogDirection::None: return QStringLiteral("   ");
    }
    return QStringLiteral("   ");
}

// Pad `s` on the right with spaces up to `width`. If `s` is already
// longer than `width`, return it unchanged — truncation would silently
// lose information (e.g. a friendly name like
// "LK_VeryLongControllerName" would lose its tail), and we'd rather
// see one misaligned row than miss data.
QString padRight(const QString &s, int width)
{
    if (s.size() >= width) return s;
    return s + QString(width - s.size(), ' ');
}

}  // anonymous namespace


LogWriter::LogWriter(const QString &rootDir, qint64 maxFileBytes, QObject *parent)
    : QThread(parent)
    , m_rootDir(rootDir)
    , m_maxFileBytes(maxFileBytes > 0 ? maxFileBytes : 1)
{
    // Note: do NOT touch the filesystem here. Caller is on the GUI thread;
    // all I/O happens inside run().
}

LogWriter::~LogWriter()
{
    // Last-resort join. Callers are expected to have called stop() and
    // checked its result; if we get here with the thread still running we
    // give it one more chance, then terminate rather than let ~QThread
    // abort the process. Terminating mid-write can truncate one line —
    // strictly better than taking the whole app down on shutdown.
    if (!stop(5000) && isRunning()) {
        qWarning() << "LogWriter: worker did not stop; terminating. "
                      "The last few log lines may be lost.";
        terminate();
        wait(1000);
    }
}

bool LogWriter::stop(int waitMs)
{
    if (!isRunning()) return true;

    {
        QMutexLocker lk(&m_queueMutex);
        m_stopRequested.store(true, std::memory_order_relaxed);
    }
    m_queueCv.wakeAll();

    // The return value matters: on false the thread is STILL RUNNING and
    // this object must not be destroyed. Previously this was discarded, so
    // a slow disk at shutdown meant delete-on-running-QThread (fatal) and,
    // because the name map was borrowed from a MainWindow member, a
    // use-after-free in the worker on the way out.
    return wait(waitMs);
}

void LogWriter::enqueue(const LogEntryPtr &entry)
{
    if (!entry) return;
    if (m_stopRequested.load(std::memory_order_relaxed)) return;

    {
        QMutexLocker lk(&m_queueMutex);
        m_queue.enqueue(entry);
    }
    // Wake the worker up so it doesn't have to wait the full 100ms tick
    // to see the new entry. This makes "burst arrives, app shuts down 50ms
    // later" not lose the burst.
    m_queueCv.wakeOne();
}

void LogWriter::enqueueBatch(const QVector<LogEntryPtr> &entries)
{
    if (entries.isEmpty()) return;
    if (m_stopRequested.load(std::memory_order_relaxed)) return;

    {
        QMutexLocker lk(&m_queueMutex);
        for (const LogEntryPtr &e : entries) {
            if (e) m_queue.enqueue(e);
        }
    }
    m_queueCv.wakeOne();
}

void LogWriter::run()
{
    while (true) {
        // Take a local copy of the queue under the mutex, then process
        // outside the lock. We also check the stop flag while holding
        // the lock so the producer can't slip a final enqueue past us
        // between the check and the drain.
        QQueue<LogEntryPtr> local;
        bool shouldStop = false;
        {
            QMutexLocker lk(&m_queueMutex);
            // Wait up to 100ms for either an enqueue or a stop request.
            if (m_queue.isEmpty()
                && !m_stopRequested.load(std::memory_order_relaxed)) {
                m_queueCv.wait(&m_queueMutex, 100);
            }
            local.swap(m_queue);
            shouldStop = m_stopRequested.load(std::memory_order_relaxed);
        }

        // Evaluate the folder budget BEFORE writing this batch, so the
        // first batch after the ceiling is reached is the one that gets
        // dropped rather than the one that pushes us over it. The wait
        // above times out every 100ms, so this runs on a steady tick even
        // when no traffic is arriving — which is what performs the initial
        // scan shortly after start(), and what notices an operator freeing
        // space on an idle console.
        updateQuotaState();

        // Drain whatever we just took. Important: this happens AFTER we
        // observed the stop flag, so a final enqueue between "stop set"
        // and "we read the queue" is still in `local` and gets written.
        while (!local.isEmpty()) {
            writeEntry(local.dequeue());
        }
        flushAll();

        if (shouldStop) {
            // One more pass under the lock to catch any entry that got
            // queued AFTER our drain and BEFORE we observed the stop
            // flag. This is the belt-and-braces final flush.
            QQueue<LogEntryPtr> tail;
            {
                QMutexLocker lk(&m_queueMutex);
                tail.swap(m_queue);
            }
            while (!tail.isEmpty()) {
                writeEntry(tail.dequeue());
            }
            flushAll();
            break;
        }
    }

    closeAll();
}

void LogWriter::writeEntry(const LogEntryPtr &entry)
{
    if (!entry) return;

    const QString key = entry->tabKey();

    // Budget reached: drop the entry rather than writing it. The queue is
    // still drained normally by run(), so entries are discarded here at the
    // last possible moment instead of being allowed to pile up in memory —
    // a suspended disk log must not turn into an unbounded RAM leak.
    if (m_quotaSuspended.load(std::memory_order_relaxed)) return;

    const QString basename = sanitizeFilename(m_names.lookupByKey(key));

    const QString todayDir = currentDateDir();

    evictIdleFilesIfNeeded();

    OpenFile &of = m_files[key];
    of.lastUsed = ++m_useTick;

    const bool wasFreshOpen = rotateIfNeeded(of, todayDir, basename, key, entry);

    // Raw first, and BEFORE the text stream's health check. The two streams
    // fail independently by design: a poisoned .log (disk full on that file,
    // a permissions change) must not also stop the archive that can still be
    // re-decoded. rotateIfNeeded has already emitted the .dlr file header if
    // the sidecar was freshly created.
    writeRawRecord(of, entry);

    if (of.poisoned || !of.file) return;

    // Emit the file header block for newly-opened files. Done HERE (in
    // writeEntry, after rotateIfNeeded reported a fresh open) rather
    // than inside rotateIfNeeded itself, because the header needs an
    // example entry to fill in the source_id / kvch_id / friendly /
    // created fields, and rotateIfNeeded doesn't have one. The first
    // entry to land in a new file conveniently has all the info we
    // need.
    if (wasFreshOpen) {
        writeHeader(of, key, entry);
    }

    // ---- Line format (column-aligned, fixed widths) ------------------
    //
    //   FIELD          WIDTH  EXAMPLE
    //   ─────────────  ─────  ────────────────────────────
    //   time_iso       23     2026-05-09T21:07:19.558
    //   <2 spaces>      2
    //   source_key      9     33_1
    //   <2 spaces>      2
    //   friendly       16     LK_1_VCC_1
    //   <2 spaces>      2
    //   direction       3     IN  / OUT / (blank)
    //   <2 spaces>      2
    //   severity        4     INFO / WARN / ERR / (blank)
    //   <2 spaces>      2
    //   message        ...    free-form text (last field, unpadded)
    //
    // Total prefix before message: 23 + 2 + 9 + 2 + 16 + 2 + 3 + 2 + 4 + 2 = 65 chars
    //
    // Widths chosen to fit common values without truncation:
    //   – source_key: "255_65535" is the widest possible (9 chars)
    //   – friendly:   most controller names fit in 16; longer names
    //                  overflow gracefully (one misaligned row) rather
    //                  than being truncated
    //   – direction:  "OUT" is the widest (3)
    //   – severity:   "INFO"/"WARN" both 4
    //
    // The header block at the top of every file documents these
    // positions so column-based parsing (cut -c1-23 etc.) works.
    //
    // Separator: two spaces between fields, not tabs. Tabs in a
    // terminal advance to the next multiple-of-8 column which breaks
    // visual alignment when a padded field's length isn't a multiple
    // of 8. Two-space separators give clean, predictable visual
    // alignment regardless of viewer.
    // fromMSecsSinceEpoch() yields a LOCAL QDateTime, and ISODateWithMs on
    // a local QDateTime emits no UTC offset — so an hour of log either side
    // of a DST transition was ambiguous, in a file whose whole purpose is
    // reconstructing when things happened. Force UTC and make it explicit
    // with the trailing 'Z'.
    const QString timeIso =
        QDateTime::fromMSecsSinceEpoch(entry->epochMs, Qt::UTC)
            .toString(Qt::ISODateWithMs);
    const QString friendly = m_names.lookupByKey(key);

    const QString line = timeIso
                       + QStringLiteral("  ")
                       + padRight(key,      9)
                       + QStringLiteral("  ")
                       + padRight(friendly, 16)
                       + QStringLiteral("  ")
                       + directionTag(entry->direction)   // already width 3
                       + QStringLiteral("  ")
                       + severityTag (entry->severity)    // already width 4
                       + QStringLiteral("  ")
                       + entry->text
                       + QStringLiteral("\n");

    const QByteArray bytes = line.toUtf8();
    const qint64 written = of.file->write(bytes);
    if (written != bytes.size()) {
        qWarning() << "LogWriter: write failed on" << of.path
                   << ":" << of.file->errorString();
        of.poisoned = true;
        m_hadError.store(true, std::memory_order_relaxed);
        of.file->close();
        return;
    }
    // flush() forces the bytes from QFile's userspace buffer down to the
    // OS. We don't fsync — that's overkill (we'd be syncing on every line)
    // — but a plain flush is essential. Without it, an abrupt termination
    // (SIGTERM, kill, power loss) can lose the most recently buffered
    // line even though write() returned success. Tested empirically:
    // without flush, exactly N-1 of N test messages survive a SIGTERM
    // shutdown; with flush, all N do. The cost is one syscall per line,
    // which at our message rates is in the noise.
    // No per-line flush. The durability argument for it was sound — an
    // unflushed QFile buffer loses its tail on SIGTERM — but the guarantee
    // is preserved by flushing once per drain cycle (at most 100ms of
    // buffered data) and again on the final drain in run(), at 1/Nth the
    // syscalls. A drain that writes 1000 lines now issues one flush, not
    // a thousand.
    m_bytesWritten.fetch_add(written, std::memory_order_relaxed);
    accountBytes(written);
}

void LogWriter::writeRawRecord(OpenFile &of, const LogEntryPtr &entry)
{
    if (!m_rawCapture || of.rawPoisoned || !of.raw || !entry) return;

    // rawBytes is the datagram exactly as it arrived — header plus payload,
    // assembled once by the dispatcher. Empty means the entry was synthetic
    // (a dropped-message banner, a replayed row); those still belong in the
    // .log, but must NOT enter the .dlr, which is a record of what came off
    // the wire and nothing else.
    if (entry->rawBytes.isEmpty()) return;

    if (entry->rawBytes.size() > SessionFile::kMaxWireBytes) {
        qWarning() << "LogWriter: refusing to archive an oversized datagram ("
                   << entry->rawBytes.size() << "bytes) for" << of.rawPath;
        return;
    }

    const QByteArray rec =
        SessionFile::encodeRecord(entry->epochMs, entry->rawBytes);

    const qint64 written = of.raw->write(rec);
    if (written != rec.size()) {
        qWarning() << "LogWriter: raw write failed on" << of.rawPath
                   << ":" << of.raw->errorString();
        of.rawPoisoned = true;
        m_hadError.store(true, std::memory_order_relaxed);
        of.raw->close();
        return;
    }
    m_rawBytesWritten.fetch_add(written, std::memory_order_relaxed);
    accountBytes(written);
}

void LogWriter::writeRawFileHeader(OpenFile &of, const QString &key)
{
    if (!of.raw || of.rawPoisoned) return;

    // Header only on a genuinely empty file. The text side's `brandNew` is
    // computed from the .log, and the two streams can disagree — a .dlr can
    // already exist when its .log doesn't, if raw capture was toggled off
    // and back on, or if someone deleted one of the pair. Appending a second
    // header into a non-empty .dlr would make SessionReader read the magic
    // as a record length and give up on the rest of the file, so the check
    // belongs here rather than being inherited from the other stream.
    if (of.raw->size() > 0) return;

    SessionFile::FileHeader h;
    h.createdMs = QDateTime::currentMSecsSinceEpoch();

    // Recover source/kvch from the tab key, which is where they came from.
    const int us = key.indexOf('_');
    if (us > 0) {
        h.sourceId = static_cast<quint8> (key.left(us).toInt());
        h.kvchId   = static_cast<quint16>(key.mid(us + 1).toInt());
    }

    const QByteArray hdr = SessionFile::encodeHeader(h);
    if (of.raw->write(hdr) != hdr.size()) {
        qWarning() << "LogWriter: raw header write failed on" << of.rawPath
                   << ":" << of.raw->errorString();
        of.rawPoisoned = true;
        m_hadError.store(true, std::memory_order_relaxed);
        return;
    }
    m_rawBytesWritten.fetch_add(hdr.size(), std::memory_order_relaxed);
    accountBytes(hdr.size());
}

qint64 LogWriter::scanFolderBytes() const
{
    qint64 total = 0;
    QDirIterator it(m_rootDir,
                    QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

void LogWriter::accountBytes(qint64 n)
{
    if (n > 0) m_folderBytes.fetch_add(n, std::memory_order_relaxed);
}

void LogWriter::updateQuotaState()
{
    const qint64 limit   = m_maxFolderBytes.load(std::memory_order_relaxed);
    const qint64 minFree = m_minFreeBytes.load(std::memory_order_relaxed);

    if (limit <= 0 && minFree <= 0) {
        // No ceiling configured. Clear any suspension left over from a
        // previous setting so turning the limit off takes effect at once.
        if (m_quotaSuspended.exchange(false, std::memory_order_relaxed)) {
            emit quotaSuspendedChanged(false, folderBytes(), 0);
        }
        return;
    }

    if (!m_rescanTimerStarted) {
        // First pass: measure what is already on disk from previous runs.
        // This happens on the worker thread, after start(), precisely so a
        // large existing archive doesn't stall the GUI at launch.
        m_folderBytes.store(scanFolderBytes(), std::memory_order_relaxed);
        m_rescanTimer.start();
        m_rescanTimerStarted = true;
    } else if (m_rescanTimer.elapsed() >= kRescanIntervalMs) {
        // Replace the running estimate with a true measurement. This is
        // also the only way we notice an operator deleting files, or files
        // arriving from something other than us.
        m_folderBytes.store(scanFolderBytes(), std::memory_order_relaxed);
        m_rescanTimer.restart();
    }

    const qint64 used       = m_folderBytes.load(std::memory_order_relaxed);
    const bool   suspended  = m_quotaSuspended.load(std::memory_order_relaxed);
    const qint64 resumeAt   = limit > 0 ? qint64(double(limit) * kResumeFraction)
                                        : 0;

    // Free space is sampled on the same schedule as the folder scan; it is
    // a syscall, not a tree walk, so it is comparatively free.
    qint64 free = -1;
    if (minFree > 0) {
        QStorageInfo si(m_rootDir);
        if (si.isValid() && si.isReady()) {
            free = si.bytesAvailable();
            m_freeBytes.store(free, std::memory_order_relaxed);
        }
    }

    const bool overBudget = (limit > 0 && used >= limit);
    const bool lowOnDisk  = (minFree > 0 && free >= 0 && free < minFree);

    if (!suspended && (overBudget || lowOnDisk)) {
        if (lowOnDisk && !overBudget) {
            qWarning() << "LogWriter: only" << free
                       << "bytes free on the log volume (floor is" << minFree
                       << "); continuous logging suspended.";
            m_quotaSuspended.store(true, std::memory_order_relaxed);
            closeAll();
            emit quotaSuspendedChanged(true, used, limit);
            return;
        }
        m_quotaSuspended.store(true, std::memory_order_relaxed);
        qWarning() << "LogWriter: disk-log folder budget reached ("
                   << used << "of" << limit
                   << "bytes ); continuous logging suspended.";
        // Release the handles so the operator can move or delete the files
        // that are in the way — on Windows an open file often can't be.
        closeAll();
        emit quotaSuspendedChanged(true, used, limit);
    } else if (suspended
               && (limit <= 0 || used <= resumeAt)
               && (minFree <= 0 || free < 0
                   || free >= qint64(double(minFree) / kResumeFraction))) {
        // Resume needs BOTH conditions clear, and the free-space side gets
        // the same hysteresis gap as the budget side — deleting one small
        // file must not restart logging only for it to suspend again.

        m_quotaSuspended.store(false, std::memory_order_relaxed);
        qWarning() << "LogWriter: folder budget no longer exceeded ("
                   << used << "of" << limit
                   << "bytes ); continuous logging resumed.";
        emit quotaSuspendedChanged(false, used, limit);
    }
}

void LogWriter::flushAll()
{
    // Recompute the fault flag from live state rather than latching it
    // forever. A poisoned file un-poisons on the next day-directory
    // rollover (rotateIfNeeded reopens it), and a rotation that failed
    // because a viewer held the file open succeeds once it's released —
    // so a sticky flag reports a fault that no longer exists.
    bool anyBad = false;
    for (auto it = m_files.begin(); it != m_files.end(); ++it) {
        if (it->poisoned || it->rawPoisoned || it->rotationDisabled) {
            anyBad = true;
        }
        if (it->file && it->file->isOpen() && !it->poisoned) {
            it->file->flush();
        }
        // The .dlr is flushed on the same cycle as the .log. A record is
        // either wholly present or absent — encodeRecord() builds the whole
        // thing before a single write() — so a flush boundary can only ever
        // land between records, which is exactly what SessionReader's
        // TruncatedTail path expects.
        if (it->raw && it->raw->isOpen() && !it->rawPoisoned) {
            it->raw->flush();
        }
    }
    m_hadError.store(anyBad, std::memory_order_relaxed);
}

void LogWriter::writeHeader(OpenFile &of, const QString &key,
                            const LogEntryPtr &firstEntry)
{
    // Header block: #-prefixed lines at the top of every freshly-opened
    // log file. Self-describes what's in this file so an operator who
    // finds a stray .log six months later knows exactly what they're
    // looking at without consulting documentation.
    //
    // grep -v '^#' strips the header for operators who just want the
    // data rows.
    if (!of.file) return;

    const QString friendly = m_names.lookupByKey(key);

    // Parse "src_kvch" key — both decimal numbers. tabKey() format is
    // documented in LogEntry::tabKey().
    int srcId  = 0;
    int kvchId = 0;
    {
        const int us = key.indexOf('_');
        if (us > 0) {
            srcId  = key.left(us).toInt();
            kvchId = key.mid(us + 1).toInt();
        }
    }

    const QString created =
        QDateTime::fromMSecsSinceEpoch(firstEntry->epochMs, Qt::UTC)
            .toString(Qt::ISODateWithMs);

    // Part number: if the canonical name (.log) is the "live" file and
    // rotated files become .NNN.log, then this file is always "part 1"
    // unless it itself was started as a continuation. Since we always
    // open the canonical name fresh after rotation, this is always 1
    // at write time. (Rotated files were already written as part 1
    // earlier in their life and just got renamed.)
    QString hdr;
    hdr += QStringLiteral("# DLConsole log v1\n");
    hdr += QStringLiteral("# created    = %1\n").arg(created);
    hdr += QStringLiteral("# source_id  = %1 (0x%2)\n")
               .arg(srcId).arg(srcId, 2, 16, QChar('0'));
    hdr += QStringLiteral("# kvch_id    = %1\n").arg(kvchId);
    hdr += QStringLiteral("# friendly   = %1\n").arg(friendly);
    hdr += QStringLiteral("# timebase   = UTC (timestamps carry a trailing Z)\n");
    hdr += QStringLiteral("# columns    = time_iso(24) src(9) friendly(16) dir(3) sev(4) message\n");
    hdr += QStringLiteral("# separator  = 2 spaces between fields\n");
    hdr += QStringLiteral("# tip        = use 'grep -v ^#' to strip this header;\n");
    hdr += QStringLiteral("#              use 'cut -c1-24' for timestamps, 'cut -c27-35' for source\n");
    hdr += QStringLiteral("#\n");

    const QByteArray bytes = hdr.toUtf8();
    const qint64 written = of.file->write(bytes);
    if (written != bytes.size()) {
        // Header write failed. Mark poisoned and bail; the actual line
        // write that triggered this header will see of.poisoned and
        // also bail.
        qWarning() << "LogWriter: header write failed on" << of.path
                   << ":" << of.file->errorString();
        of.poisoned = true;
        m_hadError.store(true, std::memory_order_relaxed);
        return;
    }
    m_bytesWritten.fetch_add(written, std::memory_order_relaxed);
    accountBytes(written);
}

bool LogWriter::rotateIfNeeded(OpenFile &of, const QString &todayDir,
                               const QString &basename,
                               const QString &key,
                               const LogEntryPtr & /*firstEntry*/)
{
    bool needOpen = false;

    if (!of.file) {
        // The sidecar can still be open here: a text open that failed now
        // leaves of.file null while of.raw survives, because the two streams
        // are allowed to fail independently. The reopen path below creates
        // BOTH, so anything still held has to be released first or we leak
        // the QFile and open a second handle on the same .dlr.
        if (of.raw) { of.raw->close(); delete of.raw; of.raw = nullptr; }
        needOpen = true;
    } else if (of.dateDir != todayDir) {
        closeStreams(of);
        of.poisoned = false;
        of.rawPoisoned = false;
        of.rotationDisabled = false;   // new day, new file, retry rotation
        needOpen = true;
    } else if (rotationSize(of)
                   >= m_maxFileBytes.load(std::memory_order_relaxed)
               && !of.rotationDisabled) {
        of.file->close();
        if (of.raw) of.raw->close();
        const QString stem = of.path.endsWith(QStringLiteral(".log"))
                                 ? of.path.left(of.path.size() - 4)
                                 : of.path;

        // Find a free numbered slot. rename()'s result is CHECKED: if it
        // fails (file locked by a viewer on Windows, read-only mount,
        // permissions) we must not fall through and reopen the same path,
        // because that silently appends past m_maxFileBytes forever while
        // re-running this whole scan on every single line.
        bool rotated = false;
        for (int n = 1; n < 1000; ++n) {
            const QString candidate = QString("%1.%2.log")
                                          .arg(stem)
                                          .arg(n, 3, 10, QChar('0'));
            if (QFile::exists(candidate)) continue;
            rotated = QFile::rename(of.path, candidate);
            // Move the sidecar to the MATCHING number so the pair stays
            // together on disk. If this fails the .log has already moved, so
            // we drop the orphaned .dlr's claim to that slot rather than
            // leaving a mismatched pair that a later reader would correlate
            // wrongly.
            if (rotated && of.raw && !of.rawPath.isEmpty()) {
                renameSidecar(of, n);
            }
            break;                       // free slot found; don't keep scanning
        }

        // All 999 slots taken, or the rename was refused. Fall back to a
        // timestamped name, which is collision-proof.
        if (!rotated) {
            const QString ts = QDateTime::currentDateTime()
                                   .toString(QStringLiteral("yyyyMMdd-hhmmsszzz"));
            rotated = QFile::rename(of.path,
                                    QStringLiteral("%1.%2.log").arg(stem, ts));
            if (rotated && of.raw && !of.rawPath.isEmpty()) {
                const QString rawStem =
                    of.rawPath.endsWith(QLatin1String(SessionFile::kExtension))
                        ? of.rawPath.left(of.rawPath.size()
                              - int(qstrlen(SessionFile::kExtension)))
                        : of.rawPath;
                QFile::rename(of.rawPath,
                              QStringLiteral("%1.%2%3")
                                  .arg(rawStem, ts,
                                       QLatin1String(SessionFile::kExtension)));
            }
        }

        if (!rotated) {
            // Rotation is impossible here. Keep writing to the oversized
            // file — losing data is worse than an outsized file — but stop
            // retrying, and surface it so the status bar shows the fault.
            qWarning() << "LogWriter: cannot rotate" << of.path
                       << "— continuing to append to the oversized file.";
            of.rotationDisabled = true;
            m_hadError.store(true, std::memory_order_relaxed);
        }

        closeStreams(of);
        needOpen = true;
    }

    if (!needOpen) return false;

    QDir parent(m_rootDir);
    if (!parent.exists()) {
        QDir().mkpath(m_rootDir);
    }
    QDir day(m_rootDir + QDir::separator() + todayDir);
    if (!day.exists()) {
        day.mkpath(".");
    }

    of.path    = day.filePath(basename + ".log");
    of.rawPath = m_rawCapture
                     ? day.filePath(basename + QLatin1String(SessionFile::kExtension))
                     : QString();

    // Detect whether the file is brand new (empty) vs. being re-appended
    // to (we crashed, restarted, and the file already exists from a
    // previous session). We only emit the header on a brand-new file —
    // appending a fresh header into the middle of an existing log file
    // would confuse downstream parsers and break grep workflows.
    const bool brandNew = !QFile::exists(of.path)
                          || QFileInfo(of.path).size() == 0;

    of.file = new QFile(of.path);
    of.dateDir = todayDir;

    // No QIODevice::Text: it rewrites '\n' as CRLF on Windows, which both
    // desynchronises m_bytesWritten from the real byte count and breaks the
    // column offsets this file's own header block advertises. We already
    // write an explicit '\n'.
    const bool textOk =
        of.file->open(QIODevice::Append | QIODevice::WriteOnly);
    if (!textOk) {
        qWarning() << "LogWriter: cannot open" << of.path
                   << ":" << of.file->errorString();
        of.poisoned = true;
        m_hadError.store(true, std::memory_order_relaxed);
        delete of.file;
        of.file = nullptr;
        // Deliberately NOT returning here. The sidecar is opened below
        // whatever happened to the text log: a permissions problem or a
        // name collision on the .log must not also cost us the archive that
        // can still be re-decoded. These two streams fail independently or
        // the independence is a fiction.
    } else {
        of.poisoned = false;
    }

    // Sidecar. A failure here is likewise not fatal to text logging —
    // losing the ability to re-decode is bad; losing the readable log as
    // well would be worse.
    if (m_rawCapture) {
        of.raw = new QFile(of.rawPath);
        if (!of.raw->open(QIODevice::Append | QIODevice::WriteOnly)) {
            qWarning() << "LogWriter: cannot open raw sidecar" << of.rawPath
                       << ":" << of.raw->errorString();
            delete of.raw;
            of.raw = nullptr;
            of.rawPoisoned = true;
            m_hadError.store(true, std::memory_order_relaxed);
        } else {
            of.rawPoisoned = false;
            // Emit the .dlr file header here, where we know the sidecar was
            // just opened. writeRawFileHeader() self-guards on size so a
            // reopened, non-empty archive is appended to cleanly.
            writeRawFileHeader(of, key);
        }
    }

    // brandNew drives the TEXT header only, so a failed text open reports
    // false — writeEntry bails on of.poisoned before it would use this.
    return textOk && brandNew;
}

qint64 LogWriter::rotationSize(const OpenFile &of) const
{
    // Rotate on whichever stream hits the cap first, so neither can run away
    // while the other stays small. Sizes are usually comparable — a wire
    // datagram and its rendered line are within a factor of two for typical
    // diagnostic traffic — but a long text payload or a large binary frame
    // can skew it either way.
    qint64 n = of.file ? of.file->size() : 0;
    if (of.raw) n = qMax(n, of.raw->size());
    return n;
}

void LogWriter::renameSidecar(OpenFile &of, int slot)
{
    const int extLen = int(qstrlen(SessionFile::kExtension));
    const QString rawStem =
        of.rawPath.endsWith(QLatin1String(SessionFile::kExtension))
            ? of.rawPath.left(of.rawPath.size() - extLen)
            : of.rawPath;
    const QString candidate = QStringLiteral("%1.%2%3")
                                  .arg(rawStem)
                                  .arg(slot, 3, 10, QChar('0'))
                                  .arg(QLatin1String(SessionFile::kExtension));
    if (!QFile::rename(of.rawPath, candidate)) {
        qWarning() << "LogWriter: could not rotate raw sidecar"
                   << of.rawPath << "->" << candidate;
    }
}

void LogWriter::closeStreams(OpenFile &of)
{
    if (of.file) { of.file->close(); delete of.file; of.file = nullptr; }
    if (of.raw)  { of.raw->close();  delete of.raw;  of.raw  = nullptr; }
}

void LogWriter::evictIdleFilesIfNeeded()
{
    if (m_files.size() <= kMaxOpenFiles) return;

    // Close (don't forget) the least-recently-written entries until we're
    // back under the cap. The map entry itself is dropped too, so a source
    // that never speaks again costs us nothing; one that does speak again
    // simply reopens its file in Append mode.
    while (m_files.size() > kMaxOpenFiles) {
        auto oldest = m_files.begin();
        for (auto it = m_files.begin(); it != m_files.end(); ++it) {
            if (it->lastUsed < oldest->lastUsed) oldest = it;
        }
        closeStreams(*oldest);
        m_files.erase(oldest);
    }
}

void LogWriter::closeAll()
{
    for (auto it = m_files.begin(); it != m_files.end(); ++it) {
        closeStreams(*it);
    }
    m_files.clear();
}

QString LogWriter::currentDateDir()
{
    return QDateTime::currentDateTime().toString("yyyy-MM-dd");
}

QString LogWriter::sanitizeFilename(const QString &in)
{
    QString out;
    out.reserve(in.size());
    for (QChar c : in) {
        switch (c.unicode()) {
        case '<': case '>': case ':': case '"': case '/':
        case '\\': case '|': case '?': case '*':
            out += '_'; break;
        default:
            if (c.unicode() < 0x20) out += '_';
            else                    out += c;
        }
    }
    if (out.isEmpty()) out = QStringLiteral("unnamed");

    // Trim trailing dots, commas and spaces. Windows silently rejects file
    // names ending in a dot or a space, and a stray trailing comma from a
    // malformed friendly_names.csv row produced files literally called
    // "L2_V1,.log". Leading junk is left alone — it is visible and harmless.
    while (!out.isEmpty()
           && (out.endsWith('.') || out.endsWith(' ') || out.endsWith(','))) {
        out.chop(1);
    }
    if (out.isEmpty()) out = QStringLiteral("unnamed");

    return out;
}
