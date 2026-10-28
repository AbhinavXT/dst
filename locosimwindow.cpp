#include "locosimwindow.h"
#include "biusim.h"
#include "settings.h"
#include "simwidgets.h"
#include "speedgauge.h"
#include "statusline.h"
#include "udpsender.h"
#include "uicolors.h"
#include "windowgeometry.h"
#include "flasher/flashercore.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTime>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace {

constexpr int kMaxFrameRows = 500;
// The drive's segmented buttons, left to right, as the motion's drives.
const LocoSim::Motion::Drive kDriveOfButton[3] = { LocoSim::Motion::Drive::Decelerate, LocoSim::Motion::Drive::Hold,
                                                   LocoSim::Motion::Drive::Accelerate };

QString defaultHosts()
{
    // Loco 1's four VCC controllers: LocoTcasSimulator's Ipconfig1.xml dest_id 1-4.
    QStringList ips;
    for (const Flasher::LocoController &controller : Flasher::locoControllers())
        if (controller.loco == 1) ips << controller.ip;
    return ips.join(QStringLiteral(", "));
}

QFrame *card(const QString &title, QWidget *parent, QVBoxLayout **body)
{
    auto *frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("card"));
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(14, 10, 14, 12);
    layout->setSpacing(8);
    if (!title.isEmpty()) {
        auto *heading = new QLabel(title, frame);
        heading->setObjectName(QStringLiteral("cardTitle"));
        layout->addWidget(heading);
    }
    *body = layout;
    return frame;
}

QLabel *labelNamed(const QString &text, const char *name, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QLatin1String(name));
    return label;
}

QDoubleSpinBox *kmhSpin(QWidget *parent, double max)
{
    auto *spin = new QDoubleSpinBox(parent);
    spin->setRange(0, max);
    spin->setDecimals(1);
    spin->setSuffix(QStringLiteral(" km/h"));
    return spin;
}

double scaleFor(double maxSpeed)
{
    // A round number a little above the max speed, so the red band shows.
    return qBound(40.0, std::ceil((maxSpeed + 10) / 20) * 20, 260.0);
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

    // ---- the top bar: what it is, what it is doing, Start --------------------
    auto *top = new QHBoxLayout;
    auto *titles = new QVBoxLayout;
    titles->setSpacing(0);
    titles->addWidget(labelNamed(tr("Loco Simulator"), "headline", this));
    m_state = labelNamed(QString(), "muted", this);
    titles->addWidget(m_state);
    top->addLayout(titles);
    top->addStretch(1);
    m_linkChip = labelNamed(QString(), "mono", this);
    m_linkChip->setTextInteractionFlags(Qt::TextSelectableByMouse);
    top->addWidget(m_linkChip);
    m_startStop = new QPushButton(tr("Start"), this);
    m_startStop->setObjectName(QStringLiteral("primary"));
    m_startStop->setToolTip(tr("Send the speed sensors' messages to the VCCs every interval"));
    m_startStop->setCursor(Qt::PointingHandCursor);
    top->addWidget(m_startStop);
    root->addLayout(top);

    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);

    auto *tabs = new QTabWidget(this);
    tabs->setDocumentMode(true);
    tabs->addTab(buildDriveTab(), tr("Drive"));
    tabs->addTab(buildFramesTab(), tr("Frames"));
    tabs->addTab(buildSettingsTab(), tr("Settings"));
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
    connect(m_timer, &QTimer::timeout, this, &LocoSimWindow::tick);

    loadSettings();
    pullFromUi();
    showMotion();
    refreshTop();
    restyle();
    UiColor::onThemeChange(this, [this]() { restyle(); });
    resize(1120, 720);
    WindowGeometry::restore(this, QStringLiteral("locoSim"));
    m_status->state(tr("Set a speed, then Start to send the speed sensors' messages to the VCCs."));
}

LocoSimWindow::~LocoSimWindow()
{
    saveSettings();
}

