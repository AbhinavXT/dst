#include "flasherprofiledialog.h"

#include "flasherstyle.h"
#include "statusline.h"
#include "uicolors.h"
#include "uistyle.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

QSpinBox *makeSpin(QWidget *parent, int minimum, int maximum, const QString &tooltip)
{
    auto *spin = new QSpinBox(parent);
    spin->setRange(minimum, maximum);
    spin->setFont(UiStyle::monoFont());
    spin->setMinimumHeight(34);
    spin->setToolTip(tooltip);
    return spin;
}

}  // namespace

FlasherProfileDialog::FlasherProfileDialog(const Flasher::FlashProfile &profile, Flasher::Mode mode,
                                           const QStringList &existingNames, bool canDelete,
                                           QWidget *parent)
    : QDialog(parent)
    , m_result(profile)
    , m_originalName(profile.name)
    , m_existingNames(existingNames)
    , m_mode(mode)
{
    setWindowTitle(tr("Edit profile"));
    resize(760, 900);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // The form scrolls: 900 px does not fit on a 768 px-high field laptop.
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *form = new QWidget(scroll);
    auto *formLayout = new QVBoxLayout(form);
    formLayout->setContentsMargins(22, 20, 22, 20);
    formLayout->setSpacing(18);
    scroll->setWidget(form);
    outer->addWidget(scroll, 1);

    auto *title = new QLabel(tr("Edit profile"), form);
    title->setFont(FlasherStyle::scaledFont(title->font(), 1.5, true));
    formLayout->addWidget(title);

    // ---- basics ------------------------------------------------------------------
    auto *basics = new QGridLayout();
    basics->setHorizontalSpacing(12);
    basics->setVerticalSpacing(4);
    basics->addWidget(new QLabel(tr("Profile name"), form), 0, 0, 1, 3);
    m_name = new QLineEdit(profile.name, form);
    m_name->setMinimumHeight(34);
    m_name->setToolTip(tr("Shown in the profile menu and written to history as the chassis/site."));
    basics->addWidget(m_name, 1, 0, 1, 3);
    basics->addWidget(new QLabel(tr("VCC IP address"), form), 2, 0, 1, 2);
    basics->addWidget(new QLabel(tr("UDP port"), form), 2, 2);
    m_ip = new QLineEdit(profile.vccIp, form);
    m_ip->setFont(UiStyle::monoFont());
    m_ip->setMinimumHeight(34);
    basics->addWidget(m_ip, 3, 0, 1, 2);
    m_port = makeSpin(form, 1, 65535, tr("The updater's UDP port on the VCC (50001 on current firmware)."));
    m_port->setValue(profile.port);
    basics->addWidget(m_port, 3, 2);
    // How long Flash keeps calling the updater while the operator power-cycles
    // the chassis. Not tuning: it depends on how far the operator is from the
    // chassis's power switch, so it is editable in both modes.
    basics->addWidget(new QLabel(tr("Wait for updater after pressing Flash (s)"), form), 4, 0, 1, 3);
    m_updaterWait = makeSpin(form, Flasher::kMinUpdaterWaitSeconds, Flasher::kMaxUpdaterWaitSeconds,
                             tr("The updater only runs for %1 s after power-on. Flash starts calling it\n"
                                "first; this is how long it keeps calling while the chassis is power-cycled.")
                                 .arg(Flasher::kUpdaterListenSeconds));
    m_updaterWait->setValue(profile.updaterWaitSeconds);
    basics->addWidget(m_updaterWait, 5, 0);
    basics->setColumnStretch(0, 1);
    formLayout->addLayout(basics);

    // ---- default images ------------------------------------------------------
    auto *imagesTitle = new QLabel(tr("Default image per card"), form);
    FlasherStyle::makeSectionLabel(imagesTitle);
    formLayout->addWidget(imagesTitle);
    auto *images = new QGridLayout();
    images->setHorizontalSpacing(10);
    images->setVerticalSpacing(8);
    const int cardOrder[] = { Flasher::CardVcc, Flasher::CardInput, Flasher::CardOutput, Flasher::CardAnalog };
    int imageRow = 0;
    for (int cardType : cardOrder) {
        auto *cardLabel = new QLabel(Flasher::cardName(cardType), form);
        QFont bold = cardLabel->font();
        bold.setBold(true);
        cardLabel->setFont(bold);
        images->addWidget(cardLabel, imageRow, 0);

        auto *path = new QLineEdit(QDir::toNativeSeparators(profile.defaultImages.value(cardType)), form);
        path->setFont(UiStyle::monoFont());
        path->setPlaceholderText(tr("No default"));
        path->setMinimumHeight(34);
        path->setClearButtonEnabled(true);
        m_imagePaths.insert(cardType, path);
        images->addWidget(path, imageRow, 1);

        auto *browse = new QPushButton(tr("Browse…"), form);
        connect(browse, &QPushButton::clicked, this, [this, cardType, path]() {
            QString start = QFileInfo(QDir::fromNativeSeparators(path->text())).absolutePath();
            if (path->text().isEmpty()) {
                start.clear();
            }
            const QString chosen = QFileDialog::getOpenFileName(
                this, tr("Default image for the %1").arg(Flasher::cardName(cardType)), start,
                tr("Firmware images (*.appimage);;All files (*)"));
            if (!chosen.isEmpty()) {
                path->setText(QDir::toNativeSeparators(chosen));
            }
        });
        images->addWidget(browse, imageRow, 2);
        ++imageRow;
    }
    images->setColumnStretch(1, 1);
    formLayout->addLayout(images);

    m_pinBySha = new QCheckBox(tr("Pin by SHA-256 — warn if the file on disk changes"), form);
    m_pinBySha->setChecked(profile.pinBySha);
    m_pinBySha->setToolTip(tr("On save, the SHA-256 of each default image is recorded. If the file\n"
                              "is later rebuilt in place, the queue flags it before anything is sent."));
    formLayout->addWidget(m_pinBySha);

    // ---- transfer tuning -------------------------------------------------------
    m_tuningBox = new QGroupBox(tr("Transfer tuning · Engineer only"), form);
    auto *tuningLayout = new QVBoxLayout(m_tuningBox);
    tuningLayout->setSpacing(10);

    auto *rateRow = new QHBoxLayout();
    rateRow->addWidget(new QLabel(tr("Send rate"), m_tuningBox));
    m_adaptive = new QRadioButton(tr("Adaptive"), m_tuningBox);
    m_fixed = new QRadioButton(tr("Fixed"), m_tuningBox);
    auto *rateGroup = new QButtonGroup(this);
    rateGroup->addButton(m_adaptive);
    rateGroup->addButton(m_fixed);
    rateRow->addWidget(m_adaptive);
    rateRow->addWidget(m_fixed);
    rateRow->addStretch(1);
    auto *resetButton = new QPushButton(tr("Reset to defaults"), m_tuningBox);
    connect(resetButton, &QPushButton::clicked, this, &FlasherProfileDialog::onResetTuning);
    rateRow->addWidget(resetButton);
    tuningLayout->addLayout(rateRow);
    auto *rateHint = new QLabel(tr("Adaptive speeds up while the board keeps up and backs off when it "
                                   "starts dropping blocks. Fixed holds the start rate."), m_tuningBox);
    rateHint->setWordWrap(true);
    rateHint->setStyleSheet(UiColor::mutedStyle());
    tuningLayout->addWidget(rateHint);
    connect(m_adaptive, &QRadioButton::toggled, this, &FlasherProfileDialog::onRateModeChanged);

    auto *settledRow = new QHBoxLayout();
    m_startFromSettled = new QCheckBox(tr("Start from this bench's last settled rate"), m_tuningBox);
    m_startFromSettled->setChecked(profile.startFromLastSettled);
    settledRow->addWidget(m_startFromSettled, 1);
    m_settledValue = new QLabel(m_tuningBox);
    m_settledValue->setFont(UiStyle::monoFont());
    if (profile.lastSettledRateKBps >= 1.0) {
        m_settledValue->setText(Flasher::formatRate(profile.lastSettledRateKBps));
    } else {
        m_settledValue->setText(tr("no run yet"));
        m_settledValue->setStyleSheet(UiColor::mutedStyle());
    }
    settledRow->addWidget(m_settledValue);
    tuningLayout->addLayout(settledRow);

    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(4);
    int cell = 0;
    auto place = [&grid, &cell, this](const QString &label, QWidget *field) {
        const int row = (cell / 3) * 2;
        const int column = cell % 3;
        grid->addWidget(new QLabel(label, m_tuningBox), row, column);
        grid->addWidget(field, row + 1, column);
        ++cell;
    };
    m_startRate = makeSpin(m_tuningBox, 1, 100000, tr("First-pass starting rate. Adaptive mode moves from here."));
    m_maxRate = makeSpin(m_tuningBox, 1, 100000, tr("Adaptive ceiling."));
    m_minRate = makeSpin(m_tuningBox, 1, 100000, tr("Adaptive floor: the pacer never backs off below this."));
    m_checkpoint = makeSpin(m_tuningBox, 0, 100000, tr("Poll for a STATUS every N blocks during the first pass,\n"
                                                        "so losses are repaired early. 0 = off."));
    m_pollMin = makeSpin(m_tuningBox, 1, 60000, tr("Floor of the adaptive poll timeout."));
    m_pollMax = makeSpin(m_tuningBox, 1, 60000, tr("Ceiling of the adaptive poll timeout."));
    m_giveUp = makeSpin(m_tuningBox, 100, 600000, tr("Total silence from the board before the card is abandoned."));
    m_metaCopies = makeSpin(m_tuningBox, 1, 50, tr("META copies sent per burst."));
    m_maxRounds = makeSpin(m_tuningBox, 1, 100000, tr("Repair rounds before giving up."));
    m_imageFailRestarts = makeSpin(m_tuningBox, 0, 10, tr("How many times a whole transfer is restarted after IMAGE_FAIL."));
    place(tr("Start rate (KB/s)"), m_startRate);
    place(tr("Max rate (KB/s)"), m_maxRate);
    place(tr("Min rate (KB/s)"), m_minRate);
    place(tr("Check-in every (blocks)"), m_checkpoint);
    place(tr("Poll timeout min (ms)"), m_pollMin);
    place(tr("Poll timeout max (ms)"), m_pollMax);
    place(tr("Give up after silence (ms)"), m_giveUp);
    place(tr("META copies / burst"), m_metaCopies);
    place(tr("Max repair rounds"), m_maxRounds);
    place(tr("IMAGE_FAIL restarts"), m_imageFailRestarts);
    auto *blockSize = new QLineEdit(QString::number(kflash::kBlockSize), m_tuningBox);
    blockSize->setReadOnly(true);
    blockSize->setEnabled(false);
    blockSize->setFont(UiStyle::monoFont());
    blockSize->setMinimumHeight(34);
    place(tr("Block size (bytes)"), blockSize);
    tuningLayout->addLayout(grid);
    auto *blockNote = new QLabel(tr("Block size is fixed by the firmware (FW_BLOCK_SIZE) and cannot be "
                                    "changed here."), m_tuningBox);
    blockNote->setWordWrap(true);
    blockNote->setStyleSheet(UiColor::mutedStyle());
    tuningLayout->addWidget(blockNote);
    formLayout->addWidget(m_tuningBox);

    loadTuning(profile.tuning);

    if (mode == Flasher::Mode::Operator) {
        // Locked, not hidden: see the header comment.
        m_tuningBox->setEnabled(false);
        m_tuningBox->setTitle(tr("Transfer tuning · 🔒 switch to Engineer to change"));
    }
    formLayout->addStretch(1);

    // ---- buttons -------------------------------------------------------------------
    auto *buttonBar = new QWidget(this);
    auto *buttonLayout = new QHBoxLayout(buttonBar);
    buttonLayout->setContentsMargins(22, 12, 22, 16);
    m_status = new StatusLine(buttonBar);
    auto *deleteButton = new QPushButton(tr("Delete profile"), buttonBar);
    deleteButton->setEnabled(canDelete);
    if (!canDelete) {
        deleteButton->setToolTip(tr("The last profile cannot be deleted."));
    }
    connect(deleteButton, &QPushButton::clicked, this, &FlasherProfileDialog::onDelete);
    buttonLayout->addWidget(deleteButton);
    buttonLayout->addWidget(m_status, 1);
    auto *cancelButton = new QPushButton(tr("Cancel"), buttonBar);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    buttonLayout->addWidget(cancelButton);
    auto *saveButton = new QPushButton(tr("Save profile"), buttonBar);
    saveButton->setDefault(true);
    connect(saveButton, &QPushButton::clicked, this, &FlasherProfileDialog::onSave);
    buttonLayout->addWidget(saveButton);
    outer->addWidget(buttonBar);
}

