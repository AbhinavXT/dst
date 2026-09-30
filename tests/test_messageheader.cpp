#include "testutil.h"

#include "messageheader.h"

#include <QByteArray>

TEST_SUITE(messageheader)
{
    // ---- message_id map + applies() --------------------------------------
    CHECK(MessageHeader::messageId("slrp") == 9,  "slrp -> 9");
    CHECK(MessageHeader::messageId("lsrp") == 10, "lsrp -> 10");
    CHECK(MessageHeader::messageId("aap")  == 11, "aap  -> 11");
    CHECK(MessageHeader::messageId("aep")  == 12, "aep  -> 12");
    CHECK(MessageHeader::messageId("arp")  == 13, "arp  -> 13");
    CHECK(MessageHeader::messageId("rfid") == -1, "rfid has no header");
    CHECK(MessageHeader::applies("slrp"), "slrp carries a header");
    CHECK(!MessageHeader::applies("nms hlth"), "nms has no header");

    // ---- exact byte layout, little-endian --------------------------------
    // src=7 dest=2 msg_id=9 (slrp), packetLen=33 -> msg_len=33+8=41=0x29,
    // seq=0x0102. Layout: 07 02 | 09 00 | 29 00 | 02 01.
    {
        const QByteArray h = MessageHeader::build("slrp", 33, 0x0102);
        CHECK(h.size() == MessageHeader::SIZE, "header is 8 bytes");
        CHECK(h.toHex() == "0702090029000201", "8-byte little-endian layout");
    }

    // ---- message_length = packet + header, both bytes ---------------------
    {
        // a length that spans both bytes: packetLen=300 -> msg_len=308=0x0134
        const QByteArray h = MessageHeader::build("arp", 300, 0);
        CHECK(quint8(h[0]) == 7 && quint8(h[1]) == 2, "default src/dest 7/2");
        CHECK(quint8(h[2]) == 13 && quint8(h[3]) == 0, "arp msg_id=13, LE");
        CHECK(quint8(h[4]) == 0x34 && quint8(h[5]) == 0x01, "msg_len 0x0134 LE");
        CHECK(quint8(h[6]) == 0 && quint8(h[7]) == 0, "seq 0 LE");
    }

    // ---- custom src/dest and a wrapping seq ------------------------------
    {
        const QByteArray h = MessageHeader::build("lsrp", 10, 0xFFFF, 0x11, 0x22);
        CHECK(quint8(h[0]) == 0x11 && quint8(h[1]) == 0x22, "custom src/dest");
        CHECK(quint8(h[6]) == 0xFF && quint8(h[7]) == 0xFF, "seq 0xFFFF LE");
    }

    // ---- a type with no header yields nothing ----------------------------
    CHECK(MessageHeader::build("rfid", 10, 1).isEmpty(), "no header => empty");

    // ---- arprecv is sent exactly as arp ----------------------------------
    //
    // Not an oversight. DLConsole is always the peer TRANSMITTING to the loco,
    // so what it sends is by definition what the loco receives — and the
    // received form is the 8-byte header this builder already produces, with
    // the default 7 -> 2 that the real receive buffer shows. There is nothing
    // to add for arprecv; the check is that nobody later "fixes" it into a
    // difference that would put a station id on a frame the loco is meant to
    // receive without one.
    {
        const QByteArray a = MessageHeader::build("arp",     29, 0);
        const QByteArray r = MessageHeader::build("arprecv", 29, 0);
        CHECK(!r.isEmpty(), "arprecv is sent with a header");
        CHECK(a == r, "and it is byte for byte the one arp gets");
        CHECK(r.size() == 8, "eight bytes: no station id on this path");
        CHECK(quint8(r[2]) == 13, "the same message id — it is the same packet");
        CHECK(quint8(r[4]) == 37 && quint8(r[5]) == 0,
              "msg_len 37 for a 29-byte packet, which is what the real "
              "received buffer carries");
    }

    // ---- and the preview says which form it is ---------------------------
    {
        CHECK(MessageHeader::shapeNote("arprecv").contains("RECEIVED"),
              "the Packet Maker explains the header shape rather than leaving "
              "two identical entries looking like a bug");
        CHECK(MessageHeader::shapeNote("arp") == MessageHeader::shapeNote("arprecv"),
              "the same note for both, because it is the same datagram");
        CHECK(MessageHeader::shapeNote("rfid").isEmpty(),
              "and nothing is said where there is nothing to say");
    }
}