void LocoSimWindow::restyle()
{
    setStyleSheet(SimUi::baseStyleSheet()
                  + QStringLiteral("QLabel#bigValue { font-size: 15pt; font-weight: 600; }"
                                   "QCheckBox#cardTitle { font-size: 13pt; font-weight: 600; }"
                                   "QPushButton#primary[running=\"true\"] { background: %1; border-color: %1; }")
                        .arg(UiColor::error().name()));
}

// ---- Drive ---------------------------------------------------------------------------

QWidget *LocoSimWindow::buildDriveTab()
{
    auto *page = new QWidget(this);
    auto *columns = new QHBoxLayout(page);
    columns->setContentsMargins(0, 8, 0, 0);
    columns->setSpacing(12);

    // ---- the loco: speedometer and controls -----------------------------------
    QVBoxLayout *locoBody = nullptr;
    QFrame *loco = card(tr("Loco"), page, &locoBody);
    m_speedometer = new SpeedGauge(QString(), true, loco);
    m_speedometer->setObjectName(QStringLiteral("locoSimSpeedometer"));
    locoBody->addWidget(m_speedometer, 1);

    auto *numbers = new QGridLayout;
    numbers->setHorizontalSpacing(18);
    numbers->addWidget(labelNamed(tr("SPEED"), "groupHeading", loco), 0, 0);
    numbers->addWidget(labelNamed(tr("ODOMETER"), "groupHeading", loco), 0, 1);
    m_speed = kmhSpin(loco, 250);
    m_speed->setObjectName(QStringLiteral("locoSimSpeed"));
    m_speed->setToolTip(tr("The loco's speed now; the speedometer sets it too"));
    numbers->addWidget(m_speed, 1, 0);
    auto *odometerRow = new QHBoxLayout;
    m_odometer = labelNamed(QString(), "bigValue", loco);
    m_odometer->setAccessibleName(tr("Odometer"));
    odometerRow->addWidget(m_odometer);
    odometerRow->addSpacing(6);
    auto *zero = new QToolButton(loco);
    zero->setText(tr("Zero"));
    zero->setToolTip(tr("Set the odometer back to 0 m"));
    connect(zero, &QToolButton::clicked, this, [this]() {
        m_motion.setPosition(0);
        showMotion();
    });
    odometerRow->addWidget(zero);
    odometerRow->addStretch(1);
    numbers->addLayout(odometerRow, 1, 1);
    numbers->setColumnStretch(2, 1);
    locoBody->addLayout(numbers);

    auto *controls = new QGridLayout;
    controls->setHorizontalSpacing(10);
    controls->setVerticalSpacing(8);
    controls->addWidget(labelNamed(tr("Drive"), "muted", loco), 0, 0);
    auto *driveRow = new QHBoxLayout;
    driveRow->addWidget(SimUi::segmented({ tr("\u25C0 Decelerate"), tr("Hold"), tr("Accelerate \u25B6") }, &m_drive, loco));
    driveRow->addSpacing(8);
    driveRow->addWidget(labelNamed(tr("at"), "muted", loco));
    m_rate = new QDoubleSpinBox(loco);
    m_rate->setRange(0.1, 50);
    m_rate->setDecimals(1);
    m_rate->setSuffix(QStringLiteral(" km/h/s"));
    driveRow->addWidget(m_rate);
    driveRow->addStretch(1);
    controls->addLayout(driveRow, 0, 1);

    controls->addWidget(labelNamed(tr("Quick"), "muted", loco), 1, 0);
    auto *quickRow = new QHBoxLayout;
    quickRow->setSpacing(6);
    for (int kmh : { 0, 15, 30, 60, 90, 110, 120 }) {
        auto *chip = new QPushButton(QString::number(kmh), loco);
        chip->setObjectName(QStringLiteral("idChip"));
        chip->setCursor(Qt::PointingHandCursor);
        chip->setToolTip(kmh == 0 ? tr("Stop the loco now") : tr("Set %1 km/h now").arg(kmh));
        connect(chip, &QPushButton::clicked, this, [this, kmh]() {
            m_drive->button(1)->setChecked(true);
            m_motion.setDrive(LocoSim::Motion::Drive::Hold);
            m_motion.setSpeed(kmh);
            showMotion();
        });
        quickRow->addWidget(chip);
    }
    quickRow->addWidget(labelNamed(tr("km/h"), "muted", loco));
    quickRow->addStretch(1);
    controls->addLayout(quickRow, 1, 1);

    controls->addWidget(labelNamed(tr("Direction"), "muted", loco), 2, 0);
    auto *directionRow = new QHBoxLayout;
    directionRow->addWidget(SimUi::segmented({ tr("Forward (1)"), tr("Reverse (2)") }, &m_direction, loco));
    directionRow->addSpacing(16);
    directionRow->addWidget(labelNamed(tr("Max speed"), "muted", loco));
    m_maxSpeed = kmhSpin(loco, 250);
    m_maxSpeed->setToolTip(tr("Accelerating stops here; a set speed above it is held to it"));
    directionRow->addWidget(m_maxSpeed);
    directionRow->addStretch(1);
    controls->addLayout(directionRow, 2, 1);
    controls->setColumnStretch(1, 1);
    locoBody->addLayout(controls);
    columns->addWidget(loco, 5);

    // ---- the sensors -----------------------------------------------------------
    auto *sensorColumn = new QVBoxLayout;
    sensorColumn->setSpacing(12);
    for (int sensor = 0; sensor < 2; ++sensor) {
        QVBoxLayout *body = nullptr;
        QFrame *sensorCard = card(QString(), page, &body);
        auto *head = new QHBoxLayout;
        m_sensorOn[sensor] = new QCheckBox(tr("Speed sensor %1").arg(sensor + 1), sensorCard);
        m_sensorOn[sensor]->setObjectName(QStringLiteral("cardTitle"));
        m_sensorOn[sensor]->setToolTip(tr("Send this sensor's message (speed_sensor_reader = %1)").arg(sensor + 1));
        head->addWidget(m_sensorOn[sensor]);
        head->addStretch(1);
        head->addWidget(labelNamed(tr("reader %1").arg(sensor + 1), "mono", sensorCard));
        body->addLayout(head);
        auto *gauges = new QGridLayout;
        gauges->setVerticalSpacing(4);
        for (int channel = 0; channel < 2; ++channel) {
            auto *gauge = new SpeedGauge(tr("Ch %1").arg(channel + 1), false, sensorCard);
            gauge->setToolTip(tr("The speed this channel's pulses say (tachometer %1)").arg(channel + 1));
            m_channelGauge[sensor][channel] = gauge;
            gauges->addWidget(gauge, 0, channel);
            auto *pulses = labelNamed(tr("not sent yet"), "mono", sensorCard);
            pulses->setAlignment(Qt::AlignCenter);
            m_channelPulses[sensor][channel] = pulses;
            gauges->addWidget(pulses, 1, channel);
            auto *spin = new QDoubleSpinBox(sensorCard);
            spin->setRange(-1, 250);
            spin->setDecimals(1);
            spin->setSuffix(QStringLiteral(" km/h"));
            spin->setSpecialValueText(tr("Loco's speed"));
            spin->setObjectName(QStringLiteral("locoSimChannel%1%2").arg(sensor + 1).arg(channel + 1));
            spin->setToolTip(tr("This channel's own speed, to simulate slip, skid or a dead channel (0). "
                                "At the bottom: the loco's speed."));
            m_channel[sensor][channel] = spin;
            gauges->addWidget(spin, 2, channel);
        }
        gauges->setRowStretch(0, 1);
        body->addLayout(gauges, 1);
        sensorColumn->addWidget(sensorCard, 1);
    }
    columns->addLayout(sensorColumn, 4);

    // ---- wiring ----------------------------------------------------------------
    connect(m_speedometer, &SpeedGauge::valueEdited, this, [this](double kmh) {
        m_motion.setSpeed(kmh);
        showMotion();
    });
    connect(m_speed, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double kmh) {
        m_motion.setSpeed(kmh);
        showMotion();
    });
    const auto pull = [this]() { pullFromUi(); showMotion(); };
    connect(m_maxSpeed, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, pull);
    connect(m_rate, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, pull);
    connect(m_drive, &QButtonGroup::idClicked, this, pull);
    connect(m_direction, &QButtonGroup::idClicked, this, pull);
    for (int sensor = 0; sensor < 2; ++sensor) {
        connect(m_sensorOn[sensor], &QCheckBox::toggled, this, pull);
        for (int channel = 0; channel < 2; ++channel)
            connect(m_channel[sensor][channel], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, pull);
    }
    return page;
}

