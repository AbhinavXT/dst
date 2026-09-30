#ifndef UDPSENDER_H
#define UDPSENDER_H
// =====================================================================
//  udpsender.{h,cpp} -- transmit a built frame over UDP.
//
//  Two modes: sendOnce() fires a single datagram; startInterval() sends
//  every N ms until stop().
//
//  Interval sending asks for the frame each tick rather than repeating one
//  buffer. It used to repeat: the message-header seq advanced but the packet
//  body did not, so FRAME_NUM stayed put and the MAC and CRC — computed once
//  over that body — were identical on every datagram. A peer that uses frame
//  number for freshness or replay detection sees a stuck stream, which is not
//  what "send every N ms" leads anyone to expect. The fixed-buffer overload
//  is still here for the cases that genuinely want the same bytes twice, but
//  it now says so explicitly.
//  This is the ONLY place in DLConsole that transmits — the tool is a
//  receiver everywhere else — so sending is explicit, opt-in, and the
//  running/stopped state is observable via signals for the UI.
//
//  It transmits exactly the bytes handed to it. It does NOT add a
//  message-header envelope: whether a STRUCT_MESSAGE_HEADER must prefix
//  the packet on your bus is a deployment fact this class shouldn't
//  assume. Wrap the frame before calling if your setup needs it.
// =====================================================================
#include <QObject>
#include <QByteArray>
#include <QHostAddress>
#include <QUdpSocket>
#include <QTimer>
#include <QVector>
#include <functional>

class UdpSender : public QObject {
    Q_OBJECT
public:
    explicit UdpSender(QObject *parent = nullptr);

    // A prefix regenerated for every datagram (given a 0-based send index).
    // Used for the message header, whose seq_num must increment per send.
    // Empty/unset means no prefix.
    using PrefixFn = std::function<QByteArray(int sendIndex)>;

    // Produces the frame for a given 0-based send index. Returning an empty
    // array aborts the run — a build that starts failing mid-stream must stop
    // rather than fall back to stale bytes, which would look like it was
    // still working.
    using FrameFn = std::function<QByteArray(int sendIndex)>;

    // Where a frame goes. Host may be a name or a dotted-quad.
    struct Target {
        QString host;
        quint16 port = 0;
        bool operator==(const Target &o) const { return host == o.host && port == o.port; }
    };

    // ---- one frame, several destinations ---------------------------------
    //
    // A station talks to more than one loco, and sending the same frame to
    // four peers is one transmission with four recipients — NOT four
    // transmissions. So a tick builds ONE frame and writes it to every
    // target, and the send index advances once. Advancing it per destination
    // would give each peer a stream with gaps in it (1, 5, 9 …), and
    // FRAME_NUM and the MAC are derived from that index.
    //
    // Partial failure is survivable by design: a target that will not resolve
    // is reported and dropped at start, and a write that fails mid-run is
    // reported without stopping the others. The run only stops when EVERY
    // destination fails — one unreachable peer must not silence the rest, and
    // an unreported failure must not look like success.
    bool sendOnce(const QByteArray &frame, const QVector<Target> &targets,
                  const PrefixFn &prefix = {});
    void startInterval(const FrameFn &frameFn, const QVector<Target> &targets,
                       int intervalMs, const PrefixFn &prefix = {});

    // Single-destination forms, kept because most callers have exactly one.
    bool sendOnce(const QByteArray &frame, const QString &dest, quint16 port,
                  const PrefixFn &prefix = {});

    // Begin sending `frame` every intervalMs milliseconds. The first send is
    // immediate. Calling again replaces the current periodic send. `prefix`
    // (if set) is re-evaluated each tick, so a header seq_num advances.
    // Rebuild per tick. `frameFn` is called with the send index before each
    // datagram, so any field that must advance can.
    void startInterval(const FrameFn &frameFn, const QString &dest,
                       quint16 port, int intervalMs, const PrefixFn &prefix = {});

    // Repeat one fixed buffer. Kept for callers that really do want identical
    // bytes each time; prefer the FrameFn form for anything a peer will treat
    // as a live stream.
    void startIntervalFixed(const QByteArray &frame, const QString &dest,
                            quint16 port, int intervalMs, const PrefixFn &prefix = {});
    void stop();

    bool isRunning() const { return m_timer.isActive(); }

    // Frames sent, not datagrams: with four destinations one "send" is one
    // frame that went to four places. The distinction matters because this
    // is also the index FRAME_NUM and the header seq derive from.
    int  sentCount() const { return m_sent; }
    int  targetCount() const { return m_targets.size(); }

signals:
    void sent(int totalCount);        // a datagram went out (once or periodic)
    void error(const QString &what);
    void runningChanged(bool running);

private slots:
    void onTick();

private:
    struct Resolved { QHostAddress addr; quint16 port = 0; QString shown; };

    // Writes one frame to every resolved target. Returns the number that
    // succeeded; the caller stops only on zero.
    int writeAll(const QByteArray &frame, const PrefixFn &prefix);

    // Resolve the list, reporting the ones that cannot be resolved rather
    // than quietly sending to fewer places than were asked for.
    QVector<Resolved> resolveAll(const QVector<Target> &in);

    QUdpSocket m_sock;
    QTimer     m_timer;
    FrameFn    m_frameFn;
    QVector<Resolved> m_targets;
    int        m_sent = 0;          // frames, not datagrams
    PrefixFn   m_prefix;
};

#endif  // UDPSENDER_H
