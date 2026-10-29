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
//              (session 405) a fault per tag: Late (N ms), Sent twice or
//              Bad CRC, for reader 1, 2 or both, each time the loco passes;
//              its word on the tag's chip in the track view.
//              (session 406) Then: at the route's end, stop; or run on into
//              the next route, found by location (same direction, its first
//              tag nearest ahead of the last one, in the same file or folder);
//              or the routes set in an order.
//              (session 409) each reader OK or Fail, and its health frame
//              every 1 s / 2 s while it is on, as the simulator; at Fail it
//              sends no tags.
//              (session 410) Send a tag…: one tag now, by hand, from its
//              page_x / page_y, on reader 1 and / or 2 (the simulator's SPAD
//              scenario RFID interface; tagsenddialog.h).
//              (session 411) a line of a Station Layout, the same way: its
//              tags in the order a loco meets them, each edit there followed,
//              the loco and the tags sent drawn on the layout.
//              (session 402) Tag Builder: the route open in a Tag Builder,
//              as it is now, saved or not; each edit there comes here and
//              the loco stays where it is (back to the start tag when the
//              edited route no longer has its place).
//    Frames    every speed message sent, decoded, with its bytes.
//    Settings  the wheel, the VCCs, how often.
//    Record    (session 404) the top bar's Record button: a test recording
//              (benchrecord.h) of this window's and the Brake Test Bench's
//              frames; starting it starts both, stopping it stops both.
//    Scenario  (session 407) a list of steps, each a trigger (a time, a tag,
//              a location) and an action on the loco, its readers or the
//              Brake Test Bench (scenario.h); Run / Stop; .scenario.json;
//              each step marked in a recording.
//    Brake     (session 398) the Brake Test Bench's air slows the loco
//              (LocoSim::brakeDeceleration): the VCC commands the BIU, the
//              bench's BP and BC move, the loco brakes; the rates are on
//              Settings. The motion and the messages are locosim.h.
// =============================================================================
#include "locosim.h"
#include "scenario.h"
#include "rfidtag.h"
#include "simpreview.h"

#include <QPointer>
#include <QSet>
#include <QWidget>

#include <functional>
#include <memory>

