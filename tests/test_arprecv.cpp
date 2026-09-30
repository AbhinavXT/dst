#include "testutil.h"

#include "capturedecoder.h"
#include "schema/schemaencoder.h"

#include <QByteArray>
#include <QHash>
#include <QString>

// =============================================================================
//  An ARP received FROM another loco.
//
//  The transmitted form and the received form are the same packet behind
//  different envelopes. What a loco sends carries a 2-byte station id on the
//  end of the message header; what it receives from another loco does not, so
//  the packet begins two bytes earlier:
//
//      sent  02 07 0d 00 27 00 00 00 00 00  D3 AC ...   header 10, frame 39
//      recv  07 02 0d 00 25 00 00 00        D3 AC ...   header  8, frame 37
//
//  The decoder used to take 10 as a constant, so a received frame decoded two
//  bytes late: PKT_TYPE 1, PKT_LENGTH 64, SOURCE_LOCO_ID 139264 — a full set
//  of plausible values, every one of them wrong, and no error anywhere. That
//  is the failure this suite exists to prevent, which is why it checks the
//  VALUES and not merely that something came back.
//
//  WHY THE FRAME CAN BE TRUSTED TO SAY WHICH IT IS
//    It states its own shape twice: message_len (LE, bytes 4..5) counts the
//    whole frame, and PKT_LENGTH (bits 4..10 of the packet) counts the packet.
//    header = message_len - PKT_LENGTH, and the two have to agree. The captype
//    token is not consulted, so a received packet decodes correctly before
//    anyone has decided what to call it.
//
//  The buffer below is real, from the loco under test.
// =============================================================================

namespace {

// The received ARP, as it sits in the receive buffer — including the five
// bytes of padding after the CRC, which are part of the point: the CRC used to
// be read from the last four bytes of what arrived, and here those are zeroes.
const QString kRecvArp = QStringLiteral(
    "@arp_2_1 2026-06-27T14:02:26 100 "
    "07 02 0D 00 25 00 00 00 D3 AC 18 10 00 02 20 00 00 06 E0 00 02 00 00 00 "
    "00 00 00 00 00 00 08 32 00 EB F8 71 0B 00 00 00 00 00");

// A transmitted ARP from replay/, for the same checks on the 10-byte form.
const QString kSentArp = QStringLiteral(
    "@arp_1_1 2026-06-26T16:24:19 998 02 07 0D 00 27 00 00 00 00 00 D3 AE 6B 40 "
    "00 01 40 00 00 07 D0 00 04 00 00 00 00 00 00 00 00 00 0F 09 A0 C8 E6 1C 65");

QString field(const CaptureLine &c, const QString &name)
{
    const auto rows = CaptureDecoder::describe(c);
    for (const auto &r : rows) {
        if (r.field.trimmed() == name) { return r.value.trimmed(); }
    }
    return QStringLiteral("<absent>");
}

QString corrupt(const QString &line, int byteIndex, int mask = 0xFF)
{
    QStringList out = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const int at = 3 + byteIndex;
    if (at < out.size()) {
        bool ok = false;
        const int v = out.at(at).toInt(&ok, 16);
        out[at] = QStringLiteral("%1").arg(v ^ mask, 2, 16, QLatin1Char('0')).toUpper();
    }
    return out.join(QLatin1Char(' '));
}

}  // namespace

