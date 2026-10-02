#include "lococonfigwindow.h"
#include "profileiodialogs.h"
#include "undolog.h"

#include <QPointer>

#include "flasherstyle.h"
#include "lococonfighistorydialog.h"
#include "lococonfigmodel.h"
#include "settings.h"
#include "statusline.h"
#include "udpsender.h"
#include "uicolors.h"
#include "uistyle.h"
#include "windowgeometry.h"
#include "capturedecoder.h"
#include "logentry.h"
#include "messagedispatcher.h"

#include <QCloseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

const char kConfigsFileName[] = "loco_configs.json";
const char kHistoryFileName[] = "loco_config_history.jsonl";
const char kSchemaPath[]      = ":/schema/kavach.xml";
const char kDefaultsPath[]    = ":/lococonfig/loco_defaults.json";

// The confirmation box lists at most this many changed fields in its main
// text; the full list is always in "Show Details".
const int kChangesShownInline = 12;

QString resolveDataDirectory(const QString &requested)
{
    if (!requested.isEmpty()) {
        return requested;
    }
    return QFileInfo(Settings::iniPath()).absolutePath();
}

}  // namespace

LocoConfigWindow::LocoConfigWindow(QWidget *parent, const QString &dataDirectory)
    : QMainWindow(parent)
    , m_dataDirectory(resolveDataDirectory(dataDirectory))
{
    setWindowTitle(tr("Loco Configuration"));
    WindowGeometry::makeResizableWindow(this);
    resize(1200, 800);
    WindowGeometry::restore(this, QStringLiteral("locoConfiguration"));

    // ---- the layout, its presentation, the tool's defaults -------------------------
    QString error;
    if (!m_layout.load(QLatin1String(kSchemaPath), &error)) {
        m_loadError = tr("The LOCO_INFO layout could not be read from the schema:\n%1").arg(error);
    } else if (!LocoInfo::loadDefaults(QLatin1String(kDefaultsPath), m_layout, &m_defaults, &error,
                                       &m_defaultsSource)) {
        m_loadError = error;
    } else {
        m_presentation = LocoInfo::Presentation::fromLayout(m_layout);
    }
    if (!m_loadError.isEmpty()) {
        // Nothing can be edited without a layout; say why instead of showing
        // an empty table.
        auto *message = new QLabel(m_loadError, this);
        message->setWordWrap(true);
        message->setAlignment(Qt::AlignCenter);
        message->setStyleSheet(UiColor::errorStyle());
        setCentralWidget(message);
        return;
    }
    LocoInfo::completeValues(m_layout, LocoInfo::Values(), &m_defaults);

    m_store = new LocoInfo::ConfigStore(QDir(m_dataDirectory).filePath(QLatin1String(kConfigsFileName)),
                                        &m_layout, m_defaults);
    m_history = new LocoInfo::SendHistory(QDir(m_dataDirectory).filePath(QLatin1String(kHistoryFileName)));
    m_sender = new UdpSender(this);

    // ---- widgets -----------------------------------------------------------------
    m_model = new LocoFieldModel(&m_layout, &m_presentation, this);
    m_filter = new LocoFieldFilter(this);
    m_filter->setSourceModel(m_model);

    auto *central = new QWidget(this);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(buildAppBar());
    auto *page = new QWidget(central);
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(16, 16, 16, 12);
    pageLayout->setSpacing(12);
    pageLayout->addWidget(buildBody(), 1);
    pageLayout->addWidget(buildSendBar());
    m_status = new StatusLine(page);
    pageLayout->addWidget(m_status);
    centralLayout->addWidget(page, 1);
    setCentralWidget(central);

    // ---- saving: debounced, so typing a value does not write the file per key ----
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &LocoConfigWindow::saveNow);

    connect(m_model, &LocoFieldModel::valueEdited, this, &LocoConfigWindow::onValueEdited);
    connect(m_model, &LocoFieldModel::editRejected, this, [this](const QString &reason) {
        m_status->warn(reason);
    });
    connect(m_sender, &UdpSender::error, this, [this](const QString &what) {
        m_status->fail(tr("Send failed: %1").arg(what));
    });

    // ---- the stored configurations --------------------------------------------------
    if (!m_store->load()) {
        QMessageBox::warning(this, windowTitle(),
                             tr("The loco configurations could not be read, and a default is in use:\n\n%1\n\n"
                                "The file is left as it is until the next save.")
                                 .arg(m_store->lastError()));
    }
    if (!m_store->droppedKeys().isEmpty()) {
        m_status->warn(tr("Saved configurations had fields the schema no longer has (ignored): %1")
                           .arg(m_store->droppedKeys().join(QStringLiteral(", "))));
    }
    reloadConfigCombo();
    loadConfig(m_store->activeName());

    UiColor::onThemeChange(this, [this]() { restyle(); });
    restyle();

    // Ages in the live check ("seen 12 s ago") tick along.
    m_liveTimer = new QTimer(this);
    m_liveTimer->setInterval(1000);
    connect(m_liveTimer, &QTimer::timeout, this, &LocoConfigWindow::refreshLiveCheck);
    m_liveTimer->start();
}

LocoConfigWindow::~LocoConfigWindow()
{
    if (m_store != nullptr && m_saveTimer.isActive()) {
        saveNow();
    }
    delete m_store;
    delete m_history;
}

QString LocoConfigWindow::configsPath() const
{
    if (m_store == nullptr) {
        return QString();
    }
    return m_store->filePath();
}

QString LocoConfigWindow::historyPath() const
{
    if (m_history == nullptr) {
        return QString();
    }
    return m_history->filePath();
}

// =============================================================================
//  Building
// =============================================================================

QWidget *LocoConfigWindow::buildAppBar()
{
    m_appBar = new QFrame(this);
    m_appBar->setObjectName(QStringLiteral("locoAppBar"));
    m_appBar->setFixedHeight(56);
    auto *barLayout = new QHBoxLayout(m_appBar);
    barLayout->setContentsMargins(18, 0, 14, 0);
    barLayout->setSpacing(12);

    auto *title = new QLabel(tr("Loco Configuration"), m_appBar);
    title->setFont(FlasherStyle::scaledFont(title->font(), 1.15, true));
    barLayout->addWidget(title);
    barLayout->addSpacing(12);

    barLayout->addWidget(new QLabel(tr("Configuration"), m_appBar));
    m_configCombo = new QComboBox(m_appBar);
    m_configCombo->setMinimumWidth(240);
    m_configCombo->setMinimumHeight(34);
    m_configCombo->setToolTip(tr("One saved configuration per loco"));
    connect(m_configCombo, QOverload<int>::of(&QComboBox::activated), this, &LocoConfigWindow::onConfigChosen);
    barLayout->addWidget(m_configCombo);

    auto *manage = new QToolButton(m_appBar);
    manage->setText(tr("Manage"));
    manage->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(manage);
    // Undo (Ctrl+Z) for Delete and Reset (session 79). Added to the window
    // too, so the key works without opening the menu.
    m_undo = new UndoLog(this);
    m_actUndo = m_undo->createAction(this);
    addAction(m_actUndo);
    menu->addAction(m_actUndo);
    menu->addSeparator();
    connect(m_undo, &UndoLog::undone, this, [this](const QString &label, bool restored) {
        if (restored) {
            m_status->ok(tr("Undone: %1").arg(label));
        } else {
            m_status->warn(tr("Could not undo %1: it has changed since").arg(label));
        }
    });
    menu->addAction(tr("New from defaults…"), this, &LocoConfigWindow::newConfig);
    menu->addAction(tr("Duplicate…"), this, &LocoConfigWindow::duplicateConfig);
    menu->addAction(tr("Rename…"), this, &LocoConfigWindow::renameConfig);
    menu->addAction(tr("Delete…"), this, &LocoConfigWindow::deleteConfig);
    menu->addSeparator();
    menu->addAction(tr("Import loco_info.bin…"), this, &LocoConfigWindow::importBin);
    menu->addSeparator();
    // Session 94: to and from another PC.
    menu->addAction(tr("Export configurations…"), this, &LocoConfigWindow::exportConfigsDialog);
    menu->addAction(tr("Import configurations…"), this, &LocoConfigWindow::importConfigsDialog);
    menu->addSeparator();
    menu->addAction(tr("Reset all fields to defaults…"), this, &LocoConfigWindow::resetToDefaults);
    // Where "defaults" come from: the tool sync_linfo.py last read.
    if (!m_defaultsSource.isEmpty()) {
        menu->setToolTipsVisible(true);
        for (QAction *action : menu->actions()) {
            if (action->text().contains(tr("defaults"))) {
                action->setToolTip(tr("Defaults are the values in %1").arg(m_defaultsSource));
            }
        }
    }
    manage->setMenu(menu);
    barLayout->addWidget(manage);

    barLayout->addStretch(1);

    auto *historyButton = new QToolButton(m_appBar);
    historyButton->setText(tr("History"));
    historyButton->setToolTip(tr("Every configuration sent from this PC, byte for byte"));
    connect(historyButton, &QToolButton::clicked, this, &LocoConfigWindow::openHistory);
    barLayout->addWidget(historyButton);
    return m_appBar;
}

