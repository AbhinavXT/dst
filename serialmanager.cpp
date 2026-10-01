#include "serialmanager.h"

#include "capturedecoder.h"
#include "messagedispatcher.h"

#include <QDateTime>
#include <QFileInfo>
#include <QSerialPortInfo>
#include <QSettings>
#include <QTimer>

#include <algorithm>

SerialManager::SerialManager(MessageDispatcher *dispatcher, QObject *parent)
    : QObject(parent)
    , m_dispatcher(dispatcher)
{
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setInterval(1000);
    connect(m_reconnectTimer, &QTimer::timeout, this, &SerialManager::tryReconnect);
    m_finder = &SerialManager::findPortBySerial;
}

SerialManager::~SerialManager()
{
    // Close explicitly: each link flushes the partial line it holds into
    // the console before the dispatcher goes.
    closeAll();
}

SerialManager::EntryPtr SerialManager::entry(const QString &portName) const
{
    return m_ports.value(key(portName));
}

QVector<SerialManager::EntryPtr> SerialManager::entries() const
{
    // Unique: a port renamed on reconnect is one entry under two names.
    QVector<EntryPtr> out;
    for (const EntryPtr &e : m_ports) if (!out.contains(e)) out << e;
    return out;
}

SerialLink *SerialManager::link(const QString &portName) const
{
    const EntryPtr e = entry(portName);
    return e ? e->link : nullptr;
}

SerialLink *SerialManager::linkFor(const QString &portName)
{
    if (const EntryPtr found = entry(portName)) return found->link;

    EntryPtr e = EntryPtr::create();
    e->tabPort = portName.trimmed();
    e->link = new SerialLink(this);
    e->link->setIdleFlushMs(300);
    Entry *raw = e.data();
    connect(e->link, &SerialLink::lineReceived, this,
            [this, raw](const QByteArray &line, qint64 ms) { onLine(raw, line, ms); });
    connect(e->link, &SerialLink::opened, this, [this, raw]() {
        // Health is per session; a reconnect continues the session, so its
        // gap shows in "longest gap" rather than being wiped.
        if (!raw->reconnecting) raw->health.reset();
        // Remember which adapter this is: Windows renumbers COM ports, the
        // USB serial number stays.
        const QString sn = usbSerialFor(raw->link->config().portName);
        if (!sn.isEmpty()) raw->usbSerial = sn;
        emit portsChanged();
    });
    connect(e->link, &SerialLink::closed, this, &SerialManager::portsChanged);
    connect(e->link, &SerialLink::lost, this, [this, raw](const QString &why) { onLost(raw, why); });
    m_ports.insert(key(portName), e);
    return e->link;
}

bool SerialManager::open(const SerialConfig &config)
{
    SerialLink *l = linkFor(config.portName);
    if (const EntryPtr e = entry(config.portName)) e->lostAtMs = 0;   // an explicit open ends any wait
    updateReconnectTimer();
    return l->open(config);
}

bool SerialManager::openProfile(const SerialProfile &profile)
{
    setFeed(profile.config.portName, profile.feed);
    setLabel(profile.config.portName, profile.name);
    return open(profile.config);
}

QStringList SerialManager::openProfiles(const QVector<SerialProfile> &profiles)
{
    QStringList failed;
    for (const SerialProfile &p : profiles) {
        SerialLink *l = link(p.config.portName);
        if (l && l->isOpen()) {
            // Already running: a second "Open all" must not reopen (and so
            // briefly drop) a port that is capturing.
            setLabel(p.config.portName, p.name);
            continue;
        }
        if (!openProfile(p)) {
            failed << tr("%1 (%2): %3").arg(p.name, p.config.portName,
                                            linkFor(p.config.portName)->errorText());
        }
    }
    return failed;
}

void SerialManager::setLabel(const QString &portName, const QString &label)
{
    linkFor(portName);
    entry(portName)->label = label.trimmed();
    emit portsChanged();
}

QString SerialManager::label(const QString &portName) const
{
    const EntryPtr e = entry(portName);
    return e ? e->label : QString();
}

QString SerialManager::titleFor(const QString &portName) const
{
    const EntryPtr e = entry(portName);
    if (e && !e->label.isEmpty()) return e->label;
    return tabTitleFor(e ? e->tabPort : portName);
}

