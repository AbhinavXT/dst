#include "tagscenario.h"

#include "rfidcheck.h"

#include <algorithm>

namespace TagScenario {

namespace {

QString mainOf(const QString &tag)
{
    return tag.endsWith(QLatin1Char('D')) ? tag.chopped(1) : tag;
}

int indexOf(const RfidTag::Route &r, const QString &name)
{
    for (int i = 0; i < r.tags.size(); ++i)
        if (r.tags.at(i).name == name) return i;
    return -1;
}

// The tag with its location field set (abs_loc_1 on an adjustment tag),
// rebuilt: CRC-30 recomputed.
bool relocate(RfidTag::Tag *t, qint64 loc, QString *why)
{
    const RfidTag::Summary s = RfidTag::summary(t->bytes);
    QHash<QString, qint64> v = RfidTag::values(t->bytes);
    v.insert(s.type == 12 ? QStringLiteral("abs_loc_1") : QStringLiteral("abs_loc"), loc);
    QString err;
    const QByteArray b = RfidTag::build(v, &err);
    if (b.isEmpty()) {
        if (why) *why = QStringLiteral("%1 cannot say %2 m: %3").arg(t->name).arg(loc).arg(err);
        return false;
    }
    t->bytes = b;
    t->file = RfidTag::FileRow();       // the file's old copy of the row no longer holds
    return true;
}

} // namespace

QString describe(Kind k)
{
    switch (k) {
    case AsIs: return QStringLiteral("as it is");
    case DuplicateMissing: return QStringLiteral("duplicate tag missing");
    case MainMissing: return QStringLiteral("main tag missing");
    case BothMissing: return QStringLiteral("both tags missing");
    case Interchanged: return QStringLiteral("main and duplicate interchanged");
    case SameLocation: return QStringLiteral("both at the same location");
    case GapUnder4: return QStringLiteral("duplicate under 4 m from the main");
    case GapOver4: return QStringLiteral("duplicate over 4 m from the main");
    }
    return QString();
}

Result apply(const RfidTag::Route &route, Kind kind, const QString &tag)
{
    Result r;
    r.route = route;
    const QString main = mainOf(tag), dup = main + QLatin1Char('D');
    const int im = indexOf(route, main), id = indexOf(route, dup);
    if (im < 0) { r.why = QStringLiteral("main tag %1 is not in the route").arg(main); return r; }
    if (id < 0 && kind != AsIs && kind != MainMissing) {
        r.why = QStringLiteral("tag %1 has no duplicate %2 in the route (Add duplicate makes one)").arg(main, dup);
        return r;
    }
    const qint64 mainLoc = RfidTag::summary(route.tags.at(im).bytes).absLoc;
    const qint64 dupLoc = id >= 0 ? RfidTag::summary(route.tags.at(id).bytes).absLoc : 0;
    // the side the duplicate is on; when both say one place, the travel direction's
    const int side = dupLoc > mainLoc ? 1 : dupLoc < mainLoc ? -1 : (route.dir == RfidTag::DirReverse ? -1 : 1);
    switch (kind) {
    case AsIs:
        r.change = QStringLiteral("as it is");
        break;
    case DuplicateMissing:
        r.route.tags.remove(id);
        r.change = QStringLiteral("%1 missing").arg(dup);
        break;
    case MainMissing:
        r.route.tags.remove(im);
        r.change = QStringLiteral("%1 missing").arg(main);
        break;
    case BothMissing:
        r.route.tags.remove(qMax(im, id));
        r.route.tags.remove(qMin(im, id));
        r.change = QStringLiteral("%1 and %2 missing").arg(main, dup);
        break;
    case Interchanged:
        r.route.tags.swapItemsAt(im, id);
        r.change = QStringLiteral("%1 read before %2").arg(im < id ? dup : main, im < id ? main : dup);
        break;
    case SameLocation:
    case GapUnder4:
    case GapOver4: {
        const qint64 gap = kind == SameLocation ? 0 : kind == GapUnder4 ? 3 : 5;
        const qint64 loc = mainLoc + side * gap;
        if (!relocate(&r.route.tags[id], loc, &r.why)) return r;
        r.change = QStringLiteral("%1 at %2 m (%3 m from %4)").arg(dup).arg(loc).arg(gap).arg(main);
        break;
    }
    }
    r.ok = true;
    return r;
}

Result jumble(const RfidTag::Route &route, const QStringList &names)
{
    Result r;
    r.route = route;
    QVector<int> rows;
    for (const QString &n : names) {
        const int i = indexOf(route, n);
        if (i < 0) { r.why = QStringLiteral("tag %1 is not in the route").arg(n); return r; }
        rows << i;
    }
    if (rows.size() < 2) { r.why = QStringLiteral("select two tags or more to jumble"); return r; }
    std::sort(rows.begin(), rows.end());
    for (int a = 0, b = rows.size() - 1; a < b; ++a, --b) r.route.tags.swapItemsAt(rows.at(a), rows.at(b));
    QStringList order;
    for (int i : rows) order << r.route.tags.at(i).name;
    r.change = QStringLiteral("read as %1").arg(order.join(QStringLiteral(", ")));
    r.ok = true;
    return r;
}

QVector<Planned> section14(const RfidTag::Route &route, const QString &tag, QString *why)
{
    const QString main = mainOf(tag), dup = main + QLatin1Char('D');
    const int im = indexOf(route, main), id = indexOf(route, dup);
    if (im < 0 || id < 0) {
        if (why) *why = QStringLiteral("the route needs tag %1 and its duplicate %2").arg(main, dup);
        return {};
    }
    const qint64 mainLoc = RfidTag::summary(route.tags.at(im).bytes).absLoc;
    const qint64 dupLoc = RfidTag::summary(route.tags.at(id).bytes).absLoc;
    if (mainLoc == dupLoc) {
        if (why) *why = QStringLiteral("%1 and %2 say one location: place the duplicate on its side first").arg(main, dup);
        return {};
    }
    const int base = dupLoc > mainLoc ? 0 : 10;     // nominal: 14.1-14.10, reverse: 14.11-14.20
    auto id14 = [base](int n) { return QStringLiteral("14.%1").arg(base + n); };
    return { { id14(1), AsIs }, { id14(2), DuplicateMissing }, { id14(3), MainMissing }, { id14(4), BothMissing },
             { id14(5), Interchanged }, { id14(6), SameLocation }, { id14(7), GapUnder4 }, { id14(9), GapOver4 } };
}

} // namespace TagScenario
