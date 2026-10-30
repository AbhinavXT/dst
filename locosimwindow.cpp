#include "locosimwindow.h"
#include "brakebenchwindow.h"
#include "benchrecord.h"
#include "tagbuilderwindow.h"
#include "simsentlog.h"
#include "tagsenddialog.h"
#include "routestrip.h"
#include "biusim.h"
#include "capturedecoder.h"
#include "settings.h"
#include "simwidgets.h"
#include "speedgauge.h"
#include "stationlayoutwindow.h"
#include "statusline.h"
#include "taglibrary.h"
#include "udpsender.h"
#include "uicolors.h"
#include "windowgeometry.h"
#include "flasher/flashercore.h"
#ifdef DL_HAVE_SERIAL
#include "seriallink.h"
#include "serialmanager.h"
#include <QSerialPortInfo>
#endif

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QDialog>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QSet>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
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
#include <limits>

namespace {

constexpr int kMaxFrameRows = 500;
constexpr double kFollowMetres = 500;   // the RFID tab's view that follows the loco
// The RFID tab's tag table.
enum TagColumn { ColAt, ColTag, ColType, ColLoc, ColNext, ColSkip, ColReader1, ColReader2, ColFault, ColFaultOn, ColDelay,
                 TagColumns };
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

LocoSimWindow::LocoSimWindow(SerialManager *serial, QWidget *parent)
    : QWidget(parent, Qt::Window), m_serial(serial)
{
    setObjectName(QStringLiteral("locoSimWindow"));
    setWindowTitle(tr("Loco Simulator"));
    setAttribute(Qt::WA_DeleteOnClose);
    WindowGeometry::makeResizableWindow(this);
    m_clock = []() { return QDateTime::currentMSecsSinceEpoch(); };
    m_udp = new UdpSender(this);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 10);
    root->setSpacing(6);

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
    m_record = new QPushButton(tr("\u25CF Record"), this);
    m_record->setObjectName(QStringLiteral("locoSimRecord"));
    m_record->setToolTip(tr("Record a whole test: this window's and the Brake Test Bench's frames to one file, "
                            "both simulators started (and stopped) with it"));
    m_record->setCursor(Qt::PointingHandCursor);
    top->addWidget(m_record);
    m_startStop = new QPushButton(tr("Start"), this);
    m_startStop->setObjectName(QStringLiteral("primary"));
    m_startStop->setToolTip(tr("Send the speed sensors' messages to the VCCs every interval"));
    m_startStop->setCursor(Qt::PointingHandCursor);
    top->addWidget(m_startStop);
    root->addLayout(top);

    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);

    // Session 415: made before the tabs, which may send as they are built.
    m_sentLog = new SimSentLog(this);
    m_sentLog->layout()->setContentsMargins(0, 8, 0, 0);

    auto *tabs = new QTabWidget(this);
    tabs->setDocumentMode(true);
    tabs->addTab(buildDriveTab(), tr("Drive"));
    tabs->addTab(buildRfidTab(), tr("RFID"));
    tabs->addTab(buildScenarioTab(), tr("Scenario"));
    tabs->addTab(buildFramesTab(), tr("Frames"));
    // Session 415: every RFID frame sent, the tags and the readers' health.
    const int rfidFrames = tabs->addTab(m_sentLog, tr("RFID frames"));
    tabs->setTabToolTip(rfidFrames, tr("Every RFID frame sent: the tags (route, by hand, with their faults) and the "
                                       "readers' health frames, with their bytes"));
    connect(m_sentLog, &SimSentLog::countChanged, tabs, [tabs, rfidFrames](int rows) {
        tabs->setTabText(rfidFrames, rows > 0 ? tr("RFID frames (%1)").arg(rows) : tr("RFID frames"));
    });
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
    // Session 409: the readers' health, whether the loco runs or not.
    m_healthTimer = new QTimer(this);
    m_healthTimer->setInterval(100);
    connect(m_healthTimer, &QTimer::timeout, this, &LocoSimWindow::healthTick);
    m_healthTimer->start();
    connect(m_record, &QPushButton::clicked, this, [this]() { toggleRecording(); });

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
    if (m_layout) m_layout->clearSimulator();   // session 411: its loco goes with this window
#ifdef DL_HAVE_SERIAL
    for (int which = 0; which < 2; ++which)
        if (m_serial && m_tagLink[which] && m_weOpened[which]) m_serial->close(m_tagLink[which]->config().portName);
#endif
}

void LocoSimWindow::restyle()
{
    setStyleSheet(SimUi::baseStyleSheet()
                  + QStringLiteral("QLabel#bigValue { font-size: 15pt; font-weight: 600; }"
                                   "QCheckBox#cardTitle { font-size: 13pt; font-weight: 600; }"
                                   "QPushButton#primary[running=\"true\"] { background: %1; border-color: %1; }"
                                   "QPushButton#locoSimRecord[running=\"true\"] { color: %1; font-weight: 600; }")
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

    controls->addWidget(labelNamed(tr("Brake"), "muted", loco), 3, 0);
    auto *brakeRow = new QHBoxLayout;
    m_brakeFollow = new QCheckBox(tr("Brake the loco"), loco);
    m_brakeFollow->setObjectName(QStringLiteral("locoSimBrakeFollow"));
    m_brakeFollow->setToolTip(tr("The source on the right slows the loco (the BIU's command, the bench's air or "
                                 "the VCC's @biu_cmd); while it brakes, the brake wins over the drive"));
    brakeRow->addWidget(m_brakeFollow);
    // Session 418: the command's brake type, as LocoTcasSimulator, or the air.
    m_brakeSource = new QComboBox(loco);
    m_brakeSource->setObjectName(QStringLiteral("locoSimBrakeSource"));
    m_brakeSource->addItem(tr("by the BIU's command"), int(LocoSim::BrakeSource::Command));
    m_brakeSource->addItem(tr("by the bench's air"), int(LocoSim::BrakeSource::Air));
    m_brakeSource->addItem(tr("by the VCC's @biu_cmd"), int(LocoSim::BrakeSource::VccLog));
    m_brakeSource->setToolTip(tr("BIU's command: A9 / SA9 read as NB, FSB, EB or LE, that brake's rate at once "
                                 "(or over the build-up time on Settings), as LocoTcasSimulator brakes.\n"
                                 "Bench's air: the rate follows BC and BP as they move, so the bench's plant "
                                 "faults (slow, leak, stuck) change the braking.\n"
                                 "VCC's @biu_cmd: the command as the VCC copies it to the log (a real BIU on CAN, "
                                 "no bench): the same brakes as the BIU's command, from the live @biu_cmd lines."));
    brakeRow->addWidget(m_brakeSource);
    brakeRow->addSpacing(8);
    m_brakeState = labelNamed(QString(), "mono", loco);
    m_brakeState->setObjectName(QStringLiteral("locoSimBrakeState"));
    m_brakeState->setTextInteractionFlags(Qt::TextSelectableByMouse);
    brakeRow->addWidget(m_brakeState, 1);
    controls->addLayout(brakeRow, 3, 1);
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
    connect(m_brakeFollow, &QCheckBox::toggled, this, [this]() { applyBrake(); showMotion(); });
    connect(m_brakeSource, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { applyBrake(); showMotion(); });
    for (int sensor = 0; sensor < 2; ++sensor) {
        connect(m_sensorOn[sensor], &QCheckBox::toggled, this, pull);
        for (int channel = 0; channel < 2; ++channel)
            connect(m_channel[sensor][channel], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, pull);
    }
    return page;
}

// ---- RFID ------------------------------------------------------------------------------

QWidget *LocoSimWindow::buildRfidTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(6);   // session 400: room for Linux's taller fonts under 700 px
    // ---- the route: one line ----------------------------------------------------
    auto *bar = new QHBoxLayout;
    bar->setSpacing(8);
    m_routeFileLabel = labelNamed(tr("No route open"), "mono", page);
    m_routeFileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bar->addWidget(m_routeFileLabel);
    auto *library = new QPushButton(tr("Library\u2026"), page);
    library->setToolTip(tr("A route from the RFID Tag Builder's scenario library"));
    bar->addWidget(library);
    auto *openFile = new QPushButton(tr("Open\u2026"), page);
    openFile->setToolTip(tr("A route file: the Tag Builder's .tagroute.xml, a route.xml or a Configuration1.xml"));
    bar->addWidget(openFile);
    m_fromBuilder = new QPushButton(tr("Tag Builder"), page);
    m_fromBuilder->setObjectName(QStringLiteral("locoSimFromBuilder"));
    m_fromBuilder->setToolTip(tr("The route open in the RFID Tag Builder, as it is now, saved or not: "
                                 "each edit there comes here, the loco staying where it is"));
    bar->addWidget(m_fromBuilder);
    bar->addSpacing(12);
    bar->addWidget(labelNamed(tr("Route"), "muted", page));
    m_routeBox = new QComboBox(page);
    m_routeBox->setObjectName(QStringLiteral("locoSimRoute"));
    m_routeBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_routeBox->setMinimumContentsLength(18);
    bar->addWidget(m_routeBox, 2);
    bar->addSpacing(12);
    bar->addWidget(labelNamed(tr("Start tag"), "muted", page));
    m_startTag = new QComboBox(page);
    m_startTag->setObjectName(QStringLiteral("locoSimStartTag"));
    m_startTag->setToolTip(tr("The tag the loco starts at: its abs_loc. Clicking a tag on the whole route picks it too."));
    m_startTag->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_startTag->setMinimumContentsLength(12);
    bar->addWidget(m_startTag, 1);
    auto *back = new QPushButton(tr("Back to start"), page);
    back->setToolTip(tr("Put the loco at the start tag again; every tag can be sent again"));
    bar->addWidget(back);
    layout->addLayout(bar);

    // ---- the track: the whole route, and near the loco; the readers beside it -----------
    auto *top = new QHBoxLayout;
    top->setSpacing(12);
    QVBoxLayout *trackBody = nullptr;
    QFrame *track = card(tr("Whole route"), page, &trackBody);
    QFrame *whole = track;
    QVBoxLayout *wholeBody = trackBody;
    m_wholeStrip = new RouteStrip(whole);
    m_wholeStrip->setObjectName(QStringLiteral("locoSimWholeRoute"));
    m_wholeStrip->setToolTip(tr("The route as the Tag Builder draws it, the loco under it. Click a tag to start there."));
    wholeBody->addWidget(m_wholeStrip);
    QVBoxLayout *followBody = trackBody;
    QFrame *follow = track;
    auto *followTitle = labelNamed(tr("Near the loco (%1 m)").arg(int(kFollowMetres)), "cardTitle", follow);
    followBody->addWidget(followTitle);
    m_followStrip = new RouteStrip(follow);
    m_followStrip->setObjectName(QStringLiteral("locoSimFollow"));
    m_followStrip->setToolTip(tr("The stretch around the loco. Filled: the loco. Dashed: where speed sensor 1's pulses put "
                                 "it. Under each tag: the readers that sent it. Shaded: reader 2's 20-24 m window."));
    followBody->addWidget(m_followStrip);
    m_routeWhere = labelNamed(QString(), "bigValue", follow);
    m_routeWhere->setWordWrap(true);
    followBody->addWidget(m_routeWhere);
    m_pulseWhere = labelNamed(QString(), "muted", follow);
    m_pulseWhere->setWordWrap(true);
    followBody->addWidget(m_pulseWhere);
    followBody->addStretch(1);
    top->addWidget(follow, 3);

    // ---- the readers and their ports ---------------------------------------------
    // Session 400: the loco's speed, read and set without the Drive tab (its
    // own card above the readers: beside the track, where there is height).
    auto *speedRow = new QHBoxLayout;
    speedRow->setSpacing(6);
    m_rfidSpeedValue = labelNamed(QString(), "bigValue", page);
    m_rfidSpeedValue->setObjectName(QStringLiteral("locoSimRfidSpeedValue"));
    m_rfidSpeedValue->setAccessibleName(tr("Speed"));
    for (int step : { -10, -5 }) {
        auto *button = new QToolButton(page);
        button->setText(QString::number(step));
        button->setToolTip(tr("%1 km/h now").arg(step));
        connect(button, &QToolButton::clicked, this, [this, step]() {
            m_motion.setSpeed(m_motion.speed() + step);
            showMotion();
        });
        speedRow->addWidget(button);
    }
    m_rfidSpeed = kmhSpin(page, 250);
    m_rfidSpeed->setObjectName(QStringLiteral("locoSimRfidSpeed"));
    m_rfidSpeed->setToolTip(tr("The loco's speed now, the same as the Drive tab's"));
    speedRow->addWidget(m_rfidSpeed);
    for (int step : { 5, 10 }) {
        auto *button = new QToolButton(page);
        button->setText(QStringLiteral("+%1").arg(step));
        button->setToolTip(tr("+%1 km/h now").arg(step));
        connect(button, &QToolButton::clicked, this, [this, step]() {
            m_motion.setSpeed(m_motion.speed() + step);
            showMotion();
        });
        speedRow->addWidget(button);
    }
    speedRow->addStretch(1);
    m_rfidBrake = labelNamed(QString(), "muted", page);
    m_rfidBrake->setObjectName(QStringLiteral("locoSimRfidBrake"));
    connect(m_rfidSpeed, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double kmh) {
        m_motion.setSpeed(kmh);
        showMotion();
    });
    auto *rightColumn = new QVBoxLayout;
    rightColumn->setSpacing(12);
    QVBoxLayout *speedBody = nullptr;
    QFrame *speedCard = card(tr("Run"), page, &speedBody);
    auto *speedHead = new QHBoxLayout;
    speedHead->addWidget(m_rfidSpeedValue);
    speedHead->addStretch(1);
    speedHead->addWidget(m_rfidBrake);
    speedBody->addLayout(speedHead);
    speedBody->addLayout(speedRow);
    // Session 406: what the loco does at the route's end.
    auto *chainRow = new QHBoxLayout;
    chainRow->setSpacing(6);
    chainRow->addWidget(labelNamed(tr("Then"), "muted", speedCard));
    m_chainBox = new QComboBox(speedCard);
    m_chainBox->setObjectName(QStringLiteral("locoSimChain"));
    m_chainBox->addItem(tr("stop at the end"), int(Chain::Stop));
    m_chainBox->addItem(tr("the next route"), int(Chain::Auto));
    m_chainBox->addItem(tr("routes in order"), int(Chain::List));
    m_chainBox->setToolTip(tr("At the route's end: stop sending; run on into the route whose first tag is nearest "
                              "ahead (same direction, this file or folder); or the routes set with Order\u2026"));
    chainRow->addWidget(m_chainBox);
    m_chainOrder = new QPushButton(tr("Order\u2026"), speedCard);
    m_chainOrder->setToolTip(tr("Pick the routes to run, in order"));
    chainRow->addWidget(m_chainOrder);
    m_chainNoteLabel = labelNamed(QString(), "muted", speedCard);
    m_chainNoteLabel->setObjectName(QStringLiteral("locoSimChainNote"));
    m_chainNoteLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);   // a long name must not widen the card
    chainRow->addWidget(m_chainNoteLabel, 1);
    speedBody->addLayout(chainRow);
    connect(m_chainBox, QOverload<int>::of(&QComboBox::activated), this,
            [this]() { setChain(Chain(m_chainBox->currentData().toInt())); });
    connect(m_chainOrder, &QPushButton::clicked, this, &LocoSimWindow::editChainOrder);
    rightColumn->addWidget(speedCard);
    QVBoxLayout *readerBody = nullptr;
    QFrame *readers = card(tr("Readers"), page, &readerBody);
    auto *readerGrid = new QGridLayout;
    readerGrid->setHorizontalSpacing(10);
    for (int reader = 0; reader < 2; ++reader) {
        m_readerOn[reader] = new QCheckBox(tr("Reader %1").arg(reader + 1), readers);
        m_readerOn[reader]->setObjectName(QStringLiteral("locoSimReader%1").arg(reader + 1));
        m_readerOn[reader]->setToolTip(reader == 0 ? tr("Sends each tag as the loco reaches it")
                                                   : tr("Sends the main tag again 20-24 m past it, then its duplicate"));
        // Session 409: OK / Fail beside it, as the simulator's.
        auto *cell = new QHBoxLayout;
        cell->setSpacing(6);
        cell->addWidget(m_readerOn[reader]);
        QWidget *health = SimUi::segmented({ tr("OK"), tr("Fail") }, &m_readerHealth[reader], readers);
        health->setObjectName(QStringLiteral("locoSimReader%1Health").arg(reader + 1));
        health->setToolTip(tr("Its health frame says OK (1) or fail (2), every %1 s; at Fail it sends no tags")
                               .arg(LocoSim::kHealthPeriodMs[reader] / 1000));
        m_readerHealth[reader]->button(0)->setChecked(true);
        cell->addWidget(health);
        cell->addStretch(1);
        readerGrid->addLayout(cell, 0, reader);
        connect(m_readerHealth[reader], &QButtonGroup::idClicked, this,
                [this, reader](int id) { setReaderHealth(reader + 1, id == 0); });
    }
    readerGrid->addWidget(labelNamed(tr("Reader 2 on"), "muted", readers), 1, 0);
    readerGrid->addWidget(SimUi::segmented({ tr("Port A"), tr("Port B") }, &m_reader2Port, readers), 1, 1);
    for (int which = 0; which < 2; ++which) {
        auto *row = new QHBoxLayout;
        m_tagPort[which] = new QComboBox(readers);
        m_tagPort[which]->setEditable(true);
        m_tagPort[which]->setObjectName(QStringLiteral("locoSimTagPort%1").arg(QChar(QLatin1Char('A' + which))));
        m_tagPort[which]->setMinimumContentsLength(12);
        row->addWidget(m_tagPort[which], 1);
        m_tagPortOpen[which] = new QPushButton(tr("Open"), readers);
        row->addWidget(m_tagPortOpen[which]);
        readerGrid->addWidget(labelNamed(which == 0 ? tr("Port A") : tr("Port B"), "muted", readers), 2 + 2 * which, 0);
        readerGrid->addLayout(row, 2 + 2 * which, 1);
        m_tagPortState[which] = labelNamed(QString(), "muted", readers);
        readerGrid->addWidget(m_tagPortState[which], 3 + 2 * which, 1);
    }
    readerGrid->setColumnStretch(1, 1);
    readerBody->addLayout(readerGrid);
    // The frames, said on the card's tooltip rather than under it (session 409: its height on Linux).
    readers->setToolTip(tr("Each tag goes out as LocoTcasSimulator's 22-byte frame: AA AA, length 22, reader, the 16 tag "
                           "bytes, BB BB, at %1 8N1. Each reader's health: AA AA 07, reader, 1 OK / 2 fail, BB BB, every "
                           "1 s (reader 1) / 2 s (reader 2) while it is on.").arg(LocoSim::kTagBaud));
    readerBody->addStretch(1);
    rightColumn->addWidget(readers, 1);
    top->addLayout(rightColumn, 2);
    layout->addLayout(top);

    // ---- the tags --------------------------------------------------------------
    m_tagsToggle = new QToolButton(page);
    m_tagsToggle->setObjectName(QStringLiteral("locoSimTagsToggle"));
    m_tagsToggle->setCheckable(true);
    m_tagsToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_tagsToggle->setArrowType(Qt::RightArrow);
    m_tagsToggle->setText(tr("Tags"));
    m_tagsToggle->setToolTip(tr("The route's rows: skip a tag, see when each reader sent it"));
    m_tagsToggle->setAutoRaise(true);
    // Session 410: a tag by hand, beside the table's toggle (no height of its own).
    auto *tagsRow = new QHBoxLayout;
    tagsRow->addWidget(m_tagsToggle);
    tagsRow->addStretch(1);
    auto *sendOne = new QToolButton(page);
    sendOne->setObjectName(QStringLiteral("locoSimSendTag"));
    sendOne->setText(tr("Send a tag\u2026"));
    sendOne->setAutoRaise(true);
    sendOne->setToolTip(tr("One tag, now, on reader 1 and / or reader 2, from its page_x and page_y "
                           "(the simulator's SPAD scenario RFID interface)"));
    connect(sendOne, &QToolButton::clicked, this, [this]() { openTagSend(); });
    tagsRow->addWidget(sendOne);
    layout->addLayout(tagsRow);
    m_tags = new QTableWidget(0, TagColumns, page);
    m_tags->setObjectName(QStringLiteral("locoSimTags"));
    m_tags->setHorizontalHeaderLabels({ QString(), tr("Tag"), tr("Type"), tr("abs_loc"), tr("Next"), tr("Skip"),
                                        tr("Reader 1"), tr("Reader 2"), tr("Fault"), tr("On"), tr("Delay") });
    m_tags->verticalHeader()->setVisible(false);
    m_tags->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tags->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tags->horizontalHeader()->setStretchLastSection(true);
    m_tags->setAlternatingRowColors(true);
    m_tags->setToolTip(tr("The route's rows as the simulator reads them. Skip: the tag is not sent "
                          "(the simulator then keeps the row before it)."));
    layout->addWidget(m_tags, 1);
    m_tags->setVisible(false);
    layout->addStretch(0);
    connect(m_tagsToggle, &QToolButton::toggled, this, [this](bool open) {
        m_tags->setVisible(open);
        m_tagsToggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });
    connect(m_wholeStrip, &RouteStrip::rowClicked, this, [this](int row) { setStartRow(row); });
    connect(m_tags, &QTableWidget::itemSelectionChanged, this, [this]() {
        const int row = m_tags->currentRow();
        if (m_selectingTag || row < 0 || !m_tags->item(row, ColTag)) return;
        emit routeTagSelected(m_tags->item(row, ColTag)->text());
    });

    // ---- wiring ----------------------------------------------------------------
    connect(library, &QPushButton::clicked, this, [this]() {
        TagLibraryDialog dialog(this);
        if (dialog.exec() != QDialog::Accepted || dialog.chosen().file.isEmpty()) return;
        if (loadRouteFile(dialog.chosen().file)) setRoute(dialog.chosen().route);
    });
    connect(openFile, &QPushButton::clicked, this, [this]() {
        const QString start = m_routeFile.startsWith(QLatin1Char(':')) ? QString() : QFileInfo(m_routeFile).absolutePath();
        const QString path = QFileDialog::getOpenFileName(this, tr("Open a tag route"), start,
                                                          tr("Tag routes (*.xml);;All files (*)"));
        if (!path.isEmpty() && loadRouteFile(path)) setRoute(0);
    });
    connect(m_fromBuilder, &QPushButton::clicked, this, &LocoSimWindow::chooseBuilder);
    connect(m_routeBox, QOverload<int>::of(&QComboBox::activated), this, [this](int index) { setRoute(index); });
    connect(m_startTag, QOverload<int>::of(&QComboBox::activated), this, [this](int row) { setStartRow(row); });
    connect(back, &QPushButton::clicked, this, [this]() { placeAtStart(); });
    for (int reader = 0; reader < 2; ++reader)
        connect(m_readerOn[reader], &QCheckBox::toggled, this, [this, reader](bool on) {
            if (m_readers) m_readers->setReader(reader + 1, readerSends(reader + 1));
            if (on) resetHealthClock(reader + 1);
        });
    connect(m_reader2Port, &QButtonGroup::idClicked, this, [this]() { refreshTagPortState(); });
    connect(m_tags, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (item->column() != ColSkip) return;
        if (m_readers) m_readers->setMissing(skippedTags());
        int &state = m_tagStates[item->row()];
        if (item->checkState() == Qt::Checked) state |= RouteStrip::Skipped;
        else state &= ~RouteStrip::Skipped;
        showRoutePosition();
    });
    for (int which = 0; which < 2; ++which)
        connect(m_tagPortOpen[which], &QPushButton::clicked, this, [this, which]() { openOrCloseTagPort(which); });
    refreshTagPorts();
    return page;
}

