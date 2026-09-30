#ifndef FLASHERPROFILEDIALOG_H
#define FLASHERPROFILEDIALOG_H
// =============================================================================
//  flasherprofiledialog.{h,cpp} -- Profile.dc.html: edit one bench/site.
//
//  The dialog edits a COPY of a profile and hands it back; the window decides
//  what to do with it (save, rename, delete) through ProfileStore. Nothing is
//  written to disk from in here.
//
//  Transfer tuning is Engineer-only. In Operator mode the panel is shown but
//  locked, so a technician can see what the bench runs with without being
//  able to change it -- the numbers are useful when phoning an engineer.
// =============================================================================
#include <QDialog>
#include <QHash>
#include <QStringList>

#include "flashercore.h"

class QCheckBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class StatusLine;

class FlasherProfileDialog : public QDialog
{
    Q_OBJECT
public:
    // `existingNames` lets the dialog refuse a rename onto another profile.
    // `canDelete` is false when this is the only profile.
    FlasherProfileDialog(const Flasher::FlashProfile &profile, Flasher::Mode mode,
                         const QStringList &existingNames, bool canDelete,
                         QWidget *parent = nullptr);

    Flasher::FlashProfile profile() const { return m_result; }
    QString originalName() const { return m_originalName; }
    bool    deleteRequested() const { return m_deleteRequested; }

private slots:
    void onSave();
    void onDelete();
    void onResetTuning();
    void onRateModeChanged();

private:
    void loadTuning(const kflash::TransferTuning &tuning);
    kflash::TransferTuning readTuning() const;
    bool validate(QString *problem) const;

    Flasher::FlashProfile m_result;
    QString     m_originalName;
    QStringList m_existingNames;
    Flasher::Mode m_mode;
    bool        m_deleteRequested = false;

    QLineEdit   *m_name = nullptr;
    QLineEdit   *m_ip = nullptr;
    QSpinBox    *m_port = nullptr;
    QSpinBox    *m_updaterWait = nullptr;
    QHash<int, QLineEdit *> m_imagePaths;
    QCheckBox   *m_pinBySha = nullptr;
    QGroupBox   *m_tuningBox = nullptr;
    QRadioButton *m_adaptive = nullptr;
    QRadioButton *m_fixed = nullptr;
    QCheckBox   *m_startFromSettled = nullptr;
    QLabel      *m_settledValue = nullptr;
    QSpinBox    *m_startRate = nullptr;
    QSpinBox    *m_maxRate = nullptr;
    QSpinBox    *m_minRate = nullptr;
    QSpinBox    *m_checkpoint = nullptr;
    QSpinBox    *m_pollMin = nullptr;
    QSpinBox    *m_pollMax = nullptr;
    QSpinBox    *m_giveUp = nullptr;
    QSpinBox    *m_metaCopies = nullptr;
    QSpinBox    *m_maxRounds = nullptr;
    QSpinBox    *m_imageFailRestarts = nullptr;
    StatusLine  *m_status = nullptr;
};

#endif  // FLASHERPROFILEDIALOG_H
