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

#include <atomic>

class QSettings;
class QThread;
class QTimer;

struct SerialConfig {
    QString portName;                  // "COM3", "ttyUSB0", or a full path
    qint32  baud      = 115200;
    QSerialPort::DataBits    dataBits = QSerialPort::Data8;
    QSerialPort::Parity      parity   = QSerialPort::NoParity;
    QSerialPort::StopBits    stopBits = QSerialPort::OneStop;
    QSerialPort::FlowControl flow     = QSerialPort::NoFlowControl;
    bool lowLatency = true;            // ask for a 1 ms FTDI latency timer (see SerialLink)

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
//
// READ ON ITS OWN THREAD (session 103)
//   The port used to be read on the GUI thread, so while the GUI was busy
//   (loading a replay, rebuilding a big table) bytes sat in the driver and
//   were stamped late — the same reason UDPCommunication reads on a thread
//   of its own. Now each link runs a SerialPortWorker in its own QThread:
//   the QSerialPort, the line splitter and the idle timer live there, and
//   bytes are stamped when they are read. The GUI receives them by queued
//   signal, already stamped.
//
//   The public API is unchanged and synchronous: open(), close(), write(),
//   setDtr()/setRts() call into the worker and wait for it. The lines a
//   close flushes are handed back with it, so they still arrive before
//   closed().
//
// LOW LATENCY
//   An FTDI adapter holds received bytes up to its latency timer (16 ms by
//   default) before passing them on: up to 16 ms of timestamp jitter. With
//   SerialConfig::lowLatency the link asks for 1 ms where the platform lets
//   a program ask: on Linux, ASYNC_LOW_LATENCY (which ftdi_sio maps to a
//   1 ms timer). Windows and macOS only set it in the driver's settings;
//   latencyNote() says so rather than pretending.

class SerialPortWorker;

struct SerialErrorOutcome {
    bool    report = false;         // say something (first of a burst, or the storm)
    bool    closed = false;         // the port was closed because of it
    QString text;
    QVector<SerialLineSplitter::Line> flushed;   // partial line delivered on close
};

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
    // What happened to the low-latency request at the last open.
    QString latencyNote() const { return m_latencyNote; }

    qint64 write(const QByteArray &bytes);
    bool setDtr(bool on);
    bool setRts(bool on);

    quint64 rxBytes() const;
    quint64 txBytes() const;
    // Every error the driver reported since open, including the ones a
    // burst folds into one report.
    quint64 errorCount() const;
    void resetCounters();

    // How long a partial line may sit before it is delivered anyway.
    void setIdleFlushMs(int ms);

    // The thread the port is read on (tests check it is not the GUI's).
    QThread *readerThread() const { return m_thread; }

    // Public for tests only: a dead port cannot be produced on demand.
    void onError(QSerialPort::SerialPortError e);

signals:
    void bytesReceived(const QByteArray &bytes, qint64 atMs);
    void lineReceived(const QByteArray &line, qint64 firstByteMs);
    void bytesWritten(const QByteArray &bytes, qint64 atMs);
    void opened();
    void closed();
    void errorOccurred(const QString &text);

private:
    void applyError(const SerialErrorOutcome &o);

    QThread            *m_thread = nullptr;
    SerialPortWorker   *m_worker = nullptr;
    SerialConfig        m_config;
    QString             m_error;
    QString             m_latencyNote;
    bool                m_open = false;
};

// The half of SerialLink that lives on the reader thread. Internal: only
// SerialLink talks to it.
class SerialPortWorker : public QObject
{
    Q_OBJECT
public:
    SerialPortWorker();

    // Called on the worker's thread (SerialLink invokes them blocking).
    bool openPort(const SerialConfig &config, QString *error, QString *latencyNote);
    QVector<SerialLineSplitter::Line> closePort();
    qint64 writeBytes(const QByteArray &bytes);
    bool setDtr(bool on);
    bool setRts(bool on, bool hardwareFlow);
    SerialErrorOutcome judgeError(QSerialPort::SerialPortError e);

    std::atomic<quint64> rx{ 0 }, tx{ 0 };
    std::atomic<quint64> errors{ 0 };   // every driver error, burst or not
    std::atomic<int>     idleMs{ 500 };

signals:
    void bytesRead(const QByteArray &bytes, qint64 atMs);
    void lineRead(const QByteArray &line, qint64 firstByteMs);
    void errorJudged(const SerialErrorOutcome &outcome);

private:
    void onReadyRead();
    void onIdleTick();

    QSerialPort        *m_port = nullptr;
    QTimer             *m_idle = nullptr;
    SerialLineSplitter  m_split;

    // Error-storm guard, see judgeError().
    static constexpr qint64 kErrorBurstWindowMs = 100;
    static constexpr int    kErrorBurstLimit    = 20;
    qint64              m_errorBurstStartMs = 0;
    int                 m_errorBurstCount   = 0;
};

// Where Linux exposes an FTDI port's latency timer, for the note; empty
// for a name that is not a USB serial device.
QString serialLatencyTimerPath(const QString &portName);

Q_DECLARE_METATYPE(SerialErrorOutcome)

#endif // SERIALLINK_H
