#include "locosimwindow.h"
#include "biusim.h"
#include "settings.h"
#include "statusline.h"
#include "udpsender.h"
#include "windowgeometry.h"
#include "flasher/flashercore.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

QString defaultHosts()
{
    // Loco 1's four VCC controllers: LocoTcasSimulator's Ipconfig1.xml dest_id 1-4.
    QStringList ips;
    for (const Flasher::LocoController &controller : Flasher::locoControllers())
        if (controller.loco == 1) ips << controller.ip;
    return ips.join(QStringLiteral(", "));
}

QDoubleSpinBox *kmhSpin(QWidget *parent, double max)
{
    auto *spin = new QDoubleSpinBox(parent);
    spin->setRange(0, max);
    spin->setDecimals(1);
    spin->setSuffix(QStringLiteral(" km/h"));
    return spin;
}

}  // namespace

LocoSimWindow::LocoSimWindow(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setObjectName(QStringLiteral("locoSimWindow"));
    setWindowTitle(tr("Loco Simulator"));
    setAttribute(Qt::WA_DeleteOnClose);
    WindowGeometry::makeResizableWindow(this);
    m_clock = []() { return QDateTime::currentMSecsSinceEpoch(); };
    m_udp = new UdpSender(this);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 10);
    root->setSpacing(10);

    auto *top = new QHBoxLayout;
    m_startStop = new QPushButton(tr("Start"), this);
    m_startStop->setObjectName(QStringLiteral("locoSimStart"));
    m_startStop->setToolTip(tr("Send both speed sensors' messages to the VCCs every interval"));
    top->addWidget(m_startStop);
    m_readout = new QLabel(this);
    m_readout->setObjectName(QStringLiteral("locoSimReadout"));
    m_readout->setTextInteractionFlags(Qt::TextSelectableByMouse);
    top->addWidget(m_readout, 1);
    root->addLayout(top);

    auto *tabs = new QTabWidget(this);
    tabs->setDocumentMode(true);
    tabs->addTab(buildSpeedTab(), tr("Speed"));
    root->addWidget(tabs, 1);

    m_status = new StatusLine(this);
    root->addWidget(m_status);

    connect(m_udp, &UdpSender::error, this, [this](const QString &what) {
        m_status->fail(tr("Speed messages over UDP: %1").arg(what));
    });
    connect(m_startStop, &QPushButton::clicked, this, [this]() {
        if (running()) stop();
        else start();
    });

    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        const qint64 nowMs = m_clock();
        // The clock's time, not the interval's: a late tick carries its pulses.
        // Held to 1 s so a stalled event loop does not jump the loco.
        advance(qBound(0.0, (nowMs - m_lastTickMs) / 1000.0, 1.0));
        m_lastTickMs = nowMs;
    });

    loadSettings();
    pullFromUi();
    showMotion();
    resize(760, 560);
    WindowGeometry::restore(this, QStringLiteral("locoSim"));
    m_status->state(tr("Press Start to send the speed sensors' messages to the VCCs."));
}

LocoSimWindow::~LocoSimWindow()
{
    saveSettings();
}

