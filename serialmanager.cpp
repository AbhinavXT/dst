#include "serialmanager.h"

#include "capturedecoder.h"
#include "messagedispatcher.h"

#include <QDateTime>
#include <QFileInfo>
#include <QSerialPortInfo>
#include <QRegularExpression>
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
    // Session 156: no finder by default -- the scan thread and the planner
    // find adapters. setPortFinder() brings back the synchronous lookup.
    m_scanner = new SerialPortScanner(this);
    connect(m_scanner, &SerialPortScanner::scanned, this, &SerialManager::onScanned);
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
    // Unique, stable, and including an entry whose names have all moved to
    // another (session 156).
    return m_owned;
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
        // USB serial number stays. Session 156: found by a scan on the scan
        // thread, not by enumerating here on the GUI thread.
        raw->adapter.lastName = raw->link->config().portName;
        raw->needIdentity = true;
        m_scanner->request();
        if (raw->reconnecting) finishReconnect(raw);
        if (raw->asyncOpening) {
            raw->asyncOpening = false;
            emit openFinished(raw->link->config().portName, true, QString());
            finishBatchItem(raw, true, QString());
        }
        emit portsChanged();
    });
    connect(e->link, &SerialLink::openFailed, this, [this, raw](const QString &why) {
        if (raw->reconnecting) {
            // Enumerated but not ready yet: the next tick looks again.
            raw->reconnecting = false;
            emit portsChanged();
            return;
        }
        if (raw->asyncOpening) {
            raw->asyncOpening = false;
            emit openFinished(raw->link->config().portName, false, why);
            finishBatchItem(raw, false, why);
        }
    });
    connect(e->link, &SerialLink::closed, this, &SerialManager::portsChanged);
    connect(e->link, &SerialLink::lost, this, [this, raw](const QString &why) { onLost(raw, why); });
    m_ports.insert(key(portName), e);
    m_owned << e;
    return e->link;
}

bool SerialManager::open(const SerialConfig &config)
{
    SerialLink *l = linkFor(config.portName);
    if (const EntryPtr e = entry(config.portName)) {
        e->lostAtMs = 0;            // an explicit open ends any wait
        e->reconnecting = false;
        if (e->asyncOpening) finishBatchItem(e.data(), false, tr("opened again before it finished"));
        e->asyncOpening = false;
        resetCardCheck(e.data());   // and is the operator's word on which card
    }
    updateReconnectTimer();
    return l->open(config);
}

void SerialManager::openAsync(const SerialConfig &config)
{
    SerialLink *l = linkFor(config.portName);
    const EntryPtr e = entry(config.portName);
    if (e->asyncOpening) finishBatchItem(e.data(), false, tr("opened again before it finished"));
    e->lostAtMs = 0;
    e->reconnecting = false;
    resetCardCheck(e.data());
    e->asyncOpening = true;
    updateReconnectTimer();
    l->openAsync(config);
}

QString SerialManager::resolveProfilePort(const SerialProfile &profile) const
{
    if (profile.usbSerial.isEmpty()) return profile.config.portName;
    const QString now = m_finder ? m_finder(profile.usbSerial, profile.config.portName)
                                 : findPortBySerial(profile.usbSerial, profile.config.portName);
    return now.isEmpty() ? profile.config.portName : now;
}

bool SerialManager::openProfile(const SerialProfile &profile)
{
    SerialProfile p = profile;
    p.config.portName = resolveProfilePort(profile);
    setFeed(p.config.portName, p.feed);
    setLabel(p.config.portName, p.name);
    if (const EntryPtr e = entry(p.config.portName))
        if (e->usbSerial.isEmpty()) e->usbSerial = p.usbSerial;
    return open(p.config);
}

