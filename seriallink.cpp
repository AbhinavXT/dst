#include "seriallink.h"

#include <QDateTime>
#include <QSettings>
#include <QTimer>

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
//  Line splitter
// =============================================================================

QVector<SerialLineSplitter::Line> SerialLineSplitter::feed(const QByteArray &bytes, qint64 nowMs)
{
    QVector<Line> out;
    for (const char c : bytes) {
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
    if (!m_buf.isEmpty() && nowMs - m_lastMs >= idleMs) {
        out.append({ m_buf, m_firstMs });
        m_buf.clear(); m_firstMs = 0;
    }
    return out;
}

// =============================================================================
//  SerialLink
// =============================================================================

SerialLink::SerialLink(QObject *parent) : QObject(parent)
{
    m_port = new QSerialPort(this);
    connect(m_port, &QSerialPort::readyRead, this, &SerialLink::onReadyRead);
    connect(m_port, &QSerialPort::errorOccurred, this, &SerialLink::onError);
    m_idle = new QTimer(this);
    m_idle->setInterval(100);
    connect(m_idle, &QTimer::timeout, this, &SerialLink::onIdleTick);
}

SerialLink::~SerialLink() { close(); }

bool SerialLink::open(const SerialConfig &config)
{
    close();
    m_config = config;
    m_error.clear();
    m_split.clear();
    m_port->setPortName(config.portName);
    if (!m_port->open(QIODevice::ReadWrite)) {
        m_error = m_port->errorString();
        emit errorOccurred(m_error);
        return false;
    }
    // Set after open: some drivers reset the line settings on open.
    const bool set = m_port->setBaudRate(config.baud) && m_port->setDataBits(config.dataBits)
                  && m_port->setParity(config.parity) && m_port->setStopBits(config.stopBits)
                  && m_port->setFlowControl(config.flow);
    if (!set) {
        m_error = tr("%1 opened, but refused %2: %3").arg(config.portName, config.summary(), m_port->errorString());
        m_port->close();
        emit errorOccurred(m_error);
        return false;
    }
    m_idle->start();
    emit opened();
    return true;
}

void SerialLink::close()
{
    if (!m_port || !m_port->isOpen()) return;
    // Deliver what is held: the last line of a log that stopped mid-line.
    for (const SerialLineSplitter::Line &l : m_split.flushIdle(QDateTime::currentMSecsSinceEpoch(), 0))
        emit lineReceived(l.text, l.firstByteMs);
    m_idle->stop();
    m_port->close();
    emit closed();
}

bool SerialLink::isOpen() const { return m_port && m_port->isOpen(); }

qint64 SerialLink::write(const QByteArray &bytes)
{
    if (!isOpen() || bytes.isEmpty()) return 0;
    const qint64 n = m_port->write(bytes);
    if (n > 0) {
        m_tx += quint64(n);
        emit bytesWritten(bytes.left(int(n)), QDateTime::currentMSecsSinceEpoch());
    }
    return n;
}

bool SerialLink::setDtr(bool on) { return isOpen() && m_port->setDataTerminalReady(on); }
bool SerialLink::setRts(bool on)
{
    // RTS belongs to the driver under hardware flow control.
    return isOpen() && m_config.flow != QSerialPort::HardwareControl && m_port->setRequestToSend(on);
}

void SerialLink::onReadyRead()
{
    const QByteArray bytes = m_port->readAll();
    if (bytes.isEmpty()) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_rx += quint64(bytes.size());
    emit bytesReceived(bytes, now);
    for (const SerialLineSplitter::Line &l : m_split.feed(bytes, now)) emit lineReceived(l.text, l.firstByteMs);
}

void SerialLink::onIdleTick()
{
    for (const SerialLineSplitter::Line &l : m_split.flushIdle(QDateTime::currentMSecsSinceEpoch(), m_idleMs))
        emit lineReceived(l.text, l.firstByteMs);
}

void SerialLink::onError(QSerialPort::SerialPortError e)
{
    if (e == QSerialPort::NoError) return;
    m_error = m_port->errorString();
    emit errorOccurred(m_error);
    // The cable was pulled or the adapter vanished: the port is gone.
    if (e == QSerialPort::ResourceError && m_port->isOpen()) {
        m_idle->stop();
        m_port->close();
        emit closed();
    }
}
