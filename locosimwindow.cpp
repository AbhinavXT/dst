#include "locosimwindow.h"
#include "biusim.h"
#include "settings.h"
#include "simwidgets.h"
#include "speedgauge.h"
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

namespace {

constexpr int kMaxFrameRows = 500;
// The RFID tab's tag table.
enum TagColumn { ColAt, ColTag, ColType, ColLoc, ColNext, ColSkip, ColReader1, ColReader2, TagColumns };
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
    tabs->addTab(buildRfidTab(), tr("RFID"));
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

// ---- RFID ------------------------------------------------------------------------------

QWidget *LocoSimWindow::buildRfidTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(12);
    auto *top = new QHBoxLayout;
    top->setSpacing(12);

    // ---- the route -----------------------------------------------------------
    QVBoxLayout *routeBody = nullptr;
    QFrame *route = card(tr("Route"), page, &routeBody);
    auto *routeGrid = new QGridLayout;
    routeGrid->setHorizontalSpacing(10);
    auto *fileRow = new QHBoxLayout;
    m_routeFileLabel = labelNamed(tr("No route open"), "mono", route);
    m_routeFileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    fileRow->addWidget(m_routeFileLabel, 1);
    auto *library = new QPushButton(tr("Library…"), route);
    library->setToolTip(tr("A route from the RFID Tag Builder's scenario library"));
    fileRow->addWidget(library);
    auto *openFile = new QPushButton(tr("Open…"), route);
    openFile->setToolTip(tr("A route file: the Tag Builder's .tagroute.xml, a route.xml or a Configuration1.xml"));
    fileRow->addWidget(openFile);
    routeGrid->addWidget(labelNamed(tr("File"), "muted", route), 0, 0);
    routeGrid->addLayout(fileRow, 0, 1);
    m_routeBox = new QComboBox(route);
    m_routeBox->setObjectName(QStringLiteral("locoSimRoute"));
    routeGrid->addWidget(labelNamed(tr("Route"), "muted", route), 1, 0);
    routeGrid->addWidget(m_routeBox, 1, 1);
    auto *startRow = new QHBoxLayout;
    m_startTag = new QComboBox(route);
    m_startTag->setObjectName(QStringLiteral("locoSimStartTag"));
    m_startTag->setToolTip(tr("The tag the loco starts at: its abs_loc"));
    startRow->addWidget(m_startTag, 1);
    auto *back = new QPushButton(tr("Back to start"), route);
    back->setToolTip(tr("Put the loco at the start tag again; every tag can be sent again"));
    startRow->addWidget(back);
    routeGrid->addWidget(labelNamed(tr("Start tag"), "muted", route), 2, 0);
    routeGrid->addLayout(startRow, 2, 1);
    routeGrid->setColumnStretch(1, 1);
    routeBody->addLayout(routeGrid);
    m_routeWhere = labelNamed(QString(), "bigValue", route);
    m_routeWhere->setWordWrap(true);
    routeBody->addWidget(m_routeWhere);
    routeBody->addStretch(1);
    top->addWidget(route, 3);

