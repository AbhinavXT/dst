#include "sessionkeystore.h"

#include "crypto/kavachmac.h"
#include "schema/schemadecoder.h"

namespace {

// Reversed 4-byte KEY_TIME -> QDateTime (see keyTime() in capturedecoder:
// on the wire the word is [hh,dd,mm,yy], yy is years-since-2000).
QDateTime keyTime(const QByteArray &b, int off)
{
    if (off + 4 > b.size()) { return QDateTime(); }
    const auto u = reinterpret_cast<const quint8 *>(b.constData());
    const int yy = u[off + 3], mm = u[off + 2], dd = u[off + 1], hh = u[off + 0];
    const QDate d(2000 + yy, mm, dd);
    if (!d.isValid() || hh > 23) { return QDateTime(); }
    return QDateTime(d, QTime(hh, 0));
}

// Which key set a tag refers to: the trailing digit of the type token
// (@auth_keys1 -> 0, @auth_keys2 / @auth_key2 -> 1). Returns -1 if absent.
int setIndexFromToken(const QString &tok)
{
    for (int i = tok.size() - 1; i >= 0; --i) {
        if (tok[i].isDigit()) {
            const int d = tok[i].digitValue();
            if (d == 1) { return 0; }
            if (d == 2) { return 1; }
            return -1;
        }
    }
    return -1;
}

// msb-first bit field, the wire order of every Annexure-C packet.
quint32 bitsMsb(const QByteArray &b, int bitOff, int nbits)
{
    quint32 v = 0;
    for (int i = bitOff; i < bitOff + nbits; ++i) {
        const int byte = i >> 3;
        const int bit  = 7 - (i & 7);
        v = (v << 1) | ((quint8(b[byte]) >> bit) & 1u);
    }
    return v;
}

// An empty state to hand back for queries about a loco we have never seen,
// so every accessor can return a reference without a null check at each call.
const SessionKeyGen::KeySet &emptyKeySet()
{
    static const SessionKeyGen::KeySet k;
    return k;
}
const SessionKeyGen::Result &emptyResult()
{
    static const SessionKeyGen::Result r;
    return r;
}

}  // namespace

QString KeySnapshot::label() const
{
    QString s = QStringLiteral("#%1  loco %2").arg(id).arg(locoId);
    if (stnId >= 0) { s += QStringLiteral(" · stn %1").arg(stnId); }
    s += QStringLiteral(" · rnd %1/%2")
             .arg(locoRandom, 4, 16, QChar('0'))
             .arg(stnRandom,  4, 16, QChar('0'));
    if (result.ok) {
        s += QStringLiteral(" · set %1 key %2")
                 .arg(result.setIndex + 1).arg(result.keyIndex + 1);
    }
    if (firstSeen.isValid()) {
        s += QStringLiteral("  (%1)").arg(firstSeen.toString(QStringLiteral("MM-dd HH:mm:ss")));
    }
    if (!changedBy.isEmpty()) { s += QStringLiteral("  [%1]").arg(changedBy); }
    return s;
}

SessionKeyStore &SessionKeyStore::instance()
{
    static SessionKeyStore s;
    return s;
}

bool SessionKeyStore::typeHasMac(const QString &captype)
{
    // SLRP is the confirmed MAC-bearing packet (reserve_tail = MAC(4)+CRC(4)).
    // Add others here once their MAC layout is verified.
    return captype.compare(QLatin1String("slrp"), Qt::CaseInsensitive) == 0;
}

int SessionKeyStore::stationIdFromFrame(CapType type, const QByteArray &frame)
{
    // Both packets are msb-first from byte 0 (a captured @slrp/@aap line has
    // no message header). SOURCE_STN_ID sits after PKT_TYPE + PKT_LENGTH +
    // FRAME_NUM, and PKT_LENGTH is the one field whose width differs:
    //   SLRP: 4 + 10 + 17 = bit 31,  16 bits
    //   AAP:  4 +  7 + 17 = bit 28,  16 bits
    int off = -1;
    if      (type == CapType::SLRP) { off = 31; }
    else if (type == CapType::AAP)  { off = 28; }
    else                            { return -1; }

    if (frame.size() * 8 < off + 16) { return -1; }
    return int(bitsMsb(frame, off, 16));
}

SessionKeyStore::LocoState &SessionKeyStore::stateFor(int locoId)
{
    if (!m_states.contains(locoId)) {
        m_states.insert(locoId, LocoState());
        m_order.append(locoId);
    }
    m_active = locoId;
    return m_states[locoId];
}

