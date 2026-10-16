#ifndef FLASHERFAKEBOARD_H
#define FLASHERFAKEBOARD_H
// =============================================================================
//  The flasher suites' fake Kavach updater (moved out of test_flasher.cpp in
//  session 291, so test_session291 can run several at once: one FakeBoard
//  per "VCC", each on its own port of 127.0.0.1).
//
//  FakeBoard is C++ rather than engine/board_sim.py so the gate needs no
//  Python for these suites and so it can be told to reject one card type. It
//  speaks the same four packets with the same layouts; if the engine's wire
//  format ever changed, the runs would fail at the handshake.
// =============================================================================
#include "flash_engine.h"

#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QString>
#include <QUdpSocket>

#include <cstring>

namespace FlasherTest {

inline QByteArray patternBytes(int size, int seed)
{
    QByteArray bytes(size, '\0');
    quint32 state = 0x9E3779B9u ^ static_cast<quint32>(seed);
    for (int index = 0; index < size; ++index) {
        state = state * 1664525u + 1013904223u;
        bytes[index] = static_cast<char>(state >> 24);
    }
    return bytes;
}

inline QString writeFile(const QString &directory, const QString &name, const QByteArray &bytes)
{
    const QString path = QDir(directory).filePath(name);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(bytes);
    }
    return path;
}

// ---- FakeBoard: a minimal updater on 127.0.0.1 ----------------------------------

#pragma pack(push, 1)
struct FakeMeta   { quint8 type; quint8 card; quint32 size; quint32 crc; quint8 sha[32]; };
struct FakeChunk  { quint8 type; quint32 offset; quint32 size; quint32 crc; };
struct FakeStatus { quint8 type; quint8 status; quint32 crc; quint32 total; quint32 got; quint16 bitmapBytes; };
#pragma pack(pop)

class FakeBoard : public QObject
{
public:
    FakeBoard()
    {
        m_socket.bind(QHostAddress(QHostAddress::LocalHost), 0);
        QObject::connect(&m_socket, &QUdpSocket::readyRead, this, [this]() { drain(); });
    }
    quint16 port() const { return m_socket.localPort(); }

    // The chassis power switch. Power-on starts the updater and its 5 s
    // listen window (FW_WATCHDOG_DEFAULT_MS); power-off or leaving for the
    // application makes the board ignore the flasher entirely.
    void powerOn()
    {
        m_mode = Mode::Updater;
        m_hasSession = false;
        m_held = false;
        m_lastPacket.start();
    }
    void powerOff() { m_mode = Mode::Off; }
    bool inApplication() const { return m_mode == Mode::Application; }

    int rejectCardType = 0;     // IMAGE_FAIL every image for this card type
    QList<int> metaCards;       // card types seen in META, in order

    // Session 291: a lossy link (every Nth DATA is lost, so the board's
    // bitmap has holes and the engine must resend), and when DATA came,
    // on one clock shared by every board, to see transfers overlap.
    int    dropEvery = 0;
    int    dataSeen = 0;        // DATA packets that reached the board, dropped ones too
    qint64 firstDataMs = -1;
    qint64 lastDataMs = -1;
    // Session 296: the 2.6 updater. With v26 off the board is a classic one
    // and ignores HOLD and BOOT, as an updater before 2.2 does.
    bool v26 = false;
    int  rejectReason = 0;      // REJECTED (with this reason) instead of COMPLETE
    bool loseComplete = false;  // all three COMPLETE replies lost on the wire
    int  holdsSeen = 0;
    int  bootsSeen = 0;
    bool held() const { return m_held; }

    static qint64 sharedClockMs()
    {
        static QElapsedTimer clock;
        if (!clock.isValid()) clock.start();
        return clock.elapsed();
    }

private:
    enum class Mode { Off, Updater, Application };

    struct Session {
        int card = 0;
        quint32 size = 0;
        quint32 crc = 0;
        QByteArray buffer;
        QByteArray have;
        quint32 blocks = 0;
        quint32 got = 0;
        quint8 state = 1;       // INCOMPLETE
    };

    void drain()
    {
        while (m_socket.hasPendingDatagrams()) {
            QByteArray packet;
            packet.resize(static_cast<int>(m_socket.pendingDatagramSize()));
            QHostAddress from;
            quint16 fromPort = 0;
            m_socket.readDatagram(packet.data(), packet.size(), &from, &fromPort);
            if (packet.isEmpty()) {
                continue;
            }
            handle(packet, from, fromPort);
        }
    }

