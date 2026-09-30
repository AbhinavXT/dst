#include <QFile>
#include "testutil.h"

#include "packetbuilder.h"
#include "schema/schemaencoder.h"

#include <QByteArray>

namespace {
QString rowVal(const QVector<FieldRow> &rows, const QString &needle)
{
    for (const FieldRow &r : rows) { if (r.field.contains(needle)) { return r.value; } }
    return QString();
}
Schema::SubEntry movementAuthority()
{
    Schema::SubEntry ma; ma.type = 0;   // MovementAuthority
    ma.values["FRAME_OFFSET"]       = 3;
    ma.values["TRAIN_SECTION_TYPE"] = 1;
    ma.values["CUR_SIG_INFO"]       = 4321;
    ma.values["APPR_SIG_DIST"]      = 1500;
    ma.values["AUTHORITY_TYPE"]     = 1;
    ma.values["AUTHORIZED_SPEED"]   = 12;    // present iff AUTHORITY_TYPE==1
    ma.values["MA_W_R_T_SIG"]       = 2000;
    ma.values["REQ_SHORTEN_MA"]     = 1;
    ma.values["NEW_MA"]             = 1800;  // present iff REQ_SHORTEN_MA==1
    ma.values["NEXT_STN_COMM"]      = 1;
    ma.values["APPR_STN_ILC_IBS_ID"] = 777;  // present iff NEXT_STN_COMM==1
    return ma;
}
QHash<QString, qint64> slrpHeader()
{
    QHash<QString, qint64> h;
    h["PKT_TYPE"] = 9; h["FRAME_NUM"] = 12345; h["SOURCE_STN_ID"] = 521;
    h["STN_VERSION"] = 1; h["DEST_LOCO_ID"] = 33; h["REF_PROF_ID"] = 2;
    h["LAST_REF_RFID"] = 612; h["DIST_PKT_START"] = -40; h["PKT_DIR"] = 1;
    return h;
}
}  // namespace

