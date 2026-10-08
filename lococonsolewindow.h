#ifndef LOCOCONSOLEWINDOW_H
#define LOCOCONSOLEWINDOW_H

// =============================================================================
//  LocoConsoleWindow
//  -----------------------------------------------------------------------------
//  A separate top-level window showing the live loco capture stream
//  (ui_capture.c), which arrives as LogEntry::text via the shared
//  MessageDispatcher. One tab per packet type, each a Field|Value table built
//  by CaptureDecoder::describe() for the latest frame of that type, plus a
//  "Link" overview tab (per-type rate / last-seen / seq / CRC). A loco/ctrl
//  selector picks which source the tabs reflect.
//
//  Find (Ctrl+F, F3 / Shift+F3): a TableFindBar under the tabs searches the
//  current tab's fields by name -- the way to get to one member of a
//  170-row struct like LINFO. The tables are rebuilt on every refresh, so
//  the bar is re-applied after each one (see tablefindbar.h).
//
//  Recording (multi-source):
//    The Record button opens a selection dialog listing every seen loco/ctrl
//    key. Each selected source is streamed to its OWN .cap file (base path
//    chosen once, a "_<loco>_<ctrl>" suffix added per source). Files are
//    opened lazily on the first matching line. Keeping one key per file means
//    every produced capture replays correctly in ReplayWindow, whose load()
//    does not filter by key.
// =============================================================================

#include <QMainWindow>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QVector>

#include "capturedecoder.h"
#include "dmitimetravel.h"
#include "livefields.h"
#include "logentry.h"

class MessageDispatcher;
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QTabWidget;
class QTimer;
class TableFindBar;
class BigNumberPanel;
class QCheckBox;
class QMenu;
class QFile;
class QTextStream;

class LocoConsoleWindow : public QMainWindow
{
    Q_OBJECT

public:
    // `tags`: MainWindow's (session 96); nullptr = a private one.
    explicit LocoConsoleWindow(MessageDispatcher *dispatcher, QWidget *parent = nullptr, class TabTags *tags = nullptr,
                               const class SessionKeyStore *keys = nullptr);
    ~LocoConsoleWindow() override;

protected:
    // Remember size/position for the next opening.
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onEntryAppended(QString tabKey, LogEntryPtr entry);
    void onSelectionChanged(int index);
    void onRefreshTick();
    void onToggleRecord();
    void onOpenReplay();
    void onTabChanged(int index);

signals:
    // A plot opened from a tile asked to show its message (session 82).
    void jumpRequested(const QString &tabKey, qint64 epochMs);

public:
    // Double-click / menu on big-number tile `index` (session 82).
    void plotTile(int index);
    // Session 82: the cab view and the two light strips.
    class CabDisplay   *cabDisplay() const { return m_cab; }
    class LinkLights   *linkLights() const { return m_linkLights; }
    class HealthLights *healthLights() const { return m_healthLights; }
    // Select a source by key ("7_1"), as picking it in the selector does.
    void selectSource(const QString &key);
    // Fields added to the cab view (session 83): saved as
    // lococonsole/cabFields. Adding one already there is refused.
    bool addCabField(const LiveFieldRef &ref);
    bool removeCabField(int index);
    QVector<LiveFieldRef> cabFields() const { return m_cabFields; }

    // For the tests: the find bar and the per-type table for a tab label.
    TableFindBar *findBar() const { return m_find; }
    QTableWidget *tableForLabel(const QString &label) const;
    QTabWidget   *tabs() const { return m_tabs; }
    QLabel       *missionChip() const { return m_lblMission; }
    // Session 182: each part's minimum width, for a width check's failure
    // message (the Linux CI renders fonts wider than the Mac).
    QString minimumWidths() const;

    // Session 169: follow the cursor, as the DMI window does (84): every
    // packet tab, the big numbers and the cab view show each type's latest
    // frame AT OR BEFORE the moment picked in a tab or a replay window,
    // instead of the live one. The Link tab and the rates stay live.
    void setFollowCursor(bool on);
    bool followCursor() const { return m_following; }
    void showMoment(const DmiMoment &moment);
    // Save snapshot: .png the window as shown; .txt / .csv every packet
    // type's decoded fields for the selected loco (live, or at the moment).
    bool saveSnapshot(const QString &path);
    QString snapshotText(bool csv);

private:

private:
    struct LinkStat {
        quint64 count      = 0;
        quint64 prevCount  = 0;
        double  rate       = 0.0;
        qint64  lastSeenMs = 0;
        quint32 lastSeq    = 0;
        quint64 crcOkCount = 0;
        quint64 crcFail    = 0;
        double  intervalMs = 0.0;   // running average gap between frames (session 82)
    };

    struct LocoState {
        int       locoId = -1;
        int       ctrlId = -1;
        QDateTime rtc;

        QMap<int, LinkStat>    link;     // (int)CapType -> stats
        QMap<int, CaptureLine> latest;   // (int)CapType -> latest full frame

        bool    seqInit = false;
        quint32 lastSeq = 0;
        quint64 seqGaps = 0;
        quint64 total   = 0;

        // Session 168: start of mission, from ARP (CaptureDecoder::
        // isStartOfMission). Times are the frames' own (RTC).
        bool      inMissionStart = false;
        QDateTime missionFrom, missionTo;
        QString   missionThen;               // LOCO_MODE of the ARP that followed

        // Session 170: the loco's own ID (SOURCE_LOCO_ID of its ARP /
        // LSRP), and the received ARPs that carried it: rejected, not
        // shown as "another loco" on the arprecv tab.
        qint64    ownLocoId = -1;
        quint64   ownArpRecv = 0;
        QDateTime ownArpRecvLast;
    };

