#include "soslog.h"

#include "capturedecoder.h"
#include "logmodel.h"

#include <QHash>
#include <QMap>

#include <algorithm>
#include <cmath>
#include <limits>

namespace SosLog {
namespace {

// Little-endian readers over a frame already checked for length.
quint8  u8 (const QByteArray &b, int o) { return quint8(b.at(o)); }
quint16 u16(const QByteArray &b, int o) { return quint16(u8(b, o) | (u8(b, o + 1) << 8)); }
quint32 u32(const QByteArray &b, int o)
{
    return quint32(u8(b, o)) | (quint32(u8(b, o + 1)) << 8) | (quint32(u8(b, o + 2)) << 16)
         | (quint32(u8(b, o + 3)) << 24);
}
qint32 i32(const QByteArray &b, int o) { return qint32(u32(b, o)); }
double dm(const QByteArray &b, int o) { return double(i32(b, o)) / 10.0; }

constexpr int kSnapFixed = 65;
constexpr int kStation   = 20;
constexpr int kSource    = 56;
constexpr int kEvent     = 27;

QString metres(double m) { return QStringLiteral("%1 m").arg(m, 0, 'f', 0); }

// "Manual SoS" -> "manual SoS" (mid-sentence), keeping the rest as written.
QString lowerFirst(const QString &s) { return s.isEmpty() ? s : s.left(1).toLower() + s.mid(1); }

// The capture tag's token without decoding ("@sossrc_1_1 ..." -> "sossrc").
QString sosToken(const QString &text)
{
    if (!text.startsWith(QLatin1String("@sos"))) return QString();
    const int sp = text.indexOf(QLatin1Char(' '));
    const QString tag = text.mid(1, sp < 0 ? -1 : sp - 1);
    const QStringList parts = tag.split(QLatin1Char('_'));
    if (parts.size() < 3) return QString();
    return parts.mid(0, parts.size() - 2).join(QLatin1Char('_'));
}

QString sosTypeName(int v)
{
    switch (v) {
    case 0: return QStringLiteral("0 (No SoS)");
    case 1: return QStringLiteral("1 (Foreign RFID)");
    case 3: return QStringLiteral("3 (Odo error ≥ 120 m)");
    case 4: return QStringLiteral("4 (SPAD)");
    case 5: return QStringLiteral("5 (Rear-end)");
    case 6: return QStringLiteral("6 (Head-on)");
    case 7: return QStringLiteral("7 (Shunt violation)");
    case 8: return QStringLiteral("8 (Station General SoS)");
    default: return QString::number(v);
    }
}

int threatOfBit(int bit)
{
    switch (bit) {
    case BitManual:  return ManualSos;
    case BitUnusual: return UnusualStop;
    case BitHeadOn:  return HeadOn;
    case BitRearEnd: return RearEnd;
    case BitParted:  return TrainParted;
    default:         return ThreatNone;
    }
}

}  // namespace

// ---- decoding ----------------------------------------------------------------------

// Session 192: README 03's minimal layout. 37 B, 24 per loco, 13 per station.
constexpr int kSnap2Fixed = 37;
constexpr int kSrc2 = 24;
constexpr int kStn2 = 13;

namespace {

// Closest of its kind, and the target, from the flags alone (the minimal
// layout logs neither): the same picks SOS_RecomputeAggregates makes.
void deriveV2(Snapshot &s)
{
    for (int bit : { int(BitManual), int(BitUnusual), int(BitHeadOn), int(BitRearEnd), int(BitParted) }) {
        const bool collision = bit == BitHeadOn || bit == BitRearEnd;
        int best = -1;
        for (int i = 0; i < s.sources.size(); ++i) {
            const Source &src = s.sources.at(i);
            if (!(src.threats & bit)) continue;
            const double d = collision ? src.collisionDistM : src.sosDistM;
            if (best < 0) { best = i; continue; }
            const double bd = collision ? s.sources.at(best).collisionDistM : s.sources.at(best).sosDistM;
            if (d < bd) best = i;
        }
        if (best >= 0) s.sources[best].closest |= quint8(bit);
    }
    int bestStn = -1;
    for (int i = 0; i < s.stations.size(); ++i)
        if (s.stations.at(i).addEmSos() && (bestStn < 0 || s.stations.at(i).sosDistM < s.stations.at(bestStn).sosDistM)) bestStn = i;
    if (bestStn >= 0) s.stations[bestStn].flags |= 0x04;

    if (s.lp & LpHeadOn)            s.aggThreat = HeadOn;
    else if (s.lp & LpRearEnd)      s.aggThreat = RearEnd;
    else if (s.lp & LpAccessSos)    s.aggThreat = ManualSos;
    else if (s.lp & LpUnusualStop)  s.aggThreat = UnusualStop;
    else if (s.lp & LpTrainParted)  s.aggThreat = TrainParted;
    else if ((s.lp & LpAddEmSos) && s.sosDistM > 0.0) {
        // a station won: the loco flags were cleared, collision_loco_id 0
        s.aggThreat = StationGeneral;
        s.aggStationWon = true;
        s.aggStation = s.sosStation;
    } else {
        s.aggThreat = ThreatNone;
    }
}

bool decodeSnapshotV2(const QByteArray &b, Snapshot *out)
{
    if (b.size() < kSnap2Fixed) return false;
    const int nSrc = u8(b, 35), nStn = u8(b, 36);
    if (b.size() < kSnap2Fixed + kSrc2 * nSrc + kStn2 * nStn) return false;
    Snapshot s;
    s.version = 2;
    s.tickMs = u32(b, 1);
    s.ownLocM = dm(b, 5);
    s.ownDir = u8(b, 9);
    s.ownMode = u8(b, 10);
    s.ownTin = u16(b, 11);
    s.ownSpeed = u16(b, 13) / 100.0;
    s.ownEmergency = u8(b, 15);
    s.collisionLoco = u32(b, 16);
    s.collisionDistM = dm(b, 20);
    s.sosDistM = dm(b, 24);
    s.sosStation = u16(b, 28);
    s.lp = u16(b, 30);
    s.dmi = u16(b, 32);
    s.self = u8(b, 34) & 0x03;
    s.nSourceSlots = nSrc;
    int o = kSnap2Fixed;
    for (int i = 0; i < nSrc; ++i, o += kSrc2) {
        Source src;
        src.slot = i;
        src.locoId = u32(b, o);
        src.ageMs = quint32(u16(b, o + 4)) * 100u;
        src.locM = src.rawLocM = i32(b, o + 6);
        src.dir = src.rawDir = u8(b, o + 10);
        src.tin = u16(b, o + 11);
        src.lengthM = u16(b, o + 13);
        src.sosDistM = dm(b, o + 15);
        src.collisionDistM = dm(b, o + 19);
        src.threats = u8(b, o + 23);
        src.emergency = -1;                       // not logged
        src.distM = std::abs(double(src.locM) - s.ownLocM);
        s.inUseMask |= quint16(1u << i);
        if (src.threats) s.activeMask |= quint16(1u << i);
        s.sources.append(src);
    }
    for (int i = 0; i < nStn; ++i, o += kStn2) {
        Station st;
        st.inUse = true;
        st.id = u16(b, o);
        st.flags = u8(b, o + 2) ? 0x01 : 0x00;
        st.ageMs = quint32(u16(b, o + 3)) * 100u;
        st.absLocM = u32(b, o + 5);
        st.sosDistM = dm(b, o + 9);
        st.distM = std::abs(double(st.absLocM) - s.ownLocM);
        s.stations.append(st);
    }
    deriveV2(s);
    *out = s;
    return true;
}

}  // namespace

bool decodeSnapshot(const QByteArray &b, Snapshot *out)
{
    if (!b.isEmpty() && u8(b, 0) == 2) return decodeSnapshotV2(b, out);
    if (b.size() < kSnapFixed || u8(b, 0) != kVersion) return false;
    const int nStations = u8(b, 64);
    if (b.size() < kSnapFixed + kStation * nStations) return false;
    Snapshot s;
    s.why = u8(b, 1);
    s.snapId = u32(b, 2);
    s.tickMs = u32(b, 6);
    s.ownLocM = dm(b, 10);
    s.ownDir = u8(b, 14);
    s.ownMode = u8(b, 15);
    s.ownTin = u16(b, 16);
    s.ownLengthM = u16(b, 18);
    s.ownSpeed = u16(b, 20) / 100.0;
    s.ownMaM = i32(b, 22);
    s.ownStation = u16(b, 26);
    s.ownTag = u16(b, 28);
    s.rearTagFound = u8(b, 30) != 0;
    s.rearSect = u8(b, 31);
    s.ownEmergency = u8(b, 32);
    s.accessReqStation = u16(b, 33);
    s.nSourceSlots = u8(b, 35);
    s.inUseMask = u16(b, 36);
    s.activeMask = u16(b, 38);
    s.aggThreat = u8(b, 40);
    s.aggStationWon = u8(b, 41) != 0;
    s.collisionLoco = u32(b, 42);
    s.aggStation = u16(b, 46);
    s.collisionDistM = dm(b, 48);
    s.sosDistM = dm(b, 52);
    s.sosStation = u16(b, 56);
    s.lp = u16(b, 58);
    s.dmi = u16(b, 60);
    s.lastDestLocoSos = u8(b, 62);
    s.self = u8(b, 63);
    for (int i = 0; i < nStations; ++i) {
        const int o = kSnapFixed + kStation * i;
        Station st;
        st.inUse = u8(b, o) != 0;
        st.flags = u8(b, o + 1);
        st.id = u16(b, o + 2);
        st.ageMs = u32(b, o + 4);
        st.absLocM = u32(b, o + 8);
        st.distM = dm(b, o + 12);
        st.sosDistM = dm(b, o + 16);
        s.stations.append(st);
    }
    *out = s;
    return true;
}

bool decodeSource(const QByteArray &b, Source *out)
{
    if (b.size() < kSource || u8(b, 0) != kVersion) return false;
    Source s;
    s.snapId = u32(b, 1);
    s.slot = u8(b, 5);
    s.locoId = u32(b, 6);
    s.ageMs = u32(b, 10);
    s.frameNo = u32(b, 14);
    s.emergency = u8(b, 18);
    s.mode = u8(b, 19);
    s.speedRaw = u16(b, 20);
    s.rawLocM = i32(b, 22);
    s.rawDir = u8(b, 26);
    s.lastTag = u16(b, 27);
    s.approachingStation = u16(b, 29);
    s.locM = i32(b, 31);
    s.dir = u8(b, 35);
    s.tin = u16(b, 36);
    s.lengthM = u16(b, 38);
    s.distM = dm(b, 40);
    s.sosDistM = dm(b, 44);
    s.collisionDistM = dm(b, 48);
    s.threats = u8(b, 52);
    s.closest = u8(b, 53);
    s.eval = u16(b, 54);
    *out = s;
    return true;
}

bool decodeEvent(const QByteArray &b, Event *out)
{
    if (b.size() < kEvent || u8(b, 0) != kVersion) return false;
    Event e;
    e.tickMs = u32(b, 1);
    e.snapId = u32(b, 5);
    e.code = u8(b, 9);
    e.aux1 = u8(b, 10);
    e.aux2 = u8(b, 11);
    e.id = u32(b, 12);
    e.distM = dm(b, 16);
    e.ownLocM = dm(b, 20);
    e.ownSpeed = u16(b, 24) / 100.0;
    e.ownMode = u8(b, 26);
    *out = e;
    return true;
}

int Snapshot::expectedSources() const
{
    int n = 0;
    for (quint16 m = inUseMask; m; m &= quint16(m - 1)) ++n;
    return n;
}

const Source *Snapshot::source(quint32 locoId) const
{
    for (const Source &s : sources)
        if (s.locoId == locoId) return &s;
    return nullptr;
}

int Timeline::snapAtOrBefore(qint64 ms) const
{
    int lo = 0, hi = snaps.size();
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (snaps.at(mid).epochMs <= ms) lo = mid + 1; else hi = mid;
    }
    return lo - 1;
}

