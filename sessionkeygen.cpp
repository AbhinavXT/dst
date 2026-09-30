#include "sessionkeygen.h"

#include "crypto/kavachmac.h"

namespace SessionKeyGen {

Result derive(const KeySet &set0, const KeySet &set1,
              quint16 locoRandom, quint16 stnRandom,
              int locoId, int stnId, const QDateTime &now)
{
    Result r;

    if (!set0.keysOk() || !set1.keysOk()) {
        r.error = QStringLiteral("each key set needs two 16-byte keys");
        return r;
    }

    // Rule 1 — set by time window: set 1 if `now` is inside its [start,end],
    // otherwise set 2.
    r.set1Active = set0.contains(now);
    r.setIndex   = r.set1Active ? 0 : 1;
    const KeySet &chosenSet = r.set1Active ? set0 : set1;

    // Informational: warn if `now` isn't inside either window (we still fall
    // back to set 2 per the rule, but the operator should know).
    r.neitherWindowValid = !set0.contains(now) && !set1.contains(now);

    // Rule 2 — key by id parity: even sum -> key 1, odd -> key 2.
    const bool even = (((locoId + stnId) % 2) == 0);
    r.keyIndex = even ? 0 : 1;
    const QByteArray &chosenKey = even ? chosenSet.key0 : chosenSet.key1;

    r.sessionKey = KavachMac::sessionKey(locoRandom, stnRandom, chosenKey);
    if (r.sessionKey.size() != 16) {
        r.error = QStringLiteral("session-key derivation failed");
        return r;
    }

    r.explanation =
        QStringLiteral("now %1 %2 set 1's window → set %3;  loco_id(%4)+stn_id(%5)=%6 (%7) → key %8")
            .arg(now.toString(QStringLiteral("yyyy-MM-dd HH:mm")))
            .arg(r.set1Active ? QStringLiteral("is inside") : QStringLiteral("is outside"))
            .arg(r.setIndex + 1)
            .arg(locoId).arg(stnId).arg(locoId + stnId)
            .arg(even ? QStringLiteral("even") : QStringLiteral("odd"))
            .arg(r.keyIndex + 1);

    r.ok = true;
    return r;
}

}  // namespace SessionKeyGen