QWidget *LocoSimWindow::buildSpeedTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    // ---- the loco ------------------------------------------------------------
    auto *loco = new QGroupBox(tr("Loco"), page);
    auto *grid = new QGridLayout(loco);
    m_speed = kmhSpin(loco, 250);
    m_speed->setObjectName(QStringLiteral("locoSimSpeed"));
    m_speedSlider = new QSlider(Qt::Horizontal, loco);
    m_speedSlider->setRange(0, 250);
    m_maxSpeed = kmhSpin(loco, 250);
    m_maxSpeed->setToolTip(tr("Accelerating stops here; a set speed above it is held to it"));
    m_rate = new QDoubleSpinBox(loco);
    m_rate->setRange(0.1, 50);
    m_rate->setDecimals(1);
    m_rate->setSuffix(QStringLiteral(" km/h/s"));
    grid->addWidget(new QLabel(tr("Speed"), loco), 0, 0);
    grid->addWidget(m_speed, 0, 1);
    grid->addWidget(m_speedSlider, 0, 2, 1, 3);
    grid->addWidget(new QLabel(tr("Max speed"), loco), 1, 0);
    grid->addWidget(m_maxSpeed, 1, 1);

    m_drive = new QButtonGroup(loco);
    auto *driveRow = new QHBoxLayout;
    const QStringList driveNames = { tr("Hold"), tr("Accelerate"), tr("Decelerate") };
    for (int id = 0; id < driveNames.size(); ++id) {
        auto *button = new QRadioButton(driveNames.at(id), loco);
        m_drive->addButton(button, id);
        driveRow->addWidget(button);
    }
    driveRow->addWidget(new QLabel(tr("at"), loco));
    driveRow->addWidget(m_rate);
    driveRow->addStretch(1);
    grid->addWidget(new QLabel(tr("Drive"), loco), 2, 0);
    grid->addLayout(driveRow, 2, 1, 1, 4);

    m_direction = new QButtonGroup(loco);
    auto *directionRow = new QHBoxLayout;
    auto *forward = new QRadioButton(tr("Forward (1)"), loco);
    auto *reverse = new QRadioButton(tr("Reverse (2)"), loco);
    m_direction->addButton(forward, LocoSim::Forward);
    m_direction->addButton(reverse, LocoSim::Reverse);
    directionRow->addWidget(forward);
    directionRow->addWidget(reverse);
    directionRow->addStretch(1);
    grid->addWidget(new QLabel(tr("Direction"), loco), 3, 0);
    grid->addLayout(directionRow, 3, 1, 1, 4);
    grid->setColumnStretch(4, 1);
    layout->addWidget(loco);

    // ---- the sensors ---------------------------------------------------------
    auto *sensors = new QGroupBox(tr("Speed sensors"), page);
    auto *sensorGrid = new QGridLayout(sensors);
    sensorGrid->addWidget(new QLabel(tr("Channel 1 (tachometer 1)"), sensors), 0, 1);
    sensorGrid->addWidget(new QLabel(tr("Channel 2 (tachometer 2)"), sensors), 0, 2);
    for (int sensor = 0; sensor < 2; ++sensor) {
        m_sensorOn[sensor] = new QCheckBox(tr("Sensor %1").arg(sensor + 1), sensors);
        sensorGrid->addWidget(m_sensorOn[sensor], sensor + 1, 0);
        for (int channel = 0; channel < 2; ++channel) {
            auto *spin = new QDoubleSpinBox(sensors);
            spin->setRange(-1, 250);
            spin->setDecimals(1);
            spin->setSuffix(QStringLiteral(" km/h"));
            spin->setSpecialValueText(tr("Loco's speed"));
            spin->setToolTip(tr("This channel's own speed, to simulate slip, skid or a dead channel (0). "
                                "At the bottom: the loco's speed."));
            m_channel[sensor][channel] = spin;
            sensorGrid->addWidget(spin, sensor + 1, channel + 1);
        }
    }
    sensorGrid->setColumnStretch(3, 1);
    layout->addWidget(sensors);

    // ---- the wheel and where it goes ----------------------------------------
    auto *link = new QGroupBox(tr("Wheel and VCCs"), page);
    auto *linkGrid = new QGridLayout(link);
    m_wheelMm = new QDoubleSpinBox(link);
    m_wheelMm->setRange(500, 1500);
    m_wheelMm->setDecimals(1);
    m_wheelMm->setSuffix(QStringLiteral(" mm"));
    m_pulsesPerRev = new QSpinBox(link);
    m_pulsesPerRev->setRange(1, 1000);
    m_countScale = new QSpinBox(link);
    m_countScale->setRange(1, 1000000);
    m_countScale->setPrefix(QStringLiteral("×"));
    m_countScale->setToolTip(tr("LocoTcasSimulator sends pulses ×1000"));
    auto *wheelRow = new QHBoxLayout;
    wheelRow->addWidget(m_wheelMm);
    wheelRow->addWidget(new QLabel(tr("pulses a turn"), link));
    wheelRow->addWidget(m_pulsesPerRev);
    wheelRow->addWidget(new QLabel(tr("count"), link));
    wheelRow->addWidget(m_countScale);
    wheelRow->addStretch(1);
    linkGrid->addWidget(new QLabel(tr("Wheel"), link), 0, 0);
    linkGrid->addLayout(wheelRow, 0, 1);

    auto *hostRow = new QHBoxLayout;
    m_hosts = new QLineEdit(link);
    m_hosts->setObjectName(QStringLiteral("locoSimHosts"));
    m_hosts->setToolTip(tr("The VCCs to send to, separated by commas"));
    hostRow->addWidget(m_hosts, 1);
    auto *locoPick = new QToolButton(link);
    locoPick->setText(tr("Loco"));
    locoPick->setPopupMode(QToolButton::InstantPopup);
    locoPick->setToolTip(tr("A loco's four VCC controllers, from the Firmware Flasher's address table"));
    auto *locoMenu = new QMenu(locoPick);
    for (int locoNumber = 1; locoNumber <= Flasher::locoCount(); ++locoNumber) {
        QStringList ips;
        for (const Flasher::LocoController &controller : Flasher::locoControllers())
            if (controller.loco == locoNumber) ips << controller.ip;
        locoMenu->addAction(tr("Loco %1: all 4 VCCs").arg(locoNumber), this,
                            [this, ips]() { setHosts(ips.join(QStringLiteral(", "))); });
    }
    locoPick->setMenu(locoMenu);
    hostRow->addWidget(locoPick);
    m_port = new QSpinBox(link);
    m_port->setRange(1, 65535);
    hostRow->addWidget(new QLabel(tr("port"), link));
    hostRow->addWidget(m_port);
    m_intervalMs = new QSpinBox(link);
    m_intervalMs->setRange(20, 1000);
    m_intervalMs->setSuffix(QStringLiteral(" ms"));
    hostRow->addWidget(new QLabel(tr("every"), link));
    hostRow->addWidget(m_intervalMs);
    linkGrid->addWidget(new QLabel(tr("VCCs"), link), 1, 0);
    linkGrid->addLayout(hostRow, 1, 1);
    layout->addWidget(link);
    layout->addStretch(1);

    // ---- wiring --------------------------------------------------------------
    connect(m_speed, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double kmh) {
        m_motion.setSpeed(kmh);
        showMotion();
    });
    connect(m_speedSlider, &QSlider::valueChanged, this, [this](int kmh) {
        m_motion.setSpeed(kmh);
        showMotion();
    });
    const auto pull = [this]() { pullFromUi(); showMotion(); };
    connect(m_maxSpeed, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, pull);
    connect(m_rate, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, pull);
    connect(m_drive, &QButtonGroup::idClicked, this, pull);
    connect(m_direction, &QButtonGroup::idClicked, this, pull);
    for (int sensor = 0; sensor < 2; ++sensor)
        for (int channel = 0; channel < 2; ++channel)
            connect(m_channel[sensor][channel], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, pull);
    connect(m_wheelMm, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, pull);
    connect(m_pulsesPerRev, QOverload<int>::of(&QSpinBox::valueChanged), this, pull);
    connect(m_countScale, QOverload<int>::of(&QSpinBox::valueChanged), this, pull);
    connect(m_intervalMs, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int ms) {
        m_timer->setInterval(ms);
    });
    connect(m_hosts, &QLineEdit::editingFinished, this, [this]() { setHosts(m_hosts->text()); });
    return page;
}