QWidget *LocoConfigWindow::buildBody()
{
    auto *splitter = new QSplitter(Qt::Horizontal, this);

    // ---- groups + search ---------------------------------------------------------
    auto *left = new QFrame(splitter);
    left->setObjectName(QStringLiteral("locoGroupsCard"));
    m_cards.append(left);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(12, 12, 12, 12);
    leftLayout->setSpacing(8);
    m_search = new QLineEdit(left);
    m_search->setPlaceholderText(tr("Search fields…"));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumHeight(32);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_filter->setSearch(text);
    });
    leftLayout->addWidget(m_search);
    auto *groupsTitle = new QLabel(tr("Groups"), left);
    m_sectionLabels.append(groupsTitle);
    leftLayout->addWidget(groupsTitle);
    m_groups = new QListWidget(left);
    m_groups->setFrameShape(QFrame::NoFrame);
    auto addGroup = [this](const QString &key) {
        auto *item = new QListWidgetItem(key, m_groups);
        item->setData(Qt::UserRole, key);
    };
    addGroup(LocoFieldFilter::allFields());
    addGroup(LocoFieldFilter::changedFromDefault());
    addGroup(LocoFieldFilter::changedSinceSent());
    for (const LocoInfo::Presentation::Group &group : m_presentation.groups) {
        addGroup(group.title);
    }
    // "Other" only when the schema has a field the presentation does not group.
    bool hasOther = false;
    for (const LocoInfo::Field &field : m_layout.fields()) {
        if (!field.isCrc && m_presentation.groupOf(field.key) == QLatin1String("Other")) {
            hasOther = true;
        }
    }
    if (hasOther) {
        addGroup(QStringLiteral("Other"));
    }
    m_groups->setCurrentRow(0);
    connect(m_groups, &QListWidget::currentRowChanged, this, &LocoConfigWindow::onGroupChanged);
    leftLayout->addWidget(m_groups, 1);
    left->setMinimumWidth(240);
    m_groups->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // ---- the field table ---------------------------------------------------------
    auto *right = new QFrame(splitter);
    right->setObjectName(QStringLiteral("locoTableCard"));
    m_cards.append(right);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    m_table = new QTableView(right);
    m_table->setModel(m_filter);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                             | QAbstractItemView::AnyKeyPressed);
    m_table->verticalHeader()->hide();
    m_table->setWordWrap(false);
    QHeaderView *header = m_table->horizontalHeader();
    header->setSectionResizeMode(LocoFieldModel::ColumnField, QHeaderView::Stretch);
    header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_table->setColumnWidth(LocoFieldModel::ColumnValue, 170);
    m_table->setColumnWidth(LocoFieldModel::ColumnDefault, 140);
    m_table->setColumnWidth(LocoFieldModel::ColumnLastSent, 140);
    m_table->setColumnWidth(LocoFieldModel::ColumnType, 110);
    m_table->setFont(UiStyle::monoFont());
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableView::customContextMenuRequested, this, [this](const QPoint &position) {
        const QModelIndex index = m_table->indexAt(position);
        if (!index.isValid()) {
            return;
        }
        const QString key = index.data(LocoFieldModel::KeyRole).toString();
        QMenu menu(this);
        QAction *revertDefault = menu.addAction(tr("Revert to default"));
        QAction *revertSent = menu.addAction(tr("Revert to last sent"));
        revertSent->setEnabled(m_model->hasLastSent());
        QAction *chosen = menu.exec(m_table->viewport()->mapToGlobal(position));
        if (chosen == revertDefault) {
            m_model->setValue(key, m_defaults.value(key));
            onValueEdited(key);
        } else if (chosen == revertSent && chosen != nullptr) {
            m_model->setValue(key, lastSentValues().value(key));
            onValueEdited(key);
        }
    });
    rightLayout->addWidget(m_table);

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({ 260, 940 });
    return splitter;
}