    // ---- the readers and their ports ---------------------------------------------
    QVBoxLayout *readerBody = nullptr;
    QFrame *readers = card(tr("Readers"), page, &readerBody);
    auto *readerGrid = new QGridLayout;
    readerGrid->setHorizontalSpacing(10);
    for (int reader = 0; reader < 2; ++reader) {
        m_readerOn[reader] = new QCheckBox(tr("Reader %1").arg(reader + 1), readers);
        m_readerOn[reader]->setObjectName(QStringLiteral("locoSimReader%1").arg(reader + 1));
        m_readerOn[reader]->setToolTip(reader == 0 ? tr("Sends each tag as the loco reaches it")
                                                   : tr("Sends the main tag again 20-24 m past it, then its duplicate"));
        readerGrid->addWidget(m_readerOn[reader], 0, reader);
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
    auto *note = labelNamed(tr("Each tag goes out as LocoTcasSimulator's 22-byte frame: AA AA, length 22, reader, the "
                               "16 tag bytes, BB BB, at %1 8N1.").arg(LocoSim::kTagBaud), "muted", readers);
    note->setWordWrap(true);
    readerBody->addWidget(note);
    readerBody->addStretch(1);
    top->addWidget(readers, 2);
    layout->addLayout(top);

    // ---- the tags --------------------------------------------------------------
    m_tags = new QTableWidget(0, TagColumns, page);
    m_tags->setObjectName(QStringLiteral("locoSimTags"));
    m_tags->setHorizontalHeaderLabels({ QString(), tr("Tag"), tr("Type"), tr("abs_loc"), tr("Next"), tr("Skip"),
                                        tr("Reader 1"), tr("Reader 2") });
    m_tags->verticalHeader()->setVisible(false);
    m_tags->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tags->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tags->horizontalHeader()->setStretchLastSection(true);
    m_tags->setAlternatingRowColors(true);
    m_tags->setToolTip(tr("The route's rows as the simulator reads them. Skip: the tag is not sent "
                          "(the simulator then keeps the row before it)."));
    layout->addWidget(m_tags, 1);

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
    connect(m_routeBox, QOverload<int>::of(&QComboBox::activated), this, [this](int index) { setRoute(index); });
    connect(m_startTag, QOverload<int>::of(&QComboBox::activated), this, [this](int row) { setStartRow(row); });
    connect(back, &QPushButton::clicked, this, [this]() { placeAtStart(); });
    for (int reader = 0; reader < 2; ++reader)
        connect(m_readerOn[reader], &QCheckBox::toggled, this, [this, reader](bool on) {
            if (m_readers) m_readers->setReader(reader + 1, on);
        });
    connect(m_reader2Port, &QButtonGroup::idClicked, this, [this]() { refreshTagPortState(); });
    connect(m_tags, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (item->column() == ColSkip && m_readers) m_readers->setMissing(skippedTags());
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
    m_routeIndex = index;
    {
        const QSignalBlocker block(m_routeBox);
        m_routeBox->setCurrentIndex(index);
    }
    fillTagTable();
    {
        const QSignalBlocker block(m_startTag);
        m_startTag->clear();
        for (int row = 0; row < m_tags->rowCount(); ++row)
            m_startTag->addItem(tr("%1 at %2 m").arg(m_tags->item(row, ColTag)->text(), m_tags->item(row, ColLoc)->text()));
    }
    placeAtStart();
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
            QString(), QString(), QString(),
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

void LocoSimWindow::setReaderOn(int reader, bool on)
{
    if (reader == 1 || reader == 2) m_readerOn[reader - 1]->setChecked(on);
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
    m_readers->setReader(1, m_readerOn[0]->isChecked());
    m_readers->setReader(2, m_readerOn[1]->isChecked());
    m_readers->setMissing(skippedTags());
    m_routeD = rows.at(start).absLoc;
    m_routeRow = start;
    showRoutePosition();
    return true;
}

// The loco moved `metres` (always ahead along the route): what the readers send there.
void LocoSimWindow::stepRoute(double metres)
{
    if (!m_readers || m_routeEnded || metres <= 0) return;
    const RfidTag::Route &route = m_routes.at(m_routeIndex);
    m_routeD += route.dir == RfidTag::DirReverse ? -metres : metres;
    QVector<SimPreview::Readers::Send> sends;
    const int row = m_readers->step(m_routeD, metres, &sends);
    if (row < 0) {
        m_routeEnded = true;
        m_status->warn(tr("Past route %1's last tag at %2 m: no more tags to send. The simulator never sends a "
                          "route's last tag.").arg(route.name).arg(m_routeD, 0, 'f', 1));
    } else {
        m_routeRow = row;
    }
    for (const SimPreview::Readers::Send &send : sends) sendTag(send);
    showRoutePosition();
}

void LocoSimWindow::sendTag(const SimPreview::Readers::Send &send)
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
    QTableWidgetItem *cell = m_tags->item(send.row, send.reader == 1 ? ColReader1 : ColReader2);
    if (cell) {
        cell->setText(sent ? tr("%1 · %2 m").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")))
                                 .arg(m_routeD, 0, 'f', 1)
                           : tr("not sent: %1 closed").arg(portName));
        cell->setToolTip(QString::fromLatin1(frame.toHex(' ')));
    }
    if (sent) {
        ++m_tagsSent;
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
    if (!m_readers) {
        m_routeWhere->setText(m_routeIndex < 0 ? tr("Open a route to send its tags.") : tr("Not on the route."));
        return;
    }
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
    QString text = on ? tr("Sending \u00B7 %1 sensor(s) \u00B7 %2 messages \u00B7 %3 s")
                            .arg(sensors).arg(m_sent).arg((m_clock() - m_startedMs) / 1000)
                      : tr("Stopped \u00B7 %1 messages sent").arg(m_sent);
    if (m_readers) text += tr(" \u00B7 %1 tag frame(s)").arg(m_tagsSent);
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
    stepRoute(step.metres);
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

    for (int reader = 0; reader < 2; ++reader)
        m_readerOn[reader]->setChecked(settings.value(QStringLiteral("rfidReader%1").arg(reader + 1), true).toBool());
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
    for (int reader = 0; reader < 2; ++reader)
        settings.setValue(QStringLiteral("rfidReader%1").arg(reader + 1), m_readerOn[reader]->isChecked());
    settings.setValue(QStringLiteral("rfidReader2OwnPort"), m_reader2Port->checkedId() == 1);
    for (int which = 0; which < 2; ++which)
        settings.setValue(QStringLiteral("rfidPort%1").arg(QChar(QLatin1Char('A' + which))), tagPortName(which));
    settings.setValue(QStringLiteral("rfidRouteFile"), m_routeFile);
    settings.setValue(QStringLiteral("rfidRoute"), m_routeIndex);
    settings.setValue(QStringLiteral("rfidStartRow"), m_startTag->currentIndex());
}
