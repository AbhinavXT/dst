#ifndef SERIALMANAGER_H
#define SERIALMANAGER_H

// =============================================================================
//  SerialManager (session 102)
//  -----------------------------------------------------------------------------
//  The serial ports the console has open, and the "feed console" path from
//  each into the dispatcher. Owned by MainWindow and handed out, like
//  FrameNumberWatch: session state, not a singleton.
//
//  WHY IT IS NOT THE WINDOW
//    Until session 101 the port and Feed console belonged to the terminal
//    window, so closing that window stopped the capture. During a trial
//    that is one stray click away from silently losing the IOA's input /
//    output / analog logs. Now a port runs until someone presses Close (or
//    the console exits); the terminal is a viewer of it, and the status bar
//    shows a chip per open port.
//
//  One SerialLink per port name, kept for the manager's lifetime once made,
//  so a window holding the pointer never sees it deleted under it. Port
//  names are matched case-insensitively ("COM3" is "com3" on Windows).
//
//  Settings are per port (serial/ports/<name>): with three IOA cards open,
//  each window used to save to the one group serial/last, and whichever
//  closed last overwrote the other two.
// =============================================================================

#include <QHash>
#include <QObject>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

#include "seriallink.h"
#include "serialportscan.h"

#include <QSet>

class MessageDispatcher;
class QSettings;

// Line health of one port (session 104): is the card talking, and are we
// reading it right? A wrong baud rate shows up at once as ~0% decoding.
//
// "Decodes" = a well-formed @ capture line of a type the console knows
// (CaptureDecoder::parseLine). That is deliberately cheap — every line of
// every port goes through it — and it is what wrong line settings break.
class SerialLineHealth
{
public:
    void reset() { *this = SerialLineHealth(); }
    void note(bool decodes, qint64 ms);

    struct Stats {
        int    lines = 0;
        int    decoded = 0;
        double perSecond = 0;          // over the last kRateWindowMs
        qint64 longestGapMs = 0;       // between two lines, since open
        qint64 sinceLastMs = -1;       // -1: no line yet
        int    decodePercent() const { return lines ? int(qRound(100.0 * decoded / lines)) : 100; }
        // Enough lines to judge, and most of them garbage.
        bool   failing() const { return lines >= kJudgeAfter && decodePercent() < 50; }
    };
    Stats stats(qint64 nowMs) const;

    static bool lineDecodes(const QByteArray &line);
    static constexpr qint64 kRateWindowMs = 5000;
    static constexpr int    kJudgeAfter = 10;

private:
    int    m_lines = 0, m_decoded = 0;
    qint64 m_lastMs = -1;
    qint64 m_longestGapMs = 0;
    QVector<qint64> m_recent;          // line times inside the rate window
};

// A one-click command for the terminal's macro row (session 109): STATUS?,
// DIAG?, RESET. Text takes the send box's escapes (\r \n \xHH), or is hex
// bytes; `ending` goes after it. `confirm` asks before sending — for the
// ones the card ACTS on.
struct SerialMacro {
    QString    label;
    QString    text;
    bool       hex = false;
    QByteArray ending = "\r\n";
    bool       confirm = false;

    // The bytes it sends; ok false (nothing to send) for hex that is not
    // hex — refused rather than sending something other than what was set.
    QByteArray bytes(bool *ok = nullptr) const;
    // RESET, REBOOT, ERASE, FORMAT, FLASH, FACTORY, DELETE, CLEAR, BURN,
    // WRITE: the words that make an editor tick "confirm" by default.
    static bool looksDangerous(const QString &labelOrText);

    static QVector<SerialMacro> loadList(QSettings &s, const QString &arrayName);
    static void saveList(QSettings &s, const QString &arrayName, const QVector<SerialMacro> &macros);
};

