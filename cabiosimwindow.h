#ifndef CABIOSIMWINDOW_H
#define CABIOSIMWINDOW_H
// =============================================================================
//  Cab I/O Simulator (session 324) -- Tools > Transmit.
//
//  Stands in for the cab on the VCC's IOA serial link:
//    Inputs -> VCC   the input controllers. Each switch is toggled by a click,
//                    a NO/NC contact has four states (00 01 10 11), and any
//                    of them flickers every 250 ms. The VCC's input requests
//                    are answered (or the data is sent on a period).
//    Outputs <- VCC  the output data the VCC sends to the output cards
//                    (13 / 14), drawn as lamps by STRUCT_LOCO_OUTPUT's names.
//    Frames          every frame both ways, and the wire format.
//  SIFA-extended and old cab layouts both (cabio.h).
//
//  The port is one of SerialManager's, so it shows a status-bar chip and a
//  terminal can watch it; its lines are never fed to a console tab (they
//  are binary frames). Closing the window closes a port it opened.
// =============================================================================
#include "cabio.h"

#include <QHash>
#include <QMap>
#include <QPointer>
#include <QWidget>

#include <functional>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QTimer;
class QVBoxLayout;
class SerialLink;
class SerialManager;
class StatusLine;

namespace CabIoUi { class TileGrid; class InputTile; class LampTile; }

class CabIoSimWindow : public QWidget
{
    Q_OBJECT
public:
    explicit CabIoSimWindow(SerialManager *serial, QWidget *parent = nullptr);
    ~CabIoSimWindow() override;

    enum class Respond { Answer, Periodic };

    void setCab(CabIo::Cab cab);
    CabIo::Cab cab() const { return m_cab; }
    int bankCount() const { return m_banks.size(); }
    CabIo::InputBank *bank(int i) { return &m_banks[i].bank; }
    // The ids a bank answers for (a chip per id in its header).
    QVector<quint8> bankIds(int i) const;
    void setBankIds(int i, const QVector<quint8> &ids);

    void setRespond(Respond r);
    Respond respond() const { return m_respond; }
    void setPeriodMs(int ms);
    void setFlickerPeriodMs(int ms);
    int  flickerPeriodMs() const;
    void setHealth(quint8 h);
    void setWireFormat(const CabIo::WireFormat &wf);
    CabIo::WireFormat wireFormat() const { return m_wf; }

    // Bytes as if read from the port, and where writes go instead of a port
    // (tests; a sink also stands in when no port is open).
    void feedBytes(const QByteArray &bytes);
    void setWriteSink(std::function<void(const QByteArray &)> sink) { m_sink = std::move(sink); }

    // Open a port as the Open button would (tests); opened() follows.
    bool openPort(const QString &portName, int baud);
    bool isPortOpen() const;

    void tickFlicker();          // one flicker half-period, now
    void sendNow();              // each answering id's data, once, unasked

    int     requestsSeen() const { return m_requests; }
    int     repliesSent() const { return m_replies; }
    bool    outputSeen() const { return m_outFrames > 0; }
    quint16 outputWord() const { return m_outWord; }
    int     outputChanges(int bit) const { return m_outChanges.value(bit); }
    int     logRows() const;     // the Frames tab's rows, after a flush
    void    flushLog();
    QString crcHint() const;     // what the last received frame's CRC matched
    QString nextReplyText() const;

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    struct Bank {
        CabIo::InputBank bank;
        QVector<CabIoUi::InputTile *> tiles;
        QVector<QPushButton *> idChips;
        QLabel *word = nullptr;
        QLabel *counts = nullptr;
        int replies = 0;
        qint64 lastReplyMs = 0;
    };
    struct OutDest { quint16 word = 0; quint16 seq = 0; qint64 atMs = 0; int frames = 0; };

    QWidget *buildPortBar();
    QWidget *buildInputsTab();
    QWidget *buildOutputsTab();
    QWidget *buildFramesTab();
    void rebuildInputs();
    void rebuildOutputs();
    void refreshInputs();
    void refreshOutputs();
    void refreshPort();
    void restyle();
    void loadSettings();
    void saveSettings() const;
    void refreshPorts();
    QString currentPortName() const;
    void openOrClose();
    void attachLink(SerialLink *link);

    void onFrame(const CabIo::Frame &f);
    void write(const QByteArray &bytes);
    void logFrame(bool tx, const CabIo::Frame &f);
    int  bankFor(quint8 id) const;
    void onPeriodic();
    void onUiTick();

    SerialManager *m_serial = nullptr;
    QPointer<SerialLink> m_link;
    bool m_weOpened = false;
    std::function<void(const QByteArray &)> m_sink;

    CabIo::Cab        m_cab = CabIo::Cab::Sifa;
    CabIo::WireFormat m_wf;
    CabIo::Scanner    m_scanner;
    Respond m_respond = Respond::Answer;
    quint8  m_health = CabIo::kHealthy;
    quint16 m_ownSeq = 0;
    QVector<Bank> m_banks;

    int m_requests = 0, m_requestsOther = 0, m_replies = 0;
    int m_outFrames = 0, m_outRequests = 0, m_otherFrames = 0;
    quint16 m_outWord = 0;
    qint64  m_outAtMs = 0;
    QMap<quint8, OutDest> m_outDest;
    QHash<int, int>    m_outChanges;
    QHash<int, qint64> m_outChangedAtMs;
    QVector<CabIo::Crc> m_lastCrcMatches;
    bool m_crcSeen = false;

    // ---- widgets ----
    QComboBox   *m_port = nullptr;
    QComboBox   *m_baud = nullptr;
    QPushButton *m_open = nullptr;
    QLabel      *m_portState = nullptr;
    QLabel      *m_traffic = nullptr;
    QButtonGroup *m_cabGroup = nullptr;
    QTabWidget  *m_tabs = nullptr;

    QButtonGroup *m_respondGroup = nullptr;
    QSpinBox    *m_period = nullptr;
    QSpinBox    *m_flicker = nullptr;
    QSpinBox    *m_healthSpin = nullptr;
    QLabel      *m_inputsSummary = nullptr;
    QVBoxLayout *m_banksBox = nullptr;
    QWidget     *m_banksHost = nullptr;
    QLabel      *m_preview = nullptr;

    QLabel      *m_outHeadline = nullptr;
    QLabel      *m_outDetail = nullptr;
    QLabel      *m_outBytes = nullptr;
    CabIoUi::TileGrid *m_lampGrid = nullptr;
    QVector<CabIoUi::LampTile *> m_lamps;
    QLabel      *m_outFooter = nullptr;

    QComboBox   *m_crc = nullptr;
    QComboBox   *m_length = nullptr;
    QCheckBox   *m_echo = nullptr;
    QLabel      *m_crcNote = nullptr;
    QPushButton *m_useCrc = nullptr;
    QLineEdit   *m_pasteHex = nullptr;
    QCheckBox   *m_pause = nullptr;
    QTableWidget *m_log = nullptr;
    QLabel      *m_logNote = nullptr;
    QVector<QStringList> m_pendingRows;
    QVector<int>         m_pendingKinds;    // StatusLine-like: 0 plain, 1 warn

    StatusLine  *m_status = nullptr;
    QTimer      *m_flickerTimer = nullptr;
    QTimer      *m_periodTimer = nullptr;
    QTimer      *m_uiTimer = nullptr;
};

#endif  // CABIOSIMWINDOW_H
