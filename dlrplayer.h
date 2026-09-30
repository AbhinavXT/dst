#ifndef DLRPLAYER_H
#define DLRPLAYER_H

// =============================================================================
//  DlrPlayer
//  -----------------------------------------------------------------------------
//  Re-transmits the datagrams stored in one or more .dlr archives over UDP, at
//  the inter-arrival timing they were recorded with.
//
//  WHY IT SENDS ON A SOCKET INSTEAD OF INJECTING
//
//    SessionWindow already replays a .dlr, but deliberately as a strict
//    consumer: its own models, its own window, nothing written back. That is
//    the right shape for looking at yesterday's traffic and the wrong shape
//    for exercising the live path, because everything interesting about the
//    live path is upstream of the models — the socket drain, the dest-id
//    filter, the backpressure counter, the ~30 ms dispatcher batching,
//    LogWriter, the .log and .dlr recorders, the Loco Console's own state
//    machine.
//
//    So this sends real datagrams to a real port. Nothing downstream is told
//    it is a replay, because the moment any component can tell, that
//    component is no longer the one you are testing. Verified against the
//    recorded archives: destination_id is 101 on every stored record and
//    message_len always equals wireLen - sizeof(STRUCT_MESSAGE_HEADER), so a
//    verbatim resend passes every check in UDPCommunication::drainSocket()
//    and lands in the same (source_id, kvchId) tab it originally came from.
//
//    The cost of that fidelity is that replayed traffic IS live traffic as
//    far as the rest of the app is concerned: it will be recorded into
//    today's files and it will interleave with anything the real Kavach is
//    sending. When the point is to test recording, that is the feature. The
//    dialog says so before it starts.
//
//  MERGING
//
//    A .dlr holds one (source_id, kvchId) pair, so a session is several
//    files. They are k-way merged on arrivalMs into a single stream — the
//    two archives from one recording start on the same millisecond and
//    interleave throughout, and playing them back to back instead of merged
//    would destroy exactly the cross-source ordering the merged view exists
//    to show.
//
//    The merge is streaming: one pending record per reader, never the whole
//    file in memory, so archive size is bounded by disk and not by RAM.
//
//  THE CLOCK
//
//    Scheduling is against a monotonic QElapsedTimer started at play(), NOT
//    against per-record sleeps. Sleeping for each gap in turn accumulates
//    the scheduler's error once per packet: at ~43 records/s a 1 ms bias
//    becomes a visible drift inside a minute. Each record instead gets an
//    absolute due time and the thread waits until the clock reaches it, so
//    error is per-record and never accumulates.
//
//    Records sharing a millisecond are common (3909 of 6944 in one measured
//    archive) and simply fall through the wait with nothing to do, which is
//    what reproduces the original bursts.
// =============================================================================

#include "colorrules.h"
#include "logquery.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QVector>
#include <QWaitCondition>
#include <atomic>

class DlrPlayer : public QThread
{
    Q_OBJECT

public:
    // What probe() reports about one archive, for the file list and for
    // working out the combined timeline before anything is played.
    struct Source {
        QString path;
        QString tabKey;        // "src/kvch", the same key the live tabs use
        quint8  sourceId = 0;
        quint16 kvchId   = 0;
        qint64  records  = 0;
        qint64  firstMs  = 0;
        qint64  lastMs   = 0;
        qint64  bytes    = 0;  // total wire bytes, for the throughput readout
        bool    truncated = false;   // ended mid-record; still playable
    };

    struct Stats {
        qint64 sent       = 0;
        qint64 seekSkipped = 0;  // records consumed by a content seek, unsent
        bool   armed      = false;   // the seek has landed and playback is live
        qint64 total      = 0;
        qint64 bytesSent  = 0;
        qint64 sendErrors = 0;
        qint64 playbackMs = 0;   // position on the (gap-compressed) timeline
        qint64 spanMs     = 0;   // length of that timeline
        qint64 elapsedMs  = 0;   // wall time since play()
        qint64 lateMs     = 0;   // worst observed lateness, a fidelity gauge
        int    loops      = 0;
    };

    explicit DlrPlayer(QObject *parent = nullptr);
    ~DlrPlayer() override;

    // Read an archive's header and walk its records to count them and find
    // its span. Streams the file; safe on a multi-gigabyte archive. Runs on
    // the caller's thread — the dialog does this when a file is added, so
    // the timeline is known before playback starts.
    static bool probe(const QString &path, Source *out, QString *err);

    // Configuration. All are latched at play() time; changing speed while
    // running is honoured live (see setSpeed), the rest are not.
    void setSources(const QVector<Source> &sources);
    void setTarget(const QHostAddress &addr, quint16 port);

    // Playback rate. 1.0 is real time. 0.0 means "as fast as the socket
    // will take it" — that is the mode that actually exercises the
    // dispatcher's backpressure path, so it is a supported setting rather
    // than an abuse of the speed field. Safe to call while playing.
    void setSpeed(double x);
    double speed() const { return m_speed.load(std::memory_order_relaxed); }

    // Cap on idle gaps. A diagnostic archive can sit quiet for minutes;
    // replaying that faithfully means watching nothing happen. Non-zero
    // clamps any gap longer than this, so the busy parts still play at
    // true speed while the dead air is skipped. 0 disables.
    void setMaxGapMs(int ms) { m_maxGapMs = ms > 0 ? ms : 0; }