QWidget *LocoConfigWindow::buildSendBar()
{
    auto *bar = new QFrame(this);
    bar->setObjectName(QStringLiteral("locoSendBar"));
    m_cards.append(bar);
    auto *barLayout = new QVBoxLayout(bar);
    barLayout->setContentsMargins(18, 12, 18, 12);
    barLayout->setSpacing(6);

    // ---- targets: up to four, in a 2 x 2 grid of [tick] IP Port ----------------
    auto *titleRow = new QHBoxLayout();
    auto *sendTitle = new QLabel(tr("Send to VCC"), bar);
    m_sectionLabels.append(sendTitle);
    titleRow->addWidget(sendTitle);
    auto *bulkHint = new QLabel(tr("up to %1 targets · the same configuration goes to every ticked one")
                                    .arg(LocoInfo::kMaxTargets), bar);
    bulkHint->setStyleSheet(UiColor::mutedStyle());
    titleRow->addWidget(bulkHint);
    titleRow->addStretch(1);
    barLayout->addLayout(titleRow);
    // Export and Send go at the end of this row (below): beside the notes
    // they took the right third of the bar and wrapped every note to two
    // lines (session 135).

    auto *targetGrid = new QGridLayout();
    targetGrid->setHorizontalSpacing(8);
    targetGrid->setVerticalSpacing(6);
    for (int row = 0; row < LocoInfo::kMaxTargets; ++row) {
        auto *on = new QCheckBox(QString::number(row + 1), bar);
        on->setToolTip(tr("Send to target %1").arg(row + 1));
        auto *ip = new QLineEdit(bar);
        ip->setFont(UiStyle::monoFont());
        ip->setMinimumHeight(30);
        ip->setMinimumWidth(150);
        ip->setPlaceholderText(tr("IP address"));
        ip->setClearButtonEnabled(true);
        auto *port = new QSpinBox(bar);
        port->setRange(1, 65535);
        port->setFont(UiStyle::monoFont());
        port->setButtonSymbols(QAbstractSpinBox::NoButtons);
        port->setMinimumHeight(30);
        port->setFixedWidth(80);
        port->setToolTip(tr("UDP port (the VCC application listens on %1)").arg(LocoInfo::kDefaultPort));
        m_targetOn.append(on);
        m_targetIps.append(ip);
        m_targetPorts.append(port);

        // Two targets per line: 1 2 / 3 4.
        const int gridRow = row / 2;
        const int gridColumn = (row % 2) * 5;
        targetGrid->addWidget(on, gridRow, gridColumn);
        targetGrid->addWidget(ip, gridRow, gridColumn + 1);
        targetGrid->addWidget(new QLabel(tr("Port"), bar), gridRow, gridColumn + 2);
        targetGrid->addWidget(port, gridRow, gridColumn + 3);
        if (row % 2 == 0) {
            targetGrid->setColumnMinimumWidth(gridColumn + 4, 18);   // gap between the two columns
        }

        auto edited = [this]() {
            if (m_loadingConfig) {
                return;
            }
            for (int index = 0; index < LocoInfo::kMaxTargets; ++index) {
                m_config.targets[index].enabled = m_targetOn.at(index)->isChecked();
                m_config.targets[index].ip = m_targetIps.at(index)->text().trimmed();
                m_config.targets[index].port = static_cast<quint16>(m_targetPorts.at(index)->value());
            }
            scheduleSave();
            refreshSummary();
        };
        connect(on, &QCheckBox::toggled, this, edited);
        connect(ip, &QLineEdit::textChanged, this, edited);
        connect(port, QOverload<int>::of(&QSpinBox::valueChanged), this, edited);
    }
    targetGrid->setColumnStretch(1, 1);
    targetGrid->setColumnStretch(6, 1);
    barLayout->addLayout(targetGrid);

    // What will be sent, and the one field set by hand, on one line
    // (session 135: on two, with the rest of the bar, it took 304 px and
    // left the field table six of its 170 rows in a 720-px window).
    auto *summaryRow = new QHBoxLayout();
    summaryRow->setSpacing(16);
    m_summary = new QLabel(bar);
    m_summary->setObjectName(QStringLiteral("locoSummary"));
    m_summary->setFont(UiStyle::monoFont());
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    summaryRow->addWidget(m_summary);
    m_vccCrc = new QLabel(bar);
    m_vccCrc->setObjectName(QStringLiteral("locoVccCrc"));
    m_vccCrc->setWordWrap(true);
    summaryRow->addWidget(m_vccCrc, 1);
    barLayout->addLayout(summaryRow);

    auto *bottomRow = new QHBoxLayout();
    bottomRow->setSpacing(12);
    auto *noReply = new QLabel(tr("The VCC does not reply: DLConsole records what it sent, "
                                  "not whether the loco applied it."), bar);
    noReply->setObjectName(QStringLiteral("locoNoReply"));
    noReply->setWordWrap(true);
    noReply->setStyleSheet(UiColor::mutedStyle());
    auto *textColumn = new QVBoxLayout();
    textColumn->setSpacing(4);
    textColumn->addWidget(noReply);
    m_lastSent = new QLabel(bar);
    m_lastSent->setStyleSheet(UiColor::mutedStyle());
    textColumn->addWidget(m_lastSent);
    m_blocker = new QLabel(bar);
    m_blocker->setObjectName(QStringLiteral("locoBlocker"));
    m_blocker->setWordWrap(true);
    m_blocker->hide();   // shown only when something stops the send
    textColumn->addWidget(m_blocker);

    // The live check (see the header): what the loco itself reports.
    auto *liveRow = new QHBoxLayout();
    m_liveStatus = new QLabel(bar);
    // While a send waits for its confirmation, the line re-judges itself so
    // "no @linfo since the send" appears without a new frame to trigger it.
    m_awaitTimer = new QTimer(this);
    m_awaitTimer->setInterval(5000);
    connect(m_awaitTimer, &QTimer::timeout, this, [this]() {
        if (m_config.lastSentAt.isValid() && m_verifiedSentAt != m_config.lastSentAt) refreshLiveCheck();
    });
    m_awaitTimer->start();
    m_liveStatus->setWordWrap(true);
    m_liveStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    liveRow->addWidget(m_liveStatus, 1);
    m_liveLoad = new QPushButton(tr("Load the loco's values…"), bar);
    m_liveLoad->setToolTip(tr("Replace this configuration's values with what the loco reports in its @linfo"));
    m_liveLoad->hide();
    connect(m_liveLoad, &QPushButton::clicked, this, [this]() {
        const LiveInfo live = m_live.value(m_liveKeyShown);
        if (live.seenMs == 0) {
            return;
        }
        const auto answer = QMessageBox::question(
            this, windowTitle(),
            tr("Load the values loco %1 reports into \"%2\"? Its current values are replaced.")
                .arg(m_liveKeyShown, m_config.name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer == QMessageBox::Yes) {
            applyValues(live.values);
            m_status->ok(tr("Loaded the values loco %1 reports").arg(m_liveKeyShown));
        }
    });
    liveRow->addWidget(m_liveLoad);
    textColumn->addLayout(liveRow);
    bottomRow->addLayout(textColumn, 1);

    auto *exportButton = new QPushButton(tr("Export loco_info.bin…"), bar);
    exportButton->setMinimumHeight(38);
    exportButton->setToolTip(tr("The %1-byte body, as the loco_config tool writes it, for flashing at 0x%2")
                                 .arg(m_layout.bodySize())
                                 .arg(LocoInfo::kFlashAddress, 8, 16, QLatin1Char('0')));
    connect(exportButton, &QPushButton::clicked, this, &LocoConfigWindow::exportBin);
    titleRow->addWidget(exportButton);

    m_sendButton = new QPushButton(tr("Send to VCC…"), bar);
    m_sendButton->setObjectName(QStringLiteral("locoSendButton"));
    m_sendButton->setMinimumHeight(38);
    m_sendButton->setMinimumWidth(150);
    connect(m_sendButton, &QPushButton::clicked, this, [this]() { sendNow(true); });
    titleRow->addWidget(m_sendButton);
    barLayout->addLayout(bottomRow);
    return bar;
}

void LocoConfigWindow::restyle()
{
    const QPalette pal = palette();
    m_appBar->setStyleSheet(
        QStringLiteral("QFrame#locoAppBar { background-color:%1; border-bottom:1px solid %2; }")
            .arg(pal.color(QPalette::AlternateBase).name(), FlasherStyle::border().name(QColor::HexArgb)));
    for (QFrame *card : m_cards) {
        card->setStyleSheet(FlasherStyle::cardSheet(card->objectName()));
    }
    for (QLabel *label : m_sectionLabels) {
        FlasherStyle::makeSectionLabel(label);
    }
    m_sendButton->setStyleSheet(
        QStringLiteral("QPushButton#locoSendButton { background-color:%1; color:%2; border:none;"
                       " border-radius:8px; padding:0 18px; font-weight:600; }"
                       "QPushButton#locoSendButton:disabled { background-color:%3; color:%4; }")
            .arg(FlasherStyle::primary().name(), pal.color(QPalette::Base).name(),
                 pal.color(QPalette::AlternateBase).name(),
                 pal.color(QPalette::Disabled, QPalette::Text).name()));
    refreshSummary();
}

// =============================================================================
//  Configurations
// =============================================================================

void LocoConfigWindow::reloadConfigCombo()
{
    const QSignalBlocker blocker(m_configCombo);
    m_configCombo->clear();
    for (const QString &name : m_store->names()) {
        m_configCombo->addItem(name, name);
    }
    m_configCombo->setCurrentIndex(m_configCombo->findData(m_store->activeName()));
}

LocoInfo::Values LocoConfigWindow::lastSentValues() const
{
    if (m_config.lastSentBody.isEmpty()) {
        return LocoInfo::Values();
    }
    const LocoInfo::Parsed parsed = LocoInfo::parseBody(m_layout, m_config.lastSentBody);
    if (!parsed.ok) {
        return LocoInfo::Values();
    }
    return parsed.values;
}

void LocoConfigWindow::loadConfig(const QString &name)
{
    m_loadingConfig = true;
    m_config = m_store->config(name);
    m_model->setConfig(m_config.values, m_defaults, lastSentValues());
    for (int row = 0; row < LocoInfo::kMaxTargets; ++row) {
        const LocoInfo::SendTarget target = m_config.targets.value(row);
        m_targetOn.at(row)->setChecked(target.enabled);
        m_targetIps.at(row)->setText(target.ip);
        m_targetPorts.at(row)->setValue(target.port);
    }
    m_loadingConfig = false;
    refreshSummary();
}

void LocoConfigWindow::onConfigChosen(int index)
{
    const QString name = m_configCombo->itemData(index).toString();
    if (name.isEmpty() || name == m_config.name) {
        return;
    }
    if (m_saveTimer.isActive()) {
        saveNow();
    }
    m_store->setActiveName(name);
    m_store->save();
    loadConfig(name);
}

void LocoConfigWindow::applyValues(const LocoInfo::Values &values)
{
    m_config.values = values;
    LocoInfo::completeValues(m_layout, m_defaults, &m_config.values);
    m_model->setConfig(m_config.values, m_defaults, lastSentValues());
    scheduleSave();
    refreshSummary();
}

void LocoConfigWindow::onValueEdited(const QString &key)
{
    Q_UNUSED(key);
    m_config.values = m_model->values();
    m_filter->refresh();
    scheduleSave();
    refreshSummary();
}

void LocoConfigWindow::scheduleSave()
{
    m_saveTimer.start();
}

void LocoConfigWindow::saveNow()
{
    m_saveTimer.stop();
    m_store->upsert(m_config);
    if (!m_store->save()) {
        m_status->fail(m_store->lastError());
    }
}

void LocoConfigWindow::newConfig()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New configuration"),
                                               tr("Name (e.g. the loco number):"), QLineEdit::Normal,
                                               QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    if (m_store->contains(name)) {
        QMessageBox::warning(this, windowTitle(), tr("There is already a configuration called \"%1\".").arg(name));
        return;
    }
    saveNow();
    LocoInfo::LocoConfig config;
    config.name = name;
    config.values = m_defaults;
    config.targets = m_config.targets;   // same bench / same VCCs, new loco values
    m_store->upsert(config);
    m_store->setActiveName(name);
    m_store->save();
    reloadConfigCombo();
    loadConfig(name);
}

