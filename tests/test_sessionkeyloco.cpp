#include "testutil.h"

#include "sessionkeystore.h"
#include "sessionkeygen.h"
#include "crypto/kavachmac.h"

#include <QByteArray>
#include <QDateTime>

// =============================================================================
//  SessionKeyStore — one state per loco.
//
//  A session is between one loco and one station. The store used to hold a
//  single global state, so a capture carrying two locos mixed them: loco 2's
//  @rand_num overwrote loco 1's randoms, and the MAC badge on a loco 1 frame
//  was then checked against a key derived from loco 2's material. The
//  replay/ directory has exactly this shape (loco 1 and loco 2 captures, and
//  stations 501 and 527), so it is not a hypothetical.
//
//  Within a loco, last write wins — that part is the intended behaviour and is
//  pinned here too, so a future change cannot quietly turn it into
//  first-write-wins or into a time-indexed history.
// =============================================================================

namespace {

QString authLine(int n, const QDateTime &start, const QDateTime &end,
                 const QByteArray &k0, const QByteArray &k1, int locoTag,
                 const QDateTime &when = QDateTime::currentDateTime())
{
    auto revTime = [](QByteArray &b, const QDateTime &t) {
        b.append(char(t.time().hour()));
        b.append(char(t.date().day()));
        b.append(char(t.date().month()));
        b.append(char(t.date().year() - 2000));
    };
    QByteArray p;
    revTime(p, start); revTime(p, end);
    p += k0; p += k1;
    return QStringLiteral("@auth_keys%1_%2_1 %3 100 %4")
               .arg(n).arg(locoTag)
               .arg(when.toString(Qt::ISODate))
               .arg(QString::fromLatin1(p.toHex()));
}

QString randLine(quint16 locoRnd, quint16 stnRnd, int locoTag,
                 const QDateTime &when = QDateTime::currentDateTime())
{
    QByteArray p;
    p.append(char(locoRnd & 0xFF)); p.append(char(locoRnd >> 8));   // little-endian
    p.append(char(stnRnd & 0xFF));  p.append(char(stnRnd >> 8));
    return QStringLiteral("@rand_num_%1_1 %2 101 %3")
               .arg(locoTag).arg(when.toString(Qt::ISODate))
               .arg(QString::fromLatin1(p.toHex()));
}

void feed(SessionKeyStore &s, const QString &line)
{
    s.observeLine(CaptureDecoder::parseLine(line));
}

}  // namespace

