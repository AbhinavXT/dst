#include "serialportscan.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSerialPortInfo>
#include <QSet>
#include <QThread>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <setupapi.h>
#  include <cfgmgr32.h>
// MinGW (Qt 5.15's 8.1) defaults _WIN32_WINNT to 0x0502 (XP), and its
// cfgmgr32.h then leaves out this Vista-era property. The value is fixed:
// SPDRP_LOCATION_PATHS (0x23) + 1, as for every CM_DRP_*.
#  ifndef CM_DRP_LOCATION_PATHS
#    define CM_DRP_LOCATION_PATHS 0x00000024
#  endif
#endif

// =============================================================================
//  Small text helpers
// =============================================================================

QString SerialAdapterId::describe() const
{
    if (!serial.isEmpty()) return QObject::tr("serial number %1").arg(serial);
    if (!location.isEmpty()) return QObject::tr("USB socket %1").arg(location);
    return QObject::tr("name %1").arg(lastName);
}

QString serialMatchByText(SerialMatchBy by)
{
    switch (by) {
    case SerialMatchBy::Serial:   return QObject::tr("by its USB serial number");
    case SerialMatchBy::Location: return QObject::tr("by the USB socket it is plugged into");
    case SerialMatchBy::Model:    return QObject::tr("as the only free adapter of its model");
    case SerialMatchBy::Name:     return QObject::tr("by its port name");
    case SerialMatchBy::None:     break;
    }
    return QString();
}

namespace {

QString shortOf(const QString &portName)
{
    return portName.section(QLatin1Char('/'), -1).section(QLatin1Char('\\'), -1);
}

bool sameName(const SerialPortSnapshot &p, const QString &portName)
{
    return p.name.compare(shortOf(portName), Qt::CaseInsensitive) == 0
        || p.systemLocation.compare(portName, Qt::CaseInsensitive) == 0;
}

bool isTaken(const SerialPortSnapshot &p, const QStringList &taken)
{
    for (const QString &t : taken)
        if (sameName(p, t)) return true;
    return false;
}

bool sameModel(const SerialAdapterId &id, const SerialPortSnapshot &p)
{
    // Unknown on either side is no evidence against.
    if (!id.hasVidPid() || !p.hasVidPid()) return true;
    return id.vid == p.vid && id.pid == p.pid;
}

int countLocation(const SerialPortList &ports, const QString &location)
{
    int n = 0;
    for (const SerialPortSnapshot &p : ports)
        if (!location.isEmpty() && p.location == location) ++n;
    return n;
}

}  // namespace

// =============================================================================
//  Platform: where each port is plugged in
// =============================================================================

QHash<QString, QString> SerialScan::locationsFromByPath(const QString &byPathDir)
{
    // udev keeps /dev/serial/by-path/<bus path> -> ../../ttyUSB0. The link's
    // NAME is the socket; what it points at is today's device name.
    QHash<QString, QString> out;
    const QDir dir(byPathDir);
    if (!dir.exists()) return out;
    const QFileInfoList links = dir.entryInfoList(QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : links) {
        const QString target = fi.canonicalFilePath();
        if (target.isEmpty()) continue;                    // dangling
        out.insert(QFileInfo(target).fileName(), fi.fileName());
    }
    return out;
}

#ifdef Q_OS_WIN
namespace {

// The physical location of a device node: its location path, else that of
// the nearest parent with one (an FTDI port is a child of the USB device
// that has the path). A path is per USB socket, so it survives replugging.
QString windowsNodeLocation(DEVINST inst)
{
    DEVINST node = inst;
    for (int level = 0; level < 5; ++level) {
        wchar_t buf[1024] = {};
        ULONG size = sizeof(buf);
        ULONG type = 0;
        if (CM_Get_DevNode_Registry_PropertyW(node, CM_DRP_LOCATION_PATHS, &type, buf, &size, 0) == CR_SUCCESS
            && buf[0] != L'\0') {
            return QString::fromWCharArray(buf);           // the first of the multi-string
        }
        DEVINST parent = 0;
        if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS) break;
        node = parent;
    }
    // No path anywhere up the tree: the location text of the node itself
    // ("Port_#0002.Hub_#0001"). Less certain; a duplicate is dropped later.
    wchar_t buf[512] = {};
    ULONG size = sizeof(buf);
    ULONG type = 0;
    if (CM_Get_DevNode_Registry_PropertyW(inst, CM_DRP_LOCATION_INFORMATION, &type, buf, &size, 0) == CR_SUCCESS)
        return QString::fromWCharArray(buf);
    return QString();
}