int Timeline::snapAtOrBeforeRow(int row) const
{
    int lo = 0, hi = snaps.size();
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (snaps.at(mid).row <= row) lo = mid + 1; else hi = mid;
    }
    return lo - 1;
}

// ---- reading a tab ------------------------------------------------------------------

bool hasSos(const LogModel *model)
{
    if (!model) return false;
    const int n = model->count();
    for (int i = 0; i < n; ++i) {
        const LogEntryPtr e = model->entryAt(i);
        if (e && !sosToken(e->text).isEmpty()) return true;
    }
    return false;
}

Timeline extract(const LogModel *model, qint64 fromMs, qint64 toMs)
{
    Timeline t;
    if (!model) return t;
    const int n = model->count();
    int lo = 0, hi = n;
    if (fromMs > 0 && toMs > fromMs) {
        auto firstAtOrAfter = [model, n](qint64 ms) {
            int a = 0, b = n;
            while (a < b) {
                const int mid = (a + b) / 2;
                const LogEntryPtr e = model->entryAt(mid);
                if (e && e->epochMs < ms) a = mid + 1; else b = mid;
            }
            return a;
        };
        lo = firstAtOrAfter(fromMs);
        hi = firstAtOrAfter(toMs + 1);
    }
    for (int i = lo; i < hi; ++i) {
        const LogEntryPtr e = model->entryAt(i);
        if (!e) continue;
        const QString token = sosToken(e->text);
        if (token.isEmpty()) continue;
        const CaptureLine cap = CaptureDecoder::parseLine(e->text);
        if (!cap.valid) continue;
        if (cap.type == CapType::Sos) {
            Snapshot s;
            if (!decodeSnapshot(cap.bytes, &s)) { ++t.otherVersion; continue; }
            s.row = i;
            s.epochMs = e->epochMs;
            if (s.version == 2) s.snapId = quint32(t.snaps.size() + 1);   // the layout has none
            t.version = s.version;
            t.snaps.append(s);
        } else if (cap.type == CapType::SosSrc) {
            Source s;
            if (!decodeSource(cap.bytes, &s)) { ++t.otherVersion; continue; }
            if (!t.snaps.isEmpty() && t.snaps.last().snapId == s.snapId
                && t.snaps.last().sources.size() < t.snaps.last().expectedSources()) {
                t.snaps.last().sources.append(s);
            } else {
                ++t.orphanSources;
            }
        } else if (cap.type == CapType::SosEv) {
            Event ev;
            if (!decodeEvent(cap.bytes, &ev)) { ++t.otherVersion; continue; }
            ev.row = i;
            ev.epochMs = e->epochMs;
            t.events.append(ev);
        }
    }
    // Session 192: the minimal layout logs no events: derive them.
    if (t.events.isEmpty() && t.minimal()) {
        t.events = deriveEvents(t.snaps);
        t.eventsDerived = true;
    }
    return t;
}

