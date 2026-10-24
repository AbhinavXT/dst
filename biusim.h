#ifndef BIUSIM_H
#define BIUSIM_H
// =============================================================================
//  BiuSimulator (session 360) -- a Brake Interface Unit on the far side of the
//  VCC's CAN-over-UDP gateway, for testing the firmware's brake logic
//  (brake_pressure_interface_user.c) without a BIU.
//
//  What a BIU does, after RDSO MP.0.01.00.31 Rev.05 Annexure-1:
//    * network manager: its boot-up, its heartbeat every 500 ms, and NMT
//      "start remote node" to KAVACH while KAVACH is not operational;
//    * watches KAVACH's heartbeat and command frames, and when they stop
//      (CAN fail) applies emergency (4 c), or holds or releases, as set;
//    * answers with three status frames every 250 ms: pressures with their
//      validity bits, codes (isolation counter, fault, display-only), and
//      three bytes of discrete bits;
//    * holds a fault or display code until KAVACH acknowledges it (3), then
//      sends the next one or 0;
//    * runs the brake panel test when KAVACH asks for it.
//  Every field it sends can be set by hand ("Manual", forced bits) for fault
//  testing. What it would apply (BP / BC targets) is targets(); the bench's
//  BrakePlant moves the pressures and feeds them back with setLivePressure.
//
//  Time is passed in (tick(now), receive(..., now)), so the tests drive it
//  exactly; the Brake Test Bench window calls them from a timer and its
//  socket. setSendSink() replaces the socket for tests.
// =============================================================================
#include "biuprotocol.h"

#include <QHostAddress>
#include <QObject>
#include <QVector>

#include <functional>

class QUdpSocket;

class BiuSimulator : public QObject
{
    Q_OBJECT
public:
    enum class BitMode   { Auto, On, Off };
    enum class ValidMode { Auto, Valid, Invalid };
    enum class OnLoss    { ApplyEb, Hold, Release };
    enum class PanelTest { Idle, InProgress, Passed, Failed };

    // What the BIU applies now: a BP and / or BC target, or nothing (the
    // loco pilot's handles: released).
    struct Targets {
        bool    controlling = false;
        bool    bpSet = false, bcSet = false;
        double  bp = 0, bc = 0;
        QString why;
    };
    struct LogItem {
        qint64     ms = 0;
        bool       tx = false;
        Biu::Frame frame;
        QString    peer;        // "192.168.25.126:50001"
        QString    note;        // what is wrong or worth knowing, empty if nothing
    };

    explicit BiuSimulator(QObject *parent = nullptr);
    ~BiuSimulator() override;

    // ---- transport --------------------------------------------------------------
    bool    listen(quint16 port, QString *why = nullptr);   // UDP, any address
    void    close();
    bool    isListening() const;
    quint16 listenPort() const { return m_listenPort; }
    // Where the BIU's frames go: no hosts = whoever sent the last frame.
    // Session 361: a list, each frame one datagram to every address on it. A
    // loco has four VCCs; one sends the command, but all four keep their own
    // BIU state, so all four must hear the heartbeat, NMT and status frames.
    // "a.b.c.d", or "a.b.c.d:port" for a port other than replyPort().
    bool        setReplyHosts(const QStringList &hosts, QString *why = nullptr);   // refused whole (and kept) on a bad entry
    QStringList replyHosts() const;
    void        setReplyHost(const QString &host);   // one address; empty = the sender
    static bool parseHosts(const QString &text, QStringList *hosts, QString *why);   // "a, b; c" -> hosts
    bool        inReplyList(const QString &host) const;   // false in sender mode
    void        setReplyPort(quint16 port) { m_replyPort = port; }
    quint16     replyPort() const { return m_replyPort; }
    QStringList replyTargets() const;     // "192.168.25.16:50001" each; empty while unknown
    QString     replyTarget() const;      // the same, joined by ", "
    QString     lastPeer() const;         // who sent the last frame, "" if nobody
    using SendFn = std::function<void(const QByteArray &datagram)>;
    void    setSendSink(const SendFn &fn) { m_sink = fn; }
    // For tests: each datagram with where it would go ("a.b.c.d:port", or ""
    // while nobody is known). Takes the place of setSendSink.
    using SendToFn = std::function<void(const QByteArray &datagram, const QString &to)>;
    void    setSendToSink(const SendToFn &fn) { m_sinkTo = fn; }
    void    receive(const QByteArray &datagram, const QString &fromHost, quint16 fromPort, qint64 nowMs);