void SerialManager::close(const QString &portName)
{
    const EntryPtr e = entry(portName);
    if (!e) return;
    // A Close stops a wait for the adapter too: closing is the operator
    // saying "not this port", whatever state it is in.
    const bool waiting = e->lostAtMs > 0;
    e->lostAtMs = 0;
    updateReconnectTimer();
    e->link->close();
    if (waiting) emit portsChanged();
}

void SerialManager::closeAll()
{
    for (const EntryPtr &e : entries()) {
        e->lostAtMs = 0;
        e->link->close();
    }
    updateReconnectTimer();
}

QStringList SerialManager::openPorts() const
{
    QStringList out;
    for (const EntryPtr &e : entries()) {
        if (e->link->isOpen()) out << e->link->config().portName;
    }
    std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return out;
}

QStringList SerialManager::reconnectingPorts() const
{
    QStringList out;
    for (const EntryPtr &e : entries()) {
        if (e->lostAtMs > 0) out << e->link->config().portName;
    }
    std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return out;
}

bool SerialManager::isReconnecting(const QString &portName) const
{
    const EntryPtr e = entry(portName);
    return e && e->lostAtMs > 0;
}

void SerialManager::setFeed(const QString &portName, bool on)
{
    linkFor(portName);
    entry(portName)->feed = on;
}

bool SerialManager::feeds(const QString &portName) const
{
    const EntryPtr e = entry(portName);
    return e && e->feed;
}

int SerialManager::fedLines(const QString &portName) const
{
    const EntryPtr e = entry(portName);
    return e ? e->fedLines : 0;
}

void SerialManager::resetFedLines(const QString &portName)
{
    if (const EntryPtr e = entry(portName)) {
        e->fedLines = 0;
        e->health.reset();
    }
}

SerialLineHealth::Stats SerialManager::health(const QString &portName, qint64 nowMs) const
{
    const EntryPtr e = entry(portName);
    return e ? e->health.stats(nowMs) : SerialLineHealth::Stats();
}

QString SerialManager::healthText(const QString &portName, qint64 nowMs) const
{
    const SerialLineHealth::Stats h = health(portName, nowMs);
    const SerialLink *l = link(portName);
    const quint64 errs = l ? l->errorCount() : 0;
    QString t = tr("%1 lines/s").arg(h.perSecond, 0, 'f', 1);
    t += h.lines ? tr(" · %1% decode").arg(h.decodePercent()) : tr(" · no lines yet");
    if (h.failing()) t += tr(" (check baud and line settings)");
    if (h.longestGapMs > 0) t += tr(" · longest gap %1 s").arg(h.longestGapMs / 1000.0, 0, 'f', 1);
    t += errs == 1 ? tr(" · 1 driver error") : tr(" · %1 driver errors").arg(errs);
    return t;
}

void SerialManager::onLine(Entry *e, const QByteArray &line, qint64 ms)
{
    if (line.trimmed().isEmpty()) return;
    // Health counts every line, fed or not: it is about the wire.
    e->health.note(SerialLineHealth::lineDecodes(line), ms);
    if (!e->feed || !m_dispatcher) return;
    // Into the tab of the name the port FIRST had: a port renamed on
    // reconnect (COM5 -> COM7) carries on in the same tab. Titled by the
    // profile's name when it has one.
    m_dispatcher->ingestLocal(kSerialSourceId, kvchForPort(e->tabPort), line, ms,
                              e->label.isEmpty() ? tabTitleFor(e->tabPort) : e->label);
    ++e->fedLines;
}

// ---- auto-reconnect -------------------------------------------------------------------

void SerialManager::marker(Entry *e, const QString &text, qint64 ms)
{
    // A line in the console tab, so the gap in the log is visible where
    // the log is read, not only in a terminal that may not be open.
    if (e->feed && m_dispatcher) {
        m_dispatcher->ingestLocal(kSerialSourceId, kvchForPort(e->tabPort), text.toUtf8(), ms,
                                  e->label.isEmpty() ? tabTitleFor(e->tabPort) : e->label);
    }
}

void SerialManager::onLost(Entry *e, const QString &why)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    e->lostAtMs = now;
    const QString port = e->link->config().portName;
    marker(e, tr("── lost %1: %2; waiting for it to come back ──").arg(port, why), now);
    emit portLost(port, why);
    emit portsChanged();
    updateReconnectTimer();
}

