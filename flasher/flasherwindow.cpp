#include "flasherwindow.h"
#include "undolog.h"

#include <QPointer>

#include "blockmapwidget.h"
#include "flasherflashingpage.h"
#include "flasherhistorydialog.h"
#include "flasherprofiledialog.h"
#include "flasherqueuemodel.h"
#include "flasherqueuepage.h"
#include "flasherstyle.h"
#include "flashersummarypage.h"
#include "flashworker.h"
#include "sendguard.h"
#include "settings.h"
#include "uicolors.h"
#include "uistyle.h"
#include "windowgeometry.h"

#include <QButtonGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QStackedWidget>
#include <QTextStream>
#include <QThread>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

const char kProfilesFileName[] = "flasher_profiles.json";
const char kHistoryFileName[]  = "flash_history.jsonl";
const char kModeSettingKey[]   = "flasher/mode";         // "engineer" | "operator"

// Beside dlconsole.ini, unless the caller (a test) says otherwise. Same
// directory as the rest of the console's state, so one folder backs up all
// of it.
QString resolveDataDirectory(const QString &requested)
{
    if (!requested.isEmpty()) {
        return requested;
    }
    return QFileInfo(Settings::iniPath()).absolutePath();
}

// A combo entry that is not a profile but an action.
const int kNewProfileMarker = -1;

}  // namespace

FlasherWindow::FlasherWindow(QWidget *parent, const QString &dataDirectory)
    : QMainWindow(parent)
    , m_dataDirectory(resolveDataDirectory(dataDirectory))
    , m_profiles(QDir(m_dataDirectory).filePath(QLatin1String(kProfilesFileName)))
    , m_history(QDir(m_dataDirectory).filePath(QLatin1String(kHistoryFileName)))
{
    setWindowTitle(tr("Firmware Flasher"));
    WindowGeometry::makeResizableWindow(this);
    resize(1280, 800);
    WindowGeometry::restore(this, QStringLiteral("firmwareFlasher"));

    // Before any connect: the queued signals carry the engine's structs.
    FlashWorker::registerMetaTypes();

    // ---- persisted state ------------------------------------------------------
    if (!m_profiles.load()) {
        // Do not silently replace someone's profiles with a default: say so.
        // The broken file is left untouched until the next save.
        QMessageBox::warning(this, windowTitle(),
                             tr("The flasher profiles could not be read and defaults are in use:\n\n%1")
                                 .arg(m_profiles.lastError()));
    }
    {
        QSettings settings(Settings::iniPath(), QSettings::IniFormat);
        if (settings.value(QLatin1String(kModeSettingKey)).toString() == QLatin1String("operator")) {
            m_mode = Flasher::Mode::Operator;
        } else {
            m_mode = Flasher::Mode::Engineer;
        }
    }

    // ---- layout -------------------------------------------------------------------
    auto *central = new QWidget(this);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(buildAppBar());

    m_stack = new QStackedWidget(central);
    m_queuePage = new FlasherQueuePage(m_stack);
    m_flashingPage = new FlasherFlashingPage(m_stack);
    m_summaryPage = new FlasherSummaryPage(m_stack);
    m_stack->addWidget(m_queuePage);      // QueuePageIndex
    m_stack->addWidget(m_flashingPage);   // FlashingPageIndex
    m_stack->addWidget(m_summaryPage);    // SummaryPageIndex
    centralLayout->addWidget(m_stack, 1);
    setCentralWidget(central);

    // ---- worker thread ------------------------------------------------------------
    m_thread = new QThread(this);
    m_worker = new FlashWorker;           // no parent: it is moved to the thread
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &FlashWorker::progressChanged, this, &FlasherWindow::onProgress);
    connect(m_worker, &FlashWorker::bitmapChanged, m_flashingPage->blockMap(), &BlockMapWidget::setBitmap);
    connect(m_worker, &FlashWorker::logLine, this, &FlasherWindow::onLogLine);
    connect(m_worker, &FlashWorker::cardFinished, this, &FlasherWindow::onCardFinished);
    m_thread->start();

    // ---- page signals ---------------------------------------------------------------
    connect(m_queuePage, &FlasherQueuePage::flashRequested, this, &FlasherWindow::onFlashRequested);
    connect(m_queuePage, &FlasherQueuePage::targetChanged, this, &FlasherWindow::refreshAppBarTarget);
    connect(m_flashingPage, &FlasherFlashingPage::abortRequested, this, &FlasherWindow::onAbortRequested);
    connect(m_summaryPage, &FlasherSummaryPage::exportReportRequested, this, &FlasherWindow::exportReport);
    connect(m_summaryPage, &FlasherSummaryPage::retryRequested, this, &FlasherWindow::tryAgain);
    connect(m_summaryPage, &FlasherSummaryPage::openHistoryRequested, this, &FlasherWindow::openHistory);
    connect(m_summaryPage, &FlasherSummaryPage::backToQueueRequested, this, [this]() { showPage(QueuePageIndex); });

    // ---- initial state -------------------------------------------------------------
    if (m_mode == Flasher::Mode::Operator) {
        m_operatorButton->setChecked(true);
    } else {
        m_engineerButton->setChecked(true);
    }
    m_queuePage->setMode(m_mode);
    reloadProfileCombo();
    applyActiveProfile();
    setFlashingState(false);
    showPage(QueuePageIndex);

    UiColor::onThemeChange(this, [this]() { restyleAppBar(); });
}

