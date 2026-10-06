#include "seriallink.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QSettings>
#include <QThread>
#include <QTimer>

#ifdef Q_OS_LINUX
#  include <linux/serial.h>
#  include <sys/ioctl.h>
#endif

// =============================================================================
//  Config
// =============================================================================

QChar serialParityLetter(QSerialPort::Parity p)
{
    switch (p) {
    case QSerialPort::EvenParity:  return QLatin1Char('E');
    case QSerialPort::OddParity:   return QLatin1Char('O');
    case QSerialPort::MarkParity:  return QLatin1Char('M');
    case QSerialPort::SpaceParity: return QLatin1Char('S');
    default:                       return QLatin1Char('N');
    }
}

QString serialStopBitsText(QSerialPort::StopBits s)
{
    switch (s) {
    case QSerialPort::OneAndHalfStop: return QStringLiteral("1.5");
    case QSerialPort::TwoStop:        return QStringLiteral("2");
    default:                          return QStringLiteral("1");
    }
}

QVector<qint32> serialStandardBauds()
{
    return { 1200, 2400, 4800, 9600, 14400, 19200, 38400, 57600, 115200,
             230400, 460800, 921600 };
}

QString SerialConfig::summary() const
{
    QString s = QStringLiteral("%1 %2%3%4").arg(baud).arg(int(dataBits))
                    .arg(serialParityLetter(parity)).arg(serialStopBitsText(stopBits));
    if (flow == QSerialPort::HardwareControl) s += QStringLiteral(" RTS/CTS");
    else if (flow == QSerialPort::SoftwareControl) s += QStringLiteral(" XON/XOFF");
    return s;
}

void SerialConfig::save(QSettings &s, const QString &group) const
{
    s.beginGroup(group);
    s.setValue(QStringLiteral("port"), portName);
    s.setValue(QStringLiteral("baud"), baud);
    s.setValue(QStringLiteral("dataBits"), int(dataBits));
    s.setValue(QStringLiteral("parity"), int(parity));
    s.setValue(QStringLiteral("stopBits"), int(stopBits));
    s.setValue(QStringLiteral("flow"), int(flow));
    s.setValue(QStringLiteral("lowLatency"), lowLatency);
    s.endGroup();
}

SerialConfig SerialConfig::load(QSettings &s, const QString &group)
{
    SerialConfig c;
    s.beginGroup(group);
    c.portName = s.value(QStringLiteral("port")).toString();
    c.baud = s.value(QStringLiteral("baud"), 115200).toInt();
    if (c.baud <= 0) c.baud = 115200;
    const int db = s.value(QStringLiteral("dataBits"), 8).toInt();
    c.dataBits = (db >= 5 && db <= 8) ? QSerialPort::DataBits(db) : QSerialPort::Data8;
    const int pa = s.value(QStringLiteral("parity"), 0).toInt();
    c.parity = (pa == 0 || (pa >= 2 && pa <= 5)) ? QSerialPort::Parity(pa) : QSerialPort::NoParity;
    const int sb = s.value(QStringLiteral("stopBits"), 1).toInt();
    c.stopBits = (sb >= 1 && sb <= 3) ? QSerialPort::StopBits(sb) : QSerialPort::OneStop;
    const int fl = s.value(QStringLiteral("flow"), 0).toInt();
    c.flow = (fl >= 0 && fl <= 2) ? QSerialPort::FlowControl(fl) : QSerialPort::NoFlowControl;
    c.lowLatency = s.value(QStringLiteral("lowLatency"), true).toBool();
    s.endGroup();
    return c;
}

// =============================================================================
//  Hex and escapes
// =============================================================================