class BrakeBenchWindow;
class TagSendDialog;
namespace BenchRecord { class Recorder; }
class SimSentLog;
class TagBuilderWindow;
class StationLayoutWindow;
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

    // ---- the test recording (session 404) ------------------------------------
    // MainWindow's recorder, shared with the Brake Test Bench. Recording on:
    // this window starts sending if it was not; off: it stops.
    void   setRecorder(BenchRecord::Recorder *recorder);
    bool   toggleRecording();                  // the Record button; false (and said) when it cannot

    // ---- the scenario (session 407) -------------------------------------------
    void   setScenario(const Scenario::Script &script);   // into the tab's table
    Scenario::Script scenario(QString *why = nullptr) const;   // the table as a script; *why when a cell is not one
    bool   loadScenario(const QString &path);   // false (and said)
    bool   saveScenario(const QString &path);
    bool   runScenario();                       // false (and said) when the table is not a script
    void   stopScenario();
    bool   scenarioRunning() const { return m_scenarioOn; }
    QString stepStatus(int step) const;         // "", "done at 12.3 s", "not done: …"

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
    // Session 402: the open Tag Builders (MainWindow's), and following one.
    void   setTagBuilders(const std::function<QList<TagBuilderWindow *>()> &list) { m_builders = list; }
    bool   followBuilder(TagBuilderWindow *builder);   // false (and said) for none
    bool   followsBuilder() const { return !m_builder.isNull(); }
    void   chooseBuilder();                     // the Tag Builder button: one builder, or a menu of them
    // Session 411: a line of a Station Layout, live (StationLayoutWindow::
    // lineAsSimRoute): each edit there comes here as a Tag Builder's does (the
    // loco stays where it is), and the layout draws the loco and the tags sent.
    bool   followLayout(StationLayoutWindow *layout, const QString &line, int dir);   // false (and said) when refused
    void   unfollowLayout();                    // the route stays; the layout no longer draws the loco
    bool   followsLayout() const { return !m_layout.isNull(); }
    QString layoutLine() const { return m_layoutLine; }
    // The loco among the tags' own locations (an adjustment tag's renumbering
    // undone), where the layout draws it; NaN off the route.
    double layoutLocation() const;
    void   setStartRow(int row);                // and places the loco there
    bool   placeAtStart();                      // false (and said) without a route whose direction is set
    double routePosition() const { return m_routeD; }
    double pulsePosition() const { return m_pulseD; }   // by speed sensor 1 channel 1's pulses
    int    routeRow() const { return m_routeRow; }   // the row whose span the loco is in, -1 none
    bool   routeEnded() const { return m_routeEnded; }
    RouteStrip *wholeRoute() const { return m_wholeStrip; }
    RouteStrip *followView() const { return m_followStrip; }
    void   setReaderOn(int reader, bool on);
    // Session 409: a reader's health (OK or Fail); at Fail it sends no tags.
    void   setReaderHealth(int reader, bool ok);
    bool   readerHealthOk(int reader) const { return reader == 2 ? !m_readerFail[1] : !m_readerFail[0]; }
    void   healthTick();                        // what the health timer does: each reader's frame when due
    // Session 410: a tag sent by hand (Send a tag…), as the simulator's SPAD window.
    bool   sendTagByHand(int reader, const QString &pageX, const QString &pageY, QString *why = nullptr);
    QVector<QPair<QString, QByteArray>> routeTags() const;   // the route's rows: name, tag
    TagSendDialog *openTagSend();               // the one window, raised
    int    healthSent(int reader) const { return reader == 2 ? m_healthSent[1] : m_healthSent[0]; }
    // Tests: health frames go here (port 0 = A, 1 = B). With only a tag sink, none is sent.
    void   setHealthSink(const std::function<void(int, const QByteArray &)> &sink) { m_healthSink = sink; }
    void   setSkipped(const QString &tag, bool skip);   // tag as the rows name it ("904", "904D")
    // Session 405: a fault on a tag (as the rows name it); readers 1, 2 or 3 (both).
    struct TagFaultSetting {
        LocoSim::TagFault kind = LocoSim::TagFault::None;
        int readers = 3;
        int delayMs = 500;
    };
    void   setTagFault(const QString &tag, LocoSim::TagFault kind, int readers = 3, int delayMs = 500);
    // Session 406: what happens at the route's end.
    enum class Chain { Stop, Auto, List };
    struct ChainRef { QString file; int index = 0; QString name; };
    void   setChain(Chain mode);
    Chain  chain() const { return m_chain; }
    void   setChainList(const QVector<ChainRef> &routes);   // and Chain::List
    QVector<ChainRef> chainList() const { return m_chainList; }
    QVector<ChainRef> chainCandidates() const;   // this file's routes, then the folder's other files'
    QString chainNote() const;                   // "then: 124_DN_LOOP1_HFZ, 196 m on"
    int    chainedCount() const { return m_chained; }
    QString routeName() const;                   // the route the loco is on, "" none
    TagFaultSetting tagFault(const QString &tag) const { return m_faults.value(tag); }
    int    lateQueued() const { return m_late.size(); }
    void   setReader2OwnPort(bool own);
    int    tagsSent() const { return m_tagsSent; }
    // Tests: tag frames go here (port 0 = A, 1 = B) instead of the serial ports.
    void   setTagSink(const std::function<void(int, const QByteArray &)> &sink) { m_tagSink = sink; }
    // Session 415: the RFID frames sent, listed in the RFID frames tab.
    SimSentLog *sentLog() const { return m_sentLog; }
    // Session 417: a row of the Tags table by the tag's name (opened, scrolled
    // to); false when the route has no such tag. routeTagSelected: the
    // operator picked one there.
    bool   selectRouteTag(const QString &name);
    void   setOpenInWorkbench(std::function<void(const QString &, const QString &)> open);