FlasherWindow::~FlasherWindow()
{
    // The window can only be destroyed while idle (closeEvent refuses
    // otherwise), but a parent tearing everything down at application exit
    // does not go through closeEvent. Cancel whatever is running so run()
    // returns, then stop the thread and wait for it.
    if (m_flashing && m_worker != nullptr) {
        m_worker->requestCancel();
    }
    m_thread->quit();
    m_thread->wait();
}

QString FlasherWindow::profilesPath() const
{
    return m_profiles.filePath();
}

QString FlasherWindow::historyPath() const
{
    return m_history.filePath();
}

int FlasherWindow::currentPage() const
{
    return m_stack->currentIndex();
}

void FlasherWindow::showPage(int page)
{
    m_stack->setCurrentIndex(page);
}

// =============================================================================
//  App bar
// =============================================================================

QWidget *FlasherWindow::buildAppBar()
{
    m_appBar = new QFrame(this);
    m_appBar->setObjectName(QStringLiteral("flasherAppBar"));
    m_appBar->setFixedHeight(56);
    auto *barLayout = new QHBoxLayout(m_appBar);
    barLayout->setContentsMargins(18, 0, 14, 0);
    barLayout->setSpacing(12);

    m_appIcon = new QLabel(QStringLiteral("▣"), m_appBar);
    m_appIcon->setFont(FlasherStyle::scaledFont(m_appIcon->font(), 1.5, true));
    barLayout->addWidget(m_appIcon);
    m_appTitle = new QLabel(tr("Kavach Flasher"), m_appBar);
    m_appTitle->setFont(FlasherStyle::scaledFont(m_appTitle->font(), 1.15, true));
    barLayout->addWidget(m_appTitle);
    barLayout->addSpacing(12);

    m_profileLabel = new QLabel(tr("Profile"), m_appBar);
    barLayout->addWidget(m_profileLabel);
    m_profileCombo = new QComboBox(m_appBar);
    m_profileCombo->setMinimumWidth(200);
    m_profileCombo->setMinimumHeight(34);
    connect(m_profileCombo, QOverload<int>::of(&QComboBox::activated), this, &FlasherWindow::onProfileChosen);
    barLayout->addWidget(m_profileCombo);

    // Engineer / Operator segmented toggle. Two exclusive checkable buttons,
    // which is what a segmented control is in Qt terms.
    m_engineerButton = new QToolButton(m_appBar);
    m_engineerButton->setText(tr("Engineer"));
    m_engineerButton->setCheckable(true);
    m_engineerButton->setToolTip(tr("Engineer: transfer tuning is editable; an image whose name does not\n"
                                    "match its card can be flashed after a confirmation."));
    m_operatorButton = new QToolButton(m_appBar);
    m_operatorButton->setText(tr("Operator"));
    m_operatorButton->setCheckable(true);
    m_operatorButton->setToolTip(tr("Operator: tuning is locked, and an image whose name does not match\n"
                                    "its card, or no adapter on the board's subnet, blocks the flash."));
    m_modeGroup = new QButtonGroup(this);
    m_modeGroup->setExclusive(true);
    m_modeGroup->addButton(m_engineerButton);
    m_modeGroup->addButton(m_operatorButton);
    connect(m_engineerButton, &QToolButton::toggled, this, &FlasherWindow::onModeChanged);
    auto *segment = new QHBoxLayout();
    segment->setSpacing(0);
    segment->addWidget(m_engineerButton);
    segment->addWidget(m_operatorButton);
    barLayout->addLayout(segment);

    barLayout->addStretch(1);

    // Shown only while flashing: the caution, and where it is going.
    m_flashingBadge = new QLabel(tr("Flashing — keep the chassis powered"), m_appBar);
    m_flashingBadge->setFont(FlasherStyle::scaledFont(m_flashingBadge->font(), 1.0, true));
    barLayout->addWidget(m_flashingBadge);
    m_targetLabel = new QLabel(m_appBar);
    m_targetLabel->setFont(UiStyle::monoFont());
    barLayout->addWidget(m_targetLabel);

    m_historyButton = new QToolButton(m_appBar);
    m_historyButton->setText(tr("History"));
    m_historyButton->setToolTip(tr("Every card flashed from this PC: who, what, when, which CRC"));
    connect(m_historyButton, &QToolButton::clicked, this, &FlasherWindow::openHistory);
    barLayout->addWidget(m_historyButton);

    m_gearButton = new QToolButton(m_appBar);
    m_gearButton->setText(QStringLiteral("⚙"));
    m_gearButton->setToolTip(tr("Edit this profile"));
    connect(m_gearButton, &QToolButton::clicked, this, &FlasherWindow::editProfile);
    barLayout->addWidget(m_gearButton);

    restyleAppBar();
    return m_appBar;
}