QByteArray serialParseHex(const QString &text, bool *ok)
{
    QString t = text;
    t.replace(QLatin1String("0x"), QLatin1String(" "), Qt::CaseInsensitive);
    QByteArray out;
    QString digits;
    bool good = true;
    for (const QChar c : t) {
        if (c.isSpace() || c == QLatin1Char(',') || c == QLatin1Char(':') || c == QLatin1Char('-')) continue;
        if (!isxdigit(c.toLatin1()) || c.unicode() > 127) { good = false; break; }
        digits += c;
    }
    if (good && digits.size() % 2 != 0) good = false;
    if (good) {
        for (int i = 0; i < digits.size(); i += 2) out.append(char(digits.mid(i, 2).toUInt(nullptr, 16)));
    }
    if (ok) *ok = good;
    return good ? out : QByteArray();
}

QString serialToHex(const QByteArray &bytes)
{
    return QString::fromLatin1(bytes.toHex(' ').toUpper());
}

QByteArray serialUnescape(const QString &text)
{
    const QByteArray in = text.toUtf8();
    QByteArray out;
    for (int i = 0; i < in.size(); ++i) {
        const char c = in.at(i);
        if (c != '\\' || i + 1 >= in.size()) { out.append(c); continue; }
        const char n = in.at(i + 1);
        switch (n) {
        case 'r':  out.append('\r'); ++i; break;
        case 'n':  out.append('\n'); ++i; break;
        case 't':  out.append('\t'); ++i; break;
        case '0':  out.append('\0'); ++i; break;
        case '\\': out.append('\\'); ++i; break;
        case 'x': {
            bool okHex = false;
            const int v = (i + 3 < in.size()) ? in.mid(i + 2, 2).toInt(&okHex, 16) : 0;
            if (okHex) { out.append(char(v)); i += 3; }
            else out.append(c);
            break;
        }
        default: out.append(c); break;
        }
    }
    return out;
}

// =============================================================================
//  Terminal display (session 107)
// =============================================================================

QByteArray serialStripAnsi(const QByteArray &in)
{
    QByteArray out;
    out.reserve(in.size());
    const int n = in.size();
    for (int i = 0; i < n; ++i) {
        const char c = in.at(i);
        if (c != '\x1b') { out.append(c); continue; }
        if (i + 1 >= n) break;                        // a lone ESC at the end
        const char k = in.at(i + 1);
        if (k == '[') {
            // CSI: parameters and intermediates, then a final byte @..~.
            int j = i + 2;
            while (j < n && !(in.at(j) >= '@' && in.at(j) <= '~')) ++j;
            i = j;                                    // the final byte is skipped too
        } else if (k == ']') {
            // OSC: up to BEL or ST (ESC \).
            int j = i + 2;
            while (j < n && in.at(j) != '\x07' && !(in.at(j) == '\x1b' && j + 1 < n && in.at(j + 1) == '\\')) ++j;
            i = (j < n && in.at(j) == '\x1b') ? j + 1 : j;
        } else {
            i += 1;                                   // two-byte ESC x
        }
    }
    return out;
}

QString serialDisplayText(const QByteArray &bytes)
{
    const QByteArray clean = serialStripAnsi(bytes);
    // With the length: Qt 5's fromUtf8(QByteArray) stops at the first NUL,
    // which cut a line short at exactly the byte this is meant to show.
    QString out = QString::fromUtf8(clean.constData(), clean.size());
    for (int i = 0; i < out.size(); ++i) {
        const ushort u = out.at(i).unicode();
        if (u == '\t') continue;
        if (u < 0x20) out[i] = QChar(0x2400 + u);     // control pictures
        else if (u == 0x7F) out[i] = QChar(0x2421);
    }
    return out;
}

QString SerialHexDumper::formatRow(quint64 offset, const QByteArray &bytes)
{
    QString hex, ascii;
    for (int i = 0; i < kRowBytes; ++i) {
        if (i == 8) hex += QLatin1Char(' ');          // the half-row gap
        if (i < bytes.size()) {
            const uchar b = uchar(bytes.at(i));
            hex += QStringLiteral("%1 ").arg(b, 2, 16, QLatin1Char('0')).toUpper();
            ascii += (b >= 0x20 && b < 0x7F) ? QChar(b) : QLatin1Char('.');
        } else {
            hex += QStringLiteral("   ");              // keeps the ASCII column aligned
        }
    }
    return QStringLiteral("%1  %2 |%3|").arg(offset, 8, 16, QLatin1Char('0')).arg(hex, ascii);
}

