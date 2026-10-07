#ifndef LOCOCONFIGWINDOW_H
#define LOCOCONFIGWINDOW_H
// =============================================================================
//  LocoConfigWindow
//  -----------------------------------------------------------------------------
//  Tools ▸ Loco Configuration…  --  edit a loco's LOCO_INFO, send it to the
//  VCC, and write loco_info.bin. The GUI form of loco_config_v12.cpp.
//
//  SHAPE
//    A standalone QMainWindow, single instance, like the Firmware Flasher.
//    One saved CONFIGURATION per loco (unit ID, wheels, tachos, ...), kept
//    in loco_configs.json beside dlconsole.ini; the app bar switches
//    between them. The body is the field table (lococonfigmodel.h) with a
//    group list and a search box; the bottom bar sends.
//
//  SENDING
//    One datagram, exactly v12's: STRUCT_MESSAGE_HEADER (src 28 -> dest 2,
//    message 120, length 375) + the 370-byte body with loco_info_crc
//    computed. Sent through UdpSender, DLConsole's one transmit path, to
//    the VCC's application (normal operation, port 50001 by default).
//
//    BULK SEND: up to four targets (tick, IP, port). Send goes to every
//    ticked row with an address -- the SAME bytes to each. Each target is
//    sent to and recorded separately, so one that fails is named and does
//    not stop the rest. The confirmation says what every target will get
//    (loco_unit_id and vcc_crc in particular), because the same unit id on
//    four different locos would be a mistake.
//
//    The VCC sends NOTHING back. So the window is honest about it -- it
//    says so where the Send button is -- and makes what WAS sent easy to
//    check: before sending it lists every field that differs from the last
//    send of this configuration; after, the exact bytes go into
//    loco_config_history.jsonl, from which any past send can be re-exported
//    as loco_info.bin or loaded back.
//
//  WHERE THE LAYOUT COMES FROM
//    schema/kavach.xml <packet name="LINFO">, the same element that
//    decodes @linfo captures (see lococonfigcore.h).
// =============================================================================
#include <QMainWindow>
#include <functional>
#include <QPointer>
#include <QTimer>

#include "lococonfigcore.h"

class LocoFieldFilter;
class LocoFieldModel;
class LocoConfigHistoryDialog;
class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableView;
class StatusLine;
class UdpSender;
class MessageDispatcher;

class LocoConfigWindow : public QMainWindow
{
    Q_OBJECT
public:
    // `dataDirectory`: where loco_configs.json and the send history live.
    // Empty = beside dlconsole.ini; the tests pass a temporary directory.
    explicit LocoConfigWindow(QWidget *parent = nullptr, const QString &dataDirectory = QString());
    ~LocoConfigWindow() override;

    bool isUsable() const { return m_layout.isLoaded(); }

    // Session 94: configurations to and from a .dlloco file, for another
    // PC. The menu items ask (which ones; what to do with a name already
    // used); these do the work and are public for the tests. importFrom()
    // returns the names stored (empty on failure; *error says why).
    bool        exportConfigsTo(const QString &path, const QStringList &names, QString *error = nullptr);
    QStringList importConfigsFrom(const QString &path,
                                  const std::function<ImportClash(const QString &name, bool *stop)> &onClash,
                                  QString *error = nullptr);
    QStringList configNames() const;
    LocoInfo::LocoConfig configNamed(const QString &name) const;

    // The Send button without (confirm = false) or with its confirmation box.
    // Public for the tests.
    bool sendNow(bool confirm);

    // For the tests.
    LocoFieldModel *fieldModel() const { return m_model; }
    void setTarget(const QString &ip, quint16 port);            // row 1 only; the others cleared
    void setTargets(const QVector<LocoInfo::SendTarget> &targets);
    QString configsPath() const;
    QString historyPath() const;
    QByteArray currentBody() const;
    QString sendBlocker() const;      // why Send is disabled, or empty
    // Session 158: lock / unlock a field of this configuration. Unlocking
    // with `ask` asks first. False when nothing changed (or refused).
    bool setFieldLocked(const QString &key, bool locked, bool ask = false);
    QStringList lockedFields() const { return m_config.locked; }
    // Session 158: the vcc_crc paragraph of the send confirmation (HTML).
    QString sendConfirmVccHtml() const;

    // ---- the live check: what the loco actually holds ------------------------
    // The VCC prints its LOCO_INFO (@linfo) periodically. Each one is
    // compared with what this configuration last sent (or, if never sent,
    // with the configuration itself), the loco being matched by
    // loco_unit_id. The result is the line under "Last sent".
    void setLiveSource(MessageDispatcher *dispatcher);
    void observeCaptureLine(const QString &line);   // one @-line (the tests; and the dispatcher)
    QString liveCheckText() const;
    // Re-judge now (the 5 s timer does this while a send awaits its answer).
    void recheck() { refreshLiveCheck(); }

