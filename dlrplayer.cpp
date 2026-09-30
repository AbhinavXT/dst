#include "dlrplayer.h"

#include "messagedispatcher.h"
#include "sessionreader.h"

#include <QUdpSocket>
#include <QtGlobal>

#include <limits>

// One archive being merged. Holds exactly one look-ahead record, which is all
// a k-way merge needs and all we are willing to keep in memory per file.
struct DlrPlayer::Cursor
{
    SessionReader reader;
    QByteArray    wire;
    qint64        arrivalMs = 0;
    bool          valid     = false;   // a record is loaded and unsent

    bool advance()
    {
        if (!reader.next()) { valid = false; return false; }
        arrivalMs = reader.arrivalMs();
        wire      = reader.wire();
        valid     = true;
        return true;
    }
};

DlrPlayer::DlrPlayer(QObject *parent)
    : QThread(parent)
{
}

DlrPlayer::~DlrPlayer()
{
    stop();
    wait(5000);
}

bool DlrPlayer::probe(const QString &path, Source *out, QString *err)
{
    if (!out) { return false; }

    SessionReader r;
    if (!r.open(path)) {
        if (err) { *err = r.errorString(); }
        return false;
    }

    Source s;
    s.path     = path;
    s.tabKey   = r.tabKey();
    s.sourceId = r.fileHeader().sourceId;
    s.kvchId   = r.fileHeader().kvchId;

    while (r.next()) {
        if (s.records == 0) { s.firstMs = r.arrivalMs(); }
        s.lastMs = r.arrivalMs();
        s.bytes += r.wire().size();
        ++s.records;
    }

    // A torn tail is the normal shape of an archive from a run that was
    // killed, and everything before the tear is still good — refusing to
    // play it would discard exactly the recording you most want after a
    // crash. Flag it, keep it.
    if (r.status() == SessionReader::Corrupt
        || r.status() == SessionReader::TruncatedTail) {
        s.truncated = true;
    } else if (r.status() != SessionReader::Ok) {
        if (err) { *err = r.errorString(); }
        return false;
    }

    if (s.records == 0) {
        if (err) { *err = QStringLiteral("archive holds no complete records"); }
        return false;
    }

    *out = s;
    return true;
}

void DlrPlayer::setSources(const QVector<Source> &sources)
{
    m_sources = sources;
}

void DlrPlayer::setTarget(const QHostAddress &addr, quint16 port)
{
    m_addr = addr;
    m_port = port;
}

void DlrPlayer::setSpeed(double x)
{
    m_speed.store(x < 0.0 ? 0.0 : x, std::memory_order_relaxed);
}

bool DlrPlayer::setSeekQuery(const QString &queryText, const ColorRules &rules,
                             QString *err)
{
    if (queryText.trimmed().isEmpty()) { clearSeekQuery(); return true; }

    LogQuery q;
    q.parse(queryText);
    if (!q.isValid()) {
        if (err) { *err = q.errorString(); }
        return false;
    }

    m_seekQuery = q;
    m_seekRules = rules;      // owned copy
    return true;
}

void DlrPlayer::clearSeekQuery()
{
    m_seekQuery = LogQuery();
}

void DlrPlayer::skipToNextMatch()
{
    if (m_seekQuery.isEmpty()) { return; }
    m_skipRequested.store(true, std::memory_order_relaxed);
    // Wake a thread parked in a pause so the skip is acted on rather than
    // queued behind a resume that may never come.
    QMutexLocker lk(&m_mtx);
    m_pauseCv.wakeAll();
}

void DlrPlayer::play()
{
    if (isRunning()) { return; }
    m_stop.store(false, std::memory_order_relaxed);
    m_paused.store(false, std::memory_order_relaxed);
    m_skipRequested.store(false, std::memory_order_relaxed);
    {
        QMutexLocker lk(&m_mtx);
        m_stats = Stats();
    }
    m_pausedTotalMs = 0;
    start();
}

void DlrPlayer::pause()
{
    m_paused.store(true, std::memory_order_relaxed);
}

