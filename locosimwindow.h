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
//    Settings  the wheel, the VCCs, how often.
//  Next: the RFID readers over serial from the same position, then braking
//  from the BIU's command. The motion and the message are locosim.h.
// =============================================================================
#include "locosim.h"

#include <QWidget>

#include <functional>

class QButtonGroup;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;
class SpeedGauge;
class StatusLine;
class UdpSender;

class LocoSimWindow : public QWidget
{
    Q_OBJECT
public:
    explicit LocoSimWindow(QWidget *parent = nullptr);
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
};

#endif  // LOCOSIMWINDOW_H
