#ifndef MESSAGEDISPATCHER_H
#define MESSAGEDISPATCHER_H

// =============================================================================
//  MessageDispatcher
//  -----------------------------------------------------------------------------
//  Replaces the original ProcessMessageThread singleton. Owns the receiver
//  thread, the color-rule engine, the friendly-name map, and the per-tab
//  log models. Lives on the GUI thread (constructed by MainWindow).
//
//  Why this exists:
//    – Patch A goal #1: kill the singletons. Now MainWindow owns one
//      MessageDispatcher; tests can construct one with a fake receiver and
//      drive it directly.
//    – Patch A goal #2: column-aware, single canonical row shape (LogEntry).
//      The dispatcher is where raw bytes become LogEntry — exactly one
//      conversion site.
//
//  Wiring:
//
//      UDPCommunication ── messageReceived ──▶ MessageDispatcher
//        (Qt::QueuedConnection, parsed bytes)         │
//                                                     ▼
//                                            ColorRules::classify
//                                            NameMap::lookup
//                                                     │ build LogEntry
//                                                     ▼
//                                          modelForTab(key) → LogModel
//                                                     │
//                                                     ▼
//                                           emit entryAppended(key, entry)
//                                                     │
//                                            (MainWindow & TabPopoutWindow
//                                             listen and update their views)
//
//  The dispatcher is the ONLY thing that creates LogModels. MainWindow
//  asks for a model by key (creating it if needed). This makes the
//  set-of-tabs and the set-of-models the same set, by construction.
// =============================================================================

#include <QObject>
#include <QHash>
#include <QPointer>
#include <QStringList>
#include <QVector>

#include "colorrules.h"
#include "logentry.h"
#include "namemap.h"
#include "theme.h"
#include "udpcommunication.h"

class LogModel;
class QThread;
class QTimer;

class MessageDispatcher : public QObject
{
    Q_OBJECT

public:
    // Caller (MainWindow) owns the dispatcher and is responsible for
    // attaching a receiver before start()ing it.
    explicit MessageDispatcher(QObject *parent = nullptr);
    ~MessageDispatcher() override;

    // Hook the dispatcher up to the UDP receiver. Must be called on the
    // GUI thread, before the receiver's thread is started.
    void attachReceiver(UDPCommunication *receiver);

    // Configuration. Both are pass-by-reference to objects MainWindow owns.
    // The dispatcher does not take ownership; MainWindow keeps these alive
    // for the dispatcher's lifetime.
    void setColorRules(ColorRules *rules) { m_rules = rules; }
    void setNameMap   (NameMap    *names) { m_names = names; }

    // Zone used when pre-rendering each entry's display time at ingest.
    // Passed down rather than read from Settings per message.
    void setShowUtc(bool utc) { m_showUtc = utc; }

    // Per-tab capacity for new LogModels. Configurable via MainWindow.
    void setPerTabCapacity(int cap) { m_perTabCapacity = cap; }

    // Hard ceiling on the number of distinct (source_id, kvchId) tabs we
    // will ever materialise.
    //
    // Both fields come straight off the wire, so the key space is 2^24
    // wide. Every first-sight key permanently allocates a LogModel, a tab,
    // and an open file handle in LogWriter — none of which were ever
    // reclaimed. A run of corrupt headers (or a misconfigured peer) would
    // walk that space and exhaust file descriptors, then memory. Past the
    // cap we drop the message and count it rather than allocating.
    void setMaxTabs(int n) { m_maxTabs = n > 0 ? n : 1; }
    int  maxTabs() const   { return m_maxTabs; }

    // Messages discarded because the tab ceiling was already reached.
    quint64 rejectedKeyCount() const { return m_rejectedKeys; }

    // Active theme. The dispatcher applies it to every new LogModel it
    // creates; existing models are updated immediately. MainWindow calls
    // this on startup and again whenever the user toggles theme.
    void setTheme(Theme t);
    Theme theme() const { return m_theme; }

    // Look up (or create) the model for a given (source, kvch) pair. The
    // dispatcher returns the same pointer for the same key on every call.
    // If `createdOut` is non-null, it's set to true when this call created
    // a new model, false when an existing one was returned.
    //
    // Returns NULLPTR when the key is new AND the tab ceiling has been hit.
    // All existing call sites already null-check.
    LogModel *modelForKey(const QString &tabKey, bool *createdOut = nullptr);

    // Friendly name for the given key (or the raw key if no mapping).
    QString friendlyNameFor(const QString &tabKey) const;

    // Snapshot of every tab key we've ever seen in this session. Used
    // by CompareWindow (and any future tool) to populate a picker.
    // Order is undefined; caller can sort if it wants stable ordering.
    QStringList knownKeys() const { return m_models.keys(); }

    // Pass-through for status-bar polling.
    quint64 droppedCount() const;
    quint64 receivedCount() const;
    int     currentQueueDepth() const;

    // Turn a wire datagram into a LogEntry: the single canonical
    // conversion, shared by live ingest and by session loading.
    //
    // Static and rule-parameterised precisely so a loaded .dlr goes through
    // the identical code path as a message off the socket. If loading had
    // its own copy of the text decoding and classification, the two would
    // drift, and a replayed archive would stop being evidence of what the
    // live view showed.
    //
    // `wire` is the complete datagram (header + payload) exactly as it
    // arrived. `rules` may be null, in which case the entry stays untagged.
    // `utc` selects the zone the display-time cache is rendered in. Passed
    // explicitly rather than read from Settings because this is static and
    // is also called from the archive-search worker thread, where a
    // QSettings read per record would be both slow and a shared-state
    // hazard.
    static LogEntryPtr buildEntry(const QByteArray &wire,
                                  qint64            arrivalMs,
                                  const ColorRules *rules,
                                  bool              utc = false);