void DlrPlayer::resume()
{
    m_paused.store(false, std::memory_order_relaxed);
    QMutexLocker lk(&m_mtx);
    m_pauseCv.wakeAll();
}

void DlrPlayer::stop()
{
    m_stop.store(true, std::memory_order_relaxed);
    m_paused.store(false, std::memory_order_relaxed);
    QMutexLocker lk(&m_mtx);
    m_pauseCv.wakeAll();
}

QByteArray DlrPlayer::tappedWire(qint64 *arrivalMs) const
{
    QMutexLocker lk(&m_mtx);
    if (arrivalMs) { *arrivalMs = m_tapArrivalMs; }
    return m_tapWire;
}

DlrPlayer::Stats DlrPlayer::stats() const
{
    QMutexLocker lk(&m_mtx);
    return m_stats;
}

void DlrPlayer::publish()
{
    Stats snap;
    {
        QMutexLocker lk(&m_mtx);
        m_stats.elapsedMs = m_wall.isValid() ? (m_wall.elapsed() - m_pausedTotalMs) : 0;
        snap = m_stats;
    }
    emit progress(snap);
}

// Wait until the monotonic clock reaches `dueMs` of playback-adjusted wall
// time, honouring pause and stop. Sleeps in slices rather than one long block
// so a stop is acted on within a few ms even when the next record is a minute
// away.
DlrPlayer::Wait DlrPlayer::waitUntil(qint64 dueMs)
{
    for (;;) {
        if (m_stop.load(std::memory_order_relaxed)) { return Wait::Stopped; }
        if (m_skipRequested.load(std::memory_order_relaxed)) { return Wait::Interrupted; }

        if (m_paused.load(std::memory_order_relaxed)) {
            // Time spent paused is not playback time: it is added to the
            // offset so that on resume the stream picks up where it left
            // off instead of fast-forwarding to "catch up" with the wall
            // clock, which would fire a burst of everything missed.
            QElapsedTimer pausedFor;
            pausedFor.start();
            {
                QMutexLocker lk(&m_mtx);
                while (m_paused.load(std::memory_order_relaxed)
                       && !m_stop.load(std::memory_order_relaxed)
                       && !m_skipRequested.load(std::memory_order_relaxed)) {
                    m_pauseCv.wait(&m_mtx, 100);
                }
            }
            m_pausedTotalMs += pausedFor.elapsed();
            continue;
        }

        const qint64 now       = m_wall.elapsed() - m_pausedTotalMs;
        const qint64 remaining = dueMs - now;
        if (remaining <= 0) { return Wait::Ready; }

        // 5 ms slices: short enough that stop/pause feel instant, long
        // enough that a quiet archive is not spinning the CPU. The final
        // approach drops to usleep so sub-millisecond gaps are not rounded
        // up into visible jitter.
        if (remaining > 5) {
            QThread::msleep(static_cast<unsigned long>(qMin<qint64>(remaining - 1, 5)));
        } else {
            QThread::usleep(static_cast<unsigned long>(remaining * 1000));
            if (m_stop.load(std::memory_order_relaxed))          { return Wait::Stopped; }
            if (m_skipRequested.load(std::memory_order_relaxed)) { return Wait::Interrupted; }
            return Wait::Ready;
        }
    }
}

bool DlrPlayer::sendOne(const QByteArray &wire)
{
    // m_sock is created inside run() so its thread affinity is this worker —
    // same reason UDPCommunication builds its socket in run() — and is only
    // ever touched from there.
    if (!m_sock) { return false; }

    // Loopback in burst mode can transiently refuse a write when the
    // receiver's kernel buffer is full. That is a real condition worth
    // reproducing, not an error to hide — but a couple of retries keeps a
    // momentary stall from being counted as data loss.
    for (int attempt = 0; attempt < 3; ++attempt) {
        const qint64 n = m_sock->writeDatagram(wire, m_addr, m_port);
        if (n == wire.size()) { return true; }
        if (m_stop.load(std::memory_order_relaxed)) { return false; }
        QThread::usleep(200);
    }
    return false;
}

