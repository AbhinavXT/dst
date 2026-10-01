#include "serialmanager.h"

#include "capturedecoder.h"
#include "messagedispatcher.h"

#include <QSettings>

#include <algorithm>

SerialManager::SerialManager(MessageDispatcher *dispatcher, QObject *parent)
    : QObject(parent)
    , m_dispatcher(dispatcher)
{
}

SerialManager::~SerialManager()
{
    // Close explicitly: each link flushes the partial line it holds into
    // the console before the dispatcher goes.
    closeAll();
}

SerialLink *SerialManager::link(const QString &portName) const
{
    const auto it = m_ports.constFind(key(portName));
    return it == m_ports.constEnd() ? nullptr : it->link;
}

SerialLink *SerialManager::linkFor(const QString &portName)
{
    const QString k = key(portName);
    auto it = m_ports.find(k);
    if (it != m_ports.end()) return it->link;

    Entry e;
    e.link = new SerialLink(this);
    e.link->setIdleFlushMs(300);
    connect(e.link, &SerialLink::lineReceived, this,
            [this, k](const QByteArray &line, qint64 ms) { onLine(k, line, ms); });
    connect(e.link, &SerialLink::opened, this, [this, k]() {
        auto it = m_ports.find(k);
        if (it != m_ports.end()) it->health.reset();     // health is per session
        emit portsChanged();
    });
    connect(e.link, &SerialLink::closed, this, &SerialManager::portsChanged);
    m_ports.insert(k, e);
    return e.link;
}

bool SerialManager::open(const SerialConfig &config)
{
    return linkFor(config.portName)->open(config);
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
    m_ports[key(portName)].label = label.trimmed();
    emit portsChanged();
}

QString SerialManager::label(const QString &portName) const
{
    const auto it = m_ports.constFind(key(portName));
    return it == m_ports.constEnd() ? QString() : it->label;
}

QString SerialManager::titleFor(const QString &portName) const
{
    const QString l = label(portName);
    return l.isEmpty() ? tabTitleFor(portName) : l;
}

void SerialManager::close(const QString &portName)
{
    if (SerialLink *l = link(portName)) l->close();
}

void SerialManager::closeAll()
{
    for (const Entry &e : qAsConst(m_ports)) e.link->close();
}

QStringList SerialManager::openPorts() const
{
    QStringList out;
    for (const Entry &e : m_ports) {
        if (e.link->isOpen()) out << e.link->config().portName;
    }
    std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return out;
}

void SerialManager::setFeed(const QString &portName, bool on)
{
    linkFor(portName);
    m_ports[key(portName)].feed = on;
}

bool SerialManager::feeds(const QString &portName) const
{
    const auto it = m_ports.constFind(key(portName));
    return it != m_ports.constEnd() && it->feed;
}

int SerialManager::fedLines(const QString &portName) const
{
    const auto it = m_ports.constFind(key(portName));
    return it == m_ports.constEnd() ? 0 : it->fedLines;
}

void SerialManager::resetFedLines(const QString &portName)
{
    auto it = m_ports.find(key(portName));
    if (it != m_ports.end()) {
        it->fedLines = 0;
        it->health.reset();
    }
}

SerialLineHealth::Stats SerialManager::health(const QString &portName, qint64 nowMs) const
{
    const auto it = m_ports.constFind(key(portName));
    return it == m_ports.constEnd() ? SerialLineHealth::Stats() : it->health.stats(nowMs);
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

void SerialManager::onLine(const QString &k, const QByteArray &line, qint64 ms)
{
    auto it = m_ports.find(k);
    if (it == m_ports.end() || line.trimmed().isEmpty()) return;
    // Health counts every line, fed or not: it is about the wire.
    it->health.note(SerialLineHealth::lineDecodes(line), ms);
    if (!it->feed || !m_dispatcher) return;
    const QString port = it->link->config().portName;
    // Tab named by the port's short name ("COM3", "ttyUSB0"); keyed by the
    // full name, so two paths never share a tab.
    m_dispatcher->ingestLocal(kSerialSourceId, kvchForPort(port), line, ms, titleFor(port));
    ++it->fedLines;
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