    void handle(const QByteArray &packet, const QHostAddress &from, quint16 fromPort)
    {
        if (m_mode != Mode::Updater) {
            return;   // off, or running the application: nobody is listening
        }
        // The updater's boot watchdog: 5 s without a firmware packet and it
        // boots the application (checked lazily, on the next arrival); held
        // (2.6), 2 minutes.
        if (m_lastPacket.elapsed() > (m_held ? 120000 : 5000)) {
            m_mode = Mode::Application;
            return;
        }
        m_lastPacket.restart();

        const quint8 kind = static_cast<quint8>(packet.at(0));
        if (v26 && (kind == 7 || kind == 8)) {
            // HOLD / BOOT, both answered with INFO (layout v3, 136 bytes).
            QByteArray info(136, '\0');
            info[0] = 5;
            info[1] = 3;
            if (kind == 7) {
                ++holdsSeen;
                m_held = true;
            } else {
                ++bootsSeen;
            }
            m_socket.writeDatagram(info, from, fromPort);
            if (kind == 8) {
                m_mode = Mode::Application;   // FW_BOOT_DELAY_MS later
            }
            return;
        }
        if (kind == 0 && packet.size() >= static_cast<int>(sizeof(FakeMeta))) {
            FakeMeta meta;
            std::memcpy(&meta, packet.constData(), sizeof(meta));
            // Like handle_meta_packet(): every META re-arms the session.
            {
                m_session = Session();
                m_session.card = meta.card;
                m_session.size = meta.size;
                m_session.crc = meta.crc;
                m_session.blocks = (meta.size + kflash::kBlockSize - 1) / kflash::kBlockSize;
                m_session.buffer = QByteArray(static_cast<int>(meta.size), '\0');
                m_session.have = QByteArray(static_cast<int>((m_session.blocks + 7) / 8), '\0');
                m_hasSession = true;
                metaCards.append(meta.card);
            }
        } else if (kind == 1 && m_hasSession && packet.size() >= static_cast<int>(sizeof(FakeChunk))) {
            ++dataSeen;
            lastDataMs = sharedClockMs();
            if (firstDataMs < 0) firstDataMs = lastDataMs;
            if (dropEvery > 0 && dataSeen % dropEvery == 0) {
                return;   // lost on the wire
            }
            FakeChunk chunk;
            std::memcpy(&chunk, packet.constData(), sizeof(chunk));
            const quint32 block = chunk.offset / kflash::kBlockSize;
            if (block < m_session.blocks && chunk.offset + chunk.size <= m_session.size) {
                const char *payload = packet.constData() + sizeof(FakeChunk);
                std::memcpy(m_session.buffer.data() + chunk.offset, payload, chunk.size);
                const int byteIndex = static_cast<int>(block >> 3);
                const char byte = m_session.have.at(byteIndex);
                if (((byte >> (block & 7)) & 1) == 0) {
                    m_session.have[byteIndex] = static_cast<char>(byte | (1 << (block & 7)));
                    ++m_session.got;
                }
            }
        } else if (kind == 2) {
            FakeStatus status;
            std::memset(&status, 0, sizeof(status));
            status.type = 3;
            if (!m_hasSession) {
                status.status = 2;   // NEED_META
                m_socket.writeDatagram(reinterpret_cast<const char *>(&status), sizeof(status), from, fromPort);
                return;
            }
            if (m_session.got == m_session.blocks && m_session.state == 1) {
                const quint32 crc = kflash::image_crc32(
                    reinterpret_cast<const uint8_t *>(m_session.buffer.constData()),
                    static_cast<size_t>(m_session.buffer.size()));
                const bool good = (crc == m_session.crc) && (m_session.card != rejectCardType);
                if (good && v26 && rejectReason != 0) {
                    m_session.state = 4;   // REJECTED: intact, but refused
                } else if (good) {
                    m_session.state = 0;   // COMPLETE
                } else {
                    m_session.state = 3;   // IMAGE_FAIL
                }
            }
            status.status = m_session.state;
            status.crc = m_session.crc;
            status.total = m_session.blocks;
            status.got = m_session.got;
            status.bitmapBytes = static_cast<quint16>(m_session.have.size());
            QByteArray reply(reinterpret_cast<const char *>(&status), sizeof(status));
            if (m_session.state == 4) {
                status.bitmapBytes = 0;
                reply = QByteArray(reinterpret_cast<const char *>(&status), sizeof(status));
                reply.append(static_cast<char>(rejectReason));
                m_socket.writeDatagram(reply, from, fromPort);
                return;
            }
            reply.append(m_session.have);
            if (m_session.state == 0 && m_held && m_session.card != 1) {
                // 2.6, held VCC, a card image: it relays it over CAN, clears
                // its session and waits in the updater for BOOT.
                m_socket.writeDatagram(reply, from, fromPort);
                m_socket.writeDatagram(reply, from, fromPort);
                m_socket.writeDatagram(reply, from, fromPort);
                m_hasSession = false;
                return;
            }
            if (m_session.state == 0 && loseComplete) {
                m_mode = Mode::Application;   // flashed; nothing reached the PC
                return;
            }
            if (m_session.state == 0) {
                // COMPLETE goes out FW_COMPLETE_REPEATS times, then the board
                // writes flash and Boot_Switch() jumps into the application.
                m_socket.writeDatagram(reply, from, fromPort);
                m_socket.writeDatagram(reply, from, fromPort);
                m_socket.writeDatagram(reply, from, fromPort);
                m_mode = Mode::Application;
                return;
            }
            m_socket.writeDatagram(reply, from, fromPort);
        }
    }

    QUdpSocket    m_socket;
    Session       m_session;
    bool          m_hasSession = false;
    bool          m_held = false;
    Mode          m_mode = Mode::Off;
    QElapsedTimer m_lastPacket;
};

}  // namespace FlasherTest

#endif  // FLASHERFAKEBOARD_H