const SessionKeyStore::LocoState *SessionKeyStore::find(int locoId) const
{
    auto it = m_states.constFind(locoId);
    return it == m_states.constEnd() ? nullptr : &it.value();
}

const SessionKeyStore::LocoState *SessionKeyStore::resolve(int locoId) const
{
    return find(locoId < 0 ? m_active : locoId);
}

void SessionKeyStore::setIds(int locoId, int stnId)
{
    LocoState &st = stateFor(locoId);
    st.locoId = locoId;
    st.stnId  = stnId;
    st.idsManual = true;
    rederive(locoId, QStringLiteral("ids set by operator"));
}

void SessionKeyStore::observe(const LogEntryPtr &entry)
{
    if (entry.isNull()) { return; }
    const QString &t = entry->text;
    // Cheap gate: auth-key and random lines, plus station packets, which are
    // where the real station id comes from. Station packets are parsed on
    // every frame now rather than only until the first one is seen -- the
    // station id follows last-write-wins like everything else, and a loco
    // that hands over from station 501 to 527 has to be able to say so.
    // Only parseLine + one bit read runs per station frame; the schema
    // decode this used to do is gone (see stationIdFromFrame).
    const bool isStn = t.startsWith(QLatin1String("@slrp"), Qt::CaseInsensitive)
                    || t.startsWith(QLatin1String("@aap"),  Qt::CaseInsensitive);
    if (!(t.startsWith(QLatin1String("@auth"), Qt::CaseInsensitive) ||
          t.startsWith(QLatin1String("@rand"), Qt::CaseInsensitive) || isStn)) {
        return;
    }
    observeLine(CaptureDecoder::parseLine(t));
}

void SessionKeyStore::observeLine(const CaptureLine &c)
{
    if (!c.valid) { return; }
    const QByteArray &b = c.bytes;
    // Every path below stamps the state with this frame's RTC before
    // re-deriving, so the window rule is evaluated at the time the traffic
    // actually happened.
    const QDateTime stamp = c.rtc;
    // The tag's loco id is the key. A capture whose tags carry none files
    // under -1, which keeps single-source logs working exactly as before.
    const int loco = c.locoId;

    if (c.type == CapType::AuthKeys) {
        const int si = setIndexFromToken(c.typeToken);
        if (si < 0 || b.size() < 40) { return; }
        // One set per frame, no count byte: start(4) end(4) key0(16) key1(16).
        SessionKeyGen::KeySet ks;
        ks.start = keyTime(b, 0);
        ks.end   = keyTime(b, 4);
        ks.key0  = b.mid(8, 16);
        ks.key1  = b.mid(24, 16);
        if (ks.keysOk()) {
            LocoState &st = stateFor(loco);
            st.set[si] = ks;                       // last write wins
            if (stamp.isValid()) { st.lastSeen = stamp; }
            rederive(loco, QStringLiteral("key set %1").arg(si + 1));
        }
        return;
    }

    if (c.type == CapType::Random) {
        if (b.size() < 4) { return; }
        const auto u = reinterpret_cast<const quint8 *>(b.constData());
        LocoState &st = stateFor(loco);
        st.locoRandom = quint16(u[0] | (u[1] << 8));   // little-endian
        st.stnRandom  = quint16(u[2] | (u[3] << 8));
        st.haveRandoms = true;
        if (stamp.isValid()) { st.lastSeen = stamp; }
        if (!st.idsManual) {
            if (c.locoId >= 0) { st.locoId = c.locoId; }   // loco id from the tag
            // Station id: the tag's ctrl id is a placeholder only. Once a
            // station packet has given a real SOURCE_STN_ID it stands, or a
            // later @rand_num would drop the id back to the ctrl index (1)
            // and flip the parity.
            if (!st.stnFromStation && c.ctrlId >= 0) { st.stnId = c.ctrlId; }
        }
        rederive(loco, QStringLiteral("randoms"));
        return;
    }

    // Station packets carry the real station id in SOURCE_STN_ID. Last one
    // wins: a loco that moves from station 501 to station 527 must follow it,
    // because the parity (loco_id + stn_id) picks which key of the set is used.
    if (c.type == CapType::SLRP || c.type == CapType::AAP) {
        const int sid = stationIdFromFrame(c.type, b);
        if (sid < 0) { return; }
        LocoState &st = stateFor(loco);
        if (st.idsManual) { return; }
        if (st.locoId < 0 && c.locoId >= 0) { st.locoId = c.locoId; }
        const bool changed = (st.stnId != sid) || !st.stnFromStation;
        st.stnId = sid;
        if (stamp.isValid()) { st.lastSeen = stamp; }
        st.stnFromStation = true;
        if (changed) { rederive(loco, QStringLiteral("stn id")); }
        return;
    }
}

