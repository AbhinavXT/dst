#ifndef BRAKEBENCHWINDOW_H
#define BRAKEBENCHWINDOW_H
// =============================================================================
//  Brake Test Bench (session 360) -- Tools > Simulators.
//
//  Everything the VCC's brake logic talks to, in one window:
//    Bench           BP, BC and MR as dials, moving toward what the BIU (or
//                    the brake relays, or a hand) asks for, at rates and with
//                    faults that are set here; KAVACH's last command; what
//                    the BIU is doing.
//    BIU -> VCC      every field of the BIU's three status frames: a value or
//                    Auto, validity bits, fault and display codes held until
//                    acknowledged, every discrete bit Auto / 1 / 0.
//    Network         where the BIU listens (UDP 51235) and answers (the VCC,
//                    50001); its heartbeat and NMT; what it does when KAVACH
//                    goes quiet.
//    Analog -> VCC   the analog card (sessions 346-351), moved here from the
//                    Cab I/O Simulator: six 4-20 mA channels over UDP; BP, BC
//                    and MR follow the bench's pressures.
//    Brake relays    the per-brake pressures of session 357, for a VCC that
//                    brakes by relay: they follow the Cab I/O Simulator's
//                    output word.
//    Frames          every BIU frame both ways and every analog frame, with
//                    what each says and what is wrong with it.
//  The BIU's protocol is biuprotocol.h, its behaviour biusim.h, the air
//  brakeplant.h.
// =============================================================================
#include "brakebenchpreset.h"
#include "brakeplant.h"
#include "cabio.h"

#include <QPointer>
#include <QVector>
#include <QWidget>

#include <functional>

class BiuSimulator;
class PressureGauge;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QTimer;
class QToolButton;
class StatusLine;
class UdpSender;
namespace SimUi { class SignalLamp; }

class BrakeBenchWindow : public QWidget
{
    Q_OBJECT
public:
    enum class Source { Biu, Relays, Manual };

    explicit BrakeBenchWindow(QWidget *parent = nullptr);
    ~BrakeBenchWindow() override;

    BiuSimulator *biu() const { return m_biu; }
    BrakePlant   &plant() { return m_plant; }

    // Time: the window's timer reads the wall clock; tests set their own and
    // call advance() (what the timer does: tick the BIU, move the air, send).
    void   setClock(const std::function<qint64()> &clock) { m_clock = clock; }
    qint64 now() const { return m_clock(); }
    void   advance(double seconds);

    // ---- BIU ---------------------------------------------------------------------
    bool startBiu();                    // listen and run; false (and said) when the port will not bind
    void stopBiu();
    bool biuRunning() const;
    void setListenPort(quint16 port);
    quint16 listenPort() const;
    // Session 361: `hosts` a list ("a, b, c, d", a.b.c.d:port for one VCC's
    // own port), every frame to each; empty: the sender.
    void setReplyTo(const QString &hosts, quint16 port);
    QString replyListText();             // what the Network tab says the frames reach

    // ---- where the pressures go ----------------------------------------------------
    void   setSource(Source s);
    Source source() const { return m_source; }
    void   setRates(int ch, double apply, double release);
    void   setPlantFault(int ch, BrakePlant::Fault f, double param);
    void   setAnalogOffset(int ch, double kgcm2);     // the analog card reads off by this
    double pressure(int ch) const { return m_plant.value(ch); }

    // ---- brake relays (from the Cab I/O Simulator's output word) ----------------------
    void   observeOutputWord(int cab, quint16 word);
    int    brakeLevelNow() const;                      // -1: no output data yet
    void   setBrakePressure(int level, int ch, double kgcm2);
    double brakePressure(int level, int ch) const;