QVector<SerialHexDumper::Row> SerialHexDumper::feed(const QByteArray &bytes, qint64 nowMs)
{
    QVector<Row> out;
    for (const char c : bytes) {
        if (m_row.isEmpty()) m_firstMs = nowMs;
        m_row.append(c);
        if (m_row.size() == kRowBytes) {
            out.append({ formatRow(m_offset, m_row), m_firstMs });
            m_offset += kRowBytes;
            m_row.clear();
        }
    }
    if (!bytes.isEmpty()) m_lastMs = nowMs;
    return out;
}

QVector<SerialHexDumper::Row> SerialHexDumper::flushPartial()
{
    if (m_row.isEmpty()) return {};
    const Row r{ formatRow(m_offset, m_row), m_firstMs };
    // The next row starts where this one stopped, so offsets stay true.
    m_offset += quint64(m_row.size());
    m_row.clear();
    return { r };
}

// =============================================================================
//  Line splitter
// =============================================================================

QVector<SerialLineSplitter::Line> SerialLineSplitter::feed(const QByteArray &bytes, qint64 nowMs)
{
    QVector<Line> out;
    for (const char c : bytes) {
        if ((c == '\n' || c == '\r') && m_buf.isEmpty() && m_afterIdleFlush) {
            // "login:" was flushed on idle; this is its line end arriving.
            // After a CR, the usual CR-LF rule absorbs the LF that follows.
            m_lastWasCr = (c == '\r');
            m_afterIdleFlush = false;
            continue;
        }
        m_afterIdleFlush = false;
        if (c == '\n') {
            if (!m_lastWasCr || !m_buf.isEmpty()) out.append({ m_buf, m_firstMs ? m_firstMs : nowMs });
            m_buf.clear(); m_firstMs = 0; m_lastWasCr = false;
            continue;
        }
        if (c == '\r') {
            // CR ends a line; an LF straight after it is the same line end.
            out.append({ m_buf, m_firstMs ? m_firstMs : nowMs });
            m_buf.clear(); m_firstMs = 0; m_lastWasCr = true;
            continue;
        }
        if (m_lastWasCr) m_lastWasCr = false;
        if (m_buf.isEmpty()) m_firstMs = nowMs;
        m_lastMs = nowMs;
        m_buf.append(c);
        if (m_buf.size() >= kMaxLine) {
            out.append({ m_buf, m_firstMs });
            m_buf.clear(); m_firstMs = 0;
        }
    }
    return out;
}

QVector<SerialLineSplitter::Line> SerialLineSplitter::flushIdle(qint64 nowMs, qint64 idleMs)
{
    QVector<Line> out;
    if (m_buf.isEmpty()) return out;
    const qint64 wait = (idleMs > 0 && m_buf.startsWith('@')) ? qMax(idleMs, kLogLineIdleMs)
                                                               : idleMs;
    if (nowMs - m_lastMs >= wait) {
        out.append({ m_buf, m_firstMs });
        m_buf.clear(); m_firstMs = 0;
        m_afterIdleFlush = true;
    }
    return out;
}

// =============================================================================
//  Low latency
// =============================================================================

QString serialLatencyTimerPath(const QString &portName)
{
    const QString dev = QFileInfo(portName).fileName();
    if (!dev.startsWith(QLatin1String("ttyUSB"))) return QString();
    return QStringLiteral("/sys/bus/usb-serial/devices/%1/latency_timer").arg(dev);
}