void FlasherProfileDialog::loadTuning(const kflash::TransferTuning &tuning)
{
    if (tuning.adaptive_rate) {
        m_adaptive->setChecked(true);
    } else {
        m_fixed->setChecked(true);
    }
    m_startRate->setValue(static_cast<int>(tuning.start_rate_kBps));
    m_maxRate->setValue(static_cast<int>(tuning.max_rate_kBps));
    m_minRate->setValue(static_cast<int>(tuning.min_rate_kBps));
    m_checkpoint->setValue(static_cast<int>(tuning.checkpoint_every_blocks));
    m_pollMin->setValue(static_cast<int>(tuning.poll_timeout_min_ms));
    m_pollMax->setValue(static_cast<int>(tuning.poll_timeout_max_ms));
    m_giveUp->setValue(static_cast<int>(tuning.poll_give_up_ms));
    m_metaCopies->setValue(tuning.meta_repeats);
    m_maxRounds->setValue(tuning.max_rounds);
    m_imageFailRestarts->setValue(tuning.max_image_fail_restarts);
    onRateModeChanged();
}

kflash::TransferTuning FlasherProfileDialog::readTuning() const
{
    // Start from the profile's own tuning so the fields this dialog does not
    // show (burst_packets, meta_handshake_tries) keep their values.
    kflash::TransferTuning tuning = m_result.tuning;
    tuning.adaptive_rate = m_adaptive->isChecked();
    tuning.start_rate_kBps = static_cast<uint32_t>(m_startRate->value());
    tuning.max_rate_kBps = static_cast<uint32_t>(m_maxRate->value());
    tuning.min_rate_kBps = static_cast<uint32_t>(m_minRate->value());
    tuning.checkpoint_every_blocks = static_cast<uint32_t>(m_checkpoint->value());
    tuning.poll_timeout_min_ms = static_cast<uint32_t>(m_pollMin->value());
    tuning.poll_timeout_max_ms = static_cast<uint32_t>(m_pollMax->value());
    tuning.poll_give_up_ms = static_cast<uint32_t>(m_giveUp->value());
    tuning.meta_repeats = m_metaCopies->value();
    tuning.max_rounds = m_maxRounds->value();
    tuning.max_image_fail_restarts = m_imageFailRestarts->value();
    return tuning;
}

