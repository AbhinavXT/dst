#ifndef FIELDSWEEPDIALOG_H
#define FIELDSWEEPDIALOG_H
// =====================================================================
//  fieldsweepdialog.{h,cpp} -- run a field sweep against a live target.
//
//  Pick a packet type, set the base field values (or seed them from a
//  captured frame), choose one field to walk, and send. Each value goes
//  out once; the log is watched for a configurable window afterwards and
//  the step is scored reply / fault only / other traffic / silent.
//
//  Header fields only. Sub-packet repeats have per-row values with no
//  single name to address, which is the same boundary PacketVary drew and
//  for the same reason -- it needs its own design rather than being
//  bolted on here.
//
//  This TRANSMITS. Like the Packet Maker it is explicit and opt-in, and
//  it stops on the first build failure rather than skipping a value and
//  leaving a gap the results table would not show.
// =====================================================================
#include <QDialog>
#include <QElapsedTimer>
#include <QHash>
#include <QVector>

#include "fieldsweep.h"
#include "logentry.h"
#include "packetbuilder.h"
#include "udpsender.h"

class SessionKeyStore;
class QCheckBox;
class QComboBox;
class QLabel;
class StatusLine;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

class FieldSweepDialog : public QDialog
{
    Q_OBJECT
public:
    // `keys`: MainWindow's session keys (session 96); nullptr = a private, empty store.
    explicit FieldSweepDialog(QWidget *parent = nullptr, SessionKeyStore *keys = nullptr);

    // Every log entry, live. The dialog decides which ones belong to the step
    // in flight; MainWindow does not need to know the rules.
    void observe(const LogEntryPtr &entry);

    // Fill the base field values from a captured frame, so a sweep starts from
    // something real rather than from a form full of zeros.
    void seedFromBuffer(const QString &bufferText);

protected:
    // Escape and the close box while a sweep is running — see sendguard.h.
    void reject() override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onPacketChanged();
    void onFieldChanged();
    void onPreview();
    void onStartStop();
    void onStep();               // send the next value
    void refreshKeySets();

private:
    void  setRunning(bool on);
    void  closeStep(int row);    // window expired: score it
    QHash<QString, qint64> baseValues() const;
    FieldSweep::Spec       currentSpec(QString *err) const;

    PacketBuilder m_builder;
    UdpSender     m_sender;

    // --- inputs
    QComboBox   *m_packetBox  = nullptr;
    QComboBox   *m_fieldBox   = nullptr;
    QComboBox   *m_modeBox    = nullptr;
    QLineEdit   *m_fromEdit   = nullptr;
    QLineEdit   *m_toEdit     = nullptr;
    QLineEdit   *m_stepEdit   = nullptr;
    QLineEdit   *m_listEdit   = nullptr;
    QTableWidget*m_baseTable  = nullptr;   // base header values
    QLineEdit   *m_destEdit   = nullptr;
    QSpinBox    *m_portSpin   = nullptr;
    QSpinBox    *m_gapSpin    = nullptr;   // ms between sends
    QSpinBox    *m_windowSpin = nullptr;   // ms to wait for an answer
    QLineEdit   *m_replyEdit  = nullptr;   // captypes that count as an answer
    QComboBox   *m_keySetBox  = nullptr;
    QLineEdit   *m_keyEdit    = nullptr;
    QCheckBox   *m_stopOnReply = nullptr;

    // --- run state
    QPushButton *m_startBtn = nullptr;
    StatusLine  *m_status   = nullptr;
    QTableWidget*m_results  = nullptr;
    QTimer       m_stepTimer;

    QVector<FieldSweep::Step> m_plan;
    int    m_index = -1;                 // step in flight, -1 when idle
    bool   m_running = false;
    QElapsedTimer m_sinceSend;
    QVector<QVector<FieldSweep::Observation>> m_obs;   // per step
    SessionKeyStore *m_keys = nullptr;     // session 96: not owned (unless made here)
};

#endif  // FIELDSWEEPDIALOG_H