void SerialManager::tryReconnect()
{
    for (const EntryPtr &e : entries()) {
        if (e->lostAtMs <= 0) continue;
        SerialConfig c = e->link->config();
        const QString oldName = c.portName;
        const QString found = m_finder(e->usbSerial, oldName);
        if (found.isEmpty()) continue;                    // not back yet
        c.portName = found;
        e->reconnecting = true;
        const bool ok = e->link->open(c);
        e->reconnecting = false;
        if (!ok) continue;                                // enumerated, not ready: next tick
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const qint64 gap = now - e->lostAtMs;
        e->lostAtMs = 0;
        if (key(found) != key(oldName)) m_ports.insert(key(found), e);   // same entry, new name
        marker(e.data(), key(found) == key(oldName)
                   ? tr("── reconnected %1, gap %2 s ──").arg(found).arg(gap / 1000.0, 0, 'f', 1)
                   : tr("── reconnected as %1 (was %2), gap %3 s ──")
                         .arg(found, oldName).arg(gap / 1000.0, 0, 'f', 1),
               now);
        emit portReconnected(found, gap);
        emit portsChanged();
    }
    updateReconnectTimer();
}

void SerialManager::updateReconnectTimer()
{
    bool waiting = false;
    for (const EntryPtr &e : m_ports) if (e->lostAtMs > 0) { waiting = true; break; }
    if (waiting && !m_reconnectTimer->isActive()) m_reconnectTimer->start();
    if (!waiting) m_reconnectTimer->stop();
}

void SerialManager::setReconnectIntervalMs(int ms) { m_reconnectTimer->setInterval(ms); }

QString SerialManager::usbSerialFor(const QString &portName)
{
    const QString shortN = shortName(portName);
    for (const QSerialPortInfo &i : QSerialPortInfo::availablePorts()) {
        if (i.portName().compare(shortN, Qt::CaseInsensitive) == 0
            || i.systemLocation() == portName) {
            return i.serialNumber();
        }
    }
    return QString();
}

QString SerialManager::findPortBySerial(const QString &usbSerial, const QString &lastName)
{
    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    if (!usbSerial.isEmpty()) {
        // By the adapter, under whatever name it now has.
        for (const QSerialPortInfo &i : ports)
            if (i.serialNumber() == usbSerial) return i.portName();
        return QString();
    }
    // No serial number (a virtual port, a built-in UART): by name.
    for (const QSerialPortInfo &i : ports)
        if (i.portName().compare(shortName(lastName), Qt::CaseInsensitive) == 0
            || i.systemLocation() == lastName) {
            return lastName;
        }
#ifdef Q_OS_UNIX
    // Not enumerated (a pty, a path the enumerator does not list) but there.
    if (lastName.startsWith(QLatin1Char('/')) && QFileInfo::exists(lastName)) return lastName;
#endif
    return QString();
}

// ---- naming ---------------------------------------------------------------------------

quint16 SerialManager::kvchForPort(const QString &portName)
{
    // Stable across runs and machines for the same name; never 0.
    quint32 h = 2166136261u;                         // FNV-1a
    for (const QChar c : portName.toUpper()) { h ^= c.unicode(); h *= 16777619u; }
    return quint16(1 + (h % 65000u));
}

QString SerialManager::tabKeyFor(const QString &portName)
{
    return QStringLiteral("%1_%2").arg(int(kSerialSourceId)).arg(int(kvchForPort(portName)));
}

QString SerialManager::shortName(const QString &portName)
{
    return portName.section(QLatin1Char('/'), -1);
}

QString SerialManager::tabTitleFor(const QString &portName)
{
    return tr("Serial %1").arg(shortName(portName));
}

// ---- per-port settings ----------------------------------------------------------------

QString SerialManager::settingsGroup(const QString &portName)
{
    // A slash would nest QSettings groups: /dev/ttyUSB0 -> _dev_ttyUSB0.
    QString n = key(portName);
    n.replace(QLatin1Char('/'), QLatin1Char('_')).replace(QLatin1Char('\\'), QLatin1Char('_'));
    return QStringLiteral("serial/ports/") + n;
}

