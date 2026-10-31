#include "stationstretch.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QRegularExpression>

#include <algorithm>

namespace StationStretch {

int Stretch::indexOf(int stationId) const
{
    for (int i = 0; i < members.size(); ++i) if (members[i].station.stationId == stationId) return i;
    return -1;
}

int Stretch::holding(int tag) const
{
    for (int i = 0; i < members.size(); ++i)
        for (const StationKavach::Route &r : members[i].station.routes)
            for (const StationKavach::RouteTag &t : r.tags)
                if (t.tag == tag) return i;
    return -1;
}

bool isStretchFile(const QString &path)
{
    QFile f(path);
    if (!path.endsWith(QLatin1String(".json"), Qt::CaseInsensitive) || !f.open(QIODevice::ReadOnly)) return false;
    return QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("format")).toString() == QLatin1String(kFormat);
}

bool load(const QString &path, Stretch *out, QString *err, QStringList *notes)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = QObject::tr("cannot read %1").arg(path);
        return false;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String(kFormat)) {
        if (err) *err = QObject::tr("%1 is not a stretch file (\"format\": \"%2\")").arg(QFileInfo(path).fileName(), kFormat);
        return false;
    }
    Stretch s;
    const QDir dir = QFileInfo(path).absoluteDir();
    for (const QJsonValue &v : root.value(QStringLiteral("stations")).toArray()) {
        const QString file = dir.filePath(v.toObject().value(QStringLiteral("file")).toString());
        Member m;
        m.path = file;
        QString e;
        QStringList kn;
        if (!StationLayout::load(file, &m.layout, &e) || m.layout.kavach.isEmpty()
            || !StationKavach::fromJson(m.layout.kavach, &m.station, &e, &kn) || m.station.routes.isEmpty()) {
            if (notes) *notes << QObject::tr("%1: %2").arg(QFileInfo(file).fileName(), e.isEmpty() ? QObject::tr("no KAVACH routes") : e);
            continue;
        }
        m.station.tagLocs = StationLayout::tagLocationsByNumber(m.layout);
        m.state = StationKavach::State::initial(m.station);
        s.members << m;
    }
    if (s.members.isEmpty()) {
        if (err) *err = QObject::tr("none of the stretch's stations has KAVACH routes");
        return false;
    }
    *out = s;
    return true;
}

int which(const Stretch &s, const QHash<QString, qint64> &pkt, bool arp, QString *why)
{
    const int loco = int(pkt.value(QStringLiteral("SOURCE_LOCO_ID")));
    if (arp) {
        const int appr = int(pkt.value(QStringLiteral("APPROACHING_STN_ID")));
        if (appr) {
            const int i = s.indexOf(appr);
            if (i < 0 && why) *why = QObject::tr("ARP for station %1, which is not on this stretch").arg(appr);
            return i;
        }
    }
    if (s.with.contains(loco)) return s.with.value(loco);
    const int i = s.holding(int(pkt.value(QStringLiteral("LAST_RFID_TAG"), -1)));
    if (i < 0 && why)
        *why = QObject::tr("loco %1 is with no station yet, and no station's routes hold tag %2")
                   .arg(loco).arg(pkt.value(QStringLiteral("LAST_RFID_TAG")));
    return i;
}

