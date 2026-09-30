#ifndef DLRPLAYERDIALOG_H
#define DLRPLAYERDIALOG_H

// =============================================================================
//  DlrPlayerDialog
//  -----------------------------------------------------------------------------
//  Front end for DlrPlayer: pick archives, pick a target, press play.
//
//  This is the one tool in DLConsole whose output is indistinguishable from
//  real traffic on purpose, so the dialog is built around making that
//  obvious rather than around hiding it. The warning is permanent, not a
//  one-time confirmation that gets clicked through and forgotten, and the
//  target port defaults to the port this instance is listening on because
//  that is what the feature is for — but it is editable, so a second
//  instance on another port can be the target instead when you want the
//  replay kept out of today's recording.
// =============================================================================

#include <QDialog>

#include "statusline.h"
#include <QVector>

#include "dlrplayer.h"

class ColorRules;
class QueryLineEdit;
class RawBytesPanel;
class FieldInspector;
class QTabWidget;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTreeWidget;

class DlrPlayerDialog : public QDialog
{
    Q_OBJECT

public:
    // `rules` is borrowed for the dialog's lifetime and only ever read on
    // the GUI thread — DlrPlayer takes its own copy before the worker
    // starts, so nothing crosses threads. May be null, in which case
    // sev: terms in a seek query have nothing to classify against.
    explicit DlrPlayerDialog(const ColorRules *rules, QWidget *parent = nullptr);
    ~DlrPlayerDialog() override;

    // Pre-load archives — used when the dialog is opened from a context
    // menu on a session file.
    void addFiles(const QStringList &paths);

protected:
    // Escape during a replay stops the replay and keeps the window. The
    // rule is the one in sendguard.h; this one puts records into the live
    // log rather than onto the wire, so there is no confirmation on close
    // — but losing the window mid-replay still leaves the operator with a
    // half-fed log and nothing on screen saying where it stopped.
    void reject() override;

private slots:
    void onAdd();
    void onRemove();
    void onPlayPause();
    void onStop();
    void onSpeedChanged();
    void onSeekQueryEdited();
    void onSkipToMatch();
    void onSeekLanded(qint64 playbackMs, QString text);
    void onSeekExhausted();
    void onToggleDecode(bool on);
    void onStarted(qint64 totalRecords, qint64 spanMs);
    void onProgress(DlrPlayer::Stats s);
    void onFinished(bool completed);
    void onFailed(QString reason);

private:
    // Render `wire` in the decode panels. Empty clears them.
    void showDecoded(const QByteArray &wire, qint64 arrivalMs);

    void refreshList();
    void refreshTimeline();
    void setRunningUi(bool running);
    double selectedSpeed() const;
    static QString fmtDuration(qint64 ms);

    DlrPlayer *m_player = nullptr;
    QVector<DlrPlayer::Source> m_sources;

    QTreeWidget  *m_list      = nullptr;
    QLabel       *m_timeline  = nullptr;
    QLineEdit    *m_host      = nullptr;
    QSpinBox     *m_port      = nullptr;
    QComboBox    *m_speed     = nullptr;
    QSpinBox     *m_maxGap    = nullptr;
    QSpinBox     *m_startAt   = nullptr;
    QueryLineEdit *m_seekQuery = nullptr;
    QPushButton  *m_skipBtn   = nullptr;
    QCheckBox    *m_showDecode = nullptr;
    QCheckBox    *m_follow     = nullptr;
    QTabWidget   *m_decodeTabs = nullptr;
    RawBytesPanel  *m_rawPanel  = nullptr;
    FieldInspector *m_fieldPanel = nullptr;
    QCheckBox    *m_loop      = nullptr;
    QPushButton  *m_playBtn   = nullptr;
    QPushButton  *m_stopBtn   = nullptr;
    QPushButton  *m_addBtn    = nullptr;
    QPushButton  *m_rmBtn     = nullptr;
    QProgressBar *m_progress  = nullptr;
    QLabel       *m_stats     = nullptr;
    StatusLine   *m_status    = nullptr;

    const ColorRules *m_rules = nullptr;
    bool m_running = false;
};

#endif // DLRPLAYERDIALOG_H