// ---- Frames ----------------------------------------------------------------------------

QWidget *LocoSimWindow::buildFramesTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 8, 0, 0);
    auto *head = new QHBoxLayout;
    head->addWidget(labelNamed(tr("Every speed message sent, newest last (the last %1).").arg(kMaxFrameRows),
                               "muted", page));
    head->addStretch(1);
    auto *clear = new QPushButton(tr("Clear"), page);
    head->addWidget(clear);
    layout->addLayout(head);
    m_frames = new QTableWidget(0, 8, page);
    m_frames->setObjectName(QStringLiteral("locoSimFrames"));
    m_frames->setHorizontalHeaderLabels({ tr("Time"), tr("Sensor"), tr("Direction"), tr("Tachometer 1"),
                                          tr("Tachometer 2"), tr("Ch 1 km/h"), tr("Ch 2 km/h"), tr("Bytes") });
    m_frames->verticalHeader()->setVisible(false);
    m_frames->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_frames->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_frames->horizontalHeader()->setStretchLastSection(true);
    m_frames->setAlternatingRowColors(true);
    layout->addWidget(m_frames, 1);
    connect(clear, &QPushButton::clicked, m_frames, [this]() { m_frames->setRowCount(0); });
    return page;
}

void LocoSimWindow::logMessage(const QByteArray &message, double channel1Kmh, double channel2Kmh)
{
    const auto *bytes = reinterpret_cast<const uchar *>(message.constData());
    const quint32 tachometer1 = bytes[7] | bytes[8] << 8 | bytes[9] << 16 | quint32(bytes[10]) << 24;
    const quint32 tachometer2 = bytes[11] | bytes[12] << 8 | bytes[13] << 16 | quint32(bytes[14]) << 24;
    const QStringList cells = {
        QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")),
        QString::number(bytes[5]),
        bytes[6] == LocoSim::Reverse ? tr("2 reverse") : tr("1 forward"),
        QString::number(tachometer1),
        QString::number(tachometer2),
        QString::number(channel1Kmh, 'f', 1),
        QString::number(channel2Kmh, 'f', 1),
        QString::fromLatin1(message.toHex(' ')),
    };
    const bool atBottom = m_frames->rowCount() == 0
                          || m_frames->verticalScrollBar() == nullptr
                          || m_frames->verticalScrollBar()->value() == m_frames->verticalScrollBar()->maximum();
    if (m_frames->rowCount() >= kMaxFrameRows) m_frames->removeRow(0);
    const int row = m_frames->rowCount();
    m_frames->insertRow(row);
    for (int column = 0; column < cells.size(); ++column) {
        auto *item = new QTableWidgetItem(cells.at(column));
        if (column >= 1 && column <= 6) item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_frames->setItem(row, column, item);
    }
    if (atBottom) m_frames->scrollToBottom();
}