void FlasherWindow::restyleAppBar()
{
    const QPalette pal = palette();
    QColor background = pal.color(QPalette::AlternateBase);
    QColor foreground = pal.color(QPalette::Text);
    QColor rule = FlasherStyle::border();

    // While flashing, the whole bar takes the caution banner's colours: the
    // same amber strip Packet Maker uses for "this transmits to live
    // equipment", which is exactly what is happening.
    if (m_flashing) {
        background = UiColor::bannerBg();
        foreground = UiColor::bannerFg();
        rule = UiColor::bannerBorder();
    }
    m_appBar->setStyleSheet(
        QStringLiteral("QFrame#flasherAppBar { background-color:%1; border-bottom:1px solid %2; }"
                       "QFrame#flasherAppBar QLabel { color:%3; }")
            .arg(background.name(), rule.name(QColor::HexArgb), foreground.name()));
    m_appIcon->setStyleSheet(UiColor::style(FlasherStyle::primary()));
    if (m_flashing) {
        m_appIcon->setStyleSheet(UiColor::style(UiColor::bannerFg()));
    }

    // Segmented toggle: checked half filled with the accent.
    const QString segmentSheet = QStringLiteral(
        "QToolButton { padding:6px 14px; border:1px solid %1; background-color:%2; color:%3; }"
        "QToolButton:checked { background-color:%4; color:%2; border-color:%4; font-weight:600; }");
    const QString filled = segmentSheet.arg(rule.name(QColor::HexArgb), pal.color(QPalette::Base).name(),
                                            pal.color(QPalette::Text).name(), FlasherStyle::primary().name());
    m_engineerButton->setStyleSheet(filled + QStringLiteral(
        "QToolButton { border-top-left-radius:6px; border-bottom-left-radius:6px; border-right:none; }"));
    m_operatorButton->setStyleSheet(filled + QStringLiteral(
        "QToolButton { border-top-right-radius:6px; border-bottom-right-radius:6px; }"));
}

void FlasherWindow::refreshAppBarTarget()
{
    if (m_flashing) {
        m_targetLabel->setText(QStringLiteral("%1:%2").arg(m_batchIp).arg(m_batchPort));
    } else {
        m_targetLabel->setText(QStringLiteral("%1:%2").arg(m_queuePage->vccIp()).arg(m_queuePage->port()));
    }
}

void FlasherWindow::setFlashingState(bool flashing)
{
    m_flashing = flashing;
    m_flashingBadge->setVisible(flashing);
    m_targetLabel->setVisible(flashing);
    m_profileLabel->setVisible(!flashing);
    // Nothing that would change what is being flashed can be touched while
    // a batch runs. History stays available: it is read-only.
    m_profileCombo->setEnabled(!flashing);
    m_engineerButton->setEnabled(!flashing);
    m_operatorButton->setEnabled(!flashing);
    m_gearButton->setEnabled(!flashing);
    refreshAppBarTarget();
    restyleAppBar();
}

void FlasherWindow::onModeChanged()
{
    if (m_engineerButton->isChecked()) {
        m_mode = Flasher::Mode::Engineer;
    } else {
        m_mode = Flasher::Mode::Operator;
    }
    m_queuePage->setMode(m_mode);
    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    if (m_mode == Flasher::Mode::Operator) {
        settings.setValue(QLatin1String(kModeSettingKey), QStringLiteral("operator"));
    } else {
        settings.setValue(QLatin1String(kModeSettingKey), QStringLiteral("engineer"));
    }
}

