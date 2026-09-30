#include "pinboard.h"

#include "capturedecoder.h"

namespace {
// Field and source in one persisted line. Unit separator rather than a comma
// or a colon: both appear in field names and in tab keys, and a separator that
// can occur in the data is a separator that will one day split a pin in half.
const QChar kSep(0x1F);
}  // namespace

bool PinBoard::add(const QString &field, const QString &sourceKey,
                   const QString &captype)
{
    const QString f = field.trimmed();
    if (f.isEmpty()) { return false; }

    for (const Pin &p : m_pins) {
        if (p.field.compare(f, Qt::CaseInsensitive) == 0
            && p.sourceKey == sourceKey
            && p.captype.compare(captype, Qt::CaseInsensitive) == 0) {
            return false;          // already pinned; not an error
        }
    }
    Pin p;
    p.field     = f;
    p.sourceKey = sourceKey;
    p.captype   = captype.trimmed().toLower();
    m_pins.append(p);
    return true;
}

bool PinBoard::remove(int index)
{
    if (index < 0 || index >= m_pins.size()) { return false; }
    m_pins.remove(index);
    return true;
}

void PinBoard::clear() { m_pins.clear(); }

bool PinBoard::observe(const LogEntryPtr &entry, const QString &sourceKey,
                       qint64 nowMs)
{
    if (entry.isNull() || m_pins.isEmpty() || isFrozen()) { return false; }

    // Decode ONCE for all pins. Decoding per pin would multiply the cost of a
    // pin board by the number of things on it, which is exactly the direction
    // that makes a feature stop being used.
    QVector<FieldRow> rows;
    QString lineType;
    bool decoded = false;
    bool changed = false;

    for (Pin &p : m_pins) {
        // Narrowed pins ignore other sources entirely — the point of pinning
        // "LOCO_MODE from L1_V1" is that L2_V1's mode must not overwrite it.
        if (!p.sourceKey.isEmpty() && p.sourceKey != sourceKey) { continue; }

        if (!decoded) {
            // The entry's text IS the capture line — that is how a capture
            // row travels through this program — so it is parsed here rather
            // than the decoder being handed something it cannot read.
            const CaptureLine c = CaptureDecoder::parseLine(entry->text);
            if (!c.valid) { return false; }
            lineType = c.typeToken;
            rows     = CaptureDecoder::describe(c);
            decoded  = true;
        }

        // Narrowed to a packet: a pin on FRAME_NUM in slrp must not take the
        // ARP's value, which is a different counter belonging to a different
        // end of the link.
        if (!p.captype.isEmpty()
            && p.captype.compare(lineType, Qt::CaseInsensitive) != 0) {
            continue;
        }

        for (const FieldRow &r : rows) {
            if (r.field.trimmed().compare(p.field, Qt::CaseInsensitive) != 0) {
                continue;
            }
            const QString v = r.value.trimmed();
            ++p.seen;
            p.atMs        = nowMs;
            p.atEpochMs   = entry->epochMs;
            p.fromSource  = sourceKey;
            if (v != p.value) {
                // First sighting is not a change: there was nothing to change
                // from, and reporting "changed 0 s ago" the moment a pin is
                // added would be a lie the operator acts on.
                if (!p.value.isEmpty()) {
                    p.prevValue      = p.value;
                    p.changedMs      = nowMs;
                    p.changedEpochMs = entry->epochMs;
                }
                p.value = v;
                changed = true;
            }
            break;                 // first match wins; a field appears once
        }
    }
    return changed;
}

void PinBoard::freeze(qint64 nowMs, const QString &why)
{
    if (isFrozen()) { return; }   // the FIRST freeze is the interesting one
    m_frozenAtMs = nowMs > 0 ? nowMs : 1;
    m_frozenWhy  = why;
}

void PinBoard::thaw()
{
    m_frozenAtMs = 0;
    m_frozenWhy.clear();
}

QStringList PinBoard::toStrings() const
{
    QStringList out;
    out.reserve(m_pins.size());
    for (const Pin &p : m_pins) {
        out << (p.field + kSep + p.sourceKey + kSep + p.captype);
    }
    return out;
}

void PinBoard::fromStrings(const QStringList &lines)
{
    m_pins.clear();
    for (const QString &line : lines) {
        const QStringList parts = line.split(kSep);
        if (parts.isEmpty() || parts.first().trimmed().isEmpty()) { continue; }
        // Two-part lines are what an earlier build wrote. They mean "any
        // packet", which is what they did then, so they keep working.
        add(parts.first(),
            parts.size() > 1 ? parts.at(1) : QString(),
            parts.size() > 2 ? parts.at(2) : QString());
    }
}