// ---- Settings ----------------------------------------------------------------------------

QWidget *LocoSimWindow::buildSettingsTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(12);

    QVBoxLayout *wheelBody = nullptr;
    QFrame *wheel = card(tr("Wheel"), page, &wheelBody);
    auto *wheelGrid = new QGridLayout;
    m_wheelMm = new QDoubleSpinBox(wheel);
    m_wheelMm->setRange(500, 1500);
    m_wheelMm->setDecimals(1);
    m_wheelMm->setSuffix(QStringLiteral(" mm"));
    m_pulsesPerRev = new QSpinBox(wheel);
    m_pulsesPerRev->setRange(1, 1000);
    m_countScale = new QSpinBox(wheel);
    m_countScale->setRange(1, 1000000);
    m_countScale->setPrefix(QStringLiteral("\u00D7"));
    m_countScale->setToolTip(tr("LocoTcasSimulator sends pulses \u00D71000"));
    wheelGrid->addWidget(labelNamed(tr("Diameter"), "muted", wheel), 0, 0);
    wheelGrid->addWidget(m_wheelMm, 0, 1);
    wheelGrid->addWidget(labelNamed(tr("Pulses a turn"), "muted", wheel), 1, 0);
    wheelGrid->addWidget(m_pulsesPerRev, 1, 1);
    wheelGrid->addWidget(labelNamed(tr("Count scale"), "muted", wheel), 2, 0);
    wheelGrid->addWidget(m_countScale, 2, 1);
    m_perMetre = labelNamed(QString(), "mono", wheel);
    wheelGrid->addWidget(m_perMetre, 3, 1, 1, 2);
    wheelGrid->setColumnStretch(2, 1);
    wheelBody->addLayout(wheelGrid);
    layout->addWidget(wheel);

    QVBoxLayout *linkBody = nullptr;
    QFrame *link = card(tr("VCCs"), page, &linkBody);
    auto *linkGrid = new QGridLayout;
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
    linkGrid->addWidget(labelNamed(tr("Send to"), "muted", link), 0, 0);
    linkGrid->addLayout(hostRow, 0, 1, 1, 2);
    m_port = new QSpinBox(link);
    m_port->setRange(1, 65535);
    linkGrid->addWidget(labelNamed(tr("Port"), "muted", link), 1, 0);
    linkGrid->addWidget(m_port, 1, 1);
    m_intervalMs = new QSpinBox(link);
    m_intervalMs->setRange(20, 1000);
    m_intervalMs->setSuffix(QStringLiteral(" ms"));
    linkGrid->addWidget(labelNamed(tr("Every"), "muted", link), 2, 0);
    linkGrid->addWidget(m_intervalMs, 2, 1);
    linkGrid->setColumnStretch(2, 1);
    linkBody->addLayout(linkGrid);
    auto *note = labelNamed(tr("Each tick sends one LPIM_LKAVACH_INFO_MSG per sensor (19 bytes: src 20, dest 2, id 1, "
                               "length 19, reader, direction, tachometer 1 and 2, CRC 0), LocoTcasSimulator's message, "
                               "to every VCC listed."), "muted", link);
    note->setWordWrap(true);
    linkBody->addWidget(note);
    layout->addWidget(link);
    layout->addStretch(1);

    const auto pull = [this]() { pullFromUi(); showMotion(); };
    connect(m_wheelMm, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, pull);
    connect(m_pulsesPerRev, QOverload<int>::of(&QSpinBox::valueChanged), this, pull);
    connect(m_countScale, QOverload<int>::of(&QSpinBox::valueChanged), this, pull);
    connect(m_port, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { refreshTop(); });
    connect(m_intervalMs, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int ms) {
        m_timer->setInterval(ms);
        refreshTop();
    });
    connect(m_hosts, &QLineEdit::editingFinished, this, [this]() { setHosts(m_hosts->text()); });
    return page;
}

