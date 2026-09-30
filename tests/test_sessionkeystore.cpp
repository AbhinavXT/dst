#include "testutil.h"

#include "sessionkeystore.h"
#include "sessionkeygen.h"
#include "packetbuilder.h"
#include "crypto/kavachmac.h"

#include <QByteArray>
#include <QDateTime>

namespace {

// The capture timestamp every line below carries.
//
// It has to track the wall clock, because the store checks a key set's
// validity against THE TIME OF THE TRAFFIC rather than against now — which is
// correct, and is what lets a month-old .cap replay against the set that was
// live when it was recorded. The windows in this suite are built from
// QDateTime::currentDateTime(), and the validity wire format carries only
// [hour, day, month, year]. So a hard-coded 09:30 here quietly stopped being
// inside a window built at 19:39: same day, later hour. The suite passed all
// morning and failed after lunch.
QString stamp()
{
    return QDateTime::currentDateTime().toString(Qt::ISODate);
}

// Build an @auth_keys line for set `n` (1 or 2) carrying one KEY_SET_INFO:
//   count(1) | start(4, reversed) | end(4, reversed) | key0(16) | key1(16)
QString authLine(int n, const QDateTime &start, const QDateTime &end,
                 const QByteArray &k0, const QByteArray &k1, int loco = 1)
{
    auto revTime = [](QByteArray &b, const QDateTime &t) {
        // wire order [hh, dd, mm, yy]; yy = year - 2000
        b.append(char(t.time().hour()));
        b.append(char(t.date().day()));
        b.append(char(t.date().month()));
        b.append(char(t.date().year() - 2000));
    };
    QByteArray p;                          // no count byte: 40-byte set
    revTime(p, start); revTime(p, end);
    p += k0; p += k1;
    return QStringLiteral("@auth_keys%1_%2_1 %3 100 %4")
               .arg(n).arg(loco).arg(stamp(), QString::fromLatin1(p.toHex()));
}

QString randLine(quint16 loco, quint16 stn, int locoTag = 1)
{
    QByteArray p;
    p.append(char(loco & 0xFF)); p.append(char(loco >> 8));   // little-endian
    p.append(char(stn & 0xFF));  p.append(char(stn >> 8));
    return QStringLiteral("@rand_num_%1_1 %2 101 %3")
               .arg(locoTag).arg(stamp(), QString::fromLatin1(p.toHex()));
}

}  // namespace