// =============================================================================
//  Profiles
// =============================================================================

void FlasherWindow::reloadProfileCombo()
{
    const QSignalBlocker blocker(m_profileCombo);
    m_profileCombo->clear();
    for (const QString &name : m_profiles.names()) {
        m_profileCombo->addItem(name, name);
    }
    m_profileCombo->insertSeparator(m_profileCombo->count());
    m_profileCombo->addItem(tr("New profile…"), kNewProfileMarker);
    const int activeIndex = m_profileCombo->findData(m_profiles.activeName());
    if (activeIndex >= 0) {
        m_profileCombo->setCurrentIndex(activeIndex);
    }
    m_profileComboPrevious = m_profileCombo->currentIndex();
}

void FlasherWindow::applyActiveProfile()
{
    m_queuePage->applyProfile(m_profiles.profile(m_profiles.activeName()));
}

void FlasherWindow::saveProfiles()
{
    if (!m_profiles.save()) {
        QMessageBox::warning(this, windowTitle(),
                             tr("The flasher profiles could not be saved:\n\n%1").arg(m_profiles.lastError()));
    }
}

void FlasherWindow::onProfileChosen(int comboIndex)
{
    const QVariant data = m_profileCombo->itemData(comboIndex);
    if (data.userType() == QMetaType::Int && data.toInt() == kNewProfileMarker) {
        // A new profile starts from the current one's settings (same bench,
        // different images is the common case), under a fresh name.
        Flasher::FlashProfile fresh = m_profiles.profile(m_profiles.activeName());
        QString name = tr("New profile");
        int suffix = 2;
        while (m_profiles.contains(name)) {
            name = tr("New profile %1").arg(suffix);
            ++suffix;
        }
        fresh.name = name;
        fresh.lastSettledRateKBps = 0.0;   // a new bench has not settled anywhere yet
        FlasherProfileDialog dialog(fresh, m_mode, m_profiles.names(), false, this);
        if (dialog.exec() == QDialog::Accepted) {
            m_profiles.upsert(dialog.profile());
            m_profiles.setActiveName(dialog.profile().name);
            saveProfiles();
            reloadProfileCombo();
            applyActiveProfile();
        } else {
            const QSignalBlocker blocker(m_profileCombo);
            m_profileCombo->setCurrentIndex(m_profileComboPrevious);
        }
        return;
    }

    const QString name = data.toString();
    if (name.isEmpty() || name == m_profiles.activeName()) {
        return;
    }
    m_profiles.setActiveName(name);
    saveProfiles();
    m_profileComboPrevious = comboIndex;
    applyActiveProfile();
}

UndoLog *FlasherWindow::undoLog()
{
    if (!m_undo) {
        m_undo = new UndoLog(this);
        // No menu bar here: the action is added to the window so Ctrl+Z works.
        addAction(m_undo->createAction(this));
    }
    return m_undo;
}

