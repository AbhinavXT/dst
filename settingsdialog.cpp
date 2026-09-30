#include "settingsdialog.h"
#include "theme.h"
#include "uicolors.h"
#include "settings.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("DLConsole — Settings"));
    setMinimumWidth(480);

    auto *form = new QFormLayout;

    // ---- Network ----------------------------------------------------
    m_udpPort = new QSpinBox;
    m_udpPort->setRange(1, 65535);
    {
        auto *row = new QHBoxLayout;
        row->addWidget(m_udpPort);
        auto *hint = new QLabel(tr("(restart required)"));
        hint->setStyleSheet(UiColor::mutedStyle() + QStringLiteral(" font-style: italic;"));
        row->addWidget(hint);
        row->addStretch();
        auto *w = new QWidget;
        w->setLayout(row);
        form->addRow(tr("UDP port:"), w);
    }

    m_queueCapacity = new QSpinBox;
    m_queueCapacity->setRange(1000, 1'000'000);
    m_queueCapacity->setSingleStep(1000);
    m_queueCapacity->setSuffix(tr(" messages"));
    {
        auto *row = new QHBoxLayout;
        row->addWidget(m_queueCapacity);
        auto *hint = new QLabel(tr("(restart required)"));
        hint->setStyleSheet(UiColor::mutedStyle() + QStringLiteral(" font-style: italic;"));
        row->addWidget(hint);
        row->addStretch();
        auto *w = new QWidget;
        w->setLayout(row);
        form->addRow(tr("Receiver queue cap:"), w);
    }

    // ---- Per-tab model ----------------------------------------------
    m_perTabCap = new QSpinBox;
    m_perTabCap->setRange(1000, 10'000'000);
    m_perTabCap->setSingleStep(10'000);
    m_perTabCap->setSuffix(tr(" rows"));
    form->addRow(tr("Per-tab capacity:"), m_perTabCap);

    // ---- Disk -------------------------------------------------------
    //
    // The master checkbox gates everything below it. Off means no .log and
    // no .dlr at all: the console still shows live traffic and the
    // in-memory ring still holds recent history, but nothing is persisted.
    m_diskEnabled = new QCheckBox(tr("Save logs continuously to disk"));
    m_diskEnabled->setToolTip(
        tr("When off, nothing is written to disk. The live view and the\n"
           "in-memory history are unaffected, but no archive is kept and\n"
           "recorded sessions cannot be reopened later."));
    form->addRow(QString(), m_diskEnabled);
    connect(m_diskEnabled, &QCheckBox::toggled,
            this, &SettingsDialog::updateDiskGroupEnabled);

    m_diskRoot = new QLineEdit;
    {
        auto *row = new QHBoxLayout;
        row->addWidget(m_diskRoot);
        auto *browse = new QPushButton(tr("Browse…"));
        connect(browse, &QPushButton::clicked, this, [this]() {
            const QString d = QFileDialog::getExistingDirectory(
                this, tr("Choose disk-log root"), m_diskRoot->text());
            if (!d.isEmpty()) m_diskRoot->setText(d);
        });
        row->addWidget(browse);
        auto *hint = new QLabel(tr("(restart required)"));
        hint->setStyleSheet(UiColor::mutedStyle() + QStringLiteral(" font-style: italic;"));
        row->addWidget(hint);
        auto *w = new QWidget;
        w->setLayout(row);
        form->addRow(tr("Disk-log root:"), w);
    }

    m_diskRotMiB = new QSpinBox;
    m_diskRotMiB->setRange(1, 4096);
    m_diskRotMiB->setSuffix(tr(" MiB"));
    form->addRow(tr("Rotate file at:"), m_diskRotMiB);

    // Whole-folder budget. Distinct from the per-file rotation size above:
    // rotation caps one file, this caps the archive. Nothing else in the
    // app ever deletes a log, so without this the folder grows until the
    // partition is full.
    m_diskBudgetMiB = new QSpinBox;
    m_diskBudgetMiB->setRange(0, 4096 * 1024);          // up to 4 TiB
    m_diskBudgetMiB->setSingleStep(256);
    m_diskBudgetMiB->setSuffix(tr(" MiB   (0 = no limit)"));
    m_diskBudgetMiB->setToolTip(
        tr("Total size of everything under the disk-log root, counting both\n"
           ".log and .dlr files.\n\n"
           "On reaching this, continuous logging suspends itself rather than\n"
           "filling the disk. Nothing is deleted — free some space and\n"
           "logging resumes on its own within about 30 seconds."));
    form->addRow(tr("Stop logging above:"), m_diskBudgetMiB);

    m_minFreeMiB = new QSpinBox;
    m_minFreeMiB->setRange(0, 1024 * 1024);
    m_minFreeMiB->setSingleStep(128);
    m_minFreeMiB->setSuffix(tr(" MiB   (0 = no floor)"));
    m_minFreeMiB->setToolTip(
        tr("Free space to leave on the log volume.\n\n"
           "The folder limit above only counts what DLConsole wrote. This\n"
           "catches the disk filling for any other reason, and suspends\n"
           "logging the same reversible way instead of failing every write."));
    form->addRow(tr("Keep free on disk:"), m_minFreeMiB);

    m_rawCapture = new QCheckBox(tr("Also record raw datagrams (.dlr)"));
    m_rawCapture->setToolTip(
        tr("Keeps every datagram byte-for-byte alongside the readable log,\n"
           "so a session can be re-decoded later with a corrected schema or\n"
           "reopened in the session viewer.\n\n"
           "Roughly doubles disk use. Without it the archive is a rendering\n"
           "only and cannot be re-interpreted."));
    form->addRow(QString(), m_rawCapture);

    // Outside the continuous-logging group on purpose: this governs the
    // manual Save button, which works whether or not continuous logging is
    // on, so greying it out with the rest would hide a setting that still
    // has an effect.
    m_saveRaw = new QCheckBox(tr("Include raw datagrams when saving a tab"));
    m_saveRaw->setToolTip(
        tr("When you press Save, write a .dlr beside the .log so the\n"
           "capture can be re-decoded or reopened later.\n\n"
           "Independent of continuous logging — this still applies when\n"
           "continuous logging is switched off."));
    form->addRow(QString(), m_saveRaw);

    // ---- Offline detection ------------------------------------------
    m_offlineWarn = new QSpinBox;
    m_offlineWarn->setRange(0, 3600);
    m_offlineWarn->setSuffix(tr(" seconds  (0 = disabled)"));
    form->addRow(tr("Tab amber after:"), m_offlineWarn);

    m_offlineErr = new QSpinBox;
    m_offlineErr->setRange(0, 3600);
    m_offlineErr->setSuffix(tr(" seconds  (0 = disabled)"));
    form->addRow(tr("Tab red after:"), m_offlineErr);

    // ---- Theme ------------------------------------------------------
    m_themeBox = new QComboBox;
    for (Theme theme : ThemeUtil::all()) {
        m_themeBox->addItem(ThemeUtil::label(theme), QString::fromLatin1(ThemeUtil::toString(theme)));
    }
    form->addRow(tr("Theme:"), m_themeBox);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                         | QDialogButtonBox::Apply
                                         | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply),
            &QPushButton::clicked, this, &SettingsDialog::onApply);

    auto *root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(buttons);

    loadFromSettings();
}