bool LocoSimWindow::loadRouteFile(const QString &path)
{
    QString why;
    QStringList notes;
    const QVector<RfidTag::Route> routes = RfidTag::readFile(path, &why, &notes);
    if (routes.isEmpty()) {
        m_status->fail(tr("No route in %1: %2").arg(QFileInfo(path).fileName(), why));
        return false;
    }
    stopFollowingBuilder();
    unfollowLayout();
    m_routes = routes;
    m_routeFile = path;
    m_routeIndex = -1;
    m_routeFileLabel->setText(QFileInfo(path).fileName());
    m_routeFileLabel->setToolTip(QDir::toNativeSeparators(path));
    fillRouteBox();
    if (!notes.isEmpty()) m_status->warn(tr("%1: %2").arg(QFileInfo(path).fileName(), notes.join(QStringLiteral("; "))));
    return true;
}

void LocoSimWindow::fillRouteBox()
{
    const QSignalBlocker block(m_routeBox);
    m_routeBox->clear();
    for (const RfidTag::Route &r : m_routes) {
        const QString dir = r.dir == RfidTag::DirNominal ? tr("dir 1") : r.dir == RfidTag::DirReverse ? tr("dir 2")
                                                                                                       : tr("no direction");
        m_routeBox->addItem(tr("%1 · %2 tags · %3").arg(r.name).arg(r.tags.size()).arg(dir));
    }
}

void LocoSimWindow::setRoute(int index)
{
    if (index < 0 || index >= m_routes.size()) return;
    m_faults.clear();   // session 405: a route picked afresh starts without faults
    m_chainAt = -1;     // session 406: its place in the order, if it has one
    for (int i = 0; i < m_chainList.size(); ++i)
        if (m_chainList.at(i).file == m_routeFile && m_chainList.at(i).index == index) m_chainAt = i;
    showRoute(index);
    placeAtStart();
}

void LocoSimWindow::showRoute(int index)
{
    if (index < 0 || index >= m_routes.size()) return;
    m_routeIndex = index;
    {
        const QSignalBlocker block(m_routeBox);
        m_routeBox->setCurrentIndex(index);
    }
    m_ownLoc.clear();   // session 411: where the layout draws the loco
    if (m_routes.at(index).dir == RfidTag::DirNominal || m_routes.at(index).dir == RfidTag::DirReverse)
        for (const RfidTag::RouteRow &row : RfidTag::routeRows(m_routes.at(index).tags, m_routes.at(index).dir))
            m_ownLoc << double(row.ownLoc);
    fillTagTable();
    m_wholeStrip->setRoute(m_routes.at(index));
    m_followStrip->setRoute(m_routes.at(index));
    m_tagsToggle->setText(tr("Tags (%1)").arg(m_routes.at(index).tags.size()));
    refreshChainNote();
    if (m_tagSend) m_tagSend->refreshRoute();
    {
        const QSignalBlocker block(m_startTag);
        m_startTag->clear();
        for (int row = 0; row < m_tags->rowCount(); ++row)
            m_startTag->addItem(tr("%1 at %2 m").arg(m_tags->item(row, ColTag)->text(), m_tags->item(row, ColLoc)->text()));
    }
}

// ---- session 402: the Tag Builder's route, live --------------------------------------

void LocoSimWindow::chooseBuilder()
{
    QList<TagBuilderWindow *> builders;
    if (m_builders)
        for (TagBuilderWindow *builder : m_builders())
            if (!builder->isClosed()) builders << builder;   // minimised (hidden in the dock) still counts; closed does not
    if (builders.isEmpty()) {
        m_status->warn(tr("No RFID Tag Builder is open: open one (Tools \u25B8 RFID Tag Builder\u2026), then press Tag Builder."));
        return;
    }
    if (builders.size() == 1) {
        followBuilder(builders.first());
        return;
    }
    QMenu menu(this);
    for (TagBuilderWindow *builder : builders) {
        const QString name = builder->route().name.isEmpty() ? tr("(unnamed route)") : builder->route().name;
        menu.addAction(tr("%1 \u00B7 %2 tags").arg(name).arg(builder->route().tags.size()), this,
                       [this, builder]() { followBuilder(builder); });
    }
    menu.exec(m_fromBuilder->mapToGlobal(QPoint(0, m_fromBuilder->height())));
}