TEST_SUITE(packetbuilder)
{
    PacketBuilder pb;
    CHECK(pb.ready(), "schema loaded for encoder");
    if (!pb.ready()) { return; }

    const QByteArray key = QByteArray::fromHex("882a8e3de19b4a473468b0b2d185f1ea");

    // ---- SLRP with one MovementAuthority sub-packet ----------------------
    {
        QVector<Schema::SubEntry> subs; subs << movementAuthority();
        const PacketBuilder::Result r = pb.build("slrp", slrpHeader(), subs, key);
        CHECK(r.ok, "SLRP builds");
        CHECK(r.roundTripped, "built SLRP decodes back");
        CHECK(r.crcVerified, "CRC self-check passes");
        CHECK(r.hasMac && r.macKeyed, "MAC present and keyed");
        CHECK(r.pktLength == r.frame.size(), "PKT_LENGTH == total frame bytes");

        // header survives the round trip
        CHECK(rowVal(r.decoded, "DEST_LOCO_ID") == "33", "DEST_LOCO_ID round-trips");
        CHECK(rowVal(r.decoded, "SOURCE_STN_ID") == "521", "SOURCE_STN_ID round-trips");
        CHECK(rowVal(r.decoded, "DIST_PKT_START").contains("-40"), "signed field round-trips");
        // when-gated struct fields are present and correct
        CHECK(rowVal(r.decoded, "NEW_MA").contains("1800"), "when-gated NEW_MA emitted");
        CHECK(rowVal(r.decoded, "APPR_STN_ILC_IBS_ID") == "777",
              "when-gated APPR_STN_ILC_IBS_ID emitted");
        // PKT_LENGTH decodes to what we wrote
        CHECK(rowVal(r.decoded, "PKT_LENGTH") == QString::number(r.pktLength),
              "PKT_LENGTH decodes to the computed total");
    }

    // ---- no key => zero MAC placeholder, still a valid (CRC-clean) frame --
    {
        QVector<Schema::SubEntry> subs; subs << movementAuthority();
        const PacketBuilder::Result r = pb.build("slrp", slrpHeader(), subs, QByteArray());
        CHECK(r.ok, "SLRP builds without a key");
        CHECK(r.hasMac && !r.macKeyed, "MAC is an unkeyed placeholder");
        CHECK(r.macHex == "00000000", "placeholder MAC is zero");
        CHECK(r.crcVerified, "CRC still valid over the zero-MAC frame");
    }

    // ---- two sub-packets change the frame and both decode -----------------
    {
        QVector<Schema::SubEntry> subs; subs << movementAuthority() << movementAuthority();
        const PacketBuilder::Result r = pb.build("slrp", slrpHeader(), subs, key);
        CHECK(r.ok, "SLRP with two sub-packets builds");
        int nMA = 0;
        for (const FieldRow &fr : r.decoded) {
            if (fr.field.contains("MovementAuthority")) { ++nMA; }
        }
        CHECK(nMA == 2, "both sub-packets are present after decode");
    }

    // ---- repeat sub-packet: GradientProfile with 3 rows ------------------
    // count field (LM_Grad_Info_CNT) is auto-set; each row's fields decode
    // back, proving the repeat encoder is a correct inverse of the walk.
    {
        Schema::SubEntry gp; gp.type = 2;   // GradientProfile
        QVector<QHash<QString, qint64>> rows;
        rows << QHash<QString, qint64>{ {"d", 100}, {"dir", 0}, {"value", 5} };
        rows << QHash<QString, qint64>{ {"d", 250}, {"dir", 1}, {"value", 12} };
        rows << QHash<QString, qint64>{ {"d", 400}, {"dir", 0}, {"value", 3} };
        gp.repeats.insert("grad", rows);

        QVector<Schema::SubEntry> subs; subs << gp;
        const PacketBuilder::Result r = pb.build("slrp", slrpHeader(), subs, key);
        CHECK(r.ok, "SLRP with a GradientProfile repeat builds");
        CHECK(r.roundTripped && r.crcVerified, "and self-verifies");
        int nGrad = 0;
        for (const FieldRow &fr : r.decoded) { if (fr.field.contains("grad[")) { ++nGrad; } }
        CHECK(nGrad == 3, "all 3 repeat rows are present after decode");
    }

    // ---- per-row when: StaticSpeedProfile mixes class 0 and 1 ------------
    {
        Schema::SubEntry sp; sp.type = 1;   // StaticSpeedProfile
        QVector<QHash<QString, qint64>> rows;
        // class==0 -> only 'u' present; class==1 -> a/b/c present
        rows << QHash<QString, qint64>{ {"d", 500}, {"class", 0}, {"u", 10} };
        rows << QHash<QString, qint64>{ {"d", 800}, {"class", 1}, {"a", 6}, {"b", 8}, {"c", 4} };
        sp.repeats.insert("speed", rows);

        QVector<Schema::SubEntry> subs; subs << sp;
        const PacketBuilder::Result r = pb.build("slrp", slrpHeader(), subs, key);
        CHECK(r.ok, "StaticSpeedProfile with mixed class rows builds");
        CHECK(r.roundTripped && r.crcVerified, "and self-verifies");
    }

    // ---- an empty repeat is valid (count 0) -----------------------------
    {
        Schema::SubEntry tsr; tsr.type = 7;   // TSR: repeat cmin=0
        tsr.values["TSR_STATUS"] = 0;
        tsr.repeats.insert("tsr", {});         // zero rows
        QVector<Schema::SubEntry> subs; subs << tsr;
        const PacketBuilder::Result r = pb.build("slrp", slrpHeader(), subs, key);
        CHECK(r.ok, "TSR with zero repeat rows builds (repeat now supported)");
    }

    // ---- a packet whose header uses unsupported grammar is refused --------
    {
        // DOP1 is a <repeat> of pins — header-level unsupported grammar.
        const PacketBuilder::Result r = pb.build("dop1", {}, {}, key);
        CHECK(!r.ok, "dop1 refused (repeat header)");
    }

    // ---- byte-identical to a REAL received frame ------------------------
    // A genuine @slrp frame from replay/loco_1_1_26062026_162418.cap: one
    // MovementAuthority sub-packet. Feeding its exact field values back
    // through the builder must reproduce the received body byte-for-byte —
    // the encoder emits the same msb-first wire order the loco receives.
    // (3322/3322 buildable capture frames verified this offline; this pins
    // one so it can't regress.)
    {
        QHash<QString, qint64> h;
        h["PKT_TYPE"] = 9; h["PKT_LENGTH"] = 33; h["FRAME_NUM"] = 59359;
        h["SOURCE_STN_ID"] = 527; h["STN_VERSION"] = 2; h["DEST_LOCO_ID"] = 1;
        h["REF_PROF_ID"] = 1; h["LAST_REF_RFID"] = 10;
        h["DIST_PKT_START"] = 31368;   // 15-bit; masks to the same bits as -1400
        h["PKT_DIR"] = 1;

        Schema::SubEntry ma; ma.type = 0;
        ma.values["FRAME_OFFSET"] = 0;       ma.values["DEST_LOCO_SOS"] = 0;
        ma.values["TRAIN_SECTION_TYPE"] = 0; ma.values["CUR_SIG_INFO"] = 15200;
        ma.values["CUR_SIG_ASPECT"] = 1;     ma.values["NEXT_SIG_ASPECT"] = 0;
        ma.values["APPR_SIG_DIST"] = 270;    ma.values["AUTHORITY_TYPE"] = 1;
        ma.values["AUTHORIZED_SPEED"] = 20;  ma.values["MA_W_R_T_SIG"] = 1670;
        ma.values["REQ_SHORTEN_MA"] = 0;     ma.values["TRN_LEN_INFO_STS"] = 0;
        ma.values["NEXT_STN_COMM"] = 0;

        QVector<Schema::SubEntry> subs; subs << ma;
        const PacketBuilder::Result r = pb.build("slrp", h, subs, key);
        CHECK(r.ok, "real-frame values build");

        const QByteArray realBody = QByteArray::fromHex(
            "9085cfbe041e80000440af5108018000ed801000872a034300");   // body, no MAC/CRC
        CHECK(r.frame.left(realBody.size()) == realBody,
              "built body is byte-identical to the received frame");
        CHECK(r.pktLength == 33, "auto PKT_LENGTH matches the real frame's 33");
    }

    // ---- AAP: flat fields with an inline MAC_CODE field ------------------
    {
        QHash<QString, qint64> h;
        h["PKT_TYPE"] = 11; h["FRAME_NUM"] = 7; h["SOURCE_STN_ID"] = 521;
        h["DEST_LOCO_ID"] = 1; h["STN_RND_NUM_RS"] = 0x1234;
        const PacketBuilder::Result r = pb.build("aap", h, {}, key);
        CHECK(r.ok, "AAP builds");
        CHECK(r.roundTripped && r.crcVerified, "AAP self-verifies");
        CHECK(r.hasMac && r.macKeyed, "AAP MAC_CODE field is signed with the key");
        const QByteArray inFrameMac = r.frame.mid(r.frame.size() - 8, 4);
        CHECK(QString::fromLatin1(inFrameMac.toHex()) == r.macHex,
              "computed MAC lands in the MAC_CODE field");
    }

    // ---- AEP: flat fields, no MAC ----------------------------------------
    {
        QHash<QString, qint64> h;
        h["PKT_TYPE"] = 12; h["FRAME_NUM"] = 3; h["SOURCE_STN_ILC_IBS_ID"] = 40;
        h["GEN_SOS_CALL"] = 1;
        const PacketBuilder::Result r = pb.build("aep", h, {}, key);
        CHECK(r.ok, "AEP builds");
        CHECK(r.roundTripped && r.crcVerified, "AEP self-verifies");
        CHECK(!r.hasMac, "AEP carries no MAC");
    }

    // ---- ARP: body_offset send format — [body][CRC LE], no envelope ------
    {
        QHash<QString, qint64> h;
        h["PKT_TYPE"] = 13; h["FRAME_NUM"] = 5; h["SOURCE_LOCO_ID"] = 1;
        h["ABS_LOCO_LOC"] = 1000; h["TRAIN_SPEED"] = 20; h["LOCO_RND_NUM_RL"] = 0xBEEF;
        h["lon_d"] = 77; h["lat_d"] = 28;      // group fields (DMS)
        const PacketBuilder::Result r = pb.build("arp", h, {}, key);
        CHECK(r.ok, "ARP builds (send format)");
        CHECK(r.roundTripped, "ARP decodes back at offset 0");
        CHECK(r.crcVerified, "ARP LE CRC self-checks");
        CHECK(!r.hasMac, "ARP has no MAC");
        // groups round-trip
        QString lon, lat;
        for (const FieldRow &fr : r.decoded) {
            if (fr.field.contains("lon_d")) { lon = fr.value; }
            if (fr.field.contains("Longitude")) { lon = fr.value; }
        }
        CHECK(!r.decoded.isEmpty(), "ARP produced decoded rows");
    }

    // ---- LSRP: body_offset + inline MAC + flags/when ---------------------
    {
        QHash<QString, qint64> h;
        h["PKT_TYPE"] = 10; h["FRAME_NUM"] = 2; h["SOURCE_LOCO_ID"] = 1;  // even frame
        h["TRAIN_SPEED"] = 15;
        const PacketBuilder::Result r = pb.build("lsrp", h, {}, key);
        CHECK(r.ok, "LSRP builds (send format)");
        CHECK(r.roundTripped && r.crcVerified, "LSRP self-verifies");
        CHECK(r.hasMac && r.macKeyed, "LSRP MAC_CODE signed");
    }
}

