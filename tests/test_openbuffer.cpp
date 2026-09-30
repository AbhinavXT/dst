#include "testutil.h"

#include "logentry.h"
#include "packetmakerdialog.h"

#include <QByteArray>
#include <QString>

// =============================================================================
//  Opening a buffer somewhere else — the two pure pieces of it.
//
//  entryBufferText()               what a log row hands to the Decode
//                                  Workbench / Packet Maker.
//  PacketMakerDialog::parseBuffer() what the Packet Maker makes of it.
//
//  Composed, they are the whole path from a row to a filled form, so the last
//  check here runs the composition rather than trusting the two halves.
// =============================================================================

namespace {

LogEntryPtr capture(const QString &line)
{
    auto e = QSharedPointer<LogEntry>::create();
    e->header.source_id = 33;
    e->header.kvchId    = 1;
    e->epochMs          = 1750000000000LL;
    e->text             = line;
    e->cacheDerived();
    return e;
}

LogEntryPtr plain(const QString &text, const QByteArray &raw)
{
    auto e = QSharedPointer<LogEntry>::create();
    e->header.source_id = 33;
    e->header.kvchId    = 1;
    e->epochMs          = 1750000000000LL;
    e->text             = text;
    e->rawBytes         = raw;
    e->cacheDerived();
    return e;
}

QByteArray parsed(const QString &text, QString *hint = nullptr)
{
    QString local;
    return PacketMakerDialog::parseBuffer(text, hint ? hint : &local);
}

}  // namespace

TEST_SUITE(openbuffer)
{
    // ---- entryBufferText ---------------------------------------------------
    CHECK(entryBufferText(LogEntryPtr()).isEmpty(), "null entry -> empty");

    {
        auto e = plain(QStringLiteral("RAD IN frame 0x42"), QByteArray());
        CHECK(entryBufferText(e).isEmpty(),
              "a text-only row with no bytes has nothing to send");
    }

    {
        const QString line =
            QStringLiteral("@slrp_1_1 2026-06-18T16:08:16 6933 91 99 C5 DE");
        CHECK(entryBufferText(capture(line)) == line,
              "a capture row is handed over whole, @tag included");
    }

    {
        // Leading whitespace must not stop the @-line from being recognised,
        // or the row silently degrades to a header-bearing hex dump.
        const QString line =
            QStringLiteral("  @slrp_1_1 2026-06-18T16:08:16 6933 91 99 C5 DE");
        CHECK(entryBufferText(capture(line)).startsWith(QLatin1Char('@')),
              "leading whitespace does not hide the capture line");
    }

    {
        auto e = plain(QStringLiteral("RAD IN frame"),
                       QByteArray::fromHex("2165031300010052"));
        CHECK(entryBufferText(e) == QStringLiteral("21 65 03 13 00 01 00 52"),
              "a non-capture row falls back to the raw datagram as hex");
    }

    // ---- parseBuffer: the shapes a paste can take --------------------------
    CHECK(parsed(QStringLiteral("91 99 C5 DE")) == QByteArray::fromHex("9199C5DE"),
          "space-separated pairs");
    CHECK(parsed(QStringLiteral("9199C5DE")) == QByteArray::fromHex("9199C5DE"),
          "one unbroken hex run");
    CHECK(parsed(QStringLiteral("0x91,0x99,0xC5")) == QByteArray::fromHex("9199C5"),
          "0x-prefixed and comma-separated");

    // ---- parseBuffer: the capture preamble ---------------------------------
    //
    // The bug this pins: a decimal sequence number is also valid hex, so
    // "6933" used to be split into 0x69 0x33 and prepended to the frame — a
    // two-byte shift that parses into plausible-looking nonsense rather than
    // failing outright, which is the worst way for a decoder to be wrong.
    {
        QString hint;
        const QByteArray b = parsed(
            QStringLiteral("@slrp_1_1 2026-06-18T16:08:16 6933 91 99 C5 DE 04 1E"),
            &hint);
        CHECK(b == QByteArray::fromHex("9199C5DE041E"),
              "capture line: the sequence number is not prepended as bytes");
        CHECK(hint == QStringLiteral("slrp"), "capture line: @tag becomes the hint");
    }

    {
        // A sequence number that is NOT hex-shaped used to work by accident.
        // It has to keep working for the same reason the other one now does.
        const QByteArray b = parsed(
            QStringLiteral("@aap_1_1 2026-06-18T16:08:16 12 AA BB CC"));
        CHECK(b == QByteArray::fromHex("AABBCC"),
              "capture line with a short sequence number");
    }

    {
        // Only a real capture preamble is dropped. Someone typing a tag in
        // front of bare bytes must not lose the first two of them — which is
        // exactly what a position-based rule would do.
        const QByteArray b = parsed(QStringLiteral("@slrp 91 99 C5"));
        CHECK(b == QByteArray::fromHex("9199C5"),
              "an @tag with no timestamp/sequence keeps every byte");
    }

    {
        const QByteArray b = parsed(QStringLiteral("@slrp_1_1 not-a-time 6933 91 99"));
        CHECK(b == QByteArray::fromHex("693391" "99"),
              "no valid timestamp means no preamble, so nothing is dropped");
    }

    // ---- composed: row -> buffer text -> bytes ------------------------------
    {
        const QString line =
            QStringLiteral("@slrp_1_1 2026-06-18T16:08:16 6933 91 99 C5 DE 04 1E");
        QString hint;
        const QByteArray b = parsed(entryBufferText(capture(line)), &hint);
        CHECK(b == QByteArray::fromHex("9199C5DE041E"),
              "row -> Packet Maker delivers exactly the captured frame");
        CHECK(hint == QStringLiteral("slrp"),
              "row -> Packet Maker also delivers the packet type");
    }
}