bool LocoSimWindow::followBuilder(TagBuilderWindow *builder)
{
    if (!builder) {
        m_status->warn(tr("No RFID Tag Builder to follow."));
        return false;
    }
    stopFollowingBuilder();
    unfollowLayout();
    m_builder = builder;
    connect(builder, &TagBuilderWindow::routeChanged, this, &LocoSimWindow::builderRouteChanged);
    connect(builder, &QObject::destroyed, this, [this]() {
        m_routeFileLabel->setText(tr("Tag Builder (closed)"));
        m_status->warn(tr("The Tag Builder closed: its last route stays here."));
    });
    m_routeFile.clear();
    m_routes = { builder->route() };
    m_routeIndex = -1;
    m_routeFileLabel->setText(tr("Tag Builder"));
    m_routeFileLabel->setToolTip(tr("The route open in the RFID Tag Builder, followed as it is edited"));
    fillRouteBox();
    setRoute(0);
    m_status->ok(tr("Following the Tag Builder's route %1: each edit there comes here.").arg(builder->route().name));
    return true;
}

void LocoSimWindow::stopFollowingBuilder()
{
    if (m_builder) disconnect(m_builder, nullptr, this, nullptr);
    m_builder.clear();
}

// An edit in the builder: the route as it is now, the loco where it was.
void LocoSimWindow::builderRouteChanged()
{
    if (m_builder) routeEdited(m_builder->route(), tr("the Tag Builder"));
}

void LocoSimWindow::routeEdited(const RfidTag::Route &edited, const QString &source)
{
    const QStringList skipped = skippedTags();
    const int startRow = m_startTag->currentIndex();
    const QString startName = startRow >= 0 && startRow < m_tags->rowCount() ? m_tags->item(startRow, ColTag)->text() : QString();
    m_routes = { edited };
    fillRouteBox();
    showRoute(0);
    for (const QString &name : skipped) setSkipped(name, true);
    for (int row = 0; row < m_tags->rowCount(); ++row)
        if (m_tags->item(row, ColTag)->text() == startName) {
            const QSignalBlocker block(m_startTag);
            m_startTag->setCurrentIndex(row);
        }
    const RfidTag::Route &route = m_routes.first();
    if (!m_readers || (route.dir != RfidTag::DirNominal && route.dir != RfidTag::DirReverse)) {
        placeAtStart();
        return;
    }
    // The readers start again at the loco's place: what it has passed is not
    // sent again; the tags ahead go out as they are now.
    m_readers.reset(new SimPreview::Readers(SimPreview::rowsOf(route)));
    m_readers->setReader(1, readerSends(1));
    m_readers->setReader(2, readerSends(2));
    m_readers->setMissing(skippedTags());
    QVector<SimPreview::Readers::Send> passed;
    const int row = m_readers->step(m_routeD, 0, &passed);
    if (row < 0) {
        m_status->warn(tr("The loco at %1 m is not on the edited route: back to the start tag.").arg(m_routeD, 0, 'f', 1));
        placeAtStart();
        return;
    }
    m_routeRow = row;
    m_routeEnded = false;
    m_tagStates.clear();
    for (const QString &name : skippedTags())
        for (int r = 0; r < m_tags->rowCount(); ++r)
            if (m_tags->item(r, ColTag)->text() == name) m_tagStates[r] = RouteStrip::Skipped;
    markFaultedRows();
    showRoutePosition();
    m_status->state(tr("Route updated from %1: the loco stays at %2 m.").arg(source).arg(m_routeD, 0, 'f', 1));
}

// ---- session 411: a line of a Station Layout, live ---------------------------------------

bool LocoSimWindow::followLayout(StationLayoutWindow *layout, const QString &line, int dir)
{
    if (!layout) {
        m_status->warn(tr("No Station Layout to follow."));
        return false;
    }
    const RfidTag::Route route = layout->lineAsSimRoute(line, dir);
    if (route.tags.isEmpty()) {
        m_status->fail(tr("Line %1 of the Station Layout has no tag that decodes.").arg(line));
        return false;
    }
    stopFollowingBuilder();
    unfollowLayout();
    m_layout = layout;
    m_layoutLine = line;
    m_layoutDir = dir;
    connect(layout, &StationLayoutWindow::stationChanged, this, &LocoSimWindow::layoutRouteChanged);
    connect(layout, &QObject::destroyed, this, [this]() {
        m_routeFileLabel->setText(tr("Station Layout (closed)"));
        m_status->warn(tr("The Station Layout closed: its last route stays here."));
    });
    m_routeFile.clear();
    m_routes = { route };
    m_routeIndex = -1;
    m_routeFileLabel->setText(tr("Station Layout \u00B7 line %1").arg(line));
    m_routeFileLabel->setToolTip(tr("Line %1 of the open Station Layout, followed as it is edited").arg(line));
    fillRouteBox();
    setRoute(0);
    m_status->ok(tr("Following line %1 of the Station Layout, dir %2: each edit there comes here.").arg(line).arg(dir));
    return true;
}

void LocoSimWindow::unfollowLayout()
{
    if (m_layout) {
        disconnect(m_layout, nullptr, this, nullptr);
        m_layout->clearSimulator();
    }
    m_layout.clear();
    m_layoutLine.clear();
}

// An edit in the layout. One elsewhere (another line, a text) leaves this
// line's route as it was: nothing changes here.
void LocoSimWindow::layoutRouteChanged()
{
    if (!m_layout) return;
    const RfidTag::Route route = m_layout->lineAsSimRoute(m_layoutLine, m_layoutDir);
    if (route.tags.isEmpty()) {
        m_status->warn(tr("Line %1 is no longer in the Station Layout (or none of its tags decodes): its last route stays "
                          "here.").arg(m_layoutLine));
        unfollowLayout();
        return;
    }
    auto same = [](const RfidTag::Route &a, const RfidTag::Route &b) {
        if (a.name != b.name || a.dir != b.dir || a.tags.size() != b.tags.size() || a.signalList.size() != b.signalList.size())
            return false;
        for (int i = 0; i < a.tags.size(); ++i)
            if (a.tags.at(i).name != b.tags.at(i).name || a.tags.at(i).bytes != b.tags.at(i).bytes) return false;
        for (int i = 0; i < a.signalList.size(); ++i)
            if (a.signalList.at(i).footTag != b.signalList.at(i).footTag || a.signalList.at(i).name != b.signalList.at(i).name)
                return false;
        return true;
    };
    if (m_routes.size() == 1 && m_routeIndex == 0 && same(m_routes.first(), route)) {
        showOnLayout();   // the layout redrew: the loco back on it
        return;
    }
    routeEdited(route, tr("the Station Layout"));
}

double LocoSimWindow::layoutLocation() const
{
    if (!m_readers) return std::numeric_limits<double>::quiet_NaN();
    const QVector<SimPreview::Row> &rows = m_readers->rows();
    if (rows.isEmpty() || m_ownLoc.size() != rows.size()) return m_routeD;
    // Between the row the loco is in and the next, in proportion.
    const int r = qBound(0, m_routeRow, rows.size() - 1);
    const double a = rows.at(r).absLoc;
    if (r + 1 < rows.size() && rows.at(r + 1).absLoc != a)
        return m_ownLoc.at(r) + (m_routeD - a) / (rows.at(r + 1).absLoc - a) * (m_ownLoc.at(r + 1) - m_ownLoc.at(r));
    return m_ownLoc.at(r) + (m_routeD - a);
}

void LocoSimWindow::showOnLayout()
{
    if (!m_layout) return;
    if (m_routeIndex < 0 || !m_readers) {
        m_layout->showSimulator(m_layoutLine, std::numeric_limits<double>::quiet_NaN(), m_layoutDir == RfidTag::DirReverse ? -1 : 1,
                                {}, m_routeWhere ? m_routeWhere->text() : QString());
        return;
    }
    const RfidTag::Route &route = m_routes.at(m_routeIndex);
    QHash<QString, int> byName;
    for (auto it = m_tagStates.cbegin(); it != m_tagStates.cend(); ++it)
        if (it.key() >= 0 && it.key() < route.tags.size() && it.value()) byName[route.tags.at(it.key()).name] |= it.value();
    m_layout->showSimulator(m_layoutLine, layoutLocation(), route.dir == RfidTag::DirReverse ? -1 : 1, byName,
                            m_routeWhere->text());
}

void LocoSimWindow::setStartRow(int row)
{
    if (row < 0 || row >= m_startTag->count()) return;
    {
        const QSignalBlocker block(m_startTag);
        m_startTag->setCurrentIndex(row);
    }
    placeAtStart();
}

void LocoSimWindow::fillTagTable()
{
    const QSignalBlocker block(m_tags);
    m_tags->setRowCount(0);
    if (m_routeIndex < 0) return;
    const RfidTag::Route &route = m_routes.at(m_routeIndex);
    // routeRows() needs the direction; without one the rows show the tags only.
    const QVector<SimPreview::Row> rows = route.dir == RfidTag::DirUnset ? QVector<SimPreview::Row>()
                                                                         : SimPreview::rowsOf(route);
    m_tags->setRowCount(route.tags.size());
    for (int i = 0; i < route.tags.size(); ++i) {
        const bool known = i < rows.size();
        const QStringList cells = {
            QString(),
            known ? rows.at(i).id : route.tags.at(i).name,
            known ? QString::number(rows.at(i).tagType) : QString(),
            known ? QString::number(rows.at(i).absLoc, 'f', 0) : QString(),
            known ? QString::number(rows.at(i).nextAbsLoc, 'f', 0) : QString(),
            QString(), QString(), QString(), QString(), QString(), QString(),
        };
        for (int column = 0; column < TagColumns; ++column) {
            auto *item = new QTableWidgetItem(cells.at(column));
            if (column == ColType || column == ColLoc || column == ColNext)
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (column == ColSkip) {
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(Qt::Unchecked);
                item->setToolTip(tr("Skip this tag: it is not sent"));
            }
            m_tags->setItem(i, column, item);
        }
    }
    // Session 405: the fault, which reader it is on, how late.
    for (int i = 0; i < route.tags.size(); ++i) {
        const QString name = m_tags->item(i, ColTag)->text();
        const TagFaultSetting f = m_faults.value(name);
        auto *kind = new QComboBox(m_tags);
        kind->setObjectName(QStringLiteral("locoSimFault_%1").arg(name));
        kind->addItem(tr("\u2014"), int(LocoSim::TagFault::None));
        kind->addItem(tr("Late"), int(LocoSim::TagFault::Late));
        kind->addItem(tr("Sent twice"), int(LocoSim::TagFault::Twice));
        kind->addItem(tr("Bad CRC"), int(LocoSim::TagFault::BadCrc));
        kind->setCurrentIndex(kind->findData(int(f.kind)));
        kind->setToolTip(tr("What goes wrong with this tag's frame, each time the loco passes it"));
        auto *on = new QComboBox(m_tags);
        on->addItem(tr("1 + 2"), 3);
        on->addItem(tr("1"), 1);
        on->addItem(tr("2"), 2);
        on->setCurrentIndex(on->findData(f.readers));
        on->setToolTip(tr("The reader(s) whose frame the fault is on"));
        auto *delay = new QSpinBox(m_tags);
        delay->setRange(10, 60000);
        delay->setSingleStep(100);
        delay->setSuffix(tr(" ms"));
        delay->setValue(f.delayMs);
        delay->setToolTip(tr("Late: sent this long after the reader would have sent it"));
        m_tags->setCellWidget(i, ColFault, kind);
        m_tags->setCellWidget(i, ColFaultOn, on);
        m_tags->setCellWidget(i, ColDelay, delay);
        const auto changed = [this, name, kind, on, delay]() {
            setTagFault(name, LocoSim::TagFault(kind->currentData().toInt()), on->currentData().toInt(), delay->value());
        };
        connect(kind, QOverload<int>::of(&QComboBox::currentIndexChanged), this, changed);
        connect(on, QOverload<int>::of(&QComboBox::currentIndexChanged), this, changed);
        connect(delay, QOverload<int>::of(&QSpinBox::valueChanged), this, changed);
        showFaultRow(i);
    }
    m_tags->resizeColumnsToContents();
    // The reader columns fill in as the loco runs: share the room.
    m_tags->horizontalHeader()->setSectionResizeMode(ColReader1, QHeaderView::Stretch);
    m_tags->horizontalHeader()->setSectionResizeMode(ColReader2, QHeaderView::Stretch);
}

QStringList LocoSimWindow::skippedTags() const
{
    QStringList names;
    for (int row = 0; row < m_tags->rowCount(); ++row)
        if (m_tags->item(row, ColSkip)->checkState() == Qt::Checked) names << m_tags->item(row, ColTag)->text();
    return names;
}

void LocoSimWindow::setSkipped(const QString &tag, bool skip)
{
    for (int row = 0; row < m_tags->rowCount(); ++row)
        if (m_tags->item(row, ColTag)->text().compare(tag, Qt::CaseInsensitive) == 0)
            m_tags->item(row, ColSkip)->setCheckState(skip ? Qt::Checked : Qt::Unchecked);
}

// ---- session 405: tag faults ----------------------------------------------------------

void LocoSimWindow::setTagFault(const QString &tag, LocoSim::TagFault kind, int readers, int delayMs)
{
    TagFaultSetting f;
    f.kind = kind;
    f.readers = qBound(1, readers, 3);
    f.delayMs = qMax(10, delayMs);
    if (kind == LocoSim::TagFault::None) m_faults.remove(tag);
    else m_faults.insert(tag, f);
    for (int row = 0; row < m_tags->rowCount(); ++row) {
        if (m_tags->item(row, ColTag)->text() != tag) continue;
        // The row's controls say it too (set from code, or by the operator).
        if (auto *box = qobject_cast<QComboBox *>(m_tags->cellWidget(row, ColFault))) {
            const QSignalBlocker block(box);
            box->setCurrentIndex(box->findData(int(kind)));
        }
        if (auto *box = qobject_cast<QComboBox *>(m_tags->cellWidget(row, ColFaultOn))) {
            const QSignalBlocker block(box);
            box->setCurrentIndex(box->findData(f.readers));
        }
        if (auto *spin = qobject_cast<QSpinBox *>(m_tags->cellWidget(row, ColDelay))) {
            const QSignalBlocker block(spin);
            spin->setValue(f.delayMs);
        }
        showFaultRow(row);
    }
    markFaultedRows();
    showRoutePosition();
}

