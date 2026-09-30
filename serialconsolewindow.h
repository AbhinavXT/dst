#ifndef SERIALCONSOLEWINDOW_H
#define SERIALCONSOLEWINDOW_H

// =============================================================================
//  Serial port terminal (session 85) — Tools ▸ Serial Port Terminal… (Ctrl+Alt+S)
//  -----------------------------------------------------------------------------
//  A QCom-style terminal on one serial port:
//    port (with refresh), baud (typed rates allowed), data bits, parity,
//    stop bits, flow control, Open / Close, DTR and RTS;
//    receive view as text or hex, with timestamps, TX echo, hold, clear,
//    and log-to-file;
//    send as text (with \r \n \xHH escapes) or hex, with a line ending
//    (none / CR / LF / CR+LF), history, repeat every N ms, send a file;
//    RX / TX byte counters.
//
//  AND ONE THING QCOM CANNOT DO
//    "Feed console": every received line also enters the console's own
//    pipeline as if it had come over UDP, into a tab named "Serial COM3".
//    That is how the IOA's input / output / analog logs — which never come
//    over Ethernet — get the same decoding, colour rules, find, query,
//    plots, recording and session files as the VCC's. Each line is stamped
//    with the time its first byte arrived.
// =============================================================================

#include <QPointer>
#include <QStringList>
#include <QWidget>

#include "seriallink.h"

class MessageDispatcher;
class QCheckBox;
class QComboBox;
class QFile;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTimer;

class SerialConsoleWindow : public QWidget
{
    Q_OBJECT
public:
    explicit SerialConsoleWindow(MessageDispatcher *dispatcher, QWidget *parent = nullptr);
    ~SerialConsoleWindow() override;

    SerialLink *link() const { return m_link; }
    SerialConfig configFromUi() const;
    void setConfigToUi(const SerialConfig &c);

    // Tests drive these as the buttons do.
    bool openPort();
    void closePort();
    bool sendText(const QString &text);        // honours hex / line-ending settings
    QString receivedText() const;
    QString statusText() const;
    QString tabKey() const;                    // the console tab fed from this port
    static quint16 kvchForPort(const QString &portName);
    static constexpr quint8 kSerialSourceId = 254;

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void refreshPorts();
    void appendView(const QString &text);
    void onLine(const QByteArray &line, qint64 ms);
    void onBytes(const QByteArray &bytes, qint64 ms);
    void onWritten(const QByteArray &bytes, qint64 ms);
    void updateState();
    void setLogging(bool on);
    QString stamp(qint64 ms) const;

    MessageDispatcher *m_dispatcher = nullptr;
    SerialLink     *m_link = nullptr;

    QComboBox *m_port = nullptr, *m_baud = nullptr, *m_dataBits = nullptr,
              *m_parity = nullptr, *m_stopBits = nullptr, *m_flow = nullptr;
    QPushButton *m_open = nullptr;
    QCheckBox *m_dtr = nullptr, *m_rts = nullptr;

    QCheckBox *m_hexView = nullptr, *m_timestamps = nullptr, *m_echo = nullptr,
              *m_hold = nullptr, *m_logFile = nullptr, *m_feed = nullptr;
    QPlainTextEdit *m_view = nullptr;

    QComboBox *m_send = nullptr, *m_ending = nullptr;
    QCheckBox *m_sendHex = nullptr, *m_repeat = nullptr;
    QSpinBox  *m_repeatMs = nullptr;
    QPushButton *m_sendBtn = nullptr;
    QTimer    *m_repeatTimer = nullptr;

    QLabel *m_state = nullptr, *m_counts = nullptr;
    QTimer *m_countTimer = nullptr;

    QFile  *m_log = nullptr;
    int     m_held = 0;
    QStringList m_heldLines;
    int     m_fedLines = 0;
};

#endif // SERIALCONSOLEWINDOW_H