    // ---- running ----------------------------------------------------------------
    void setRunning(bool on, qint64 nowMs);
    bool running() const { return m_running; }
    void tick(qint64 nowMs);              // at least every 50 ms

    // ---- network management -----------------------------------------------------
    void   setBiuState(quint8 state) { m_biuState = state; emit changed(); }
    quint8 biuState() const { return m_biuState; }
    void   setHeartbeatOn(bool on) { m_heartbeatOn = on; emit changed(); }
    bool   heartbeatOn() const { return m_heartbeatOn; }
    void   setHeartbeatPeriodMs(int ms) { m_hbPeriod = qMax(10, ms); }
    int    heartbeatPeriodMs() const { return m_hbPeriod; }
    void   setAutoStart(bool on) { m_autoStart = on; }
    bool   autoStart() const { return m_autoStart; }
    void   sendNmt(quint8 command, quint8 node, qint64 nowMs);
    int    kavachState() const { return m_kavachState; }        // -1: never heard
    qint64 kavachHeartbeatMs() const { return m_kavachHbMs; }   // 0: never
    void   setKavachTimeoutMs(int ms) { m_kavachTimeout = qMax(100, ms); }
    int    kavachTimeoutMs() const { return m_kavachTimeout; }
    bool   kavachLost(qint64 nowMs) const;
    void   setOnLoss(OnLoss o) { m_onLoss = o; emit changed(); }
    OnLoss onLoss() const { return m_onLoss; }

    // ---- status frames ----------------------------------------------------------
    void setStatusPeriodMs(int ms) { m_stPeriod = qMax(10, ms); }
    int  statusPeriodMs() const { return m_stPeriod; }
    void setFrameOn(int which, bool on);  // 0 pressures, 1 codes, 2 discretes
    bool frameOn(int which) const;
    void setSilent(bool on) { m_silent = on; emit changed(); }   // nothing at all, heartbeat included
    bool silent() const { return m_silent; }

    void      setLivePressure(int id, double kgcm2);
    double    livePressure(int id) const;
    void      setPressureManual(int id, bool manual, double kgcm2);
    bool      pressureManual(int id) const;
    double    pressureManualValue(int id) const;
    void      setValidMode(int id, ValidMode m);
    ValidMode validMode(int id) const;

    void    setIsolationCounter(quint16 n) { m_isolations = n; emit changed(); }
    quint16 isolationCounter() const { return m_isolations; }
    void    setCountIsolations(bool on) { m_countIsolations = on; }
    bool    countIsolations() const { return m_countIsolations; }
    void    setRaiseIsolatedFault(bool on) { m_raiseIsolated = on; }
    bool    raiseIsolatedFault() const { return m_raiseIsolated; }
    void    raiseFault(quint16 code);
    void    raiseDisplay(quint16 code);
    void    clearFaults();
    void    clearDisplays();
    QVector<quint16> faultQueue() const { return m_faults; }
    QVector<quint16> displayQueue() const { return m_displays; }
    // Hold each code until KAVACH acknowledges it (Annexure-1 3); off: keep
    // sending it, acked or not, until cleared by hand.
    void    setWaitForAck(bool on) { m_waitForAck = on; }
    bool    waitForAck() const { return m_waitForAck; }
    qint64  lastFaultAckMs() const { return m_faultAckLatency; }      // -1: none yet
    qint64  lastDisplayAckMs() const { return m_displayAckLatency; }

    void    setBitMode(int byte, int bit, BitMode m);   // byte 1-3, bit 0-7
    BitMode bitMode(int byte, int bit) const;
    bool    autoBit(int byte, int bit, qint64 nowMs) const;