QVector<Event> deriveEvents(const QVector<Snapshot> &snaps)
{
    QVector<Event> out;
    QHash<quint32, Source> prev;          // loco id -> its entry in the previous snapshot
    QHash<int, Station> prevStn;
    int prevStatus = -1;
    for (const Snapshot &s : snaps) {
        auto ev = [&](int code, int aux1, int aux2, quint32 id, double distM) {
            Event e;
            e.row = s.row;
            e.epochMs = s.epochMs;
            e.tickMs = s.tickMs;
            e.snapId = s.snapId;
            e.code = code;
            e.aux1 = aux1;
            e.aux2 = aux2;
            e.id = id;
            e.distM = distM;
            e.ownLocM = s.ownLocM;
            e.ownSpeed = s.ownSpeed;
            e.ownMode = s.ownMode;
            out.append(e);
        };
        if (prevStatus >= 0 && s.ownEmergency != prevStatus) ev(EvDerivedOwnStatus, prevStatus, s.ownEmergency, 0, 0.0);
        prevStatus = s.ownEmergency;

        QHash<quint32, Source> now;
        for (const Source &src : s.sources) {
            now.insert(src.locoId, src);
            const auto it = prev.constFind(src.locoId);
            const quint8 was = it == prev.constEnd() ? 0 : it->threats;
            if (it == prev.constEnd()) ev(EvSrcAdded, 0, 0, src.locoId, src.distM);
            for (int bit : { int(BitManual), int(BitUnusual), int(BitHeadOn), int(BitRearEnd), int(BitParted) }) {
                const bool collision = bit == BitHeadOn || bit == BitRearEnd;
                if ((src.threats & bit) && !(was & bit))
                    ev(EvThreatStart, threatOfBit(bit), 0, src.locoId, collision ? src.collisionDistM : src.sosDistM);
                else if (!(src.threats & bit) && (was & bit))
                    ev(EvThreatEnd, threatOfBit(bit), 0, src.locoId, collision ? it->collisionDistM : it->sosDistM);
            }
        }
        for (auto it = prev.constBegin(); it != prev.constEnd(); ++it)
            if (!now.contains(it.key())) ev(EvDerivedSrcLeft, it->threats, 0, it.key(), it->distM);
        prev = now;

        QHash<int, Station> nowStn;
        for (const Station &st : s.stations) {
            nowStn.insert(st.id, st);
            const auto it = prevStn.constFind(st.id);
            const bool was = it != prevStn.constEnd() && it->addEmSos();
            if (it == prevStn.constEnd()) ev(EvStnAdded, 0, 0, quint32(st.id), st.distM);
            if (st.addEmSos() && !was) ev(EvStnSosStart, 0, 0, quint32(st.id), st.sosDistM);
            else if (!st.addEmSos() && was) ev(EvStnSosEnd, 0, 0, quint32(st.id), it->sosDistM);
        }
        for (auto it = prevStn.constBegin(); it != prevStn.constEnd(); ++it)
            if (!nowStn.contains(it.key())) ev(EvDerivedStnLeft, it->addEmSos() ? 1 : 0, 0, quint32(it.key()), it->distM);
        prevStn = nowStn;
    }
    return out;
}