    // ---- analog card (moved from the Cab I/O Simulator, session 360) -----------------
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
    void   setAnalogTarget(const QString &host, quint16 port);
    QString analogHost() const;
    quint16 analogPort() const;
    void   setAnalogFollows(bool on);                  // BP, BC, MR channels follow the bench
    bool   analogFollows() const;
    void   setWireFormat(const CabIo::WireFormat &wf) { m_wf = wf; }
    // Analog frames sent, for tests without a socket.
    void   setAnalogSink(const std::function<void(const QByteArray &)> &fn) { m_analogSink = fn; }

    // ---- saved presets (session 362): the fault setup under a name ------------------
    BenchPreset currentPreset() const;   // what the bench is set to now
    BenchPreset cleanPreset() const { return m_clean; }   // as it opened
    void applyPreset(const BenchPreset &p);
    bool savePreset(const QString &name);         // false (and said) for an empty name
    bool loadPreset(const QString &name);         // false (and said) when there is none
    void deletePreset(const QString &name);
    void startClean();                            // the fault setup as the window opens
    QString activePreset() const { return m_activePreset; }   // "" until one is loaded or saved

    void flushLog();                    // the Frames tab's pending rows, now
    int  logRows() const;

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    QWidget *buildTopBar();
    QWidget *buildBenchTab();
    QWidget *buildStatusTab();
    QWidget *buildNetworkTab();
    QWidget *buildAnalogTab();
    QWidget *buildRelaysTab();
    QWidget *buildFramesTab();
    void restyle();
    void buildPresetMenu();
    void loadSettings();
    void saveSettings() const;
    void onUiTick();
    void refreshTop();
    bool applyReplyHosts();              // session 361: the list into the BIU; false (and said) when it is not one
    void refreshReplyList();
    static QString defaultReplyHosts();
    void refreshBench();
    void refreshStatusTab();
    void refreshAnalog();
    void refreshRelays();
    void applyTargets();
    void feedAnalog();
    void logRow(qint64 ms, bool tx, const QString &cob, const QString &name, const QByteArray &bytes,
                const QString &says, const QString &note, bool heartbeat);
    void styleChip(QLabel *chip, bool on, bool known = true);

    // model
    BiuSimulator *m_biu = nullptr;
    BrakePlant    m_plant;
    Source        m_source = Source::Biu;
    std::function<qint64()> m_clock;
    qint64        m_lastAdvanceMs = 0;
    double        m_analogOffset[BrakePlant::kChannels] = {};
    double        m_fedKg[BrakePlant::kChannels] = { -1, -1, -1 };
    int           m_relayCab = 0;        // CabIo::Cab
    quint16       m_relayWord = 0;
    bool          m_relayKnown = false;

    // top bar
    QPushButton *m_startBtn = nullptr;
    QToolButton *m_presetBtn = nullptr;   // session 362
    BenchPreset  m_clean;
    QString      m_activePreset;
    QJsonObject  m_activeJson;            // as loaded or saved: a change since is marked
    QLabel      *m_runState = nullptr;
    QLabel      *m_kavachChip = nullptr;
    QLabel      *m_cmdChip = nullptr;

    QTabWidget  *m_tabs = nullptr;
    StatusLine  *m_status = nullptr;
    QTimer      *m_timer = nullptr;
    QTimer      *m_uiTimer = nullptr;

    // bench
    QButtonGroup  *m_sourceGroup = nullptr;
    PressureGauge *m_gauge[BrakePlant::kChannels] = {};
    QLabel        *m_gaugeNote[BrakePlant::kChannels] = {};
    QDoubleSpinBox *m_apply[BrakePlant::kChannels] = {};
    QDoubleSpinBox *m_release[BrakePlant::kChannels] = {};
    QComboBox     *m_faultBox[BrakePlant::kChannels] = {};
    QDoubleSpinBox *m_faultParam[BrakePlant::kChannels] = {};
    QDoubleSpinBox *m_offsetBox[BrakePlant::kChannels] = {};
    QLabel        *m_targetWhy = nullptr;
    QLabel        *m_cmdSpeed = nullptr, *m_cmdA9 = nullptr, *m_cmdSa9 = nullptr, *m_cmdMeta = nullptr;
    QVector<QLabel *> m_cmdBits1, m_cmdBits2;
    QLabel        *m_biuNow = nullptr;
    QCheckBox     *m_isolateBox = nullptr;
    QCheckBox     *m_silentBox = nullptr;

