#ifndef KAVACHMAC_H
#define KAVACHMAC_H
//
//  Host-side reimplementation of the Kavach session-key derivation and packet
//  CBC-MAC, matching the loco firmware in encryption.c. DLConsole uses this to
//  recompute a packet's MAC from a capture and check it against the tag on the
//  wire, and (later) to stamp a valid MAC onto packets it builds and sends.
//
//  ── Session key ────────────────────────────────────────────────────────────
//  The 16-byte AES input block is tiled from the two 16-bit randoms
//  (GenLCU_SCURandomNumber): each cell below is one little-endian uint16.
//
//      bytes  0  2  4  6 | 8 10 12 14
//             S  L  S  L | L  S  L  S     (S = stn_random, L = loco_random)
//
//  session_key = AES128-Encrypt(key = auth_key, block), IV = 0. A single block
//  under CBC with a zero IV is exactly one ECB block, which is what the
//  firmware's Session_Key() does. The auth_key is chosen by the caller: key set
//  by validity window, and key[0]/key[1] by (loco_id + stn_id) % 2.
//
//  ── CBC-MAC ────────────────────────────────────────────────────────────────
//  key = session_key, IV = 0, buffer zero-padded to a 16-byte multiple, CBC
//  chained, and the tag is the LAST 4 BYTES of the final ciphertext block
//  (GenerateCBC_MAC). Two byte orderings matter:
//    - raw:  the 4 bytes as AES emits them (== the firmware's cbc_mac buffer)
//    - wire: those 4 bytes as they travel in the packet. Check_MAC compares
//            PP_HTONL(raw) against the received tag, i.e. the wire tag is the
//            big-endian reading — raw reversed. Verify against the wire form.
//
//  Caveats carried from the firmware, worth remembering when reading results:
//    * plain CBC-MAC (zero IV, zero padding, 4-byte truncation) — not CMAC;
//      zero padding is ambiguous and it is only sound for fixed-length input.
//    * Check_MAC computes the MAC over the effective packet length
//      (total − MAC(4) − CRC); pass that same slice here, not the whole frame.
//
#include <cstddef>
#include <cstdint>

#ifdef QT_CORE_LIB
#include <QByteArray>
#endif

namespace KavachMac {

// Build the 16-byte AES input block from the two randoms (see table above).
void buildRandomBlock(uint16_t locoRandom, uint16_t stnRandom, uint8_t out[16]);

// Derive the 16-byte session key from the two randoms and a 16-byte auth key.
void sessionKey(uint16_t locoRandom, uint16_t stnRandom,
                const uint8_t authKey[16], uint8_t out[16]);

// CBC-MAC of [data, len) under sessionKey. `rawTag4` is the AES-output order
// (matches firmware GenerateCBC_MAC); `wireTag4` is the big-endian wire form.
void cbcMacRaw (const uint8_t *data, size_t len,
                const uint8_t sessionKey[16], uint8_t rawTag4[4]);
void cbcMacWire(const uint8_t *data, size_t len,
                const uint8_t sessionKey[16], uint8_t wireTag4[4]);

// True if the wire tag recomputed over [data, len) equals targetWire4.
bool verify(const uint8_t *data, size_t len,
            const uint8_t sessionKey[16], const uint8_t targetWire4[4]);

#ifdef QT_CORE_LIB
// Qt-friendly overloads for DLConsole call sites. authKey / sessionKey must be
// 16 bytes; a wrong size yields an empty/false result rather than reading OOB.
QByteArray sessionKey(uint16_t locoRandom, uint16_t stnRandom,
                      const QByteArray &authKey16);
QByteArray cbcMacRaw (const QByteArray &data, const QByteArray &sessionKey16);
QByteArray cbcMacWire(const QByteArray &data, const QByteArray &sessionKey16);
bool       verify    (const QByteArray &data, const QByteArray &sessionKey16,
                      const QByteArray &targetWire4);
#endif

}  // namespace KavachMac

#endif  // KAVACHMAC_H
