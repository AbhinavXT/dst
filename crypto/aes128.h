#ifndef AES128_H
#define AES128_H
//
//  Minimal AES-128 block encryption — just enough for the Kavach session-key
//  and CBC-MAC derivations (see kavachmac.h). Encrypt-only: neither the
//  session key (a single ECB-equivalent block) nor the CBC-MAC ever decrypt.
//
//  Deliberately dependency-free plain C++ so it can be unit-tested without Qt
//  and reused by any future packet-building code. Validated against the
//  FIPS-197 known-answer vector in tests/test_cbcmac.cpp.
//
//  This is a host-side analysis reimplementation of the firmware's
//  aes_128_cbc primitive. It is NOT hardened against side-channel attacks and
//  must never be used to protect live traffic — only to recompute and check
//  MACs on captures DLConsole already holds.
//
#include <cstddef>
#include <cstdint>

namespace Aes128 {

// Expanded key schedule for one 128-bit key: 11 round keys, 16 bytes each.
struct Ctx {
    uint8_t roundKeys[176];
};

// Expand a 16-byte key into ctx. Do this once, then encrypt many blocks —
// the CBC-MAC over a long buffer reuses one schedule.
void init(Ctx &ctx, const uint8_t key[16]);

// Encrypt one 16-byte block. `in` and `out` may alias.
void encryptBlock(const Ctx &ctx, const uint8_t in[16], uint8_t out[16]);

// Convenience: expand `key` and encrypt a single block in one call.
void encryptBlock(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);

}  // namespace Aes128

#endif  // AES128_H
