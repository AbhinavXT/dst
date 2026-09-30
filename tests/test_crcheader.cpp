#include "testutil.h"

#include "capturedecoder.h"

#include <QByteArray>
#include <QString>

// =============================================================================
//  CRC verification for the message-header-bearing captures (ARP, LSRP).
//
//  These two were reporting FAIL on every frame. The span was right — JAMCRC
//  from the PKT_TYPE nibble at byte 10, skipping the message header, which is
//  a transport envelope and is not covered — but the stored word was being
//  read little-endian. Annexure-C packets store it big-endian, like the rest
//  of the frame, which is msb-first throughout.
//
//  The frames below are real, lifted verbatim from replay/*.cap. The fix was
//  verified across every ARP and LSRP frame in that directory (9207 + 3253,
//  all PASS big-endian, none little-endian); these are the golden few that
//  keep it pinned.
//
//  Two traps this suite is built to avoid:
//
//    - "crcOk is true" means nothing on its own if the type is never checked
//      at all, so crcChecked is asserted alongside every verdict.
//    - The CRC init is 0, so leading zero bytes do not change the result.
//      Bytes 8..9 of an ARP frame are often 00 00, which makes a span starting
//      at byte 8 pass for those frames by accident. The corrupted-byte checks
//      below poke bytes that are inside the real span for every frame, so a
//      wrong offset cannot pass this suite quietly.
// =============================================================================

namespace {

// A capture line as it appears in a .cap file, minus the trailing CR.
CaptureLine decode(const QString &line)
{
    return CaptureDecoder::parseLine(line);
}

// Flip one byte of the hex payload of a capture line and re-decode, to prove
// the check is actually looking at the bytes. `byteIndex` is an index into the
// frame, not into the line.
CaptureLine decodeWithCorruptedByte(const QString &line, int byteIndex)
{
    const QStringList tok = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QStringList out = tok;
    const int hexStart = 3;                 // @tag, timestamp, seq
    const int at = hexStart + byteIndex;
    if (at < out.size()) {
        bool ok = false;
        const int v = out.at(at).toInt(&ok, 16);
        out[at] = QStringLiteral("%1").arg(v ^ 0xFF, 2, 16, QLatin1Char('0')).toUpper();
    }
    return CaptureDecoder::parseLine(out.join(QLatin1Char(' ')));
}

const QString kLsrp1 = QStringLiteral(
    "@lsrp_1_1 2026-06-26T16:29:16 1112 02 07 0A 00 27 00 00 00 0F 02 A3 AE 7D D0 "
    "00 01 40 9D 5C 81 20 90 7D 0C 90 40 50 0D 00 00 00 78 34 A8 37 56 19 1D 46");

const QString kLsrp2 = QStringLiteral(
    "@lsrp_1_1 2026-06-26T16:29:18 1139 02 07 0A 00 27 00 00 00 0F 02 A3 AE 7D F0 "
    "00 01 40 9D 63 41 40 A0 7D 0B 50 C0 50 0D 20 20 10 A4 00 79 73 87 2A 50 E2");

const QString kArp1 = QStringLiteral(
    "@arp_1_1 2026-06-26T16:24:19 998 02 07 0D 00 27 00 00 00 00 00 D3 AE 6B 40 "
    "00 01 40 00 00 07 D0 00 04 00 00 00 00 00 00 00 00 00 0F 09 A0 C8 E6 1C 65");

const QString kArp2 = QStringLiteral(
    "@arp_1_1 2026-06-26T16:24:21 1020 02 07 0D 00 27 00 00 00 00 00 D3 AE 6B 60 "
    "00 01 40 00 00 07 D0 00 04 00 00 00 00 00 00 00 00 00 0F 09 A0 40 79 8E A6");

// SLRP and AAP carry no message header and are checked over the whole frame.
// They are here so that a future change to the ARP/LSRP branch cannot quietly
// take these with it.
const QString kSlrp = QStringLiteral(
    "@slrp_1_1 2026-06-26T16:29:16 1122 91 31 CF BA 04 1E 80 00 04 40 AF 51 08 01 "
    "80 00 ED 80 10 00 87 2A 03 43 00 10 A1 0D 0C 58 21 43 09 BA 20 14 00 00 34 C6 "
    "52 A8 30 00 00 87 BC 04 99 E0 1A 3E 80 80 F1 02 84 38 0C 00 D0 60 E2 E0 00 03 "
    "43 00 F1 81 93 42 79 00 DE F2");

const QString kAap = QStringLiteral(
    "@aap_1_1 2026-06-26T16:29:14 1086 B3 4E 7D B0 20 F4 09 D4 90 00 00 5F 41 77 0F "
    "82 1F 00 55 AD 1E 9F 87 6E B9 B9");

}  // namespace

// A real 116-byte DMI frame (replay/loco_1_1_26062026_162418.cap): the form
// with the num-coaches byte at [109] and the CRC word at [110:114]. Every
// @dmi frame in replay/ is this length.
const QString kDmi116 = QStringLiteral(
    "@dmi_1_1 2026-06-26T16:24:19 997 AA AA 74 02 01 0A 6F 00 C9 08 00 00 00 00 00 00 00 00 00 00 1A 06 EA 07 10 18 13 00 00 00 00 00 00 00 00 40 00 00 00 00 00 00 01 00 00 20 00 00 00 00 00 00 00 00 00 00 00 50 00 00 00 00 00 00 00 00 00 00 00 00 FA 00 00 00 00 00 01 00 00 0D 6A 0D 00 00 00 00 00 00 00 00 00 00 00 00 F0 3C 00 F4 01 00 00 00 00 08 06 0F 00 00 01 00 D3 89 1D 43 BB BB");