void FlasherWindow::editProfile()
{
    const Flasher::FlashProfile current = m_profiles.profile(m_profiles.activeName());
    FlasherProfileDialog dialog(current, m_mode, m_profiles.names(), m_profiles.names().size() > 1, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (dialog.deleteRequested()) {
        // Ctrl+Z brings a deleted profile back (session 79).
        const Flasher::FlashProfile removed = m_profiles.profile(dialog.originalName());
        m_profiles.remove(dialog.originalName());
        QPointer<FlasherWindow> self(this);
        undoLog()->push(tr("Delete profile \"%1\"").arg(removed.name), [self, removed]() {
            // Not mid-batch (the profile in force must not change under it),
            // and not if a new profile has taken the name meanwhile.
            if (!self || self->m_flashing || self->m_profiles.contains(removed.name)) {
                return false;
            }
            self->m_profiles.upsert(removed);
            self->m_profiles.setActiveName(removed.name);
            self->saveProfiles();
            self->reloadProfileCombo();
            self->applyActiveProfile();
            return true;
        });
    } else {
        m_profiles.upsert(dialog.profile(), dialog.originalName());
        m_profiles.setActiveName(dialog.profile().name);
    }
    saveProfiles();
    reloadProfileCombo();
    applyActiveProfile();
}

// =============================================================================
//  Starting a batch
// =============================================================================

void FlasherWindow::onFlashRequested()
{
    startBatch(true);
}

bool FlasherWindow::startBatch(bool confirmNames)
{
    if (m_flashing) {
        return false;
    }
    const Flasher::PreflightReport report = m_queuePage->preflight();
    if (!report.ready) {
        return false;   // the button is disabled in this state; this is the belt to its braces
    }

    // Engineer mode lets a name mismatch through, but only past an explicit
    // yes that lists exactly what did not match.
    if (confirmNames && report.needsNameConfirmation) {
        QStringList problems;
        for (const Flasher::PreflightItem &item : report.items) {
            if (item.state == Flasher::PreflightItem::State::Warning && !item.blocksFlash
                && (item.text.contains(QLatin1String("does not name")) || item.text.contains(QLatin1String("looks like")))) {
                problems.append(QStringLiteral("•  ") + item.text);
            }
        }
        const auto answer = QMessageBox::warning(
            this, windowTitle(),
            tr("These images may not belong to the card they are queued for:\n\n%1\n\n"
               "Flash them anyway?").arg(problems.join(QLatin1Char('\n'))),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return false;
        }
    }

    // Snapshot everything the batch depends on, so an edit made while it
    // runs (the fields are disabled, but the files on disk are not) cannot
    // change what the rest of the batch sends.
    m_plan = Flasher::BatchPlan(m_queuePage->batchEntries(), true);
    m_batchProfile = m_profiles.profile(m_profiles.activeName());
    m_batchIp = m_queuePage->vccIp();
    m_batchPort = m_queuePage->port();
    m_batchId = Flasher::newBatchId(QDateTime::currentDateTimeUtc());
    m_closeWhenIdle = false;

    m_flashingPage->beginBatch(m_plan);
    setFlashingState(true);
    showPage(FlashingPageIndex);
    startNextCard();
    return true;
}

void FlasherWindow::startNextCard()
{
    const int index = m_plan.startNext();
    if (index < 0) {
        finishBatch();
        return;
    }
    const Flasher::BatchEntry &entry = m_plan.entry(index);

    kflash::FlashConfig config;
    config.receiver_ip = m_batchIp.toStdString();
    config.port = m_batchPort;
    config.card_type = entry.cardType;
    config.image.assign(entry.image.bytes.constBegin(), entry.image.bytes.constEnd());
    config.tuning = Flasher::effectiveTuning(m_batchProfile);

    m_currentAlreadyHeld = false;
    m_flashingPage->setMaxRounds(config.tuning.max_rounds);
    m_flashingPage->setUpdaterWaitMs(static_cast<qint64>(m_batchProfile.updaterWaitSeconds) * 1000);
    m_flashingPage->beginCard(index, entry);

    // Queued: flashCard() runs on the worker thread and blocks it for the
    // length of the card.
    QMetaObject::invokeMethod(m_worker, "flashCard", Qt::QueuedConnection,
                              Q_ARG(kflash::FlashConfig, config));
}

// =============================================================================
//  During a card
// =============================================================================

void FlasherWindow::onProgress(const kflash::Progress &progress)
{
    // FlashEngine::run() clears its cancel flag when it starts. An Abort
    // pressed in the moment between invokeMethod() and run() starting would
    // be wiped, so while an abort is pending, repeat it on every progress
    // report until the card ends.
    if (m_plan.abortRequested()) {
        m_worker->requestCancel();
    }
    m_flashingPage->onProgress(progress);
}

void FlasherWindow::onLogLine(int level, const QString &text)
{
    m_flashingPage->appendLog(level, text);
    const int index = m_plan.currentIndex();
    if (index >= 0) {
        // The page stamps the line; history keeps the same stamped text.
        m_plan.entry(index).log.append(m_flashingPage->logLines().last());
    }
    if (Flasher::logLineMeansAlreadyHeld(text)) {
        m_currentAlreadyHeld = true;
    }
}

void FlasherWindow::onAbortRequested()
{
    if (!m_flashing || m_plan.currentIndex() < 0) {
        return;
    }
    QString question;
    if (m_flashingPage->waitingForUpdater()) {
        question = tr("Stop waiting for the updater?\n\nNothing has been sent to the board yet.");
    } else {
        question = tr("Abort the transfer to the %1 now?\n\n"
                      "The updater discards a part-received image; the card keeps running its "
                      "current firmware. Flashing it needs a fresh run and another power cycle.")
                       .arg(Flasher::cardName(m_plan.entry(m_plan.currentIndex()).cardType));
    }
    const auto answer = QMessageBox::warning(this, windowTitle(), question,
                                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }
    abortCurrent();
}

void FlasherWindow::abortCurrent()
{
    if (!m_flashing) {
        return;
    }
    m_plan.requestAbort();
    // Direct call, not queued: see the class comment.
    m_worker->requestCancel();
    m_flashingPage->setAborting();
}

void FlasherWindow::onCardFinished(const kflash::FlashResult &result)
{
    const int index = m_plan.currentIndex();
    if (index < 0) {
        return;
    }
    m_plan.finishCurrent(result, m_currentAlreadyHeld);
    const Flasher::BatchEntry &entry = m_plan.entry(index);
    m_flashingPage->finishCard(index, entry);

    // "Start from this bench's last settled rate": remember where the pacer
    // settled on a card that actually transferred.
    if (entry.outcome == Flasher::CardOutcome::Accepted && result.final_rate_kBps >= 1.0) {
        Flasher::FlashProfile profile = m_profiles.profile(m_batchProfile.name);
        if (m_profiles.contains(m_batchProfile.name)) {
            profile.lastSettledRateKBps = result.final_rate_kBps;
            m_profiles.upsert(profile);
            saveProfiles();
        }
    }
    startNextCard();
}

// =============================================================================
//  End of a batch
// =============================================================================

void FlasherWindow::finishBatch()
{
    m_flashingPage->refreshBatchRows(m_plan);

    // Every card in the batch goes to history, including the ones that never
    // ran: "Not run" is part of what happened to that chassis.
    QList<Flasher::HistoryRecord> records;
    const QString operatorName = Flasher::currentOperatorName();
    for (const Flasher::BatchEntry &entry : m_plan.entries()) {
        records.append(Flasher::makeHistoryRecord(entry, m_batchId, operatorName,
                                                  m_batchProfile.name, m_batchIp));
    }
    const bool saved = m_history.appendAll(records);

    m_summaryPage->showBatch(m_plan, m_batchId, operatorName, saved);
    setFlashingState(false);
    showPage(SummaryPageIndex);
    if (m_historyDialog) {
        m_historyDialog->reload();
    }
    emit batchFinished();

    if (!saved) {
        QMessageBox::warning(this, windowTitle(),
                             tr("This batch could not be written to the flash history:\n\n%1")
                                 .arg(m_history.lastError()));
    }
    if (m_closeWhenIdle) {
        close();
    }
}

void FlasherWindow::exportReport()
{
    const QString suggested = QStringLiteral("flash_report_%1.txt").arg(m_batchId);
    const QString path = QFileDialog::getSaveFileName(this, tr("Export report"), suggested,
                                                      tr("Text files (*.txt);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, windowTitle(), tr("Could not write %1:\n%2").arg(path, file.errorString()));
        return;
    }
    QTextStream out(&file);
    out << Flasher::batchReportText(m_plan, m_batchId, Flasher::currentOperatorName(),
                                    m_batchProfile.name, m_batchIp, m_batchPort);
}

void FlasherWindow::tryAgain()
{
    // Back to the queue with the same card selected, rather than straight
    // into a run: the pre-flight list is re-evaluated (a rebuilt image is
    // re-hashed), and after an IMAGE_FAIL the operator should be looking at
    // which file is queued before sending it again.
    if (m_plan.size() > 0) {
        m_queuePage->selectCard(m_plan.entries().first().cardType);
    }
    showPage(QueuePageIndex);
}

void FlasherWindow::openHistory()
{
    if (!m_historyDialog) {
        m_historyDialog = new FlasherHistoryDialog(m_history.filePath(), this);
        m_historyDialog->setAttribute(Qt::WA_DeleteOnClose);
    } else {
        m_historyDialog->reload();
    }
    m_historyDialog->show();
    m_historyDialog->raise();
    m_historyDialog->activateWindow();
}

// =============================================================================
//  Closing
// =============================================================================

void FlasherWindow::closeEvent(QCloseEvent *event)
{
    if (m_flashing) {
        // Closing mid-flash asks first (SendGuard, like the other transmitting
        // windows). A yes aborts the card and closes once the worker has
        // actually returned -- tearing the window down while run() is still
        // sending would leave the thread running with nothing to report to.
        if (!m_closeWhenIdle && SendGuard::confirmClose(this, tr("A firmware flash"))) {
            m_closeWhenIdle = true;
            abortCurrent();
        }
        event->ignore();
        return;
    }
    WindowGeometry::save(this, QStringLiteral("firmwareFlasher"));
    QMainWindow::closeEvent(event);
}