    void      setPanelTestSeconds(double s) { m_testSeconds = qMax(0.0, s); }
    double    panelTestSeconds() const { return m_testSeconds; }
    void      setPanelTestPasses(bool on) { m_testPasses = on; }
    bool      panelTestPasses() const { return m_testPasses; }
    PanelTest panelTest(qint64 nowMs) const;

    // ---- what it does -----------------------------------------------------------
    bool        isolated() const;          // manual isolate, or KAVACH's isolate bit
    Targets     targets(qint64 nowMs) const;
    Biu::Status buildStatus(qint64 nowMs) const;
    Biu::Status lastStatus() const { return m_lastStatus; }

    bool         hasCommand() const { return m_commands > 0; }
    Biu::Command lastCommand() const { return m_cmd; }
    qint64       lastCommandMs() const { return m_cmdMs; }
    int          commandCount() const { return m_commands; }
    double       commandPeriodMs() const;  // mean of the last few gaps; 0 if unknown
    int          framesSent() const { return m_sent; }
    int          datagramsSent() const { return m_datagrams; }   // a frame to 4 VCCs is 4
    int          framesReceived() const { return m_received; }
    int          rejected() const { return m_rejected; }

signals:
    void logged(const BiuSimulator::LogItem &item);
    void changed();
    void problem(const QString &what);

private:
    void send(const Biu::Frame &f, qint64 nowMs, const QString &note = QString());
    void sendStatus(qint64 nowMs);
    void onCommand(const Biu::Command &c, int bytes, qint64 nowMs, QString *note);
    void noteIsolation(qint64 nowMs);
    void readSocket();

    // transport
    QUdpSocket  *m_sock = nullptr;
    quint16      m_listenPort = 0;
    struct Dest {
        QHostAddress addr;
        quint16      port = 0;   // 0: m_replyPort
    };
    QVector<Dest> m_dests;       // empty: the sender
    quint16      m_replyPort = 50001;
    QHostAddress m_peerAddr;
    quint16      m_peerPort = 0;
    SendFn       m_sink;
    SendToFn     m_sinkTo;
    bool         m_warnedNoTarget = false;

    // running
    bool   m_running = false;
    bool   m_silent = false;
    qint64 m_hbDue = 0, m_stDue = 0;

    // NMT
    quint8 m_biuState = Biu::Operational;
    bool   m_heartbeatOn = true;
    int    m_hbPeriod = 500;
    bool   m_autoStart = true;
    int    m_kavachState = -1;
    qint64 m_kavachHbMs = 0;
    int    m_kavachTimeout = 1500;
    OnLoss m_onLoss = OnLoss::ApplyEb;

    // status
    int       m_stPeriod = 250;
    bool      m_frameOn[3] = { true, true, true };
    double    m_live[Biu::kPressures] = { 5.0, 0.0, 9.0, 5.0, 0.0, 0.0, 6.0 };
    bool      m_manual[Biu::kPressures] = {};
    double    m_manualKg[Biu::kPressures] = { 5.0, 0.0, 9.0, 5.0, 0.0, 0.0, 6.0 };
    ValidMode m_valid[Biu::kPressures] = {};
    BitMode   m_bits[3][8] = {};
    quint16   m_isolations = 0;
    bool      m_countIsolations = true;
    bool      m_raiseIsolated = true;
    bool      m_wasIsolated = false;
    QVector<quint16> m_faults, m_displays;
    qint64    m_faultSentMs = 0, m_displaySentMs = 0;      // when the front code first went out
    qint64    m_faultAckLatency = -1, m_displayAckLatency = -1;
    bool      m_waitForAck = true;
    double    m_testSeconds = 10;
    bool      m_testPasses = true;
    qint64    m_testStartMs = 0;                            // 0: no test asked for
    Biu::Status m_lastStatus;

    // commands
    Biu::Command   m_cmd;
    qint64         m_cmdMs = 0;
    int            m_commands = 0;
    QVector<qint64> m_gaps;
    int            m_sent = 0, m_datagrams = 0, m_received = 0, m_rejected = 0;
};

#endif  // BIUSIM_H
