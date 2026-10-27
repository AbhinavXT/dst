#include "biusim.h"

#include <QDateTime>
#include <QNetworkDatagram>
#include <QRegularExpression>
#include <QUdpSocket>

using namespace Biu;

namespace {
// Braking, as the BIU's own bits mean it: BP below charged, or BC above empty.
constexpr double kBpReleased = 4.95;
constexpr double kBcReleased = 0.05;
constexpr double kBpEmergency = 0.3;
}

BiuSimulator::BiuSimulator(QObject *parent) : QObject(parent) {}

BiuSimulator::~BiuSimulator() { close(); }

// ---- transport ------------------------------------------------------------------

bool BiuSimulator::listen(quint16 port, QString *why)
{
    close();
    m_sock = new QUdpSocket(this);
    if (!m_sock->bind(QHostAddress::AnyIPv4, port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        if (why) *why = m_sock->errorString();
        delete m_sock;
        m_sock = nullptr;
        return false;
    }
    m_listenPort = m_sock->localPort();
    connect(m_sock, &QUdpSocket::readyRead, this, &BiuSimulator::readSocket);
    emit changed();
    return true;
}

void BiuSimulator::close()
{
    if (!m_sock) return;
    m_sock->close();
    m_sock->deleteLater();
    m_sock = nullptr;
    emit changed();
}

bool BiuSimulator::isListening() const { return m_sock != nullptr; }

void BiuSimulator::readSocket()
{
    while (m_sock && m_sock->hasPendingDatagrams()) {
        const QNetworkDatagram d = m_sock->receiveDatagram();
        QHostAddress from = d.senderAddress();
        bool v4 = false;
        const quint32 ip4 = from.toIPv4Address(&v4);
        if (v4) from = QHostAddress(ip4);    // "::ffff:a.b.c.d" -> "a.b.c.d"
        receive(d.data(), from.toString(), quint16(d.senderPort()), QDateTime::currentMSecsSinceEpoch());
    }
}

bool BiuSimulator::parseHosts(const QString &text, QStringList *hosts, QString *why)
{
    // Strict dotted quads: QHostAddress alone takes "192.168.25" as
    // 192.168.0.25 (inet_aton shorthand) and would answer the wrong machine.
    static const QRegularExpression quad(QStringLiteral(
        "^(25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)(\\.(25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)){3}(:(\\d{1,5}))?$"));
    QStringList out;
    const QStringList tokens = text.split(QRegularExpression(QStringLiteral("[\\s,;]+")));
    for (const QString &token : tokens) {
        if (token.isEmpty()) continue;
        const QRegularExpressionMatch m = quad.match(token);
        const int port = m.hasMatch() && !m.captured(5).isEmpty() ? m.captured(5).toInt() : -1;
        if (!m.hasMatch() || port == 0 || port > 65535) {
            if (why) *why = tr("“%1” is not an IPv4 address (a.b.c.d, or a.b.c.d:port)").arg(token);
            return false;
        }
        if (out.contains(token)) {
            if (why) *why = tr("%1 is listed twice").arg(token);
            return false;
        }
        out << token;
    }
    if (hosts) *hosts = out;
    return true;
}

bool BiuSimulator::setReplyHosts(const QStringList &hosts, QString *why)
{
    QStringList checked;
    if (!parseHosts(hosts.join(QLatin1Char(',')), &checked, why)) return false;
    QVector<Dest> dests;
    for (const QString &h : checked) {
        const int colon = h.indexOf(QLatin1Char(':'));
        Dest d;
        d.addr = QHostAddress(colon < 0 ? h : h.left(colon));
        if (colon >= 0) d.port = quint16(h.mid(colon + 1).toInt());
        dests << d;
    }
    m_dests = dests;
    m_warnedNoTarget = false;
    emit changed();
    return true;
}

void BiuSimulator::setReplyHost(const QString &host)
{
    if (!setReplyHosts(host.trimmed().isEmpty() ? QStringList() : QStringList{ host.trimmed() })) {
        // Not an address: nowhere to send, said by send() as before.
        m_dests = { Dest() };
        emit changed();
    }
}

QStringList BiuSimulator::replyHosts() const
{
    QStringList out;
    for (const Dest &d : m_dests)
        out << (d.port ? QStringLiteral("%1:%2").arg(d.addr.toString()).arg(d.port) : d.addr.toString());
    return out;
}

bool BiuSimulator::inReplyList(const QString &host) const
{
    const QHostAddress a(host);
    for (const Dest &d : m_dests)
        if (d.addr == a) return true;
    return false;
}

QStringList BiuSimulator::replyTargets() const
{
    QStringList out;
    if (m_dests.isEmpty()) {
        if (!m_peerAddr.isNull()) out << QStringLiteral("%1:%2").arg(m_peerAddr.toString()).arg(m_replyPort);
        return out;
    }
    for (const Dest &d : m_dests)
        if (!d.addr.isNull()) out << QStringLiteral("%1:%2").arg(d.addr.toString()).arg(d.port ? d.port : m_replyPort);
    return out;
}

QString BiuSimulator::replyTarget() const { return replyTargets().join(QStringLiteral(", ")); }

QString BiuSimulator::lastPeer() const
{
    return m_peerAddr.isNull() ? QString() : QStringLiteral("%1:%2").arg(m_peerAddr.toString()).arg(m_peerPort);
}

void BiuSimulator::send(const Frame &f, qint64 nowMs, const QString &note)
{
    const QByteArray datagram = toDatagram(f);
    LogItem item;
    item.ms = nowMs;
    item.tx = true;
    item.frame = f;
    item.note = note;
    const QStringList to = replyTargets();
    if (m_sink || m_sinkTo) {
        // Tests: one call per datagram, as the socket would send them.
        const QStringList each = to.isEmpty() ? QStringList{ QString() } : to;
        for (const QString &target : each) {
            if (m_sinkTo) m_sinkTo(datagram, target);
            else          m_sink(datagram);
            ++m_datagrams;
        }
        item.peer = to.join(QStringLiteral(", "));
    } else {
        if (!m_sock || to.isEmpty()) {
            // Nowhere to send yet: said once, not every 250 ms.
            if (!m_warnedNoTarget) {
                m_warnedNoTarget = true;
                emit problem(!m_sock ? tr("Not listening: nothing is sent.")
                                     : tr("No VCC address yet: frames go out once the VCC sends one, "
                                          "or set the VCCs' addresses on the Network tab."));
            }
            return;
        }
        QStringList reached;
        for (const QString &target : to) {
            const int colon = target.lastIndexOf(QLatin1Char(':'));
            const QHostAddress addr(target.left(colon));
            const quint16 port = quint16(target.mid(colon + 1).toInt());
            if (m_sock->writeDatagram(datagram, addr, port) < 0) {
                emit problem(tr("Send to %1 failed: %2").arg(target, m_sock->errorString()));
                continue;
            }
            ++m_datagrams;
            reached << target;
        }
        if (reached.isEmpty()) return;
        m_warnedNoTarget = false;
        item.peer = reached.join(QStringLiteral(", "));
    }
    ++m_sent;
    emit logged(item);
}

// ---- running --------------------------------------------------------------------

void BiuSimulator::setRunning(bool on, qint64 nowMs)
{
    if (on == m_running) return;
    m_running = on;
    if (on) {
        // Boot-up first (Annexure-1 2.1), then the heartbeat on its period.
        if (m_heartbeatOn && !m_silent) send({ kCobHeartbeatBiu, QByteArray(1, char(BootUp)) }, nowMs, tr("boot-up"));
        m_hbDue = nowMs + m_hbPeriod;
        m_stDue = nowMs;
        m_wasIsolated = isolated();
    }
    emit changed();
}

void BiuSimulator::tick(qint64 nowMs)
{
    if (!m_running) return;
    noteIsolation(nowMs);
    if (m_silent) return;

    if (nowMs >= m_hbDue) {
        if (m_heartbeatOn) send({ kCobHeartbeatBiu, QByteArray(1, char(m_biuState)) }, nowMs);
        // The BIU starts KAVACH, and says it again while KAVACH is not
        // operational ("continuously, whenever required", 4).
        if (m_autoStart && m_kavachState >= 0 && m_kavachState != Operational && !kavachLost(nowMs))
            sendNmt(NmtStart, kKavachNode, nowMs);
        m_hbDue = qMax(m_hbDue + m_hbPeriod, nowMs + 1);
    }
    if (nowMs >= m_stDue) {
        sendStatus(nowMs);
        m_stDue = qMax(m_stDue + m_stPeriod, nowMs + 1);
    }
}

void BiuSimulator::sendNmt(quint8 command, quint8 node, qint64 nowMs)
{
    QByteArray d(2, '\0');
    d[0] = char(command);
    d[1] = char(node);
    send({ kCobNmt, d }, nowMs);
}

bool BiuSimulator::kavachLost(qint64 nowMs) const
{
    const qint64 last = qMax(m_kavachHbMs, m_cmdMs);
    return m_running && last > 0 && nowMs - last > m_kavachTimeout;
}

void BiuSimulator::sendStatus(qint64 nowMs)
{
    const Status s = buildStatus(nowMs);
    m_lastStatus = s;
    if (m_frameOn[0]) send({ kCobPressures, encodePressures(s) }, nowMs);
    if (m_frameOn[1]) {
        send({ kCobCodes, encodeCodes(s) }, nowMs);
        // A code is "received" once it has gone out: its ack may come now.
        if (s.faultCode && !m_faultSentMs)     m_faultSentMs = nowMs;
        if (s.displayCode && !m_displaySentMs) m_displaySentMs = nowMs;
    }
    if (m_frameOn[2]) send({ kCobDiscretes, encodeDiscretes(s) }, nowMs);
    emit changed();
}

void BiuSimulator::setFrameOn(int which, bool on) { if (which >= 0 && which < 3) m_frameOn[which] = on; }
bool BiuSimulator::frameOn(int which) const { return which >= 0 && which < 3 && m_frameOn[which]; }

// ---- receiving ------------------------------------------------------------------

void BiuSimulator::receive(const QByteArray &datagram, const QString &fromHost, quint16 fromPort, qint64 nowMs)
{
    LogItem item;
    item.ms = nowMs;
    item.peer = QStringLiteral("%1:%2").arg(fromHost).arg(fromPort);
    QString why;
    if (!fromDatagram(datagram, &item.frame, &why)) {
        ++m_rejected;
        item.frame.data = datagram;
        item.note = tr("Not a frame in the UDP link's format: %1").arg(why);
        emit logged(item);
        emit changed();
        return;
    }
    ++m_received;
    m_peerAddr = QHostAddress(fromHost);
    m_peerPort = fromPort;
    const Frame &f = item.frame;

    switch (f.cob) {
    case kCobHeartbeatKavach:
        if (f.data.isEmpty()) { item.note = tr("no state byte"); break; }
        m_kavachState = quint8(f.data.at(0));
        m_kavachHbMs = nowMs;
        if (f.data.size() != 1) item.note = tr("%1 bytes; a heartbeat is 1").arg(f.data.size());
        break;
    case kCobCommand:
        if (f.data.size() < kDataLen) { item.note = tr("%1 bytes; a command is 8: not taken").arg(f.data.size()); break; }
        {
            Command c;
            decodeCommand(f.data, &c);
            onCommand(c, f.data.size(), nowMs, &item.note);
            // Session 361: the commanding VCC is one of the four; left off
            // the list, it hears nothing back and reads the BIU as lost.
            if (!m_dests.isEmpty() && !inReplyList(fromHost)) {
                const QString off = tr("from %1, which is not in the reply list: it is sent nothing").arg(fromHost);
                item.note = item.note.isEmpty() ? off : item.note + QStringLiteral("; ") + off;
            }
        }
        break;
    case kCobNmt:
        item.note = tr("NMT from the VCC: the BIU is the network manager (Annexure-1 4)");
        break;
    default:
        if (f.cob >= 0x180 && f.cob < 0x200)
            item.note = tr("COB-ID 0x%1: the BIU takes KAVACH's command on 0x190 (0x180 + its node 0x10)")
                            .arg(f.cob, 3, 16, QLatin1Char('0'));
        else
            item.note = tr("not a frame the BIU takes");
        break;
    }
    emit logged(item);
    emit changed();
}

void BiuSimulator::onCommand(const Command &c, int bytes, qint64 nowMs, QString *note)
{
    QStringList notes;
    if (bytes != kDataLen) notes << tr("%1 bytes; a command is 8").arg(bytes);
    if (m_kavachState == PreOperational || m_kavachState == Stopped)
        notes << tr("sent while KAVACH's heartbeat says %1: only the heartbeat should go (Annexure-1 4)")
                     .arg(nmtStateName(m_kavachState));
    if (c.spare6 || c.spare7) notes << tr("spare bytes not 0");
    if (c.a9Valid() && c.a9() > kA9ValidMax + 1e-9) notes << tr("A9 %1 marked valid, above 6.0").arg(c.a9(), 0, 'f', 2);
    if (c.sa9Valid() && c.sa9() > kSa9ValidMax + 1e-9) notes << tr("SA9 %1 marked valid, above 4.0").arg(c.sa9(), 0, 'f', 2);

    // Acknowledges (Annexure-1 3): the code in flight is done with.
    if (c.bit2(CmdFaultAck)) {
        if (m_waitForAck && !m_faults.isEmpty() && m_faultSentMs) {
            m_faultAckLatency = nowMs - m_faultSentMs;
            notes << tr("fault 0x%1 acknowledged after %2 ms").arg(m_faults.first(), 4, 16, QLatin1Char('0')).arg(m_faultAckLatency);
            m_faults.removeFirst();
            m_faultSentMs = 0;
        } else if (m_faults.isEmpty()) {
            notes << tr("fault ack with no fault code sent");
        }
    }
    if (c.bit2(CmdDisplayAck)) {
        if (m_waitForAck && !m_displays.isEmpty() && m_displaySentMs) {
            m_displayAckLatency = nowMs - m_displaySentMs;
            notes << tr("display 0x%1 acknowledged after %2 ms").arg(m_displays.first(), 4, 16, QLatin1Char('0')).arg(m_displayAckLatency);
            m_displays.removeFirst();
            m_displaySentMs = 0;
        } else if (m_displays.isEmpty()) {
            notes << tr("display ack with no display code sent");
        }
    }

    // The panel test runs from the request's rising edge; it ends when the
    // request drops.
    const bool asked = c.bit1(CmdBrakeTestRequest);
    if (asked && !(m_commands > 0 && m_cmd.bit1(CmdBrakeTestRequest))) m_testStartMs = nowMs;
    if (!asked) m_testStartMs = 0;

    if (m_cmdMs > 0) {
        m_gaps << (nowMs - m_cmdMs);
        if (m_gaps.size() > 8) m_gaps.removeFirst();
    }
    m_cmd = c;
    m_cmdMs = nowMs;
    ++m_commands;
    noteIsolation(nowMs);
    *note = notes.join(QStringLiteral("; "));
}

double BiuSimulator::commandPeriodMs() const
{
    if (m_gaps.isEmpty()) return 0;
    double sum = 0;
    for (qint64 g : m_gaps) sum += g;
    return sum / m_gaps.size();
}

// ---- status fields ------------------------------------------------------------------

void BiuSimulator::setLivePressure(int id, double kgcm2) { if (id >= 0 && id < kPressures) m_live[id] = kgcm2; }
double BiuSimulator::livePressure(int id) const { return id >= 0 && id < kPressures ? m_live[id] : 0; }

void BiuSimulator::setPressureManual(int id, bool manual, double kgcm2)
{
    if (id < 0 || id >= kPressures) return;
    m_manual[id] = manual;
    m_manualKg[id] = kgcm2;
}

bool BiuSimulator::pressureManual(int id) const { return id >= 0 && id < kPressures && m_manual[id]; }
double BiuSimulator::pressureManualValue(int id) const { return id >= 0 && id < kPressures ? m_manualKg[id] : 0; }
void BiuSimulator::setValidMode(int id, ValidMode m) { if (id >= 0 && id < kPressures) m_valid[id] = m; }
BiuSimulator::ValidMode BiuSimulator::validMode(int id) const { return id >= 0 && id < kPressures ? m_valid[id] : ValidMode::Auto; }

void BiuSimulator::raiseFault(quint16 code)
{
    if (!code) return;
    if (m_faults.isEmpty()) m_faultSentMs = 0;
    m_faults << code;
    emit changed();
}

void BiuSimulator::raiseDisplay(quint16 code)
{
    if (!code) return;
    if (m_displays.isEmpty()) m_displaySentMs = 0;
    m_displays << code;
    emit changed();
}

void BiuSimulator::clearFaults() { m_faults.clear(); m_faultSentMs = 0; emit changed(); }
void BiuSimulator::clearDisplays() { m_displays.clear(); m_displaySentMs = 0; emit changed(); }

void BiuSimulator::setBitMode(int byte, int bit, BitMode m)
{
    if (byte < 1 || byte > 3 || bit < 0 || bit > 7) return;
    m_bits[byte - 1][bit] = m;
    emit changed();
}

BiuSimulator::BitMode BiuSimulator::bitMode(int byte, int bit) const
{
    return byte >= 1 && byte <= 3 && bit >= 0 && bit <= 7 ? m_bits[byte - 1][bit] : BitMode::Auto;
}

BiuSimulator::PanelTest BiuSimulator::panelTest(qint64 nowMs) const
{
    if (!m_testStartMs) return PanelTest::Idle;
    if (nowMs - m_testStartMs < qint64(m_testSeconds * 1000)) return PanelTest::InProgress;
    return m_testPasses ? PanelTest::Passed : PanelTest::Failed;
}

bool BiuSimulator::isolated() const
{
    if (m_bits[0][StManualIsolate] == BitMode::On) return true;
    return m_commands > 0 && m_cmd.bit1(CmdBiuIsolate);
}

void BiuSimulator::noteIsolation(qint64 nowMs)
{
    Q_UNUSED(nowMs);
    const bool iso = isolated();
    if (iso && !m_wasIsolated) {
        if (m_countIsolations) ++m_isolations;
        if (m_raiseIsolated) raiseFault(0x1040);
    }
    m_wasIsolated = iso;
}

BiuSimulator::Targets BiuSimulator::targets(qint64 nowMs) const
{
    Targets t;
    const bool lost = kavachLost(nowMs);
    if (lost && m_onLoss == OnLoss::ApplyEb) {
        t.controlling = t.bpSet = true;
        t.bp = 0;
        if (m_commands > 0 && m_cmd.sa9Valid()) { t.bcSet = true; t.bc = m_cmd.sa9(); }
        t.why = tr("KAVACH lost: emergency (Annexure-1 4 c)");
        return t;
    }
    if (lost && m_onLoss == OnLoss::Release) { t.why = tr("KAVACH lost: released"); return t; }
    if (isolated()) { t.why = tr("Isolated: the loco pilot's brakes"); return t; }
    if (m_commands == 0) { t.why = tr("No command from KAVACH yet"); return t; }
    t.bpSet = m_cmd.a9Valid();
    t.bp = m_cmd.a9();
    t.bcSet = m_cmd.sa9Valid();
    t.bc = m_cmd.sa9();
    t.controlling = t.bpSet || t.bcSet;
    t.why = lost ? tr("KAVACH lost: holding its last command")
                 : t.controlling ? tr("Following KAVACH's command") : tr("KAVACH's A9 and SA9 are not valid");
    return t;
}

bool BiuSimulator::autoBit(int byte, int bit, qint64 nowMs) const
{
    const Targets t = targets(nowMs);
    const bool bpBraking = t.controlling && t.bpSet && t.bp < kBpReleased;
    const bool bcBraking = t.controlling && t.bcSet && t.bc > kBcReleased;
    const bool braking = bpBraking || bcBraking;
    const bool cutout = m_commands > 0 && m_cmd.bit1(CmdBpChargingCutout);
    if (byte == 1) {
        switch (bit) {
        case StHealthy:            return true;
        case StPilotOverrideByBiu: return braking;
        case StBpChargingCutout:   return cutout;
        case StEmergencyValve:     return true;          // cut in
        case StPowerCutoffRelay:   return braking;       // traction cut while braking
        default:                   return false;
        }
    }
    if (byte == 2) {
        const PanelTest p = panelTest(nowMs);
        switch (bit) {
        case StPanelTestSuccess:  return p == PanelTest::Passed;
        case StPanelTestFail:     return p == PanelTest::Failed;
        case StPanelTestProgress: return p == PanelTest::InProgress;
        default:                  return false;
        }
    }
    switch (bit) {
    case StBpCutoutValveOn:  return cutout;
    case StBpSelectValveOn:  return bpBraking;
    case StBcSelectValveOn:  return bcBraking;
    case StEmergencyValveOn: return t.controlling && t.bpSet && t.bp <= kBpEmergency;
    default:                 return bit >= StBpCutoutValveOk;   // the health bits
    }
}

Status BiuSimulator::buildStatus(qint64 nowMs) const
{
    Status s;
    const Targets t = targets(nowMs);
    for (int i = 0; i < kPressures; ++i) {
        double kg = m_live[i];
        // The references are what the BIU is working to.
        if (i == A9Ref)  kg = t.bpSet ? t.bp : 0;
        if (i == Sa9Ref) kg = t.bcSet ? t.bc : 0;
        if (m_manual[i]) kg = m_manualKg[i];
        s.raw[i] = kgToByte(kg);
        switch (m_valid[i]) {
        case ValidMode::Valid:   s.valid[i] = true;  break;
        case ValidMode::Invalid: s.valid[i] = false; break;
        case ValidMode::Auto:    s.valid[i] = byteToKg(s.raw[i]) <= validLimit(i) + 1e-9; break;
        }
    }
    s.isolationCounter = m_isolations;
    s.faultCode = m_faults.isEmpty() ? 0 : m_faults.first();
    s.displayCode = m_displays.isEmpty() ? 0 : m_displays.first();
    quint8 d[3] = { 0, 0, 0 };
    for (int byte = 1; byte <= 3; ++byte)
        for (int bit = 0; bit < 8; ++bit) {
            const BitMode m = m_bits[byte - 1][bit];
            const bool on = m == BitMode::On || (m == BitMode::Auto && autoBit(byte, bit, nowMs));
            if (on) d[byte - 1] |= quint8(1u << bit);
        }
    // Spare bits stay 0 (D1 bit 7, D2 bits 3-7), forced or not.
    s.d1 = d[0] & 0x7F;
    s.d2 = d[1] & 0x07;
    s.d3 = d[2];
    return s;
}
