#include "messagedispatcher.h"
#include "logmodel.h"
#include "keyblock.h"

#include <QDateTime>
#include <QDebug>
#include <QTimer>
#include <cstring>

MessageDispatcher::MessageDispatcher(QObject *parent)
    : QObject(parent)
{
    // Drain timer. Ticks every 30ms — fast enough that the user can't
    // perceive the latency (60Hz vsync is 16.7ms; we're under 2 frames),
    // slow enough that hundreds-of-packets-per-tick bursts coalesce into
    // single model updates instead of overwhelming the event loop.
    //
    // 30ms was picked empirically: at 33 ticks/sec the worst-case visible
    // delay is ~30ms which feels instant; tick overhead is negligible
    // (single QTimer fire, queue inspection, branch out if empty). For
    // sustained 1000 msg/sec traffic that's batches of ~30 entries per
    // tick — each batch is a single model insert + single repaint, which
    // the view handles in microseconds.
    m_drainTimer = new QTimer(this);
    m_drainTimer->setInterval(30);
    connect(m_drainTimer, &QTimer::timeout,
            this,         &MessageDispatcher::drainBatch);
    m_drainTimer->start();
}

MessageDispatcher::~MessageDispatcher() = default;

void MessageDispatcher::attachReceiver(UDPCommunication *receiver)
{
    m_receiver = receiver;

    // Cross-thread connection. Qt::QueuedConnection is the default when
    // sender and receiver live on different threads, but we name it
    // explicitly so the intent is unambiguous.
    connect(receiver, &UDPCommunication::messageReceived,
            this,     &MessageDispatcher::onMessageReceived,
            Qt::QueuedConnection);
}

LogModel *MessageDispatcher::modelForKey(const QString &tabKey) const
{
    return m_models.value(tabKey, nullptr);
}

LogModel *MessageDispatcher::ensureModel(const QString &tabKey, bool *createdOut)
{
    auto it = m_models.constFind(tabKey);
    if (it != m_models.constEnd()) {
        if (createdOut) *createdOut = false;
        return it.value();
    }
    // New key. Refuse past the ceiling — see setMaxTabs().
    if (m_models.size() >= m_maxTabs) {
        ++m_rejectedKeys;
        if (!m_warnedTabCap) {
            m_warnedTabCap = true;
            qWarning() << "MessageDispatcher: tab ceiling of" << m_maxTabs
                       << "reached; further (source_id, kvchId) pairs will be"
                       << "ignored. This usually means corrupt headers or a"
                       << "misconfigured peer.";
        }
        if (createdOut) *createdOut = false;
        return nullptr;
    }

    // Lazily create on first sight of this key.
    LogModel *m = new LogModel(this, m_perTabCapacity);
    m->setShowUtc(m_showUtc);   // new tabs match the current zone
    if (m_names) m->setNameMap(m_names);
    m->setTheme(m_theme);
    m_models.insert(tabKey, m);
    if (createdOut) *createdOut = true;
    return m;
}

void MessageDispatcher::setTheme(Theme t)
{
    m_theme = t;
    // Push to every existing model. setTheme is a no-op if the model
    // already has the same theme, so the cost of repeated calls is
    // negligible.
    for (LogModel *m : m_models) {
        m->setTheme(t);
    }
}

QString MessageDispatcher::friendlyNameFor(const QString &tabKey) const
{
    const QString mapped = m_names ? m_names->lookupByKey(tabKey) : tabKey;
    if (mapped == tabKey && m_friendlyOverride.contains(tabKey)) return m_friendlyOverride.value(tabKey);
    return mapped;
}

void MessageDispatcher::ingestLocal(quint8 sourceId, quint16 kvchId, const QByteArray &payload,
                                    qint64 arrivalMs, const QString &friendlyName)
{
    auto msg = ParsedMessagePtr::create();
    msg->header.source_id = sourceId;
    msg->header.destination_id = 101;
    msg->header.message_id = 1;
    msg->header.message_len = quint16(qMin(payload.size(), 0xFFFF));
    msg->header.kvchId = kvchId;
    msg->payload = payload;
    msg->arrivalMs = arrivalMs;
    if (!friendlyName.isEmpty()) {
        m_friendlyOverride.insert(QStringLiteral("%1_%2").arg(int(sourceId)).arg(int(kvchId)), friendlyName);
    }
    m_pending.append(msg);
    ++m_localPending;       // not the receiver's: never credited back to it
}

quint64 MessageDispatcher::droppedCount() const
{
    return m_receiver ? m_receiver->droppedCount() : 0;
}

quint64 MessageDispatcher::malformedCount() const
{
    return m_receiver ? m_receiver->malformedCount() : 0;
}