// =============================================================================
//  splitBuffer — where the packet body starts inside a pasted buffer.
//
//  The bug this pins: the Packet Maker stripped MessageHeader::SIZE (8) from a
//  pasted capture, but arp and lsrp bodies start 10 bytes in (body_offset=80).
//  Two bytes out of step, and parseBody does NOT complain — it reads plausible
//  values, gates LSRP's FRAME_NUM-dependent health branch on the wrong number,
//  and fills the form. Field Sweep seeded through the same paste and stripped
//  nothing at all, then transmitted from those values.
// =============================================================================

#include "schema/schemaencoder.h"
#include "capturedecoder.h"

TEST_SUITE(splitbuffer)
{
    Schema::Encoder enc;
    QString err;
    CHECK(enc.load(QStringLiteral(":/schema/kavach.xml"), &err), "the schema loads");

    // A real captured lsrp datagram: 8-byte message header, two more bytes,
    // then the body at byte 10.
    const QString lsrpLine = QStringLiteral(
        "@lsrp_1_1 2026-06-27T14:02:27 21441 02 07 0A 00 27 00 00 00 0F 02 A3 AC 57 "
        "40 00 01 40 9F FB 41 E0 F0 7D 00 10 FC 30 15 20 8D 00 F2 F3 26 DD C6 ED 59 9B");
    const QByteArray lsrp = CaptureDecoder::parseLine(lsrpLine).bytes;
    CHECK(lsrp.size() == 39, "the sample frame is 39 B");

    {
        const auto sp = PacketMakerDialog::splitBuffer(enc, QStringLiteral("lsrp"), lsrp);
        CHECK(sp.stripped == 10, "ten bytes come off an lsrp capture, not eight");
        CHECK(sp.body == lsrp.mid(10), "and the body is what is left");
        CHECK(sp.hadHeader, "the message header was recognised");
        CHECK(sp.srcId == 2 && sp.destId == 7 && sp.seq == 0,
              "src, dest and seq are read out of it");
        CHECK(!sp.notes.isEmpty(), "and the split says what it did");

        // The check that matters: the body actually parses as the packet it
        // claims to be. Off by two, PKT_TYPE reads as 0 instead of 10.
        const Schema::ParsedPacket good = enc.parseBody(QStringLiteral("lsrp"), sp.body);
        CHECK(good.ok, "the split body parses");
        CHECK(good.header.value(QStringLiteral("PKT_TYPE")) == 10,
              "and PKT_TYPE is 10, as the frame's own tag says");

        const Schema::ParsedPacket old = enc.parseBody(QStringLiteral("lsrp"),
                                                       lsrp.mid(8));   // the old behaviour
        CHECK(old.ok, "the old eight-byte strip did not fail...");
        CHECK(old.header.value(QStringLiteral("PKT_TYPE")) != 10,
              "...it just read a different packet type");
    }

    // arp is the same shape.
    {
        const QString arpLine = QStringLiteral(
            "@arp_1_1 2026-06-27T14:02:26 21436 02 07 0D 00 27 00 00 00 0F 02 D3 AC 57 30 "
            "00 01 40 9F FB 47 D0 01 0E 04 1F C3 15 00 00 00 00 00 08 32 00 C9 5E DE 2F");
        const QByteArray arp = CaptureDecoder::parseLine(arpLine).bytes;
        const auto sp = PacketMakerDialog::splitBuffer(enc, QStringLiteral("arp"), arp);
        CHECK(sp.stripped == 10, "ten bytes come off an arp capture too");
        CHECK(enc.parseBody(QStringLiteral("arp"), sp.body).header
                  .value(QStringLiteral("PKT_TYPE")) == 13,
              "and the body reads as PKT_TYPE 13");
    }

    // slrp is captured with no envelope at all: body_offset is 0 and a capture
    // line carries no message header. Nothing may be stripped.
    {
        const QString slrpLine = QStringLiteral(
            "@slrp_1_1 2026-06-27T14:02:27 21440 90 81 8A E6 04 1E 80 00 05 38 60 00 08 "
            "01 60 94 BD 80 10 00 64 00 00 00 13 12 CB 04 DA A0 37 D0");
        const QByteArray slrp = CaptureDecoder::parseLine(slrpLine).bytes;
        const auto sp = PacketMakerDialog::splitBuffer(enc, QStringLiteral("slrp"), slrp);
        CHECK(sp.stripped == 0, "a captured slrp body is left alone");
        CHECK(!sp.hadHeader, "there is no message header on it");
        CHECK(sp.body == slrp, "the buffer is the body");
    }

    // A bare lsrp body — what this program itself transmits, with no envelope.
    // It must not have ten bytes taken off it, and the operator must be told
    // that the usual envelope was not there.
    {
        const QByteArray bare = lsrp.mid(10);
        const auto sp = PacketMakerDialog::splitBuffer(enc, QStringLiteral("lsrp"), bare);
        CHECK(sp.stripped == 0, "a bare body is not stripped");
        CHECK(!sp.hadHeader, "no message header was claimed");
        CHECK(sp.notes.join(" ").contains(QLatin1String("bare body")),
              "and the assumption is stated rather than made quietly");
    }

    // A buffer with the right length but the wrong message_id is not a header.
    {
        QByteArray forged = lsrp;
        forged[2] = char(0x63);                       // message_id 99
        const auto sp = PacketMakerDialog::splitBuffer(enc, QStringLiteral("lsrp"), forged);
        CHECK(!sp.hadHeader, "a wrong message_id is not treated as a header");
        CHECK(sp.stripped == 0, "so nothing is stripped on a guess");
    }

    // Shorter than the envelope: say so, hand back nothing, and let the caller
    // stop. Returning a truncated body here would be parsed as a real one.
    {
        const auto sp = PacketMakerDialog::splitBuffer(enc, QStringLiteral("lsrp"),
                                                       lsrp.left(6));
        CHECK(sp.stripped == 0, "nothing is stripped from a too-short buffer");
        CHECK(!sp.hadHeader, "and no header is claimed from it");
    }
}