// A named port setup (session 105): "IOA Input = COM5 115200 8N1, feed
// on, auto-open". Saved in serial/profiles; opening one sets everything at
// once, "Open all" starts every IOA card in one click, and auto-open ones
// start with DLConsole.
struct SerialProfile {
    QString      name;
    SerialConfig config;
    bool         feed = true;
    bool         autoOpen = false;
    QVector<SerialMacro> macros;     // session 109: its own macro row
    // Session 115: the adapter, so the profile finds it again under a new
    // COM number. Empty for ports with none (virtual, built-in UARTs).
    QString      usbSerial;
    // Session 156: the USB socket it was saved on, for adapters with no
    // serial number of their own (CH340, PL2303, clone FTDI chips).
    QString      usbLocation;

    static QVector<SerialProfile> loadAll(QSettings &s);
    static void saveAll(QSettings &s, const QVector<SerialProfile> &profiles);
    // Replace a profile of the same name (case-insensitive), or add it.
    static void upsert(QVector<SerialProfile> *profiles, const SerialProfile &p);
    static bool remove(QVector<SerialProfile> *profiles, const QString &name);
    static int  indexOf(const QVector<SerialProfile> &profiles, const QString &name);
};

class SerialManager : public QObject
{
    Q_OBJECT
public:
    explicit SerialManager(MessageDispatcher *dispatcher, QObject *parent = nullptr);
    ~SerialManager() override;

    MessageDispatcher *dispatcher() const { return m_dispatcher; }

    // The link for a port, made (closed) on first ask. Never null.
    SerialLink *linkFor(const QString &portName);
    // Null when the port has never been asked for.
    SerialLink *link(const QString &portName) const;

    // Opens `config.portName` (closing and reopening it if it was open with
    // other settings). False with the reason in the link's errorText().
    bool open(const SerialConfig &config);
    // Session 156: the same, without waiting (see SerialLink::openAsync).
    // openFinished() follows.
    void openAsync(const SerialConfig &config);
    void close(const QString &portName);
    void closeAll();

    QStringList openPorts() const;           // sorted, as the user typed them

    // Auto-reconnect (session 106). A port the DRIVER lost (the adapter was
    // pulled; not a Close) is waited for: each tick the manager looks for
    // the same adapter by its USB serial number, under any name, and
    // reopens it with the same settings, feed and label — into the same
    // console tab, with "── lost … ──" and "── reconnected, gap 4.2 s ──"
    // lines in it so the gap in the log is visible. Close stops the wait.
    QStringList reconnectingPorts() const;
    bool isReconnecting(const QString &portName) const;
    // Tests: a faster tick, and a stand-in for the port enumerator.
    void setReconnectIntervalMs(int ms);
    //
    // Session 156: by default the search runs on a scan thread
    // (SerialPortScanner) and matches by unique serial number, then USB
    // socket, then model, then name (SerialScan::planReconnects), and the
    // reopen does not block. setPortFinder() replaces all of that with the
    // old synchronous name lookup -- kept for the session 106/115 tests.
    using PortFinder = std::function<QString(const QString &usbSerial, const QString &lastName)>;
    void setPortFinder(PortFinder f) { m_finder = std::move(f); }
    // Tests: a stand-in for the enumerator, called on the scan thread.
    void setPortScanFunction(SerialPortScanner::ScanFn fn) { m_scanner->setScanFunction(std::move(fn)); }
    SerialPortScanner *scanner() const { return m_scanner; }
    // The adapter a port is known by (from the scan after it opened).
    SerialAdapterId adapterOf(const QString &portName) const;
    // A port's USB serial number, or empty when it has none -- or shares it
    // with another adapter present (clone chips), which identifies nothing.
    static QString usbSerialFor(const QString &portName);
    static QString usbLocationFor(const QString &portName);
    static QString findPortBySerial(const QString &usbSerial, const QString &lastName);