quint64 MessageDispatcher::queueFullCount() const
{
    return m_receiver ? m_receiver->queueFullCount() : 0;
}

quint64 MessageDispatcher::receivedCount() const
{
    return m_receiver ? m_receiver->receivedCount() : 0;
}

int MessageDispatcher::currentQueueDepth() const
{
    return m_receiver ? m_receiver->currentQueueDepth() : 0;
}

void MessageDispatcher::feedBlockAssembler(const LogEntryPtr &entry,
                                           const QString &key)
{
    if (!entry) return;

    auto it = m_blocks.find(key);

    if (KeyBlock::isBlockStart(entry->text)) {
        // A new marker abandons any half-collected block: the previous one
        // was truncated, and guessing at it would be worse than dropping it.
        BlockBuf b;
        b.anchor = entry;
        b.lines << entry->text;
        m_blocks.insert(key, b);
        return;
    }
    if (it == m_blocks.end()) return;

    it->lines << entry->text;

    // Decode once enough lines have arrived; the count is also a ceiling,
    // so an interrupted block cannot grow without bound.
    if (it->lines.size() < KeyBlock::expectedLineCount()) return;

    const KeyBlock::Result r = KeyBlock::parse(it->lines);
    if (r.valid && it->anchor) {
        // Attached to the marker row, which is where anyone selecting the
        // block would look. The individual lines stay in the log exactly as
        // they arrived — the decode is an addition, never a replacement.
        LogEntryPtr a = it->anchor;
        a->decodedFields.clear();
        for (const FieldRow &row : KeyBlock::describe(r)) {
            a->decodedFields.append({ row.field.trimmed(), row.value });
        }
        a->fieldsDecoded = true;
    }
    m_blocks.remove(key);
}

void MessageDispatcher::injectBannerToAllTabs(const QString &message)
{
    // Build one synthetic entry per existing tab. Each gets the tab's own
    // (source, kvch) header so the entry sits naturally in that tab's
    // model — but severity is Error so the row is conspicuously colored
    // and any future severity filter will keep it visible.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    for (auto it = m_models.constBegin(); it != m_models.constEnd(); ++it) {
        const QString &key = it.key();
        LogModel *model = it.value();

        auto banner = QSharedPointer<LogEntry>::create();
        banner->epochMs        = now;
        banner->text           = message;
        banner->severity       = Severity::Error;
        banner->direction      = Direction::None;
        // Reconstruct a header with the tab's source/kvch so tabKey() works.
        // We parse the key back out — it was built from those fields.
        const int us = key.indexOf('_');
        if (us > 0) {
            banner->header.source_id =
                static_cast<quint8>(key.left(us).toInt());
            banner->header.kvchId =
                static_cast<quint16>(key.mid(us + 1).toInt());
        }
        banner->cacheDerived(m_showUtc);   // after the header is populated

        model->appendEntry(banner);

        // Emit BOTH signals. This previously fired only the per-entry one,
        // so the banner reached disk but the tab never scrolled to it and
        // lastSeenMs was left stale — the row announcing dropped data could
        // sit below the fold on a scroll-locked tab.
        emit entriesAppended(key, QVector<LogEntryPtr>{ banner });
        emit entryAppended(key, banner);
    }
}

LogEntryPtr MessageDispatcher::buildEntry(const QByteArray &wire,
                                          qint64            arrivalMs,
                                          const ColorRules *rules,
                                          bool              utc)
{
    constexpr int kHdr = int(sizeof(STRUCT_MESSAGE_HEADER));
    if (wire.size() < kHdr) return LogEntryPtr();

    auto entry = QSharedPointer<LogEntry>::create();
    entry->epochMs = arrivalMs;
    std::memcpy(&entry->header, wire.constData(), kHdr);

    // Payload text. Trailing NULs are stripped (the backend pads fixed-size
    // buffers), and CR/LF are flattened because a log row is one line.
    QString text = QString::fromUtf8(wire.constData() + kHdr,
                                     wire.size() - kHdr);
    while (!text.isEmpty() && text.back() == QChar('\0')) {
        text.chop(1);
    }
    text.replace(QChar('\r'), QChar(' '));
    text.replace(QChar('\n'), QStringLiteral("  "));
    entry->text = text;

    // rawBytes is the wire form, kept verbatim.
    entry->rawBytes = wire;

    if (rules) {
        const ColorRules::RuleMatch m = rules->classify(entry->text);
        entry->severity  = m.severity;
        entry->direction = m.direction;
        entry->fgLight   = m.fgLight;
        entry->bgLight   = m.bgLight;
        entry->fgDark    = m.fgDark;
        entry->bgDark    = m.bgDark;
    }

    entry->cacheDerived(utc);
    return entry;
}

