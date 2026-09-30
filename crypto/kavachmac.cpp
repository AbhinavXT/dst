#include "crypto/kavachmac.h"

#include "crypto/aes128.h"

#include <cstring>
#include <vector>

namespace {

inline void putLE16(uint8_t *p, uint16_t v)
{
    p[0] = uint8_t(v & 0xFF);
    p[1] = uint8_t(v >> 8);
}

}  // namespace

namespace KavachMac {

void buildRandomBlock(uint16_t locoRandom, uint16_t stnRandom, uint8_t out[16])
{
    // Layout from GenLCU_SCURandomNumber:  S L S L | L S L S
    putLE16(out + 0,  stnRandom);
    putLE16(out + 2,  locoRandom);
    putLE16(out + 4,  stnRandom);
    putLE16(out + 6,  locoRandom);
    putLE16(out + 8,  locoRandom);
    putLE16(out + 10, stnRandom);
    putLE16(out + 12, locoRandom);
    putLE16(out + 14, stnRandom);
}

void sessionKey(uint16_t locoRandom, uint16_t stnRandom,
                const uint8_t authKey[16], uint8_t out[16])
{
    uint8_t block[16];
    buildRandomBlock(locoRandom, stnRandom, block);
    // IV = 0, single block => one ECB block, matching Session_Key().
    Aes128::encryptBlock(authKey, block, out);
}

void cbcMacRaw(const uint8_t *data, size_t len,
               const uint8_t sessionKey[16], uint8_t rawTag4[4])
{
    const size_t rem     = len % 16;
    const size_t padding = rem ? (16 - rem) : 0;
    const size_t total   = len + padding;

    // Zero-padded working copy (an empty buffer yields a zero final block,
    // matching the firmware, whose enc_block starts zeroed and never runs).
    std::vector<uint8_t> buff(total, 0);
    if (len) { std::memcpy(buff.data(), data, len); }

    Aes128::Ctx ctx;
    Aes128::init(ctx, sessionKey);

    uint8_t chain[16] = {0};                 // IV = 0
    for (size_t off = 0; off < total; off += 16) {
        uint8_t block[16];
        for (int i = 0; i < 16; ++i) { block[i] = buff[off + i] ^ chain[i]; }
        Aes128::encryptBlock(ctx, block, chain);   // chain <- ciphertext
    }
    std::memcpy(rawTag4, chain + 12, 4);      // last 4 bytes of final block
}

void cbcMacWire(const uint8_t *data, size_t len,
                const uint8_t sessionKey[16], uint8_t wireTag4[4])
{
    uint8_t raw[4];
    cbcMacRaw(data, len, sessionKey, raw);
    // Check_MAC compares PP_HTONL(raw-as-uint32) against the received tag.
    // Read raw little-endian, emit big-endian — host-endianness independent.
    const uint32_t v = uint32_t(raw[0])        | (uint32_t(raw[1]) << 8)
                     | (uint32_t(raw[2]) << 16) | (uint32_t(raw[3]) << 24);
    wireTag4[0] = uint8_t(v >> 24);
    wireTag4[1] = uint8_t(v >> 16);
    wireTag4[2] = uint8_t(v >> 8);
    wireTag4[3] = uint8_t(v);
}

bool verify(const uint8_t *data, size_t len,
            const uint8_t sessionKey[16], const uint8_t targetWire4[4])
{
    uint8_t wire[4];
    cbcMacWire(data, len, sessionKey, wire);
    return std::memcmp(wire, targetWire4, 4) == 0;
}

#ifdef QT_CORE_LIB

QByteArray sessionKey(uint16_t locoRandom, uint16_t stnRandom,
                      const QByteArray &authKey16)
{
    if (authKey16.size() != 16) { return QByteArray(); }
    QByteArray out(16, Qt::Uninitialized);
    sessionKey(locoRandom, stnRandom,
               reinterpret_cast<const uint8_t *>(authKey16.constData()),
               reinterpret_cast<uint8_t *>(out.data()));
    return out;
}

QByteArray cbcMacRaw(const QByteArray &data, const QByteArray &sessionKey16)
{
    if (sessionKey16.size() != 16) { return QByteArray(); }
    QByteArray out(4, Qt::Uninitialized);
    cbcMacRaw(reinterpret_cast<const uint8_t *>(data.constData()),
              size_t(data.size()),
              reinterpret_cast<const uint8_t *>(sessionKey16.constData()),
              reinterpret_cast<uint8_t *>(out.data()));
    return out;
}

QByteArray cbcMacWire(const QByteArray &data, const QByteArray &sessionKey16)
{
    if (sessionKey16.size() != 16) { return QByteArray(); }
    QByteArray out(4, Qt::Uninitialized);
    cbcMacWire(reinterpret_cast<const uint8_t *>(data.constData()),
               size_t(data.size()),
               reinterpret_cast<const uint8_t *>(sessionKey16.constData()),
               reinterpret_cast<uint8_t *>(out.data()));
    return out;
}

bool verify(const QByteArray &data, const QByteArray &sessionKey16,
            const QByteArray &targetWire4)
{
    if (sessionKey16.size() != 16 || targetWire4.size() != 4) { return false; }
    return verify(reinterpret_cast<const uint8_t *>(data.constData()),
                  size_t(data.size()),
                  reinterpret_cast<const uint8_t *>(sessionKey16.constData()),
                  reinterpret_cast<const uint8_t *>(targetWire4.constData()));
}

#endif  // QT_CORE_LIB

}  // namespace KavachMac
