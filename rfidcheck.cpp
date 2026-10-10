#include "rfidcheck.h"

#include <QHash>
#include <QSet>
#include <QStringList>

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