// ---- the controls and the motion ---------------------------------------------------

void LocoSimWindow::pullFromUi()
{
    m_motion.setMaxSpeed(m_maxSpeed->value());
    m_motion.setRate(m_rate->value());
    m_motion.setDrive(LocoSim::Motion::Drive(qMax(0, m_drive->checkedId())));
    m_motion.setDirection(m_direction->checkedId() == LocoSim::Reverse ? LocoSim::Reverse : LocoSim::Forward);
    for (int sensor = 0; sensor < 2; ++sensor)
        for (int channel = 0; channel < 2; ++channel)
            m_motion.setChannelSpeed(sensor, channel, m_channel[sensor][channel]->value());
    LocoSim::Wheel wheel;
    wheel.diameterMm = m_wheelMm->value();
    wheel.pulsesPerRev = m_pulsesPerRev->value();
    wheel.countScale = m_countScale->value();
    m_motion.setWheel(wheel);
}

void LocoSimWindow::showMotion()
{
    const QSignalBlocker blockSpeed(m_speed);
    const QSignalBlocker blockSlider(m_speedSlider);
    m_speed->setValue(m_motion.speed());
    m_speedSlider->setValue(int(m_motion.speed() + 0.5));
    const LocoSim::Motion::Drive drive = m_motion.drive();
    if ((drive == LocoSim::Motion::Drive::Accelerate && m_motion.speed() >= m_motion.maxSpeed())
        || (drive == LocoSim::Motion::Drive::Decelerate && m_motion.speed() <= 0)) {
        // At the end of the ramp: hold.
        m_motion.setDrive(LocoSim::Motion::Drive::Hold);
        m_drive->button(0)->setChecked(true);
    }
    m_readout->setText(tr("%1 km/h · position %2 m · %3 messages sent")
                           .arg(m_motion.speed(), 0, 'f', 1)
                           .arg(m_motion.position(), 0, 'f', 1)
                           .arg(m_sent));
}

// ---- running ---------------------------------------------------------------------

void LocoSimWindow::setHosts(const QString &hosts)
{
    m_hosts->setText(hosts);
    m_hosts->setCursorPosition(0);
    QStringList parsed;
    QString why;
    if (!BiuSimulator::parseHosts(hosts, &parsed, &why)) {
        m_status->fail(tr("The VCC list: %1").arg(why));
        return;
    }
    m_hostList = parsed;
}