// ---- the controls and the motion ---------------------------------------------------

void LocoSimWindow::pullFromUi()
{
    m_motion.setMaxSpeed(m_maxSpeed->value());
    m_motion.setRate(m_rate->value());
    m_motion.setDrive(kDriveOfButton[qBound(0, m_drive->checkedId(), 2)]);
    m_motion.setDirection(m_direction->checkedId() == 1 ? LocoSim::Reverse : LocoSim::Forward);
    for (int sensor = 0; sensor < 2; ++sensor) {
        for (int channel = 0; channel < 2; ++channel) {
            m_motion.setChannelSpeed(sensor, channel, m_channel[sensor][channel]->value());
            m_channelGauge[sensor][channel]->setEnabled(m_sensorOn[sensor]->isChecked());
            m_channel[sensor][channel]->setEnabled(m_sensorOn[sensor]->isChecked());
        }
    }
    LocoSim::Wheel wheel;
    wheel.diameterMm = m_wheelMm->value();
    wheel.pulsesPerRev = m_pulsesPerRev->value();
    wheel.countScale = m_countScale->value();
    m_motion.setWheel(wheel);
    m_perMetre->setText(tr("%1 counts a metre").arg(LocoSim::pulsesPerMetre(wheel), 0, 'f', 1));
}