namespace {

// Ask the driver for low latency. Never fails the open: a port that cannot
// do it is still a working port, and the note says what happened.
QString requestLowLatency(QSerialPort *port, const SerialConfig &config)
{
    if (!config.lowLatency) return QString();
#ifdef Q_OS_LINUX
    const int fd = int(port->handle());
    serial_struct ss{};
    if (fd < 0 || ::ioctl(fd, TIOCGSERIAL, &ss) != 0) {
        return QObject::tr("low latency: not a serial driver that offers it");
    }
    ss.flags |= ASYNC_LOW_LATENCY;
    if (::ioctl(fd, TIOCSSERIAL, &ss) != 0) {
        return QObject::tr("low latency: the driver refused it");
    }
    return QObject::tr("low latency: on (an FTDI timer goes to 1 ms)");
#elif defined(Q_OS_WIN)
    Q_UNUSED(port);
    return QObject::tr("low latency: set it in Device Manager ▸ the port ▸ Port Settings "
                       "▸ Advanced ▸ Latency Timer = 1 ms (a program cannot)");
#else
    Q_UNUSED(port);
    return QObject::tr("low latency: not settable on this platform");
#endif
}

}  // namespace

// =============================================================================
//  SerialPortWorker — the reader thread's half
// =============================================================================

SerialPortWorker::SerialPortWorker()
{
    // Children, so they move to the reader thread with this object.
    m_port = new QSerialPort(this);
    connect(m_port, &QSerialPort::readyRead, this, &SerialPortWorker::onReadyRead);
    connect(m_port, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError e) {
        const SerialErrorOutcome o = judgeError(e);
        if (o.report) emit errorJudged(o);
    });
    m_idle = new QTimer(this);
    m_idle->setInterval(100);
    connect(m_idle, &QTimer::timeout, this, &SerialPortWorker::onIdleTick);
}

bool SerialPortWorker::openPort(const SerialConfig &config, QString *error, QString *latencyNote)
{
    m_split.clear();
    errors = 0;
    m_errorBurstStartMs = 0;
    m_errorBurstCount = 0;
    m_port->setPortName(config.portName);
    if (!m_port->open(QIODevice::ReadWrite)) {
        *error = m_port->errorString();
        return false;
    }
    // Set after open: some drivers reset the line settings on open.
    const bool set = m_port->setBaudRate(config.baud) && m_port->setDataBits(config.dataBits)
                  && m_port->setParity(config.parity) && m_port->setStopBits(config.stopBits)
                  && m_port->setFlowControl(config.flow);
    if (!set) {
        *error = QObject::tr("%1 opened, but refused %2: %3")
                     .arg(config.portName, config.summary(), m_port->errorString());
        m_port->close();
        return false;
    }
    *latencyNote = requestLowLatency(m_port, config);
    m_idle->start();
    return true;
}

QVector<SerialLineSplitter::Line> SerialPortWorker::closePort()
{
    if (!m_port->isOpen()) return {};
    // Read what the driver still holds, then deliver the partial line: the
    // last line of a log that stopped mid-line.
    onReadyRead();
    const QVector<SerialLineSplitter::Line> held =
        m_split.flushIdle(QDateTime::currentMSecsSinceEpoch(), 0);
    m_idle->stop();
    m_port->clearError();
    m_port->close();
    return held;
}

qint64 SerialPortWorker::writeBytes(const QByteArray &bytes)
{
    if (!m_port->isOpen()) return 0;
    const qint64 n = m_port->write(bytes);
    if (n > 0) tx += quint64(n);
    return n;
}

bool SerialPortWorker::setDtr(bool on)
{
    return m_port->isOpen() && m_port->setDataTerminalReady(on);
}

bool SerialPortWorker::setRts(bool on, bool hardwareFlow)
{
    // RTS belongs to the driver under hardware flow control.
    return m_port->isOpen() && !hardwareFlow && m_port->setRequestToSend(on);
}

void SerialPortWorker::onReadyRead()
{
    if (!m_port->isOpen()) return;
    const QByteArray bytes = m_port->readAll();
    if (bytes.isEmpty()) return;
    // Stamped HERE, on the reader thread, when the bytes are read.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    rx += quint64(bytes.size());
    emit bytesRead(bytes, now);
    for (const SerialLineSplitter::Line &l : m_split.feed(bytes, now)) emit lineRead(l.text, l.firstByteMs);
}