TEST_SUITE(arprecv)
{
    // ---- the envelope --------------------------------------------------------
    const CaptureLine rx = CaptureDecoder::parseLine(kRecvArp);
    CHECK(rx.valid, "the received frame parses");
    CHECK(rx.bytes.size() == 42, "42 bytes arrived: 8 header + 29 packet + 5 padding");

    CHECK(field(rx, QStringLiteral("msg.src_id"))  == QStringLiteral("7"),
          "source and destination are the other way round from a sent frame");
    CHECK(field(rx, QStringLiteral("msg.dest_id")) == QStringLiteral("2"), "dest 2");
    CHECK(field(rx, QStringLiteral("msg.message_id")) == QStringLiteral("13"),
          "message id 13, as on the sent side");
    CHECK(field(rx, QStringLiteral("msg.length")) == QStringLiteral("37"),
          "the length field counts header + packet, not what the buffer holds");
    CHECK(field(rx, QStringLiteral("msg.stn_id")).contains(QStringLiteral("not present")),
          "and says the station id is absent rather than reading two body bytes "
          "as if they were it");

    // ---- the body, at the offset the frame implies ---------------------------
    //
    // Decoded two bytes late these read 1, 64 and 139264 — which is what makes
    // a wrong offset dangerous rather than obvious.
    CHECK(field(rx, QStringLiteral("PKT_TYPE"))       == QStringLiteral("13"), "PKT_TYPE 13");
    CHECK(field(rx, QStringLiteral("PKT_LENGTH"))     == QStringLiteral("29"), "PKT_LENGTH 29");
    CHECK(field(rx, QStringLiteral("FRAME_NUM"))      == QStringLiteral("49537"), "FRAME_NUM");
    CHECK(field(rx, QStringLiteral("SOURCE_LOCO_ID")) == QStringLiteral("2"),
          "the OTHER loco's id — the whole reason this packet is interesting");
    CHECK(field(rx, QStringLiteral("LOCO_VERSION"))   == QStringLiteral("1"), "LOCO_VERSION 1");
    CHECK(field(rx, QStringLiteral("TRAIN_LENGTH")).startsWith(QStringLiteral("220")),
          "TRAIN_LENGTH 220 m");
    CHECK(field(rx, QStringLiteral("LOCO_MODE")).startsWith(QStringLiteral("1")),
          "LOCO_MODE 1 (Stand_By)");
    CHECK(field(rx, QStringLiteral("ABS_LOCO_LOC")).startsWith(QStringLiteral("0")),
          "ABS_LOCO_LOC 0");

    // ---- the CRC -------------------------------------------------------------
    //
    // Two assumptions had to go for this to pass: that the packet starts at
    // byte 10, and that the CRC is the last four bytes of what arrived. Here
    // the last four bytes are padding.
    CHECK(rx.crcChecked, "the received frame's CRC is checked at all");
    CHECK(rx.crcOk, "and passes — the span is the packet, not the buffer");

    {
        // Byte 12 is inside the packet on the received form and is not one of
        // the zero bytes, so a wrong span cannot pass this quietly.
        const CaptureLine bad =
            CaptureDecoder::parseLine(corrupt(kRecvArp, 12));
        CHECK(bad.crcChecked && !bad.crcOk,
              "corrupting a body byte fails the CRC — it is reading the packet");
    }
    {
        // The check that the span really starts at 8 and not at 10.
        //
        // It cannot be done by flipping byte 8 or the top of byte 9: those
        // carry PKT_TYPE and PKT_LENGTH, so corrupting them makes the frame
        // disagree with its own length and it is declined before any CRC is
        // attempted (checked below). The low five bits of byte 9 are FRAME_NUM
        // bits — inside the packet on the received form, inside the HEADER on
        // the sent form, and part of no length field either way. A span
        // starting at 10 would never read them.
        const CaptureLine bad =
            CaptureDecoder::parseLine(corrupt(kRecvArp, 9, 0x1F));
        CHECK(field(bad, QStringLiteral("PKT_LENGTH")) == QStringLiteral("29"),
              "the frame still declares the same packet length");
        CHECK(bad.crcChecked && !bad.crcOk,
              "but the CRC fails — the span begins at byte 8, not byte 10");
    }
    {
        // Corrupting PKT_LENGTH itself: the frame no longer agrees with its
        // own message_len, so it is declined rather than decoded somewhere
        // else. Being told nothing is better than being told something wrong.
        const CaptureLine bad =
            CaptureDecoder::parseLine(corrupt(kRecvArp, 8));
        CHECK(!bad.crcOk, "a frame whose length fields disagree claims no CRC pass");
        CHECK(field(bad, QStringLiteral("PKT_TYPE")) == QStringLiteral("<absent>"),
              "and is not decoded at whichever offset happens to fit");
    }

    // ---- the sent form is untouched ------------------------------------------
    const CaptureLine tx = CaptureDecoder::parseLine(kSentArp);
    CHECK(tx.bytes.size() == 39, "the sent frame is still 39 bytes");
    CHECK(field(tx, QStringLiteral("msg.stn_id")) == QStringLiteral("0"),
          "with a station id that is present and read");
    CHECK(field(tx, QStringLiteral("msg.length")) == QStringLiteral("39"),
          "and a length covering the 10-byte header");
    CHECK(field(tx, QStringLiteral("PKT_TYPE"))       == QStringLiteral("13"), "PKT_TYPE 13");
    CHECK(field(tx, QStringLiteral("PKT_LENGTH"))     == QStringLiteral("29"),
          "the same 29-byte packet as the received one");
    CHECK(field(tx, QStringLiteral("SOURCE_LOCO_ID")) == QStringLiteral("1"), "from loco 1");
    CHECK(tx.crcChecked && tx.crcOk, "and its CRC still passes");

    // ---- a frame that agrees with neither ------------------------------------
    //
    // Decline rather than guess. A guessed offset yields a full set of
    // believable values, which is the one outcome worse than no decode.
    {
        QStringList tok = kRecvArp.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        tok[3 + 4] = QStringLiteral("30");        // message_len 48: matches neither
        const CaptureLine odd =
            CaptureDecoder::parseLine(tok.join(QLatin1Char(' ')));
        CHECK(field(odd, QStringLiteral("msg.header"))
                  .contains(QStringLiteral("does not agree")),
              "a length that fits no header size is reported, not guessed at");
        CHECK(field(odd, QStringLiteral("PKT_TYPE")) == QStringLiteral("<absent>"),
              "and no body is decoded from an offset nobody could justify");
        CHECK(!odd.crcOk, "nor is a CRC claimed to pass");
    }
}