    // CONFIRMING IT IS THE SAME CARD (session 156)
    //   After a reconnect, the first log lines are checked against the
    //   packet types this port's tab had before the loss (@dip1, @dop2, ...).
    //   The lines wait -- held, not fed -- until one of a known type arrives;
    //   then they go into the console in order. If kVerifyLines log lines
    //   arrive and none is of a known type (or kVerifyMs passes with only
    //   unknown types), the port is SUSPECT: it stays open and visible in its
    //   terminal, but its lines are kept out of the console tab, which says
    //   why. feedAnyway() (the terminal's button), or a Close and an Open,
    //   accepts it. kVerifyMs with no log line at all: fed, and said so.
    static constexpr int    kVerifyLines = 5;
    static constexpr qint64 kVerifyMs = 3000;
    bool isVerifying(const QString &portName) const;
    bool isSuspect(const QString &portName) const;
    QString suspectText(const QString &portName) const;
    void feedAnyway(const QString &portName);
    // "dop1" from "@dop1_2_1 2026-06-29T13:41:29 ...": the type, without the
    // loco's suffix. Empty for anything that is not an @ line.
    static QString captureTypeOf(const QByteArray &line);

    // Opens a profile's port with its settings and feed, labelled with the
    // profile's name. A profile that knows its adapter opens it wherever it
    // now is (session 115). False with the reason in the link's errorText().
    bool openProfile(const SerialProfile &profile);
    // Where a profile's port is now: its adapter's current name when that
    // can be found, else the name it was saved with.
    QString resolveProfilePort(const SerialProfile &profile) const;
    // Opens each; returns "name: why" for each that failed (empty = all open).
    QStringList openProfiles(const QVector<SerialProfile> &profiles);
    // Session 156: the same, without the GUI waiting on the enumerator or
    // the driver. One scan finds every profile's adapter; each opens on its
    // reader thread; profilesOpened() reports once all have finished. A
    // profile that knows its adapter and cannot find it is NOT opened by its
    // old name, which may now be another card.
    void openProfilesAsync(const QVector<SerialProfile> &profiles);

    // A port's label: the profile it was opened from, else empty. Shown on
    // its chip and as its console tab's title.
    void setLabel(const QString &portName, const QString &label);
    QString label(const QString &portName) const;
    // "IOA Input" for a labelled port, else "Serial COM5".
    QString titleFor(const QString &portName) const;

    // Feed console: every received line also enters the dispatcher, into a
    // tab named "Serial <port>".
    void setFeed(const QString &portName, bool on);
    bool feeds(const QString &portName) const;
    int  fedLines(const QString &portName) const;
    void resetFedLines(const QString &portName);     // and the line health

    // Line health of a port since it opened.
    SerialLineHealth::Stats health(const QString &portName, qint64 nowMs) const;
    // One line for a status bar or tooltip: "12.0 lines/s · 100% decode ·
    // longest gap 1.2 s · 0 driver errors".
    QString healthText(const QString &portName, qint64 nowMs) const;

    // The console tab a port feeds.
    static constexpr quint8 kSerialSourceId = 254;
    static quint16 kvchForPort(const QString &portName);
    static QString tabKeyFor(const QString &portName);
    static QString tabTitleFor(const QString &portName);   // "Serial COM3"
    static QString shortName(const QString &portName);     // "/dev/ttyUSB0" -> "ttyUSB0"

