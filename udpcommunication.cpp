#include "udpcommunication.h"

#include <QDebug>
#include <QDateTime>
#include <QHostAddress>
#include <QtEndian>
#include <cstring>

// 4 MiB kernel receive buffer. Default on Windows is ~64 KiB which is small
// enough that a short burst from Kavach can cause the OS to drop datagrams
// before we ever see them. 4 MiB covers ~2700 fully-loaded 1501-byte payloads.
// Receive-buffer size we request from the kernel. 4 MiB chosen to
// absorb burst arrival; at our typical message sizes (~50-150 bytes)
// that's room for ~25-30K messages in flight before the OS UDP queue
// overflows.
//
// Kernels typically cap this via net.core.rmem_max. On Linux the
// default cap is often 4 MiB anyway; asking for more is silently
// granted whatever's allowed. On Windows SO_RCVBUF generally accepts
// whatever we ask for.
//
// Burst losses beyond this are at the kernel level, not anything
// DLConsole can fix without OS-level tuning (raising
// net.core.rmem_max via sysctl).
static constexpr int kSocketRecvBufBytes = 4 * 1024 * 1024;

UDPCommunication::UDPCommunication(quint16 port, int maxQueueDepth, QObject *parent)
    : QThread(parent)
    , m_recvPort(port)
    , m_maxQueueDepth(maxQueueDepth)
{
    // Note: do NOT create the QUdpSocket here. We deliberately defer that to
    // run() so the socket's thread affinity is the worker thread. If we
    // created it here it would belong to whichever thread called the
    // constructor (the GUI thread), and queued events on the socket would be
    // delivered there instead of here.
}

UDPCommunication::~UDPCommunication()
{
    // Ask the event loop to exit, then wait for the thread to finish. Without
    // this, destroying a still-running QThread is a fatal error in Qt.
    if (isRunning()) {
        quit();
        wait(2000);   // 2-second grace period before forcing
    }
}

void UDPCommunication::run()
{
    // -- Build socket on this thread so its events fire here --------------
    m_socket = new QUdpSocket();   // no parent on purpose; we delete it below

    // Increase the OS-level receive buffer. This must be done BEFORE bind on
    // some platforms to take effect.
    m_socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption,
                              kSocketRecvBufBytes);

    const bool ok = m_socket->bind(QHostAddress::AnyIPv4,
                                   m_recvPort,
                                   QUdpSocket::ShareAddress
                                       | QUdpSocket::ReuseAddressHint);
    if (!ok) {
        // Tell the GUI rather than ::exit(1) — the user can change the port
        // in settings and retry, which is impossible if we just kill the app.
        emit bindFailed(m_socket->errorString());
        delete m_socket;
        m_socket = nullptr;
        return;
    }

    // Wire up the event-driven drain. Direct connection is correct here
    // because both signal and slot live on this same thread (we just created
    // m_socket on this thread).
    connect(m_socket, &QUdpSocket::readyRead,
            this,     &UDPCommunication::drainSocket,
            Qt::DirectConnection);

    emit bindSucceeded(m_recvPort);

    // Hand off to the Qt event loop. exec() returns when quit() is called
    // (from our destructor on shutdown).
    exec();

    // Cleanup once exec() returns.
    m_socket->close();
    delete m_socket;
    m_socket = nullptr;
}

void UDPCommunication::drainSocket()
{
    // Drain every pending datagram in this wake-up. There may be many if
    // several arrived between event-loop ticks, and we don't want to wait for
    // another readyRead() to handle them.
    while (m_socket && m_socket->hasPendingDatagrams()) {

        // We allocate a sized buffer for each datagram. pendingDatagramSize()
        // tells us exactly how many bytes are in the next pending datagram so
        // we don't waste a whole MAX_MSG_SIZE on every read.
        const qint64 dgramSize = m_socket->pendingDatagramSize();

        if (dgramSize <= 0 || dgramSize > MAX_MSG_SIZE) {
            // Malformed/oversize — discard and count as a drop. We still have
            // to read it to advance past it in the kernel queue.
            QByteArray scratch;
            scratch.resize(dgramSize > 0 ? dgramSize : 0);
            m_socket->readDatagram(scratch.data(), scratch.size());
            m_dropped.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        // Sanity check: we must at least be able to fit a header.
        if (dgramSize < static_cast<qint64>(sizeof(STRUCT_MESSAGE_HEADER))) {
            QByteArray scratch;
            scratch.resize(dgramSize);
            m_socket->readDatagram(scratch.data(), scratch.size());
            m_dropped.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        // Read the datagram into a tight buffer. The arrival clock read
        // happens per-datagram rather than once per readyRead() wake: a
        // single wake can drain a long burst, and datagrams that land
        // mid-drain really did arrive later.
        const qint64 arrivalMs = QDateTime::currentMSecsSinceEpoch();

        QByteArray buf;
        buf.resize(dgramSize);
        const qint64 got = m_socket->readDatagram(buf.data(), buf.size());
        if (got <= 0) {
            // Spurious wake-up; keep draining.
            continue;
        }
        buf.truncate(got);

        // Parse the header, swapping each field from little-endian (Kavach
        // uses LE wire format). On x86/x64 these are no-ops but they document
        // intent and stay correct on big-endian hosts.
        STRUCT_MESSAGE_HEADER hdr;
        std::memcpy(&hdr, buf.constData(), sizeof(hdr));
        hdr.source_id      = qFromLittleEndian<quint8> (hdr.source_id);
        hdr.destination_id = qFromLittleEndian<quint8> (hdr.destination_id);
        hdr.message_id     = qFromLittleEndian<quint8> (hdr.message_id);
        hdr.message_len    = qFromLittleEndian<quint16>(hdr.message_len);
        hdr.kvchId         = qFromLittleEndian<quint16>(hdr.kvchId);

        // Filter: only messages addressed to *this* console (dest 101).
        // Anything else is for a different consumer on the same multicast/
        // broadcast — silently ignored, NOT counted as a drop because we
        // were never the intended recipient.
        if (hdr.destination_id != kThisConsoleId) {
            continue;
        }

        // Validate that message_len fits inside the datagram we received.
        // A spoofed/corrupt header could otherwise lie about its size.
        const int payloadAvail = buf.size() - static_cast<int>(sizeof(hdr));
        if (hdr.message_len > payloadAvail || hdr.message_len > MAX_MSG_SIZE) {
            m_dropped.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        // Backpressure: refuse new messages if the GUI thread is too far
        // behind. m_queueDepth is "messages emitted but not yet consumed".
        const int depth = m_queueDepth.load(std::memory_order_relaxed);
        if (depth >= m_maxQueueDepth) {
            m_dropped.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        // Build the small parcel. Only message_len bytes of payload are
        // copied — much cheaper than the original 8000-byte memcpy per msg.
        auto msg = QSharedPointer<ParsedMessage>::create();
        msg->header    = hdr;
        msg->payload   = QByteArray(buf.constData() + sizeof(hdr), hdr.message_len);
        msg->arrivalMs = arrivalMs;

        // Increment depth BEFORE emitting, so a fast consumer that's already
        // running can never decrement below zero.
        m_queueDepth.fetch_add(1, std::memory_order_relaxed);
        m_received.fetch_add(1, std::memory_order_relaxed);
        emit messageReceived(msg);
    }
}