QString windowsPortName(HDEVINFO set, SP_DEVINFO_DATA *info)
{
    HKEY key = SetupDiOpenDevRegKey(set, info, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
    if (key == INVALID_HANDLE_VALUE) return QString();
    wchar_t buf[256] = {};
    DWORD type = 0;
    DWORD size = sizeof(buf) - sizeof(wchar_t);
    QString out;
    if (RegQueryValueExW(key, L"PortName", nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS
        && type == REG_SZ) {
        out = QString::fromWCharArray(buf);
    }
    RegCloseKey(key);
    return out;
}

void windowsLocationsOfClass(const GUID &cls, QHash<QString, QString> *out)
{
    HDEVINFO set = SetupDiGetClassDevsW(&cls, nullptr, nullptr, DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return;
    SP_DEVINFO_DATA info;
    info.cbSize = sizeof(info);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &info); ++i) {
        const QString name = windowsPortName(set, &info);
        if (name.isEmpty()) continue;
        const QString loc = windowsNodeLocation(info.DevInst);
        if (!loc.isEmpty()) out->insert(name.toUpper(), loc);
    }
    SetupDiDestroyDeviceInfoList(set);
}

}  // namespace
#endif

QHash<QString, QString> SerialScan::platformLocations()
{
#if defined(Q_OS_LINUX)
    return locationsFromByPath(QStringLiteral("/dev/serial/by-path"));
#elif defined(Q_OS_WIN)
    // Ports (COM & LPT) and Modem: USB CDC devices land in either.
    static const GUID kPorts = { 0x4D36E978, 0xE325, 0x11CE, { 0xBF, 0xC1, 0x08, 0x00, 0x2B, 0xE1, 0x03, 0x18 } };
    static const GUID kModem = { 0x4D36E96D, 0xE325, 0x11CE, { 0xBF, 0xC1, 0x08, 0x00, 0x2B, 0xE1, 0x03, 0x18 } };
    QHash<QString, QString> out;
    windowsLocationsOfClass(kPorts, &out);
    windowsLocationsOfClass(kModem, &out);
    return out;
#else
    return QHash<QString, QString>();     // macOS: matched by serial number, then by name
#endif
}

SerialPortList SerialScan::scan()
{
    const QHash<QString, QString> locs = platformLocations();
    SerialPortList out;
    for (const QSerialPortInfo &i : QSerialPortInfo::availablePorts()) {
        SerialPortSnapshot s;
        s.name = i.portName();
        s.systemLocation = i.systemLocation();
        s.serial = i.serialNumber().trimmed();
        s.description = i.description();
        if (i.hasVendorIdentifier()) s.vid = i.vendorIdentifier();
        if (i.hasProductIdentifier()) s.pid = i.productIdentifier();
#ifdef Q_OS_WIN
        s.location = locs.value(s.name.toUpper());
#else
        s.location = locs.value(s.name);
#endif
        out << s;
    }
    return out;
}

// =============================================================================
//  Identity and matching
// =============================================================================

bool SerialScan::serialIsUnique(const SerialPortList &ports, const QString &serial, const QString &exceptName)
{
    if (serial.isEmpty()) return false;
    for (const SerialPortSnapshot &p : ports) {
        if (!exceptName.isEmpty() && sameName(p, exceptName)) continue;
        if (p.serial == serial) return false;
    }
    return true;
}

const SerialPortSnapshot *SerialScan::find(const SerialPortList &ports, const QString &portName)
{
    for (const SerialPortSnapshot &p : ports)
        if (sameName(p, portName)) return &p;
    return nullptr;
}