void SettingsDialog::loadFromSettings()
{
    m_udpPort      ->setValue(Settings::udpPort());
    m_queueCapacity->setValue(Settings::queueCapacity());
    m_perTabCap    ->setValue(Settings::perTabCapacity());
    m_diskEnabled  ->setChecked(Settings::diskLoggingEnabled());
    m_diskRoot     ->setText (Settings::diskLogRoot());
    m_diskRotMiB   ->setValue(int(Settings::diskRotationBytes() / (1024 * 1024)));
    m_diskBudgetMiB->setValue(int(Settings::maxFolderBytes() / (1024 * 1024)));
    m_minFreeMiB   ->setValue(int(Settings::minFreeBytes()   / (1024 * 1024)));
    m_rawCapture   ->setChecked(Settings::rawCapture());
    m_saveRaw      ->setChecked(Settings::saveIncludesRaw());
    updateDiskGroupEnabled();
    m_offlineWarn  ->setValue(Settings::offlineWarnSeconds());
    m_offlineErr   ->setValue(Settings::offlineErrSeconds());
    const int tIdx = m_themeBox->findData(Settings::theme());
    if (tIdx >= 0) m_themeBox->setCurrentIndex(tIdx);
}

void SettingsDialog::writeToSettings()
{
    Settings::setUdpPort      (m_udpPort      ->value());
    Settings::setQueueCapacity(m_queueCapacity->value());
    Settings::setPerTabCapacity(m_perTabCap   ->value());
    Settings::setDiskLoggingEnabled(m_diskEnabled->isChecked());
    Settings::setDiskLogRoot  (m_diskRoot     ->text());
    Settings::setDiskRotationBytes(qint64(m_diskRotMiB->value()) * 1024 * 1024);
    Settings::setMaxFolderBytes(qint64(m_diskBudgetMiB->value()) * 1024 * 1024);
    Settings::setMinFreeBytes(qint64(m_minFreeMiB->value()) * 1024 * 1024);
    Settings::setRawCapture(m_rawCapture->isChecked());
    Settings::setSaveIncludesRaw(m_saveRaw->isChecked());
    Settings::setOfflineWarnSeconds(m_offlineWarn->value());
    Settings::setOfflineErrSeconds (m_offlineErr ->value());
    Settings::setTheme(m_themeBox->currentData().toString());
}

void SettingsDialog::onApply()
{
    writeToSettings();
    // Emit signals for everything that can be applied live. UDP port and
    // disk root are silently written; nothing live can act on them.
    emit themeChanged(m_themeBox->currentData().toString());
    emit perTabCapacityChanged(m_perTabCap->value());
    emit offlineThresholdsChanged(m_offlineWarn->value(),
                                  m_offlineErr ->value());
    emit diskRotationBytesChanged(qint64(m_diskRotMiB->value()) * 1024 * 1024);
    emit maxFolderBytesChanged(qint64(m_diskBudgetMiB->value()) * 1024 * 1024);
    emit minFreeBytesChanged(qint64(m_minFreeMiB->value()) * 1024 * 1024);
    emit rawCaptureChanged(m_rawCapture->isChecked());
    emit saveIncludesRawChanged(m_saveRaw->isChecked());
    // Last, because MainWindow may tear down and rebuild the writer in
    // response — the settings above are read during that rebuild, so they
    // must already be in the INI and applied.
    emit diskLoggingEnabledChanged(m_diskEnabled->isChecked());
}

void SettingsDialog::updateDiskGroupEnabled()
{
    const bool on = m_diskEnabled->isChecked();
    m_diskRoot     ->setEnabled(on);
    m_diskRotMiB   ->setEnabled(on);
    m_diskBudgetMiB->setEnabled(on);
    m_minFreeMiB   ->setEnabled(on);
    m_rawCapture   ->setEnabled(on);
}

void SettingsDialog::onAccept()
{
    onApply();
    accept();
}