bool LocoSimWindow::start()
{
    setHosts(m_hosts->text());
    if (m_hostList.isEmpty() && !m_sink) {
        m_status->fail(tr("No VCC to send to: list at least one address."));
        return false;
    }
    if (!m_sensorOn[0]->isChecked() && !m_sensorOn[1]->isChecked())
        m_status->warn(tr("Both sensors are off: the loco moves but nothing is sent."));
    else
        m_status->ok(tr("Sending to %1 on port %2 every %3 ms.")
                         .arg(m_hostList.join(QStringLiteral(", ")))
                         .arg(m_port->value())
                         .arg(m_intervalMs->value()));
    pullFromUi();
    m_motion.resetCounts();
    m_lastTickMs = m_clock();
    m_timer->start(m_intervalMs->value());
    m_startStop->setText(tr("Stop"));
    return true;
}

void LocoSimWindow::stop()
{
    m_timer->stop();
    m_startStop->setText(tr("Start"));
    m_status->state(tr("Stopped after %1 messages.").arg(m_sent));
}

bool LocoSimWindow::running() const
{
    return m_timer->isActive();
}

void LocoSimWindow::advance(double seconds)
{
    const LocoSim::Step step = m_motion.step(seconds);
    QVector<UdpSender::Target> targets;
    for (const QString &host : m_hostList) targets.append(UdpSender::Target{ host, quint16(m_port->value()) });
    for (int sensor = 0; sensor < 2; ++sensor) {
        if (!m_sensorOn[sensor]->isChecked()) continue;
        const QByteArray message = LocoSim::speedMessage(sensor + 1, m_motion.direction(),
                                                         step.pulses[sensor][0], step.pulses[sensor][1]);
        if (m_sink) m_sink(message);
        else m_udp->sendOnce(message, targets);
        ++m_sent;
    }
    showMotion();
}

// ---- settings ----------------------------------------------------------------------

void LocoSimWindow::loadSettings()
{
    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("locosim"));
    m_maxSpeed->setValue(settings.value(QStringLiteral("maxSpeed"), 120).toDouble());
    m_speed->setValue(settings.value(QStringLiteral("speed"), 0).toDouble());
    m_rate->setValue(settings.value(QStringLiteral("rate"), 1).toDouble());
    m_drive->button(0)->setChecked(true);
    const int direction = settings.value(QStringLiteral("direction"), int(LocoSim::Forward)).toInt();
    m_direction->button(direction == LocoSim::Reverse ? LocoSim::Reverse : LocoSim::Forward)->setChecked(true);
    for (int sensor = 0; sensor < 2; ++sensor) {
        m_sensorOn[sensor]->setChecked(settings.value(QStringLiteral("sensor%1").arg(sensor + 1), sensor == 0).toBool());
        for (int channel = 0; channel < 2; ++channel)
            m_channel[sensor][channel]->setValue(-1);   // a slip is a test, not a setting
    }
    m_wheelMm->setValue(settings.value(QStringLiteral("wheelMm"), 950).toDouble());
    m_pulsesPerRev->setValue(settings.value(QStringLiteral("pulsesPerRev"), 30).toInt());
    m_countScale->setValue(settings.value(QStringLiteral("countScale"), 1000).toInt());
    m_port->setValue(settings.value(QStringLiteral("port"), 50001).toInt());
    m_intervalMs->setValue(settings.value(QStringLiteral("intervalMs"), 100).toInt());
    m_timer->setInterval(m_intervalMs->value());
    setHosts(settings.value(QStringLiteral("hosts"), defaultHosts()).toString());
}

void LocoSimWindow::saveSettings() const
{
    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("locosim"));
    settings.setValue(QStringLiteral("maxSpeed"), m_maxSpeed->value());
    settings.setValue(QStringLiteral("speed"), m_motion.speed());
    settings.setValue(QStringLiteral("rate"), m_rate->value());
    settings.setValue(QStringLiteral("direction"), int(m_motion.direction()));
    for (int sensor = 0; sensor < 2; ++sensor)
        settings.setValue(QStringLiteral("sensor%1").arg(sensor + 1), m_sensorOn[sensor]->isChecked());
    settings.setValue(QStringLiteral("wheelMm"), m_wheelMm->value());
    settings.setValue(QStringLiteral("pulsesPerRev"), m_pulsesPerRev->value());
    settings.setValue(QStringLiteral("countScale"), m_countScale->value());
    settings.setValue(QStringLiteral("hosts"), m_hosts->text());
    settings.setValue(QStringLiteral("port"), m_port->value());
    settings.setValue(QStringLiteral("intervalMs"), m_intervalMs->value());
}
