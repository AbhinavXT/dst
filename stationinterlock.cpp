#include "stationinterlock.h"

#include <QObject>

#include <algorithm>

namespace StationInterlock {

Track trackOf(const StationLayout::Layout &l)
{
    Track t;
    t.circuits = StationLayout::circuits(l);
    const QHash<QString, QString> lines = StationLayout::lineOfTag(l);
    for (auto it = lines.cbegin(); it != lines.cend(); ++it) {
        bool ok = false;
        const int n = it.key().toInt(&ok);   // a duplicate ("…D") is not a number: its main tag's line
        if (ok && !t.lineOfTag.contains(n)) t.lineOfTag.insert(n, it.value());
    }
    return t;
}

Occupancy occupancy(const Track &t, const QList<StationKavach::Loco> &locos)
{
    Occupancy out;
    QList<StationKavach::Loco> sorted = locos;
    std::sort(sorted.begin(), sorted.end(), [](const StationKavach::Loco &a, const StationKavach::Loco &b) { return a.id < b.id; });
    for (const StationKavach::Loco &o : sorted) {
        if (o.absLoc <= 0 || (o.dir != 1 && o.dir != 2)) {
            out.notes << QObject::tr("loco %1 has not said where it is (ABS_LOCO_LOC %2, direction %3): it occupies nothing")
                             .arg(o.id).arg(o.absLoc).arg(o.dir);
            continue;
        }
        if (!t.lineOfTag.contains(o.lastTag)) {
            out.notes << QObject::tr("loco %1: tag %2 is on no line of the layout: it occupies nothing").arg(o.id).arg(o.lastTag);
            continue;
        }
        const QString line = t.lineOfTag.value(o.lastTag);
        const qint64 rear = o.absLoc + (o.dir == 2 ? o.trainLength : -qint64(o.trainLength));
        const qint64 lo = std::min(o.absLoc, rear), hi = std::max(o.absLoc, rear);
        for (const StationLayout::Circuit &c : t.circuits)
            if (c.line == line && c.from <= hi && c.to >= lo && !out.by.contains(c.name)) out.by.insert(c.name, o.id);
    }
    return out;
}

}  // namespace StationInterlock