void FlasherProfileDialog::onRateModeChanged()
{
    // With a fixed rate the ceiling, floor and "start from last settled" mean
    // nothing, so they are greyed rather than left to mislead.
    const bool adaptive = m_adaptive->isChecked();
    m_maxRate->setEnabled(adaptive);
    m_minRate->setEnabled(adaptive);
    m_startFromSettled->setEnabled(adaptive);
}

void FlasherProfileDialog::onResetTuning()
{
    loadTuning(kflash::TransferTuning());
    m_startFromSettled->setChecked(false);
    m_status->say(tr("Tuning reset to the shipped defaults — not saved yet"));
}

bool FlasherProfileDialog::validate(QString *problem) const
{
    const QString name = m_name->text().trimmed();
    if (name.isEmpty()) {
        *problem = tr("Give the profile a name.");
        return false;
    }
    if (name != m_originalName && m_existingNames.contains(name)) {
        *problem = tr("Another profile is already called \"%1\".").arg(name);
        return false;
    }
    uint32_t ip = 0;
    if (!kflash::parse_ipv4(m_ip->text().trimmed().toStdString(), &ip) || ip == 0 || ip == 0xFFFFFFFFu) {
        *problem = tr("The VCC IP address is not a valid IPv4 address.");
        return false;
    }
    if (m_maxRate->value() < m_minRate->value()) {
        *problem = tr("Max rate is below min rate.");
        return false;
    }
    if (m_pollMax->value() < m_pollMin->value()) {
        *problem = tr("Poll timeout max is below its min.");
        return false;
    }
    return true;
}