SerialAdapterId SerialScan::identify(const QString &portName, const SerialPortList &ports)
{
    SerialAdapterId id;
    id.lastName = portName;
    const SerialPortSnapshot *s = find(ports, portName);
    if (!s) return id;
    // A serial number another port also has (clone FTDI chips) identifies
    // nothing: kept, it would hand this port the other adapter.
    if (serialIsUnique(ports, s->serial, s->name)) id.serial = s->serial;
    if (countLocation(ports, s->location) == 1) id.location = s->location;
    id.vid = s->vid;
    id.pid = s->pid;
    return id;
}

QVector<SerialReconnectPlan> SerialScan::planReconnects(const QVector<SerialAdapterId> &lost,
                                                        const SerialPortList &ports,
                                                        const QStringList &taken)
{
    QVector<SerialReconnectPlan> plans;
    QVector<bool> planned(lost.size(), false);
    QSet<int> claimed;                                   // indexes into `ports`

    auto claim = [&](int lostIdx, int portIdx, SerialMatchBy by) {
        planned[lostIdx] = true;
        claimed.insert(portIdx);
        SerialReconnectPlan p;
        p.index = lostIdx;
        p.portName = ports.at(portIdx).name;
        p.by = by;
        plans << p;
    };
    auto freePort = [&](int i) { return !claimed.contains(i) && !isTaken(ports.at(i), taken); };

    // 1. By USB serial number, where it identifies one adapter.
    for (int li = 0; li < lost.size(); ++li) {
        const SerialAdapterId &id = lost.at(li);
        if (id.serial.isEmpty()) continue;
        QVector<int> cands;
        for (int pi = 0; pi < ports.size(); ++pi)
            if (ports.at(pi).serial == id.serial) cands << pi;
        if (cands.size() > 1 && !id.location.isEmpty()) {
            // The serial is shared now (a clone came in): the socket decides.
            QVector<int> at;
            for (int pi : cands) if (ports.at(pi).location == id.location) at << pi;
            cands = at;
        }
        if (cands.size() == 1 && freePort(cands.first())) claim(li, cands.first(), SerialMatchBy::Serial);
        // Present under another adapter's serial, or not back: keep waiting.
    }

    // 2. By the socket, for adapters with no serial number of their own.
    for (int li = 0; li < lost.size(); ++li) {
        const SerialAdapterId &id = lost.at(li);
        if (planned[li] || !id.serial.isEmpty() || id.location.isEmpty()) continue;
        if (countLocation(ports, id.location) != 1) continue;
        for (int pi = 0; pi < ports.size(); ++pi) {
            const SerialPortSnapshot &p = ports.at(pi);
            if (p.location != id.location) continue;
            // Another model in that socket, or an adapter that has a serial
            // number of its own, is a different adapter.
            if (freePort(pi) && sameModel(id, p) && !serialIsUnique(ports, p.serial))
                claim(li, pi, SerialMatchBy::Location);
            break;
        }
    }

    // 3. Moved to another socket: the only free adapter of its model, when
    //    no other lost port of that model could claim it too.
    for (int li = 0; li < lost.size(); ++li) {
        const SerialAdapterId &id = lost.at(li);
        if (planned[li] || !id.serial.isEmpty() || !id.hasVidPid()) continue;
        int rivals = 0;
        for (int lj = 0; lj < lost.size(); ++lj) {
            if (planned[lj]) continue;
            const SerialAdapterId &o = lost.at(lj);
            if (o.serial.isEmpty() && o.vid == id.vid && o.pid == id.pid) ++rivals;
        }
        if (rivals != 1) continue;
        int found = -1, count = 0;
        for (int pi = 0; pi < ports.size(); ++pi) {
            const SerialPortSnapshot &p = ports.at(pi);
            if (!freePort(pi) || p.vid != id.vid || p.pid != id.pid) continue;
            if (serialIsUnique(ports, p.serial)) continue;   // an identifiable, other adapter
            // A socket another waiting port is known by belongs to that port.
            bool othersSocket = false;
            for (int lj = 0; lj < lost.size(); ++lj)
                if (lj != li && !lost.at(lj).location.isEmpty() && lost.at(lj).location == p.location)
                    othersSocket = true;
            if (othersSocket) continue;
            found = pi;
            ++count;
        }
        if (count == 1) claim(li, found, SerialMatchBy::Model);
    }

    // 4. By name, only for ports known by nothing else (built-in UARTs,
    //    virtual ports, platforms with no location): the old behaviour.
    for (int li = 0; li < lost.size(); ++li) {
        const SerialAdapterId &id = lost.at(li);
        if (planned[li] || !id.serial.isEmpty() || !id.location.isEmpty()) continue;
        bool done = false;
        for (int pi = 0; pi < ports.size() && !done; ++pi) {
            const SerialPortSnapshot &p = ports.at(pi);
            if (!sameName(p, id.lastName)) continue;
            done = true;
            // Known by a model: not an adapter with an identity of its own
            // that this one lacked. Known by nothing: the name is all there is.
            if (freePort(pi) && sameModel(id, p) && (id.isEmpty() || !serialIsUnique(ports, p.serial)))
                claim(li, pi, SerialMatchBy::Name);
        }
        if (planned[li] || done) continue;
#ifdef Q_OS_UNIX
        // Not enumerated (a pty, a path the enumerator does not list) but there.
        if (id.lastName.startsWith(QLatin1Char('/')) && QFileInfo::exists(id.lastName)
            && !taken.contains(id.lastName, Qt::CaseSensitive)) {
            planned[li] = true;
            SerialReconnectPlan p;
            p.index = li;
            p.portName = id.lastName;
            p.by = SerialMatchBy::Name;
            plans << p;
        }
#endif
    }

    // Each plan keeps the full path the port was opened by when the name
    // did not change (so "/dev/ttyUSB0" stays "/dev/ttyUSB0", not "ttyUSB0").
    for (SerialReconnectPlan &p : plans) {
        const SerialAdapterId &id = lost.at(p.index);
        if (shortOf(id.lastName).compare(p.portName, Qt::CaseInsensitive) == 0) {
            p.portName = id.lastName;
        } else {
            for (const SerialPortSnapshot &s : ports)
                if (s.name == p.portName && id.lastName.startsWith(QLatin1Char('/')))
                    p.portName = s.systemLocation;
        }
    }
    return plans;
}

