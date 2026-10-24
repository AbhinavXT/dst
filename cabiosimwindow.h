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
//    Analog -> VCC   the analog card (session 346): six 4-20 mA channels as
//                    controllers 15 / 16 send them, with what the VCC reads.
//                    Sent over UDP to a host and port (session 350), not on
//                    the serial port.
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
class QDoubleSpinBox;
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

// The window's own colour decisions (session 328). Everything else it draws
// is a palette role or a UiColor, which the contrast audit already holds in
// every theme; these mix their own -- a tinted tile, a lamp's rim -- so they
// are public for tests/test_session328.cpp to hold to the same floors.
namespace CabIoColors {
enum class Tile { Plain, On, Fault };
double textFloor();                          // 7 : 1 in High Contrast, else 4.5 : 1
double graphicFloor();                       // 7 : 1 in High Contrast, else 3 : 1
QColor tileFill(Tile t);                     // Base, tinted for On and Fault
QColor tileInk(const QColor &ink, Tile t);   // `ink`, held to textFloor() on tileFill(t)
QColor lampRim(const QString &lamp);         // a lit lamp's outline, graphicFloor() on Base
}

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
    // Readback -> VCC (session 329): answer the readback requests to 17 / 18
    // as output cards 13 / 14, each pin Auto (from the output word) or Manual.
    void setAnswerReadback(bool on);
    bool answersReadback() const { return m_rbAnswer; }
    void setReadbackCards(const QVector<quint8> &cards);     // 13, 14
    QVector<quint8> readbackCards() const;
    CabIo::ReadbackSim *readbackSim() { return &m_rb; }
    void setReadbackHealth(quint8 h);
    int  readbacksSent() const { return m_readbacksSent; }
    void refreshReadback();

    // The inputs' default (session 329): the operator's own state for every
    // input, plain or NO/NC, saved per bank; the window opens with it and
    // "Reset to default" returns to it. Without one: every input Off.
    void saveInputDefault();
    void resetInputsToDefault();
    void forgetInputDefault();
    bool hasInputDefault() const;
    // Analog -> VCC (session 346): one set of six channels, sent as each
    // ticked controller (15 top, 16 bottom) every period while sending is on.
    void   setAnalogMa(int ch, double mA);
    double analogMa(int ch) const;
    void   setAnalogPressure(int ch, double kgcm2);   // sets the mA that reads as it
    void   setAnalogScale(int ch, int bar);           // 10 or 16
    int    analogScale(int ch) const;
    void   setAnalogCards(const QVector<quint8> &ids);
    QVector<quint8> analogCards() const;
    void   setAnalogSending(bool on);
    bool   analogSending() const;
    void   setAnalogPeriodMs(int ms);
    void   sendAnalogNow();
    int    analogSent() const { return m_analogSent; }
    // Session 350: where the analog frames go (UDP).
    void    setAnalogTarget(const QString &host, quint16 port);
    QString analogHost() const;
    quint16 analogPort() const;
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
    QWidget *buildReadbackTab();
    QWidget *buildAnalogTab();
    void refreshAnalog();
    void rebuildReadback();
    void applyDefault(Bank &b) const;
    quint16 readbackWord(quint8 card, bool *known) const;
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
    int m_readbacksSent = 0, m_readbackUnanswered = 0, m_readbacksHeard = 0;
    QByteArray m_readbackPinsHeard;      // the last readback a real card sent
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
    QLabel      *m_outLegend = nullptr;
    QLabel      *m_outBytes = nullptr;
    CabIoUi::TileGrid *m_lampGrid = nullptr;
    QVector<CabIoUi::LampTile *> m_lamps;
    // Readback -> VCC
    struct RbRow {
        QLabel *name = nullptr;
        QPushButton *mode = nullptr;           // checked = Manual
        QPushButton *bits[5] = {};             // a b fault trans output
        QLabel *byte = nullptr;
        QLabel *commanded = nullptr;
    };
    CabIo::ReadbackSim m_rb;
    bool m_rbAnswer = false;
    quint8 m_rbHealth = CabIo::kHealthy;
    QButtonGroup *m_rbAnswerGroup = nullptr;
    QVector<QPushButton *> m_rbCardChips;
    QSpinBox    *m_rbHealthSpin = nullptr;
    QLabel      *m_rbSummary = nullptr;
    QWidget     *m_rbTable = nullptr;
    QVector<RbRow> m_rbRows;
    // Analog -> VCC
    struct AnRow {
        QDoubleSpinBox *mA = nullptr;
        QDoubleSpinBox *kg = nullptr;
        QComboBox *scale = nullptr;
        QLabel *reads = nullptr;
    };
    QVector<AnRow> m_anRows;
    QButtonGroup *m_anSendGroup = nullptr;
    QSpinBox    *m_anPeriod = nullptr;
    QVector<QPushButton *> m_anCardChips;
    QLabel      *m_anSummary = nullptr;
    QTimer      *m_anTimer = nullptr;
    QLineEdit   *m_anHost = nullptr;
    QSpinBox    *m_anPort = nullptr;
    class UdpSender *m_anUdp = nullptr;
    quint16 m_anSeq = 0;
    int m_analogSent = 0;
    QPushButton *m_resetDefault = nullptr;
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
