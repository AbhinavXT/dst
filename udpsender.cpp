#include "udpsender.h"

#include <QHostInfo>

UdpSender::UdpSender(QObject *parent) : QObject(parent)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &UdpSender::onTick);
}

static QHostAddress resolve(const QString &dest)
{
    QHostAddress a(dest);
    if (!a.isNull()) { return a; }
    const QHostInfo info = QHostInfo::fromName(dest);          // may block briefly
    if (!info.addresses().isEmpty()) { return info.addresses().first(); }
    return QHostAddress();
}

int UdpSender::writeAll(const QByteArray &frame, const PrefixFn &prefix)
{
    if (m_targets.isEmpty()) {
        emit error(QStringLiteral("no destination"));
        return 0;
    }
    if (frame.isEmpty()) {
        emit error(QStringLiteral("nothing to send (empty frame)"));
        return 0;
    }

    // One prefix for the whole frame, not one per destination: the header
    // seq_num identifies the transmission, and four peers receiving the same
    // transmission should see the same number on it.
    QByteArray datagram;
    if (prefix) { datagram = prefix(m_sent); }
    datagram += frame;

    int ok = 0;
    QStringList failed;
    for (const Resolved &t : m_targets) {
        const qint64 n = m_sock.writeDatagram(datagram, t.addr, t.port);
        if (n == datagram.size()) { ++ok; }
        else { failed << t.shown; }
    }

    if (!failed.isEmpty()) {
        emit error(QStringLiteral("write failed to %1: %2")
                       .arg(failed.join(QStringLiteral(", ")), m_sock.errorString()));
    }
    if (ok > 0) {
        ++m_sent;
        emit sent(m_sent);
    }
    return ok;
}

QVector<UdpSender::Resolved> UdpSender::resolveAll(const QVector<Target> &in)
{
    QVector<Resolved> out;
    QStringList bad;
    for (const Target &t : in) {
        if (t.host.trimmed().isEmpty()) { continue; }
        Resolved r;
        r.addr  = resolve(t.host);
        r.port  = t.port;
        r.shown = QStringLiteral("%1:%2").arg(t.host).arg(t.port);
        if (r.addr.isNull()) { bad << t.host; continue; }
        // Two identical targets would double every datagram without saying
        // so; the operator typing the same address twice means one peer.
        bool dup = false;
        for (const Resolved &have : out) {
            if (have.addr == r.addr && have.port == r.port) { dup = true; break; }
        }
        if (!dup) { out.push_back(r); }
    }
    if (!bad.isEmpty()) {
        emit error(QStringLiteral("unresolved destination(s): %1")
                       .arg(bad.join(QStringLiteral(", "))));
    }
    return out;
}

bool UdpSender::sendOnce(const QByteArray &frame, const QVector<Target> &targets,
                         const PrefixFn &prefix)
{
    const QVector<Resolved> saved = m_targets;
    m_targets = resolveAll(targets);
    const int ok = writeAll(frame, prefix);
    if (!isRunning()) { m_targets = saved; }   // a one-shot does not adopt them
    return ok > 0;
}

bool UdpSender::sendOnce(const QByteArray &frame, const QString &dest, quint16 port,
                         const PrefixFn &prefix)
{
    return sendOnce(frame, QVector<Target>{ Target{ dest, port } }, prefix);
}

void UdpSender::startInterval(const FrameFn &frameFn, const QVector<Target> &targets,
                              int intervalMs, const PrefixFn &prefix)
{
    stop();
    m_frameFn = frameFn;
    m_prefix  = prefix;
    m_targets = resolveAll(targets);

    if (!m_frameFn) { emit error(QStringLiteral("no frame source")); return; }
    if (m_targets.isEmpty()) {
        // Every destination failed to resolve. Starting a timer that can only
        // report errors would look like it was running.
        emit error(QStringLiteral("no destination could be resolved"));
        return;
    }
    if (intervalMs < 1) { intervalMs = 1; }

    m_timer.start(intervalMs);
    emit runningChanged(true);
    onTick();                       // fire immediately, then tick
}

void UdpSender::startInterval(const FrameFn &frameFn, const QString &dest,
                              quint16 port, int intervalMs, const PrefixFn &prefix)
{
    startInterval(frameFn, QVector<Target>{ Target{ dest, port } }, intervalMs, prefix);
}

void UdpSender::startIntervalFixed(const QByteArray &frame, const QString &dest,
                                   quint16 port, int intervalMs, const PrefixFn &prefix)
{
    startInterval([frame](int) { return frame; }, dest, port, intervalMs, prefix);
}

void UdpSender::stop()
{
    if (m_timer.isActive()) {
        m_timer.stop();
        m_prefix  = {};
        m_frameFn = {};
        emit runningChanged(false);
    }
}

void UdpSender::onTick()
{
    if (!m_frameFn) { stop(); return; }

    const QByteArray frame = m_frameFn(m_sent);
    if (frame.isEmpty()) {
        // The builder refused this send. Stopping is the only honest
        // response: continuing with the previous bytes would resurrect
        // exactly the frozen-body behaviour this replaced.
        emit error(QStringLiteral("frame build failed at send %1 — stopped").arg(m_sent));
        stop();
        return;
    }
    // Stops only when nothing got through. One unreachable peer out of four
    // is reported by writeAll() and the run carries on to the other three.
    if (writeAll(frame, m_prefix) == 0) { stop(); }
}