void LocoConfigWindow::duplicateConfig()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Duplicate configuration"), tr("Name of the copy:"),
                                               QLineEdit::Normal, m_config.name + tr(" (copy)"), &ok).trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    if (m_store->contains(name)) {
        QMessageBox::warning(this, windowTitle(), tr("There is already a configuration called \"%1\".").arg(name));
        return;
    }
    saveNow();
    LocoInfo::LocoConfig copy = m_config;
    copy.name = name;
    // A copy has never been sent anywhere.
    copy.lastSentBody.clear();
    copy.lastSentAt = QDateTime();
    copy.lastSentTarget.clear();
    m_store->upsert(copy);
    m_store->setActiveName(name);
    m_store->save();
    reloadConfigCombo();
    loadConfig(name);
}

void LocoConfigWindow::renameConfig()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename configuration"), tr("New name:"),
                                               QLineEdit::Normal, m_config.name, &ok).trimmed();
    if (!ok || name.isEmpty() || name == m_config.name) {
        return;
    }
    if (m_store->contains(name)) {
        QMessageBox::warning(this, windowTitle(), tr("There is already a configuration called \"%1\".").arg(name));
        return;
    }
    const QString previous = m_config.name;
    m_config.name = name;
    m_store->upsert(m_config, previous);
    m_store->setActiveName(name);
    saveNow();
    reloadConfigCombo();
}

void LocoConfigWindow::deleteConfig()
{
    if (m_store->names().size() <= 1) {
        QMessageBox::information(this, windowTitle(), tr("The last configuration cannot be deleted."));
        return;
    }
    const auto answer = QMessageBox::question(
        this, windowTitle(),
        tr("Delete the configuration \"%1\"?\n\nIts sends stay in the history.").arg(m_config.name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }
    m_saveTimer.stop();
    const LocoInfo::LocoConfig removed = m_store->config(m_config.name);
    const int removedAt = m_store->names().indexOf(m_config.name);
    m_store->remove(m_config.name);
    m_store->save();
    reloadConfigCombo();
    loadConfig(m_store->activeName());

    QPointer<LocoConfigWindow> self(this);
    m_undo->push(tr("Delete configuration \"%1\"").arg(removed.name), [self, removed, removedAt]() {
        if (!self || self->m_store->contains(removed.name)) {
            return false;   // a new one of the same name was made meanwhile
        }
        if (self->m_saveTimer.isActive()) {
            self->saveNow();
        }
        self->m_store->insertAt(removedAt, removed);
        self->m_store->setActiveName(removed.name);
        self->m_store->save();
        self->reloadConfigCombo();
        self->loadConfig(removed.name);
        return true;
    });
    m_status->say(tr("Deleted \"%1\". Ctrl+Z brings it back.").arg(removed.name));
}

void LocoConfigWindow::resetToDefaults()
{
    const auto answer = QMessageBox::question(
        this, windowTitle(),
        tr("Set every field of \"%1\" back to the defaults (%2)?").arg(m_config.name, m_defaultsSource),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes) {
        const LocoInfo::Values before = m_model->values();
        const QString configName = m_config.name;
        applyValues(m_defaults);
        m_status->say(tr("All fields reset to defaults. Ctrl+Z puts them back."));
        QPointer<LocoConfigWindow> self(this);
        m_undo->push(tr("Reset \"%1\" to defaults").arg(configName), [self, before, configName]() {
            if (!self || self->m_config.name != configName) {
                return false;   // another configuration is open now
            }
            self->applyValues(before);
            return true;
        });
    }
}

// =============================================================================
//  Files
// =============================================================================

void LocoConfigWindow::importBin()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import loco_info.bin"), QString(),
                                                      tr("LOCO_INFO (*.bin);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, windowTitle(), tr("Could not read %1:\n%2").arg(path, file.errorString()));
        return;
    }
    const LocoInfo::Parsed parsed = LocoInfo::parseBody(m_layout, file.readAll());
    if (!parsed.ok) {
        QMessageBox::warning(this, windowTitle(), tr("%1 is not a LOCO_INFO file:\n%2").arg(path, parsed.error));
        return;
    }
    QString question = tr("Load %1 into \"%2\"? Its current values are replaced.")
                           .arg(QFileInfo(path).fileName(), m_config.name);
    if (!parsed.crcOk) {
        question = tr("The file's loco_info_crc is %1 but its contents give %2: it was damaged or edited "
                      "by hand.\n\n").arg(LocoInfo::crcText(parsed.storedCrc), LocoInfo::crcText(parsed.computedCrc))
                   + question + tr("\n\nThe CRC is recomputed on send.");
    }
    const auto answer = QMessageBox::question(this, windowTitle(), question,
                                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes) {
        applyValues(parsed.values);
        m_status->ok(tr("Loaded %1").arg(QFileInfo(path).fileName()));
    }
}

