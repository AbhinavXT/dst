#ifndef PACKETSEQUENCEDIALOG_H
#define PACKETSEQUENCEDIALOG_H

// =============================================================================
//  PacketSequenceDialog
//  -----------------------------------------------------------------------------
//  Sends a list of saved presets in order, with a repeat count and a delay
//  after each step.
//
//  A single packet, however carefully composed, only asks a receiver one
//  question. The things worth testing are transitions — an authority followed
//  by a revision, a health report followed by a fault — and those need two or
//  three frames in a defined order with defined gaps. Doing that by hand means
//  editing the Packet Maker between sends, which is both slow and impossible
//  to repeat identically.
//
//  Steps are FILE REFERENCES, not copies. A sequence that inlined its presets
//  would go stale the moment one of them was corrected, and the correction
//  would silently not apply — which is the failure mode you would least want
//  in something whose whole purpose is repeatability. The cost is that moving
//  a preset breaks the sequence, which at least announces itself.
//
//  Every step is built through PacketBuilder and self-verified, exactly as
//  pressing Build does. A step that fails to build stops the run rather than
//  being skipped: a sequence that silently omitted a frame would still report
//  success while testing something other than what was asked for.
// =============================================================================

#include <QDialog>
#include <QVector>

#include "packetbuilder.h"
#include "packetpreset.h"
#include "udpsender.h"

class QCheckBox;
class QLabel;
class StatusLine;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTimer;

class PacketSequenceDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PacketSequenceDialog(QWidget *parent = nullptr);
    ~PacketSequenceDialog() override;

protected:
    // Escape and the close box while a sequence is running — see sendguard.h.
    void reject() override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onAddStep();
    void onRemoveStep();
    void onMoveUp();
    void onMoveDown();
    void onRunStop();
    void onSaveSequence();
    void onLoadSequence();
    void onTick();
    void onSenderError(const QString &what);

private:
    struct Step {
        QString path;          // preset file
        QString captype;       // cached from the preset, for the table
        int     repeats    = 1;
        int     intervalMs = 200;   // gap between repeats within the step
        int     delayMs    = 500;   // gap after the step's last datagram
        bool    loaded     = false;
        PacketPreset preset;
    };

    void  addStepRow(const Step &s);
    QVector<Step> readSteps() const;    // reads the table, reloading presets
    void  setRunning(bool on);
    void  advance();                    // fire the next datagram or step
    void  finish(const QString &why, bool ok);
    QByteArray sessionKey() const;

    PacketBuilder m_builder;
    UdpSender     m_sender;
    QTimer       *m_timer = nullptr;

    QVector<Step> m_run;        // snapshot taken at Run
    int  m_step      = 0;
    int  m_sendInStep = 0;
    int  m_totalSends = 0;
    int  m_sentSoFar  = 0;
    bool m_running    = false;

    QTableWidget *m_table    = nullptr;
    QLineEdit    *m_dest     = nullptr;
    QLineEdit    *m_key      = nullptr;
    QCheckBox    *m_loop     = nullptr;
    QPushButton  *m_runBtn   = nullptr;
    QProgressBar *m_progress = nullptr;
    StatusLine   *m_status   = nullptr;
};

#endif  // PACKETSEQUENCEDIALOG_H