void SerialPortWorker::onIdleTick()
{
    for (const SerialLineSplitter::Line &l : m_split.flushIdle(QDateTime::currentMSecsSinceEpoch(), idleMs.load()))
        emit lineRead(l.text, l.firstByteMs);
}

SerialErrorOutcome SerialPortWorker::judgeError(QSerialPort::SerialPortError e)
{
    SerialErrorOutcome o;
    if (e == QSerialPort::NoError) return o;
    ++errors;

    // An error that repeats in a burst is a port that is gone but still
    // "open": a virtual port whose far end vanished reported ReadError about
    // 114,000 times a second, each one restyling the terminal's status label.
    // Counted per burst so a stray framing error now and then never trips it.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_errorBurstStartMs > kErrorBurstWindowMs) {
        m_errorBurstStartMs = now;
        m_errorBurstCount = 0;
    }
    ++m_errorBurstCount;
    // Report the first of a burst, and the one that trips the guard; the
    // rest of the burst is the same news.
    if (m_errorBurstCount > 1 && m_errorBurstCount != kErrorBurstLimit) return o;

    // Gone for good: the adapter vanished (ResourceError, what a real USB
    // adapter reports), or reading/writing it fails (ReadError/WriteError,
    // what a virtual port reports when its far end goes).
    const bool fatal = e == QSerialPort::ResourceError || e == QSerialPort::ReadError
                    || e == QSerialPort::WriteError;
    const bool storm = m_errorBurstCount >= kErrorBurstLimit;

    o.report = true;
    o.text = m_port->errorString();
    if (storm) {
        o.text = QObject::tr("%1 (repeated %2 times in %3 ms; port closed)")
                     .arg(o.text).arg(m_errorBurstCount).arg(kErrorBurstWindowMs);
    }
    if ((fatal || storm) && m_port->isOpen()) {
        o.flushed = closePort();
        o.closed = true;
    }
    return o;
}

// =============================================================================
//  SerialLink — the GUI thread's half
// =============================================================================

SerialLink::SerialLink(QObject *parent) : QObject(parent)
{
    static const int registered = [] {
        qRegisterMetaType<SerialErrorOutcome>("SerialErrorOutcome");
        return 0;
    }();
    Q_UNUSED(registered);

    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("serial reader"));
    m_worker = new SerialPortWorker;
    m_worker->moveToThread(m_thread);
    connect(m_worker, &SerialPortWorker::bytesRead, this, &SerialLink::bytesReceived);
    connect(m_worker, &SerialPortWorker::lineRead, this, &SerialLink::lineReceived);
    connect(m_worker, &SerialPortWorker::errorJudged, this, &SerialLink::applyError);
    m_thread->start();
}

SerialLink::~SerialLink()
{
    close();
    m_thread->quit();
    m_thread->wait();
    delete m_worker;            // its thread has stopped; safe from here
}

bool SerialLink::open(const SerialConfig &config)
{
    close();
    m_config = config;
    m_error.clear();
    m_latencyNote.clear();
    bool ok = false;
    QString err, note;
    QMetaObject::invokeMethod(m_worker, [&]() { ok = m_worker->openPort(config, &err, &note); },
                              Qt::BlockingQueuedConnection);
    if (!ok) {
        m_error = err;
        emit errorOccurred(m_error);
        return false;
    }
    m_latencyNote = note;
    m_open = true;
    emit opened();
    return true;
}