bool LocoConfigWindow::writeBin(const QString &path, const QByteArray &body)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, windowTitle(), tr("Could not write %1:\n%2").arg(path, file.errorString()));
        return false;
    }
    file.write(body);
    return true;
}

void LocoConfigWindow::exportBin()
{
    const QString blocker = sendBlocker();
    const QByteArray body = currentBody();
    if (body.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), blocker);
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Export loco_info.bin"),
                                                      QStringLiteral("loco_info.bin"),
                                                      tr("LOCO_INFO (*.bin)"));
    if (path.isEmpty()) {
        return;
    }
    if (writeBin(path, body)) {
        m_status->ok(tr("Wrote %1 (%2 bytes, CRC %3)")
                         .arg(QFileInfo(path).fileName()).arg(body.size())
                         .arg(LocoInfo::crcText(LocoInfo::crc32(body.left(body.size() - 4)))));
    }
}

// =============================================================================
//  Summary and sending
// =============================================================================

QByteArray LocoConfigWindow::currentBody() const
{
    return LocoInfo::encodeBody(m_layout, m_config.values);
}

QString LocoConfigWindow::sendBlocker() const
{
    const QStringList problems = LocoInfo::validate(m_layout, m_config.values);
    if (!problems.isEmpty()) {
        return problems.first();
    }
    return LocoInfo::targetProblem(m_config.targets);
}

void LocoConfigWindow::refreshSummary()
{
    if (m_summary == nullptr || m_model == nullptr) {
        return;
    }
    const QByteArray body = currentBody();
    if (!body.isEmpty()) {
        const quint32 crc = LocoInfo::crc32(body.left(body.size() - 4));
        // Short enough to share its line with vcc_crc (session 135).
        m_summary->setToolTip(tr("The %1-byte datagram: message header, then the LOCO_INFO body")
                                  .arg(body.size() + LocoInfo::kHeaderBytes));
        m_summary->setText(tr("%1 B · src %2 → dest %3 · msg %4 · loco_info_crc %5")
                               .arg(body.size() + LocoInfo::kHeaderBytes)
                               .arg(LocoInfo::kSourceId).arg(LocoInfo::kDestId).arg(LocoInfo::kMessageId)
                               .arg(LocoInfo::crcText(crc)));
    } else {
        m_summary->setText(tr("Cannot be packed — see below"));
    }

    // vcc_crc is set by hand and has to match the VCC build on the loco, so
    // it is repeated here, where the operator is about to press Send.
    const LocoInfo::Field *vccField = m_layout.field(QStringLiteral("vcc_crc"));
    if (vccField != nullptr) {
        m_vccCrc->setText(tr("vcc_crc %1 — set by hand: must match this loco's VCC build")
                              .arg(LocoInfo::formatValue(*vccField, m_config.values.value(vccField->key))));
        m_vccCrc->setStyleSheet(UiColor::style(FlasherStyle::attention()));
    }

    if (m_config.lastSentBody.isEmpty()) {
        m_lastSent->setText(tr("Not sent from this configuration yet"));
    } else {
        const QStringList changed = LocoInfo::changedKeys(m_layout, lastSentValues(), m_config.values);
        QString changes = tr("no changes since");
        if (changed.size() == 1) {
            changes = tr("1 field changed since");
        } else if (changed.size() > 1) {
            changes = tr("%1 fields changed since").arg(changed.size());
        }
        m_lastSent->setText(tr("Last sent %1 to %2 · %3")
                                .arg(m_config.lastSentAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                                     m_config.lastSentTarget, changes));
    }

    const QString blocker = sendBlocker();
    m_sendButton->setEnabled(blocker.isEmpty());
    refreshLiveCheck();
    int usedTargets = 0;
    for (const LocoInfo::SendTarget &target : m_config.targets) {
        if (target.isUsed()) {
            ++usedTargets;
        }
    }
    if (usedTargets > 1) {
        m_sendButton->setText(tr("Send to %1 VCCs…").arg(usedTargets));
    } else {
        m_sendButton->setText(tr("Send to VCC…"));
    }
    m_blocker->setVisible(!blocker.isEmpty());   // an empty line still cost its height and spacing
    if (blocker.isEmpty()) {
        m_blocker->clear();
    } else {
        m_blocker->setText(QStringLiteral("⚠ ") + blocker);
        m_blocker->setStyleSheet(UiColor::style(FlasherStyle::attention()));
    }
    refreshGroupCounts();
}

void LocoConfigWindow::refreshGroupCounts()
{
    // Each group shows how many of its fields differ from the defaults, so
    // a hand-edit in a group the operator is not looking at stays visible.
    QHash<QString, int> changedPerGroup;
    int changedFromDefault = 0;
    int changedSinceSent = 0;
    const LocoInfo::Values sent = lastSentValues();
    for (const LocoInfo::Field &field : m_layout.fields()) {
        if (field.isCrc) {
            continue;
        }
        const QVariant value = m_config.values.value(field.key);
        if (!LocoInfo::sameValue(field, value, m_defaults.value(field.key))) {
            ++changedFromDefault;
            changedPerGroup[m_presentation.groupOf(field.key)] += 1;
        }
        if (!sent.isEmpty() && !LocoInfo::sameValue(field, value, sent.value(field.key))) {
            ++changedSinceSent;
        }
    }
    for (int row = 0; row < m_groups->count(); ++row) {
        QListWidgetItem *item = m_groups->item(row);
        const QString key = item->data(Qt::UserRole).toString();
        int count = changedPerGroup.value(key);
        if (key == LocoFieldFilter::allFields()) {
            count = 0;
        } else if (key == LocoFieldFilter::changedFromDefault()) {
            count = changedFromDefault;
        } else if (key == LocoFieldFilter::changedSinceSent()) {
            count = changedSinceSent;
        }
        if (count > 0) {
            item->setText(QStringLiteral("%1  (%2)").arg(key).arg(count));
        } else {
            item->setText(key);
        }
    }
}

void LocoConfigWindow::onGroupChanged()
{
    QListWidgetItem *item = m_groups->currentItem();
    if (item != nullptr) {
        m_filter->setGroup(item->data(Qt::UserRole).toString());
    }
}

void LocoConfigWindow::setTarget(const QString &ip, quint16 port)
{
    QVector<LocoInfo::SendTarget> targets(LocoInfo::kMaxTargets);
    targets[0].ip = ip;
    targets[0].port = port;
    setTargets(targets);
}

void LocoConfigWindow::setTargets(const QVector<LocoInfo::SendTarget> &targets)
{
    for (int row = 0; row < LocoInfo::kMaxTargets; ++row) {
        LocoInfo::SendTarget target = targets.value(row);
        if (row >= targets.size()) {
            target.ip.clear();
        }
        m_targetOn.at(row)->setChecked(target.enabled);
        m_targetIps.at(row)->setText(target.ip);
        m_targetPorts.at(row)->setValue(target.port);
    }
}