void FlasherProfileDialog::onSave()
{
    QString problem;
    if (!validate(&problem)) {
        m_status->fail(problem);
        return;
    }
    m_result.name = m_name->text().trimmed();
    m_result.vccIp = m_ip->text().trimmed();
    m_result.port = static_cast<quint16>(m_port->value());
    m_result.updaterWaitSeconds = m_updaterWait->value();
    m_result.defaultImages.clear();
    for (auto entry = m_imagePaths.constBegin(); entry != m_imagePaths.constEnd(); ++entry) {
        const QString path = QDir::fromNativeSeparators(entry.value()->text().trimmed());
        if (!path.isEmpty()) {
            m_result.defaultImages.insert(entry.key(), path);
        }
    }

    // Pins are taken NOW, from the files as they are at save time.
    m_result.pinBySha = m_pinBySha->isChecked();
    m_result.pinnedSha.clear();
    QStringList unpinnable;
    if (m_result.pinBySha) {
        for (auto entry = m_result.defaultImages.constBegin(); entry != m_result.defaultImages.constEnd(); ++entry) {
            const Flasher::ImageInfo image = Flasher::loadImage(entry.value());
            if (image.loaded) {
                m_result.pinnedSha.insert(entry.key(), image.shaHex());
            } else {
                unpinnable.append(Flasher::cardName(entry.key()));
            }
        }
    }
    if (!unpinnable.isEmpty()) {
        const auto answer = QMessageBox::question(
            this, windowTitle(),
            tr("These default images could not be read, so they cannot be pinned:\n\n%1\n\n"
               "Save anyway?").arg(unpinnable.join(QLatin1Char('\n'))),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    // Tuning is only taken from the form in Engineer mode. In Operator mode
    // the (locked) fields still hold the profile's values, but not reading
    // them makes it impossible for the lock to leak.
    if (m_mode == Flasher::Mode::Engineer) {
        m_result.tuning = readTuning();
        m_result.startFromLastSettled = m_startFromSettled->isChecked();
    }
    accept();
}

void FlasherProfileDialog::onDelete()
{
    const auto answer = QMessageBox::question(
        this, windowTitle(), tr("Delete the profile \"%1\"?\n\nHistory records that name it are kept.")
                                 .arg(m_originalName),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes) {
        m_deleteRequested = true;
        accept();
    }
}