void LocoSimWindow::showFaultRow(int row)
{
    const TagFaultSetting f = m_faults.value(m_tags->item(row, ColTag)->text());
    const bool any = f.kind != LocoSim::TagFault::None;
    if (QWidget *w = m_tags->cellWidget(row, ColFaultOn)) w->setEnabled(any);
    if (QWidget *w = m_tags->cellWidget(row, ColDelay)) w->setEnabled(f.kind == LocoSim::TagFault::Late);
}

void LocoSimWindow::markFaultedRows()
{
    for (int row = 0; row < m_tags->rowCount(); ++row) {
        int &state = m_tagStates[row];
        if (m_faults.contains(m_tags->item(row, ColTag)->text())) state |= RouteStrip::Faulted;
        else state &= ~RouteStrip::Faulted;
        if (!state) m_tagStates.remove(row);
    }
}

QHash<int, QString> LocoSimWindow::faultLabels() const
{
    QHash<int, QString> labels;
    for (int row = 0; row < m_tags->rowCount(); ++row) {
        const TagFaultSetting f = m_faults.value(m_tags->item(row, ColTag)->text());
        switch (f.kind) {
        case LocoSim::TagFault::None:   break;
        case LocoSim::TagFault::Late:   labels.insert(row, tr("late")); break;
        case LocoSim::TagFault::Twice:  labels.insert(row, tr("\u00D72")); break;
        case LocoSim::TagFault::BadCrc: labels.insert(row, tr("crc")); break;
        }
    }
    return labels;
}

// What the readers send, with the tag's fault when it is on that reader.
void LocoSimWindow::sendWithFault(const SimPreview::Readers::Send &send)
{
    const QString name = send.row < m_tags->rowCount() ? m_tags->item(send.row, ColTag)->text() : QString();
    const TagFaultSetting f = m_faults.value(name);
    if (f.kind == LocoSim::TagFault::None || !(f.readers & (1 << (send.reader - 1)))) {
        sendTag(send);
        return;
    }
    switch (f.kind) {
    case LocoSim::TagFault::Late:
        m_late.append({ m_clock() + f.delayMs, send, tr("late by %1 ms").arg(f.delayMs) });
        if (QTableWidgetItem *cell = m_tags->item(send.row, send.reader == 1 ? ColReader1 : ColReader2))
            cell->setText(tr("due in %1 ms").arg(f.delayMs));
        break;
    case LocoSim::TagFault::Twice:
        sendTag(send, tr("sent twice (1st)"));
        sendTag(send, tr("sent twice (2nd)"));
        break;
    case LocoSim::TagFault::BadCrc: {
        SimPreview::Readers::Send bad = send;
        bad.tag = LocoSim::badCrcTag(send.tag);
        sendTag(bad, tr("bad CRC"));
        break;
    }
    case LocoSim::TagFault::None:
        break;
    }
}

void LocoSimWindow::flushLate()
{
    const qint64 now = m_clock();
    for (int i = 0; i < m_late.size();) {
        if (m_late.at(i).dueMs > now) { ++i; continue; }
        const Late late = m_late.takeAt(i);
        sendTag(late.send, late.note);
    }
}

// ---- session 407: the scenario --------------------------------------------------------

namespace {
enum StepColumn { StepWhen, StepAt, StepDo, StepOn, StepKind, StepValue, StepText, StepStatus, StepColumns };
}

QWidget *LocoSimWindow::buildScenarioTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(8);
    auto *bar = new QHBoxLayout;
    bar->addWidget(labelNamed(tr("Scenario"), "muted", page));
    m_scenarioName = new QLineEdit(page);
    m_scenarioName->setObjectName(QStringLiteral("locoSimScenarioName"));
    m_scenarioName->setPlaceholderText(tr("its name"));
    bar->addWidget(m_scenarioName, 1);
    auto *add = new QPushButton(tr("Add step"), page);
    auto *remove = new QPushButton(tr("Remove"), page);
    auto *up = new QToolButton(page);
    up->setArrowType(Qt::UpArrow);
    up->setToolTip(tr("Move the step up"));
    auto *down = new QToolButton(page);
    down->setArrowType(Qt::DownArrow);
    down->setToolTip(tr("Move the step down"));
    auto *load = new QPushButton(tr("Load\u2026"), page);
    auto *save = new QPushButton(tr("Save\u2026"), page);
    for (QWidget *w : { static_cast<QWidget *>(add), static_cast<QWidget *>(remove), static_cast<QWidget *>(up),
                        static_cast<QWidget *>(down), static_cast<QWidget *>(load), static_cast<QWidget *>(save) })
        bar->addWidget(w);
    bar->addSpacing(16);
    m_scenarioRun = new QPushButton(tr("Run"), page);
    m_scenarioRun->setObjectName(QStringLiteral("locoSimScenarioRun"));
    m_scenarioRun->setToolTip(tr("Start the steps' clock now (and the loco, if it is not sending)"));
    bar->addWidget(m_scenarioRun);
    layout->addLayout(bar);

    m_steps = new QTableWidget(0, StepColumns, page);
    m_steps->setObjectName(QStringLiteral("locoSimSteps"));
    m_steps->setHorizontalHeaderLabels({ tr("When"), tr("At (s, m or tag)"), tr("Do"), tr("On"), tr("Kind"), tr("Value"),
                                         tr("Text"), tr("Status") });
    m_steps->verticalHeader()->setVisible(true);
    m_steps->horizontalHeader()->setStretchLastSection(true);
    m_steps->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_steps->setToolTip(tr("On: the tag, the reader (1, 2) or the pressure (BP, BC, MR). Kind: a tag's fault (none, "
                           "late, twice, crc) or an air fault (none, stuck, slow, leak). Value: the number the action "
                           "takes. Text: a note, or a fault code as 0x1040."));
    layout->addWidget(m_steps, 1);
    m_scenarioState = labelNamed(tr("No scenario running."), "muted", page);
    m_scenarioState->setObjectName(QStringLiteral("locoSimScenarioState"));
    m_scenarioState->setWordWrap(true);
    layout->addWidget(m_scenarioState);

    connect(add, &QPushButton::clicked, this, [this]() { addStepRow(Scenario::Step()); });
    connect(remove, &QPushButton::clicked, this, [this]() {
        if (m_scenarioOn) return;
        const int row = m_steps->currentRow();
        if (row >= 0) m_steps->removeRow(row);
    });
    const auto move = [this](int delta) {
        if (m_scenarioOn) return;
        QString why;
        Scenario::Script s = scenario(&why);
        const int row = m_steps->currentRow(), to = row + delta;
        if (!why.isEmpty() || row < 0 || to < 0 || to >= s.steps.size()) return;
        std::swap(s.steps[row], s.steps[to]);
        setScenario(s);
        m_steps->selectRow(to);
    };
    connect(up, &QToolButton::clicked, this, [move]() { move(-1); });
    connect(down, &QToolButton::clicked, this, [move]() { move(1); });
    connect(load, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Load a scenario"), QString(),
                                                          tr("Scenarios (*.scenario.json);;All files (*)"));
        if (!path.isEmpty()) loadScenario(path);
    });
    connect(save, &QPushButton::clicked, this, [this]() {
        const QString name = m_scenarioName->text().trimmed().isEmpty() ? tr("scenario") : m_scenarioName->text().trimmed();
        const QString path = QFileDialog::getSaveFileName(this, tr("Save the scenario"), name + QStringLiteral(".scenario.json"),
                                                          tr("Scenarios (*.scenario.json)"));
        if (!path.isEmpty()) saveScenario(path);
    });
    connect(m_scenarioRun, &QPushButton::clicked, this, [this]() {
        if (m_scenarioOn) stopScenario();
        else runScenario();
    });
    return page;
}

void LocoSimWindow::addStepRow(const Scenario::Step &step)
{
    const int row = m_steps->rowCount();
    m_steps->insertRow(row);
    auto *when = new QComboBox(m_steps);
    when->addItems(Scenario::whenNames());
    when->setCurrentIndex(int(step.when));
    m_steps->setCellWidget(row, StepWhen, when);
    auto *action = new QComboBox(m_steps);
    action->addItems(Scenario::doNames());
    action->setCurrentIndex(int(step.action));
    m_steps->setCellWidget(row, StepDo, action);
    const auto num = [](double v) { return QString::number(v, 'g', 10); };
    const QStringList cells = { QString(), step.when == Scenario::When::Tag ? step.tag : num(step.at), QString(), step.on,
                                step.kind, num(step.value), step.text, QString() };
    for (int col = 0; col < StepColumns; ++col) {
        if (col == StepWhen || col == StepDo) continue;
        auto *item = new QTableWidgetItem(cells.at(col));
        if (col == StepStatus) item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        m_steps->setItem(row, col, item);
    }
}

void LocoSimWindow::setScenario(const Scenario::Script &script)
{
    if (m_scenarioOn) stopScenario();
    m_steps->setRowCount(0);
    m_scenarioName->setText(script.name);
    for (const Scenario::Step &s : script.steps) addStepRow(s);
    m_steps->resizeColumnsToContents();
}

Scenario::Script LocoSimWindow::scenario(QString *why) const
{
    Scenario::Script script;
    script.name = m_scenarioName->text().trimmed();
    if (why) why->clear();
    for (int row = 0; row < m_steps->rowCount(); ++row) {
        Scenario::Step s;
        s.when = Scenario::When(qobject_cast<QComboBox *>(m_steps->cellWidget(row, StepWhen))->currentIndex());
        s.action = Scenario::Do(qobject_cast<QComboBox *>(m_steps->cellWidget(row, StepDo))->currentIndex());
        const QString at = m_steps->item(row, StepAt)->text().trimmed();
        bool ok = true;
        if (s.when == Scenario::When::Tag) s.tag = at;
        else s.at = at.isEmpty() ? 0 : at.toDouble(&ok);
        if (!ok && why && why->isEmpty()) *why = tr("Step %1: \"%2\" is not a number of %3").arg(row + 1).arg(at)
                                                   .arg(s.when == Scenario::When::Time ? tr("seconds") : tr("metres"));
        s.on = m_steps->item(row, StepOn)->text().trimmed();
        s.kind = m_steps->item(row, StepKind)->text().trimmed().toLower();
        const QString value = m_steps->item(row, StepValue)->text().trimmed();
        s.value = value.isEmpty() ? 0 : value.toDouble(&ok);
        if (!ok && why && why->isEmpty()) *why = tr("Step %1: the value \"%2\" is not a number").arg(row + 1).arg(value);
        s.text = m_steps->item(row, StepText)->text().trimmed();
        if (s.action == Scenario::Do::AirFault) s.on = s.on.toUpper();
        script.steps.append(s);
    }
    QString rule;
    if (why && why->isEmpty() && !Scenario::check(script, &rule)) *why = rule;
    return script;
}

bool LocoSimWindow::loadScenario(const QString &path)
{
    QFile f(path);
    QString why;
    Scenario::Script script;
    if (!f.open(QIODevice::ReadOnly) || !Scenario::fromJson(f.readAll(), &script, &why)) {
        m_status->fail(tr("%1 not loaded: %2").arg(QFileInfo(path).fileName(), why.isEmpty() ? f.errorString() : why));
        return false;
    }
    setScenario(script);
    m_status->ok(tr("Scenario %1: %2 step(s).").arg(script.name).arg(script.steps.size()));
    return true;
}

bool LocoSimWindow::saveScenario(const QString &path)
{
    QString why;
    const Scenario::Script script = scenario(&why);
    if (!why.isEmpty()) {
        m_status->fail(tr("Not saved: %1").arg(why));
        return false;
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_status->fail(tr("Not saved: %1").arg(f.errorString()));
        return false;
    }
    f.write(Scenario::toJson(script));
    m_status->ok(tr("Saved: %1").arg(QDir::toNativeSeparators(path)));
    return true;
}

bool LocoSimWindow::runScenario()
{
    QString why;
    const Scenario::Script script = scenario(&why);
    if (!why.isEmpty() || script.steps.isEmpty()) {
        m_status->fail(why.isEmpty() ? tr("No step to run: add one.") : tr("Not run: %1").arg(why));
        return false;
    }
    m_script = script;
    m_stepDone = QVector<int>(script.steps.size(), 0);
    m_stepNote = QStringList();
    for (int i = 0; i < script.steps.size(); ++i) m_stepNote << QString();
    for (int row = 0; row < m_steps->rowCount(); ++row) m_steps->item(row, StepStatus)->setText(tr("waiting"));
    m_tagsSeen.clear();
    m_scenarioStartMs = m_clock();
    m_scenarioOn = true;
    m_steps->setEnabled(false);
    m_scenarioRun->setText(tr("Stop"));
    if (m_recorder && m_recorder->recording())
        m_recorder->mark(m_scenarioStartMs, tr("Scenario %1 started").arg(script.name.isEmpty() ? tr("(unnamed)") : script.name));
    if (!running()) start();
    scenarioTick();   // the steps at 0 s, now
    return true;
}

void LocoSimWindow::stopScenario()
{
    if (!m_scenarioOn) return;
    m_scenarioOn = false;
    m_steps->setEnabled(true);
    m_scenarioRun->setText(tr("Run"));
    int waiting = 0;
    for (int done : m_stepDone) waiting += done == 0 ? 1 : 0;
    m_scenarioState->setText(tr("Stopped: %1 of %2 step(s) still waiting.").arg(waiting).arg(m_stepDone.size()));
    if (m_recorder && m_recorder->recording()) m_recorder->mark(m_clock(), tr("Scenario stopped"));
}

QString LocoSimWindow::stepStatus(int step) const
{
    return step >= 0 && step < m_stepNote.size() ? m_stepNote.at(step) : QString();
}

bool LocoSimWindow::hasTag(const QString &name) const
{
    for (int row = 0; row < m_tags->rowCount(); ++row)
        if (m_tags->item(row, ColTag)->text().compare(name, Qt::CaseInsensitive) == 0) return true;
    return false;
}