quint32 locoOfKey(const QString &key)
{
    bool ok = false;
    const quint32 v = key.section(QLatin1Char('_'), 0, 0).toUInt(&ok);
    return ok ? v : 0;
}

// ---- words ----------------------------------------------------------------------------

QString threatName(int threat)
{
    switch (threat) {
    case ThreatNone:     return QStringLiteral("None");
    case HeadOn:         return QStringLiteral("Head-on");
    case RearEnd:        return QStringLiteral("Rear-end");
    case UnusualStop:    return QStringLiteral("Unusual stop");
    case ManualSos:      return QStringLiteral("Manual SoS");
    case TrainParted:    return QStringLiteral("Train parted");
    case StationGeneral: return QStringLiteral("Station general SoS");
    case DestGeneral:    return QStringLiteral("Station DEST_LOCO_SOS (general)");
    case StationHeadOn:  return QStringLiteral("Station head-on");
    case StationRearEnd: return QStringLiteral("Station rear-end");
    case Spad:           return QStringLiteral("SPAD");
    case ShuntingLimit:  return QStringLiteral("Shunting limit");
    default:             return QStringLiteral("Threat %1").arg(threat);
    }
}

QString threatBitsText(quint8 bits)
{
    QStringList out;
    for (int bit : { int(BitManual), int(BitUnusual), int(BitHeadOn), int(BitRearEnd), int(BitParted) })
        if (bits & bit) out << lowerFirst(threatName(threatOfBit(bit)));
    return out.isEmpty() ? QStringLiteral("none") : out.join(QStringLiteral(", "));
}

QString dirName(int dir)
{
    switch (dir) {
    case 0:  return QStringLiteral("undefined");
    case 1:  return QStringLiteral("nominal");
    case 2:  return QStringLiteral("reverse");
    default: return QString::number(dir);
    }
}

QString emergencyName(int status)
{
    switch (status) {
    case 0: return QStringLiteral("0 (No Emergency)");
    case 1: return QStringLiteral("1 (Unusual Stoppage)");
    case 2: return QStringLiteral("2 (SoS)");
    case 3: return QStringLiteral("3 (Roll Back)");
    case 4: return QStringLiteral("4 (Head-On Collision)");
    case 5: return QStringLiteral("5 (Rear-End Collision)");
    case 6: return QStringLiteral("6 (Parting SoS)");
    case -1: return QStringLiteral("not logged");
    default: return QString::number(status);
    }
}

QString endReasonText(int reason)
{
    switch (reason) {
    case 1: return QStringLiteral("the source stopped sending it");
    case 2: return QStringLiteral("no longer to be processed (range, direction or adjacency)");
    case 3: return QStringLiteral("the collision condition no longer holds");
    case 4: return QStringLiteral("timeout");
    case 5: return QStringLiteral("station out of range");
    case 6: return QStringLiteral("the station cancelled it");
    case 0: return QStringLiteral("reason not logged");     // session 192: the minimal layout
    default: return QStringLiteral("reason %1").arg(reason);
    }
}