    // Reassembles multi-line text blocks that arrive as separate
    // datagrams. See the AUTH_KEYS note in keyblock.h: the lines are
    // individually meaningless and only decode together.
    //
    // Per source, because two locos printing at once would otherwise
    // interleave into one another's blocks.
    struct BlockBuf {
        QStringList  lines;
        LogEntryPtr  anchor;      // the marker row the decode is attached to
    };
    QHash<QString, BlockBuf> m_blocks;
    QHash<QString, QString> m_friendlyOverride;   // tab key -> name, for ingestLocal
    int m_localPending = 0;                       // of m_pending, how many ingestLocal added
    void feedBlockAssembler(const LogEntryPtr &entry, const QString &key);

    // Inject a synthetic warning line into every existing tab. Used by
    // MainWindow's drop-counter watchdog (section 1c) so dropped data is
    // visible IN CONTEXT in the affected tabs and survives into saved
    // files. The synthetic entry has Severity::Error and an empty header
    // (source/dest/kvch all 0) so it is visually distinct.
    void injectBannerToAllTabs(const QString &message);

    // Session 85: text that arrived by another route than the UDP socket
    // (a serial port) enters the same pipeline, so it is decoded, classified,
    // tabbed, recorded and searchable exactly as a datagram would be. The
    // header is synthesised: source `sourceId`, dest 101, message 1,
    // kvch `kvchId` -> tab "<sourceId>_<kvchId>". `friendlyName` names the
    // tab when the name map has nothing for it.
    void ingestLocal(quint8 sourceId, quint16 kvchId, const QByteArray &payload,
                     qint64 arrivalMs, const QString &friendlyName = QString());
    // Tests: run the batch now instead of on the next tick.
    void drainNow() { drainBatch(); }

signals:
    // Fired whenever a new tab key is seen for the first time. MainWindow
    // listens and creates the corresponding QListView in the tab widget.
    // The dispatcher has already created and stored the LogModel by the
    // time this fires; MainWindow can fetch it via modelForKey().
    void tabRequested(QString tabKey, QString friendlyName);

    // Per-entry signal — fires once for every accepted message. Used by
    // fine-grained consumers like TabPopoutWindow that want to react
    // to each entry individually. NOT used by MainWindow for UI updates
    // anymore (see entriesAppended below for the batched version that
    // performs much better under load).
    void entryAppended(QString tabKey, LogEntryPtr entry);

    // BATCHED signal — fires once per drain tick (~30ms), carrying all
    // entries that arrived for one tab during that tick. MainWindow
    // listens to this for UI updates (scroll-to-bottom, last-seen,
    // count) so a thousand-packet burst produces one UI update instead
    // of a thousand. The per-tab grouping means a single tick may emit
    // multiple entriesAppended signals (one per tab with new traffic).
    void entriesAppended(QString tabKey, QVector<LogEntryPtr> entries);

    // Everything from one drain tick, across ALL sources, in arrival
    // order. This is the merged chronological stream.
    //
    // Emitted separately from entriesAppended rather than being
    // reconstructed by the consumer, because arrival order is only
    // knowable here: entriesAppended is grouped per key, so a consumer
    // stitching those back together would see each source's batch
    // contiguously and lose the interleaving that makes the merged view
    // worth having. Since arrivalMs is captured at the socket, arrival
    // order IS time order.
    void allEntriesAppended(QVector<LogEntryPtr> entries);

private slots:
    // Connected (queued) to UDPCommunication::messageReceived. This is
    // now just an enqueue path — heavy lifting happens in drainBatch().
    void onMessageReceived(ParsedMessagePtr msg);

    // Periodic timer callback. Drains the pending queue, builds entries,
    // groups by tab, calls model->appendEntries() once per tab, emits
    // entriesAppended() once per tab plus entryAppended() per entry for
    // backwards compat with per-entry subscribers.
    void drainBatch();

private:
    // Receiver — weak handle so we survive its destruction order.
    QPointer<UDPCommunication> m_receiver;

    // Borrowed; owned by MainWindow. Optional — null is acceptable and the
    // dispatcher will fall back to default styling / raw key.
    ColorRules *m_rules = nullptr;
    NameMap    *m_names = nullptr;

    int m_perTabCapacity = 200'000;
    int m_maxTabs        = 512;
    quint64 m_rejectedKeys = 0;
    bool m_warnedTabCap    = false;
    Theme m_theme = Theme::Light;
    bool  m_showUtc = false;

    // tabKey → model. Models are parented to `this` so they are destroyed
    // when the dispatcher goes away.
    QHash<QString, LogModel*> m_models;

    // Pending queue + drain timer. onMessageReceived appends here; the
    // timer drains the whole queue every tick. The queue lives on the
    // GUI thread (we're already on the GUI thread when onMessageReceived
    // fires, courtesy of Qt::QueuedConnection from the receiver thread).
    QVector<ParsedMessagePtr> m_pending;
    QTimer *m_drainTimer = nullptr;
};

#endif // MESSAGEDISPATCHER_H