// Each step whose trigger has come, once.
void LocoSimWindow::scenarioTick()
{
    if (!m_scenarioOn) return;
    const double seconds = (m_clock() - m_scenarioStartMs) / 1000.0;
    const bool onRoute = bool(m_readers);
    const double position = onRoute ? m_routeD : m_motion.position();
    const double sign = onRoute ? (m_routes.at(m_routeIndex).dir == RfidTag::DirReverse ? -1 : 1)
                                : (m_motion.direction() == LocoSim::Reverse ? -1 : 1);
    int waiting = 0;
    for (int i = 0; i < m_script.steps.size(); ++i) {
        if (m_stepDone.at(i) != 0) continue;
        const Scenario::Step &s = m_script.steps.at(i);
        bool due = false;
        switch (s.when) {
        case Scenario::When::Time:     due = seconds + 1e-9 >= s.at; break;
        case Scenario::When::Tag:      due = m_tagsSeen.contains(s.tag); break;
        case Scenario::When::Location: due = (position - s.at) * sign >= 0; break;
        }
        if (!due) { ++waiting; continue; }
        QString why;
        const bool done = doStep(s, &why);
        m_stepDone[i] = done ? 1 : 2;
        m_stepNote[i] = done ? tr("done at %1 s").arg(seconds, 0, 'f', 1) : tr("not done: %1").arg(why);
        m_steps->item(i, StepStatus)->setText(m_stepNote.at(i));
        const QString said = tr("Step %1: %2").arg(i + 1).arg(Scenario::describe(s));
        if (done) m_status->ok(said);
        else m_status->warn(tr("%1, not done: %2").arg(said, why));
        if (m_recorder && m_recorder->recording())
            m_recorder->mark(m_clock(), done ? said : tr("%1 (not done: %2)").arg(said, why));
    }
    m_scenarioState->setText(waiting ? tr("Running %1 s: %2 of %3 step(s) waiting.").arg(seconds, 0, 'f', 1).arg(waiting)
                                           .arg(m_script.steps.size())
                                     : tr("Every step has fired (%1 s).").arg(seconds, 0, 'f', 1));
}

bool LocoSimWindow::doStep(const Scenario::Step &s, QString *why)
{
    using Scenario::Do;
    if (Scenario::isBenchAction(s.action) && !m_bench) {
        *why = tr("the Brake Test Bench is not open");
        return false;
    }
    if ((s.action == Do::SkipTag || s.action == Do::TagFault) && !hasTag(s.on)) {
        *why = tr("no tag %1 on this route").arg(s.on);
        return false;
    }
    switch (s.action) {
    case Do::Speed:
        m_motion.setDrive(LocoSim::Motion::Drive::Hold);
        m_motion.setSpeed(s.value);
        break;
    case Do::Accelerate:
    case Do::Decelerate:
        if (s.value > 0) m_rate->setValue(s.value);
        m_motion.setRate(m_rate->value());
        m_motion.setDrive(s.action == Do::Accelerate ? LocoSim::Motion::Drive::Accelerate : LocoSim::Motion::Drive::Decelerate);
        break;
    case Do::Hold:
        m_motion.setDrive(LocoSim::Motion::Drive::Hold);
        break;
    case Do::SkipTag:
        setSkipped(s.on, s.value != 0);
        break;
    case Do::TagFault: {
        const LocoSim::TagFault kind = s.kind == QLatin1String("late")    ? LocoSim::TagFault::Late
                                     : s.kind == QLatin1String("twice")   ? LocoSim::TagFault::Twice
                                     : s.kind == QLatin1String("crc")     ? LocoSim::TagFault::BadCrc
                                                                          : LocoSim::TagFault::None;
        setTagFault(s.on, kind, 3, s.value > 0 ? int(s.value) : 500);
        break;
    }
    case Do::Reader:
        if (!s.kind.isEmpty()) setReaderHealth(s.on.toInt(), s.kind == QLatin1String("ok"));
        setReaderOn(s.on.toInt(), s.value != 0);
        break;
    case Do::AirFault: {
        const int ch = s.on == QLatin1String("BP") ? BrakePlant::Bp : s.on == QLatin1String("BC") ? BrakePlant::Bc : BrakePlant::Mr;
        const BrakePlant::Fault f = s.kind == QLatin1String("stuck") ? BrakePlant::Fault::Stuck
                                  : s.kind == QLatin1String("slow")  ? BrakePlant::Fault::Slow
                                  : s.kind == QLatin1String("leak")  ? BrakePlant::Fault::Leak
                                                                     : BrakePlant::Fault::None;
        m_bench->setPlantFault(ch, f, s.value);
        break;
    }
    case Do::BiuSilent:
        m_bench->biu()->setSilent(s.value != 0);
        break;
    case Do::BiuFault:
        m_bench->biu()->raiseFault(quint16(s.text.isEmpty() ? quint32(s.value) : s.text.toUInt(nullptr, 0)));
        break;
    case Do::Note:
        break;
    }
    showMotion();
    return true;
}

// ---- session 406: on into the next route --------------------------------------------

QString LocoSimWindow::routeName() const
{
    return m_routeIndex >= 0 && m_routeIndex < m_routes.size() ? m_routes.at(m_routeIndex).name : QString();
}

QVector<RfidTag::Route> LocoSimWindow::routesIn(const QString &file) const
{
    if (file == m_routeFile) return m_routes;
    auto it = m_fileRoutes.find(file);
    if (it == m_fileRoutes.end()) it = m_fileRoutes.insert(file, RfidTag::readFile(file));
    return it.value();
}

QVector<LocoSimWindow::ChainRef> LocoSimWindow::chainCandidates() const
{
    QVector<ChainRef> out;
    if (m_routeFile.isEmpty()) return out;   // the Tag Builder's route: no file to look beside
    for (int i = 0; i < m_routes.size(); ++i) out.append({ m_routeFile, i, m_routes.at(i).name });
    const QString folder = QFileInfo(m_routeFile).absolutePath();
    for (const TagLibrary::Entry &e : TagLibrary::scan(folder))
        if (e.folder.isEmpty() && QFileInfo(e.file).fileName() != QFileInfo(m_routeFile).fileName())
            out.append({ e.file, e.route, e.name });
    return out;
}

bool LocoSimWindow::plannedNext(ChainRef *next, double *ahead, QString *why) const
{
    *ahead = 0;
    if (m_routeIndex < 0) { *why = QString(); return false; }
    const RfidTag::Route &from = m_routes.at(m_routeIndex);
    switch (m_chain) {
    case Chain::Stop:
        *why = tr("then: stop");
        return false;
    case Chain::List: {
        const int at = m_chainAt + 1;
        if (at >= m_chainList.size()) { *why = tr("then: the order's end"); return false; }
        *next = m_chainList.at(at);
        const QVector<RfidTag::Route> routes = routesIn(next->file);
        if (next->index < 0 || next->index >= routes.size()) { *why = tr("then: %1 cannot be read").arg(next->name); return false; }
        const QVector<SimPreview::Row> own = SimPreview::rowsOf(from), rows = SimPreview::rowsOf(routes.at(next->index));
        const double sign = from.dir == RfidTag::DirReverse ? -1 : 1;
        if (!own.isEmpty() && !rows.isEmpty()) *ahead = (rows.first().absLoc - own.last().absLoc) * sign;
        *why = tr("then: %1, %2 m on").arg(next->name).arg(*ahead, 0, 'f', 0);
        return true;
    }
    case Chain::Auto: {
        const QVector<ChainRef> refs = chainCandidates();
        QVector<RfidTag::Route> routes;
        for (const ChainRef &r : refs) routes.append(routesIn(r.file).value(r.index));
        QVector<int> tied;
        const int pick = SimPreview::nextRoute(routes, from, ahead, &tied);
        if (pick < 0) {
            *why = refs.isEmpty() ? tr("then: no file to look in for the next route")
                                  : tr("then: no route continues from its last tag (dir %1, within %2 m)")
                                        .arg(from.dir).arg(int(SimPreview::kChainAhead));
            return false;
        }
        *next = refs.at(pick);
        *why = tr("then: %1, %2 m on").arg(next->name).arg(*ahead, 0, 'f', 0);
        if (!tied.isEmpty()) {
            QStringList others;
            for (int t : tied) others << refs.at(t).name;
            *why += tr(" (as near: %1; Order\u2026 picks)").arg(others.join(QStringLiteral(", ")));
        }
        return true;
    }
    }
    return false;
}

QString LocoSimWindow::chainNote() const
{
    ChainRef next;
    double ahead = 0;
    QString why;
    plannedNext(&next, &ahead, &why);
    return why;
}

void LocoSimWindow::refreshChainNote()
{
    if (!m_chainNoteLabel) return;
    const QString note = chainNote();
    m_chainNoteLabel->setText(note);
    m_chainNoteLabel->setToolTip(note);
    m_chainOrder->setEnabled(!m_routeFile.isEmpty());
}

void LocoSimWindow::setChain(Chain mode)
{
    m_chain = mode;
    {
        const QSignalBlocker block(m_chainBox);
        m_chainBox->setCurrentIndex(m_chainBox->findData(int(mode)));
    }
    refreshChainNote();
}

void LocoSimWindow::setChainList(const QVector<ChainRef> &routes)
{
    m_chainList = routes;
    m_chainAt = -1;
    for (int i = 0; i < routes.size(); ++i)
        if (routes.at(i).file == m_routeFile && routes.at(i).index == m_routeIndex) m_chainAt = i;
    setChain(Chain::List);
}