QStringList mainLine(const Stretch &s, bool down, int *dir)
{
    const QRegularExpression want(down ? QStringLiteral("^\\s*(down|dn)\\b") : QStringLiteral("^\\s*up\\b"),
                                  QRegularExpression::CaseInsensitiveOption);
    auto isMain = [&](const QString &name) {
        return name.contains(QLatin1String("main"), Qt::CaseInsensitive) && !name.contains(QLatin1String("loop"), Qt::CaseInsensitive)
               && want.match(name).hasMatch();
    };
    QHash<QString, qint64> loc;
    int nominal = 0, reverse = 0;
    for (const Member &m : s.members) {
        QHash<QString, qint64> l;   // tags whose CRC-30 holds: a corrupt page's location is no place
        for (const StationLayout::Tag &t : m.layout.tags) {
            const StationLayout::TagInfo in = StationLayout::info(t);
            if (in.ok && in.crcOk && !l.contains(t.name)) l.insert(t.name, in.absLoc);
        }
        for (const StationLayout::Line &ln : m.layout.lines) {
            if (!isMain(ln.name)) continue;
            for (const QString &t : ln.tags)
                if (!t.endsWith(QLatin1Char('D')) && l.contains(t) && !loc.contains(t)) loc.insert(t, l.value(t));
        }
    }
    // The way its routes run: those starting on one of its tags.
    for (const Member &m : s.members)
        for (const StationKavach::Route &r : m.station.routes)
            if (!r.tags.isEmpty() && loc.contains(QString::number(r.tags.first().tag))) (r.pktDir == 2 ? reverse : nominal)++;
    QStringList out = loc.keys();
    const bool rising = nominal >= reverse;
    std::sort(out.begin(), out.end(), [&](const QString &a, const QString &b) {
        return rising ? loc.value(a) < loc.value(b) : loc.value(a) > loc.value(b);
    });
    if (dir) *dir = rising ? 1 : 2;
    return out;
}

QVector<Step> run(Stretch s, const QStringList &line, int dir, int aspect, const Schema::Encoder &enc)
{
    for (Member &m : s.members)
        for (const StationKavach::Signal &g : m.station.signalList) m.state.aspect.insert(g.name, aspect);
    QHash<QString, qint64> locs;
    for (const Member &m : s.members) {
        const QHash<QString, qint64> l = StationLayout::tagLocations(m.layout);
        for (auto it = l.cbegin(); it != l.cend(); ++it)
            if (!locs.contains(it.key())) locs.insert(it.key(), it.value());
    }
    QVector<Step> out;
    const int loco = 1;
    int with = -1, pending = 0;
    qint64 frame = 1000;
    for (const QString &name : line) {
        if (name.endsWith(QLatin1Char('D'))) continue;
        bool ok = false;
        const int tag = name.toInt(&ok);
        if (!ok) continue;
        Step st;
        st.tag = tag;
        st.absLoc = locs.value(name, 0);
        // Who: the station it is with when that holds the tag; else the one it
        // was handed to; else whichever holds it (and say it was not handed over).
        const bool holds = with >= 0 && [&]() {
            for (const StationKavach::Route &r : s.members[with].station.routes)
                for (const StationKavach::RouteTag &t : r.tags) if (t.tag == tag) return true;
            return false;
        }();
        if (!holds) {
            const int h = s.holding(tag);
            if (pending && s.indexOf(pending) >= 0 && (h < 0 || s.members[h].station.stationId == pending || h == s.indexOf(pending))) {
                with = s.indexOf(pending);
                st.event = QObject::tr("handed over to %1").arg(pending);
            } else if (h >= 0) {
                if (with >= 0 && h != with)
                    st.event = QObject::tr("not handed over: station %1's routes end before tag %2; station %3 holds it")
                                   .arg(s.members[with].station.stationId).arg(tag).arg(s.members[h].station.stationId);
                with = h;
            } else {
                st.note = QObject::tr("tag %1 is on no route of any station on the stretch").arg(tag);
                out << st;
                continue;
            }
            pending = 0;
        }
        Member &m = s.members[with];
        const QHash<QString, qint64> lsrp{ { "FRAME_NUM", frame++ }, { "SOURCE_LOCO_ID", loco }, { "LAST_RFID_TAG", tag },
                                           { "MOVEMENT_DIR", dir }, { "ABS_LOCO_LOC", st.absLoc } };
        QList<StationKavach::Loco> others;
        const StationKavach::Answer a = StationKavach::answer(m.station, m.state, &m.locos[loco], lsrp, enc, others);
        st.station = m.station.stationId;
        st.ok = a.ok;
        st.note = a.ok ? a.note : a.why;
        for (const Schema::SubEntry &x : a.subs)
            if (x.type == 0) {
                st.ma = x.values.value(QStringLiteral("MA_W_R_T_SIG"), -1);
                if (x.values.value(QStringLiteral("NEXT_STN_COMM")) == 1)
                    st.handTo = int(x.values.value(QStringLiteral("APPR_STN_ILC_IBS_ID")));
            }
        if (st.handTo) pending = st.handTo;
        out << st;
    }
    return out;
}

}  // namespace StationStretch