    // One open output for a single recorded source.
    struct RecordSink {
        QFile       *file   = nullptr;
        QTextStream *stream = nullptr;
    };

    void buildUi();
    void addTypeTab(CapType t);
    void ingest(const CaptureLine &c, qint64 nowMs);
    void learnOwnId(LocoState &st, qint64 id);
    void refreshHeader(const LocoState &st, qint64 nowMs);
    void refreshLink  (const LocoState &st, qint64 nowMs);
    void refreshTypeTables(const LocoState &st, qint64 nowMs);
    static QString ageText(qint64 ageMs);

    // ---- multi-source recording -------------------------------------
    // Returns false if the user cancelled. On success, outKeys holds the
    // explicitly chosen source keys and outAutoNew whether sources that
    // appear later should also be captured.
    bool    promptRecordSelection(QSet<QString> &outKeys, bool &outAutoNew);
    void    startRecording(const QSet<QString> &keys, bool autoNew);
    void    stopRecording();
    void    routeRecord(const QString &key, const QString &rawLine);
    QString sinkPathFor(const QString &key) const;
    void    updateRecordButton();
    static QString replayDirPath();    // ensure + return the absolute replay/ folder

    MessageDispatcher *m_dispatcher = nullptr;

    QHash<QString, LocoState> m_states;
    QString                   m_selectedKey;
    bool                      m_dirty = false;

    QComboBox    *m_selector  = nullptr;
    QLabel       *m_lblRtc    = nullptr;
    QLabel       *m_lblRate   = nullptr;
    QLabel       *m_lblCrc    = nullptr;
    QLabel       *m_lblSeq    = nullptr;
    QLabel       *m_lblMission = nullptr;     // session 168
    // Session 169: following the cursor.
    QCheckBox    *m_chkFollow = nullptr;
    bool          m_following = false;
    DmiMoment     m_moment;
    QHash<int, qint64> m_momentFrameMs;      // type -> its frame's time, at the moment
    LocoState     momentView(const LocoState &live);
    QTabWidget   *m_tabs      = nullptr;
    QTableWidget *m_linkTable = nullptr;
    QMap<int, QTableWidget*>  m_typeTables;   // (int)CapType -> field table
    QTimer       *m_refresh   = nullptr;
    TableFindBar *m_find      = nullptr;

    // Colour tags follow the loco here too. A tag is stored per TAB key (the
    // UDP header's source id / kavach id); the selector lists the capture
    // line's own loco / ctrl ids. They are not assumed to be the same
    // numbers: each capture key maps to the tab its lines arrived on.
    QHash<QString, QString> m_tabKeyFor;
    void refreshTagDisplay();

    // ---- big numbers (bignumberpanel.h) ------------------------------------
    BigNumberPanel *m_bigNumbers = nullptr;
    QPushButton    *m_btnBigNumbers = nullptr;
    void refreshBigNumbers(const LocoState &st, qint64 nowMs);

    // ---- change highlighting -------------------------------------------------
    // Per type table: each row's last value and when it last changed, keyed
    // by field name + occurrence (names repeat inside some structs). Reset
    // when the watched loco changes, so switching locos does not light up
    // every row.
    QCheckBox *m_chkHighlight = nullptr;
    QHash<int, QHash<QString, QString>> m_lastValues;
    QHash<int, QHash<QString, qint64>>  m_changedAt;
    void highlightChanges(qint64 nowMs);

    // ---- right-click on a field: big number, pin, copy ----------------------
    void showFieldMenu(CapType type, QTableWidget *table, const QPoint &pos);

public:
    // For the tests.
    BigNumberPanel *bigNumbers() const { return m_bigNumbers; }
    bool isRowHighlighted(const QString &typeLabel, int row) const;
    // Hold, then fade: a changed row is fully marked for kChangeHoldMs and
    // gone after kChangeFadeMs.
    static const int kChangeHoldMs = 2000;
    static const int kChangeFadeMs = 4000;
private:

    QPushButton  *m_btnRecord = nullptr;

    // Recording state. m_recSinks maps a source key -> its open .cap file.
    // A key present with a null stream means "open failed, don't retry".
    bool                      m_recording  = false;
    bool                      m_recAutoNew = false;   // capture sources seen after start
    QSet<QString>             m_recWanted;            // explicitly selected keys
    QString                   m_recDir;               // absolute replay/ folder for this session
    QString                   m_recStamp;             // ddMMyyyy_HHmmss, fixed at record start
    QMap<QString, RecordSink> m_recSinks;             // key -> open file/stream
    class UndoLog *m_undo = nullptr;     // session 79: Ctrl+Z for big-number edits
    class QAction *m_actUndo = nullptr;
public:
    UndoLog *undoLog() const { return m_undo; }
    // ---- session 82 -------------------------------------------------------------
    void refreshCabAndLights(const LocoState &st, qint64 nowMs);
    class CabDisplay   *m_cab = nullptr;
    class LinkLights   *m_linkLights = nullptr;
    class HealthLights *m_healthLights = nullptr;
    QPushButton        *m_btnCab = nullptr;
    QVector<LiveFieldRef> m_cabFields;
    void saveCabFields() const;
    void showCabMenu(const QPoint &pos);
    class TabTags *m_tags = nullptr;       // not owned (unless made here)
    const class SessionKeyStore *m_keys = nullptr;   // session 96: live keys for the SLRP MAC row; not owned
};

#endif // LOCOCONSOLEWINDOW_H