// =============================================================================
//  SerialPortScanner
// =============================================================================

SerialPortScanner::SerialPortScanner(QObject *parent) : QObject(parent)
{
    static const int registered = [] {
        qRegisterMetaType<SerialPortList>("SerialPortList");
        return 0;
    }();
    Q_UNUSED(registered);
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("serial port scan"));
    m_worker = new QObject;
    m_worker->moveToThread(m_thread);
    m_thread->start();
}

SerialPortScanner::~SerialPortScanner()
{
    m_thread->quit();
    m_thread->wait();
    delete m_worker;
}

void SerialPortScanner::setScanFunction(ScanFn fn)
{
    QMutexLocker l(&m_fnMutex);
    m_fn = std::move(fn);
}

void SerialPortScanner::request()
{
    if (m_busy.load()) { m_again = true; return; }
    m_busy = true;
    QMetaObject::invokeMethod(m_worker, [this]() { runScan(); }, Qt::QueuedConnection);
}

void SerialPortScanner::runScan()
{
    ScanFn fn;
    {
        QMutexLocker l(&m_fnMutex);
        fn = m_fn;
    }
    const qint64 started = QDateTime::currentMSecsSinceEpoch();
    const SerialPortList ports = fn ? fn() : SerialScan::scan();
    // Back to the scanner's own thread. Dropped if the scanner is gone: its
    // destructor waits for this thread first, then Qt discards the event.
    QMetaObject::invokeMethod(this, [this, ports, started]() { onScanned(ports, started); }, Qt::QueuedConnection);
}

void SerialPortScanner::onScanned(const SerialPortList &ports, qint64 startedAtMs)
{
    m_lastStartedAtMs = startedAtMs;
    {
        QMutexLocker l(&m_lastMutex);
        m_last = ports;
    }
    m_lastAtMs = QDateTime::currentMSecsSinceEpoch();
    m_busy = false;
    ++m_done;
    emit scanned(ports);
    if (m_again) {
        m_again = false;
        request();
    }
}