void MessageDispatcher::onMessageReceived(ParsedMessagePtr msg)
{
    // Fast path: just enqueue. All decoding / classification / model
    // updates happen in drainBatch() on a ~30ms tick. This keeps the
    // GUI event loop responsive even when a thousand packets arrive in
    // a single burst — we'd otherwise run the full decode+model-update
    // pipeline a thousand times in a row, saturating the event loop and
    // freezing the window.
    //
    // The receiver thread already credits its own queue when it fires
    // this signal; the message-consumed credit-back happens in
    // drainBatch where we're about to throw the work away into the
    // model.
    if (!msg) return;
    m_pending.append(msg);
}

void MessageDispatcher::drainBatch()
{
    if (m_pending.isEmpty()) return;

    // Swap-and-clear pattern: take everything pending into a local
    // vector before processing. New arrivals during this call go into
    // the (now empty) m_pending and will be picked up next tick.
    QVector<ParsedMessagePtr> local;
    local.swap(m_pending);
    const int localCount = m_localPending;
    m_localPending = 0;

    // First pass: convert ParsedMessage → LogEntry, group by tab key.
    // We don't touch the model yet — we want all entries built and
    // grouped before we start firing tabRequested / appendEntries
    // signals.
    QHash<QString, QVector<LogEntryPtr>> byKey;
    QStringList newTabs;          // tabs we'll need to announce
    byKey.reserve(qMin(local.size(), 64));

    // Flat, arrival-ordered copy for the merged chronological view. Built
    // in the same pass rather than by re-sorting afterwards: `local` is
    // already in arrival order, and arrival order is time order because
    // arrivalMs is stamped at the socket.
    QVector<LogEntryPtr> ordered;
    ordered.reserve(local.size());

    for (const ParsedMessagePtr &msg : local) {
        if (!msg) continue;

        // Build the canonical LogEntry. The conversion itself lives in
        // buildEntry() so session loading uses the identical code path;
        // here we just assemble the wire form the archive would have
        // stored and hand it over.
        QByteArray wire;
        wire.reserve(int(sizeof(STRUCT_MESSAGE_HEADER)) + msg->payload.size());
        wire.append(reinterpret_cast<const char*>(&msg->header),
                    sizeof(STRUCT_MESSAGE_HEADER));
        wire.append(msg->payload);

        // Arrival time captured on the receiver thread when the datagram
        // was read. NOT the current time: this loop runs on a ~30ms batch
        // tick and may be draining a backlog, so "now" can be arbitrarily
        // later than when the message actually landed.
        LogEntryPtr entry = buildEntry(wire, msg->arrivalMs, m_rules, m_showUtc);
        if (!entry) continue;

        const QString key = entry->cachedTabKey;

        // Single hash probe instead of contains() + insert() + operator[].
        auto bucket = byKey.find(key);
        if (bucket == byKey.end()) {
            // First time seeing this key in this tick. modelForKey() also
            // creates the model on first sight overall, and returns null if
            // we're at the tab ceiling — in which case drop the entry.
            bool created = false;
            if (!ensureModel(key, &created)) continue;
            if (created) newTabs.append(key);
            bucket = byKey.insert(key, QVector<LogEntryPtr>());
        }
        bucket->append(entry);
        ordered.append(entry);          // global arrival order, all sources
        feedBlockAssembler(entry, key);
    }

    // Announce new tabs (must happen BEFORE the appendEntries calls so
    // MainWindow's tab UI exists when entriesAppended fires).
    for (const QString &k : newTabs) {
        emit tabRequested(k, friendlyNameFor(k));
    }

    // Second pass: per-tab batched insert + signal. One begin/end
    // pair on the model per tab; one entriesAppended signal per tab.
    for (auto it = byKey.constBegin(); it != byKey.constEnd(); ++it) {
        const QString &key = it.key();
        const QVector<LogEntryPtr> &entries = it.value();
        if (entries.isEmpty()) continue;

        LogModel *model = modelForKey(key);
        if (model) {
            model->appendEntries(entries);
        }

        // Batched signal — MainWindow uses this for the heavy UI
        // updates (single scrollToBottom, single count refresh).
        emit entriesAppended(key, entries);

        // Per-entry signal — for fine-grained consumers like
        // TabPopoutWindow. These get one fire per entry, but they
        // do cheap work; the cost is small and the consumer's API
        // contract stays the same.
        for (const LogEntryPtr &e : entries) {
            emit entryAppended(key, e);
        }
    }

    if (!ordered.isEmpty()) {
        emit allEntriesAppended(ordered);
    }

    // Credit the receiver: tell it we've consumed all the messages it
    // gave us this tick, so its queue-depth gauge for backpressure
    // stays accurate.
    if (m_receiver) {
        m_receiver->notifyMessagesConsumed(local.size() - localCount);
    }
}
