#ifndef SERIALPORTSCAN_H
#define SERIALPORTSCAN_H

// =============================================================================
//  Serial port scanning and adapter matching (session 156)
//  -----------------------------------------------------------------------------
//  WHICH ADAPTER IS WHICH
//    Auto-reconnect (session 106) found a lost adapter again by its USB
//    serial number, and fell back to the port NAME when there was none. Cheap
//    CH340 / PL2303 adapters have none, and clone FTDI chips often all share
//    one. Replug two of those in the other order and Linux swaps ttyUSB0 and
//    ttyUSB1: the "IOA Input" tab carried on — with the Output card's lines.
//
//    So an adapter is now known by up to three things, strongest first:
//      1. its USB serial number, but only when no other port present has the
//         same one (a shared clone serial identifies nothing);
//      2. its physical USB location — the socket it is plugged into
//         (/dev/serial/by-path on Linux, the device's location path on
//         Windows) — which stays put whatever order things are plugged in;
//      3. its name, for ports with neither (a built-in UART, a virtual
//         port, macOS), and then only on an adapter of the same model.
//    The match is planned for all lost ports at once, so two lost ports can
//    never be given the same adapter, and a guess that two ports could both
//    claim is not made.
//
//  OFF THE GUI THREAD
//    Enumerating ports takes 50-500 ms on Windows. Reconnect used to do it
//    on the GUI thread, once per lost port, every second, for as long as
//    the adapters stayed out. SerialPortScanner does it on a thread of its
//    own, one scan per request, and coalesces requests that arrive while a
//    scan is running.
// =============================================================================

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <functional>

class QThread;

// One port as the enumerator saw it.
struct SerialPortSnapshot {
    QString name;               // "COM5", "ttyUSB0"
    QString systemLocation;     // "\\.\COM5", "/dev/ttyUSB0"
    QString serial;             // USB serial number, empty if none
    QString location;           // physical USB socket, empty if unknown
    QString description;
    quint16 vid = 0, pid = 0;   // 0/0: unknown
    bool hasVidPid() const { return vid != 0 || pid != 0; }
};
using SerialPortList = QVector<SerialPortSnapshot>;

// What a port's adapter was known by when it was last open.
struct SerialAdapterId {
    QString lastName;           // the name it had ("COM5", "/dev/ttyUSB0")
    QString serial;             // only kept when it was unique at the time
    QString location;
    quint16 vid = 0, pid = 0;
    bool hasVidPid() const { return vid != 0 || pid != 0; }
    bool isEmpty() const { return serial.isEmpty() && location.isEmpty() && !hasVidPid(); }
    // "serial A50285BI", "USB socket pci-0000:00:14.0-usb-0:2:1.0", "name COM5"
    QString describe() const;
};

// How a lost port was matched to an adapter.
enum class SerialMatchBy { None, Serial, Location, Model, Name };
QString serialMatchByText(SerialMatchBy by);

struct SerialReconnectPlan {
    int           index = -1;      // into the `lost` list given
    QString       portName;        // where to reopen it
    SerialMatchBy by = SerialMatchBy::None;
};

namespace SerialScan {

// Every port present, with serial numbers and physical locations. Slow on
// Windows: call it off the GUI thread (SerialPortScanner does).
SerialPortList scan();

// Physical locations by device: "ttyUSB0" -> "pci-0000:00:14.0-usb-0:2:1.0-port0".
// Linux reads the udev links in `byPathDir`; tests pass a directory of
// their own. Elsewhere empty except on Windows, where the system is asked.
QHash<QString, QString> locationsFromByPath(const QString &byPathDir);
QHash<QString, QString> platformLocations();

// True when no OTHER port in `ports` has this serial number.
bool serialIsUnique(const SerialPortList &ports, const QString &serial, const QString &exceptName = QString());

// The identity to remember for `portName`, from a scan taken while it was
// present. A serial number shared with another port is not kept.
SerialAdapterId identify(const QString &portName, const SerialPortList &ports);

// The snapshot for a name (short name or full path); null when absent.
const SerialPortSnapshot *find(const SerialPortList &ports, const QString &portName);

// Plans where each lost port should be reopened. `taken`: names already in
// use by ports that are open (never handed out). A port whose adapter cannot
// be found — or could equally be another lost port's — gets no plan and
// keeps waiting. On Unix a lost port known only by a path that still
// exists (a pty, a path the enumerator does not list) is matched by name.
QVector<SerialReconnectPlan> planReconnects(const QVector<SerialAdapterId> &lost,
                                            const SerialPortList &ports,
                                            const QStringList &taken = QStringList());

}  // namespace SerialScan

// Scans on a thread of its own. request() never blocks; the result comes
// back as scanned() on the thread that owns the scanner. Requests made
// while a scan runs are folded into one more scan after it.
class SerialPortScanner : public QObject
{
    Q_OBJECT
public:
    using ScanFn = std::function<SerialPortList()>;
    explicit SerialPortScanner(QObject *parent = nullptr);
    ~SerialPortScanner() override;

    void request();
    bool busy() const { return m_busy.load(); }
    int  scansDone() const { return m_done.load(); }
    // Tests: a stand-in for the enumerator. Called on the scan thread.
    void setScanFunction(ScanFn fn);
    // The most recent result, and when it was taken (0: never).
    SerialPortList last() const { QMutexLocker l(&m_lastMutex); return m_last; }
    qint64 lastAtMs() const { return m_lastAtMs.load(); }
    // When the scan just delivered STARTED. A port lost after that moment
    // may still be in its list: the list says nothing about it.
    qint64 lastStartedAtMs() const { return m_lastStartedAtMs.load(); }
    QThread *scanThread() const { return m_thread; }

signals:
    void scanned(const SerialPortList &ports);

private:
    void runScan();                     // on the scan thread
    void onScanned(const SerialPortList &ports, qint64 startedAtMs);

    QThread             *m_thread = nullptr;
    QObject             *m_worker = nullptr;   // lives on m_thread
    mutable QMutex       m_fnMutex;
    ScanFn               m_fn;
    std::atomic<bool>    m_busy{ false };
    bool                 m_again = false;      // GUI side only
    std::atomic<int>     m_done{ 0 };
    mutable QMutex       m_lastMutex;
    SerialPortList       m_last;
    std::atomic<qint64>  m_lastAtMs{ 0 };
    std::atomic<qint64>  m_lastStartedAtMs{ 0 };
};

Q_DECLARE_METATYPE(SerialPortList)

#endif // SERIALPORTSCAN_H