TEST_SUITE(sessionkeystore)
{
    SessionKeyStore &store = SessionKeyStore::instance();
    store.clear();

    const QByteArray AK    = QByteArray::fromHex("1234567890abcdef1234567890abcdef");
    const QByteArray OTHER = QByteArray::fromHex("00112233445566778899aabbccddeeff");
    const QDateTime now = QDateTime::currentDateTime();
    // set 1 active now; set 2 in the future.
    const QDateTime s1a = now.addDays(-10), s1b = now.addDays(10);
    const QDateTime s2a = now.addDays(20),  s2b = now.addDays(40);
    const quint16 loco = 0x1234, stn = 0xABCD;

    // ---- partial input: no key until everything is present ---------------
    store.observeLine(CaptureDecoder::parseLine(authLine(1, s1a, s1b, AK, OTHER)));
    CHECK(!store.haveSessionKey(), "no key with only set 1");
    store.observeLine(CaptureDecoder::parseLine(authLine(2, s2a, s2b, OTHER, AK)));
    CHECK(!store.haveSessionKey(), "no key without randoms");

    // ---- randoms complete the picture; ids default from the frame (1,1) --
    store.observeLine(CaptureDecoder::parseLine(randLine(loco, stn)));
    CHECK(store.haveRandoms(), "randoms captured");
    CHECK(store.locoRandom() == loco && store.stnRandom() == stn,
          "randoms are little-endian");
    CHECK(store.haveSessionKey(), "session key derived once all inputs present");
    // ids 1+1 = even -> key 1; now in set 1 -> set 1's key0 = AK
    CHECK(store.selection().setIndex == 0, "now in set 1 -> set 1");
    CHECK(store.selection().keyIndex == 0, "1+1 even -> key 1");

    const QByteArray expect = KavachMac::sessionKey(loco, stn, AK);
    CHECK(store.sessionKey() == expect, "derived key matches the direct computation");

    // ---- live MAC verification against a frame signed with that key ------
    PacketBuilder pb;
    if (pb.ready()) {
        QHash<QString, qint64> h;
        h["PKT_TYPE"] = 9; h["FRAME_NUM"] = 42; h["SOURCE_STN_ID"] = 521;
        h["DEST_LOCO_ID"] = 1; h["PKT_DIR"] = 1;
        Schema::SubEntry ma; ma.type = 0;
        ma.values["FRAME_OFFSET"] = 1; ma.values["AUTHORITY_TYPE"] = 1;
        ma.values["AUTHORIZED_SPEED"] = 10; ma.values["MA_W_R_T_SIG"] = 500;
        QVector<Schema::SubEntry> subs; subs << ma;

        const PacketBuilder::Result built = pb.build("slrp", h, subs, store.sessionKey());
        CHECK(built.ok && built.macKeyed, "built a MAC-signed SLRP with the live key");

        CHECK(store.verifyMac("slrp", built.frame) == SessionKeyStore::Mac::Pass,
              "live MAC verifies on the signed frame");

        // The user-visible path: describe() must surface a passing MAC row.
        {
            const QString line = QStringLiteral("@slrp_1_1 2026-08-26T09:30:19 200 %1")
                                     .arg(QString::fromLatin1(built.frame.toHex()));
            const QVector<FieldRow> rows =
                CaptureDecoder::describe(CaptureDecoder::parseLine(line));
            QString mac;
            for (const FieldRow &fr : rows) { if (fr.field.contains("MAC (live)")) { mac = fr.value; } }
            CHECK(mac.contains("PASS"), "decode shows MAC (live) PASS");
        }

        // Tamper one body byte -> MAC must fail.
        QByteArray bad = built.frame; bad[5] = char(bad[5] ^ 0x01);
        CHECK(store.verifyMac("slrp", bad) == SessionKeyStore::Mac::Fail,
              "tampered frame fails the MAC");

        // A non-MAC type reports None.
        CHECK(store.verifyMac("rfid", built.frame) == SessionKeyStore::Mac::None,
              "types without a MAC report None");

        // ---- station id: from SOURCE_STN_ID, and last write wins ---------
        // Before any station frame, stn_id falls back to the rand tag's ctrl
        // id (1). A station packet for THIS loco must correct it, and a later
        // one from a different station must move it again — a loco handing
        // over from one station to the next has to follow, because the parity
        // (loco_id + stn_id) picks which key of the set is used.
        {
            auto stationFrame = [&](int stnIdValue) {
                QHash<QString, qint64> sh;
                sh["PKT_TYPE"] = 9; sh["SOURCE_STN_ID"] = stnIdValue;
                sh["DEST_LOCO_ID"] = 1; sh["PKT_DIR"] = 1;
                Schema::SubEntry m; m.type = 0; m.values["AUTHORITY_TYPE"] = 1;
                QVector<Schema::SubEntry> ss; ss << m;
                return pb.build("slrp", sh, ss, store.sessionKey());
            };

            const PacketBuilder::Result sp = stationFrame(527);
            store.observeLine(CaptureDecoder::parseLine(
                QStringLiteral("@slrp_1_1 2026-08-26T15:10:40 300 %1")
                    .arg(QString::fromLatin1(sp.frame.toHex()))));
            CHECK(store.stnId(1) == 527, "stn_id corrected to SOURCE_STN_ID (527)");

            // The fast bit read must agree with the schema decode, or the two
            // silently disagree the day a field width changes.
            CHECK(SessionKeyStore::stationIdFromFrame(CapType::SLRP, sp.frame) == 527,
                  "stationIdFromFrame reads what the schema encoded");

            const PacketBuilder::Result sp2 = stationFrame(528);
            store.observeLine(CaptureDecoder::parseLine(
                QStringLiteral("@slrp_1_1 2026-08-26T15:11:40 301 %1")
                    .arg(QString::fromLatin1(sp2.frame.toHex()))));
            CHECK(store.stnId(1) == 528, "a later station frame wins (528)");
            // 1+527 = 528 even -> key 1;  1+528 = 529 odd -> key 2.
            CHECK(store.selection(1).keyIndex == 1,
                  "the new station id flips the parity and the chosen key");

            // A @rand_num arriving afterwards must NOT drag the station id
            // back to the tag's ctrl id (1) — the ctrl index is not a station.
            store.observeLine(CaptureDecoder::parseLine(randLine(loco, stn)));
            CHECK(store.stnId(1) == 528,
                  "a later @rand_num does not overwrite a real SOURCE_STN_ID");
        }
    }

    // ---- id override changes the selected key ----------------------------
    store.setIds(1, 2);   // 1+2 = odd -> key 2
    CHECK(store.selection().keyIndex == 1, "odd id sum -> key 2");
    CHECK(store.sessionKey() != expect, "different key -> different session key");

    // ---- no-key state before anything is observed ------------------------
    store.clear();
    CHECK(!store.haveSessionKey(), "clear() resets the store");
    CHECK(store.verifyMac("slrp", QByteArray(40, '\0')) == SessionKeyStore::Mac::NoKey,
          "verify reports no-key after clear");
}
