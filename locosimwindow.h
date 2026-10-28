#ifndef LOCOSIMWINDOW_H
#define LOCOSIMWINDOW_H
// =============================================================================
//  Loco Simulator (session 391) -- Tools > Simulators.
//
//  LocoTcasSimulator brought into DLConsole, a step at a time:
//    Speed   the loco's speed (set, accelerate, decelerate, hold), direction,
//            and the two speed sensors' messages to the VCCs every 100 ms,
//            each channel at the loco's speed or its own (slip, skid).
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
class QSlider;
class QSpinBox;
class QTimer;
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
    void   loadSettings();
    void   saveSettings() const;
    QWidget *buildSpeedTab();

    LocoSim::Motion m_motion;
    UdpSender      *m_udp = nullptr;
    QTimer         *m_timer = nullptr;
    std::function<qint64()> m_clock;
    std::function<void(const QByteArray &)> m_sink;
    qint64          m_lastTickMs = 0;
    int             m_sent = 0;
    QStringList     m_hostList;

    QPushButton    *m_startStop = nullptr;
    QDoubleSpinBox *m_speed = nullptr;
    QSlider        *m_speedSlider = nullptr;
    QDoubleSpinBox *m_maxSpeed = nullptr;
    QDoubleSpinBox *m_rate = nullptr;
    QButtonGroup   *m_drive = nullptr;
    QButtonGroup   *m_direction = nullptr;
    QCheckBox      *m_sensorOn[2] = { nullptr, nullptr };
    QDoubleSpinBox *m_channel[2][2] = { { nullptr, nullptr }, { nullptr, nullptr } };
    QDoubleSpinBox *m_wheelMm = nullptr;
    QSpinBox       *m_pulsesPerRev = nullptr;
    QSpinBox       *m_countScale = nullptr;
    QLineEdit      *m_hosts = nullptr;
    QSpinBox       *m_port = nullptr;
    QSpinBox       *m_intervalMs = nullptr;
    QLabel         *m_readout = nullptr;
    StatusLine     *m_status = nullptr;
};

#endif  // LOCOSIMWINDOW_H