signals:
    void routeTagSelected(const QString &name);   // session 417

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
    QWidget *buildScenarioTab();
    void   addStepRow(const Scenario::Step &step);
    void   scenarioTick();
    bool   doStep(const Scenario::Step &step, QString *why);
    bool   hasTag(const QString &name) const;
    void   applyBrake();
    bool   readerSends(int reader) const;       // on, and at OK
    void   resetHealthClock(int reader);
    void   record(const QString &kind, const QString &peer, const QByteArray &bytes, const QString &says,
                  const QString &note = QString());
    void   refreshRecordButton();
    void   fillRouteBox();
    void   showRoute(int index);                // the route on the tab, the loco left where it is
    void   builderRouteChanged();
    void   stopFollowingBuilder();
    void   routeEdited(const RfidTag::Route &route, const QString &source);   // the loco stays where it is
    void   layoutRouteChanged();
    void   showOnLayout();
    void   fillTagTable();
    void   stepRoute(const LocoSim::Step &step);
    void   sendTag(const SimPreview::Readers::Send &send, const QString &note = QString());
    void   sendWithFault(const SimPreview::Readers::Send &send);
    void   flushLate();
    void   markFaultedRows();
    void   showFaultRow(int row);
    QHash<int, QString> faultLabels() const;
    bool   chainOn();                           // at the route's end: into the next; false: none
    bool   plannedNext(ChainRef *next, double *ahead, QString *why) const;
    QVector<RfidTag::Route> routesIn(const QString &file) const;
    void   refreshChainNote();
    void   editChainOrder();
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
    QPushButton    *m_record = nullptr;
    QPointer<BenchRecord::Recorder> m_recorder;
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
    QPushButton    *m_fromBuilder = nullptr;
    QPointer<TagBuilderWindow> m_builder;
    std::function<QList<TagBuilderWindow *>()> m_builders;
    QPointer<StationLayoutWindow> m_layout;   // session 411
    QString         m_layoutLine;
    int             m_layoutDir = 0;
    QVector<double> m_ownLoc;           // per route row: the tag's own location
    QComboBox      *m_routeBox = nullptr;
    QComboBox      *m_startTag = nullptr;
    QLabel         *m_routeWhere = nullptr;
    QCheckBox      *m_readerOn[2] = { nullptr, nullptr };
    QComboBox      *m_tagPort[2] = { nullptr, nullptr };
    QPushButton    *m_tagPortOpen[2] = { nullptr, nullptr };
    QLabel         *m_tagPortState[2] = { nullptr, nullptr };
    QButtonGroup   *m_reader2Port = nullptr;
    QButtonGroup   *m_readerHealth[2] = { nullptr, nullptr };   // session 409: 0 OK, 1 Fail
    bool            m_readerFail[2] = { false, false };
    QTimer         *m_healthTimer = nullptr;
    qint64          m_healthDue[2] = { 0, 0 };
    int             m_healthSent[2] = { 0, 0 };
    std::function<void(int, const QByteArray &)> m_healthSink;
    QTableWidget   *m_tags = nullptr;
    QToolButton    *m_tagsToggle = nullptr;
    bool            m_selectingTag = false;   // session 417: selectRouteTag at work
    SimSentLog     *m_sentLog = nullptr;      // session 415: the RFID frames tab
    QPointer<TagSendDialog> m_tagSend;
    RouteStrip     *m_wholeStrip = nullptr;
    RouteStrip     *m_followStrip = nullptr;
    QLabel         *m_pulseWhere = nullptr;
    QLabel         *m_rfidSpeedValue = nullptr;   // session 400: the speed on the RFID tab
    QDoubleSpinBox *m_rfidSpeed = nullptr;
    QLabel         *m_rfidBrake = nullptr;
    double          m_pulseD = 0;       // where speed sensor 1's pulses put the loco
    QHash<int, int> m_tagStates;        // route row -> RouteStrip::TagState
    QHash<QString, TagFaultSetting> m_faults;   // session 405: by tag name
    struct Late { qint64 dueMs; SimPreview::Readers::Send send; QString note; };
    QVector<Late>   m_late;             // late tags waiting for their time
    Chain           m_chain = Chain::Stop;   // session 406
    QVector<ChainRef> m_chainList;
    int             m_chainAt = -1;     // the current route's place in m_chainList
    int             m_chained = 0;
    mutable QHash<QString, QVector<RfidTag::Route>> m_fileRoutes;
    QComboBox      *m_chainBox = nullptr;
    QPushButton    *m_chainOrder = nullptr;
    QLabel         *m_chainNoteLabel = nullptr;
    // the scenario
    QTableWidget   *m_steps = nullptr;
    QLineEdit      *m_scenarioName = nullptr;
    QPushButton    *m_scenarioRun = nullptr;
    QLabel         *m_scenarioState = nullptr;
    Scenario::Script m_script;          // what Run took from the table
    QVector<int>    m_stepDone;         // per step: 0 waiting, 1 done, 2 not done
    QStringList     m_stepNote;
    qint64          m_scenarioStartMs = 0;
    bool            m_scenarioOn = false;
    QSet<QString>   m_tagsSeen;         // reader 1's tags since Run
};

#endif  // LOCOSIMWINDOW_H