// =============================================================================
//  Live check: what the loco actually holds
// =============================================================================

void LocoConfigWindow::setLiveSource(MessageDispatcher *dispatcher)
{
    if (dispatcher == nullptr || m_layout.bodySize() == 0) {
        return;
    }
    connect(dispatcher, &MessageDispatcher::entryAppended, this,
            [this](QString, QSharedPointer<LogEntry> entry) {
                if (entry && entry->text.startsWith(QLatin1String("@linfo"))) {
                    observeCaptureLine(entry->text);
                }
            });
}

void LocoConfigWindow::observeCaptureLine(const QString &line)
{
    if (!isUsable()) {
        return;
    }
    const CaptureLine capture = CaptureDecoder::parseLine(line);
    if (!capture.valid || capture.type != CapType::Linfo) {
        return;
    }
    const LocoInfo::Parsed parsed = LocoInfo::parseBody(m_layout, capture.bytes);
    if (!parsed.ok) {
        return;   // another LOCO_INFO version: nothing to compare field by field
    }
    LiveInfo live;
    live.values = parsed.values;
    live.seenMs = QDateTime::currentMSecsSinceEpoch();
    live.crcOk = parsed.crcOk;
    // How often this loco prints it: the timeout after a send is judged
    // against the loco's own rhythm, not a guess.
    const auto previous = m_live.constFind(capture.key());
    if (previous != m_live.constEnd() && live.seenMs > previous->seenMs) {
        m_linfoIntervalMs.insert(capture.key(), live.seenMs - previous->seenMs);
    }
    m_live.insert(capture.key(), live);
    refreshLiveCheck();
}

QString LocoConfigWindow::liveCheckText() const
{
    return m_liveStatus->text();
}

void LocoConfigWindow::refreshLiveCheck()
{
    if (m_liveStatus == nullptr) {
        return;
    }
    // Which loco: the one reporting this configuration's loco_unit_id (the
    // unit id last SENT, if it was sent: that is what the loco should hold).
    const LocoInfo::Values sent = lastSentValues();
    LocoInfo::Values reference = m_config.values;
    if (!sent.isEmpty()) {
        reference = sent;
    }
    const qint64 unitId = reference.value(QStringLiteral("loco_unit_id")).toLongLong();
    QString key;
    LiveInfo live;
    for (auto it = m_live.constBegin(); it != m_live.constEnd(); ++it) {
        if (it->values.value(QStringLiteral("loco_unit_id")).toLongLong() == unitId && it->seenMs > live.seenMs) {
            key = it.key();
            live = it.value();
        }
    }
    m_liveKeyShown = key;
    m_liveLoad->hide();
    m_liveStatus->setToolTip(QString());

    if (live.seenMs == 0) {
        if (awaitingOverdue(QString())) {
            m_liveStatus->setText(tr("⚠ No @linfo from loco_unit_id %1 in the %2 s since the send. The VCC takes the "
                                     "config only in application mode (not the updater), and prints @linfo only "
                                     "if its capture reaches this PC.")
                                      .arg(unitId).arg(sinceSendMs() / 1000));
            m_liveStatus->setStyleSheet(UiColor::warningStyle());
            return;
        }
        m_liveStatus->setText(tr("Loco check: no @linfo from loco_unit_id %1 received yet — the VCC prints "
                                 "it periodically; the check appears when one arrives.").arg(unitId));
        m_liveStatus->setStyleSheet(UiColor::mutedStyle());
        return;
    }

    // "1 field" / "3 fields": read on screen, so no "field(s)".
    auto fieldCount = [this](int count) {
        if (count == 1) {
            return tr("1 field");
        }
        return tr("%1 fields").arg(count);
    };
    const qint64 ageMs = QDateTime::currentMSecsSinceEpoch() - live.seenMs;
    const QString seen = tr("seen %1 s ago").arg(ageMs / 1000);
    auto differences = [this, &live](const LocoInfo::Values &expected) {
        QStringList lines;
        for (const QString &field : LocoInfo::changedKeys(m_layout, expected, live.values)) {
            const LocoInfo::Field *f = m_layout.field(field);
            const QString format = m_presentation.formats.value(field);
            lines.append(tr("%1: expected %2, loco has %3")
                             .arg(field, LocoInfo::formatValue(*f, expected.value(field), format),
                                  LocoInfo::formatValue(*f, live.values.value(field), format)));
        }
        return lines;
    };

    if (!live.crcOk) {
        m_liveStatus->setText(tr("✗ Loco %1 reports a LOCO_INFO whose loco_info_crc does not match its "
                                 "contents (%2)").arg(key, seen));
        m_liveStatus->setStyleSheet(UiColor::errorStyle());
        return;
    }

    if (!sent.isEmpty()) {
        const QStringList diff = differences(sent);
        const bool afterSend = !m_config.lastSentAt.isValid()
                            || live.seenMs >= m_config.lastSentAt.toMSecsSinceEpoch();
        if (afterSend) {
            recordVerification(key, live, diff);
        }
        if (diff.isEmpty()) {
            m_liveStatus->setText(tr("✓ Loco %1 holds exactly what was last sent (%2)").arg(key, seen));
            m_liveStatus->setStyleSheet(UiColor::okStyle());
            return;
        }
        // The last @linfo is from BEFORE the send: nothing to judge yet.
        if (m_config.lastSentAt.isValid() && live.seenMs < m_config.lastSentAt.toMSecsSinceEpoch()) {
            if (awaitingOverdue(key)) {
                m_liveStatus->setText(tr("⚠ No @linfo from loco %1 in the %2 s since the send (it prints one about "
                                         "every %3 s). The VCC takes the config only in application mode, not the "
                                         "updater.")
                                          .arg(key).arg(sinceSendMs() / 1000)
                                          .arg(qMax<qint64>(1, m_linfoIntervalMs.value(key) / 1000)));
                m_liveStatus->setStyleSheet(UiColor::warningStyle());
                m_liveStatus->setToolTip(diff.join(QLatin1Char('\n')));
                return;
            }
            m_liveStatus->setText(tr("Waiting for loco %1's next @linfo since the send at %2 (the last one, "
                                     "from before the send, differs in %3)")
                                      .arg(key, m_config.lastSentAt.toLocalTime().toString(QStringLiteral("HH:mm:ss")),
                                           fieldCount(diff.size())));
            m_liveStatus->setStyleSheet(UiColor::mutedStyle());
            m_liveStatus->setToolTip(diff.join(QLatin1Char('\n')));
            return;
        }
        m_liveStatus->setText(tr("✗ Loco %1 holds something else: %2 differ from the last send (%3)")
                                  .arg(key, fieldCount(diff.size()), seen));
        m_liveStatus->setStyleSheet(UiColor::errorStyle());
        m_liveStatus->setToolTip(diff.join(QLatin1Char('\n')));
        m_liveLoad->show();
        return;
    }

    const QStringList diff = differences(m_config.values);
    if (diff.isEmpty()) {
        m_liveStatus->setText(tr("✓ Loco %1 holds exactly this configuration (%2)").arg(key, seen));
        m_liveStatus->setStyleSheet(UiColor::okStyle());
        return;
    }
    m_liveStatus->setText(tr("Loco %1 differs from this configuration in %2 (%3) — not sent from here yet")
                              .arg(key, fieldCount(diff.size()), seen));
    m_liveStatus->setStyleSheet(UiColor::warningStyle());
    m_liveStatus->setToolTip(diff.join(QLatin1Char('\n')));
    m_liveLoad->show();
}

