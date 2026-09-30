#include "testutil.h"

#include "packetbuilder.h"
#include "schema/schemaencoder.h"

#include <QByteArray>
#include <QString>

// =============================================================================
//  Schema::Encoder::parseBody() — the read side of the Packet Maker.
//
//  The property that matters is not "the numbers look plausible" but
//  parse(bytes) -> encode -> the SAME bytes. If a field is missing, mis-sized
//  or gated on the wrong condition, the rebuilt body comes out a different
//  length and the comparison fails; nothing subtler is needed to catch it.
//
//  The golden frame below is a real station-to-loco SLRP capture (station 527,
//  reference RFID 6) taken from the 81_2 log. It was chosen because it carries
//  a type-5 TagLinking sub-packet with ADJ_LINE_CNT=1 — the case that used to
//  look like the sub-packet ended in an unexplained pad byte, and that is
//  actually the always-present trailing 9-bit TIN.
// =============================================================================

namespace {

const char kSlrpRef6[] =
    "91 B9 AA E6 04 1E 80 00 08 40 6F 8E 68 01 80 00 00 00 00 01 7C AA 08 20 "
    "00 11 63 0D 0C 58 10 B1 00 F4 84 00 22 05 09 BA 20 14 00 00 34 C6 08 5A "
    "40 F4 93 00 54 E8 60 00 00 87 BC 04 99 E0 1A 3E 80 80 F1 02 84 38 0C 11 "
    "30 38 22 41 08 7A 04 83 23 8A 29 0E 08 41 38 40 95 0A 60 61 64 E0 00 03 "
    "43 38 68 60 42 C0 3E A4 18 46 67 50 A7 6E";

QByteArray golden()
{
    return QByteArray::fromHex(QByteArray(kSlrpRef6).replace(' ', ""));
}

const Schema::SubEntry *subOfType(const Schema::ParsedPacket &pp, int type)
{
    for (const Schema::SubEntry &se : pp.subs) {
        if (se.type == type) { return &se; }
    }
    return nullptr;
}

}  // namespace

TEST_SUITE(bufferload)
{
    PacketBuilder pb;
    CHECK(pb.ready(), "schema loaded");
    if (!pb.ready()) { return; }
    const Schema::Encoder &enc = pb.encoder();

    const QByteArray frame = golden();
    CHECK(frame.size() == 110, "golden SLRP frame is 110 B");

    // ---- parse -------------------------------------------------------------
    const Schema::ParsedPacket pp = enc.parseBody(QStringLiteral("slrp"), frame);
    CHECK(pp.ok, "golden frame parses");
    if (!pp.ok) { std::printf("        error: %s\n", qPrintable(pp.error)); return; }

    // No notes at all is the real assertion here: parseBody complains whenever
    // a sub-packet leaves 8+ unread bits, which is exactly what a struct that
    // is missing a field looks like from the outside.
    CHECK(pp.notes.isEmpty(), "no leftover-bit or length warnings");
    for (const QString &n : pp.notes) { std::printf("        note: %s\n", qPrintable(n)); }

    CHECK(pp.header.value("PKT_TYPE") == 9,        "PKT_TYPE read back");
    CHECK(pp.header.value("PKT_LENGTH") == 110,    "PKT_LENGTH matches the buffer");
    CHECK(pp.header.value("SOURCE_STN_ID") == 527, "SOURCE_STN_ID read back");
    CHECK(pp.header.value("LAST_REF_RFID") == 6,   "LAST_REF_RFID read back");
    CHECK(pp.header.value("PKT_DIR") == 1,         "PKT_DIR read back");
    CHECK(pp.tailBytes == 8,                       "8 reserved tail bytes (MAC + CRC)");
    CHECK(pp.subs.size() == 5,                     "five sub-packets found");

    // ---- the sub-packet the whole exercise is about ------------------------
    const Schema::SubEntry *tl = subOfType(pp, 5);
    CHECK(tl != nullptr, "TagLinking sub-packet present");
    if (tl) {
        CHECK(tl->values.value("ROUTE_RFID_CNT") == 12, "12 route RFIDs");
        CHECK(tl->repeats.value("tag").size() == 12,    "12 tag rows parsed");
        CHECK(tl->values.value("LOC_RESET") == 0,       "no location reset");
        CHECK(tl->values.value("ADJ_LINE_CNT") == 1,    "one adjacent line");
        CHECK(tl->repeats.value("line_tin").size() == 1, "one adjacent TIN row");
        CHECK(tl->repeats.value("line_tin").value(0).value("tin") == 84,
              "adjacent line TIN is 84");

        // The trailing TIN. Present with ADJ_LINE_CNT already satisfied, and
        // one off the adjacent line — a pad byte would read 0.
        CHECK(tl->values.contains("TIN"), "trailing TIN field exists");
        CHECK(tl->values.value("TIN") == 83, "trailing TIN is 83, not padding");

        const auto tag0 = tl->repeats.value("tag").value(0);
        CHECK(tag0.value("tag") == 2 && tag0.value("dist") == 0,
              "first tag hop is RFID 2 at 0 m");
    }

    // ---- parse -> encode is byte-identical ---------------------------------
    QString err;
    const QByteArray rebuilt = enc.encodeBody(QStringLiteral("slrp"),
                                              pp.header, pp.subs, &err);
    CHECK(!rebuilt.isEmpty(), "parsed values re-encode");
    CHECK(rebuilt == frame.left(frame.size() - pp.tailBytes),
          "re-encoded body is byte-identical to the capture");

    // And the full builder path reproduces the whole frame, CRC included. The
    // MAC is a placeholder without a session key, so compare only up to it.
    const PacketBuilder::Result r = pb.build(QStringLiteral("slrp"), pp.header, pp.subs);
    CHECK(r.ok, "PacketBuilder rebuilds the frame");
    if (r.ok) {
        CHECK(r.frame.size() == frame.size(), "rebuilt frame is the same length");
        CHECK(r.pktLength == 110, "PKT_LENGTH recomputed to 110");
        CHECK(r.frame.left(frame.size() - 8) == frame.left(frame.size() - 8),
              "rebuilt body matches the capture up to the MAC");
        CHECK(r.roundTripped && r.crcVerified, "rebuilt frame self-verifies");
    }

    // ---- captype detection -------------------------------------------------
    CHECK(enc.detectCaptype(frame) == QLatin1String("slrp"),
          "detectCaptype identifies an untagged SLRP buffer");

    // ---- refusals ----------------------------------------------------------
    CHECK(!enc.parseBody(QStringLiteral("slrp"), QByteArray()).ok,
          "empty buffer is refused");
    CHECK(!enc.parseBody(QStringLiteral("slrp"), frame.left(6)).ok,
          "truncated header is refused, not zero-filled");
    CHECK(!enc.parseBody(QStringLiteral("nosuchpacket"), frame).ok,
          "unknown captype is refused");

    // A buffer whose sub-packet claims more bytes than are left must stop and
    // say so rather than walking off the end.
    {
        const Schema::ParsedPacket cut = enc.parseBody(QStringLiteral("slrp"),
                                                       frame.left(40));
        CHECK(cut.ok, "a short buffer still parses what it can");
        CHECK(!cut.notes.isEmpty(), "and reports where it stopped");
    }
}