void SessionKeyStore::rederive(int locoId, const QString &changedBy)
{
    LocoState &st = stateFor(locoId);
    const bool ready = st.set[0].keysOk() && st.set[1].keysOk() && st.haveRandoms
                    && st.locoId >= 0 && st.stnId >= 0;
    if (!ready) {
        if (st.result.ok) { st.result = SessionKeyGen::Result(); }
        emit changed();                      // still notify: partial progress
        return;
    }
    // The window rule asks whether set 1 was valid AT THE TIME OF THE TRAFFIC.
    // Live those are the same thing; replaying a month-old .cap they are not,
    // and the wall clock silently fell outside set 1's window on every frame,
    // which sends every MAC check to set 2's key. Fall back to now only when
    // the stream carried no usable RTC.
    const QDateTime when = st.lastSeen.isValid() ? st.lastSeen
                                                 : QDateTime::currentDateTime();
    st.result = SessionKeyGen::derive(st.set[0], st.set[1], st.locoRandom, st.stnRandom,
                                      st.locoId, st.stnId, when);
    captureSnapshot(locoId, changedBy);
    emit changed();
}

void SessionKeyStore::captureSnapshot(int locoId, const QString &changedBy)
{
    LocoState &st = stateFor(locoId);
    if (!st.result.ok) { return; }        // only complete, usable combinations

    auto sameSet = [](const SessionKeyGen::KeySet &a, const SessionKeyGen::KeySet &b) {
        return a.start == b.start && a.end == b.end
            && a.key0  == b.key0  && a.key1 == b.key1;
    };
    auto matches = [&](const KeySnapshot &k) {
        return k.locoId == st.locoId && k.stnId == st.stnId
            && k.locoRandom == st.locoRandom && k.stnRandom == st.stnRandom
            && sameSet(k.set[0], st.set[0]) && sameSet(k.set[1], st.set[1]);
    };

    // A combination that recurs is the SAME set, not a new one. A loco moving
    // 527 -> 501 -> 527 would otherwise pile up a snapshot per handover and
    // bury the picker in duplicates.
    for (KeySnapshot &k : m_snaps) {
        if (matches(k)) {
            if (st.lastSeen.isValid()) { k.lastSeen = st.lastSeen; }
            m_currentSnap[locoId] = k.id;
            return;
        }
    }

    KeySnapshot k;
    k.id         = m_nextSnapId++;
    k.locoId     = st.locoId;
    k.stnId      = st.stnId;
    k.set[0]     = st.set[0];
    k.set[1]     = st.set[1];
    k.locoRandom = st.locoRandom;
    k.stnRandom  = st.stnRandom;
    k.firstSeen  = st.lastSeen;
    k.lastSeen   = st.lastSeen;
    k.changedBy  = changedBy;
    k.result     = st.result;
    m_snaps.push_back(k);
    m_currentSnap[locoId] = k.id;

    // Bounded: a long shift with a chatty link should not grow this without
    // limit. The oldest go first; ids are never reused, so a stale id in a
    // picker resolves to nothing rather than to the wrong key.
    const int kMaxSnaps = 128;
    while (m_snaps.size() > kMaxSnaps) { m_snaps.removeFirst(); }
}

const KeySnapshot *SessionKeyStore::snapshot(int id) const
{
    for (const KeySnapshot &k : m_snaps) {
        if (k.id == id) { return &k; }
    }
    return nullptr;
}

int SessionKeyStore::currentSnapshotId(int locoId) const
{
    const int loco = (locoId < 0) ? m_active : locoId;
    return m_currentSnap.value(loco, 0);
}

SessionKeyStore::Mac SessionKeyStore::verifyMacWith(const QString &captype,
                                                    const QByteArray &frame,
                                                    int snapshotId) const
{
    if (!typeHasMac(captype)) { return Mac::None; }
    const KeySnapshot *k = snapshot(snapshotId);
    if (!k || !k->result.ok) { return Mac::NoKey; }
    if (frame.size() < 8)    { return Mac::Fail; }

    const QByteArray body    = frame.left(frame.size() - 8);
    const QByteArray wireMac = frame.mid(frame.size() - 8, 4);
    return KavachMac::verify(body, k->result.sessionKey, wireMac) ? Mac::Pass : Mac::Fail;
}