qint64 LocoConfigWindow::sinceSendMs() const
{
    if (!m_config.lastSentAt.isValid()) return -1;
    return QDateTime::currentMSecsSinceEpoch() - m_config.lastSentAt.toMSecsSinceEpoch();
}

bool LocoConfigWindow::awaitingOverdue(const QString &key) const
{
    // Waiting on a confirmation, and longer than the loco's own rhythm
    // allows: three of its intervals, and never under a minute (a loco not
    // heard twice yet has no rhythm to go on).
    if (!m_config.lastSentAt.isValid() || m_verifiedSentAt == m_config.lastSentAt) return false;
    // This loco's rhythm; with no key yet (never heard), none.
    const qint64 interval = m_linfoIntervalMs.value(key, 0);
    return sinceSendMs() > qMax<qint64>(m_confirmMinWaitMs, interval * 3);
}

void LocoConfigWindow::recordVerification(const QString &key, const LiveInfo &live, const QStringList &differences)
{
    if (!m_config.lastSentAt.isValid() || m_verifiedSentAt == m_config.lastSentAt) return;
    m_verifiedSentAt = m_config.lastSentAt;
    LocoInfo::Verification v;
    v.sentAt = m_config.lastSentAt;
    v.seenAt = QDateTime::fromMSecsSinceEpoch(live.seenMs).toUTC();
    v.locoKey = key;
    v.crcOk = live.crcOk;
    v.match = live.crcOk && differences.isEmpty();
    v.differences = differences;
    if (m_history != nullptr) {
        m_history->appendVerification(v);
    }
    const QString text = v.match
        ? tr("Loco %1 confirmed the configuration \"%2\" sent at %3")
              .arg(key, m_config.name, m_config.lastSentAt.toLocalTime().toString(QStringLiteral("HH:mm:ss")))
        : tr("Loco %1 holds something else than the configuration \"%2\" sent at %3: %4 field(s) differ")
              .arg(key, m_config.name, m_config.lastSentAt.toLocalTime().toString(QStringLiteral("HH:mm:ss")))
              .arg(differences.size());
    emit verified(text, v.match, differences.join(QLatin1Char('\n')));
}

