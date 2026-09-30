#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

// =============================================================================
//  SettingsDialog
//  -----------------------------------------------------------------------------
//  Modal dialog that lets the operator tune the INI-backed settings without
//  hand-editing the file. Two-column form layout, OK / Cancel / Apply.
//
//  Most values take effect immediately (capacity, theme, offline thresholds,
//  rotation size). UDP port and disk log root require a restart — the
//  dialog shows a small "(restart required)" hint next to those fields and
//  the apply path writes the new value to the INI without trying to apply
//  it live.
// =============================================================================

#include <QDialog>

class QCheckBox;
class QSpinBox;
class QLineEdit;
class QComboBox;
class QPushButton;
class QLabel;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

signals:
    // Fired when the user clicks Apply or OK with changes that can be
    // applied live. MainWindow listens and propagates each one to the
    // appropriate subsystem.
    void themeChanged(QString themeName);              // "light" | "dark"
    void perTabCapacityChanged(int newCap);
    void offlineThresholdsChanged(int warnSec, int errSec);
    void diskRotationBytesChanged(qint64 bytes);
    // Master switch for continuous logging. MainWindow creates or tears
    // down the LogWriter in response — it is applied live rather than
    // needing a restart, because "stop writing to disk" is something an
    // operator may need to do immediately.
    void diskLoggingEnabledChanged(bool enabled);
    void maxFolderBytesChanged(qint64 bytes);
    void minFreeBytesChanged(qint64 bytes);
    void rawCaptureChanged(bool enabled);
    void saveIncludesRawChanged(bool enabled);
    // Settings that need restart (port, log root) are silently written
    // to the INI; we don't emit a signal for those because nothing live
    // can act on them.

private slots:
    void onApply();
    void onAccept();

private:
    void loadFromSettings();
    void writeToSettings();
    void updateDiskGroupEnabled();

    QSpinBox    *m_udpPort         = nullptr;
    QSpinBox    *m_queueCapacity   = nullptr;
    QSpinBox    *m_perTabCap       = nullptr;
    QCheckBox   *m_diskEnabled     = nullptr;
    QLineEdit   *m_diskRoot        = nullptr;
    QSpinBox    *m_diskRotMiB      = nullptr;   // shown to user in MiB
    QSpinBox    *m_diskBudgetMiB   = nullptr;   // 0 = unlimited
    QSpinBox    *m_minFreeMiB      = nullptr;   // 0 = no floor
    QCheckBox   *m_rawCapture      = nullptr;
    QCheckBox   *m_saveRaw         = nullptr;
    QSpinBox    *m_offlineWarn     = nullptr;
    QSpinBox    *m_offlineErr      = nullptr;
    QComboBox   *m_themeBox        = nullptr;
};

#endif // SETTINGSDIALOG_H