// The legacy 115-byte form, rebuilt from the frame above: num coaches
// removed, CRC recomputed over [3:109] and stored at [109:113].
QString legacyDmi115()
{
    const QStringList tok = kDmi116.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QByteArray b;
    for (int i = 3; i < tok.size(); ++i) { b.append(char(tok.at(i).toInt(nullptr, 16))); }
    QByteArray out = b.left(109);                        // drop num coaches + old CRC
    const quint32 crc = CaptureDecoder::jamcrc(out, 3, 106);
    for (int i = 0; i < 4; ++i) { out.append(char((crc >> (8 * i)) & 0xFF)); }
    out.append(char(0xBB)).append(char(0xBB));
    QStringList hex = tok.mid(0, 3);
    for (char ch : out) {
        hex << QStringLiteral("%1").arg(quint8(ch), 2, 16, QLatin1Char('0')).toUpper();
    }
    return hex.join(QLatin1Char(' '));
}

TEST_SUITE(crcheader)
{
    // ---- LSRP --------------------------------------------------------------
    {
        const CaptureLine c = decode(kLsrp1);
        CHECK(c.valid, "LSRP line parses");
        CHECK(c.type == CapType::LSRP, "and is recognised as LSRP");
        CHECK(c.bytes.size() == 39, "39 bytes: 10 header + 25 packet + 4 CRC");
        CHECK(c.crcChecked, "LSRP CRC is checked at all");
        CHECK(c.crcOk, "LSRP CRC passes (big-endian stored word)");
    }
    {
        const CaptureLine c = decode(kLsrp2);
        CHECK(c.crcChecked && c.crcOk, "a second real LSRP frame passes too");
    }
    {
        // Byte 12 is inside the packet body on every frame — not a header byte,
        // not one of the often-zero bytes at 8..9.
        const CaptureLine c = decodeWithCorruptedByte(kLsrp1, 12);
        CHECK(c.crcChecked, "a corrupted LSRP frame is still checked");
        CHECK(!c.crcOk, "and fails — the check reads the body, not a constant");
    }
    {
        // The message header is NOT covered: corrupting it must not change the
        // verdict. This is the half of the rule that the span start encodes.
        const CaptureLine c = decodeWithCorruptedByte(kLsrp1, 6);
        CHECK(c.crcChecked && c.crcOk,
              "corrupting the message header leaves the LSRP CRC passing");
    }

    // ---- ARP ---------------------------------------------------------------
    {
        const CaptureLine c = decode(kArp1);
        CHECK(c.valid, "ARP line parses");
        CHECK(c.type == CapType::ARP, "and is recognised as ARP");
        CHECK(c.crcChecked, "ARP CRC is checked at all");
        CHECK(c.crcOk, "ARP CRC passes (big-endian stored word)");
    }
    {
        const CaptureLine c = decode(kArp2);
        CHECK(c.crcChecked && c.crcOk, "a second real ARP frame passes too");
    }
    {
        const CaptureLine c = decodeWithCorruptedByte(kArp1, 11);
        CHECK(c.crcChecked && !c.crcOk, "a corrupted ARP body fails");
    }
    {
        const CaptureLine c = decodeWithCorruptedByte(kArp1, 3);
        CHECK(c.crcChecked && c.crcOk,
              "corrupting the message header leaves the ARP CRC passing");
    }
    {
        // The trap described at the top: byte 10 is the first covered byte. If
        // the span ever slides back to byte 8, an ARP frame with 00 00 there
        // still passes — but corrupting byte 10 must fail under any correct
        // span, and under a span starting at 12 it would not.
        const CaptureLine c = decodeWithCorruptedByte(kArp1, 10);
        CHECK(c.crcChecked && !c.crcOk,
              "the PKT_TYPE byte is inside the covered span");
    }

    // ---- unchanged neighbours ----------------------------------------------
    {
        const CaptureLine c = decode(kSlrp);
        CHECK(c.crcChecked && c.crcOk, "SLRP still passes (whole frame, big-endian)");
    }
    {
        const CaptureLine c = decode(kAap);
        CHECK(c.crcChecked && c.crcOk, "AAP still passes (whole frame, big-endian)");
    }

    // ---- DMI: both frame lengths (session 65) --------------------------------
    // Before 65 only 115 B was accepted, every real capture is 116 B, and the
    // result was crcChecked == false on all of them — no FAIL, just no check.
    {
        const CaptureLine c = decode(kDmi116);
        CHECK(c.bytes.size() == 116, "the real DMI frame is 116 bytes");
        CHECK(c.crcChecked, "116-byte DMI CRC is checked at all");
        CHECK(c.crcOk, "and passes (JAMCRC over [3:110], LE word at [110:114])");
    }
    {
        const CaptureLine c = decodeWithCorruptedByte(kDmi116, 109);
        CHECK(c.crcChecked && !c.crcOk,
              "the num-coaches byte is inside the covered span");
    }
    {
        const CaptureLine c = decodeWithCorruptedByte(kDmi116, 50);
        CHECK(c.crcChecked && !c.crcOk, "a corrupted DMI body fails");
    }
    {
        const CaptureLine c = decode(legacyDmi115());
        CHECK(c.bytes.size() == 115, "the legacy DMI frame is 115 bytes");
        CHECK(c.crcChecked && c.crcOk, "the 115-byte form still checks and passes");
    }
    {
        const CaptureLine c = decodeWithCorruptedByte(legacyDmi115(), 50);
        CHECK(c.crcChecked && !c.crcOk, "and a corrupted 115-byte body fails");
    }
}