    // BIU -> VCC
    QLabel        *m_pSends[8] = {};
    QButtonGroup  *m_pMode[8] = {};
    QDoubleSpinBox *m_pManual[8] = {};
    QButtonGroup  *m_pValid[8] = {};
    QLabel        *m_pValidNow[8] = {};
    QSpinBox      *m_isoCount = nullptr;
    QCheckBox     *m_countIso = nullptr, *m_raiseIso = nullptr, *m_waitAck = nullptr;
    QComboBox     *m_faultPick = nullptr, *m_displayPick = nullptr;
    QLabel        *m_faultQueue = nullptr, *m_displayQueue = nullptr, *m_ackNote = nullptr;
    QButtonGroup  *m_bitMode[3][8] = {};
    QLabel        *m_bitNow[3][8] = {};
    QSpinBox      *m_statusPeriod = nullptr;
    QCheckBox     *m_frameOn[3] = {};
    QDoubleSpinBox *m_testSeconds = nullptr;
    QButtonGroup  *m_testResult = nullptr;

    // network
    QSpinBox      *m_listenPort = nullptr;
    QButtonGroup  *m_replyMode = nullptr;
    QLineEdit     *m_replyHost = nullptr;      // session 361: a list
    QToolButton   *m_locoPick = nullptr;
    QLabel        *m_replyList = nullptr;
    QString        m_replyHostError, m_replyHostTip;
    QSpinBox      *m_replyPort = nullptr;
    QCheckBox     *m_hbOn = nullptr, *m_autoStart = nullptr;
    QSpinBox      *m_hbPeriod = nullptr, *m_kavachTimeout = nullptr;
    QButtonGroup  *m_biuState = nullptr, *m_nmtNode = nullptr, *m_onLoss = nullptr;
    QLabel        *m_kavachNow = nullptr;

    // analog
    struct AnRow {
        QDoubleSpinBox *mA = nullptr;
        QDoubleSpinBox *kg = nullptr;
        QComboBox *scale = nullptr;
        QLabel *reads = nullptr;
        PressureGauge *gauge = nullptr;
    };
    QVector<AnRow> m_anRows;
    QButtonGroup *m_anSendGroup = nullptr;
    QSpinBox     *m_anPeriod = nullptr;
    QVector<QPushButton *> m_anCardChips;
    QLabel       *m_anSummary = nullptr;
    QTimer       *m_anTimer = nullptr;
    QLineEdit    *m_anHost = nullptr;
    QSpinBox     *m_anPort = nullptr;
    QCheckBox    *m_anFollow = nullptr;
    UdpSender    *m_anUdp = nullptr;
    std::function<void(const QByteArray &)> m_analogSink;
    CabIo::WireFormat m_wf;
    quint16 m_anSeq = 0;
    int     m_analogSent = 0;

    // relays
    QDoubleSpinBox *m_brakeKg[CabIo::kBrakeLevels][CabIo::kBrakeChannels] = {};
    QVector<SimUi::SignalLamp *> m_relayLamps;               // NB FSB EB LE
    QLabel *m_relayNow = nullptr;

    // frames
    QTableWidget *m_log = nullptr;
    QCheckBox    *m_logHeartbeats = nullptr, *m_logStatus = nullptr, *m_logAnalog = nullptr, *m_logPause = nullptr;
    struct PendingRow { qint64 ms; bool tx; QString cob, name, bytes, says, note; };
    QVector<PendingRow> m_pending;
    bool m_loading = false;
};

#endif  // BRAKEBENCHWINDOW_H