void SerialManager::savePortSettings(QSettings &s, const SerialConfig &config, bool feed)
{
    if (config.portName.trimmed().isEmpty()) return;
    const QString g = settingsGroup(config.portName);
    config.save(s, g);
    s.setValue(g + QStringLiteral("/feed"), feed);
    s.setValue(QStringLiteral("serial/lastPort"), config.portName);
}

bool SerialManager::loadPortSettings(QSettings &s, const QString &portName,
                                     SerialConfig *config, bool *feed)
{
    const QString g = settingsGroup(portName);
    if (!s.contains(g + QStringLiteral("/feed"))) return false;
    *config = SerialConfig::load(s, g);
    config->portName = portName;
    *feed = s.value(g + QStringLiteral("/feed"), true).toBool();
    return true;
}

// ---- line health ----------------------------------------------------------------------

bool SerialLineHealth::lineDecodes(const QByteArray &line)
{
    if (!line.startsWith('@')) return false;      // cheap reject: text, garbage
    const CaptureLine c = CaptureDecoder::parseLine(QString::fromLatin1(line));
    return c.valid && c.type != CapType::Unknown;
}

void SerialLineHealth::note(bool decodes, qint64 ms)
{
    ++m_lines;
    if (decodes) ++m_decoded;
    if (m_lastMs >= 0) m_longestGapMs = qMax(m_longestGapMs, ms - m_lastMs);
    m_lastMs = ms;
    m_recent.append(ms);
    while (!m_recent.isEmpty() && ms - m_recent.first() > kRateWindowMs) m_recent.removeFirst();
}

SerialLineHealth::Stats SerialLineHealth::stats(qint64 nowMs) const
{
    Stats s;
    s.lines = m_lines;
    s.decoded = m_decoded;
    s.longestGapMs = m_longestGapMs;
    s.sinceLastMs = m_lastMs < 0 ? -1 : nowMs - m_lastMs;
    int inWindow = 0;
    for (qint64 t : m_recent) if (nowMs - t <= kRateWindowMs) ++inWindow;
    s.perSecond = inWindow * 1000.0 / kRateWindowMs;
    return s;
}

// ---- profiles -------------------------------------------------------------------------

QVector<SerialProfile> SerialProfile::loadAll(QSettings &s)
{
    QVector<SerialProfile> out;
    const int n = s.beginReadArray(QStringLiteral("serial/profiles"));
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        SerialProfile p;
        p.name = s.value(QStringLiteral("name")).toString().trimmed();
        p.config = SerialConfig::load(s, QStringLiteral("config"));
        p.feed = s.value(QStringLiteral("feed"), true).toBool();
        p.autoOpen = s.value(QStringLiteral("autoOpen"), false).toBool();
        if (!p.name.isEmpty() && !p.config.portName.isEmpty()) out << p;
    }
    s.endArray();
    return out;
}

void SerialProfile::saveAll(QSettings &s, const QVector<SerialProfile> &profiles)
{
    s.remove(QStringLiteral("serial/profiles"));
    s.beginWriteArray(QStringLiteral("serial/profiles"), profiles.size());
    for (int i = 0; i < profiles.size(); ++i) {
        s.setArrayIndex(i);
        const SerialProfile &p = profiles.at(i);
        s.setValue(QStringLiteral("name"), p.name);
        p.config.save(s, QStringLiteral("config"));
        s.setValue(QStringLiteral("feed"), p.feed);
        s.setValue(QStringLiteral("autoOpen"), p.autoOpen);
    }
    s.endArray();
}

int SerialProfile::indexOf(const QVector<SerialProfile> &profiles, const QString &name)
{
    for (int i = 0; i < profiles.size(); ++i)
        if (profiles.at(i).name.compare(name.trimmed(), Qt::CaseInsensitive) == 0) return i;
    return -1;
}

void SerialProfile::upsert(QVector<SerialProfile> *profiles, const SerialProfile &p)
{
    const int i = indexOf(*profiles, p.name);
    if (i >= 0) (*profiles)[i] = p;
    else profiles->append(p);
}

bool SerialProfile::remove(QVector<SerialProfile> *profiles, const QString &name)
{
    const int i = indexOf(*profiles, name);
    if (i < 0) return false;
    profiles->remove(i);
    return true;
}
