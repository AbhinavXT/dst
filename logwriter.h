#ifndef LOGWRITER_H
#define LOGWRITER_H

// =============================================================================
//  LogWriter
//  -----------------------------------------------------------------------------
//  Always-on rolling disk log. One file per (source_id, kvchId) pair, kept
//  open and appended to as messages stream in.
//
//  Why this exists:
//    – The original "Save" button required the operator to remember to click
//      it before closing the app. Anything that happened on Tuesday at 3 AM
//      while the operator wasn't watching was simply lost.
//    – For Kavach diagnostic work this is a real problem; root-cause analysis
//      often happens hours after the symptom.
//
//  Layout on disk:
//
//      LOGS/
//        2026-05-08/
//          LK_1_VCC_1.log              ← if friendly name is mapped
//          LK_1_VCC_2.log
//          21_3.log                    ← raw key, no mapping in CSV
//          21_3.001.log                ← rotated when 21_3.log exceeded size cap
//        2026-05-09/
//          LK_1_VCC_1.log
//          ...
//
//  Daily directory rotation: a new directory is created at local midnight.
//  Per-file size rotation: when a file exceeds m_maxFileBytes (100 MiB by
//  default) it's renamed with a numeric suffix and a fresh file opened.
//
//  Threading:
//    – LogWriter IS-A QThread. We override run() to take full control of
//      the lifecycle — no relying on an event loop, no thread-affinity
//      surprises. The run() function loops on an internal flag, sleeps in
//      a QWaitCondition with a 100 ms timeout, and drains the queue on
//      every wake. This is the same shape as section 1's UDPCommunication.
//    – enqueue() locks m_queueMutex, appends, signals m_queueCv. Constant
//      time, no I/O.
//    – stop() flips m_stopRequested, signals the cv, joins. The run loop
//      sees the flag, drains everything one last time, closes files, and
//      returns. The wait() call in stop() then returns immediately.
//
//  This shape — rather than QObject::moveToThread — sidesteps the
//  "moveToThread: Cannot move objects with a parent" warning entirely AND
//  guarantees that pending entries flushed at shutdown are never lost
//  because the drain at end-of-run is synchronous, on the worker thread,
//  before run() returns.
//
//  Failure handling:
//    – If a file open or write fails, we log ONE qWarning and mark that
//      file as poisoned — further writes for that source are dropped to
//      avoid log spam. hadError() is exposed so the status bar shows ✗.
//    – We never throw, never block the GUI thread.
// =============================================================================

#include <QHash>
#include <QElapsedTimer>
#include <QMutex>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QThread>
#include <QWaitCondition>
#include <atomic>

#include "logentry.h"
#include "namemap.h"
#include "sessionfile.h"

class QFile;

class LogWriter : public QThread
{
    Q_OBJECT

public:
    explicit LogWriter(const QString &rootDir,
                       qint64         maxFileBytes = 100 * 1024 * 1024,
                       QObject       *parent = nullptr);
    ~LogWriter() override;

    // Takes a COPY of the map, deliberately. The writer reads it from its
    // own thread on every line; a borrowed pointer into a MainWindow member
    // is a use-after-free waiting for either a shutdown where wait() times
    // out (member dtors run before the thread is joined) or the "reload
    // friendly names" menu item that NameMap's header already anticipates.
    // The map is a handful of short strings — copying it once at startup is
    // free, and it removes the cross-thread aliasing entirely.
    // Call before start().
    void setNameMap(const NameMap &names) { m_names = names; }

    // Enable the byte-exact .dlr sidecar alongside each .log.
    //
    // Off means the archive is a rendering only: decoded text, plus whatever
    // severity the colour rules assigned at the time. You cannot re-decode
    // it, re-check a CRC, or replay it. On means every datagram is preserved
    // verbatim next to its rendered line, for roughly the same disk cost.
    // Call before start().
    void setRawCapture(bool on) { m_rawCapture = on; }

    // Per-file rotation ceiling. Safe to call while running; takes effect
    // at the next write.
    void setRotationBytes(qint64 n)
    { m_maxFileBytes.store(n > 0 ? n : 1, std::memory_order_relaxed); }
    bool rawCapture() const     { return m_rawCapture; }

    // Bytes written to .dlr sidecars, tracked separately from the text logs
    // so the status bar can show what raw capture is actually costing.
    qint64 rawBytesWritten() const
    { return m_rawBytesWritten.load(std::memory_order_relaxed); }

    // Push one entry onto the work queue. Safe to call from the GUI thread
    // at any frequency. Returns immediately — no I/O.
    void enqueue(const LogEntryPtr &entry);

