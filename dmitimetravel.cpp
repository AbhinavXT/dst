#include "dmitimetravel.h"

#include "logentry.h"
#include "logmodel.h"

#include <QHash>
#include <QSet>

#include <algorithm>

// =============================================================================
//  DmiMoment
// =============================================================================

const DmiFrameAt *DmiMoment::frameFor(const QString &key) const
{
    for (const DmiFrameAt &f : frames) {
        if (f.key == key) return &f;
    }
    return nullptr;
}

QVector<DmiFrameAt> DmiMoment::latestFor(const QString &key) const
{
    QVector<DmiFrameAt> out;
    for (const DmiFrameAt &f : latest) {
        if (f.key == key) out << f;
    }
    return out;
}

QStringList DmiMoment::keys() const
{
    QStringList out;
    for (const DmiFrameAt &f : frames) out << f.key;
    return out;
}

// =============================================================================
//  Finding the frames
// =============================================================================

QString dmiKeyOfText(const QString &text)
{
    static const QLatin1String prefix("@dmi_");
    if (!text.startsWith(prefix)) return QString();
    int end = text.indexOf(QLatin1Char(' '), prefix.size());
    if (end < 0) end = text.size();
    return text.mid(prefix.size(), end - prefix.size());
}

QString captureTagOfText(const QString &text)
{
    if (!text.startsWith(QLatin1Char('@'))) return QString();
    int end = text.indexOf(QLatin1Char(' '), 1);
    if (end < 0) end = text.size();
    return text.mid(1, end - 1);
}

int logModelRowAfter(const LogModel *model, qint64 ms)
{
    if (!model) return 0;
    int lo = 0, hi = model->count();          // first row with epochMs > ms
    while (lo < hi) {
        const int mid = lo + (hi - lo) / 2;
        const LogEntry *e = model->entryPtrAt(mid);
        if (e && e->epochMs <= ms) lo = mid + 1;
        else                       hi = mid;
    }
    return lo;
}

int logModelRowOf(const LogModel *model, const LogEntry *entry, int hint)
{
    if (!model || !entry) return -1;
    if (model->entryPtrAt(hint) == entry) return hint;
    // Rows only ever leave from the front, so it is at or above `hint`;
    // among the rows of its millisecond.
    const int after = logModelRowAfter(model, entry->epochMs);
    for (int r = qMin(after, model->count()) - 1; r >= 0; --r) {
        const LogEntry *e = model->entryPtrAt(r);
        if (e == entry) return r;
        if (e && e->epochMs < entry->epochMs) break;
    }
    return -1;
}

DmiResolver dmiTabResolver(const QVector<LogModel *> &models, LogModel *clicked,
                           const LogEntryPtr &entry, int hint, const QString &origin)
{
    QVector<QPointer<LogModel>> guarded;
    for (LogModel *m : models) guarded.append(m);
    QPointer<LogModel> at(clicked);
    return [guarded, at, entry, hint, origin]() -> DmiMoment {
        if (at.isNull()) return DmiMoment();
        const int row = logModelRowOf(at.data(), entry.data(), hint);
        if (row < 0) return DmiMoment();
        QVector<const LogModel *> list;
        for (const QPointer<LogModel> &m : guarded) {
            if (!m.isNull()) list.append(m.data());
        }
        return dmiMomentFromModels(list, at.data(), row, origin);
    };
}

DmiMoment dmiMomentFromModels(const QVector<const LogModel *> &models,
                              const LogModel *clicked, int clickedRow,
                              const QString &origin, qint64 lookbackMs)
{
    DmiMoment m;
    const LogEntry *at = clicked ? clicked->entryPtrAt(clickedRow) : nullptr;
    if (!at) return m;
    m.valid = true;
    m.atMs = at->epochMs;
    m.origin = origin;
    if (at->text.startsWith(QLatin1Char('@'))) {
        const CaptureLine c = CaptureDecoder::parseLine(at->text);
        if (c.valid) m.preferredKey = c.key();
    }

    struct Best { qint64 ms = 0; QString text; };
    QHash<QString, Best> best;
    const qint64 floorMs = m.atMs - lookbackMs;

    // Newest first from `from`; within one model the first hit per loco is
    // its latest, so later hits of that loco are skipped without parsing.
    auto scan = [&](const LogModel *model, int from) {
        QSet<QString> seen;
        for (int r = from; r >= 0; --r) {
            const LogEntry *e = model->entryPtrAt(r);
            if (!e) continue;
            if (e->epochMs < floorMs) break;
            // Every packet type (session 169): keyed by its tag, "slrp_1_1".
            const QString key = captureTagOfText(e->text);
            if (key.isEmpty() || seen.contains(key)) continue;
            seen.insert(key);
            auto it = best.find(key);
            // The clicked model is scanned first; another tab only wins
            // with a strictly later frame.
            if (it == best.end()) best.insert(key, Best{ e->epochMs, e->text });
            else if (e->epochMs > it->ms) *it = Best{ e->epochMs, e->text };
        }
    };

    scan(clicked, clickedRow);
    for (const LogModel *model : models) {
        if (!model || model == clicked) continue;
        scan(model, logModelRowAfter(model, m.atMs) - 1);
    }

    for (auto it = best.constBegin(); it != best.constEnd(); ++it) {
        const CaptureLine cap = CaptureDecoder::parseLine(it->text);
        if (!cap.valid) continue;
        const QString loco = cap.key().isEmpty() ? it.key().section(QLatin1Char('_'), 1) : cap.key();
        m.latest.append(DmiFrameAt{ loco, cap, it->ms });
        if (cap.type == CapType::Dmi) m.frames.append(DmiFrameAt{ loco, cap, it->ms });
    }
    std::sort(m.frames.begin(), m.frames.end(),
              [](const DmiFrameAt &a, const DmiFrameAt &b) { return a.key < b.key; });
    return m;
}

// =============================================================================
//  Broker
// =============================================================================

DmiTimeTravel *DmiTimeTravel::instance()
{
    static DmiTimeTravel *s = new DmiTimeTravel;
    return s;
}

void DmiTimeTravel::offer(QObject *owner, Resolver resolver)
{
    m_owner = owner;
    m_resolver = std::move(resolver);
    m_hasResolver = bool(m_resolver);
    if (hasFollowers()) resolveNow();
}

void DmiTimeTravel::follow(QObject *follower)
{
    if (!follower) return;
    for (const QPointer<QObject> &f : m_followers) {
        if (f == follower) return;
    }
    const bool first = !hasFollowers();
    m_followers.append(follower);
    connect(follower, &QObject::destroyed, this, [this](QObject *o) { unfollow(o); });
    // Catch up to the last place the operator pointed at.
    if (first && m_hasResolver) resolveNow();
}

void DmiTimeTravel::unfollow(QObject *follower)
{
    m_followers.erase(std::remove_if(m_followers.begin(), m_followers.end(),
                                     [follower](const QPointer<QObject> &f) { return f.isNull() || f == follower; }),
                      m_followers.end());
}

void DmiTimeTravel::reset()
{
    m_followers.clear();
    m_owner.clear();
    m_resolver = nullptr;
    m_hasResolver = false;
    m_last = DmiMoment();
}

void DmiTimeTravel::resolveNow()
{
    // The window that offered it has gone: its moment cannot be computed.
    if (!m_hasResolver || m_owner.isNull()) return;
    m_last = m_resolver();
    emit momentChanged(m_last);
}