    // Per-port remembered settings: the line configuration and Feed console.
    static QString settingsGroup(const QString &portName);
    // Session 115: settings are ALSO kept under the adapter's USB serial
    // number when it has one, and that copy wins when loading — Windows
    // renumbers COM ports, and settings keyed only by "COM5" were lost
    // when the same adapter came back as COM7. `usbSerial` defaults to the
    // adapter now on that port; tests pass one.
    static QString adapterGroup(const QString &usbSerial);
    static void savePortSettings(QSettings &s, const SerialConfig &config, bool feed);
    static void savePortSettings(QSettings &s, const SerialConfig &config, bool feed,
                                 const QString &usbSerial);
    // False when nothing was saved for that port (or adapter) yet.
    static bool loadPortSettings(QSettings &s, const QString &portName,
                                 SerialConfig *config, bool *feed);
    static bool loadPortSettings(QSettings &s, const QString &portName,
                                 SerialConfig *config, bool *feed, const QString &usbSerial);

signals:
    void portsChanged();                     // a port opened, closed, was lost or came back
    void portLost(const QString &portName, const QString &why);
    void portReconnected(const QString &portName, qint64 gapMs);
    // Session 156.
    void openFinished(const QString &portName, bool ok, const QString &error);
    void profilesOpened(int opened, const QStringList &failed);
    void portSuspect(const QString &portName, const QString &why);

private:
    struct Entry {
        SerialLink *link = nullptr;
        bool feed = true;
        int  fedLines = 0;
        SerialLineHealth health;
        QString label;
        QString tabPort;            // the name its console tab is keyed by (the first)
        QString usbSerial;          // the adapter, for finding it again
        qint64  lostAtMs = 0;       // > 0: lost, waiting for it
        bool    reconnecting = false;   // a reopen after a loss is in flight
        // Session 156.
        SerialAdapterId adapter;        // what it is known by, for reconnect
        bool    needIdentity = false;   // opened; the next scan says what it is
        bool    asyncOpening = false;   // an openAsync() by the operator in flight
        int     batch = -1;             // the openProfilesAsync() it belongs to
        QString reconnectFrom;
        SerialMatchBy reconnectBy = SerialMatchBy::None;
        QSet<QString> typesSeen;        // packet types its tab has had
        bool    verifying = false;
        quint64 verifyRound = 0;
        QVector<QPair<QByteArray, qint64>> held;
        QStringList strangers;          // types seen while verifying
        int     strangerLines = 0;
        bool    suspect = false;
        QString suspectText;
    };
    using EntryPtr = QSharedPointer<Entry>;
    static QString key(const QString &portName) { return portName.trimmed().toUpper(); }
    EntryPtr entry(const QString &portName) const;
    QVector<EntryPtr> entries() const;
    void onLine(Entry *e, const QByteArray &line, qint64 ms);
    void onLost(Entry *e, const QString &why);
    void marker(Entry *e, const QString &text, qint64 ms);
    void tryReconnect();
    void updateReconnectTimer();
    // Session 156.
    void onScanned(const SerialPortList &ports);
    void startReconnect(Entry *e, const QString &portName, SerialMatchBy by);
    void finishReconnect(Entry *e);
    void feedLine(Entry *e, const QByteArray &line, qint64 ms);
    void endVerify(Entry *e, bool confirmed);
    void markSuspect(Entry *e);
    void verifyTimeout(Entry *e, quint64 round);
    void resetCardCheck(Entry *e);
    void finishBatchItem(Entry *e, bool ok, const QString &error);
    struct Batch { int pending = 0; int opened = 0; QStringList failed; };

    MessageDispatcher        *m_dispatcher = nullptr;
    // Keyed by port name; a port renamed on reconnect is one entry under
    // both names.
    QHash<QString, EntryPtr>  m_ports;
    // Session 156: every entry ever made, in order, for the manager's
    // lifetime. m_ports is only the name -> entry index: after a swap (two
    // adapters replugged in the other order) one name moves from one entry
    // to the other, and an entry reachable by no name must still not be
    // freed -- its link's signal handlers point at it.
    QVector<EntryPtr>         m_owned;
    class QTimer             *m_reconnectTimer = nullptr;
    PortFinder                m_finder;
    SerialPortScanner        *m_scanner = nullptr;
    // Profiles waiting for a scan to find their adapters.
    QVector<QPair<int, SerialProfile>> m_pendingProfiles;
    QHash<int, Batch>         m_batches;
    int                       m_nextBatch = 1;
    quint64                   m_verifyRound = 0;
};

#endif // SERIALMANAGER_H