void LocoSimWindow::editChainOrder()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Routes in order"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *note = new QLabel(tr("Tick the routes to run and drag them into order. The loco runs on from each into the "
                               "next; it starts on the route picked above."), &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *list = new QListWidget(&dialog);
    list->setDragDropMode(QAbstractItemView::InternalMove);
    const auto add = [list](const ChainRef &r, bool on) {
        auto *item = new QListWidgetItem(r.file == QString() ? r.name : tr("%1  \u00B7  %2").arg(r.name, QFileInfo(r.file).fileName()), list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
        item->setCheckState(on ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, r.file);
        item->setData(Qt::UserRole + 1, r.index);
        item->setData(Qt::UserRole + 2, r.name);
    };
    QSet<QString> listed;
    for (const ChainRef &r : m_chainList) { add(r, true); listed.insert(r.file + QLatin1Char('#') + QString::number(r.index)); }
    for (const ChainRef &r : chainCandidates())
        if (!listed.contains(r.file + QLatin1Char('#') + QString::number(r.index))) add(r, false);
    layout->addWidget(list, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(520, 560);
    if (dialog.exec() != QDialog::Accepted) return;
    QVector<ChainRef> chosen;
    for (int i = 0; i < list->count(); ++i) {
        const QListWidgetItem *item = list->item(i);
        if (item->checkState() == Qt::Checked)
            chosen.append({ item->data(Qt::UserRole).toString(), item->data(Qt::UserRole + 1).toInt(),
                            item->data(Qt::UserRole + 2).toString() });
    }
    setChainList(chosen);
}

// At the route's end: on into the next one, the loco where it is.
bool LocoSimWindow::chainOn()
{
    ChainRef next;
    double ahead = 0;
    QString why;
    if (!plannedNext(&next, &ahead, &why)) return false;
    const QString from = routeName();
    if (next.file != m_routeFile) {
        const QVector<RfidTag::Route> routes = routesIn(next.file);
        if (next.index < 0 || next.index >= routes.size()) return false;
        unfollowLayout();   // session 411: off the layout's line, into another file's route
        m_routes = routes;
        m_routeFile = next.file;
        m_routeFileLabel->setText(QFileInfo(next.file).fileName());
        m_routeFileLabel->setToolTip(QDir::toNativeSeparators(next.file));
        fillRouteBox();
    }
    if (m_chain == Chain::List) ++m_chainAt;
    showRoute(next.index);
    const RfidTag::Route &route = m_routes.at(m_routeIndex);
    m_readers.reset(new SimPreview::Readers(SimPreview::rowsOf(route)));
    m_readers->setReader(1, readerSends(1));
    m_readers->setReader(2, readerSends(2));
    m_readers->setMissing(skippedTags());
    m_routeRow = -1;
    m_routeEnded = false;
    m_tagStates.clear();
    markFaultedRows();
    ++m_chained;
    m_status->ok(tr("Route %1 ended: on to route %2, its first tag %3 m ahead.").arg(from, route.name).arg(ahead, 0, 'f', 0));
    if (m_recorder && m_recorder->recording())
        m_recorder->mark(m_clock(), tr("Route %1 ended: on to route %2").arg(from, route.name));
    // Run into the middle of it (an overlap): its tags from here on.
    QVector<SimPreview::Readers::Send> sends;
    const int row = m_readers->step(m_routeD, 0, &sends);
    if (row >= 0) m_routeRow = row;
    for (const SimPreview::Readers::Send &send : sends) sendWithFault(send);
    refreshChainNote();
    return true;
}

void LocoSimWindow::setReaderOn(int reader, bool on)
{
    if (reader == 1 || reader == 2) m_readerOn[reader - 1]->setChecked(on);
}

// ---- session 410: a tag by hand ------------------------------------------------------------

QVector<QPair<QString, QByteArray>> LocoSimWindow::routeTags() const
{
    QVector<QPair<QString, QByteArray>> tags;
    if (m_routeIndex < 0 || m_routeIndex >= m_routes.size()) return tags;
    for (const RfidTag::Tag &t : m_routes.at(m_routeIndex).tags) tags.append({ RfidTag::nameOf(t.bytes), t.bytes });
    return tags;
}

TagSendDialog *LocoSimWindow::openTagSend()
{
    if (!m_tagSend) m_tagSend = new TagSendDialog(this);
    m_tagSend->refreshRoute();
    m_tagSend->show();
    m_tagSend->raise();
    return m_tagSend;
}

bool LocoSimWindow::sendTagByHand(int reader, const QString &pageX, const QString &pageY, QString *why)
{
    QString dummy;
    QString &whyNot = why ? *why : dummy;
    if (reader != 1 && reader != 2) { whyNot = tr("reader 1 or 2"); return false; }
    const QString x = pageX.trimmed(), y = pageY.trimmed();
    if (x.isEmpty() || y.isEmpty()) { whyNot = tr("neither page_x nor page_y may be empty"); return false; }
    const QByteArray frame = LocoSim::simulatorTagFrame(reader, x, y);
    if (frame.isEmpty()) { whyNot = tr("page_y and page_x together must be exactly 32 hex digits"); return false; }
    const int port = reader == 2 && m_reader2Port->checkedId() == 1 ? 1 : 0;
    const QString portName = port == 0 ? tr("port A") : tr("port B");
    bool sent = false;
    if (m_tagSink) {
        m_tagSink(port, frame);
        sent = true;
    }
#ifdef DL_HAVE_SERIAL
    else if (m_tagLink[port] && m_tagLink[port]->isOpen()) {
        sent = m_tagLink[port]->write(frame) == frame.size();
    }
#endif
    const QString name = RfidTag::nameOf(frame.mid(4, 16));
    if (m_recorder && m_recorder->recording())
        record(QStringLiteral("tag"), portName, frame, tr("reader %1 \u00B7 tag %2 \u00B7 by hand").arg(reader).arg(name),
               sent ? QString() : tr("not sent: %1 closed").arg(portName));
    {
        SimSentLog::Frame f;
        f.ms = m_clock();
        f.reader = reader;
        f.port = portName;
        f.bytes = frame;
        f.sent = sent;
        f.note = sent ? tr("by hand") : tr("by hand, not sent: %1 closed").arg(portName);
        m_sentLog->add(f);
    }
    if (!sent) { whyNot = tr("%1 is not open").arg(portName); return false; }
    ++m_tagsSent;
    m_status->ok(tr("Tag %1 sent by hand on reader %2 (%3).").arg(name).arg(reader).arg(portName));
    refreshTop();
    return true;
}

// ---- session 409: the readers' health ----------------------------------------------------

bool LocoSimWindow::readerSends(int reader) const
{
    return (reader == 1 || reader == 2) && m_readerOn[reader - 1]->isChecked() && !m_readerFail[reader - 1];
}

void LocoSimWindow::setReaderHealth(int reader, bool ok)
{
    if (reader != 1 && reader != 2) return;
    m_readerFail[reader - 1] = !ok;
    m_readerHealth[reader - 1]->button(ok ? 0 : 1)->setChecked(true);
    if (m_readers) m_readers->setReader(reader, readerSends(reader));
}

void LocoSimWindow::resetHealthClock(int reader)
{
    // As the simulator's timer: the first frame one period after the reader is set.
    m_healthDue[reader - 1] = m_clock() + LocoSim::kHealthPeriodMs[reader - 1];
}

void LocoSimWindow::healthTick()
{
    const qint64 now = m_clock();
    // Session 420: stopped, nothing else re-reads the @biu_cmd's age.
    if (!running() && m_logKnown && brakeSource() == LocoSim::BrakeSource::VccLog
        && now - m_logMs > kBiuCmdStaleMs) {
        const bool was = m_motion.braking();
        applyBrake();
        if (was != m_motion.braking()) showMotion();
    }
    for (int reader = 1; reader <= 2; ++reader) {
        if (!m_readerOn[reader - 1]->isChecked() || now < m_healthDue[reader - 1]) continue;
        m_healthDue[reader - 1] = now + LocoSim::kHealthPeriodMs[reader - 1];
        const QByteArray frame = LocoSim::healthFrame(reader, !m_readerFail[reader - 1]);
        const int port = reader == 2 && m_reader2Port->checkedId() == 1 ? 1 : 0;
        bool sent = false;
        if (m_healthSink) {
            m_healthSink(port, frame);
            sent = true;
        } else if (!m_tagSink) {
#ifdef DL_HAVE_SERIAL
            if (m_tagLink[port] && m_tagLink[port]->isOpen()) sent = m_tagLink[port]->write(frame) == frame.size();
#endif
        }
        if (!sent) continue;   // its port closed: nothing to say it on
        ++m_healthSent[reader - 1];
        {
            SimSentLog::Frame f;
            f.ms = now;
            f.health = true;
            f.reader = reader;
            f.port = port == 0 ? tr("port A") : tr("port B");
            f.bytes = frame;
            m_sentLog->add(f);
        }
        if (m_recorder && m_recorder->recording())
            record(QStringLiteral("health"), port == 0 ? tr("port A") : tr("port B"), frame,
                   tr("reader %1 \u00B7 health %2").arg(reader).arg(m_readerFail[reader - 1] ? tr("fail (2)") : tr("OK (1)")));
    }
}

bool LocoSimWindow::selectRouteTag(const QString &name)
{
    for (int row = 0; row < m_tags->rowCount(); ++row) {
        if (!m_tags->item(row, ColTag) || m_tags->item(row, ColTag)->text() != name) continue;
        m_selectingTag = true;
        m_tagsToggle->setChecked(true);
        m_tags->selectRow(row);
        m_tags->scrollToItem(m_tags->item(row, ColTag));
        m_selectingTag = false;
        return true;
    }
    return false;
}

void LocoSimWindow::setOpenInWorkbench(std::function<void(const QString &, const QString &)> open)
{
    m_sentLog->setOpenInWorkbench(std::move(open));
}

void LocoSimWindow::setReader2OwnPort(bool own)
{
    m_reader2Port->button(own ? 1 : 0)->setChecked(true);
    refreshTagPortState();
}

bool LocoSimWindow::placeAtStart()
{
    m_readers.reset();
    m_routeRow = -1;
    m_routeEnded = false;
    m_portWarned = false;
    m_late.clear();
    m_tagStates.clear();
    for (const QString &name : skippedTags())
        for (int row = 0; row < m_tags->rowCount(); ++row)
            if (m_tags->item(row, ColTag)->text() == name) m_tagStates[row] = RouteStrip::Skipped;
    markFaultedRows();
    const QSignalBlocker block(m_tags);
    for (int row = 0; row < m_tags->rowCount(); ++row) {
        m_tags->item(row, ColAt)->setText(QString());
        m_tags->item(row, ColReader1)->setText(QString());
        m_tags->item(row, ColReader2)->setText(QString());
    }
    if (m_routeIndex < 0) {
        showRoutePosition();
        return false;
    }
    const RfidTag::Route &route = m_routes.at(m_routeIndex);
    if (route.dir != RfidTag::DirNominal && route.dir != RfidTag::DirReverse) {
        m_status->fail(tr("Route %1 has no direction: set it in the RFID Tag Builder. The loco moves by it.").arg(route.name));
        showRoutePosition();
        return false;
    }
    const QVector<SimPreview::Row> rows = SimPreview::rowsOf(route);
    const int start = qBound(0, m_startTag->currentIndex(), rows.size() - 1);
    m_readers.reset(new SimPreview::Readers(rows));
    m_readers->setReader(1, readerSends(1));
    m_readers->setReader(2, readerSends(2));
    m_readers->setMissing(skippedTags());
    m_routeD = m_pulseD = rows.at(start).absLoc;
    m_routeRow = start;
    showRoutePosition();
    return true;
}

// The loco moved (always ahead along the route): what the readers send there.
// The pulses' location follows what speed sensor 1 channel 1 counted, as the
// VCC would; it parts from the loco's on a slip or a skid.
void LocoSimWindow::stepRoute(const LocoSim::Step &step)
{
    const double metres = step.metres;
    if (!m_readers || m_routeEnded || metres <= 0) return;
    const RfidTag::Route &route = m_routes.at(m_routeIndex);
    const double sign = route.dir == RfidTag::DirReverse ? -1 : 1;
    m_routeD += sign * metres;
    const double perMetre = LocoSim::pulsesPerMetre(m_motion.wheel());
    if (m_sensorOn[0]->isChecked() && perMetre > 0) m_pulseD += sign * step.pulses[0][0] / perMetre;
    QVector<SimPreview::Readers::Send> sends;
    const int row = m_readers->step(m_routeD, metres, &sends);
    if (row < 0) {
        // Session 406: short of the route's first tag (run into from the
        // route before): on the way to it, nothing to send yet.
        const bool before = !m_readers->rows().isEmpty() && (m_routeD - m_readers->rows().first().absLoc) * sign < 0;
        if (!before && !chainOn()) {
            m_routeEnded = true;
            QString why;
            ChainRef next;
            double ahead = 0;
            plannedNext(&next, &ahead, &why);
            m_status->warn(tr("Past route %1's last tag at %2 m: no more tags to send. The simulator never sends a "
                              "route's last tag.").arg(route.name).arg(m_routeD, 0, 'f', 1)
                           + (m_chain == Chain::Stop ? QString() : tr(" %1").arg(why)));
        }
    } else {
        m_routeRow = row;
    }
    for (const SimPreview::Readers::Send &send : sends) sendWithFault(send);
    showRoutePosition();
}

void LocoSimWindow::sendTag(const SimPreview::Readers::Send &send, const QString &note)
{
    const QByteArray frame = LocoSim::tagFrame(send.reader, send.tag);
    if (frame.isEmpty()) return;
    const int port = send.reader == 2 && m_reader2Port->checkedId() == 1 ? 1 : 0;
    bool sent = false;
    if (m_tagSink) {
        m_tagSink(port, frame);
        sent = true;
    }
#ifdef DL_HAVE_SERIAL
    else if (m_tagLink[port] && m_tagLink[port]->isOpen()) {
        sent = m_tagLink[port]->write(frame) == frame.size();
    }
#endif
    const QString portName = port == 0 ? tr("port A") : tr("port B");
    if (m_recorder && m_recorder->recording())
        record(QStringLiteral("tag"), portName, frame,
               tr("reader %1 \u00B7 tag %2 \u00B7 at %3 m").arg(send.reader).arg(RfidTag::nameOf(send.tag)).arg(m_routeD, 0, 'f', 1),
               sent ? note : tr("not sent: %1 closed").arg(portName));
    {
        SimSentLog::Frame f;
        f.ms = m_clock();
        f.reader = send.reader;
        f.port = portName;
        f.bytes = frame;
        f.atM = m_routeD;
        f.sent = sent;
        f.note = sent ? note : tr("not sent: %1 closed").arg(portName);
        m_sentLog->add(f);
    }
    QTableWidgetItem *cell = m_tags->item(send.row, send.reader == 1 ? ColReader1 : ColReader2);
    if (cell) {
        cell->setText(sent ? tr("%1 · %2 m").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")))
                                     .arg(m_routeD, 0, 'f', 1)
                                 + (note.isEmpty() ? QString() : tr(" · %1").arg(note))
                           : tr("not sent: %1 closed").arg(portName));
        cell->setToolTip(QString::fromLatin1(frame.toHex(' ')));
    }
    if (sent && send.reader == 1 && m_scenarioOn) m_tagsSeen.insert(RfidTag::nameOf(send.tag));
    if (sent) {
        ++m_tagsSent;
        m_tagStates[send.row] |= send.reader == 1 ? RouteStrip::SentOn1 : RouteStrip::SentOn2;
    } else if (!m_portWarned) {
        m_portWarned = true;
        m_status->warn(tr("Reader %1's tag %2 was not sent: %3 is not open.")
                           .arg(send.reader).arg(RfidTag::nameOf(send.tag), portName));
    }
}

void LocoSimWindow::showRoutePosition()
{
    if (!m_routeWhere) return;
    {
        const QSignalBlocker block(m_tags);
        for (int row = 0; row < m_tags->rowCount(); ++row)
            m_tags->item(row, ColAt)->setText(row == m_routeRow ? QStringLiteral("▶") : QString());
    }
    const double none = std::numeric_limits<double>::quiet_NaN();
    if (!m_readers) {
        m_routeWhere->setText(m_routeIndex < 0 ? tr("Open a route to send its tags.") : tr("Not on the route."));
        m_pulseWhere->clear();
        for (RouteStrip *strip : { m_wholeStrip, m_followStrip }) {
            strip->setLoco(none, none);
            strip->setTagStates({});
            strip->setReader2Window(none, none);
        }
        showOnLayout();
        return;
    }
    const RfidTag::Route &route = m_routes.at(m_routeIndex);
    const double sign = route.dir == RfidTag::DirReverse ? -1 : 1;
    // The follow view keeps a third behind the loco and two thirds ahead.
    const double behind = kFollowMetres / 3;
    const double lo = sign > 0 ? m_routeD - behind : m_routeD - (kFollowMetres - behind);
    m_followStrip->setView(lo, lo + kFollowMetres);
    // Reader 2's window after the row the loco is in, while reader 2 is on.
    double from = none, to = none;
    if (m_readerOn[1]->isChecked() && !m_routeEnded && m_routeRow >= 0 && m_routeRow < m_readers->rows().size()) {
        const double at = m_readers->rows().at(m_routeRow).absLoc;
        if ((m_routeD - at) * sign < 24) {    // until the loco is through it
            from = at + sign * 20;
            to = at + sign * 24;
        }
    }
    for (RouteStrip *strip : { m_wholeStrip, m_followStrip }) {
        strip->setLoco(m_routeD, m_pulseD);
        strip->setTagStates(m_tagStates);
        strip->setTagLabels(faultLabels());
        strip->setReader2Window(from, to);
    }
    const double gap = m_pulseD - m_routeD;
    QString pulses;
    if (!m_sensorOn[0]->isChecked())
        pulses = tr("Speed sensor 1 is off: no pulses to place the loco by");
    else if (std::fabs(gap) < 0.05)
        pulses = tr("By speed sensor 1's pulses: %1 m, with the loco").arg(m_pulseD, 0, 'f', 1);
    else
        pulses = tr("By speed sensor 1's pulses: %1 m, %2 m %3 the loco")
                     .arg(m_pulseD, 0, 'f', 1)
                     .arg(std::fabs(gap), 0, 'f', 1)
                     .arg(gap * sign > 0 ? tr("ahead of") : tr("behind"));
    m_pulseWhere->setText(pulses);
    const QVector<SimPreview::Row> &rows = m_readers->rows();
    QString text = tr("At %1 m").arg(m_routeD, 0, 'f', 1);
    if (m_routeEnded) {
        text += tr(" · past the last tag");
    } else if (m_routeRow >= 0 && m_routeRow < rows.size()) {
        const SimPreview::Row &row = rows.at(m_routeRow);
        text += tr(" · next tag in %1 m").arg(std::fabs(row.nextAbsLoc - m_routeD), 0, 'f', 1);
        if (m_routeRow + 1 < rows.size()) text += tr(" (%1)").arg(rows.at(m_routeRow + 1).id);
    }
    m_routeWhere->setText(text + tr(" · %1 tag frame(s) sent").arg(m_tagsSent));
    showOnLayout();
}

// ---- the tag ports ---------------------------------------------------------------------

void LocoSimWindow::refreshTagPorts()
{
#ifdef DL_HAVE_SERIAL
    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    for (int which = 0; which < 2; ++which) {
        const QString current = tagPortName(which);
        const QSignalBlocker block(m_tagPort[which]);
        m_tagPort[which]->clear();
        for (const QSerialPortInfo &p : ports) {
            QString label = p.portName();
            if (!p.description().isEmpty()) label += QStringLiteral(" — ") + p.description();
            m_tagPort[which]->addItem(label, p.portName());
        }
        const int i = m_tagPort[which]->findData(current);
        if (i >= 0) m_tagPort[which]->setCurrentIndex(i);
        else m_tagPort[which]->setEditText(current);
    }
#endif
    refreshTagPortState();
}

// The listed port's name ("COM5", not "COM5 — USB Serial"), or what was typed.
QString LocoSimWindow::tagPortName(int which) const
{
    const QComboBox *box = m_tagPort[which];
    const int i = box->currentIndex();
    if (i >= 0 && box->currentText() == box->itemText(i)) return box->itemData(i).toString();
    return box->currentText().section(QStringLiteral(" — "), 0, 0).trimmed();
}

void LocoSimWindow::openOrCloseTagPort(int which)
{
#ifdef DL_HAVE_SERIAL
    if (!m_serial) return;
    SerialLink *link = m_tagLink[which];
    if (link && (link->isOpen() || link->isOpening())) {
        m_serial->close(link->config().portName);
        m_weOpened[which] = false;
        refreshTagPortState();
        return;
    }
    const QString port = tagPortName(which);
    if (port.isEmpty()) {
        m_status->fail(tr("Pick a port first."));
        return;
    }
    if (m_tagLink[1 - which] && m_tagLink[1 - which]->config().portName == port && m_tagLink[1 - which]->isOpen()) {
        m_status->fail(tr("%1 is already port %2.").arg(port, which == 0 ? tr("B") : tr("A")));
        return;
    }
    SerialConfig config;
    config.portName = port;
    config.baud = LocoSim::kTagBaud;
    m_serial->setFeed(port, false);      // frames out only: never into a console tab,
    m_serial->setBinary(port, true);     // nor judged as log lines
    link = m_serial->linkFor(port);
    if (m_tagLink[which] != link) {
        if (m_tagLink[which]) disconnect(m_tagLink[which], nullptr, this, nullptr);
        m_tagLink[which] = link;
        connect(link, &SerialLink::opened, this, [this, which]() {
            refreshTagPortState();
            m_status->ok(tr("Port %1 open: %2.").arg(which == 0 ? tr("A") : tr("B"), m_tagLink[which]->config().portName));
        });
        connect(link, &SerialLink::openFailed, this, [this, which](const QString &why) {
            m_weOpened[which] = false;
            refreshTagPortState();
            m_status->fail(tr("Port %1 not opened: %2").arg(which == 0 ? tr("A") : tr("B"), why));
        });
        connect(link, &SerialLink::closed, this, [this]() { refreshTagPortState(); });
        connect(link, &SerialLink::lost, this, [this](const QString &why) {
            refreshTagPortState();
            m_status->fail(tr("A tag port went away: %1").arg(why));
        });
    }
    m_weOpened[which] = true;
    m_portWarned = false;
    m_serial->openAsync(config);
    refreshTagPortState();
#else
    Q_UNUSED(which);
#endif
}

void LocoSimWindow::refreshTagPortState()
{
    if (!m_tagPortState[0]) return;
    const bool ownPort = m_reader2Port && m_reader2Port->checkedId() == 1;
    for (int which = 0; which < 2; ++which) {
        const bool used = which == 0 || ownPort;
        bool open = false, opening = false;
        QString summary;
#ifdef DL_HAVE_SERIAL
        if (SerialLink *link = m_tagLink[which]) {
            open = link->isOpen();
            opening = link->isOpening();
            summary = link->config().summary();
        }
#endif
        const bool can = m_serial != nullptr;
        m_tagPort[which]->setEnabled(can && used && !open && !opening);
        m_tagPortOpen[which]->setEnabled(can && (used || open));
        m_tagPortOpen[which]->setText(open || opening ? tr("Close") : tr("Open"));
        QLabel *state = m_tagPortState[which];
        if (!can) {
            state->setText(tr("No serial ports in this build"));
            state->setStyleSheet(UiColor::mutedStyle());
        } else if (open) {
            state->setText(QStringLiteral("● ") + tr("Open · %1").arg(summary));
            state->setStyleSheet(UiColor::okStyle());
        } else if (opening) {
            state->setText(QStringLiteral("○ ") + tr("Opening…"));
            state->setStyleSheet(UiColor::warningStyle());
        } else {
            state->setText(QStringLiteral("○ ") + (used ? tr("Closed") : tr("Not used: reader 2 is on port A")));
            state->setStyleSheet(UiColor::mutedStyle());
        }
    }
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
    wheelBody->addStretch(1);
    auto *topRow = new QHBoxLayout;
    topRow->setSpacing(12);
    topRow->addWidget(wheel, 1);
    layout->addLayout(topRow);

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

    QVBoxLayout *brakeBody = nullptr;
    QFrame *brakes = card(tr("Brake"), page, &brakeBody);
    auto *brakeGrid = new QGridLayout;
    const auto rateSpin = [brakes](const char *name, double max, const QString &suffix) {
        auto *spin = new QDoubleSpinBox(brakes);
        spin->setObjectName(QLatin1String(name));
        spin->setRange(0.05, max);
        spin->setDecimals(2);
        spin->setSingleStep(0.05);
        spin->setSuffix(suffix);
        return spin;
    };
    m_normal = rateSpin("locoSimNormal", 3, QStringLiteral(" m/s\u00B2"));
    m_normal->setToolTip(tr("NB: BC 1.2 with BP at the FSB level (3.5) or above"));
    m_fullService = rateSpin("locoSimFullService", 3, QStringLiteral(" m/s\u00B2"));
    m_fullService->setToolTip(tr("BC at full service with BP at the FSB level (3.5) or above"));
    m_emergency = rateSpin("locoSimEmergency", 3, QStringLiteral(" m/s\u00B2"));
    m_emergency->setToolTip(tr("BC at full service with BP at 0"));
    m_fullBc = rateSpin("locoSimFullBc", 6, QStringLiteral(" kg/cm\u00B2"));
    m_fullBc->setToolTip(tr("Annexure-4: 2.4 in formation, 3.5 for a light engine"));
    m_buildUp = new QDoubleSpinBox(brakes);
    m_buildUp->setObjectName(QStringLiteral("locoSimBuildUp"));
    m_buildUp->setRange(0, 60);
    m_buildUp->setDecimals(1);
    m_buildUp->setSingleStep(0.5);
    m_buildUp->setSuffix(QStringLiteral(" s"));
    m_buildUp->setToolTip(tr("Braking by the BIU's command: the brake grows from 0 to its full rate over this "
                             "time after it comes on (0: at once)"));
    brakeGrid->addWidget(labelNamed(tr("Normal (NB)"), "muted", brakes), 0, 0);
    brakeGrid->addWidget(m_normal, 0, 1);
    brakeGrid->addWidget(labelNamed(tr("Full service"), "muted", brakes), 1, 0);
    brakeGrid->addWidget(m_fullService, 1, 1);
    brakeGrid->addWidget(labelNamed(tr("Emergency"), "muted", brakes), 2, 0);
    brakeGrid->addWidget(m_emergency, 2, 1);
    brakeGrid->addWidget(labelNamed(tr("BC at full service"), "muted", brakes), 3, 0);
    brakeGrid->addWidget(m_fullBc, 3, 1);
    brakeGrid->addWidget(labelNamed(tr("Build-up"), "muted", brakes), 4, 0);
    brakeGrid->addWidget(m_buildUp, 4, 1);
    brakeGrid->setColumnStretch(2, 1);
    brakeBody->addLayout(brakeGrid);
    auto *brakeNote = labelNamed(tr("By the BIU's command: A9 under 1.75 is EB (the emergency rate), under 3.95 "
                                    "FSB (full service), under 4.7 NB (the NB rate); A9 charged with SA9 on is "
                                    "the light engine brake (full service). By the bench's air: BC sets the rate, "
                                    "the NB rate at BC 1.2, full service at BC full service, straight lines "
                                    "between; BP under 3.5 raises it, to the emergency rate at BP 0. "
                                    "These are settings, not loco data."), "muted", brakes);
    brakeNote->setWordWrap(true);
    brakeBody->addWidget(brakeNote);
    brakeBody->addStretch(1);
    topRow->addWidget(brakes, 1);
    layout->addStretch(1);

    const auto pull = [this]() { pullFromUi(); showMotion(); };
    connect(m_wheelMm, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, pull);
    connect(m_pulsesPerRev, QOverload<int>::of(&QSpinBox::valueChanged), this, pull);
    connect(m_countScale, QOverload<int>::of(&QSpinBox::valueChanged), this, pull);
    connect(m_port, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { refreshTop(); });
    connect(m_intervalMs, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int ms) {
        m_timer->setInterval(ms);
        m_motion.setPulseInterval(ms / 1000.0);
        refreshTop();
    });
    connect(m_hosts, &QLineEdit::editingFinished, this, [this]() { setHosts(m_hosts->text()); });
    for (QDoubleSpinBox *spin : { m_normal, m_fullService, m_emergency, m_fullBc, m_buildUp })
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]() { applyBrake(); showMotion(); });
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
    m_motion.setPulseInterval(m_intervalMs->value() / 1000.0);   // session 408: each message is one interval's
    m_perMetre->setText(tr("%1 counts a metre").arg(LocoSim::pulsesPerMetre(wheel), 0, 'f', 1));
}