// =============================================================================
//  A printed frame beats the datagram it arrived in.
//
//  Reported from the field: opening a non-capture row in the Decode Workbench
//  handed it the wrong bytes. The row read
//
//      2026-09-09T01:55:04.269  21_2  INFO  02 07 0D 00 27 00 …
//
//  and what reached the workbench began 15 65 97 76 00 02 00 30 32 20 30 37 —
//  the transport header, then 30 32 20 30 37, which is the ASCII CODES of the
//  characters "02 07".
//
//  entryBufferText preferred rawBytes for any non-@ row. rawBytes is the
//  datagram kept verbatim: transport header plus payload. That is right when
//  the payload is binary and wrong when the payload is a frame the backend
//  already printed as text — which is this case, and there the frame the
//  operator is pointing at is the one in the message.
// =============================================================================

TEST_SUITE(printedframe)
{
    // The reported row, and the datagram it would have arrived in: a header
    // followed by the ASCII of that same text.
    const QString printed = QStringLiteral(
        "02 07 0D 00 27 00 00 00 00 00 D3 AD 51 60 00 02 20 00 00 06 E0 00 "
        "02 00 00 00 00 00 00 00 00 00 08 32 00 1F FB 5F AB");

    QByteArray wire = QByteArray::fromHex("15659776000200");
    wire += printed.toLatin1();
    wire += '\n';

    const LogEntryPtr e = plain(printed, wire);
    const QString out = entryBufferText(e);

    CHECK(out == printed.toUpper(),
          "the frame in the message is what is handed over, not the datagram "
          "it arrived in");
    CHECK(!out.startsWith(QStringLiteral("15 65")),
          "the transport header is gone");
    CHECK(!out.contains(QStringLiteral("30 32 20 30 37")),
          "and so are the ASCII codes of the hex digits, which is what made "
          "the decode nonsense");

    // The whole point is that it now decodes. Composed rather than trusted.
    QString hint;
    const QByteArray parsed = PacketMakerDialog::parseBuffer(out, &hint);
    CHECK(!parsed.isEmpty(), "and the result parses as a buffer");
    CHECK(parsed.size() == 39,
          "with the byte count the operator counted on screen");
    CHECK(parsed.size() == 39 && parsed.at(0) == char(0x02)
              && parsed.at(1) == char(0x07),
          "starting where the message starts");

    // ---- rawBytes still wins when the payload is really binary -------------
    {
        QByteArray binary = QByteArray::fromHex("15659776000200");
        binary += QByteArray::fromHex("DEADBEEF");
        const LogEntryPtr b = plain(QStringLiteral("link down on 21_2"), binary);
        CHECK(entryBufferText(b).startsWith(QStringLiteral("15 65")),
              "a message that is prose leaves rawBytes as the only frame "
              "there is, header and all");
    }

    // ---- and a message that merely mentions hex is not a frame -------------
    //
    // The cost of guessing wrong is silent: the workbench would decode the
    // wrong bytes with nothing on screen saying so. So the test is strict.
    {
        const LogEntryPtr b =
            plain(QStringLiteral("CRC DE AD mismatch"), QByteArray("xy"));
        CHECK(!entryBufferText(b).startsWith(QStringLiteral("CRC")),
              "two hex-looking words inside a sentence are not a packet");
    }
    {
        const LogEntryPtr b =
            plain(QStringLiteral("02 07 0D 00 27 00 00"), QByteArray("xy"));
        CHECK(!entryBufferText(b).startsWith(QStringLiteral("02 07")),
              "and seven bytes is below the bar — no frame worth decoding "
              "here is that short, and the fallback is still correct");
    }
    {
        const LogEntryPtr b =
            plain(QStringLiteral("02 07 0D 00 27 00 00 0"), QByteArray("xy"));
        CHECK(!entryBufferText(b).startsWith(QStringLiteral("02 07")),
              "a ragged token means it was never a dump");
    }

    // A capture line still wins over everything, as before.
    {
        const LogEntryPtr c = capture(QStringLiteral("@lsrp 01 02 03"));
        CHECK(entryBufferText(c).startsWith(QLatin1Char('@')),
              "an @-capture line is handed over verbatim");
    }
}
