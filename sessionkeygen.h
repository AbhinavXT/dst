#ifndef SESSIONKEYGEN_H
#define SESSIONKEYGEN_H
// =====================================================================
//  sessionkeygen.{h,cpp} -- pick the right auth key and derive the
//  session key, mirroring the firmware's GenerateCurrentSessionKey.
//
//  Inputs: the two auth key sets (each two 16-byte keys with a validity
//  window), the loco and station random numbers, and the loco/station
//  ids. Selection:
//
//    1. Set:  if `now` is within key set 1's [start, end] window, use
//             set 1; otherwise use set 2.
//    2. Key:  within the chosen set, (loco_id + stn_id) even -> key 1,
//             odd -> key 2.
//
//  Then session_key = AES128(chosen_key, random-block(loco, stn)), which
//  is KavachMac::sessionKey. The result reports which set and key were
//  chosen and why, so the operator can sanity-check the selection.
// =====================================================================
#include <QByteArray>
#include <QDateTime>
#include <QString>

namespace SessionKeyGen {

struct KeySet {
    QDateTime  start;
    QDateTime  end;
    QByteArray key0;   // "first" key  (16 bytes)
    QByteArray key1;   // "second" key (16 bytes)

    bool keysOk() const { return key0.size() == 16 && key1.size() == 16; }
    bool windowOk() const { return start.isValid() && end.isValid() && start <= end; }
    bool contains(const QDateTime &t) const { return windowOk() && t >= start && t <= end; }
};

struct Result {
    bool       ok = false;
    QString    error;
    QByteArray sessionKey;      // 16 bytes when ok
    int        setIndex = -1;   // 0 = set 1, 1 = set 2
    int        keyIndex = -1;   // 0 = key 1, 1 = key 2
    bool       set1Active = false;   // was `now` inside set 1's window?
    bool       neitherWindowValid = false;  // now is outside both — informational
    QString    explanation;
};

// set0 = "key set 1", set1 = "key set 2".
Result derive(const KeySet &set0, const KeySet &set1,
              quint16 locoRandom, quint16 stnRandom,
              int locoId, int stnId, const QDateTime &now);

}  // namespace SessionKeyGen

#endif  // SESSIONKEYGEN_H
