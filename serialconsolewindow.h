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
//
//  A VIEWER OF A PORT, NOT ITS OWNER (session 102)
//    The port and Feed console live in the SerialManager MainWindow owns.
//    Closing this window leaves the port running and feeding the console
//    (the status bar shows a chip for it); only Close stops it. Choosing a
//    port that is already running attaches to it.
//
//    The MessageDispatcher constructor is the standalone form: it makes a
//    manager of its own, so its ports close with the window, as before.
// =============================================================================

#include <QPointer>
#include <functional>
#include <QStringList>
#include <QWidget>

#include "seriallink.h"
#include "serialfilesender.h"
#include "serialmanager.h"
#include "logentry.h"
#include "logquery.h"

class MessageDispatcher;
class SerialManager;
class QCheckBox;
class QComboBox;
class QFile;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QueryLineEdit;
class QPushButton;
class QSpinBox;
class QTimer;

class SerialConsoleWindow : public QWidget
{
    Q_OBJECT
public:
    explicit SerialConsoleWindow(MessageDispatcher *dispatcher, QWidget *parent = nullptr);
    explicit SerialConsoleWindow(SerialManager *manager, QWidget *parent = nullptr);
    ~SerialConsoleWindow() override;

    // Select a port and attach to it (a running port shows its own settings).
    void showPort(const QString &portName);
    SerialManager *manager() const { return m_mgr; }

    SerialLink *link() const { return m_link; }
    SerialConfig configFromUi() const;
    void setConfigToUi(const SerialConfig &c);

    // Tests drive these as the buttons do.
    bool openPort();
    void closePort();
    bool sendText(const QString &text);        // honours hex / line-ending settings
    // Send file without the file and options dialogs (session 108).
    bool sendFileData(const QByteArray &data, const QString &name, const SerialSendOptions &options);
    SerialFileSender *fileSender() const { return m_sender; }

    // Macros (session 109): the current profile's, or the default set.
    QVector<SerialMacro> macros() const { return m_macros; }
    void setMacros(const QVector<SerialMacro> &macros);   // saves them
    // Click on macro `index`. A confirm macro asks through the confirmer
    // (a dialog, unless a test replaces it). True when it was sent.
    bool runMacro(int index);
    void setMacroConfirmer(std::function<bool(const SerialMacro &)> f) { m_confirmMacro = std::move(f); }
    QString receivedText() const;
    QString statusText() const;
    QString healthText() const;                // the line-health label
    // Session 110: Show only / Highlight, as typed (tests set them).
    void setViewFilter(const QString &query);
    void setViewHighlight(const QString &query);
    QString viewQueryError() const;
    int highlightedLines() const;              // blocks currently highlighted

    // Profiles (session 105). saveProfile is the Save button without its
    // name prompt; it stores the current settings under `name`.
    bool saveProfile(const QString &name);
    bool deleteProfile(const QString &name);
    void selectProfile(const QString &name);   // loads it into the window
    QString currentProfile() const;            // empty: none selected
    QString tabKey() const;                    // the console tab fed from this port
    static quint16 kvchForPort(const QString &portName);   // SerialManager's
    static constexpr quint8 kSerialSourceId = 254;

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void build();
    void attach(SerialLink *link);
    void onPortChosen();
    void saveSettings();
    bool isOpen() const { return m_link && m_link->isOpen(); }
    void refreshPorts();
    void refreshProfiles(const QString &select = QString());
    // Every line the view shows goes through here. `raw` empty = a marker
    // ("── opened … ──", a hex row): always shown. Otherwise it is a line
    // the card sent (In) or we sent (Out), subject to Show only / Highlight.
    void appendView(const QString &text, const QByteArray &raw = QByteArray(),
                    qint64 ms = 0, Direction dir = Direction::None);
    struct ViewLine { QString text; QByteArray raw; qint64 ms = 0; Direction dir = Direction::None; };
    bool passesFilter(const ViewLine &l) const;
    void showLine(const ViewLine &l);
    void applyViewQueries();                   // parse both boxes, rebuild the view
    LogEntry entryFor(const ViewLine &l) const;
    void onLine(const QByteArray &line, qint64 ms);
    void onBytes(const QByteArray &bytes, qint64 ms);
    void onWritten(const QByteArray &bytes, qint64 ms);
    void flushHexRow();
    bool askSendOptions(SerialSendOptions *o, const QString &fileName, qint64 size);
    void loadMacros();
    void rebuildMacroButtons();
    void editMacros();
    void updateState();
    void setLogging(bool on);
    QString stamp(qint64 ms) const;

    SerialManager  *m_mgr = nullptr;
    bool            m_ownsMgr = false;
    SerialLink     *m_link = nullptr;          // attached; owned by m_mgr
    bool            m_attaching = false;

    QComboBox *m_port = nullptr, *m_baud = nullptr, *m_dataBits = nullptr,
              *m_parity = nullptr, *m_stopBits = nullptr, *m_flow = nullptr;
    QPushButton *m_open = nullptr;
    QComboBox *m_profile = nullptr;
    QCheckBox *m_autoOpen = nullptr;
    QCheckBox *m_dtr = nullptr, *m_rts = nullptr, *m_lowLatency = nullptr;

    QCheckBox *m_hexView = nullptr, *m_timestamps = nullptr, *m_echo = nullptr,
              *m_hold = nullptr, *m_logFile = nullptr, *m_feed = nullptr;
    QPlainTextEdit *m_view = nullptr;

    QComboBox *m_send = nullptr, *m_ending = nullptr;
    QCheckBox *m_sendHex = nullptr, *m_repeat = nullptr;
    QSpinBox  *m_repeatMs = nullptr;
    QPushButton *m_sendBtn = nullptr;
    QTimer    *m_repeatTimer = nullptr;

    QLabel *m_state = nullptr, *m_counts = nullptr, *m_health = nullptr;
    QTimer *m_countTimer = nullptr;

    QFile  *m_log = nullptr;
    SerialFileSender *m_sender = nullptr;      // session 108: paced file send
    QProgressBar *m_sendProgress = nullptr;
    QPushButton  *m_sendStop = nullptr;
    QString       m_sendingName;
    QVector<SerialMacro> m_macros;
    QWidget      *m_macroBar = nullptr;
    std::function<bool(const SerialMacro &)> m_confirmMacro;
    SerialHexDumper m_hex;                     // session 107: 16-byte rows
    QTimer *m_hexFlush = nullptr;
    int     m_held = 0;
    QVector<ViewLine> m_heldLines;
    QVector<ViewLine> m_buffer;                // what the view can be rebuilt from
    // Session 110: Show only / Highlight, in the console's query language.
    QueryLineEdit *m_filterEdit = nullptr, *m_highlightEdit = nullptr;
    QLabel   *m_queryError = nullptr;
    QTimer   *m_queryDebounce = nullptr;
    LogQuery  m_filter, m_highlight;
};

#endif // SERIALCONSOLEWINDOW_H
