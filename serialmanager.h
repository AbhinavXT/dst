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
#include <QString>
#include <QStringList>
#include <QVector>

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
    static void savePortSettings(QSettings &s, const SerialConfig &config, bool feed);
    // False when nothing was saved for that port yet.
    static bool loadPortSettings(QSettings &s, const QString &portName,
                                 SerialConfig *config, bool *feed);

signals:
    void portsChanged();                     // a port opened or closed

private:
    struct Entry {
        SerialLink *link = nullptr;
        bool feed = true;
        int  fedLines = 0;
        SerialLineHealth health;
    };
    static QString key(const QString &portName) { return portName.trimmed().toUpper(); }
    void onLine(const QString &key, const QByteArray &line, qint64 ms);

    MessageDispatcher     *m_dispatcher = nullptr;
    QHash<QString, Entry>  m_ports;
};

#endif // SERIALMANAGER_H