QString eventText(const Event &e)
{
    const QString loco = QStringLiteral("loco %1").arg(e.id);
    const QString stn = QStringLiteral("station %1").arg(e.id);
    const QString d = metres(e.distM);
    switch (e.code) {
    case EvSrcAdded:
        return QStringLiteral("Loco %1 added to the SoS table, %2 away").arg(e.id).arg(d);
    case EvSrcEvicted:
        return QStringLiteral("Loco %1 evicted from the SoS table for a nearer loco%2")
            .arg(e.id).arg(e.aux1 ? QStringLiteral(", with %1 active").arg(threatBitsText(quint8(e.aux1))) : QString());
    case EvSrcDroppedFull:
        return QStringLiteral("Loco %1 not tracked: the table is full and every tracked loco is nearer (%2)").arg(e.id).arg(d);
    case EvSrcQuietReleased:
        return QStringLiteral("Loco %1 released from the SoS table (no threat, quiet for 5 s)").arg(e.id);
    case EvSrcTimeout:
        return QStringLiteral("Loco %1 timed out with %2 active").arg(e.id).arg(threatBitsText(quint8(e.aux1)));
    case EvArpRejected:
        return QStringLiteral("ARP from %1 rejected: %2").arg(loco,
            e.aux1 == 1 ? QStringLiteral("its own loco ID") : QStringLiteral("undefined direction"));
    case EvThreatStart:
        return QStringLiteral("%1 from %2 started, %3").arg(threatName(e.aux1), loco, d);
    case EvThreatEnd:
        if (e.aux2 == 0)   // session 192: derived, the reason is not logged
            return QStringLiteral("%1 from %2 ended (reason not logged), at %3").arg(threatName(e.aux1), loco, d);
        return QStringLiteral("%1 from %2 ended: %3, at %4").arg(threatName(e.aux1), loco, endReasonText(e.aux2), d);
    case EvTargetRemoved:
        return QStringLiteral("Passed %1: SoS target distance removed (was %2)").arg(loco, d);
    case EvBrakeApplied:
        return QStringLiteral("Emergency brake applied: %1 (LB code %2), %3").arg(threatName(e.aux1)).arg(e.id).arg(d);
    case EvBrakeSkipped:
        return QStringLiteral("Brake not applied for %1 from %2: %3, %4").arg(lowerFirst(threatName(e.aux1)),
            (e.aux1 == StationGeneral) ? stn : loco,
            e.aux2 == 1 ? QStringLiteral("standstill") : QStringLiteral("not the closest"), d);
    case EvStnAdded:         return QStringLiteral("Station %1 added to the SoS table, %2 away").arg(e.id).arg(d);
    case EvStnEvicted:       return QStringLiteral("Station %1 evicted for a nearer station").arg(e.id);
    case EvStnDroppedFull:   return QStringLiteral("Station %1 not tracked: the station table is full").arg(e.id);
    case EvStnQuietReleased: return QStringLiteral("Station %1 released (no SoS, quiet for 5 s)").arg(e.id);
    case EvStnSosStart:      return QStringLiteral("Station general SoS from %1 started, %2").arg(stn, d);
    case EvStnSosEnd:
        if (e.aux2 == 0) return QStringLiteral("Station general SoS from %1 ended (reason not logged)").arg(stn);
        return QStringLiteral("Station general SoS from %1 ended: %2").arg(stn, endReasonText(e.aux2));
    case EvDerivedSrcLeft:
        return QStringLiteral("Loco %1 left the SoS table%2").arg(e.id)
            .arg(e.aux1 ? QStringLiteral(" with %1 on").arg(threatBitsText(quint8(e.aux1))) : QString());
    case EvDerivedStnLeft:   return QStringLiteral("Station %1 left the SoS table").arg(e.id);
    case EvDerivedOwnStatus:
        return QStringLiteral("Own ARP status %1 \u2192 %2").arg(emergencyName(e.aux1), emergencyName(e.aux2));
    case EvDestLocoSos:
        return e.aux1 ? QStringLiteral("DEST_LOCO_SOS from %1: %2").arg(stn, sosTypeName(e.aux1))
                      : QStringLiteral("DEST_LOCO_SOS from %1 back to 0").arg(stn);
    case EvTableReset:
        return QStringLiteral("SoS table reset: %1").arg(
            e.aux1 == 2 ? QStringLiteral("Non-Leading mode") :
            e.aux1 == 3 ? QStringLiteral("Isolation mode") : QStringLiteral("direction undefined"));
    case EvSelfUnusualAlarm: return QStringLiteral("Own unusual-stoppage alarm raised (stopped in the block section)");
    case EvSelfUnusualEnd:   return QStringLiteral("Own unusual-stoppage alarm ended");
    case EvSelfUnusualAck:   return QStringLiteral("Own unusual-stoppage alarm acknowledged");
    default:                 return QStringLiteral("SoS event %1 (%2, %3, %4)").arg(e.code).arg(e.aux1).arg(e.aux2).arg(e.id);
    }
}

