#ifndef LOCOSIMWINDOW_H
#define LOCOSIMWINDOW_H
// =============================================================================
//  Loco Simulator (session 391, laid out in 393) -- Tools > Simulators.
//
//  LocoTcasSimulator brought into DLConsole, a step at a time:
//    Drive     the loco's speedometer (click or drag to set), Decelerate /
//              Hold / Accelerate at a rate, quick speeds, direction, the
//              odometer; each speed sensor's two channels as gauges of the
//              speed their pulses say, at the loco's speed or their own
//              (slip, skid).
//    Frames    every speed message sent, decoded, with its bytes.
//    RFID      (session 395) a Tag Builder route and a start tag: the loco
//              runs along the route at the Drive tab's speed (up for a dir 1
//              route, down for dir 2; Forward / Reverse stays the sensors'
//              direction bit), and reader 1 and reader 2 send each tag over
//              serial as LocoTcasSimulator does (SimPreview::Readers): one
//              port for both, or reader 2 on its own. A reader can be off, a
//              tag skipped. No chaining to the next station's route.
//              (session 400) the loco's speed: read and set here too.
//    Frames    every speed message sent, decoded, with its bytes.
//    Settings  the wheel, the VCCs, how often.
//    Brake     (session 398) the Brake Test Bench's air slows the loco
//              (LocoSim::brakeDeceleration): the VCC commands the BIU, the
//              bench's BP and BC move, the loco brakes; the rates are on
//              Settings. The motion and the messages are locosim.h.
// =============================================================================
#include "locosim.h"
#include "rfidtag.h"
#include "simpreview.h"

#include <QPointer>
#include <QWidget>

#include <functional>
#include <memory>

class BrakeBenchWindow;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;
class QToolButton;
class RouteStrip;
class SerialLink;
class SerialManager;
class SpeedGauge;
class StatusLine;
class UdpSender;

class LocoSimWindow : public QWidget
{
    Q_OBJECT
public:
    // `serial` null: the RFID tab's ports are off (no Qt Serial Port, tests).
    explicit LocoSimWindow(SerialManager *serial = nullptr, QWidget *parent = nullptr);
    ~LocoSimWindow() override;

    LocoSim::Motion &motion() { return m_motion; }

    // Time: the timer reads the wall clock; tests set their own and call
    // advance() (what the timer does: move the loco, send both sensors).
    void   setClock(const std::function<qint64()> &clock) { m_clock = clock; }
    void   advance(double seconds);
    void   tick();                      // what the timer calls: advance by the clock's time since the last tick
    // Tests: messages go here instead of over UDP.
    void   setSendSink(const std::function<void(const QByteArray &)> &sink) { m_sink = sink; }

    bool start();                       // false (and said) with no VCC to send to
    void stop();
    bool running() const;
    int  sentCount() const { return m_sent; }
    void setHosts(const QString &hosts);

    // ---- the brake (session 398) --------------------------------------------
    void   followBench(BrakeBenchWindow *bench);    // its air brakes the loco while it is open
    void   observeAir(double bpKgcm2, double bcKgcm2);
    void   setBrakeFollows(bool on);
    bool   brakeFollows() const;
    LocoSim::BrakeRates brakeRates() const;
    void   setBrakeRates(const LocoSim::BrakeRates &rates);

    // ---- RFID (session 395) ------------------------------------------------
    bool   loadRouteFile(const QString &path);   // false (and said) if it holds no route
    int    routeCount() const { return m_routes.size(); }
    void   setRoute(int index);                 // and places the loco at its first tag
    void   setStartRow(int row);                // and places the loco there
    bool   placeAtStart();                      // false (and said) without a route whose direction is set
    double routePosition() const { return m_routeD; }
    double pulsePosition() const { return m_pulseD; }   // by speed sensor 1 channel 1's pulses
    int    routeRow() const { return m_routeRow; }   // the row whose span the loco is in, -1 none
    bool   routeEnded() const { return m_routeEnded; }
    RouteStrip *wholeRoute() const { return m_wholeStrip; }
    RouteStrip *followView() const { return m_followStrip; }
    void   setReaderOn(int reader, bool on);
    void   setSkipped(const QString &tag, bool skip);   // tag as the rows name it ("904", "904D")
    void   setReader2OwnPort(bool own);
    int    tagsSent() const { return m_tagsSent; }
    // Tests: tag frames go here (port 0 = A, 1 = B) instead of the serial ports.
    void   setTagSink(const std::function<void(int, const QByteArray &)> &sink) { m_tagSink = sink; }

private:
    void   pullFromUi();                // the controls into the motion
    void   showMotion();                // the motion back into the controls
    void   showStep(const LocoSim::Step &step, double seconds);
    void   logMessage(const QByteArray &message, double channel1Kmh, double channel2Kmh);
    void   refreshTop();
    void   restyle();
    void   loadSettings();
    void   saveSettings() const;
    QWidget *buildDriveTab();
    QWidget *buildFramesTab();
    QWidget *buildSettingsTab();
    QWidget *buildRfidTab();
    void   applyBrake();
    void   fillRouteBox();
    void   fillTagTable();
    void   stepRoute(const LocoSim::Step &step);
    void   sendTag(const SimPreview::Readers::Send &send);
    void   showRoutePosition();
    QStringList skippedTags() const;
    void   refreshTagPorts();
    void   refreshTagPortState();
    void   openOrCloseTagPort(int which);
    QString tagPortName(int which) const;

