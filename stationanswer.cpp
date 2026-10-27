#include "stationanswer.h"

#include <QObject>

namespace StationAnswer {

QHash<int, qint64> tagLocations(const StationLayout::Layout &l)
{
    QHash<int, qint64> out;
    for (const StationLayout::Tag &t : l.tags) {
        const StationLayout::TagInfo i = StationLayout::info(t);
        if (i.ok) out.insert(i.unique, i.absLoc);
    }
    return out;
}

QHash<QString, qint64> aapFor(QHash<QString, qint64> aap, const QHash<QString, qint64> &arp,
                              quint16 stnRandom)
{
    aap.insert(QStringLiteral("FRAME_NUM"), arp.value(QStringLiteral("FRAME_NUM")));
    aap.insert(QStringLiteral("DEST_LOCO_ID"), arp.value(QStringLiteral("SOURCE_LOCO_ID")));
    aap.insert(QStringLiteral("STN_RND_NUM_RS"), stnRandom);
    return aap;
}

Reply slrpFor(const QHash<QString, qint64> &lsrp, const QHash<int, qint64> &tags, int *refTag,
              QHash<QString, qint64> *slrp, QVector<Schema::SubEntry> *subs)
{
    Reply r;
    const int formRef = int(slrp->value(QStringLiteral("LAST_REF_RFID")));
    if (!tags.contains(formRef)) {
        r.why = QObject::tr("the form's LAST_REF_RFID %1 is not in the station layout").arg(formRef);
        return r;
    }
    const int read = int(lsrp.value(QStringLiteral("LAST_RFID_TAG")));
    if (tags.contains(read)) *refTag = read;
    if (!tags.contains(*refTag)) *refTag = formRef;

    const int travel = slrp->value(QStringLiteral("PKT_DIR")) == 2 ? -1 : 1;
    const qint64 shift = travel * (tags.value(*refTag) - tags.value(formRef));
    const qint64 dps = slrp->value(QStringLiteral("DIST_PKT_START")) - shift;
    if (dps < -16384 || dps > 16383) {   // DIST_PKT_START: 15 bits, signed
        r.why = QObject::tr("DIST_PKT_START from tag %1 would be %2 m, outside 15 bits").arg(*refTag).arg(dps);
        return r;
    }
    QVector<Schema::SubEntry> out = *subs;
    QString appr;
    for (Schema::SubEntry &s : out) {
        // (station FRAME_NUM - last loco FRAME_NUM) / 2, C.4.2: the reply carries the LSRP's own.
        if (s.values.contains(QStringLiteral("FRAME_OFFSET"))) s.values.insert(QStringLiteral("FRAME_OFFSET"), 0);
        if (s.type != 0 || !s.values.contains(QStringLiteral("APPR_SIG_DIST"))) continue;   // 0: MovementAuthority
        const qint64 a = s.values.value(QStringLiteral("APPR_SIG_DIST")) - shift;
        if (a < 0 || a > 32767) {   // APPR_SIG_DIST: 15 bits
            r.why = QObject::tr("APPR_SIG_DIST from tag %1 would be %2 m: the signal is %3")
                        .arg(*refTag).arg(a)
                        .arg(a < 0 ? QObject::tr("behind that tag") : QObject::tr("too far ahead"));
            return r;
        }
        s.values.insert(QStringLiteral("APPR_SIG_DIST"), a);
        appr = QObject::tr(", APPR_SIG_DIST %1 m").arg(a);
    }
    *subs = out;
    slrp->insert(QStringLiteral("FRAME_NUM"), lsrp.value(QStringLiteral("FRAME_NUM")));
    slrp->insert(QStringLiteral("LAST_REF_RFID"), *refTag);
    slrp->insert(QStringLiteral("DIST_PKT_START"), dps);
    r.note = QObject::tr("ref tag %1, DIST_PKT_START %2 m").arg(*refTag).arg(dps) + appr;
    if (read != *refTag)
        r.note += QObject::tr(" (tag %1 read, not in the layout)").arg(read);
    r.ok = true;
    return r;
}

}  // namespace StationAnswer