QStringList decisionLines(const Snapshot &s)
{
    QStringList out;
    if (s.aggThreat == ThreatNone) {
        out << QStringLiteral("No SoS or collision target.");
    } else if (s.aggStationWon) {
        out << QStringLiteral("Target: station %1, %2, %3.").arg(s.aggStation).arg(lowerFirst(threatName(s.aggThreat)), metres(s.sosDistM));
    } else {
        const bool collision = s.aggThreat == HeadOn || s.aggThreat == RearEnd;
        const double d = collision ? s.collisionDistM : s.sosDistM;
        QString line = QStringLiteral("Target: loco %1, %2, %3").arg(s.collisionLoco).arg(lowerFirst(threatName(s.aggThreat)), metres(d));
        if (!collision && d == 0.0) line += QStringLiteral(" (0 m: target distance removed or source passed)");
        out << line + QLatin1Char('.');
    }

    out << QStringLiteral("Own ARP broadcasts emergency status %1.").arg(emergencyName(s.ownEmergency));

    QStringList limit;
    if (s.lp & LpAccessSos)   limit << QStringLiteral("other loco manual SoS");
    if (s.lp & LpUnusualStop) limit << QStringLiteral("other loco unusual stop");
    if (s.lp & LpAddEmSos)    limit << QStringLiteral("station general SoS");
    if (s.lp & LpDestLocoSos) limit << QStringLiteral("station DEST_LOCO_SOS");
    out << (limit.isEmpty() ? QStringLiteral("No flag that sets the SoS speed limit.")
                            : QStringLiteral("SoS speed-limit flags set: %1.").arg(limit.join(QStringLiteral(", "))));

    QStringList other;
    if (s.lp & LpTrainParted) other << QStringLiteral("train parted (no speed limit from it in v1.2.9)");
    if (s.lp & LpHeadOn)      other << QStringLiteral("head-on");
    if (s.lp & LpRearEnd)     other << QStringLiteral("rear-end");
    if (s.lp & LpStnHeadOn)   other << QStringLiteral("station head-on");
    if (s.lp & LpStnRearEnd)  other << QStringLiteral("station rear-end");
    if (s.lp & LpStnShunting) other << QStringLiteral("station shunting limit");
    if (!other.isEmpty()) out << QStringLiteral("Also set: %1.").arg(other.join(QStringLiteral(", ")));

    if (s.lastDestLocoSos)
        out << QStringLiteral("Last DEST_LOCO_SOS seen: %1.").arg(sosTypeName(s.lastDestLocoSos));

    static const char *const kDmi[] = {
        "SOS - Self Loco Manual", "SOS - Self Loco Stopped in Block Section", "SOS - Self Loco Train Parted",
        "SOS - Other Loco Manual", "SOS - Other Loco Stopped in Block Section", "SOS - Other Loco Train Parted",
        "SOS - Station (All Locos)", "SOS - Station (This Loco)", "Head-on collision with loco",
        "Rear-end collision with loco", "Brake applied - station general SoS", "Ack block stop, SoS generates in XX s" };
    QStringList dmi;
    for (int i = 0; i < 12; ++i)
        if (s.dmi & (1 << i)) dmi << QLatin1String(kDmi[i]);
    out << (dmi.isEmpty() ? QStringLiteral("DMI: no SoS message.") : QStringLiteral("DMI: %1.").arg(dmi.join(QStringLiteral("; "))));

    // The flags and the DMI bits are set together by SOS_RecomputeAggregates;
    // say so when they are not.
    struct Pair { quint16 lp; int dmiBit; const char *name; };
    static const Pair kPairs[] = {
        { LpAccessSos, 3, "other loco manual SoS" }, { LpUnusualStop, 4, "other loco unusual stop" },
        { LpTrainParted, 5, "other loco train parted" }, { LpAddEmSos, 6, "station (all locos)" },
        { LpHeadOn, 8, "head-on" }, { LpRearEnd, 9, "rear-end" } };
    for (const Pair &p : kPairs) {
        const bool flag = s.lp & p.lp, shown = s.dmi & (1 << p.dmiBit);
        if (flag != shown)
            out << QStringLiteral("Observed: the %1 flag is %2 but the DMI bit is %3.")
                       .arg(QLatin1String(p.name), flag ? QStringLiteral("set") : QStringLiteral("clear"),
                            shown ? QStringLiteral("set") : QStringLiteral("clear"));
    }

    if (s.self & 0x01) out << QStringLiteral("Own unusual-stoppage alarm raised.");
    if (s.self & 0x02) out << QStringLiteral("Own unusual-stoppage alarm acknowledged.");
    return out;
}

