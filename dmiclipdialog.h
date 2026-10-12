#ifndef DMICLIPDIALOG_H
#define DMICLIPDIALOG_H

// =============================================================================
//  DMI clip… (session 262) — the dialog over dmiclip.h. Opened from a log
//  row's right-click ("DMI clip around here…", the row's moment ±30 s) and
//  from the DMI window ("Export clip…", the moment it follows, or the newest
//  frame). Loco, From / To, speed and size; it says, as they change, how
//  many DMI frames that is and how long the clip will play; Export GIF…
//  draws it and writes it.
// =============================================================================

#include <QDialog>
#include <QPointer>

#include "dmiclip.h"

class LogModel;
class QComboBox;
class QDateTimeEdit;
class QLabel;
class QPushButton;
class StatusLine;

class DmiClipDialog : public QDialog
{
    Q_OBJECT
public:
    // `centreMs`: the moment the clip is around (±30 s by default).
    // `loco`: the loco to start on ("" = the first with @dmi frames).
    DmiClipDialog(LogModel *model, const QString &tabName, qint64 centreMs, const QString &loco,
                  QWidget *parent = nullptr);

    DmiClip::Options options() const;
    void setRange(qint64 fromMs, qint64 toMs);
    void setSpeed(int speed);
    void setScale(int pct);
    void setLoco(const QString &key);
    QStringList locos() const;
    // Draw and write the clip to `path` (what Export does after its file
    // dialog). False, with the reason on the status line, when refused.
    bool exportTo(const QString &path);
    QLabel *previewLabel() const { return m_preview; }
    StatusLine *status() const { return m_status; }

private:
    void refreshPreview();

    QPointer<LogModel> m_model;
    QString m_tabName;
    QComboBox *m_loco = nullptr;
    QDateTimeEdit *m_from = nullptr;
    QDateTimeEdit *m_to = nullptr;
    QComboBox *m_speed = nullptr;
    QComboBox *m_scale = nullptr;
    QLabel *m_preview = nullptr;
    QPushButton *m_export = nullptr;
    StatusLine *m_status = nullptr;
};

#endif // DMICLIPDIALOG_H
