#include "dmichanges.h"

#include "dmipanel.h"
#include "dmitimetravel.h"
#include "logmodel.h"

#include <QSet>

namespace DmiChanges {

QString Change::text() const
{
    QStringList parts;
    for (const DmiDiff::Item &i : items) parts << i.text();
    return parts.join(QStringLiteral("; "));
}

QStringList locos(const LogModel *model)
{
    QSet<QString> keys;
    for (int r = 0; model && r < model->count(); ++r) {
        const LogEntry *e = model->entryPtrAt(r);
        if (!e) continue;
        const QString k = dmiKeyOfText(e->text);
        if (!k.isEmpty()) keys.insert(k);
    }
    QStringList out = keys.values();
    out.sort();
    return out;
}

QVector<Change> list(const LogModel *model, const QString &loco, bool withContinuous)
{
    QVector<Change> out;
    DmiState prev;
    for (int r = 0; model && r < model->count(); ++r) {
        const LogEntry *e = model->entryPtrAt(r);
        if (!e || dmiKeyOfText(e->text) != loco) continue;
        const DmiState s = dmiStateFromLine(e->text);
        if (!s.valid) continue;   // a frame that does not decode changes nothing
        if (prev.valid) {
            Change c;
            c.ms = e->epochMs;
            c.row = r;
            for (const DmiDiff::Item &i : DmiDiff::compare(prev, s))
                if (withContinuous || !i.continuous) c.items.append(i);
            if (!c.items.isEmpty()) out.append(c);
        }
        prev = s;
    }
    return out;
}

const Change *next(const QVector<Change> &changes, qint64 ms)
{
    for (const Change &c : changes)
        if (c.ms > ms) return &c;
    return nullptr;
}

const Change *previous(const QVector<Change> &changes, qint64 ms)
{
    for (int i = changes.size() - 1; i >= 0; --i)
        if (changes.at(i).ms < ms) return &changes.at(i);
    return nullptr;
}

}  // namespace DmiChanges