// =============================================================================
//  The @arprecv token.
//
//  Decoding was made to work from the frame's own length fields, so a received
//  ARP decodes correctly whatever it is tagged. The token is what lets it be
//  told apart on sight — one line is what this loco said, the other is what
//  another loco said — and what gives it a tab, a direction and a row of its
//  own everywhere those are listed.
// =============================================================================

TEST_SUITE(arprecvtoken)
{
    // The same real buffer, re-tagged: the bytes are identical, only the
    // capture token changes.
    const QString line = kRecvArp;
    QString tagged = line;
    tagged.replace(QStringLiteral("@arp_2_1"), QStringLiteral("@arprecv_2_1"));

    const CaptureLine c = CaptureDecoder::parseLine(tagged);
    CHECK(c.valid, "an @arprecv line parses");
    CHECK(c.type == CapType::ArpRecv, "and is its own type, not folded into arp");
    CHECK(QString::fromLatin1(CaptureDecoder::typeLabel(c.type))
              == QStringLiteral("arprecv"),
          "labelled as itself wherever a type name is shown");
    CHECK(CaptureDecoder::directionFor(c.type) == CapDir::In,
          "and counted as inbound — it is another loco talking, never this one");

    // The spelling varies in the field; all of them route to one decoder
    // rather than falling through to Unknown, which is what the auth-key
    // tokens taught.
    for (const QString &tok : { QStringLiteral("arprecv"),
                                QStringLiteral("arp_recv"),
                                QStringLiteral("rarp") }) {
        QString v = line;
        v.replace(QStringLiteral("@arp_2_1"), QStringLiteral("@%1_2_1").arg(tok));
        CHECK(CaptureDecoder::parseLine(v).type == CapType::ArpRecv,
              qPrintable(QStringLiteral("@%1 routes to the same decoder").arg(tok)));
    }

    // ---- the body still decodes, under the new tag --------------------------
    CHECK(field(c, QStringLiteral("SOURCE_LOCO_ID")) == QStringLiteral("2"),
          "the token changes what it is called, not how it is read");
    CHECK(c.crcChecked && c.crcOk, "and the CRC still passes");

    // ---- the schema answers to both tokens ----------------------------------
    //
    // One <packet> with two names. Two elements would be two copies of
    // twenty-one field definitions that have to stay identical, and the day
    // one of them was edited alone is the day a received ARP quietly decoded
    // differently from a sent one.
    {
        Schema::Encoder enc;
        QString err;
        CHECK(enc.load(QStringLiteral(":/schema/kavach.xml"), &err),
              "the built-in schema loads");
        const QStringList names = enc.packetNames();
        CHECK(names.contains(QStringLiteral("arp")), "arp is offered for building");
        CHECK(names.contains(QStringLiteral("arprecv")),
              "and so is arprecv — the Packet Maker lists both");

        const Schema::PacketInfo a = enc.packet(QStringLiteral("arp"));
        const Schema::PacketInfo r = enc.packet(QStringLiteral("arprecv"));
        CHECK(r.header.size() == a.header.size() && !r.header.isEmpty(),
              "both resolve to the same field list, because they are one packet");
        bool same = true;
        for (int i = 0; i < a.header.size(); ++i) {
            if (a.header.at(i).name != r.header.at(i).name
                || a.header.at(i).bits != r.header.at(i).bits) { same = false; }
        }
        CHECK(same, "field for field, name and width");
    }
}