    // Push a whole batch: one mutex acquisition and one wakeOne() for the
    // lot, instead of N of each.
    //
    // This is the path MainWindow should use. The dispatcher already
    // coalesces arrivals into ~30ms batches, but the disk hop was still
    // driven off the per-entry signal — so a 1000 msg/sec stream meant 1000
    // lock/unlock pairs on the GUI thread and 1000 wakeups on this one,
    // which is precisely the per-message cost the batching was introduced
    // to remove.
    void enqueueBatch(const QVector<LogEntryPtr> &entries);

    // Ask the worker to flush and stop. Blocks up to 'waitMs' for the run
    // loop to drain everything and close files. Idempotent.
    //
    // Returns true if the thread actually finished. A false return means
    // entries may be unflushed AND the thread is still running — the caller
    // must NOT delete this object in that case (deleting a running QThread
    // is a fatal error in Qt).
    Q_REQUIRED_RESULT bool stop(int waitMs = 2000);

    // True if any per-file writer has hit a permanent error.
    bool hadError() const { return m_hadError.load(std::memory_order_relaxed); }

    // ---- folder budget ----------------------------------------------
    //
    // Ceiling on the total size of everything under the log root. 0
    // disables the check.
    //
    // Enforcement is deliberately "stop", not "delete oldest". Silently
    // discarding the earliest archive is the wrong default for a tool whose
    // logs may be evidence in an incident investigation — the operator
    // should decide what is expendable. Suspension is reversible: free some
    // space and logging resumes on its own at the next rescan.
    //
    // Safe to call while running.
    void setMaxFolderBytes(qint64 n)
    { m_maxFolderBytes.store(n < 0 ? 0 : n, std::memory_order_relaxed); }
    qint64 maxFolderBytes() const
    { return m_maxFolderBytes.load(std::memory_order_relaxed); }

    // Minimum free space to leave on the log volume. Suspends writing the
    // same way the folder budget does.
    //
    // The folder budget alone is not enough: it only counts what WE wrote.
    // Anything else filling the partition — another service, a core dump,
    // an operator copying a video onto the diagnostic box — still ends with
    // every file poisoned and a small red tick in the status bar. This
    // catches that case with the same reversible suspension, which is a
    // far better outcome than a wall of write failures.
    void setMinFreeBytes(qint64 n)
    { m_minFreeBytes.store(n < 0 ? 0 : n, std::memory_order_relaxed); }
    qint64 minFreeBytes() const
    { return m_minFreeBytes.load(std::memory_order_relaxed); }

    qint64 freeBytes() const
    { return m_freeBytes.load(std::memory_order_relaxed); }

    // Current measured size of the log root. Maintained incrementally
    // between rescans, so it is an estimate that is corrected periodically.
    qint64 folderBytes() const
    { return m_folderBytes.load(std::memory_order_relaxed); }

    // True while writing is suspended because the budget was reached.
    bool quotaSuspended() const
    { return m_quotaSuspended.load(std::memory_order_relaxed); }

signals:
    // Emitted from the worker thread (queued to the GUI thread) when the
    // budget is first reached, and again when it is no longer exceeded.
    // The status bar surfaces both; a suspension that nobody notices is
    // just silent data loss.
    void quotaSuspendedChanged(bool suspended, qint64 usedBytes, qint64 limitBytes);

public:

    qint64 bytesWritten() const { return m_bytesWritten.load(std::memory_order_relaxed); }

protected:
    // Thread entry point. Owns the loop directly — no QObject event loop
    // here, just a wait/drain cycle that we can shut down deterministically.
    void run() override;

private:
    struct OpenFile {
        QFile  *file = nullptr;       // human-readable .log
        QString path;

        // Byte-exact .dlr sidecar. Null when raw capture is off. Kept in the
        // same OpenFile so it inherits the day-directory rollover, the size
        // rotation, and the LRU close — a .log and its .dlr must never
        // disagree about which slice of time they cover, and the surest way
        // to guarantee that is to rotate them together in one place.
        QFile  *raw = nullptr;
        QString rawPath;
        // Independent poison flag: a full disk or a permissions problem on
        // one stream should not silently take the other down with it.
        bool    rawPoisoned = false;

        QString dateDir;
        bool    poisoned = false;
        // Set when a size-triggered rotation could not be performed (all
        // numbered slots taken, or rename() refused). We then keep
        // appending to the oversized file rather than losing data — but we
        // stop RE-ATTEMPTING rotation on every subsequent line, which would
        // otherwise mean up to 999 stat() calls per message written.
        bool    rotationDisabled = false;
        // Monotonic tick of the last write, for LRU eviction.
        quint64 lastUsed = 0;
    };