    // Start `ms` into the merged timeline. Records before it are read and
    // discarded, not sent — seeking has to respect the file's record
    // framing, so there is no cheaper way to land mid-archive.
    void setStartOffsetMs(qint64 ms) { m_startOffsetMs = ms > 0 ? ms : 0; }

    void setLoop(bool on) { m_loop.store(on, std::memory_order_relaxed); }

    // ---- seeking by content --------------------------------------------
    //
    // Seeking by time offset assumes you know when the thing you care about
    // happened. Usually you know WHAT it was and not when — and at 1x a
    // four-minute archive is a four-minute wait to find out. This takes the
    // same LogQuery the filter bar and archive search use, so the expression
    // that found the row is the expression that seeks to it.
    //
    // Records before the first match are read and discarded, not sent.
    // Once a record matches, the seek is "armed" and everything from there
    // plays normally — the point is to reach an event and then watch what
    // follows it, not to send only the matching records.
    //
    // `rules` is COPIED, for the same reason ArchiveSearcher copies it:
    // classification runs per record on this thread, and borrowing a
    // pointer to a MainWindow member would be a live cross-thread read that
    // a rules reload could trip. Returns false and fills `err` if the query
    // does not parse — a seek that silently matched nothing would be
    // indistinguishable from an archive that never contained the event.
    bool setSeekQuery(const QString &queryText, const ColorRules &rules,
                      QString *err = nullptr);
    void clearSeekQuery();
    bool hasSeekQuery() const { return !m_seekQuery.isEmpty(); }

    // Mid-playback: discard forward — without pacing, so it is immediate
    // even across a long idle gap — until the next record matching the seek
    // query. Pacing re-anchors at whatever is found, so the stream resumes
    // at true speed from there instead of firing a catch-up burst.
    void skipToNextMatch();

    // Transport. play() starts the thread; the others are safe from the GUI
    // thread at any time.
    void play();
    void pause();
    void resume();
    void stop();

    bool isPaused() const { return m_paused.load(std::memory_order_relaxed); }

    Stats stats() const;

    // ---- live tap -------------------------------------------------------
    //
    // A copy of the most recently sent record, for a decode panel watching
    // the stream. Off by default and only captured while enabled, because
    // in burst mode this sits in a loop that moves 200k records/s and the
    // panel can only render a handful of them — sampling what the tap holds
    // on the progress tick costs one copy per record instead of one decode.
    //
    // The record a content seek lands on is captured regardless: that one is
    // the whole reason the seek was run, and it must be inspectable even
    // with the tap off.
    void setTapEnabled(bool on) { m_tap.store(on, std::memory_order_relaxed); }
    bool tapEnabled() const     { return m_tap.load(std::memory_order_relaxed); }

    // Returns an empty array when nothing has been tapped yet.
    QByteArray tappedWire(qint64 *arrivalMs = nullptr) const;

signals:
    // Emitted on a ~100 ms cadence while playing, and once more at the end.
    // Deliberately not per-record: at burst speed that would be tens of
    // thousands of queued signals competing with the datagrams themselves
    // for the GUI thread we are trying to load-test.
    void progress(DlrPlayer::Stats s);

    void started(qint64 totalRecords, qint64 spanMs);

    // A content seek found its record. `playbackMs` is where it landed on
    // the timeline; `text` is the decoded line, so the UI can show what was
    // matched rather than just a position.
    void seekLanded(qint64 playbackMs, QString text);

    // The archive ended without the query ever matching. Not a failure —
    // the event genuinely is not in these files — but it must be said, or
    // an empty run looks identical to a broken one.
    void seekExhausted();
    void finished(bool completed);      // false when stopped early
    void failed(QString reason);

protected:
    void run() override;

private:
    // One open archive in the k-way merge.
    struct Cursor;

    // Why an enum and not a bool: a skip request has to break a wait that
    // may be parked on a multi-minute gap, and the caller must be able to
    // tell "time to send" from "abandon this record and go looking".
    enum class Wait { Ready, Stopped, Interrupted };
    Wait  waitUntil(qint64 dueMs);
    bool  sendOne(const QByteArray &wire);
    void  publish();

    QVector<Source> m_sources;
    QHostAddress    m_addr = QHostAddress::LocalHost;
    quint16         m_port = 0;

    std::atomic<double> m_speed{1.0};
    int     m_maxGapMs      = 0;
    qint64  m_startOffsetMs = 0;

    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_loop{false};
    std::atomic<bool> m_skipRequested{false};
    std::atomic<bool> m_tap{false};

    // Guarded by m_mtx, like Stats.
    QByteArray m_tapWire;
    qint64     m_tapArrivalMs = 0;

    // Owned copies — see setSeekQuery().
    LogQuery   m_seekQuery;
    ColorRules m_seekRules;

    // Pause is a wait on a condition rather than a polled sleep so that
    // resume() is immediate and a paused player costs nothing.
    mutable QMutex  m_mtx;
    QWaitCondition  m_pauseCv;

    // Guarded by m_mtx; the GUI reads it via stats().
    Stats m_stats;

    QElapsedTimer m_wall;
    qint64        m_pausedTotalMs = 0;

    // Owned by run()'s stack; non-null only while the thread is inside run().
    class QUdpSocket *m_sock = nullptr;
};

Q_DECLARE_METATYPE(DlrPlayer::Stats)

#endif // DLRPLAYER_H