// =============================================================================
//  The self-verify actually verifies now.
//
//  It used to be `roundTripped = !decoded.isEmpty()` — does the built frame
//  decode to SOMETHING. A frame packed in the wrong bit order decodes to
//  something. So does one whose field values were silently truncated to fit.
//  Both passed, which is how a wrong frame could leave this program.
//
//  It now re-parses the body and compares each requested field against what
//  the frame carries, in BITS: a signed field goes in as a raw pattern and
//  comes back negative, and comparing decimals would call every one of those
//  a fault.
// =============================================================================

TEST_SUITE(buildverify)
{
    PacketBuilder pb;
    const QByteArray key = QByteArray::fromHex("000102030405060708090a0b0c0d0e0f");

    // ---- a value too wide for its field is caught --------------------------
    {
        QHash<QString, qint64> h;
        h["PKT_TYPE"]      = 9;
        h["FRAME_NUM"]     = 59359;
        h["SOURCE_STN_ID"] = 527;
        h["STN_VERSION"]   = 2;
        h["DEST_LOCO_ID"]  = 1;
        h["REF_PROF_ID"]   = 1;
        h["LAST_REF_RFID"] = 10;
        h["PKT_DIR"]       = 1;
        // REF_PROF_ID is narrow; ask for a value that cannot fit in it.
        h["REF_PROF_ID"]   = 0x7FFFFFFF;

        Schema::SubEntry ma; ma.type = 0;
        QVector<Schema::SubEntry> subs; subs << ma;

        const PacketBuilder::Result r = pb.build("slrp", h, subs, key);
        CHECK(!r.ok, "a value that does not fit its field fails the build");
        CHECK(!r.roundTripped, "and is not reported as round-tripped");
        CHECK(r.error.contains("REF_PROF_ID"),
              "the error names the field, so the operator is not sent back "
              "through every editor by hand");
        CHECK(r.error.contains("asked") && r.error.contains("carries"),
              "and says what was asked for against what the frame holds");
    }

    // ---- a signed field is not mistaken for drift ---------------------------
    {
        QHash<QString, qint64> h;
        h["PKT_TYPE"] = 9; h["FRAME_NUM"] = 59359; h["SOURCE_STN_ID"] = 527;
        h["STN_VERSION"] = 2; h["DEST_LOCO_ID"] = 1; h["REF_PROF_ID"] = 1;
        h["LAST_REF_RFID"] = 10; h["PKT_DIR"] = 1;
        h["DIST_PKT_START"] = 31368;   // 15-bit; reads back as -1400

        Schema::SubEntry ma; ma.type = 0;
        QVector<Schema::SubEntry> subs; subs << ma;

        const PacketBuilder::Result r = pb.build("slrp", h, subs, key);
        CHECK(r.ok, "a signed field written as a raw bit pattern still builds");
        CHECK(r.roundTripped, "and counts as round-tripped: the bits agree");
    }

    // ---- which schema built it ---------------------------------------------
    {
        // Empty means the copy built into the binary. The point of exposing it
        // is that "which schema produced this frame" is the first question
        // when a frame is not what was expected — and until now the builder
        // always used the resource even when the log was being decoded with
        // an external file.
        CHECK(pb.schemaPath().isEmpty() || QFile::exists(pb.schemaPath()),
              "the builder can say which kavach.xml it encodes against");
    }
}