QVector<Check> checks(const Source &src)
{
    QVector<Check> out;
    out << Check{ QStringLiteral("Same TIN"), src.check(EvalSameTin), false,
                  QStringLiteral("TIN %1").arg(src.tin) };
    out << Check{ QStringLiteral("In SoS range"), src.check(EvalWithinTrigger), false,
                  QStringLiteral("Gap below sos_trigger_distance") };
    out << Check{ QStringLiteral("In collision range"), src.check(EvalWithinCollision), false,
                  QStringLiteral("Gap below collision_trigger_distance") };
    QString adjFrom = src.check(EvalAdjFromTag) ? QStringLiteral("from an RFID tag") : QString();
    if (src.check(EvalAdjFromProfile))
        adjFrom += (adjFrom.isEmpty() ? QString() : QStringLiteral(" and ")) + QStringLiteral("from the profile");
    if (src.check(EvalAdjFailOpen)) adjFrom = QStringLiteral("no adjacency information: the check fails open (treated as adjacent)");
    out << Check{ QStringLiteral("Adjacent line"), src.check(EvalAdjacent), src.check(EvalAdjFailOpen),
                  QStringLiteral("IsAdjacentLineInfringing; %1").arg(adjFrom) };
    out << Check{ QStringLiteral("Adjusted"), src.check(EvalAdjusted), false,
                  QStringLiteral("ARP said %1 m %2; after SOSWithAdjustment %3 m %4")
                      .arg(src.rawLocM).arg(dirName(src.rawDir)).arg(src.locM).arg(dirName(src.dir)) };
    const bool fw = src.check(EvalStationOkFirmware), slot = src.check(EvalStationOkSlot);
    out << Check{ QStringLiteral("Station check"), fw, fw != slot,
                  fw != slot ? QStringLiteral("The firmware's answer (one global station id) differs from this loco's own "
                                              "approaching station %1 (bug #5 in 02_SOS_POSSIBLE_BUGS)").arg(src.approachingStation)
                             : QStringLiteral("Collision allowed by the station-section check") };
    out << Check{ QStringLiteral("Manual would process"), src.check(EvalManualWouldProcess), false,
                  QStringLiteral("IsManualSOSToBeProcessed, re-run at the snapshot") };
    out << Check{ QStringLiteral("Unusual would process"), src.check(EvalUnusualWouldProcess), false,
                  QStringLiteral("IsUnusualStopOfOtherLocoToBeProcessed, re-run at the snapshot") };
    out << Check{ QStringLiteral("Passed"), src.check(EvalTargetRemoved), false,
                  QStringLiteral("IsSOSTargetDistanceRemove: own loco is past the source") };
    return out;
}

// ---- spells ----------------------------------------------------------------------------

QString Spell::what() const
{
    return QStringLiteral("%1 from %2 %3").arg(threatName(threat),
        station ? QStringLiteral("station") : QStringLiteral("loco")).arg(source);
}

QVector<Spell> spells(const Timeline &t)
{
    QVector<Spell> out;
    // (threat, source, station) -> index into out of the open spell
    QMap<QString, int> open;
    auto key = [](int threat, quint32 src, bool station) {
        return QStringLiteral("%1/%2/%3").arg(threat).arg(src).arg(station ? 1 : 0);
    };
    auto start = [&](const Event &e, int threat, quint32 src, bool station) {
        const QString k = key(threat, src, station);
        if (open.contains(k)) return;
        Spell s;
        s.fromMs = s.toMs = e.epochMs;
        s.row = e.row;
        s.threat = threat;
        s.source = src;
        s.station = station;
        s.startDistM = e.distM;
        s.open = true;
        open.insert(k, out.size());
        out.append(s);
    };
    auto finish = [&](const QString &k, qint64 ms, double distM, int reason, const QString &by) {
        const auto it = open.find(k);
        if (it == open.end()) return;
        Spell &s = out[it.value()];
        s.toMs = ms;
        s.endDistM = distM;
        s.endReason = reason;
        s.endedBy = by;
        s.open = false;
        open.erase(it);
    };
    auto finishSource = [&](quint32 src, quint8 mask, qint64 ms, const QString &by) {
        for (int bit : { int(BitManual), int(BitUnusual), int(BitHeadOn), int(BitRearEnd), int(BitParted) })
            if (mask & bit) finish(key(threatOfBit(bit), src, false), ms, 0.0, 0, by);
    };

    // Events and the DEST_LOCO_SOS flag (from the snapshots) in time order.
    int si = 0;
    bool destOn = false;
    int destStation = 0;
    auto snapsUpTo = [&](qint64 ms) {
        for (; si < t.snaps.size() && t.snaps.at(si).epochMs <= ms; ++si) {
            const Snapshot &s = t.snaps.at(si);
            const bool on = s.lp & LpDestLocoSos;
            if (on && !destOn) {
                Event e; e.epochMs = s.epochMs; e.row = s.row;
                destStation = s.ownStation;
                start(e, DestGeneral, quint32(destStation), true);
            } else if (!on && destOn) {
                finish(key(DestGeneral, quint32(destStation), true), s.epochMs, 0.0, 0,
                       QStringLiteral("is_dest_loco_sos_recvd cleared"));
            }
            destOn = on;
        }
    };

    for (const Event &e : t.events) {
        snapsUpTo(e.epochMs);
        switch (e.code) {
        case EvThreatStart: start(e, e.aux1, e.id, false); break;
        case EvThreatEnd:
            finish(key(e.aux1, e.id, false), e.epochMs, e.distM, e.aux2, endReasonText(e.aux2));
            break;
        case EvSrcTimeout:
            finishSource(e.id, quint8(e.aux1), e.epochMs, QStringLiteral("timeout"));
            break;
        case EvSrcEvicted:
            finishSource(e.id, 0x1F, e.epochMs, QStringLiteral("evicted from the table"));
            break;
        case EvDerivedSrcLeft:
            finishSource(e.id, quint8(e.aux1), e.epochMs, QStringLiteral("left the SoS table"));
            break;
        case EvDerivedStnLeft:
            finish(key(StationGeneral, e.id, true), e.epochMs, e.distM, 0, QStringLiteral("left the SoS table"));
            break;
        case EvStnSosStart: start(e, StationGeneral, e.id, true); break;
        case EvStnSosEnd:
            finish(key(StationGeneral, e.id, true), e.epochMs, e.distM, e.aux2, endReasonText(e.aux2));
            break;
        case EvTableReset: {
            const QStringList keys = open.keys();
            for (const QString &k : keys) {
                if (k.startsWith(QStringLiteral("%1/").arg(DestGeneral))) continue;   // the flag, from snapshots
                finish(k, e.epochMs, 0.0, 0, QStringLiteral("table reset"));
            }
            break;
        }
        default: break;
        }
    }
    snapsUpTo(std::numeric_limits<qint64>::max());

    qint64 lastMs = 0;
    if (!t.snaps.isEmpty()) lastMs = t.snaps.last().epochMs;
    if (!t.events.isEmpty()) lastMs = qMax(lastMs, t.events.last().epochMs);
    for (Spell &s : out) {
        if (!s.open) continue;
        s.toMs = qMax(s.fromMs, lastMs);
        s.endedBy = QStringLiteral("(no end)");
    }
    std::stable_sort(out.begin(), out.end(), [](const Spell &a, const Spell &b) { return a.fromMs < b.fromMs; });
    return out;
}