TEST_SUITE(sessionkeyloco)
{
    SessionKeyStore &store = SessionKeyStore::instance();
    store.clear();

    const QByteArray K1 = QByteArray::fromHex("1234567890abcdef1234567890abcdef");
    const QByteArray K2 = QByteArray::fromHex("00112233445566778899aabbccddeeff");
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime s1a = now.addDays(-10), s1b = now.addDays(10);   // set 1 active
    const QDateTime s2a = now.addDays(20),  s2b = now.addDays(40);

    // ---- two locos, complete material each, different randoms --------------
    feed(store, authLine(1, s1a, s1b, K1, K2, /*loco*/1));
    feed(store, authLine(2, s2a, s2b, K2, K1, /*loco*/1));
    feed(store, randLine(0x1111, 0x2222, /*loco*/1));

    feed(store, authLine(1, s1a, s1b, K1, K2, /*loco*/2));
    feed(store, authLine(2, s2a, s2b, K2, K1, /*loco*/2));
    feed(store, randLine(0xAAAA, 0xBBBB, /*loco*/2));

    CHECK(store.locos().size() == 2, "two locos are tracked separately");
    CHECK(store.locos().contains(1) && store.locos().contains(2),
          "and they are the two seen in the stream");
    CHECK(store.activeLoco() == 2, "the active loco is the one that moved last");

    CHECK(store.haveSessionKey(1), "loco 1 has a key");
    CHECK(store.haveSessionKey(2), "loco 2 has a key");
    CHECK(store.sessionKey(1) != store.sessionKey(2),
          "different randoms give different session keys");

    // The regression this suite exists for: loco 2's material must NOT have
    // landed on loco 1.
    CHECK(store.locoRandom(1) == 0x1111 && store.stnRandom(1) == 0x2222,
          "loco 1 keeps its own randoms after loco 2 is seen");
    CHECK(store.locoRandom(2) == 0xAAAA && store.stnRandom(2) == 0xBBBB,
          "loco 2 keeps its own");

    // Derivation is per loco: ids 1+1 and 2+1 differ in parity, so the two
    // locos select different keys out of the same set.
    CHECK(store.selection(1).keyIndex == 0, "loco 1: 1+1 even -> key 1");
    CHECK(store.selection(2).keyIndex == 1, "loco 2: 2+1 odd  -> key 2");

    const QByteArray expect1 = KavachMac::sessionKey(0x1111, 0x2222, K1);
    CHECK(store.sessionKey(1) == expect1, "loco 1's key is the direct computation");

    // ---- last write wins, within a loco ------------------------------------
    feed(store, randLine(0x3333, 0x4444, /*loco*/1));
    CHECK(store.locoRandom(1) == 0x3333,
          "a newer @rand_num replaces the older one for that loco");
    CHECK(store.sessionKey(1) == KavachMac::sessionKey(0x3333, 0x4444, K1),
          "and the key is re-derived from it");
    CHECK(store.locoRandom(2) == 0xAAAA, "the other loco is untouched");

    // A newer key set replaces the old one, same rule.
    const QByteArray K3 = QByteArray::fromHex("0f0e0d0c0b0a09080706050403020100");
    feed(store, authLine(1, s1a, s1b, K3, K2, /*loco*/1));
    CHECK(store.sessionKey(1) == KavachMac::sessionKey(0x3333, 0x4444, K3),
          "a newer @auth_keys1 replaces the key set");

    // ---- the default accessor follows the active loco ----------------------
    feed(store, randLine(0xCCCC, 0xDDDD, /*loco*/2));
    CHECK(store.activeLoco() == 2, "loco 2 moved last, so it is active");
    CHECK(store.sessionKey() == store.sessionKey(2),
          "the no-argument accessor means the active loco");

    // ---- a MAC is checked against ITS OWN loco's key ------------------------
    // Not the active one. This is what the decoder now passes through from the
    // capture tag, and it is the difference between a correct FAIL and a
    // meaningless one on a two-loco log.
    {
        const QByteArray body(24, '\x11');
        const QByteArray mac1 = KavachMac::cbcMacWire(body, store.sessionKey(1));
        QByteArray frame = body; frame += mac1; frame += QByteArray(4, '\0');

        CHECK(store.verifyMac("slrp", frame, 1) == SessionKeyStore::Mac::Pass,
              "a loco 1 frame verifies against loco 1's key");
        CHECK(store.verifyMac("slrp", frame, 2) == SessionKeyStore::Mac::Fail,
              "the same frame fails against loco 2's key");
        CHECK(store.verifyMac("slrp", frame, 99) == SessionKeyStore::Mac::NoKey,
              "an unseen loco reports no-key rather than a verdict");
    }

    // ---- manual ids are per loco too ---------------------------------------
    store.setIds(1, 2);                       // 1+2 odd -> key 2
    CHECK(store.selection(1).keyIndex == 1, "the override applies to loco 1");
    CHECK(store.selection(2).keyIndex == 1, "loco 2 keeps its own selection");

    // ---- the window rule runs on the LOG's clock, not the wall clock -------
    // Replaying an old capture: the traffic is from last year, and set 1's
    // window is around the traffic. Judged by "now" the frame falls outside
    // both windows and the rule drops to set 2 — every MAC then checked
    // against the wrong key. Judged by the frame's own RTC it is set 1.
    {
        store.clear();
        const QDateTime then = QDateTime::currentDateTime().addYears(-1);
        const QDateTime w1a = then.addDays(-5), w1b = then.addDays(5);
        const QDateTime w2a = then.addDays(10), w2b = then.addDays(40);

        feed(store, authLine(1, w1a, w1b, K1, K2, 1, then));
        feed(store, authLine(2, w2a, w2b, K2, K1, 1, then));
        feed(store, randLine(0x1111, 0x2222, 1, then));

        CHECK(store.haveSessionKey(1), "an old capture still derives a key");
        CHECK(store.logTime(1).date() == then.date(),
              "the state is stamped with the log's time, not today's");
        CHECK(store.selection(1).setIndex == 0,
              "the traffic was inside set 1's window -> set 1");
        CHECK(!store.selection(1).neitherWindowValid,
              "and it is not reported as outside both windows");
        CHECK(store.sessionKey(1) == KavachMac::sessionKey(0x1111, 0x2222, K1),
              "so the key comes from set 1, as it did when the log was recorded");
    }

    store.clear();
    CHECK(store.locos().isEmpty(), "clear() forgets every loco");
}
