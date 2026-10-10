#include "planrun.h"

#include "capturedecoder.h"
#include "logmodel.h"

#include <QHash>
#include <QSet>

namespace PlanRun {

QVector<Read> readsOf(const LogModel *model, qint64 fromMs, qint64 toMs)
{
    QVector<Read> out;
    if (!model) return out;
    for (int i = 0; i < model->count(); ++i) {
        const LogEntryPtr e = model->entryAt(i);
        if (!e || !e->text.startsWith(QLatin1String("@rfid_"))) continue;
        if ((fromMs > 0 && e->epochMs < fromMs) || (toMs > 0 && e->epochMs > toMs)) continue;
        const CaptureLine c = CaptureDecoder::parseLine(e->text);
        if (!c.valid || c.bytes.size() < 1 + RfidTag::TagBytes) continue;
        const QByteArray tag = c.bytes.mid(1, RfidTag::TagBytes);      // after the reader-id byte
        const RfidTag::Summary s = RfidTag::summary(tag);
        if (s.unique == 0) continue;                                    // the null tag: nothing read
        out.append({ e->epochMs, tag, s.unique, s.duplicate });
    }
    return out;
}

int Result::count(State s) const
{
    int n = 0;
    for (const Planned &p : planned) n += p.state == s ? 1 : 0;
    return n;
}

QString stateText(State s)
{
    switch (s) {
    case State::Read:       return QStringLiteral("read");
    case State::Different:  return QStringLiteral("read, different");
    case State::NotRead:    return QStringLiteral("not read");
    case State::OutOfOrder: return QStringLiteral("read, out of order");
    }
    return QString();
}

namespace {

QString key(int unique, bool dup) { return QString::number(unique) + (dup ? QStringLiteral("D") : QString()); }

QStringList differences(const QByteArray &planned, const QByteArray &read)
{
    const QHash<QString, qint64> a = RfidTag::values(planned), b = RfidTag::values(read);
    QStringList out;
    for (auto it = a.cbegin(); it != a.cend(); ++it)
        if (b.value(it.key()) != it.value())
            out << QStringLiteral("%1 %2 → %3").arg(it.key()).arg(it.value()).arg(b.value(it.key()));
    out.sort();
    if (out.isEmpty()) out << QStringLiteral("CRC-30 only");
    return out;
}

}  // namespace

Result compare(const RfidTag::Route &route, const QVector<Read> &reads)
{
    Result r;
    // First read of each tag (by id and main/duplicate), and of its exact bytes.
    QHash<QString, int> firstOf;                   // key -> index in reads
    QHash<QByteArray, int> firstExact;
    for (int i = 0; i < reads.size(); ++i) {
        const QString k = key(reads.at(i).unique, reads.at(i).duplicate);
        if (!firstOf.contains(k)) firstOf.insert(k, i);
        if (!firstExact.contains(reads.at(i).bytes)) firstExact.insert(reads.at(i).bytes, i);
    }

    QSet<QString> planned;
    qint64 latest = 0;                             // latest first-read so far along the route
    QString latestName;
    for (int row = 0; row < route.tags.size(); ++row) {
        const QByteArray &b = route.tags.at(row).bytes;
        const RfidTag::Summary s = RfidTag::summary(b);
        const QString k = key(s.unique, s.duplicate);
        planned.insert(k);
        Planned p;
        p.row = row;
        p.name = RfidTag::nameOf(b);
        if (firstExact.contains(b)) {
            p.state = State::Read;
            p.firstMs = reads.at(firstExact.value(b)).ms;
        } else if (firstOf.contains(k)) {
            const Read &rd = reads.at(firstOf.value(k));
            p.state = State::Different;
            p.firstMs = rd.ms;
            p.detail = QStringLiteral("the tag read differs: %1").arg(differences(b, rd.bytes).join(QStringLiteral(", ")));
        } else {
            p.state = State::NotRead;
            p.detail = QStringLiteral("no @rfid frame of it in the log");
        }
        if (p.firstMs > 0) {
            if (p.firstMs < latest) {
                if (p.state == State::Read) p.state = State::OutOfOrder;
                const QString why = QStringLiteral("first read before %1, which comes earlier in the route").arg(latestName);
                p.detail = p.detail.isEmpty() ? why : p.detail + QStringLiteral("; ") + why;
            } else {
                latest = p.firstMs;
                latestName = p.name;
            }
        }
        r.planned.append(p);
    }

    QSet<QString> listed;
    for (const Read &rd : reads) {
        const QString k = key(rd.unique, rd.duplicate);
        if (planned.contains(k) || listed.contains(k)) continue;
        listed.insert(k);
        r.notPlanned << k;
        r.notPlannedMs << rd.ms;
    }
    return r;
}

RfidTag::Route routeFromRun(const QVector<Read> &reads)
{
    RfidTag::Route route;
    QSet<QString> seen;
    for (const Read &rd : reads) {
        const QString k = key(rd.unique, rd.duplicate);
        if (seen.contains(k)) continue;
        seen.insert(k);
        route.tags.append({ k, rd.bytes });
    }
    // The direction the loco met them in, from the first two locations that differ.
    for (int i = 1; i < route.tags.size() && route.dir == RfidTag::DirUnset; ++i) {
        const qint64 a = RfidTag::summary(route.tags.at(i - 1).bytes).absLoc, b = RfidTag::summary(route.tags.at(i).bytes).absLoc;
        if (a != b) route.dir = b > a ? RfidTag::DirNominal : RfidTag::DirReverse;
    }
    return route;
}

}  // namespace PlanRun
