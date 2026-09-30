# Session 66 — SLRP geometry settled; gate fully green

Confirmed (Abhinav): the start signal sits at LAST_REF_RFID's absolute
location plus DIST_PKT_START, the signed distance counted along the direction
of travel — `refAbs + travel · dps` — and every packet distance, the tag list
included, is counted from the start signal the same way.

That is what `engine.py` and `schemadecoder.cpp` already did. The oracle
(`kschema_oracle.decodeSlrp`) was the stale one on both counts:

| | before | now |
|---|---|---|
| start signal | `ref − dps` (Nominal), `ref + dps` (Reverse) | `ref + travel · dps` |
| tag list from | `refAbs` | the start signal (`blockAbs`) |

No decoder output changes; only the oracle.

Scored against the engine on the 18 mode-B frames that carry absolute
locations (16 with DIST_PKT_START ≠ 0: 12 Nominal, 4 Reverse): the old
oracle differed on all 16; with the confirmed model, 0.

## Verified

`./verify.sh` — **14 / 14 stages ok, exit 0**: all 11 golden validators
(`validate_slrp` 6680/6680 across both modes), unit suite 120 suites /
3140 checks / 0 failed, menu audit 59 / 0, headless smoke alive at timeout.