    LocoSim::Motion m_motion;
    UdpSender      *m_udp = nullptr;
    QTimer         *m_timer = nullptr;
    std::function<qint64()> m_clock;
    std::function<void(const QByteArray &)> m_sink;
    qint64          m_lastTickMs = 0;
    qint64          m_startedMs = 0;
    int             m_sent = 0;
    QStringList     m_hostList;

    QPushButton    *m_startStop = nullptr;
    QLabel         *m_state = nullptr;
    QLabel         *m_linkChip = nullptr;
    SpeedGauge     *m_speedometer = nullptr;
    QDoubleSpinBox *m_speed = nullptr;
    QLabel         *m_odometer = nullptr;
    QDoubleSpinBox *m_maxSpeed = nullptr;
    QDoubleSpinBox *m_rate = nullptr;
    QButtonGroup   *m_drive = nullptr;
    QButtonGroup   *m_direction = nullptr;
    QCheckBox      *m_sensorOn[2] = { nullptr, nullptr };
    SpeedGauge     *m_channelGauge[2][2] = { { nullptr, nullptr }, { nullptr, nullptr } };
    QLabel         *m_channelPulses[2][2] = { { nullptr, nullptr }, { nullptr, nullptr } };
    QDoubleSpinBox *m_channel[2][2] = { { nullptr, nullptr }, { nullptr, nullptr } };
    QDoubleSpinBox *m_wheelMm = nullptr;
    QSpinBox       *m_pulsesPerRev = nullptr;
    QSpinBox       *m_countScale = nullptr;
    QLabel         *m_perMetre = nullptr;
    QLineEdit      *m_hosts = nullptr;
    QSpinBox       *m_port = nullptr;
    QSpinBox       *m_intervalMs = nullptr;
    QTableWidget   *m_frames = nullptr;
    StatusLine     *m_status = nullptr;

    // the brake
    QPointer<BrakeBenchWindow> m_bench;
    bool            m_airKnown = false;
    double          m_airBp = 0;
    double          m_airBc = 0;
    QCheckBox      *m_brakeFollow = nullptr;
    QLabel         *m_brakeState = nullptr;
    QDoubleSpinBox *m_normal = nullptr;
    QDoubleSpinBox *m_fullService = nullptr;
    QDoubleSpinBox *m_emergency = nullptr;
    QDoubleSpinBox *m_fullBc = nullptr;

    // RFID
    SerialManager  *m_serial = nullptr;
    QPointer<SerialLink> m_tagLink[2];
    bool            m_weOpened[2] = { false, false };
    std::function<void(int, const QByteArray &)> m_tagSink;
    QString         m_routeFile;
    QVector<RfidTag::Route> m_routes;
    int             m_routeIndex = -1;
    std::unique_ptr<SimPreview::Readers> m_readers;
    double          m_routeD = 0;
    int             m_routeRow = -1;
    bool            m_routeEnded = false;
    int             m_tagsSent = 0;
    bool            m_portWarned = false;
    QLabel         *m_routeFileLabel = nullptr;
    QComboBox      *m_routeBox = nullptr;
    QComboBox      *m_startTag = nullptr;
    QLabel         *m_routeWhere = nullptr;
    QCheckBox      *m_readerOn[2] = { nullptr, nullptr };
    QComboBox      *m_tagPort[2] = { nullptr, nullptr };
    QPushButton    *m_tagPortOpen[2] = { nullptr, nullptr };
    QLabel         *m_tagPortState[2] = { nullptr, nullptr };
    QButtonGroup   *m_reader2Port = nullptr;
    QTableWidget   *m_tags = nullptr;
    QToolButton    *m_tagsToggle = nullptr;
    RouteStrip     *m_wholeStrip = nullptr;
    RouteStrip     *m_followStrip = nullptr;
    QLabel         *m_pulseWhere = nullptr;
    QLabel         *m_rfidSpeedValue = nullptr;   // session 400: the speed on the RFID tab
    QDoubleSpinBox *m_rfidSpeed = nullptr;
    QLabel         *m_rfidBrake = nullptr;
    double          m_pulseD = 0;       // where speed sensor 1's pulses put the loco
    QHash<int, int> m_tagStates;        // route row -> RouteStrip::TagState
};

#endif  // LOCOSIMWINDOW_H
