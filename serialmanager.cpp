#include "serialmanager.h"

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
    connect(e.link, &SerialLink::opened, this, &SerialManager::portsChanged);
    connect(e.link, &SerialLink::closed, this, &SerialManager::portsChanged);
    m_ports.insert(k, e);
    return e.link;
}

bool SerialManager::open(const SerialConfig &config)
{
    return linkFor(config.portName)->open(config);
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
    if (it != m_ports.end()) it->fedLines = 0;
}

void SerialManager::onLine(const QString &k, const QByteArray &line, qint64 ms)
{
    auto it = m_ports.find(k);
    if (it == m_ports.end() || !it->feed || !m_dispatcher || line.trimmed().isEmpty()) return;
    const QString port = it->link->config().portName;
    // Tab named by the port's short name ("COM3", "ttyUSB0"); keyed by the
    // full name, so two paths never share a tab.
    m_dispatcher->ingestLocal(kSerialSourceId, kvchForPort(port), line, ms, tabTitleFor(port));
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
