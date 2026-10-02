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
    using PortFinder = std::function<QString(const QString &usbSerial, const QString &lastName)>;
    void setPortFinder(PortFinder f) { m_finder = std::move(f); }
    static QString usbSerialFor(const QString &portName);
    static QString findPortBySerial(const QString &usbSerial, const QString &lastName);

    // Opens a profile's port with its settings and feed, labelled with the
    // profile's name. A profile that knows its adapter opens it wherever it
    // now is (session 115). False with the reason in the link's errorText().
    bool openProfile(const SerialProfile &profile);
    // Where a profile's port is now: its adapter's current name when that
    // can be found, else the name it was saved with.
    QString resolveProfilePort(const SerialProfile &profile) const;
    // Opens each; returns "name: why" for each that failed (empty = all open).
    QStringList openProfiles(const QVector<SerialProfile> &profiles);

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
        bool    reconnecting = false;
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

    MessageDispatcher        *m_dispatcher = nullptr;
    // Keyed by port name; a port renamed on reconnect is one entry under
    // both names.
    QHash<QString, EntryPtr>  m_ports;
    class QTimer             *m_reconnectTimer = nullptr;
    PortFinder                m_finder;
};

#endif // SERIALMANAGER_H