    // Ceiling on simultaneously-open log files. One handle per distinct
    // (source_id, kvchId) was held open forever, and that key space is
    // wire-controlled — so a burst of corrupt headers ran the process out
    // of file descriptors. Past this we close the least-recently-written
    // file; it reopens in Append mode on its next message, so no data is
    // lost, we just pay one open() for a source that had gone quiet.
    static constexpr int kMaxOpenFiles = 128;
    void evictIdleFilesIfNeeded();
    quint64 m_useTick = 0;

    // Worker-thread-only helpers.
    void  drainQueue_locked(QQueue<LogEntryPtr> &local);
    void  writeEntry(const LogEntryPtr &entry);
    void  writeRawRecord(OpenFile &of, const LogEntryPtr &entry);
    // Writes the 32-byte .dlr file header. Called once per freshly-created
    // sidecar, mirroring writeHeader() on the text side.
    void  writeRawFileHeader(OpenFile &of, const QString &key);
    // Returns true iff a brand-new (empty) file was just opened — the
    // signal for writeEntry() to emit a header block before the first
    // data line. Returns false when no rotation happened, when we
    // rotated to a continuation that's still part of the same logical
    // session, OR when we reopened an existing non-empty file (e.g.
    // after a crash + restart on the same calendar day).
    bool  rotateIfNeeded(OpenFile &of, const QString &todayDir,
                         const QString &basename,
                         const QString &key,
                         const LogEntryPtr &firstEntry);
    void  writeHeader(OpenFile &of, const QString &key,
                      const LogEntryPtr &firstEntry);
    void  closeAll();

    // Walk the log root and total up every file. Runs on the worker thread
    // only — with daily subdirectories and one or two files per source this
    // is a few hundred to a few thousand stat() calls, which is fine every
    // 30 seconds but would not be fine per message.
    qint64 scanFolderBytes() const;

    // Re-measure if the rescan interval has elapsed, then apply the budget
    // decision (suspend / resume). Called once per drain cycle.
    void   updateQuotaState();

    // Add to the running estimate after a successful write.
    void   accountBytes(qint64 n);
    // Close and delete both the .log and its .dlr sidecar. One helper
    // because every place that lets go of a file must let go of both.
    void  closeStreams(OpenFile &of);
    // Larger of the two streams — rotation triggers on whichever fills first.
    qint64 rotationSize(const OpenFile &of) const;
    void  renameSidecar(OpenFile &of, int slot);
    // Flush every open file's userspace buffer. Called once per drain
    // cycle and on the final drain, replacing the per-line flush.
    void  flushAll();
    static QString currentDateDir();
    static QString sanitizeFilename(const QString &in);

    QString m_rootDir;
    // Atomic because Settings can change it while the worker is running.
    // Previously construction-time only, which meant the rotation size in
    // the Settings dialog silently did nothing until the next restart.
    std::atomic<qint64> m_maxFileBytes;
    NameMap m_names;              // owned copy; see setNameMap()

    // Cross-thread queue. m_queueMutex guards m_queue AND is the lock used
    // with the wait condition.
    QMutex             m_queueMutex;
    QWaitCondition     m_queueCv;
    QQueue<LogEntryPtr> m_queue;

    // Worker-only state.
    QHash<QString, OpenFile> m_files;

    bool m_rawCapture = true;

    // Budget state. m_folderBytes is incremented per write and replaced
    // wholesale by each rescan, so it drifts upward slightly between
    // rescans (open-file buffering) and is corrected on the next one.
    std::atomic<qint64> m_maxFolderBytes{0};
    std::atomic<qint64> m_minFreeBytes{0};
    std::atomic<qint64> m_freeBytes{-1};
    std::atomic<qint64> m_folderBytes{0};
    std::atomic<bool>   m_quotaSuspended{false};
    QElapsedTimer       m_rescanTimer;
    bool                m_rescanTimerStarted = false;

    // How often to re-walk the tree. Between rescans an operator deleting
    // files by hand won't be noticed, which is why resume is not instant.
    static constexpr qint64 kRescanIntervalMs = 30'000;

    // Resume at 95% of the ceiling rather than at the ceiling itself.
    // Without the gap, deleting a single small file would resume logging,
    // which would immediately re-cross the line and suspend again — a
    // suspend/resume flutter that spams the status bar and the log.
    static constexpr double kResumeFraction = 0.95;

    std::atomic<bool>   m_hadError{false};
    std::atomic<qint64> m_bytesWritten{0};
    std::atomic<qint64> m_rawBytesWritten{0};
    std::atomic<bool>   m_stopRequested{false};
};

#endif // LOGWRITER_H
