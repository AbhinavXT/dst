#include "testutil.h"

#include "crypto/aes128.h"
#include "crypto/kavachmac.h"

#include <QByteArray>

namespace {
QByteArray hx(const char *h) { return QByteArray::fromHex(h); }
QByteArray toHex(const uint8_t *b, int n)
{
    return QByteArray(reinterpret_cast<const char *>(b), n).toHex();
}
}  // namespace

TEST_SUITE(cbcmac)
{
    // ---- AES-128 known-answer (FIPS-197 C.1) -----------------------------
    // If this fails, nothing else in this suite means anything.
    {
        const QByteArray key = hx("000102030405060708090a0b0c0d0e0f");
        const QByteArray pt  = hx("00112233445566778899aabbccddeeff");
        uint8_t ct[16];
        Aes128::encryptBlock(reinterpret_cast<const uint8_t *>(key.constData()),
                             reinterpret_cast<const uint8_t *>(pt.constData()),
                             ct);
        CHECK(toHex(ct, 16) == "69c4e0d86a7b0430d8cdb78070b4c55a",
              "FIPS-197 AES-128 vector");
    }

    // ---- random block layout: S L S L | L S L S, each u16 little-endian --
    {
        uint8_t blk[16];
        KavachMac::buildRandomBlock(/*loco*/0x1234, /*stn*/0xABCD, blk);
        CHECK(toHex(blk, 16) == "cdab3412cdab34123412cdab3412cdab",
              "random block tiling matches GenLCU_SCURandomNumber");
    }

    // ---- session key = AES(auth_key, block), IV 0 ------------------------
    // Cross-checked against an independent AES-ECB reference.
    {
        const QByteArray ak = hx("1234567890abcdef1234567890abcdef");
        const QByteArray sk = KavachMac::sessionKey(0x1234, 0xABCD, ak);
        CHECK(sk.toHex() == "882a8e3de19b4a473468b0b2d185f1ea",
              "session key derivation");
        // A different pair of randoms must give a different key.
        CHECK(KavachMac::sessionKey(0x0001, 0x0002, ak) != sk,
              "session key depends on the randoms");
    }

    // ---- CBC-MAC raw vs wire, and verify() -------------------------------
    {
        const QByteArray sk  = hx("882a8e3de19b4a473468b0b2d185f1ea");
        const QByteArray msg = QByteArrayLiteral("KAVACH-ATP-PACKET-verify-me-123"); // 31 B
        const QByteArray raw  = KavachMac::cbcMacRaw(msg, sk);
        const QByteArray wire = KavachMac::cbcMacWire(msg, sk);
        CHECK(raw.toHex()  == "44c2f08b", "raw tag = last 4 bytes of final block");
        CHECK(wire.toHex() == "8bf0c244", "wire tag = big-endian (raw reversed)");

        // verify() takes the WIRE tag, as it sits in the packet.
        CHECK(KavachMac::verify(msg, sk, hx("8bf0c244")), "verify accepts good tag");
        CHECK(!KavachMac::verify(msg, sk, hx("44c2f08b")),
              "verify rejects the raw ordering (a real capture is big-endian)");
        CHECK(!KavachMac::verify(msg, sk, hx("8bf0c245")), "verify rejects a bad tag");
    }

    // ---- an exact-block-multiple buffer adds no padding block ------------
    {
        const QByteArray sk = hx("882a8e3de19b4a473468b0b2d185f1ea");
        const QByteArray m16 = hx("00112233445566778899aabbccddeeff");   // 16 B
        const QByteArray padded = m16 + QByteArray(16, '\0');            // 32 B
        // Zero padding is ambiguous only across different lengths; a 16-byte
        // message and a 32-byte one differ, so their MACs must differ.
        CHECK(KavachMac::cbcMacWire(m16, sk) != KavachMac::cbcMacWire(padded, sk),
              "16 B and its zero-extension are distinct inputs");
    }

    // ---- bad-size inputs fail closed -------------------------------------
    {
        CHECK(KavachMac::sessionKey(1, 2, hx("00")).isEmpty(),
              "short auth key yields empty, not OOB read");
        CHECK(KavachMac::cbcMacWire(QByteArrayLiteral("x"), hx("00")).isEmpty(),
              "short session key yields empty");
        CHECK(!KavachMac::verify(QByteArrayLiteral("x"),
                                 hx("882a8e3de19b4a473468b0b2d185f1ea"),
                                 hx("0011")),
              "wrong target size fails closed");
    }
}
