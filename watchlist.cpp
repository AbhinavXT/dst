#include "watchlist.h"

#include "namemap.h"

namespace {
const QChar kSep(0x1F);
}  // namespace

void WatchList::compile(int index)
{
    if (index < 0 || index >= m_watches.size()) { return; }
    Watch &w = m_watches[index];
    Compiled &c = m_compiled[index];

    c.ok = c.query.parse(w.expr);
    if (c.ok) {
        w.parseError.clear();
        w.errorOffset = -1;
    } else {
        // Kept, not dropped. A row that vanished on a typo would leave the
        // operator unsure whether the watch was ever added — and a watch
        // believed to be armed and silently absent is the worst outcome this
        // feature can produce.
        w.parseError  = c.query.errorString();
        w.errorOffset = c.query.errorOffset();
    }
}

int WatchList::add(const QString &expr, const QString &label)
{
    const QString e = expr.trimmed();
    if (e.isEmpty()) { return -1; }

    Watch w;
    w.expr  = e;
    w.label = label.trimmed();
    m_watches.append(w);
    m_compiled.append(Compiled());
    compile(m_watches.size() - 1);
    return m_watches.size() - 1;
}

bool WatchList::remove(int index)
{
    if (index < 0 || index >= m_watches.size()) { return false; }
    m_watches.remove(index);
    m_compiled.remove(index);
    return true;
}

void WatchList::clear()
{
    m_watches.clear();
    m_compiled.clear();
}

bool WatchList::rearm(int index)
{
    if (index < 0 || index >= m_watches.size()) { return false; }
    Watch &w = m_watches[index];
    w.fired     = false;
    w.firedAtMs = 0;
    w.firedBy.clear();
    w.evidence.clear();
    w.firedEntry.reset();
    // The count goes too. A re-armed watch holding its old total would report
    // an event that has not happened again.
    w.matches   = 0;
    w.occurrences = 0;
    w.lastMatchMs = 0;
    w.armed     = true;
    return true;
}

void WatchList::rearmAll()
{
    for (int i = 0; i < m_watches.size(); ++i) { rearm(i); }
}

QVector<int> WatchList::observe(const LogEntryPtr &entry,
                                const QString &sourceKey,
                                const NameMap *names)
{
    QVector<int> justFired;
    if (entry.isNull() || m_watches.isEmpty()) { return justFired; }

    for (int i = 0; i < m_watches.size(); ++i) {
        Watch &w = m_watches[i];
        if (!w.armed || !m_compiled.at(i).ok) { continue; }

        if (!m_compiled.at(i).query.match(*entry, names)) { continue; }

        ++w.matches;
        const bool newOccurrence = w.occurrences == 0
                                || entry->epochMs - w.lastMatchMs > kOccurrenceGapMs;
        w.lastMatchMs = entry->epochMs;
        if (newOccurrence) ++w.occurrences;
        if (!w.fired || (w.everyTime && newOccurrence)) {
            w.fired      = true;
            w.firedAtMs  = entry->epochMs;
            w.firedBy    = sourceKey;
            w.evidence   = entry->text;
            w.firedEntry = entry;
            justFired.append(i);
        }
        // Already firing: counted, not announced again. A condition true for
        // three hundred frames is one event. With everyTime, a new occurrence
        // (after a gap) is announced as the event it is.
    }
    return justFired;
}

QStringList WatchList::toStrings() const
{
    QStringList out;
    out.reserve(m_watches.size());
    for (const Watch &w : m_watches) {
        // The evidence is deliberately NOT saved. A watch reloaded next
        // session showing a hit from a run that has ended would be read as a
        // hit in this one.
        // Fourth part (session 82): the actions, as letters. Older builds
        // read three parts and ignore it.
        QString actions;
        if (w.beep) actions += QLatin1Char('b');
        if (w.bookmark) actions += QLatin1Char('m');
        if (w.everyTime) actions += QLatin1Char('e');
        out << (w.expr + kSep + w.label + kSep
                + (w.armed ? QStringLiteral("1") : QStringLiteral("0")) + kSep + actions);
    }
    return out;
}

void WatchList::fromStrings(const QStringList &lines)
{
    clear();
    for (const QString &line : lines) {
        const QStringList parts = line.split(kSep);
        if (parts.isEmpty() || parts.first().trimmed().isEmpty()) { continue; }
        const int i = add(parts.first(),
                          parts.size() > 1 ? parts.at(1) : QString());
        if (i >= 0 && parts.size() > 2) {
            m_watches[i].armed = (parts.at(2) != QStringLiteral("0"));
        }
        if (i >= 0 && parts.size() > 3) {
            m_watches[i].beep = parts.at(3).contains(QLatin1Char('b'));
            m_watches[i].bookmark = parts.at(3).contains(QLatin1Char('m'));
            m_watches[i].everyTime = parts.at(3).contains(QLatin1Char('e'));
        }
    }
}

bool WatchList::setActions(int index, bool beep, bool bookmark, bool everyTime)
{
    if (index < 0 || index >= m_watches.size()) return false;
    m_watches[index].beep = beep;
    m_watches[index].bookmark = bookmark;
    m_watches[index].everyTime = everyTime;
    return true;
}
