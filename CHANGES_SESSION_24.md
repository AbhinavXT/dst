# DL Console — session changes (patch 24)

One fix on top of patch 23: the ARP / LSRP CRC.

| File | Change |
|---|---|
| `capturedecoder.cpp` | ARP/LSRP CRC trailer read big-endian, not little-endian |
| `schema/kavach.xml` | `anxc_jamcrc` declaration corrected (`xor_out`, `trailer_endian`) — descriptive only |
| `tests/test_crcheader.cpp` | new suite (19 checks), wired into `tests.pro` |

## The bug

`verifyCrc()` had the span right — JAMCRC from byte 10, the `PKT_TYPE` nibble,
skipping the 10-byte message header, which is a transport envelope and is not
covered — and ran it to the 4-byte trailer. But it compared against the trailer
read **little-endian**:

```cpp
c.crcOk = (jamcrc(b, 10, n - 14) == last4le());   // was
c.crcOk = (jamcrc(b, 10, n - 14) == last4be());   // now
```

Annexure-C packets store the word big-endian, like the rest of the frame, which
is msb-first throughout. AAP / SLRP / AEP were already `last4be()`; these two
were the odd ones out, so they reported FAIL on every frame.

## Verification

Not from the spec — from `replay/*.cap`, every frame in the directory:

| type | frames | `start=10` BE | `start=10` LE | `start=0` BE | `start=8` BE |
|---|---|---|---|---|---|
| LSRP | 3253 | **3253** | 0 | 0 | 0 |
| ARP | 9207 | **9207** | 0 | 0 | 91/200 |
| SLRP | 3340 | 0 | 0 | **all** | 0 |
| AAP | 46 | 0 | 0 | **all** | 0 |

The `start=8` column is a trap worth naming: the CRC init is 0, so leading zero
bytes do not change the result, and ARP bytes 8..9 are frequently `00 00`. A
span starting at byte 8 therefore passes for those frames by accident. It is not
evidence for that offset — and it is also a nice independent confirmation that
the init really is 0.

Byte 10 as the start is confirmed structurally too: at byte 10 the LSRP frame
reads `0xA3` → `PKT_TYPE = 0xA = 10`, matching `message_id = 10` in the header,
and matching the schema's `body_offset="80"`. At byte 8 it would read 0.

## Tests

`tests/test_crcheader.cpp` pins real frames from `replay/`: two LSRP, two ARP,
plus an SLRP and an AAP so a future edit to this branch cannot quietly take the
whole-frame types with it. Each verdict asserts `crcChecked` alongside `crcOk`,
so "passes" cannot mean "never checked". Corrupting a body byte must fail;
corrupting a **message header** byte must still pass — that check is what pins
the span start, and it is the half a wrong offset would break.

Confirmed the suite bites: reverted to `last4le()` and 4 checks fail; restored
and the full suite is 53 suites, 1653 checks, 0 failed.
