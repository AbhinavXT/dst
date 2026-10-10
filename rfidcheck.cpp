#include "rfidcheck.h"

#include <QHash>
#include <QSet>
#include <QStringList>

#include <cctype>

namespace RfidCheck {

namespace {

QString hex8(quint32 v) { return QStringLiteral("%1").arg(v, 8, 16, QLatin1Char('0')).toUpper(); }

// +1 when the loco meets rising locations, -1 falling, 0 unknown.
int travel(const RfidTag::Route &r)
{
    return r.dir == RfidTag::DirNominal ? 1 : r.dir == RfidTag::DirReverse ? -1 : 0;
}

bool isFoot(int placement, int dir)
{
    if (placement == 6) return true;                                  // signal foot (both)
    return dir == RfidTag::DirNominal ? placement == 1 : dir == RfidTag::DirReverse && placement == 2;
}

// Where two tags' field values differ, ignoring what a main and its duplicate
// are meant to differ in.
QStringList differences(const QByteArray &a, const QByteArray &b)
{
    static const QSet<QString> expected{ QStringLiteral("duplication"), QStringLiteral("abs_loc"),
                                         QStringLiteral("abs_loc_1"), QStringLiteral("abs_loc_2") };
    const QHash<QString, qint64> va = RfidTag::values(a), vb = RfidTag::values(b);
    QStringList out;
    for (auto it = va.cbegin(); it != va.cend(); ++it)
        if (!expected.contains(it.key()) && vb.value(it.key()) != it.value()) out << it.key();
    out.sort();
    return out;
}

}  // namespace

QVector<Finding> check(const RfidTag::Route &r)
{
    QVector<Finding> out;
    const int s = travel(r);
    auto add = [&out](int row, const QString &tag, Level level, const QString &text) {
        out.append({ row, tag, level, text });
    };
    if (r.tags.isEmpty()) {
        add(-1, QString(), Level::Info, QStringLiteral("The route has no tags"));
        return out;
    }
    if (s == 0)
        add(-1, QString(), Level::Info,
            QStringLiteral("Direction not set: location order, TIN and signals are not checked"));

    QVector<RfidTag::Summary> t;
    QVector<QString> names;
    for (const RfidTag::Tag &tag : r.tags) {
        t << RfidTag::summary(tag.bytes);
        names << RfidTag::nameOf(tag.bytes);
    }

    bool lcNoted = false;
    QHash<int, int> mainRow;                       // unique id -> first row as a main tag
    QSet<int> mainsWithDuplicate;
    for (int i = 0; i < t.size(); ++i) {
        const RfidTag::Summary &x = t.at(i);
        const QString &n = names.at(i);

        // ---- the tag itself -----------------------------------------------------------------
        if (!x.crcOk)
            add(i, n, Level::Attention, QStringLiteral("CRC-30 stored %1, the contents give %2: a loco would not process this tag")
                                            .arg(hex8(x.crcStored), hex8(x.crcCalc)));
        if (x.type < 9 || x.type > 12)
            add(i, n, Level::Attention, QStringLiteral("Type %1 is not a tag type (9 to 12)").arg(x.type));
        if (x.type == 10 && !lcNoted) {
            lcNoted = true;
            add(i, n, Level::Info, QStringLiteral("LC gate tag: its layout is the specification's; no LC gate tag "
                                                  "has been seen in a capture to check it against"));
        }

        // ---- main and duplicate -------------------------------------------------------------
        if (!x.duplicate) {
            if (mainRow.contains(x.unique))
                add(i, n, Level::Attention, QStringLiteral("Main tag %1 is also at row %2").arg(x.unique).arg(mainRow.value(x.unique) + 1));
            else
                mainRow.insert(x.unique, i);
        } else {
            const bool after = i > 0 && !t.at(i - 1).duplicate && t.at(i - 1).unique == x.unique;
            if (!after) {
                int at = -1;
                for (int k = 0; k < t.size() && at < 0; ++k)
                    if (!t.at(k).duplicate && t.at(k).unique == x.unique) at = k;
                add(i, n, Level::Attention,
                    at < 0 ? QStringLiteral("No main tag %1 in the route").arg(x.unique)
                           : QStringLiteral("Not right after its main tag %1 (row %2)").arg(x.unique).arg(at + 1));
            } else {
                mainsWithDuplicate.insert(x.unique);
                const RfidTag::Summary &m = t.at(i - 1);
                if (s != 0 && m.absLoc != kNotApplicable && x.absLoc != kNotApplicable && (x.absLoc - m.absLoc) * s <= 0)
                    add(i, n, Level::Attention, QStringLiteral("At %1 m, not further along the direction than its main tag (%2 m)")
                                                    .arg(x.absLoc).arg(m.absLoc));
                const QStringList diff = differences(r.tags.at(i - 1).bytes, r.tags.at(i).bytes);
                if (!diff.isEmpty())
                    add(i, n, Level::Attention, QStringLiteral("Differs from its main tag in %1").arg(diff.join(QStringLiteral(", "))));
            }
        }

        // ---- along the route ----------------------------------------------------------------
        if (i == 0) continue;
        const RfidTag::Summary &p = t.at(i - 1);
        const bool sameTag = x.unique == p.unique;     // a main tag and its duplicate
        if (s != 0 && !sameTag && p.type != 12 && x.type != 12 && p.absLoc != kNotApplicable
            && x.absLoc != kNotApplicable && (x.absLoc - p.absLoc) * s <= 0)
            add(i, n, Level::Attention,
                x.absLoc == p.absLoc ? QStringLiteral("At the same location as %1 (%2 m)").arg(names.at(i - 1)).arg(x.absLoc)
                                     : QStringLiteral("At %1 m: %2 m back from %3 along the direction")
                                           .arg(x.absLoc).arg(qAbs(x.absLoc - p.absLoc)).arg(names.at(i - 1)));
        if (s != 0 && !sameTag && !x.duplicate) {
            const int tinP = s > 0 ? p.tinNom : p.tinRev, tinX = s > 0 ? x.tinNom : x.tinRev;
            if (tinP != tinX)
                add(i, n, Level::Info, QStringLiteral("TIN for this direction changes %1 → %2").arg(tinP).arg(tinX));
        }
    }

    // ---- main tags with no duplicate ------------------------------------------------------------
    QStringList lone;
    int firstLone = -1;
    for (int i = 0; i < t.size(); ++i)
        if (!t.at(i).duplicate && !mainsWithDuplicate.contains(t.at(i).unique) && mainRow.value(t.at(i).unique) == i) {
            lone << names.at(i);
            if (firstLone < 0) firstLone = i;
        }
    if (!lone.isEmpty())
        add(firstLone, lone.size() == 1 ? lone.first() : QString(), Level::Info,
            QStringLiteral("%1 main tag%2 with no duplicate: %3")
                .arg(lone.size()).arg(lone.size() == 1 ? "" : "s").arg(lone.join(QStringLiteral(", "))));

    // ---- signals --------------------------------------------------------------------------------
    if (s != 0) {
        QHash<QString, int> signalOf;               // foot tag -> signal index
        for (int k = 0; k < r.signalList.size(); ++k) signalOf.insert(r.signalList.at(k).footTag.trimmed(), k);
        for (int i = 0; i < t.size(); ++i) {
            if (t.at(i).duplicate || (t.at(i).type != 9 && t.at(i).type != 10)) continue;
            const QString id = QString::number(t.at(i).unique);
            const bool foot = isFoot(t.at(i).placement, r.dir);
            if (foot && !signalOf.contains(id))
                add(i, names.at(i), Level::Info, QStringLiteral("Signal-foot tag for this direction, with no signal in the signals table"));
            if (!foot && signalOf.contains(id))
                add(i, names.at(i), Level::Attention,
                    QStringLiteral("Signal %1 is at this tag, whose placement (%2) is not a signal foot for this direction")
                        .arg(r.signalList.at(signalOf.value(id)).name, RfidTag::enumLabel(QStringLiteral("rfidPlace"), t.at(i).placement)));
        }
        for (const RfidTag::Signal &sg : r.signalList) {
            bool ok = false;
            const int id = sg.footTag.trimmed().toInt(&ok);
            if (!ok || !mainRow.contains(id))
                add(-1, QString(), Level::Attention,
                    QStringLiteral("Signal %1 (id %2) names foot tag %3, which is not a main tag of this route")
                        .arg(sg.name, sg.sigId, sg.footTag));
        }
    }

    // ---- adjustment tags ------------------------------------------------------------------------
    for (int i = 0; i < t.size(); ++i) {
        if (t.at(i).type != 12) continue;
        const QHash<QString, qint64> v = RfidTag::values(r.tags.at(i).bytes);
        QString text = QStringLiteral("Adjustment tag: location-1 %1 m, location-2 %2 m")
                           .arg(v.value(QStringLiteral("abs_loc_1"))).arg(v.value(QStringLiteral("abs_loc_2")));
        if (s != 0) {
            const QString field = s > 0 ? QStringLiteral("dir_corr_1") : QStringLiteral("dir_corr_2");
            text += QStringLiteral("; for this direction %1 = %2")
                        .arg(field, RfidTag::enumLabel(QStringLiteral("rfidDirCorr"), v.value(field)));
        }
        add(i, names.at(i), Level::Info, text);
    }

    // ---- the file's rows, as the RFID simulator reads them (session 204) ------------------------
    // Rows whose tag was edited since the file was read are left out: an
    // export writes them anew.
    const QVector<RfidTag::RouteRow> rr = s != 0 ? RfidTag::routeRows(r.tags, r.dir) : QVector<RfidTag::RouteRow>();
    auto hexDigits = [](const QString &v) {
        int n = 0;
        for (const QChar c : v) n += isxdigit(c.toLatin1()) ? 1 : 0;
        return n;
    };
    int stale = 0;
    for (int i = 0; i < t.size(); ++i) {
        const RfidTag::FileRow &f = r.tags.at(i).file;
        if (!f.present) continue;
        if (RfidTag::fromPages(f.pageX, f.pageY) != r.tags.at(i).bytes) {
            ++stale;
            continue;
        }
        const RfidTag::Summary &x = t.at(i);
        const QString &n = names.at(i);
        if (f.tagType != x.type)
            add(i, n, Level::Attention,
                f.tagType >= 9 && f.tagType <= 12
                    ? QStringLiteral("Simulator: the row's tag_type is %1, the tag is type %2; it decodes and sends the tag with the %3 layout")
                          .arg(f.tagType).arg(x.type).arg(RfidTag::typeName(f.tagType))
                    : QStringLiteral("Simulator: the row's tag_type is %1, the tag is type %2; it sends nothing for tag_type %1")
                          .arg(f.tagType).arg(x.type));
        if ((f.tagName == QLatin1String("main")) == x.duplicate)
            add(i, n, Level::Attention, QStringLiteral("Simulator: the row's tag_name is \"%1\", the tag is a %2 tag; reader 2 keeps it as %3")
                                            .arg(f.tagName, x.duplicate ? QStringLiteral("duplicate") : QStringLiteral("main"),
                                                 f.tagName == QLatin1String("main") ? QStringLiteral("the main tag") : QStringLiteral("the duplicate")));
        if (hexDigits(f.pageX) != 16 || hexDigits(f.pageY) != 16)
            add(i, n, Level::Attention, QStringLiteral("Simulator: page_x / page_y have %1 / %2 hex digits, not 16; it joins page_y and "
                                                       "page_x as they are, so every field after the short one is shifted")
                                            .arg(hexDigits(f.pageX)).arg(hexDigits(f.pageY)));
        if (f.rfidId != n)
            add(i, n, Level::Info, QStringLiteral("Simulator: the row's rfid_id is \"%1\"; its missing-tags list names the tag that way").arg(f.rfidId));
        if (!rr.isEmpty() && f.absLoc != double(rr.at(i).absLoc))
            add(i, n, Level::Info, QStringLiteral("Simulator: the row's abs_loc is %1 m, route.xml would write %2 m; it sends the tag at %1 m")
                                       .arg(f.absLoc, 0, 'f', 0).arg(rr.at(i).absLoc));
        const bool last = i == t.size() - 1;
        if (i > 0 && r.tags.at(i - 1).file.present && r.tags.at(i - 1).file.rfidId == f.rfidId)
            add(i, n, Level::Attention, QStringLiteral("Simulator: the row before has the same rfid_id (%1); reader 1 does not send it again").arg(f.rfidId));
        if (f.nextAbsLoc == f.absLoc)
            add(i, n, Level::Attention, last ? QStringLiteral("Simulator: the route's last row (next_rfid_abs_loc = abs_loc): never sent")
                                             : QStringLiteral("Simulator: next_rfid_abs_loc = abs_loc (%1 m): no location is inside this row, "
                                                              "so it is never sent").arg(f.absLoc, 0, 'f', 0));
        else if (!last && r.tags.at(i + 1).file.present && f.nextAbsLoc != r.tags.at(i + 1).file.absLoc) {
            const double nextAt = r.tags.at(i + 1).file.absLoc;
            QString what = QStringLiteral("the rows overlap; the earlier row in the file wins");
            if (qAbs(f.nextAbsLoc - f.absLoc) < qAbs(nextAt - f.absLoc)) {
                // The simulator takes the first row in the file whose span holds the location.
                const double mid = (f.nextAbsLoc + nextAt) / 2;
                int cover = -1;
                for (int k = 0; k < r.tags.size() && cover < 0; ++k) {
                    const RfidTag::FileRow &o = r.tags.at(k).file;
                    if (o.present && ((o.absLoc <= mid && o.nextAbsLoc > mid) || (o.absLoc >= mid && o.nextAbsLoc < mid))) cover = k;
                }
                what = cover < 0 ? QStringLiteral("a gap no row covers: the simulator ends the route there")
                                 : QStringLiteral("a gap; row %1 (%2) covers it, so the simulator sends that tag there")
                                       .arg(cover + 1).arg(r.tags.at(cover).file.rfidId);
            }
            add(i, n, Level::Attention, QStringLiteral("Simulator: next_rfid_abs_loc %1 m, the next row starts at %2 m: %3")
                                            .arg(f.nextAbsLoc, 0, 'f', 0).arg(nextAt, 0, 'f', 0).arg(what));
        }
    }
    if (stale > 0)
        add(-1, QString(), Level::Info, QStringLiteral("%1 row%2 edited since the file was read: not checked as the simulator "
                                                       "reads them (an export writes them anew)").arg(stale).arg(stale == 1 ? "" : "s"));

    std::stable_sort(out.begin(), out.end(), [](const Finding &a, const Finding &b) { return a.row < b.row; });
    return out;
}

QString deltaText(const RfidTag::Route &r, int row)
{
    if (row <= 0 || row >= r.tags.size()) return QString();
    const RfidTag::Summary p = RfidTag::summary(r.tags.at(row - 1).bytes), x = RfidTag::summary(r.tags.at(row).bytes);
    if (p.type == 12 || p.absLoc == kNotApplicable || x.absLoc == kNotApplicable) return QString();
    const int s = travel(r);
    const qint64 d = (x.absLoc - p.absLoc) * (s == 0 ? 1 : s);
    return d > 0 ? QStringLiteral("+%1").arg(d) : QString::number(d);
}

}  // namespace RfidCheck