void LocoSimWindow::showMotion()
{
    const LocoSim::Motion::Drive drive = m_motion.drive();
    if ((drive == LocoSim::Motion::Drive::Accelerate && m_motion.speed() >= m_motion.maxSpeed())
        || (drive == LocoSim::Motion::Drive::Decelerate && m_motion.speed() <= 0)) {
        // At the end of the ramp: hold.
        m_motion.setDrive(LocoSim::Motion::Drive::Hold);
    }
    // The motion's drive on the buttons: a ramp's end or a brake sets it to Hold.
    for (int button = 0; button < 3; ++button)
        if (kDriveOfButton[button] == m_motion.drive()) m_drive->button(button)->setChecked(true);
    for (QAbstractButton *button : m_drive->buttons()) button->setEnabled(!m_motion.braking());
    const double scale = scaleFor(m_motion.maxSpeed());
    m_speedometer->setScaleMaximum(scale);
    m_speedometer->setLimit(m_motion.maxSpeed());
    m_speedometer->setValue(m_motion.speed());
    {
        const QSignalBlocker block(m_speed);
        m_speed->setValue(m_motion.speed());
    }
    if (m_rfidSpeed) {
        const QSignalBlocker block(m_rfidSpeed);
        m_rfidSpeed->setValue(m_motion.speed());
        m_rfidSpeedValue->setText(tr("%1 km/h").arg(m_motion.speed(), 0, 'f', 1));
        m_rfidBrake->setText(m_motion.braking() ? tr("braking %1 m/s\u00B2").arg(m_motion.brakeNow(), 0, 'f', 2)
                                                : QString());
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
            // Session 408: the pulses are one interval's, so the speed they say is over it.
            gauge->setValue(step.pulses[sensor][channel] / perMetre / m_motion.pulseInterval() * 3.6);
            pulses->setText(tr("%1 pulses").arg(step.pulses[sensor][channel]));
        }
    }
}

void LocoSimWindow::refreshTop()
{
    if (!m_state) return;
    const bool on = running();
    const int sensors = int(m_sensorOn[0] && m_sensorOn[0]->isChecked()) + int(m_sensorOn[1] && m_sensorOn[1]->isChecked());
    QString text = on ? tr("Sending \u00B7 %1 sensor(s) \u00B7 %2 messages \u00B7 %3 s")
                            .arg(sensors).arg(m_sent).arg((m_clock() - m_startedMs) / 1000)
                      : tr("Stopped \u00B7 %1 messages sent").arg(m_sent);
    if (m_readers) text += tr(" \u00B7 %1 tag frame(s)").arg(m_tagsSent);
    if (m_motion.braking()) text += tr(" \u00B7 braking %1 m/s\u00B2").arg(m_motion.brakeNow(), 0, 'f', 2);
    m_state->setText(text);
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
    m_portWarned = false;
#ifdef DL_HAVE_SERIAL
    if (m_readers && !m_tagSink && (m_readerOn[0]->isChecked() || m_readerOn[1]->isChecked())
        && !(m_tagLink[0] && m_tagLink[0]->isOpen()))
        m_status->warn(tr("Tag port A is not open (RFID tab): the loco runs along the route but no tag is sent."));
#endif
    pullFromUi();
    m_motion.setPulseInterval(m_intervalMs->value() / 1000.0);
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
    m_late.clear();   // a late tag due after the stop is not sent
    stopScenario();   // session 407: its clock is the loco's run
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
    applyBrake();
    const LocoSim::Step step = m_motion.step(seconds);
    const double perMetre = LocoSim::pulsesPerMetre(m_motion.wheel());
    const auto kmhOf = [&](quint32 pulses) {
        return perMetre > 0 ? pulses / perMetre / m_motion.pulseInterval() * 3.6 : 0.0;
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
        if (m_recorder && m_recorder->recording())
            record(QStringLiteral("speed"), QStringLiteral("%1 : %2").arg(m_hostList.join(QStringLiteral(", "))).arg(m_port->value()),
                   message, tr("sensor %1 \u00B7 dir %2 \u00B7 ch1 %3 \u00B7 ch2 %4").arg(sensor + 1).arg(int(m_motion.direction()))
                                .arg(step.pulses[sensor][0]).arg(step.pulses[sensor][1]));
        logMessage(message, kmhOf(step.pulses[sensor][0]), kmhOf(step.pulses[sensor][1]));
    }
    showStep(step, seconds);
    stepRoute(step);
    flushLate();
    scenarioTick();
    showMotion();
}

// ---- the test recording (session 404) ---------------------------------------------

void LocoSimWindow::setRecorder(BenchRecord::Recorder *recorder)
{
    if (m_recorder == recorder) return;
    if (m_recorder) disconnect(m_recorder, nullptr, this, nullptr);
    m_recorder = recorder;
    if (recorder) {
        connect(recorder, &BenchRecord::Recorder::started, this, [this]() {
            if (!running()) start();
            refreshRecordButton();
        });
        connect(recorder, &BenchRecord::Recorder::stopped, this, [this](const QString &path, int count) {
            if (running()) stop();
            refreshRecordButton();
            m_status->ok(tr("Recording saved: %1 frames in %2.").arg(count).arg(QDir::toNativeSeparators(path)));
        });
        if (recorder->recording() && !running()) start();
    }
    refreshRecordButton();
}

