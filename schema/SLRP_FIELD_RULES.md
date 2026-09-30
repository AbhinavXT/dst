# SLRP field rules, from the loco firmware

Derived from `fly.c` — the loco's own receiver for the stationary→loco regular
packet. Where this disagrees with `kavach.xml`, **this is the authority**: it
is the code that has to accept the packet for the loco to act on it.

Everything below was read out of the parsing functions, not inferred from the
spec.

---

## 1. Framing

```
byte 0                    13                                    pkt_length
+-------------------------+-------------------------------------+--------+
| PACKET_HEADER (13 B)    | sub-packets, back to back            | CRC+MAC|
+-------------------------+-------------------------------------+--------+
                          |<------ pkt_length - 8 ------------->|   8 B
```

- **Header is 13 bytes.** `ProcessSkavachLkavachRegularPkt` reverses those 13
  bytes into a struct, which is what makes the whole packet **msb-first** on
  the wire.
- **The tail is 8 bytes**: CRC *and* MAC. The sub-packet walk runs while
  `offset < pkt_length - 8`.
- **Sub-packet type is the high nibble** of the sub-packet's first byte:
  `(buffer[off] & 0xF0) >> 4`.
- **Each sub-packet advances by its own declared length**, in bytes, from the
  7 bits after the type nibble — not by the size of its fixed part. MA makes
  this explicit: `offset = offset - sizeof(fixed) + sub_pkt_length_ma`.

| type | sub-packet |
|---|---|
| 0 | Movement Authority |
| 1 | Static Speed Profile |
| 2 | Gradient Profile |
| 3 | LC Gate Profile |
| 4 | Turnout Speed |
| 5 | Tag Linking |
| 6 | Track Condition |
| 7 | TSR |

Each sub-packet's fixed part is byte-reversed into `buffer_read` before its
bitfields are read; the variable entries after it are read straight out of the
unreversed buffer, msb-first. Same bit order either way.

## 2. Acceptance rules, before any field is used

A packet is dropped, silently, if any of these hold:

- `TimeDiffInSeconds(frame_num, now) <= -2` or `> 4` — more than 4 s old, or
  more than 2 s in the future. Five in a row raises
  `SKV_PKT_TIME_ERROR_CROSSED_THRESHOLD`.
- `frame_num` older than the previous accepted packet.
- `source_stn_ilc_ibs_id != own station_id` (unless the border-tag case).
- `dest_loco_id != own loco_id`.
- `last_ref_rfid == 0` → "SERIOUS ERROR RFID IS ZERO".
- that RFID not in the loco's tag queue, or already adjusted.
- loco in `STAND_BY_MODE` — frame number is recorded, nothing else.

When `ref_prof_id` changes, the **entire held profile is cleared** — SSP,
gradient, MA, LC, turnout, TSR, track conditions, target and section speeds.
`ref_prof_id == 0` means the station does not know the route ahead and the
current profile is deliberately kept.

## 3. Geometry — how a distance becomes a location

Everything is anchored on the reference tag:

```
ref  = absolute_location of last_ref_rfid       (from the loco's tag queue)
base = ref + dist_pkt_start        (nominal)
     = ref - dist_pkt_start        (reverse)
```

`dist_pkt_start` is **signed** (`int16_t`).

Two different entry shapes, and mixing them up is the easy mistake:

| sub-packet | entry distance means | location |
|---|---|---|
| SSP, Gradient | **segment length** — entries CHAIN | first starts at `base`; entry *n* starts where *n−1* ended |
| LC, Track Condition, TSR, Turnout | **offset from the reference** | `base ± entry_distance` |

For reverse running every `+` above becomes `−`, including the length that
produces `end_loc`.

Entries whose computed start or end is **≤ 0 are dropped** by the loco.

## 4. Field layouts

### Movement Authority — variable length, four conditions

Fixed part is 67 bits, then the conditional tail:

| field | bits | present when |
|---|---|---|
| SUB_PKT_TYPE | 4 | |
| SUB_PKT_LENGTH_MA | 7 | |
| FRAME_OFFSET | 4 | |
| DEST_LOCO_SOS | 4 | |
| TRAIN_SECTION_TYPE | 2 | |
| CUR_SIG_INFO | 17 | |
| CUR_SIG_ASPECT | 6 | |
| NEXT_SIG_ASPECT | 6 | |
| APPR_SIG_DIST | 15 | |
| AUTHORITY_TYPE | 2 | |
| AUTHORIZED_SPEED | 6 | `AUTHORITY_TYPE == 1` (OS) |
| MA_W_R_T_SIG | 16 | |
| REQ_SHORTEN_MA | 1 | |
| NEW_MA | 16 | `REQ_SHORTEN_MA == 1` |
| TRN_LEN_INFO_STS | 1 | |
| TRN_LEN_INFO_TYPE | 1 | `TRN_LEN_INFO_STS == 1` |
| REF_FRAME_NUM_TLM | 17 | `TRN_LEN_INFO_STS == 1` |
| REF_OFFSET_INT_TLM | 8 | `TRN_LEN_INFO_STS == 1` |
| NEXT_STN_COMM | 1 | |
| APPR_STN_ILC_IBS_ID | 16 | `NEXT_STN_COMM == 1` |