// ---- per-loco queries ------------------------------------------------------

bool SessionKeyStore::haveSet(int i, int locoId) const
{
    const LocoState *st = resolve(locoId);
    return st && i >= 0 && i < 2 && st->set[i].keysOk();
}

bool SessionKeyStore::haveRandoms(int locoId) const
{
    const LocoState *st = resolve(locoId);
    return st && st->haveRandoms;
}

quint16 SessionKeyStore::locoRandom(int locoId) const
{
    const LocoState *st = resolve(locoId);
    return st ? st->locoRandom : quint16(0);
}

quint16 SessionKeyStore::stnRandom(int locoId) const
{
    const LocoState *st = resolve(locoId);
    return st ? st->stnRandom : quint16(0);
}

const SessionKeyGen::KeySet &SessionKeyStore::keySet(int i, int locoId) const
{
    const LocoState *st = resolve(locoId);
    if (!st || i < 0 || i > 1) { return emptyKeySet(); }
    return st->set[i];
}

int SessionKeyStore::locoId(int locoId) const
{
    const LocoState *st = resolve(locoId);
    return st ? st->locoId : -1;
}

int SessionKeyStore::stnId(int locoId) const
{
    const LocoState *st = resolve(locoId);
    return st ? st->stnId : -1;
}

QByteArray SessionKeyStore::sessionKey(int locoId) const
{
    const LocoState *st = resolve(locoId);
    return st ? st->result.sessionKey : QByteArray();
}

bool SessionKeyStore::haveSessionKey(int locoId) const
{
    const LocoState *st = resolve(locoId);
    return st && st->result.ok;
}

const SessionKeyGen::Result &SessionKeyStore::selection(int locoId) const
{
    const LocoState *st = resolve(locoId);
    return st ? st->result : emptyResult();
}

SessionKeyStore::Mac SessionKeyStore::verifyMac(const QString &captype,
                                                const QByteArray &frame,
                                                int locoId) const
{
    if (!typeHasMac(captype)) { return Mac::None; }
    const LocoState *st = resolve(locoId);
    if (!st || !st->result.ok) { return Mac::NoKey; }
    if (frame.size() < 8)      { return Mac::Fail; }

    // Tail is [MAC_CODE 4][CRC 4]; MAC covers the body before it, wire form.
    const QByteArray body    = frame.left(frame.size() - 8);
    const QByteArray wireMac = frame.mid(frame.size() - 8, 4);
    return KavachMac::verify(body, st->result.sessionKey, wireMac) ? Mac::Pass : Mac::Fail;
}

QString SessionKeyStore::status(int locoId) const
{
    const LocoState *st = resolve(locoId);
    if (!st) { return QStringLiteral("waiting…"); }
    if (st->result.ok) {
        return QStringLiteral("session key ready — set %1, key %2")
                   .arg(st->result.setIndex + 1).arg(st->result.keyIndex + 1);
    }
    QStringList missing;
    if (!st->set[0].keysOk()) { missing << QStringLiteral("key set 1"); }
    if (!st->set[1].keysOk()) { missing << QStringLiteral("key set 2"); }
    if (!st->haveRandoms)     { missing << QStringLiteral("randoms"); }
    if (st->locoId < 0 || st->stnId < 0) { missing << QStringLiteral("ids"); }
    return missing.isEmpty() ? QStringLiteral("waiting…")
                             : QStringLiteral("waiting for: %1").arg(missing.join(", "));
}

QString SessionKeyStore::statusAll() const
{
    if (m_order.isEmpty()) { return QStringLiteral("waiting…"); }
    if (m_order.size() == 1) { return status(m_order.first()); }
    QStringList parts;
    for (int loco : m_order) {
        const QString who = (loco < 0) ? QStringLiteral("loco ?")
                                       : QStringLiteral("loco %1").arg(loco);
        QString line = status(loco);
        if (const LocoState *st = find(loco)) {
            if (st->stnId >= 0) { line += QStringLiteral(" (stn %1)").arg(st->stnId); }
        }
        parts << QStringLiteral("%1: %2").arg(who, line);
    }
    return parts.join(QStringLiteral("  |  "));
}

QDateTime SessionKeyStore::logTime(int locoId) const
{
    const LocoState *st = resolve(locoId);
    return st ? st->lastSeen : QDateTime();
}

void SessionKeyStore::clear()
{
    m_states.clear();
    m_order.clear();
    m_snaps.clear();
    m_currentSnap.clear();
    m_nextSnapId = 1;
    m_active = -1;
    emit changed();
}