    // How long after a send, at least, before a missing @linfo is reported.
    static constexpr qint64 kConfirmMinWaitMs = 60000;
    void setConfirmMinWaitMs(qint64 ms) { m_confirmMinWaitMs = ms; }   // tests

signals:
    void sent();
    // The first @linfo after a send, judged (session 81). Also recorded in
    // the send history. `detail` lists the differing fields.
    void verified(const QString &text, bool match, const QString &detail);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onValueEdited(const QString &key);
    void onGroupChanged();
    void onConfigChosen(int index);
    void refreshSummary();
    void saveNow();
    void newConfig();
    void duplicateConfig();
    void renameConfig();
    void deleteConfig();
    void importBin();
    void exportConfigsDialog();
    void importConfigsDialog();
    void exportBin();
    void resetToDefaults();
    void compareConfigs();
    void openHistory();

private:
    QWidget *buildAppBar();
    QWidget *buildBody();
    QWidget *buildSendBar();
    void     restyle();
    void     loadConfig(const QString &name);
    void     reloadConfigCombo();
    void     refreshGroupCounts();
    // Locked fields keep their values; returns " · N locked field(s) kept: …"
    // for the status line, or empty when the locks held nothing back.
    QString  applyValues(const LocoInfo::Values &values);
    void     scheduleSave();
    LocoInfo::Values lastSentValues() const;
    bool     writeBin(const QString &path, const QByteArray &body);

    // ---- data -------------------------------------------------------------------
    QString                 m_dataDirectory;
    QString                 m_loadError;
    LocoInfo::Layout        m_layout;
    LocoInfo::Presentation  m_presentation;
    LocoInfo::Values        m_defaults;
    QString                 m_defaultsSource;   // "loco_config_v12.cpp", from loco_defaults.json
    LocoInfo::ConfigStore  *m_store = nullptr;
    LocoInfo::SendHistory  *m_history = nullptr;
    LocoInfo::LocoConfig    m_config;
    QTimer                  m_saveTimer;

    // ---- widgets ----------------------------------------------------------------
    QFrame          *m_appBar = nullptr;
    QComboBox       *m_configCombo = nullptr;
    QLineEdit       *m_search = nullptr;
    QListWidget     *m_groups = nullptr;
    QTableView      *m_table = nullptr;
    LocoFieldModel  *m_model = nullptr;
    LocoFieldFilter *m_filter = nullptr;
    // Bulk send: kMaxTargets rows of [tick] IP Port.
    QVector<QCheckBox *> m_targetOn;
    QVector<QLineEdit *> m_targetIps;
    QVector<QSpinBox *>  m_targetPorts;
    QLabel          *m_summary = nullptr;
    QLabel          *m_vccCrc = nullptr;
    QLabel          *m_lastSent = nullptr;
    QLabel          *m_blocker = nullptr;
    QLabel          *m_liveStatus = nullptr;
    QPushButton     *m_liveLoad = nullptr;
    QTimer          *m_liveTimer = nullptr;
    struct LiveInfo {
        LocoInfo::Values values;
        qint64           seenMs = 0;
        bool             crcOk = true;
    };
    QHash<QString, LiveInfo> m_live;      // capture key ("7_1") -> its latest @linfo
    QHash<QString, qint64>   m_linfoIntervalMs;   // key -> time between its last two @linfo
    QDateTime                m_verifiedSentAt;    // the send already confirmed/refuted
    qint64                   m_confirmMinWaitMs = kConfirmMinWaitMs;
    class QTimer            *m_awaitTimer = nullptr;
    qint64 sinceSendMs() const;
    bool   awaitingOverdue(const QString &key) const;
    void   recordVerification(const QString &key, const LiveInfo &live, const QStringList &differences);
    QString                  m_liveKeyShown;
    void refreshLiveCheck();
    QPushButton     *m_sendButton = nullptr;
    StatusLine      *m_status = nullptr;
    UdpSender       *m_sender = nullptr;
    QList<QFrame *>  m_cards;
    QList<QLabel *>  m_sectionLabels;
    QPointer<LocoConfigHistoryDialog> m_historyDialog;
    bool             m_loadingConfig = false;
    class UndoLog   *m_undo = nullptr;      // session 79: Delete / Reset undo
    class QAction   *m_actUndo = nullptr;
public:
    UndoLog *undoLog() const { return m_undo; }
};

#endif  // LOCOCONFIGWINDOW_H
