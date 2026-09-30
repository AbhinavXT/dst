# DL Console — session changes (patch 26)

Session key material is now tracked per loco, and the station id follows
last-write-wins like everything else.

| File | Change |
|---|---|
| `sessionkeystore.{h,cpp}` | one `LocoState` per loco; station id last-write-wins; `stationIdFromFrame()` |
| `capturedecoder.cpp` | `verifyMac()` is given the frame's loco id |
| `sessionkeydialog.cpp` | live status line uses `statusAll()` |
| `tests/test_sessionkeyloco.cpp` | new suite (28 checks) |
| `tests/test_sessionkeystore.cpp` | station-id block reworked for the new rule |

## Per loco

The store held one global state, so a capture carrying two locos mixed them:
loco 2's `@rand_num` overwrote loco 1's randoms, and the MAC badge on a loco 1
frame was then checked against a key derived from loco 2's material. Your
`replay/` directory is exactly this shape — loco 1 and loco 2 captures, stations
501 and 527 — so it was not hypothetical.

State is now keyed by the loco id in the capture tag (`@slrp_1_1` → loco 1).
Tags with no loco id file under -1, so a single-source log behaves as before.

Accessors take an optional `locoId`; `-1` (the default) means "the loco whose
state changed most recently", which keeps every existing call site correct on a
single-loco capture. `locos()` and `activeLoco()` expose the rest.

`verifyMac()` takes the loco explicitly, and `describe()` passes `c.locoId` —
the loco the frame was *captured on*, not whichever loco happened to transmit
last. On a two-loco log that is the difference between a real FAIL and a
meaningless one.

## Station id

Was first-write-wins: `m_stnFromStation` latched the first `SOURCE_STN_ID` seen
and never updated, so a loco handing over from station 501 to 527 kept the old
id — and the id parity (`loco_id + stn_id`) is what picks which key of the set
is used, so a stale station id silently selects the wrong key.

Now every station packet updates that loco's station id. One exception, which is
a judgement call worth knowing about: the ctrl id on a `@rand_num` tag is **not**
a station id (it is the ctrl index, typically 1). It is still accepted as a
placeholder until a station packet supplies a real `SOURCE_STN_ID`, but it no
longer overwrites one afterwards — otherwise the next `@rand_num` would drop the
id from 527 back to 1 and flip the parity. Last-write-wins holds among real
station ids.

### Cost

The old latch existed to avoid a schema decode per frame. Reading the id from
every station packet would have made that per-frame cost real, so the id is now
read straight from its bit range instead:

```
SLRP:  PKT_TYPE(4) + PKT_LENGTH(10) + FRAME_NUM(17) -> bit 31, 16 bits
AAP:   PKT_TYPE(4) + PKT_LENGTH( 7) + FRAME_NUM(17) -> bit 28, 16 bits
```

(`PKT_LENGTH` is the field whose width differs between the two.) Measured over
all 130,951 lines in `replay/`: **34 ms** total for the whole observe pass.

Hand-rolled bit offsets drift away from the schema, so `test_sessionkeystore`
pins `stationIdFromFrame()` against a frame the schema *encoder* built — if a
field width changes, the test fails rather than the store quietly reading
garbage.

## Verified against replay/

Feeding every line of every `.cap`: two locos tracked separately, each ending on
station 527, having followed 501 → 527 during the capture.

Full suite: **54 suites, 1685 checks, 0 failed.**

## The window rule now runs on the log's clock

`rederive()` passed `QDateTime::currentDateTime()` into `derive()`, so the
set-1-vs-set-2 decision was made against the wall clock. Live that is the same
thing as the frame time. Replaying a month-old `.cap` it is not: "now" falls
outside both windows, the rule drops to set 2, and every MAC on that log is then
checked against the wrong key — silently, because falling back to set 2 is also
what the rule does legitimately.

Each `LocoState` now carries `lastSeen`, the RTC of the newest frame that fed
it, and the derivation is evaluated at that time. `logTime()` exposes it.
`QDateTime::currentDateTime()` remains the fallback for a stream with no usable
RTC, and the Session Key dialog's own "Now:" field is untouched — that one is
the operator asking a hypothetical, which is a different question.

`test_sessionkeyloco` places a capture a year in the past with set 1's window
around it and asserts set 1 is chosen. Reverting to the wall clock fails three
of its checks.

## Still true, not changed

- The MAC badge is computed when a row is inspected, so after a re-key an older
  SLRP row reads FAIL rather than "no longer verifiable". That follows from
  last-write-wins.