### Static Speed Profile

Fixed: `LM_SPEED_INFO_CNT`. Per entry:

| field | bits | present when |
|---|---|---|
| distance (segment length) | 15 | |
| class | 1 | |
| universal speed | 6 | `class == 0` |
| category A / B / C speeds | 6 each | `class == 1` |

Which category applies is chosen by the loco's own `train_type` — light
engine, ICF and LHB passenger take one of the three; the packet carries all
three regardless.

### Gradient

Per entry: distance (segment length), direction, value. Order confirmed;
widths not stated inline in the firmware.

### LC Gate — reference-relative

Per entry: distance 15, numeric id 10, alpha suffix 3, manning, class,
whistle-enabled, whistle-type.

**An entry with numeric id 0 is a placeholder**: the loco consumes its bits
and skips it. It is not an LC gate with id 0.

### Track Condition — reference-relative

Fixed: count. Per entry: type 4, start distance 15, length 15.

**An entry with type 0 is a placeholder**, consumed and skipped, exactly as
with LC id 0.

### TSR — reference-relative

Per entry: id 8, distance 15, length 15, class 1, then speeds as SSP
(universal or three categories), then whistle.

The whole entry list is **only acted on when `TSR_STATUS == 2`**.

### Turnout — see the discrepancy below

Header: type 4, length 7, count 2. Per entry: speed, then DIFF_DIST and
REL_DIST.

### Tag Linking

Fixed part ends at **bit 15** (type 4 + length 7 + `DIST_DUP_TAG` 4).

| field | bits |
|---|---|
| ROUTE_RFID_CNT | 6 |
| per tag: dist to next | 11 |
| per tag: next tag id | 10 |
| per tag: duplicate-tag direction | 1 |
| ABS_LOC_RESET | 1 |
| START_DIST_TO_LOC_RESET | 15 (when reset) |
| ADJ_LOCO_DIR | 2 (when reset) |
| ABS_LOCO_CORRECTION | 23 (when reset) |
| ADJ_LINE_CNT | 3 |
| per line: TIN | 9 |

Self block-section TIN sits at index 0.

---

## 5. Against `kavach.xml`

**Matching, field for field and width for width:** the 13-byte header and its
ten fields; sub-packet framing including the 8-byte tail reserve; Movement
Authority including all four conditional fields; SSP; LC gate; track
condition; TSR; tag linking. Segment-length versus reference-offset is right
in every case — `role="seglen"` on SSP and gradient, `role="start"` on the
rest.

**One structural disagreement — Turnout.**

`kavach.xml` has:

```xml
<field name="start"   bits="15" when="speed in 1..18"/>
<field name="release" bits="12" when="speed in 1..18"/>
```

The firmware consumes those two fields for **every** entry, whatever the
speed. Both of its branches advance the cursor by `TO_DIFF_DIST_BITS +
TO_REL_DIST_BITS` — one reads them, the other skips them — so a turnout entry
is fixed width.

Consequence if the schema is wrong: any turnout sub-packet carrying an entry
with speed outside 1..18 mis-parses that entry **and everything after it in
the sub-packet**.

Not yet seen in traffic. All five turnout sub-packets in `replay/` carry
speed 6 entries only, where the two readings agree, so the captures cannot
settle it.

## 6. One thing to raise with the firmware team

`ProcessTurnout` reads:

```c
uint8_t to_restricted = to_speed != 31;
uint8_t to_invalid    = to_speed != 0;

if (to_invalid) { /* skip this entry */ continue; }
if (to_restricted) { /* read DIFF_DIST and REL_DIST */ }
```

Because `to_invalid` is true for every speed except 0, only a turnout entry
with **speed 0** is ever acted on. The `to_restricted` branch and the
non-restricted branch below it are unreachable for anything else.

The captured traffic carries turnout entries with speed 6, which this logic
discards. The naming suggests the test was meant to be the other way round.

This does not change the layout conclusion above — the cursor advances
identically either way — but it does mean turnout speed restrictions may not
be reaching the loco at all.