bool LocoSimWindow::toggleRecording()
{
    if (!m_recorder) {
        m_status->fail(tr("No recorder: open the Loco Simulator from Tools \u25B8 Simulators to record."));
        return false;
    }
    if (m_recorder->recording()) {
        m_recorder->stop(m_clock());
        return true;
    }
    QString why;
    const QString path = BenchRecord::defaultPath(m_clock());
    if (!m_recorder->start(path, m_clock(), tr("Loco Simulator and Brake Test Bench"), &why)) {
        m_status->fail(tr("Cannot record to %1: %2").arg(QDir::toNativeSeparators(path), why));
        return false;
    }
    m_status->ok(tr("Recording to %1.").arg(QDir::toNativeSeparators(path)));
    return true;
}

void LocoSimWindow::record(const QString &kind, const QString &peer, const QByteArray &bytes, const QString &says,
                           const QString &note)
{
    BenchRecord::Item item;
    item.ms = m_clock();
    item.kind = kind;
    item.peer = peer;
    item.bytes = bytes;
    item.says = says;
    item.note = note;
    m_recorder->append(item);
}

void LocoSimWindow::refreshRecordButton()
{
    const bool on = m_recorder && m_recorder->recording();
    m_record->setText(on ? tr("\u25A0 Stop recording") : tr("\u25CF Record"));
    m_record->setProperty("running", on);
    restyle();
}

// ---- the brake (session 398) ------------------------------------------------------

void LocoSimWindow::followBench(BrakeBenchWindow *bench)
{
    if (!bench || bench == m_bench) return;
    m_bench = bench;
    connect(bench, &BrakeBenchWindow::airChanged, this, &LocoSimWindow::observeAir, Qt::UniqueConnection);
    connect(bench, &BrakeBenchWindow::commandChanged, this, &LocoSimWindow::observeCommand, Qt::UniqueConnection);
    connect(bench, &QObject::destroyed, this, [this]() {
        m_airKnown = false;
        m_cmdKnown = false;
        applyBrake();
        showMotion();
    });
    observeCommand(bench->target(BrakePlant::Bp), bench->target(BrakePlant::Bc));
    observeAir(bench->pressure(BrakePlant::Bp), bench->pressure(BrakePlant::Bc));
}

void LocoSimWindow::observeCommand(double bpTargetKgcm2, double bcTargetKgcm2)
{
    const bool same = m_cmdKnown && bpTargetKgcm2 == m_cmdBp && bcTargetKgcm2 == m_cmdBc;
    m_cmdKnown = true;
    m_cmdBp = bpTargetKgcm2;
    m_cmdBc = bcTargetKgcm2;
    if (same) return;
    const bool was = m_motion.braking();
    applyBrake();
    if (was != m_motion.braking()) showMotion();
}

bool LocoSimWindow::observeBiuCmdLine(const QString &line)
{
    const CaptureLine cap = CaptureDecoder::parseLine(line);
    LocoSim::CommandTargets t;
    if (!cap.valid || cap.type != CapType::BiuCmd || !LocoSim::biuCommandTargets(cap.bytes, brakeRates(), &t)) return false;
    const qint64 now = m_clock();
    const QString from = tr("loco %1 VCC %2").arg(cap.locoId).arg(cap.ctrlId);
    // One loco's BIU: the source followed holds until it has been silent.
    const bool other = cap.locoId != m_logLoco || cap.ctrlId != m_logVcc;
    if (m_logKnown && other && now - m_logMs <= kBiuCmdStaleMs) {
        ++m_logIgnored;
        m_logIgnoredFrom = from;
        return false;
    }
    if (other) m_logIgnored = 0;   // a new source: the count was against the old one
    ++m_logLines;
    m_logKnown = true;
    m_log = t;
    m_logMs = now;
    m_logLoco = cap.locoId;
    m_logVcc = cap.ctrlId;
    m_logFrom = from;
    if (brakeSource() == LocoSim::BrakeSource::VccLog) {
        const bool was = m_motion.braking();
        applyBrake();
        if (was != m_motion.braking()) showMotion();
    }
    return true;
}

void LocoSimWindow::setBrakeSource(LocoSim::BrakeSource source)
{
    m_brakeSource->setCurrentIndex(m_brakeSource->findData(int(source)));
}

LocoSim::BrakeSource LocoSimWindow::brakeSource() const
{
    return LocoSim::BrakeSource(m_brakeSource->currentData().toInt());
}

void LocoSimWindow::observeAir(double bpKgcm2, double bcKgcm2)
{
    const bool same = m_airKnown && bpKgcm2 == m_airBp && bcKgcm2 == m_airBc;
    m_airKnown = true;
    m_airBp = bpKgcm2;
    m_airBc = bcKgcm2;
    if (same) return;
    const bool was = m_motion.braking();
    applyBrake();
    if (was != m_motion.braking()) showMotion();
}

void LocoSimWindow::setBrakeFollows(bool on)
{
    m_brakeFollow->setChecked(on);
}

bool LocoSimWindow::brakeFollows() const
{
    return m_brakeFollow->isChecked();
}

LocoSim::BrakeRates LocoSimWindow::brakeRates() const
{
    LocoSim::BrakeRates rates;
    rates.normalMps2 = m_normal->value();
    rates.fullServiceMps2 = m_fullService->value();
    rates.emergencyMps2 = m_emergency->value();
    rates.fullBcKgcm2 = m_fullBc->value();
    rates.buildUpSeconds = m_buildUp->value();
    return rates;
}

void LocoSimWindow::setBrakeRates(const LocoSim::BrakeRates &rates)
{
    m_normal->setValue(rates.normalMps2);
    m_fullService->setValue(rates.fullServiceMps2);
    m_emergency->setValue(rates.emergencyMps2);
    m_fullBc->setValue(rates.fullBcKgcm2);
    m_buildUp->setValue(rates.buildUpSeconds);
}

// The command (session 418) or the air into the motion's brake, and what the Drive tab says of it.
void LocoSimWindow::applyBrake()
{
    if (!m_brakeState || !m_fullBc || !m_buildUp || !m_brakeSource) return;
    const bool follows = m_brakeFollow->isChecked();
    const LocoSim::BrakeRates rates = brakeRates();
    const LocoSim::BrakeSource source = brakeSource();
    if (source != LocoSim::BrakeSource::Air) {
        // The bench's BIU (session 418), or the VCC's @biu_cmd lines (session 420).
        const bool fromLog = source == LocoSim::BrakeSource::VccLog;
        const qint64 age = fromLog && m_logKnown ? m_clock() - m_logMs : 0;
        const bool known = fromLog ? m_logKnown && age <= kBiuCmdStaleMs : m_cmdKnown;
        const double bp = fromLog ? m_log.bp : m_cmdBp, bc = fromLog ? m_log.bc : m_cmdBc;
        const LocoSim::BrakeType type = known ? LocoSim::commandedBrake(rates, bp, bc) : LocoSim::BrakeType::Released;
        const double mps2 = follows ? LocoSim::brakeTypeRate(rates, type) : 0.0;
        m_motion.setBrakeBuildUp(rates.buildUpSeconds);
        m_motion.setBrake(mps2);
        QString cmd = tr("A9 %1 \u00B7 SA9 %2 kg/cm\u00B2").arg(bp, 0, 'f', 2).arg(bc, 0, 'f', 2);
        if (fromLog) {
            cmd = m_log.isolated ? tr("BIU isolated: the loco pilot's brakes") : cmd;
            if (m_log.a9Above) cmd += tr(" \u00B7 A9 marked valid, above 6.0");
            if (m_log.sa9Above) cmd += tr(" \u00B7 SA9 marked valid, above 4.0");
            cmd += tr(" \u00B7 @biu_cmd, %1").arg(m_logFrom);
            if (m_logIgnored > 0)
                cmd += tr(" \u00B7 %n not taken from another source (last %1)", nullptr, m_logIgnored).arg(m_logIgnoredFrom);
        }
        if (!follows) m_brakeState->setText(tr("off: nothing slows the loco"));
        else if (fromLog && !m_logKnown)
            m_brakeState->setText(tr("no @biu_cmd from the VCC yet (its biu_dlconsole_mirror copies)"));
        else if (fromLog && !known)
            m_brakeState->setText(tr("no @biu_cmd for %1 s: released \u00B7 last from %2")
                                      .arg(age / 1000.0, 0, 'f', 1).arg(m_logFrom));
        else if (!known) m_brakeState->setText(tr("open Tools \u25B8 Simulators \u25B8 Brake Test Bench"));
        else if (!m_motion.braking()) m_brakeState->setText(tr("released \u00B7 %1").arg(cmd));
        else if (m_motion.brakeEffort() < 1)
            m_brakeState->setText(tr("braking %1 %2 of %3 m/s\u00B2 (building up) \u00B7 %4")
                                      .arg(LocoSim::brakeTypeName(type)).arg(m_motion.brakeNow(), 0, 'f', 2)
                                      .arg(mps2, 0, 'f', 2).arg(cmd));
        else m_brakeState->setText(tr("braking %1 %2 m/s\u00B2 \u00B7 %3")
                                       .arg(LocoSim::brakeTypeName(type)).arg(mps2, 0, 'f', 2).arg(cmd));
        return;
    }
    const double mps2 = follows && m_airKnown ? LocoSim::brakeDeceleration(rates, m_airBp, m_airBc) : 0.0;
    m_motion.setBrakeBuildUp(0);   // the air builds up by itself
    m_motion.setBrake(mps2);
    const QString air = tr("BC %1 \u00B7 BP %2 kg/cm\u00B2").arg(m_airBc, 0, 'f', 2).arg(m_airBp, 0, 'f', 2);
    if (!follows) m_brakeState->setText(tr("off: nothing slows the loco"));
    else if (!m_airKnown) m_brakeState->setText(tr("open Tools \u25B8 Simulators \u25B8 Brake Test Bench"));
    else if (m_motion.braking()) m_brakeState->setText(tr("braking %1 m/s\u00B2 \u00B7 %2").arg(mps2, 0, 'f', 2).arg(air));
    else m_brakeState->setText(tr("released \u00B7 %1").arg(air));
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
    const LocoSim::BrakeRates rates;
    m_brakeFollow->setChecked(settings.value(QStringLiteral("brakeFollowsBench"), true).toBool());
    m_normal->setValue(settings.value(QStringLiteral("brakeNormal"), rates.normalMps2).toDouble());
    m_fullService->setValue(settings.value(QStringLiteral("brakeFullService"), rates.fullServiceMps2).toDouble());
    m_emergency->setValue(settings.value(QStringLiteral("brakeEmergency"), rates.emergencyMps2).toDouble());
    m_fullBc->setValue(settings.value(QStringLiteral("brakeFullBc"), rates.fullBcKgcm2).toDouble());
    m_buildUp->setValue(settings.value(QStringLiteral("brakeBuildUp"), rates.buildUpSeconds).toDouble());
    const QString source = settings.value(QStringLiteral("brakeSource")).toString();
    setBrakeSource(source == QLatin1String("air")   ? LocoSim::BrakeSource::Air
                   : source == QLatin1String("log") ? LocoSim::BrakeSource::VccLog
                                                    : LocoSim::BrakeSource::Command);
    applyBrake();

    for (int reader = 0; reader < 2; ++reader)
        m_readerOn[reader]->setChecked(settings.value(QStringLiteral("rfidReader%1").arg(reader + 1), true).toBool());
    for (int reader = 1; reader <= 2; ++reader) resetHealthClock(reader);
    m_reader2Port->button(settings.value(QStringLiteral("rfidReader2OwnPort"), false).toBool() ? 1 : 0)->setChecked(true);
    for (int which = 0; which < 2; ++which)
        m_tagPort[which]->setEditText(settings.value(QStringLiteral("rfidPort%1").arg(QChar(QLatin1Char('A' + which)))).toString());
    refreshTagPorts();
    const QString routeFile = settings.value(QStringLiteral("rfidRouteFile")).toString();
    if (!routeFile.isEmpty() && (routeFile.startsWith(QLatin1Char(':')) || QFileInfo::exists(routeFile))
        && loadRouteFile(routeFile)) {
        const int route = qBound(0, settings.value(QStringLiteral("rfidRoute"), 0).toInt(), m_routes.size() - 1);
        setRoute(route);
        setStartRow(settings.value(QStringLiteral("rfidStartRow"), 0).toInt());
    } else {
        showRoutePosition();
    }
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
    settings.setValue(QStringLiteral("brakeFollowsBench"), m_brakeFollow->isChecked());
    settings.setValue(QStringLiteral("brakeNormal"), m_normal->value());
    settings.setValue(QStringLiteral("brakeFullService"), m_fullService->value());
    settings.setValue(QStringLiteral("brakeEmergency"), m_emergency->value());
    settings.setValue(QStringLiteral("brakeFullBc"), m_fullBc->value());
    settings.setValue(QStringLiteral("brakeBuildUp"), m_buildUp->value());
    settings.setValue(QStringLiteral("brakeSource"),
                      brakeSource() == LocoSim::BrakeSource::Air      ? QStringLiteral("air")
                      : brakeSource() == LocoSim::BrakeSource::VccLog ? QStringLiteral("log")
                                                                      : QStringLiteral("command"));
    for (int reader = 0; reader < 2; ++reader)
        settings.setValue(QStringLiteral("rfidReader%1").arg(reader + 1), m_readerOn[reader]->isChecked());
    settings.setValue(QStringLiteral("rfidReader2OwnPort"), m_reader2Port->checkedId() == 1);
    for (int which = 0; which < 2; ++which)
        settings.setValue(QStringLiteral("rfidPort%1").arg(QChar(QLatin1Char('A' + which))), tagPortName(which));
    settings.setValue(QStringLiteral("rfidRouteFile"), m_routeFile);
    settings.setValue(QStringLiteral("rfidRoute"), m_routeIndex);
    settings.setValue(QStringLiteral("rfidStartRow"), m_startTag->currentIndex());
}