QStringList SerialManager::openProfiles(const QVector<SerialProfile> &profiles)
{
    QStringList failed;
    for (const SerialProfile &p : profiles) {
        const QString port = resolveProfilePort(p);
        SerialLink *l = link(port);
        if (l && l->isOpen()) {
            // Already running: a second "Open all" must not reopen (and so
            // briefly drop) a port that is capturing.
            setLabel(port, p.name);
            continue;
        }
        if (!openProfile(p)) {
            failed << tr("%1 (%2): %3").arg(p.name, port, linkFor(port)->errorText());
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
    const bool waiting = e->lostAtMs > 0 || e->reconnecting || e->suspect;
    e->lostAtMs = 0;
    e->reconnecting = false;
    if (e->asyncOpening) {
        e->asyncOpening = false;
        finishBatchItem(e.data(), false, tr("closed before it opened"));
    }
    // A held or suspect port is set aside with it: Open again is the
    // operator's word on which card it is.
    e->verifying = false;
    e->held.clear();
    e->suspect = false;
    updateReconnectTimer();
    e->link->close();
    if (waiting) emit portsChanged();
}

void SerialManager::closeAll()
{
    for (const EntryPtr &e : entries()) {
        e->lostAtMs = 0;
        e->reconnecting = false;
        e->asyncOpening = false;
        e->verifying = false;
        e->held.clear();
        e->suspect = false;
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
    const bool decodes = SerialLineHealth::lineDecodes(line);
    e->health.note(decodes, ms);
    const QString type = decodes ? captureTypeOf(line) : QString();

    if (e->verifying) {
        // Session 156: just reconnected; is this the same card?
        e->held.append(qMakePair(line, ms));
        if (e->held.size() > 2000) e->held.removeFirst();
        if (type.isEmpty()) return;                        // a banner, a prompt: no evidence
        if (e->typesSeen.contains(type)) { endVerify(e, true); return; }
        if (!e->strangers.contains(type)) e->strangers << type;
        if (++e->strangerLines >= kVerifyLines) markSuspect(e);
        return;
    }
    if (e->suspect) return;                                // kept out of the console
    if (!type.isEmpty() && e->typesSeen.size() < 64) e->typesSeen.insert(type);
    feedLine(e, line, ms);
}

void SerialManager::feedLine(Entry *e, const QByteArray &line, qint64 ms)
{
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
    bool waiting = false;
    for (const EntryPtr &e : entries())
        if (e->lostAtMs > 0 && !e->reconnecting) waiting = true;
    if (!waiting) { updateReconnectTimer(); return; }

    if (m_finder) {
        // The synchronous stand-in (tests): by serial, else by name.
        for (const EntryPtr &e : entries()) {
            if (e->lostAtMs <= 0 || e->reconnecting) continue;
            const QString found = m_finder(e->usbSerial, e->link->config().portName);
            if (!found.isEmpty())
                startReconnect(e.data(), found, e->usbSerial.isEmpty() ? SerialMatchBy::Name : SerialMatchBy::Serial);
        }
        return;
    }
    // Session 156: one scan, on the scan thread, for every lost port; a
    // tick while one runs is folded into it. onScanned() does the rest.
    m_scanner->request();
}

void SerialManager::onScanned(const SerialPortList &ports)
{
    // What each newly opened port is.
    for (const EntryPtr &e : entries()) {
        if (!e->needIdentity || !e->link->isOpen()) continue;
        const SerialPortSnapshot *s = SerialScan::find(ports, e->link->config().portName);
        e->needIdentity = false;
        if (!s) continue;                      // a pty, a path not enumerated: known by name
        e->adapter = SerialScan::identify(e->link->config().portName, ports);
        if (!e->adapter.serial.isEmpty()) e->usbSerial = e->adapter.serial;
    }

    // Profiles waiting to find their adapters.
    if (!m_pendingProfiles.isEmpty()) {
        const QVector<QPair<int, SerialProfile>> pending = m_pendingProfiles;
        m_pendingProfiles.clear();
        for (const auto &bp : pending) {
            const SerialProfile &p = bp.second;
            SerialAdapterId id;
            id.lastName = p.config.portName;
            id.serial = p.usbSerial;
            id.location = p.usbLocation;
            const QVector<SerialReconnectPlan> plan = SerialScan::planReconnects({ id }, ports, openPorts());
            QString port = p.config.portName;
            if (!plan.isEmpty()) {
                port = plan.first().portName;
            } else if (!id.serial.isEmpty() || !id.location.isEmpty()) {
                // Its adapter is not here. Opening the old name would read
                // whatever is plugged in there now.
                Batch &b = m_batches[bp.first];
                b.failed << tr("%1 (%2): its adapter (%3) is not plugged in")
                                .arg(p.name, p.config.portName, id.describe());
                if (--b.pending == 0) {
                    const Batch done = m_batches.take(bp.first);
                    emit profilesOpened(done.opened, done.failed);
                }
                continue;
            }
            SerialConfig c = p.config;
            c.portName = port;
            setFeed(port, p.feed);
            setLabel(port, p.name);
            const EntryPtr e = entry(port);
            if (e->usbSerial.isEmpty()) e->usbSerial = p.usbSerial;
            openAsync(c);
            e->batch = bp.first;               // after: openAsync() settles any earlier one
        }
    }

    // Lost ports: plan them all at once, so no two get the same adapter.
    // Only from a scan that began after the loss: one already running when
    // the adapter was pulled still lists it, and says nothing about whether
    // it is back.
    const qint64 scanStarted = m_scanner->lastStartedAtMs();
    QVector<Entry *> lost;
    QVector<SerialAdapterId> ids;
    QStringList taken = openPorts();
    for (const EntryPtr &e : entries()) {
        if (e->reconnecting) taken << e->link->config().portName;
        if (e->lostAtMs <= 0 || e->reconnecting) continue;
        if (scanStarted <= e->lostAtMs) {
            // Stale for this port; ask for a fresh one (folded if one runs).
            m_scanner->request();
            continue;
        }
        SerialAdapterId id = e->adapter;
        if (id.lastName.isEmpty()) id.lastName = e->link->config().portName;
        if (id.serial.isEmpty() && !e->usbSerial.isEmpty() && id.location.isEmpty()) id.serial = e->usbSerial;
        lost << e.data();
        ids << id;
    }
    if (lost.isEmpty()) return;
    for (const SerialReconnectPlan &plan : SerialScan::planReconnects(ids, ports, taken))
        startReconnect(lost.at(plan.index), plan.portName, plan.by);
}

void SerialManager::startReconnect(Entry *e, const QString &portName, SerialMatchBy by)
{
    SerialConfig c = e->link->config();
    e->reconnectFrom = c.portName;
    e->reconnectBy = by;
    c.portName = portName;
    e->reconnecting = true;
    e->link->openAsync(c);                 // opened() -> finishReconnect()
}

void SerialManager::finishReconnect(Entry *e)
{
    e->reconnecting = false;
    const QString found = e->link->config().portName;
    const QString oldName = e->reconnectFrom;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 gap = now - e->lostAtMs;
    e->lostAtMs = 0;
    if (key(found) != key(oldName)) {
        // Same entry, new name. The name may have belonged to another port
        // (a swap); it now means this one, and that one keeps its own entry.
        for (const EntryPtr &owned : m_owned)
            if (owned.data() == e) m_ports.insert(key(found), owned);
    }
    QString how;
    if (e->reconnectBy != SerialMatchBy::None && e->reconnectBy != SerialMatchBy::Name)
        how = tr(", found %1").arg(serialMatchByText(e->reconnectBy));
    marker(e, key(found) == key(oldName)
               ? tr("── reconnected %1, gap %2 s%3 ──").arg(found).arg(gap / 1000.0, 0, 'f', 1).arg(how)
               : tr("── reconnected as %1 (was %2), gap %3 s%4 ──")
                     .arg(found, oldName).arg(gap / 1000.0, 0, 'f', 1).arg(how),
           now);
    // Hold its lines until they show it is the same card (session 156).
    if (!e->typesSeen.isEmpty()) {
        e->verifying = true;
        e->held.clear();
        e->strangers.clear();
        e->strangerLines = 0;
        const quint64 round = ++m_verifyRound;
        e->verifyRound = round;
        QTimer::singleShot(int(kVerifyMs), this, [this, e, round]() { verifyTimeout(e, round); });
    }
    emit portReconnected(found, gap);
    updateReconnectTimer();
}

// ---- is it the same card? (session 156) -------------------------------------------------

QString SerialManager::captureTypeOf(const QByteArray &line)
{
    if (!line.startsWith('@')) return QString();
    int end = 1;
    while (end < line.size() && line.at(end) != ' ' && line.at(end) != '\t') ++end;
    QString t = QString::fromLatin1(line.mid(1, end - 1)).toLower();
    // "dop1_2_1" -> "dop1": drop the loco's _N_N suffix, keep "analog_top".
    static const QRegularExpression suffix(QStringLiteral("(_\\d+)+$"));
    t.remove(suffix);
    return t;
}

void SerialManager::endVerify(Entry *e, bool confirmed)
{
    e->verifying = false;
    const QVector<QPair<QByteArray, qint64>> held = e->held;
    e->held.clear();
    if (!confirmed) {
        marker(e, tr("── no log line in %1 s after reconnecting: could not confirm it is the same card; "
                     "feeding it ──").arg(kVerifyMs / 1000), QDateTime::currentMSecsSinceEpoch());
    }
    for (const auto &l : held) {
        const QString type = captureTypeOf(l.first);
        if (!type.isEmpty() && e->typesSeen.size() < 64) e->typesSeen.insert(type);
        feedLine(e, l.first, l.second);
    }
}

void SerialManager::markSuspect(Entry *e)
{
    e->verifying = false;
    e->held.clear();
    e->suspect = true;
    QStringList had = e->typesSeen.values();
    std::sort(had.begin(), had.end());
    QStringList now = e->strangers;
    for (QString &t : had) t.prepend(QLatin1Char('@'));
    for (QString &t : now) t.prepend(QLatin1Char('@'));
    const QString port = e->link->config().portName;
    e->suspectText = tr("%1 now sends %2, but this tab had %3: it may be a different card "
                        "(adapters swapped?). Its lines are kept out of this tab. Check which cable "
                        "is in which adapter, then Feed anyway, or Close and Open it.")
                         .arg(port, now.join(QStringLiteral(", ")), had.join(QStringLiteral(", ")));
    marker(e, tr("── %1 ──").arg(e->suspectText), QDateTime::currentMSecsSinceEpoch());
    emit portSuspect(port, e->suspectText);
    emit portsChanged();
}

void SerialManager::verifyTimeout(Entry *e, quint64 round)
{
    if (!e->verifying || e->verifyRound != round) return;
    // Log lines, none of a type it had: not the same card. None at all
    // (the card is quiet, or rebooting): nothing to judge by.
    if (e->strangerLines > 0) markSuspect(e);
    else endVerify(e, false);
}

void SerialManager::resetCardCheck(Entry *e)
{
    e->typesSeen.clear();
    e->verifying = false;
    e->held.clear();
    e->strangers.clear();
    e->strangerLines = 0;
    e->suspect = false;
    e->suspectText.clear();
}

bool SerialManager::isVerifying(const QString &portName) const
{
    const EntryPtr e = entry(portName);
    return e && e->verifying;
}

bool SerialManager::isSuspect(const QString &portName) const
{
    const EntryPtr e = entry(portName);
    return e && e->suspect;
}

QString SerialManager::suspectText(const QString &portName) const
{
    const EntryPtr e = entry(portName);
    return e && e->suspect ? e->suspectText : QString();
}

void SerialManager::feedAnyway(const QString &portName)
{
    const EntryPtr e = entry(portName);
    if (!e || !e->suspect) return;
    resetCardCheck(e.data());          // what it sends from now on is this port's
    marker(e.data(), tr("── fed anyway, by the operator ──"), QDateTime::currentMSecsSinceEpoch());
    emit portsChanged();
}

SerialAdapterId SerialManager::adapterOf(const QString &portName) const
{
    const EntryPtr e = entry(portName);
    return e ? e->adapter : SerialAdapterId();
}

// ---- profiles, without waiting (session 156) ----------------------------------------------

void SerialManager::openProfilesAsync(const QVector<SerialProfile> &profiles)
{
    const int id = m_nextBatch++;
    Batch b;
    QVector<SerialProfile> toOpen;
    for (const SerialProfile &p : profiles) {
        // Already running -- under its profile name, its adapter or its
        // name: a second "Open all" must not reopen (and so drop) it.
        bool running = false;
        for (const EntryPtr &e : entries()) {
            if (!e->link->isOpen()) continue;
            if (e->label.compare(p.name, Qt::CaseInsensitive) == 0
                || (!p.usbSerial.isEmpty() && e->usbSerial == p.usbSerial)
                || key(e->link->config().portName) == key(p.config.portName)) {
                setLabel(e->link->config().portName, p.name);
                running = true;
                break;
            }
        }
        if (running) ++b.opened;
        else toOpen << p;
    }
    b.pending = toOpen.size();
    if (b.pending == 0) {
        emit profilesOpened(b.opened, b.failed);
        return;
    }
    m_batches.insert(id, b);
    if (m_finder) {
        // The synchronous stand-in (tests): resolve here.
        for (const SerialProfile &p : toOpen) {
            SerialConfig c = p.config;
            c.portName = resolveProfilePort(p);
            setFeed(c.portName, p.feed);
            setLabel(c.portName, p.name);
            openAsync(c);
            entry(c.portName)->batch = id;
        }
        return;
    }
    for (const SerialProfile &p : toOpen) m_pendingProfiles << qMakePair(id, p);
    m_scanner->request();
}

void SerialManager::finishBatchItem(Entry *e, bool ok, const QString &error)
{
    const int id = e->batch;
    e->batch = -1;
    if (id < 0 || !m_batches.contains(id)) return;
    Batch &b = m_batches[id];
    if (ok) ++b.opened;
    else b.failed << tr("%1 (%2): %3").arg(e->label.isEmpty() ? e->link->config().portName : e->label,
                                           e->link->config().portName, error);
    if (--b.pending == 0) {
        const Batch done = m_batches.take(id);
        emit profilesOpened(done.opened, done.failed);
    }
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
    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    QString sn;
    for (const QSerialPortInfo &i : ports) {
        if (i.portName().compare(shortN, Qt::CaseInsensitive) == 0
            || i.systemLocation() == portName) {
            sn = i.serialNumber().trimmed();
        }
    }
    if (sn.isEmpty()) return QString();
    // Session 156: a serial number another adapter here also has (clone FTDI
    // chips ship with one) identifies nothing; keying settings or a profile
    // by it would mix the adapters up.
    int same = 0;
    for (const QSerialPortInfo &i : ports)
        if (i.serialNumber().trimmed() == sn) ++same;
    return same == 1 ? sn : QString();
}

QString SerialManager::usbLocationFor(const QString &portName)
{
    const QHash<QString, QString> locs = SerialScan::platformLocations();
#ifdef Q_OS_WIN
    const QString loc = locs.value(shortName(portName).toUpper());
#else
    const QString loc = locs.value(shortName(portName));
#endif
    if (loc.isEmpty()) return QString();
    int same = 0;
    for (const QString &v : locs) if (v == loc) ++same;
    return same == 1 ? loc : QString();
}

QString SerialManager::findPortBySerial(const QString &usbSerial, const QString &lastName)
{
    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    if (!usbSerial.isEmpty()) {
        // By the adapter, under whatever name it now has -- when only one
        // adapter has that number (session 156: clones share one).
        QString found;
        int count = 0;
        for (const QSerialPortInfo &i : ports)
            if (i.serialNumber() == usbSerial) { found = i.portName(); ++count; }
        return count == 1 ? found : QString();
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

QString SerialManager::adapterGroup(const QString &usbSerial)
{
    QString n = usbSerial.trimmed();
    n.replace(QLatin1Char('/'), QLatin1Char('_')).replace(QLatin1Char('\\'), QLatin1Char('_'));
    return QStringLiteral("serial/adapters/") + n;
}

void SerialManager::savePortSettings(QSettings &s, const SerialConfig &config, bool feed)
{
    savePortSettings(s, config, feed, usbSerialFor(config.portName));
}

void SerialManager::savePortSettings(QSettings &s, const SerialConfig &config, bool feed,
                                     const QString &usbSerial)
{
    if (config.portName.trimmed().isEmpty()) return;
    QStringList groups{ settingsGroup(config.portName) };
    if (!usbSerial.trimmed().isEmpty()) groups << adapterGroup(usbSerial);
    for (const QString &g : groups) {
        config.save(s, g);
        s.setValue(g + QStringLiteral("/feed"), feed);
    }
    s.setValue(QStringLiteral("serial/lastPort"), config.portName);
}

bool SerialManager::loadPortSettings(QSettings &s, const QString &portName,
                                     SerialConfig *config, bool *feed)
{
    return loadPortSettings(s, portName, config, feed, usbSerialFor(portName));
}

bool SerialManager::loadPortSettings(QSettings &s, const QString &portName,
                                     SerialConfig *config, bool *feed, const QString &usbSerial)
{
    // The adapter first: it is the same card under whatever name it has today.
    QString g;
    if (!usbSerial.trimmed().isEmpty() && s.contains(adapterGroup(usbSerial) + QStringLiteral("/feed")))
        g = adapterGroup(usbSerial);
    else if (s.contains(settingsGroup(portName) + QStringLiteral("/feed")))
        g = settingsGroup(portName);
    else
        return false;
    *config = SerialConfig::load(s, g);
    config->portName = portName;           // where it is now, not where it was saved
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
        p.macros = SerialMacro::loadList(s, QStringLiteral("macros"));
        p.usbSerial = s.value(QStringLiteral("usbSerial")).toString();
        p.usbLocation = s.value(QStringLiteral("usbLocation")).toString();
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
        SerialMacro::saveList(s, QStringLiteral("macros"), p.macros);
        s.setValue(QStringLiteral("usbSerial"), p.usbSerial);
        s.setValue(QStringLiteral("usbLocation"), p.usbLocation);
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

// ---- macros ---------------------------------------------------------------------------

QByteArray SerialMacro::bytes(bool *ok) const
{
    bool good = true;
    QByteArray out = hex ? serialParseHex(text, &good) : serialUnescape(text);
    if (ok) *ok = good;
    if (!good) return QByteArray();
    return out + ending;
}

bool SerialMacro::looksDangerous(const QString &labelOrText)
{
    static const QRegularExpression re(QStringLiteral(
        "\\b(RESET|REBOOT|RESTART|ERASE|FORMAT|FLASH|FACTORY|DELETE|CLEAR|BURN|WRITE|PROG(RAM)?)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(labelOrText).hasMatch();
}

QVector<SerialMacro> SerialMacro::loadList(QSettings &s, const QString &arrayName)
{
    QVector<SerialMacro> out;
    const int n = s.beginReadArray(arrayName);
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        SerialMacro m;
        m.label = s.value(QStringLiteral("label")).toString();
        m.text = s.value(QStringLiteral("text")).toString();
        m.hex = s.value(QStringLiteral("hex"), false).toBool();
        m.ending = QByteArray::fromHex(s.value(QStringLiteral("ending"), QStringLiteral("0d0a")).toByteArray());
        m.confirm = s.value(QStringLiteral("confirm"), false).toBool();
        if (!m.label.trimmed().isEmpty()) out << m;
    }
    s.endArray();
    return out;
}

void SerialMacro::saveList(QSettings &s, const QString &arrayName, const QVector<SerialMacro> &macros)
{
    s.remove(arrayName);
    s.beginWriteArray(arrayName, macros.size());
    for (int i = 0; i < macros.size(); ++i) {
        s.setArrayIndex(i);
        const SerialMacro &m = macros.at(i);
        s.setValue(QStringLiteral("label"), m.label);
        s.setValue(QStringLiteral("text"), m.text);
        s.setValue(QStringLiteral("hex"), m.hex);
        // As hex: an ending is bytes (CR, LF), which an ini mangles as text.
        s.setValue(QStringLiteral("ending"), QString::fromLatin1(m.ending.toHex()));
        s.setValue(QStringLiteral("confirm"), m.confirm);
    }
    s.endArray();
}
