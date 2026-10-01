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

#include "seriallink.h"

class MessageDispatcher;
class QSettings;

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
    void resetFedLines(const QString &portName);

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
    };
    static QString key(const QString &portName) { return portName.trimmed().toUpper(); }
    void onLine(const QString &key, const QByteArray &line, qint64 ms);

    MessageDispatcher     *m_dispatcher = nullptr;
    QHash<QString, Entry>  m_ports;
};

#endif // SERIALMANAGER_H