QVector<Event> brakeDecisions(const Timeline &t)
{
    QVector<Event> out;
    for (const Event &e : t.events)
        if (e.code == EvBrakeApplied || e.code == EvBrakeSkipped) out.append(e);
    return out;
}

QVector<RunReport::Episode> spellEpisodes(const Timeline &t)
{
    QVector<RunReport::Episode> out;
    for (const Spell &s : spells(t)) {
        RunReport::Episode e;
        e.fromMs = s.fromMs;
        e.toMs = s.toMs;
        e.row = s.row;
        e.worst = s.startDistM;
        e.what = s.what() + QStringLiteral(" — ") + (s.open ? QStringLiteral("(no end)")
                                                               : QStringLiteral("ended: ") + s.endedBy);
        out.append(e);
    }
    return out;
}

QVector<RunReport::Change> brakeChanges(const Timeline &t)
{
    QVector<RunReport::Change> out;
    for (const Event &e : brakeDecisions(t)) {
        RunReport::Change c;
        c.ms = e.epochMs;
        c.row = e.row;
        c.from = e.code == EvBrakeApplied ? QStringLiteral("applied") : QStringLiteral("not applied");
        c.to = eventText(e);
        out.append(c);
    }
    return out;
}

// ---- two logs ------------------------------------------------------------------------------

QVector<Relay> relay(const Timeline &sender, const Timeline &receiver, quint32 senderId)
{
    QVector<Relay> out;
    if (sender.snaps.isEmpty()) return out;
    int prev = sender.snaps.first().ownEmergency;
    for (const Snapshot &s : sender.snaps) {
        if (s.ownEmergency == prev) continue;
        Relay r;
        r.sentMs = s.epochMs;
        r.sentRow = s.row;
        r.fromStatus = prev;
        r.toStatus = s.ownEmergency;
        prev = s.ownEmergency;

        // The receiver's clock is its own RTC: look a little before too.
        // With the minimal layout "heard" is a threat appearing or clearing, and
        // the receiver may have cleared one before for its own reasons: from the send.
        const qint64 from = receiver.minimal() ? r.sentMs : r.sentMs - 5000, to = r.sentMs + kRelayWindowMs;
        for (const Snapshot &rs : receiver.snaps) {
            if (rs.epochMs < from) continue;
            if (rs.epochMs > to) break;
            const Source *src = rs.source(senderId);
            if (!src) continue;
            r.tracked = true;
            bool heard = src->emergency == r.toStatus;
            if (rs.version == 2) {
                // No ARP status in the minimal layout: the threat it makes.
                r.byThreat = true;
                const quint8 sosBits = src->threats & (BitManual | BitUnusual | BitParted);
                heard = (r.toStatus == 1 && (sosBits & BitUnusual)) || (r.toStatus == 2 && (sosBits & BitManual))
                     || (r.toStatus == 6 && (sosBits & BitParted)) || (r.toStatus == 0 && sosBits == 0);
            }
            if (heard) {
                r.heardMs = rs.epochMs;
                r.heardRow = rs.row;
                break;
            }
        }
        const qint64 actFrom = r.heardMs >= 0 ? r.heardMs : r.sentMs;
        for (const Event &e : receiver.events) {
            if (e.epochMs < actFrom) continue;
            if (e.epochMs > actFrom + kRelayWindowMs) break;
            const bool aboutSender = e.id == senderId
                && (e.code == EvThreatStart || e.code == EvThreatEnd || e.code == EvTargetRemoved
                    || e.code == EvBrakeSkipped || e.code == EvSrcTimeout || e.code == EvSrcEvicted
                    || e.code == EvDerivedSrcLeft);
            if (!aboutSender) continue;
            r.actedMs = e.epochMs;
            r.actedRow = e.row;
            r.acted = eventText(e);
            break;
        }
        out.append(r);
    }
    return out;
}

}  // namespace SosLog