void LocoSimWindow::showMotion()
{
    const LocoSim::Motion::Drive drive = m_motion.drive();
    if ((drive == LocoSim::Motion::Drive::Accelerate && m_motion.speed() >= m_motion.maxSpeed())
        || (drive == LocoSim::Motion::Drive::Decelerate && m_motion.speed() <= 0)) {
        // At the end of the ramp: hold.
        m_motion.setDrive(LocoSim::Motion::Drive::Hold);
        m_drive->button(1)->setChecked(true);
    }
    const double scale = scaleFor(m_motion.maxSpeed());
    m_speedometer->setScaleMaximum(scale);
    m_speedometer->setLimit(m_motion.maxSpeed());
    m_speedometer->setValue(m_motion.speed());
    {
        const QSignalBlocker block(m_speed);
        m_speed->setValue(m_motion.speed());
    }
    for (int sensor = 0; sensor < 2; ++sensor)
        for (int channel = 0; channel < 2; ++channel) {
            m_channelGauge[sensor][channel]->setScaleMaximum(scale);
            m_channelGauge[sensor][channel]->setLimit(m_motion.maxSpeed());
        }
    const double metres = m_motion.position();
    m_odometer->setText(std::fabs(metres) >= 10000 ? tr("%1 km").arg(metres / 1000, 0, 'f', 3)
                                                   : tr("%1 m").arg(metres, 0, 'f', 1));
    m_speedometer->setTitle(m_motion.direction() == LocoSim::Reverse ? tr("\u25C0 Reverse") : tr("Forward \u25B6"));
    refreshTop();
}

void LocoSimWindow::showStep(const LocoSim::Step &step, double seconds)
{
    const double perMetre = LocoSim::pulsesPerMetre(m_motion.wheel());
    if (seconds <= 0 || perMetre <= 0) return;   // no time passed: keep the last reading
    for (int sensor = 0; sensor < 2; ++sensor) {
        for (int channel = 0; channel < 2; ++channel) {
            SpeedGauge *gauge = m_channelGauge[sensor][channel];
            QLabel *pulses = m_channelPulses[sensor][channel];
            if (!m_sensorOn[sensor]->isChecked()) {
                gauge->setValue(0);
                pulses->setText(tr("off"));
                continue;
            }
            gauge->setValue(step.pulses[sensor][channel] / perMetre / seconds * 3.6);
            pulses->setText(tr("%1 pulses").arg(step.pulses[sensor][channel]));
        }
    }
}

void LocoSimWindow::refreshTop()
{
    if (!m_state) return;
    const bool on = running();
    const int sensors = int(m_sensorOn[0] && m_sensorOn[0]->isChecked()) + int(m_sensorOn[1] && m_sensorOn[1]->isChecked());
    m_state->setText(on ? tr("Sending \u00B7 %1 sensor(s) \u00B7 %2 messages \u00B7 %3 s")
                              .arg(sensors).arg(m_sent).arg((m_clock() - m_startedMs) / 1000)
                        : tr("Stopped \u00B7 %1 messages sent").arg(m_sent));
    if (m_linkChip && m_port && m_intervalMs)
        m_linkChip->setText(tr("%1 VCC(s) \u00B7 UDP %2 \u00B7 every %3 ms")
                                .arg(m_hostList.size()).arg(m_port->value()).arg(m_intervalMs->value()));
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
    refreshTop();
}