bool LocoConfigWindow::sendNow(bool confirm)
{
    if (!isUsable()) {
        return false;
    }
    const QString blocker = sendBlocker();
    if (!blocker.isEmpty()) {
        m_status->fail(blocker);
        return false;
    }
    const QByteArray body = currentBody();
    QVector<LocoInfo::SendTarget> targets;
    QStringList targetTexts;
    for (const LocoInfo::SendTarget &target : m_config.targets) {
        if (target.isUsed()) {
            targets.append(target);
            targetTexts.append(target.text());
        }
    }

    if (confirm) {
        // Say exactly what is about to change on the loco, relative to the
        // last thing this configuration sent. The VCC will not confirm it, so
        // this is the operator's last look.
        QString summary;
        QStringList details;
        if (m_config.lastSentBody.isEmpty()) {
            summary = tr("This configuration has not been sent before: all %1 fields go out.")
                          .arg(m_layout.fields().size() - 1);
        } else {
            const LocoInfo::Values sent = lastSentValues();
            const QStringList changed = LocoInfo::changedKeys(m_layout, sent, m_config.values);
            for (const QString &key : changed) {
                const LocoInfo::Field *field = m_layout.field(key);
                const QString format = m_presentation.formats.value(key);
                details.append(QStringLiteral("%1: %2 → %3")
                                   .arg(key, LocoInfo::formatValue(*field, sent.value(key), format),
                                        LocoInfo::formatValue(*field, m_config.values.value(key), format)));
            }
            if (changed.isEmpty()) {
                summary = tr("Nothing has changed since the last send (%1): the same bytes go out again.")
                              .arg(m_config.lastSentAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
            } else {
                if (changed.size() == 1) {
                    summary = tr("1 field changed since the last send:");
                } else {
                    summary = tr("%1 fields changed since the last send:").arg(changed.size());
                }
                const QStringList shown = details.mid(0, kChangesShownInline);
                summary += QLatin1Char('\n') + shown.join(QLatin1Char('\n'));
                if (details.size() > shown.size()) {
                    summary += tr("\n… and %1 more (Show Details)").arg(details.size() - shown.size());
                }
            }
        }
        QString question = tr("Send \"%1\" to the VCC at %2?").arg(m_config.name, targetTexts.first());
        QString sameForAll;
        if (targets.size() > 1) {
            question = tr("Send \"%1\" to %2 VCCs?\n\n%3")
                           .arg(m_config.name).arg(targets.size())
                           .arg(QStringLiteral("•  ") + targetTexts.join(QStringLiteral("\n•  ")));
            // Bulk send gives every target the SAME bytes. Right for the
            // cards of one loco; wrong for four different locos. Say what
            // they all get, so the operator can tell which this is.
            const LocoInfo::Field *unitField = m_layout.field(QStringLiteral("loco_unit_id"));
            if (unitField != nullptr) {
                sameForAll = tr("\n\nAll %1 receive the same configuration, including loco_unit_id %2.")
                                 .arg(targets.size())
                                 .arg(LocoInfo::formatValue(*unitField, m_config.values.value(unitField->key)));
            }
        }
        QMessageBox box(QMessageBox::Question, windowTitle(), question,
                        QMessageBox::Yes | QMessageBox::No, this);
        const LocoInfo::Field *vccField = m_layout.field(QStringLiteral("vcc_crc"));
        QString vccLine;
        if (vccField != nullptr) {
            vccLine = tr("\n\nvcc_crc %1 · loco_info_crc %2")
                          .arg(LocoInfo::formatValue(*vccField, m_config.values.value(vccField->key)),
                               LocoInfo::crcText(LocoInfo::crc32(body.left(body.size() - 4))));
        }
        box.setInformativeText(summary + vccLine + sameForAll
                               + tr("\n\nThe VCC does not reply; nothing will confirm it arrived."));
        if (!details.isEmpty()) {
            box.setDetailedText(details.join(QLatin1Char('\n')));
        }
        box.setDefaultButton(QMessageBox::No);
        if (box.exec() != QMessageBox::Yes) {
            return false;
        }
    }

    // One target at a time, so each result is known: UdpSender's
    // multi-target send only says whether ANY got through. Its error()
    // signal names the reason for the one that just failed.
    const QByteArray datagram = LocoInfo::message(body);
    QString lastError;
    const QMetaObject::Connection errorCapture =
        connect(m_sender, &UdpSender::error, this, [&lastError](const QString &what) { lastError = what; });
    QStringList sentTo;
    QStringList failures;
    for (const LocoInfo::SendTarget &target : targets) {
        lastError.clear();
        if (m_sender->sendOnce(datagram, target.ip.trimmed(), target.port)) {
            sentTo.append(target.text());
        } else {
            QString reason = lastError;
            if (reason.isEmpty()) {
                reason = tr("send failed");
            }
            failures.append(tr("%1 (%2)").arg(target.text(), reason));
        }
    }
    disconnect(errorCapture);

    if (sentTo.isEmpty()) {
        m_status->fail(tr("Nothing sent: %1").arg(failures.join(QStringLiteral("; "))));
        return false;
    }

    // Record it: in the configuration (for "changed since last send") and in
    // the append-only history, one record per target that was sent to.
    const QDateTime now = QDateTime::currentDateTimeUtc();
    m_config.lastSentBody = body;
    m_config.lastSentAt = now;
    m_config.lastSentTarget = sentTo.join(QStringLiteral(", "));
    saveNow();

    bool recorded = true;
    for (const QString &target : sentTo) {
        LocoInfo::SendRecord record;
        record.when = now;
        record.operatorName = LocoInfo::currentOperatorName();
        record.configName = m_config.name;
        record.target = target;
        record.crc = LocoInfo::crc32(body.left(body.size() - 4));
        record.vccCrc = static_cast<quint32>(m_config.values.value(QStringLiteral("vcc_crc")).toLongLong());
        record.body = body;
        if (!m_history->append(record)) {
            recorded = false;
        }
    }

    m_model->setConfig(m_config.values, m_defaults, lastSentValues());
    m_filter->refresh();
    refreshSummary();
    if (m_historyDialog) {
        m_historyDialog->reload();
    }
    const QString when = now.toLocalTime().toString(QStringLiteral("HH:mm:ss"));
    if (!failures.isEmpty()) {
        m_status->fail(tr("Sent to %1 of %2 at %3 — FAILED: %4")
                           .arg(sentTo.size()).arg(targets.size()).arg(when)
                           .arg(failures.join(QStringLiteral("; "))));
    } else if (!recorded) {
        m_status->fail(tr("Sent, but NOT recorded in the history: %1").arg(m_history->lastError()));
    } else if (sentTo.size() == 1) {
        m_status->ok(tr("Sent %1 bytes to %2 at %3 — no reply is expected")
                         .arg(datagram.size()).arg(sentTo.first()).arg(when));
    } else {
        m_status->ok(tr("Sent %1 bytes to %2 VCCs (%3) at %4 — no reply is expected")
                         .arg(datagram.size()).arg(sentTo.size())
                         .arg(sentTo.join(QStringLiteral(", ")), when));
    }
    emit sent();
    return true;
}

// =============================================================================
//  History and closing
// =============================================================================

void LocoConfigWindow::openHistory()
{
    if (!m_historyDialog) {
        m_historyDialog = new LocoConfigHistoryDialog(m_history->filePath(), &m_layout, this);
        m_historyDialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_historyDialog, &LocoConfigHistoryDialog::loadRequested, this,
                [this](const QByteArray &body, const QString &description) {
                    const LocoInfo::Parsed parsed = LocoInfo::parseBody(m_layout, body);
                    if (!parsed.ok) {
                        return;
                    }
                    const auto answer = QMessageBox::question(
                        this, windowTitle(),
                        tr("Load the values sent %1 into \"%2\"? Its current values are replaced.")
                            .arg(description, m_config.name),
                        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                    if (answer == QMessageBox::Yes) {
                        applyValues(parsed.values);
                        m_status->ok(tr("Loaded the values sent %1").arg(description));
                    }
                });
    } else {
        m_historyDialog->reload();
    }
    m_historyDialog->show();
    m_historyDialog->raise();
    m_historyDialog->activateWindow();
}

void LocoConfigWindow::closeEvent(QCloseEvent *event)
{
    if (m_store != nullptr && m_saveTimer.isActive()) {
        saveNow();
    }
    WindowGeometry::save(this, QStringLiteral("locoConfiguration"));
    QMainWindow::closeEvent(event);
}

// =============================================================================
//  Session 94: export / import configurations (another PC)
// =============================================================================

QStringList LocoConfigWindow::configNames() const { return m_store->names(); }

LocoInfo::LocoConfig LocoConfigWindow::configNamed(const QString &name) const { return m_store->config(name); }

bool LocoConfigWindow::exportConfigsTo(const QString &path, const QStringList &names, QString *error)
{
    saveNow();                                  // what is on screen is what goes
    QList<LocoInfo::LocoConfig> chosen;
    for (const LocoInfo::LocoConfig &c : m_store->all())
        if (names.contains(c.name)) chosen << c;
    if (chosen.isEmpty()) {
        if (error) *error = tr("nothing chosen to export");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(LocoInfo::exportConfigs(m_layout, chosen)) < 0 || !file.commit()) {
        if (error) *error = tr("cannot write %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
        return false;
    }
    return true;
}

QStringList LocoConfigWindow::importConfigsFrom(const QString &path,
                                                const std::function<ImportClash(const QString &, bool *)> &onClash,
                                                QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = tr("cannot open %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
        return {};
    }
    const LocoInfo::ImportedConfigs in = LocoInfo::importConfigs(m_layout, m_defaults, file.readAll());
    if (!in.ok) {
        if (error) *error = in.error;
        return {};
    }
    saveNow();
    const QString current = m_config.name;
    QStringList stored;
    for (const LocoInfo::LocoConfig &c : in.configs) {
        ImportClash how = ImportClash::KeepBoth;
        if (m_store->contains(c.name)) {
            bool stop = false;
            how = onClash(c.name, &stop);
            if (stop) break;
        }
        const QString name = m_store->importConfig(c, how);
        if (!name.isEmpty()) stored << name;
    }
    if (!stored.isEmpty()) {
        m_store->save();
        reloadConfigCombo();
        // The one on screen was replaced: show the imported values.
        loadConfig(m_store->contains(current) ? current : m_store->activeName());
    }
    if (!in.droppedKeys.isEmpty())
        m_status->warn(tr("The file had fields this schema does not (ignored): %1").arg(in.droppedKeys.join(QStringLiteral(", "))));
    return stored;
}

void LocoConfigWindow::exportConfigsDialog()
{
    const QStringList names = ProfileIo::pickNames(this, tr("Export configurations"), tr("configurations"),
                                                   m_store->names(), { m_config.name });
    if (names.isEmpty()) return;
    const QString suggested = QDir(QDir::homePath()).filePath(
        names.size() == 1 ? QString(names.first()).replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]+")), QStringLiteral("_")) + QStringLiteral(".dlloco")
                          : QStringLiteral("loco_configs.dlloco"));
    const QString path = QFileDialog::getSaveFileName(this, tr("Export configurations"), suggested,
                                                      tr("DLConsole loco configurations (*.dlloco);;JSON (*.json)"));
    if (path.isEmpty()) return;
    QString error;
    if (exportConfigsTo(path, names, &error))
        m_status->ok(tr("Exported %n configuration(s) to %1", "", names.size()).arg(QDir::toNativeSeparators(path)));
    else
        m_status->warn(tr("Export failed: %1").arg(error));
}

void LocoConfigWindow::importConfigsDialog()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import configurations"), QDir::homePath(),
                                                      tr("DLConsole loco configurations (*.dlloco *.json);;All files (*)"));
    if (path.isEmpty()) return;
    bool haveAnswer = false;
    ImportClash remembered = ImportClash::KeepBoth;
    QString error;
    const QStringList stored = importConfigsFrom(path, [&](const QString &name, bool *stop) {
        if (haveAnswer) return remembered;
        bool same = false, cancelled = false;
        const ImportClash how = ProfileIo::askClash(this, tr("configuration"), name, &same, &cancelled);
        *stop = cancelled;
        if (same) { haveAnswer = true; remembered = how; }
        return how;
    }, &error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Import configurations"), tr("Nothing was imported: %1").arg(error));
        return;
    }
    if (stored.isEmpty()) m_status->warn(tr("Nothing was imported"));
    else m_status->ok(tr("Imported %1").arg(stored.join(QStringLiteral(", "))));
}
