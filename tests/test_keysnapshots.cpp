#include "testutil.h"

#include "sessionkeystore.h"
#include "sessionkeygen.h"
#include "crypto/kavachmac.h"

#include <QByteArray>
#include <QDateTime>

// =============================================================================
//  SessionKeyStore — key set snapshots.
//
//  The live state moves (last write wins). A snapshot does not: every complete
//  combination of {auth key sets, randoms, loco id, stn id} the log has
//  produced is frozen with the session key it derives to, so a frame can be
//  checked — or built — against the material in force when it was sent rather
//  than only against the newest. The Decode Workbench and Packet Maker pickers
//  are lists of these.
//
//  Two rules that are easy to get wrong and are pinned here:
//    - only COMPLETE combinations become snapshots; a half-filled state is not
//      something to offer as a key to sign with.
//    - a combination that RECURS reuses its snapshot. A loco moving
//      527 -> 501 -> 527 must not pile up duplicates in the picker.
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
               .arg(n).arg(locoTag).arg(when.toString(Qt::ISODate))
               .arg(QString::fromLatin1(p.toHex()));
}

QString randLine(quint16 locoRnd, quint16 stnRnd, int locoTag,
                 const QDateTime &when = QDateTime::currentDateTime())
{
    QByteArray p;
    p.append(char(locoRnd & 0xFF)); p.append(char(locoRnd >> 8));
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

TEST_SUITE(keysnapshots)
{
    SessionKeyStore ownStore;                 // session 96: the test's own, not a global
    SessionKeyStore &store = ownStore;
    store.clear();

    const QByteArray K1 = QByteArray::fromHex("1234567890abcdef1234567890abcdef");
    const QByteArray K2 = QByteArray::fromHex("00112233445566778899aabbccddeeff");
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime s1a = now.addDays(-10), s1b = now.addDays(10);
    const QDateTime s2a = now.addDays(20),  s2b = now.addDays(40);

    // ---- incomplete material produces no snapshot --------------------------
    feed(store, authLine(1, s1a, s1b, K1, K2, 1));
    CHECK(store.snapshots().isEmpty(), "one key set alone is not a usable set");
    feed(store, authLine(2, s2a, s2b, K2, K1, 1));
    CHECK(store.snapshots().isEmpty(), "still nothing without randoms");

    // ---- completion creates the first snapshot -----------------------------
    feed(store, randLine(0x1111, 0x2222, 1));
    CHECK(store.snapshots().size() == 1, "completing the material creates a set");
    {
        const KeySnapshot k = store.snapshots().first();
        CHECK(k.id == 1, "ids start at 1");
        CHECK(k.isValid(), "and the set carries a derived key");
        CHECK(k.locoId == 1, "with the loco it belongs to");
        CHECK(k.locoRandom == 0x1111 && k.stnRandom == 0x2222, "and its randoms");
        CHECK(k.result.sessionKey == KavachMac::sessionKey(0x1111, 0x2222, K1),
              "the key is the one derived from that material");
        CHECK(store.currentSnapshotId(1) == k.id, "and it is the one in force");
        CHECK(!k.label().isEmpty(), "a set has a label for the pickers");
    }

    // ---- each change makes a new set ---------------------------------------
    feed(store, randLine(0x3333, 0x4444, 1));
    CHECK(store.snapshots().size() == 2, "new randoms -> new set");
    CHECK(store.currentSnapshotId(1) == 2, "the new one is in force");

    store.setIds(1, 4);                       // station id change
    CHECK(store.snapshots().size() == 3, "a new stn id -> new set");

    const QByteArray K3 = QByteArray::fromHex("0f0e0d0c0b0a09080706050403020100");
    feed(store, authLine(1, s1a, s1b, K3, K2, 1));
    CHECK(store.snapshots().size() == 4, "a new key set -> new set");

    // ---- the older sets are still intact and still usable ------------------
    {
        const KeySnapshot *first = store.snapshot(1);
        CHECK(first != nullptr, "set #1 is still there");
        CHECK(first && first->result.sessionKey
                           == KavachMac::sessionKey(0x1111, 0x2222, K1),
              "and still holds the key it was created with");
        CHECK(store.snapshot(999) == nullptr, "an unknown id resolves to nothing");
    }

    // ---- a recurring combination reuses its set ----------------------------
    // Back to the ids of set #3's combination: same material, so the picker
    // must not grow. This is the 527 -> 501 -> 527 handover case.
    {
        const int before = store.snapshots().size();
        store.setIds(1, 2);                   // a combination not seen before
        CHECK(store.snapshots().size() == before + 1, "an unseen combination is new");
        const int newId = store.currentSnapshotId(1);

        store.setIds(1, 4);                   // back to set #3's ids
        CHECK(store.snapshots().size() == before + 1,
              "returning to an earlier combination adds nothing");
        CHECK(store.currentSnapshotId(1) != newId,
              "and the earlier set becomes the one in force again");
    }

    // ---- verifying against a chosen set ------------------------------------
    {
        const KeySnapshot *k1 = store.snapshot(1);
        const KeySnapshot *k2 = store.snapshot(2);
        CHECK(k1 && k2 && k1->result.sessionKey != k2->result.sessionKey,
              "two sets, two different keys");

        const QByteArray body(24, '\x22');
        QByteArray frame = body;
        frame += KavachMac::cbcMacWire(body, k1->result.sessionKey);
        frame += QByteArray(4, '\0');

        CHECK(store.verifyMacWith("slrp", frame, 1) == SessionKeyStore::Mac::Pass,
              "a frame signed under set #1 verifies against set #1");
        CHECK(store.verifyMacWith("slrp", frame, 2) == SessionKeyStore::Mac::Fail,
              "and fails against set #2");
        CHECK(store.verifyMacWith("slrp", frame, 999) == SessionKeyStore::Mac::NoKey,
              "an unknown set reports no-key, not a verdict");
        CHECK(store.verifyMacWith("rfid", frame, 1) == SessionKeyStore::Mac::None,
              "a type with no MAC still reports None");

        // The decoder path: passing a set id must reach the MAC row, and the
        // row must say which set the verdict came from.
        const QString line = QStringLiteral("@slrp_1_1 %1 200 %2")
                                 .arg(now.toString(Qt::ISODate))
                                 .arg(QString::fromLatin1(frame.toHex()));
        const CaptureLine c = CaptureDecoder::parseLine(line);
        auto macRow = [](const QVector<FieldRow> &rows) {
            for (const FieldRow &fr : rows) {
                if (fr.field.contains(QLatin1String("MAC (live)"))) { return fr.value; }
            }
            return QString();
        };
        CHECK(macRow(CaptureDecoder::describe(c, nullptr, 1, nullptr, &store)).contains("PASS"),
              "describe() with set #1 shows PASS");
        CHECK(macRow(CaptureDecoder::describe(c, nullptr, 2, nullptr, &store)).contains("FAIL"),
              "describe() with set #2 shows FAIL");
        CHECK(macRow(CaptureDecoder::describe(c, nullptr, 1, nullptr, &store)).contains("#1"),
              "and names the set the verdict came from");
    }

    // ---- sets are per loco, and both stay available ------------------------
    {
        feed(store, authLine(1, s1a, s1b, K1, K2, 2));
        feed(store, authLine(2, s2a, s2b, K2, K1, 2));
        feed(store, randLine(0xAAAA, 0xBBBB, 2));
        const int loco2Set = store.currentSnapshotId(2);
        CHECK(loco2Set > 0, "loco 2 gets its own set");
        CHECK(store.currentSnapshotId(1) != loco2Set,
              "which is not the set in force for loco 1");
        const KeySnapshot *k = store.snapshot(loco2Set);
        CHECK(k && k->locoId == 2, "and it is labelled with its own loco");
    }

    store.clear();
    CHECK(store.snapshots().isEmpty(), "clear() drops every set");
    CHECK(store.currentSnapshotId(1) == 0, "and nothing is in force");
}