bool LocoSimWindow::start()
{
    setHosts(m_hosts->text());
    if (m_hostList.isEmpty() && !m_sink) {
        m_status->fail(tr("No VCC to send to: list at least one address in Settings."));
        return false;
    }
    if (!m_sensorOn[0]->isChecked() && !m_sensorOn[1]->isChecked())
        m_status->warn(tr("Both sensors are off: the loco moves but nothing is sent."));
    else
        m_status->ok(tr("Sending to %1.").arg(m_hostList.join(QStringLiteral(", "))));
    pullFromUi();
    m_motion.resetCounts();
    m_lastTickMs = m_startedMs = m_clock();
    m_timer->start(m_intervalMs->value());
    m_startStop->setText(tr("Stop"));
    m_startStop->setProperty("running", true);
    restyle();
    refreshTop();
    return true;
}

void LocoSimWindow::stop()
{
    m_timer->stop();
    m_startStop->setText(tr("Start"));
    m_startStop->setProperty("running", false);
    restyle();
    m_status->state(tr("Stopped after %1 messages.").arg(m_sent));
    refreshTop();
}

void LocoSimWindow::tick()
{
    const qint64 nowMs = m_clock();
    // No time passed: nothing to count. A message of 0 pulses would say the loco stopped.
    if (nowMs <= m_lastTickMs) return;
    // The clock's time, not the interval's: a late tick carries its pulses.
    // Held to 1 s so a stalled event loop does not jump the loco.
    advance(qMin(1.0, (nowMs - m_lastTickMs) / 1000.0));
    m_lastTickMs = nowMs;
}

bool LocoSimWindow::running() const
{
    return m_timer && m_timer->isActive();
}

void LocoSimWindow::advance(double seconds)
{
    const LocoSim::Step step = m_motion.step(seconds);
    const double perMetre = LocoSim::pulsesPerMetre(m_motion.wheel());
    const auto kmhOf = [&](quint32 pulses) {
        return seconds > 0 && perMetre > 0 ? pulses / perMetre / seconds * 3.6 : 0.0;
    };
    QVector<UdpSender::Target> targets;
    for (const QString &host : m_hostList) targets.append(UdpSender::Target{ host, quint16(m_port->value()) });
    for (int sensor = 0; sensor < 2; ++sensor) {
        if (!m_sensorOn[sensor]->isChecked()) continue;
        const QByteArray message = LocoSim::speedMessage(sensor + 1, m_motion.direction(),
                                                         step.pulses[sensor][0], step.pulses[sensor][1]);
        if (m_sink) m_sink(message);
        else m_udp->sendOnce(message, targets);
        ++m_sent;
        logMessage(message, kmhOf(step.pulses[sensor][0]), kmhOf(step.pulses[sensor][1]));
    }
    showStep(step, seconds);
    showMotion();
}

// ---- settings ----------------------------------------------------------------------

void LocoSimWindow::loadSettings()
{
    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("locosim"));
    m_maxSpeed->setValue(settings.value(QStringLiteral("maxSpeed"), 120).toDouble());
    m_motion.setMaxSpeed(m_maxSpeed->value());
    m_speed->setValue(settings.value(QStringLiteral("speed"), 0).toDouble());
    m_rate->setValue(settings.value(QStringLiteral("rate"), 1).toDouble());
    m_drive->button(1)->setChecked(true);
    const bool reverse = settings.value(QStringLiteral("direction"), int(LocoSim::Forward)).toInt() == LocoSim::Reverse;
    m_direction->button(reverse ? 1 : 0)->setChecked(true);
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