void DlrPlayer::run()
{
    if (m_sources.isEmpty()) { emit failed(tr("no archives selected")); return; }
    if (m_port == 0)         { emit failed(tr("no target port set"));   return; }

    QUdpSocket sock;
    // A generous send buffer keeps burst mode limited by the receiver rather
    // than by our own outbound queue.
    sock.setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, 4 * 1024 * 1024);
    m_sock = &sock;

    // The socket lives on this thread's stack, so clear the pointer on every
    // exit path rather than leaving a dangling one behind for a later call.
    struct SockGuard {
        QUdpSocket **slot;
        ~SockGuard() { *slot = nullptr; }
    } sockGuard{&m_sock};

    qint64 totalRecords = 0;
    qint64 spanFirst = std::numeric_limits<qint64>::max();
    qint64 spanLast  = std::numeric_limits<qint64>::min();
    for (const Source &s : m_sources) {
        totalRecords += s.records;
        spanFirst = qMin(spanFirst, s.firstMs);
        spanLast  = qMax(spanLast,  s.lastMs);
    }

    {
        QMutexLocker lk(&m_mtx);
        m_stats.total  = totalRecords;
        m_stats.spanMs = qMax<qint64>(0, spanLast - spanFirst);
    }
    emit started(totalRecords, qMax<qint64>(0, spanLast - spanFirst));

    bool completed = false;
    m_wall.start();
    m_pausedTotalMs = 0;

    do {
        QVector<Cursor *> cursors;
        auto cleanup = [&cursors]() { qDeleteAll(cursors); cursors.clear(); };

        bool openOk = true;
        for (const Source &s : m_sources) {
            auto *c = new Cursor;
            if (!c->reader.open(s.path)) {
                emit failed(tr("%1: %2").arg(s.path, c->reader.errorString()));
                delete c;
                openOk = false;
                break;
            }
            c->advance();
            cursors.push_back(c);
        }
        if (!openOk) { cleanup(); return; }

        // The gap-compressed timeline. `virtualMs` is where we are on it;
        // `lastRaw` is the arrival time of the previous record, so each new
        // record advances the virtual clock by the real gap, clamped.
        qint64 virtualMs = 0;
        qint64 lastRaw   = -1;

        // Pacing anchor: the virtual time of the first record played since
        // the last (re)anchor, and the wall time it went out at. Every due
        // time is derived from this pair rather than from a per-record
        // sleep, so scheduler error never accumulates.
        //
        // It is re-anchored — not reset — whenever the stream jumps: after
        // a content seek lands, and after a skip. Without that, the virtual
        // clock would have leapt forward while the wall clock had not, and
        // every record after the jump would be "overdue", flushing the rest
        // of the archive in one burst. Re-anchoring is what makes a seek
        // land and then play at true speed.
        qint64 anchorVirtualMs = -1;
        qint64 anchorWallMs    = 0;

        // A content seek starts disarmed: records are read and dropped
        // until one matches. With no query there is nothing to wait for.
        bool armed = m_seekQuery.isEmpty();
        bool everArmed = armed;

        QElapsedTimer sinceReport;
        sinceReport.start();

        for (;;) {
            if (m_stop.load(std::memory_order_relaxed)) { break; }

            // k-way merge step: the earliest pending record wins. With two
            // sources this is trivially a comparison; the loop generalises
            // to however many tabs a session had.
            Cursor *next = nullptr;
            for (Cursor *c : cursors) {
                if (!c->valid) { continue; }
                if (!next || c->arrivalMs < next->arrivalMs) { next = c; }
            }
            if (!next) { completed = true; break; }

            const qint64 raw = next->arrivalMs;
            if (lastRaw < 0) {
                virtualMs = 0;
            } else {
                qint64 gap = raw - lastRaw;
                if (gap < 0) { gap = 0; }          // clock stepped backwards
                if (m_maxGapMs > 0 && gap > m_maxGapMs) { gap = m_maxGapMs; }
                virtualMs += gap;
            }
            lastRaw = raw;

            const QByteArray wire = next->wire;
            next->advance();

            // Time seek: discards rather than sends, but still walks the
            // virtual clock so the first record played lands at the right
            // offset. Checked before the content gate because it is free
            // and the content gate is not.
            if (virtualMs < m_startOffsetMs) {
                QMutexLocker lk(&m_mtx);
                ++m_stats.seekSkipped;
                continue;
            }

            // A skip asked for mid-playback puts us back to hunting.
            if (m_skipRequested.load(std::memory_order_relaxed)) { armed = false; }

            // Content seek. Decoding every candidate is the cost of asking
            // "what" instead of "when"; it is the same work archive search
            // does per record, and only happens while disarmed.
            if (!armed) {
                LogEntryPtr e = MessageDispatcher::buildEntry(wire, raw, &m_seekRules, false);
                if (!e || !m_seekQuery.match(*e, nullptr)) {
                    QMutexLocker lk(&m_mtx);
                    ++m_stats.seekSkipped;
                    continue;
                }
                armed     = true;
                everArmed = true;
                m_skipRequested.store(false, std::memory_order_relaxed);
                anchorVirtualMs = -1;               // this record plays now
                {
                    QMutexLocker lk(&m_mtx);
                    m_stats.armed  = true;
                    m_tapWire      = wire;      // always, tap or not
                    m_tapArrivalMs = raw;
                }
                emit seekLanded(virtualMs - m_startOffsetMs, e->text);
            }

            const double sp = m_speed.load(std::memory_order_relaxed);
            const qint64 offsetOnTimeline = virtualMs - m_startOffsetMs;

            if (anchorVirtualMs < 0) {
                anchorVirtualMs = virtualMs;
                anchorWallMs    = m_wall.elapsed() - m_pausedTotalMs;
            }
            const qint64 dueMs =
                (sp <= 0.0) ? 0
                            : anchorWallMs + qint64(double(virtualMs - anchorVirtualMs) / sp);

            if (sp > 0.0) {
                const Wait w = waitUntil(dueMs);
                if (w == Wait::Stopped) { break; }
                if (w == Wait::Interrupted) { armed = false; continue; }
            } else if (m_stop.load(std::memory_order_relaxed)) {
                break;
            }

            // Burst mode has no wait to interrupt, so the skip is caught here.
            if (m_skipRequested.load(std::memory_order_relaxed)) { armed = false; continue; }

            const bool ok = sendOne(wire);

            {
                QMutexLocker lk(&m_mtx);
                if (m_tap.load(std::memory_order_relaxed)) {
                    m_tapWire      = wire;
                    m_tapArrivalMs = raw;
                }
                if (ok) { m_stats.bytesSent += wire.size(); }
                else    { ++m_stats.sendErrors; }
                ++m_stats.sent;
                m_stats.playbackMs = offsetOnTimeline;
                if (sp > 0.0) {
                    const qint64 late = (m_wall.elapsed() - m_pausedTotalMs) - dueMs;
                    if (late > m_stats.lateMs) { m_stats.lateMs = late; }
                }
            }

            // ~100 ms cadence. At burst speed a per-record signal would
            // queue tens of thousands of events onto the very GUI thread
            // the burst is meant to be stressing, and we would be measuring
            // our own instrumentation.
            if (sinceReport.elapsed() >= 100) {
                publish();
                sinceReport.restart();
            }
        }

        cleanup();

        // Say so rather than finishing silently: a query that never matched
        // produces exactly the same empty run as a broken player.
        if (!everArmed && !m_stop.load(std::memory_order_relaxed)) {
            emit seekExhausted();
        }

        if (m_stop.load(std::memory_order_relaxed)) { break; }

        if (m_loop.load(std::memory_order_relaxed)) {
            QMutexLocker lk(&m_mtx);
            ++m_stats.loops;
            m_stats.sent        = 0;
            m_stats.seekSkipped = 0;
            m_stats.armed       = false;
        }
    } while (m_loop.load(std::memory_order_relaxed)
             && !m_stop.load(std::memory_order_relaxed));

    publish();
    emit finished(completed && !m_stop.load(std::memory_order_relaxed));
}
