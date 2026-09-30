#ifndef UDPCOMMUNICATION_H
#define UDPCOMMUNICATION_H

// =============================================================================
//  UDPCommunication
//  -----------------------------------------------------------------------------
//  The UDP receiver thread.
//
//  Design notes (improvements over the original):
//    1. Event-driven, not a busy-loop. The QUdpSocket is created INSIDE run()
//       so its thread affinity is this worker thread. We connect its
//       readyRead() signal to drainSocket() and call exec(), so the thread's
//       event loop wakes us only when datagrams arrive — instead of pegging a
//       CPU core in `while(1) { hasPendingDatagrams() }`.
//
//    2. The kernel receive buffer is enlarged to 4 MiB (default on Windows is
//       ~64 KiB). Otherwise a brief burst of traffic from Kavach can overflow
//       the kernel buffer and the OS itself drops packets before our code
//       ever sees them. We could not detect those drops in the original.
//
//    3. We do NOT memcpy 8 KB STRUCT_MQBUF blobs around any more. A datagram is
//       parsed locally into a small ParsedMessage (header + only the actual
//       payload bytes) and shipped to the GUI thread via a queued signal. Qt's
//       Qt::QueuedConnection takes care of the thread handoff for us — no
//       mutex on our side at all.
//
//    4. Backpressure: we keep an atomic in-flight counter (incremented before
//       emit, decremented when the GUI slot has finished consuming). If it
//       exceeds m_maxQueueDepth we DROP the new message and bump m_dropped.
//       That counter is exposed via droppedCount() so MainWindow can show a
//       "X messages dropped" banner. Without this, a slow GUI thread could
//       grow Qt's internal event queue without bound and eventually OOM us.
// =============================================================================

#include <QObject>
#include <QUdpSocket>
#include <QThread>
#include <QByteArray>
#include <QSharedPointer>
#include <atomic>

#include "Structures.h"

// One parsed inbound message. Lives on the heap, ownership passed via a
// QSharedPointer so the producer (UDP thread) and consumer (GUI thread) can't
// race on the destructor and we never deep-copy the payload on the queued
// signal hop.
// Our own address on the Kavach bus. Datagrams addressed elsewhere are
// ignored. Exposed here (rather than living as a file-static in the .cpp)
// so the UI can tell the operator which id it is filtering on without
// hard-coding a second copy that could drift.
static constexpr quint8 kThisConsoleId = 101;

struct ParsedMessage
{
    STRUCT_MESSAGE_HEADER header;   // already byte-swapped to host order
    QByteArray            payload;  // exactly message_len bytes

    // Wall-clock time this datagram was pulled off the socket, in ms since
    // the Unix epoch. Captured HERE, on the receiver thread, rather than
    // downstream at model-insert time: the dispatcher batches on a ~30ms
    // tick and the queue is allowed to grow to m_maxQueueDepth, so a
    // timestamp taken at drain time drifts by however far behind the GUI
    // thread has fallen — seconds, under load. Event ordering is the whole
    // point of this tool, so the clock read happens as close to arrival as
    // we can get it.
    qint64 arrivalMs = 0;
};

using ParsedMessagePtr = QSharedPointer<ParsedMessage>;

// Register this typedef with Qt's metatype system so it can be used as the
// argument of a queued (cross-thread) signal/slot connection. The matching
// qRegisterMetaType<>() call lives in main().
Q_DECLARE_METATYPE(ParsedMessagePtr)

class UDPCommunication : public QThread
{
    Q_OBJECT

public:
    // 'port'          – UDP port to bind on 0.0.0.0.
    // 'maxQueueDepth' – soft cap on in-flight messages between the UDP thread
    //                   and the GUI thread. Above this we start dropping and
    //                   incrementing the drop counter.
    explicit UDPCommunication(quint16 port,
                              int     maxQueueDepth = 50000,
                              QObject *parent = nullptr);
    ~UDPCommunication() override;

    // Number of messages we have dropped since startup, either because the
    // queue was full or because the datagram failed validation. Safe to call
    // from any thread.
    quint64 droppedCount() const  { return m_dropped.load(std::memory_order_relaxed); }

    // Total messages we have successfully accepted (i.e. emitted upstream).
    quint64 receivedCount() const { return m_received.load(std::memory_order_relaxed); }

    // Current cross-thread queue depth (best-effort gauge).
    int currentQueueDepth() const { return m_queueDepth.load(std::memory_order_relaxed); }

    // The consumer (the GUI-side dispatcher) MUST call this exactly once per
    // ParsedMessagePtr it has received via messageReceived(), so we know how
    // deep the cross-thread backlog is and can apply backpressure.
    void notifyMessageConsumed() { notifyMessagesConsumed(1); }

    // Batched form of the above — one atomic instead of N. The dispatcher
    // drains a whole tick's worth at once, so this is the path that
    // actually gets used.
    void notifyMessagesConsumed(int n)
    {
        if (n > 0) m_queueDepth.fetch_sub(n, std::memory_order_relaxed);
    }

    // Thread entry point. Binds the socket, sets options, then hands control
    // to the Qt event loop. Returns when the thread is asked to quit().
    void run() override;

signals:
    // Fired (queued connection across threads) whenever a valid datagram is
    // accepted. The receiving slot owns the QSharedPointer; when it has
    // finished it must call notifyMessageConsumed() on the sender.
    void messageReceived(ParsedMessagePtr msg);

    // Fired once if we cannot bind the UDP socket. The original code called
    // ::exit(1) here, which is unfriendly in a desktop app — we let the GUI
    // surface the error instead.
    void bindFailed(QString reason);

    // Fired once when the bind succeeds, so the status bar can show a green
    // "listening" indicator.
    void bindSucceeded(quint16 port);

private slots:
    // Connected to QUdpSocket::readyRead. Drains every pending datagram in
    // a tight inner loop (still good throughput when many arrive between
    // ticks), but the OUTER loop is event-driven.
    void drainSocket();

private:
    quint16        m_recvPort;
    int            m_maxQueueDepth;
    QUdpSocket    *m_socket = nullptr;   // owned by this thread, created in run()

    // Atomics — touched from both threads, so they have to be lock-free on
    // any platform Qt supports. std::atomic<integral> is lock-free on x86/x64.
    std::atomic<quint64> m_dropped{0};
    std::atomic<quint64> m_received{0};
    std::atomic<int>     m_queueDepth{0};
};

#endif // UDPCOMMUNICATION_H