void SerialLink::openAsync(const SerialConfig &config)
{
    close();
    m_config = config;
    m_error.clear();
    m_latencyNote.clear();
    const quint64 generation = ++m_generation;
    m_opening = true;
    SerialPortWorker *worker = m_worker;
    QMetaObject::invokeMethod(m_worker, [this, worker, config, generation]() {
        QString err, note;
        const bool ok = worker->openPort(config, &err, &note);
        // Posted before any readyRead of the new port is handled (that waits
        // for this call to return), so opened() comes before the first line.
        // Dropped with the link if it is destroyed first: its destructor
        // waits for this thread, and Qt discards events for a dead object.
        QMetaObject::invokeMethod(this, [this, generation, ok, err, note]() {
            finishOpen(generation, ok, err, note);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void SerialLink::finishOpen(quint64 generation, bool ok, const QString &error, const QString &note)
{
    // Superseded by a close() or a newer open: that call already closed
    // whatever this one opened (its close ran on the worker after it).
    if (generation != m_generation || !m_opening) return;
    m_opening = false;
    if (!ok) {
        m_error = error;
        emit errorOccurred(m_error);
        emit openFailed(m_error);
        return;
    }
    m_latencyNote = note;
    m_open = true;
    emit opened();
}

void SerialLink::close()
{
    if (m_opening) {
        // An open in flight: cancel it. The close below is queued behind it
        // on the reader thread, so it closes the port that open makes.
        m_opening = false;
        ++m_generation;
        QMetaObject::invokeMethod(m_worker, [this]() { m_worker->closePort(); },
                                  Qt::BlockingQueuedConnection);
        if (!m_open) return;
    }
    if (!m_open) return;
    ++m_generation;
    QVector<SerialLineSplitter::Line> held;
    QMetaObject::invokeMethod(m_worker, [&]() { held = m_worker->closePort(); },
                              Qt::BlockingQueuedConnection);
    m_open = false;
    // Lines already read are queued ahead of this; the held partial line
    // goes last, then closed().
    QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
    for (const SerialLineSplitter::Line &l : held) emit lineReceived(l.text, l.firstByteMs);
    emit closed();
}

bool SerialLink::isOpen() const { return m_open; }

quint64 SerialLink::rxBytes() const { return m_worker->rx.load(); }
quint64 SerialLink::txBytes() const { return m_worker->tx.load(); }
quint64 SerialLink::errorCount() const { return m_worker->errors.load(); }
void SerialLink::resetCounters() { m_worker->rx = 0; m_worker->tx = 0; }
void SerialLink::setIdleFlushMs(int ms) { m_worker->idleMs = ms; }

qint64 SerialLink::write(const QByteArray &bytes)
{
    if (!m_open || bytes.isEmpty()) return 0;
    qint64 n = 0;
    QMetaObject::invokeMethod(m_worker, [&]() { n = m_worker->writeBytes(bytes); },
                              Qt::BlockingQueuedConnection);
    if (n > 0) emit bytesWritten(bytes.left(int(n)), QDateTime::currentMSecsSinceEpoch());
    return n;
}

bool SerialLink::setDtr(bool on)
{
    if (!m_open) return false;
    bool ok = false;
    QMetaObject::invokeMethod(m_worker, [&]() { ok = m_worker->setDtr(on); }, Qt::BlockingQueuedConnection);
    return ok;
}

bool SerialLink::setRts(bool on)
{
    if (!m_open) return false;
    bool ok = false;
    const bool hw = m_config.flow == QSerialPort::HardwareControl;
    QMetaObject::invokeMethod(m_worker, [&]() { ok = m_worker->setRts(on, hw); }, Qt::BlockingQueuedConnection);
    return ok;
}

void SerialLink::onError(QSerialPort::SerialPortError e)
{
    SerialErrorOutcome o;
    QMetaObject::invokeMethod(m_worker, [&]() { o = m_worker->judgeError(e); },
                              Qt::BlockingQueuedConnection);
    if (o.report) applyError(o);
}

void SerialLink::applyError(const SerialErrorOutcome &o)
{
    const bool lostNow = o.closed && m_open;
    if (lostNow) {
        m_open = false;
        for (const SerialLineSplitter::Line &l : o.flushed) emit lineReceived(l.text, l.firstByteMs);
        emit closed();
    }
    m_error = o.text;
    emit errorOccurred(m_error);
    if (lostNow) emit lost(m_error);
}
