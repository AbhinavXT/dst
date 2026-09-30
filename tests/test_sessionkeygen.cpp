#include "testutil.h"

#include "sessionkeygen.h"
#include "crypto/kavachmac.h"

#include <QByteArray>
#include <QDateTime>

namespace {
QByteArray hx(const char *h) { return QByteArray::fromHex(h); }

QByteArray sessionOf(quint16 loco, quint16 stn, const char *keyHex)
{
    return KavachMac::sessionKey(loco, stn, QByteArray::fromHex(keyHex));
}

SessionKeyGen::KeySet mkSet(const QDateTime &s, const QDateTime &e,
                            const char *k0, const char *k1)
{
    SessionKeyGen::KeySet ks;
    ks.start = s; ks.end = e; ks.key0 = hx(k0); ks.key1 = hx(k1);
    return ks;
}
}  // namespace

TEST_SUITE(sessionkeygen)
{
    const QDateTime jan  = QDateTime(QDate(2026, 1, 1), QTime(0, 0));
    const QDateTime jun  = QDateTime(QDate(2026, 6, 1), QTime(0, 0));
    const QDateTime dec  = QDateTime(QDate(2026, 12, 1), QTime(0, 0));
    const QDateTime nowIn  = QDateTime(QDate(2026, 3, 1), QTime(0, 0));  // in set 1
    const QDateTime nowOut = QDateTime(QDate(2026, 9, 1), QTime(0, 0));  // out of set 1

    // key that yields a known session key with loco=0x1234, stn=0xABCD
    const char *AK = "1234567890abcdef1234567890abcdef";
    const char *OTHER = "00112233445566778899aabbccddeeff";
    const QByteArray KNOWN = hx("882a8e3de19b4a473468b0b2d185f1ea");

    // set 1 valid Jan..Jun, set 2 valid Jun..Dec
    const auto set1 = mkSet(jan, jun, AK, OTHER);
    const auto set2 = mkSet(jun, dec, OTHER, AK);

    // ---- rule 1: time window picks the set --------------------------------
    {
        // now in set 1, even id sum -> set 1, key 1 (= AK) -> KNOWN
        const auto r = SessionKeyGen::derive(set1, set2, 0x1234, 0xABCD,
                                             /*loco*/2, /*stn*/4, nowIn);
        CHECK(r.ok, "derives when now is in set 1");
        CHECK(r.setIndex == 0, "now in set-1 window -> set 1");
        CHECK(r.set1Active, "set1Active flagged");
        CHECK(r.keyIndex == 0, "even id sum -> key 1");
        CHECK(r.sessionKey == KNOWN, "session key matches the known vector");
    }
    {
        // now outside set 1 -> falls back to set 2
        const auto r = SessionKeyGen::derive(set1, set2, 0x1234, 0xABCD,
                                             2, 4, nowOut);
        CHECK(r.setIndex == 1, "now outside set-1 window -> set 2");
        CHECK(!r.set1Active, "set1 not active");
        // set 2 key1 (even) is OTHER, not AK, so the key differs from KNOWN
        CHECK(r.sessionKey != KNOWN, "different set -> different session key");
    }

    // ---- rule 2: id parity picks the key ---------------------------------
    {
        const auto even = SessionKeyGen::derive(set1, set2, 0x1234, 0xABCD, 3, 5, nowIn);
        const auto odd  = SessionKeyGen::derive(set1, set2, 0x1234, 0xABCD, 3, 4, nowIn);
        CHECK(even.keyIndex == 0, "loco+stn even -> key 1");
        CHECK(odd.keyIndex == 1,  "loco+stn odd  -> key 2");
        CHECK(even.sessionKey != odd.sessionKey, "different key -> different session key");
        // set 1 key1 (odd) is OTHER; verify odd picked it
        CHECK(odd.sessionKey == sessionOf(0x1234, 0xABCD, "00112233445566778899aabbccddeeff"),
              "odd selects the second key");
    }

    // ---- randoms matter ---------------------------------------------------
    {
        const auto a = SessionKeyGen::derive(set1, set2, 0x1111, 0x2222, 2, 4, nowIn);
        const auto b = SessionKeyGen::derive(set1, set2, 0x3333, 0x4444, 2, 4, nowIn);
        CHECK(a.sessionKey != b.sessionKey, "different randoms -> different session key");
    }

    // ---- outside both windows is flagged (still falls back to set 2) -----
    {
        const auto narrow2 = mkSet(jun, dec, OTHER, AK);
        const auto r = SessionKeyGen::derive(set1, narrow2, 0x1234, 0xABCD, 2, 4,
                                             QDateTime(QDate(2027, 1, 1), QTime(0, 0)));
        CHECK(r.ok, "still derives");
        CHECK(r.setIndex == 1, "falls back to set 2");
        CHECK(r.neitherWindowValid, "flags that now is outside both windows");
    }

    // ---- bad key sizes fail closed ---------------------------------------
    {
        const auto bad = mkSet(jan, jun, "0011", OTHER);   // key0 too short
        const auto r = SessionKeyGen::derive(bad, set2, 0x1234, 0xABCD, 2, 4, nowIn);
        CHECK(!r.ok, "short key -> error");
    }
}
