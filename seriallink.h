#ifndef SERIALLINK_H
#define SERIALLINK_H

// =============================================================================
//  SerialLink (session 85)
//  -----------------------------------------------------------------------------
//  One serial port, opened with a full line configuration (baud, data bits,
//  parity, stop bits, flow control), with:
//    - raw bytes out as they arrive (for a terminal's hex view),
//    - complete text lines out, each stamped with the time its FIRST byte
//      arrived (a line that trickles in over 40 ms at 9600 baud belongs to
//      when it started, which is when the card printed it),
//    - writes of text or raw bytes, and the DTR / RTS lines.
//
//  WHY IT IS SEPARATE FROM THE WINDOW
//    Only the VCC logs over Ethernet. The IOA's input, output and analog logs
//    come out of a serial port, and the RFID reader data for the mission
//    driver will go INTO one. Both need the same port handling without a
//    terminal window attached, so the port is a plain QObject here and the
//    QCom-style window (serialconsolewindow.h) is one user of it.
//
//  Pure helpers (line splitting, hex parsing and printing, the "115200 8N1"
//  summary) are free functions so they are tested without a port.
// =============================================================================

#include <QByteArray>
#include <QObject>
#include <QSerialPort>
#include <QString>
#include <QStringList>
#include <QVector>

class QSettings;

struct SerialConfig {
    QString portName;                  // "COM3", "ttyUSB0", or a full path
    qint32  baud      = 115200;
    QSerialPort::DataBits    dataBits = QSerialPort::Data8;
    QSerialPort::Parity      parity   = QSerialPort::NoParity;
    QSerialPort::StopBits    stopBits = QSerialPort::OneStop;
    QSerialPort::FlowControl flow     = QSerialPort::NoFlowControl;

    QString summary() const;           // "115200 8N1", "9600 7E2 RTS/CTS"
    void save(QSettings &s, const QString &group) const;
    static SerialConfig load(QSettings &s, const QString &group);
};

// ---- pure helpers ---------------------------------------------------------------
// The standard rates offered in the baud box (anything else can be typed).
QVector<qint32> serialStandardBauds();
QChar serialParityLetter(QSerialPort::Parity p);        // N E O M S
QString serialStopBitsText(QSerialPort::StopBits s);    // "1" "1.5" "2"

// "AA 55 0d0A", "aa550d0a", "0xAA,0x55" -> bytes. Refuses (ok = false) an odd
// digit count or anything that is not a hex digit or a separator, rather
// than sending something else than was typed.
QByteArray serialParseHex(const QString &text, bool *ok = nullptr);
QString    serialToHex(const QByteArray &bytes);        // "AA 55 0D 0A"
// C-style escapes in typed text: \r \n \t \\ \xHH. Unknown escapes are kept.
QByteArray serialUnescape(const QString &text);

// Splits a byte stream into lines on LF (a CR before it is dropped; a lone
// CR also ends a line, as some cards print CR only). Each line carries the
// time its first byte arrived.
class SerialLineSplitter
{
public:
    struct Line { QByteArray text; qint64 firstByteMs = 0; };
    QVector<Line> feed(const QByteArray &bytes, qint64 nowMs);
    // A partial line whose LAST byte is `idleMs` old is flushed as a line (a
    // card that printed without a newline and went quiet; a prompt). A line
    // still trickling in is not cut. Empty when none is due.
    //
    // A partial that starts with '@' is a LOG line, not a prompt: cards
    // print one in several printf calls, and a pause between two of them
    // is not the card going quiet. Cutting it there gave two halves that
    // neither decode. Those wait kLogLineIdleMs instead (or for the link
    // to close, which flushes with idleMs 0 regardless).
    QVector<Line> flushIdle(qint64 nowMs, qint64 idleMs);
    int pendingBytes() const { return m_buf.size(); }
    void clear() { m_buf.clear(); m_firstMs = 0; m_lastWasCr = false; m_afterIdleFlush = false; }
    static constexpr int kMaxLine = 4096;          // longer is split, not held forever
    static constexpr qint64 kLogLineIdleMs = 5000;

private:
    QByteArray m_buf;
    qint64     m_firstMs = 0;
    qint64     m_lastMs = 0;
    bool       m_lastWasCr = false;
    // An idle flush delivered a line with no line end yet. The CR/LF that
    // finally arrives ends THAT line, not a new empty one.
    bool       m_afterIdleFlush = false;
};

// ---- the port -----------------------------------------------------------------
class SerialLink : public QObject
{
    Q_OBJECT
public:
    explicit SerialLink(QObject *parent = nullptr);
    ~SerialLink() override;

    bool open(const SerialConfig &config);
    void close();
    bool isOpen() const;
    const SerialConfig &config() const { return m_config; }
    QString errorText() const { return m_error; }

    qint64 write(const QByteArray &bytes);
    bool setDtr(bool on);
    bool setRts(bool on);

    quint64 rxBytes() const { return m_rx; }
    quint64 txBytes() const { return m_tx; }
    void resetCounters() { m_rx = m_tx = 0; }

    // How long a partial line may sit before it is delivered anyway.
    void setIdleFlushMs(int ms) { m_idleMs = ms; }

signals:
    void bytesReceived(const QByteArray &bytes, qint64 atMs);
    void lineReceived(const QByteArray &line, qint64 firstByteMs);
    void bytesWritten(const QByteArray &bytes, qint64 atMs);
    void opened();
    void closed();
    void errorOccurred(const QString &text);

private:
    void onReadyRead();
    // Public for tests only: a dead port cannot be produced on demand.
public:
    void onError(QSerialPort::SerialPortError e);
private:
    void onIdleTick();

    QSerialPort        *m_port = nullptr;
    SerialConfig        m_config;
    SerialLineSplitter  m_split;
    class QTimer       *m_idle = nullptr;
    int                 m_idleMs = 500;
    QString             m_error;
    quint64             m_rx = 0, m_tx = 0;

    // Error-storm guard, see onError().
    static constexpr qint64 kErrorBurstWindowMs = 100;
    static constexpr int    kErrorBurstLimit    = 20;
    qint64              m_errorBurstStartMs = 0;
    int                 m_errorBurstCount   = 0;
};

#endif // SERIALLINK_H
