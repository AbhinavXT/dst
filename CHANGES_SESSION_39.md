# Session 39 — turnout entries are fixed width

| File | Change |
|---|---|
| `schema/kavach.xml` | turnout `start`/`release` no longer conditional; LC id 0 named |
| `schema/SLRP_FIELD_RULES.md` | new: the whole SLRP field map, from the loco firmware |
| `tests/test_turnoutwidth.cpp` | new: 11 checks |

From reviewing `fly.c`, the loco's own receiver for the regular packet.

---

# What the firmware says

The schema was right about almost everything: the 13-byte header and its ten
fields, sub-packet framing including the 8-byte CRC+MAC tail reserve, Movement
Authority with all four of its conditional fields, SSP, LC gate, track
condition, TSR, and tag linking — field for field and width for width.
Segment-length versus reference-offset is right in every case too.

One thing was wrong.

```xml
<!-- before -->
<field name="start"   bits="15" when="speed in 1..18"/>
<field name="release" bits="12" when="speed in 1..18"/>
```

`ProcessTurnout` consumes those two fields for **every** entry. Both of its
branches advance the cursor by `TO_DIFF_DIST_BITS + TO_REL_DIST_BITS` — one
reads the pair, the other skips it — so a turnout entry is always 5 + 15 + 12
bits whatever the speed.

Under the conditional reading an entry with a speed outside 1..18 was 27 bits
short, and **every entry after it in the sub-packet** was read from the wrong
bit.

## Why no capture ever showed it

All five turnout sub-packets in `replay/` carry speed 6 entries only, which is
inside 1..18, where the two readings agree exactly. Re-checked after the
change: all five decode identically to before.

That is the useful shape of this bug. It was not that the captures disagreed
with the schema — it is that the captures could not tell, and would not have
told us until a turnout with an unrestricted speed arrived during an
acceptance run.

So the test constructs the frame the traffic has not yet produced: an
out-of-range speed FIRST, then a normal entry after it. Under the old rule six
of its eleven checks fail; under the new rule none do. A test that passes
either way would have proved nothing, so that was checked both ways.

---

# LC entry with id 0

`ProcessLCGate` consumes such an entry's bits and skips it — it is a
placeholder, not a level crossing numbered 0. It now renders as
`0 (unused slot)` rather than `id=0`.

Track condition already reads correctly: type 0 maps to `0 (Not used)`.

---

# The reference

`schema/SLRP_FIELD_RULES.md` now holds the whole thing next to the schema it
justifies: framing, the acceptance rules a packet has to survive before any
field is used, the geometry that turns a distance into a location, and the
field layout of all eight sub-packets.

Two parts of it are worth knowing even where the schema is already right:

- **Segment length versus reference offset.** SSP and gradient entries CHAIN —
  each distance is the length of that segment, and entry *n* starts where
  *n−1* ended. LC, track condition, TSR and turnout are offsets from the
  reference tag. Same 15-bit field, different meaning.
- **What the loco discards.** Entries whose computed start or end comes out
  ≤ 0 are dropped; TSR entries are only acted on when `TSR_STATUS == 2`; and
  the placeholder entries above. A frame can be perfectly valid and still have
  most of its content ignored.

---

# For the firmware team, not for us

`ProcessTurnout`:

```c
uint8_t to_restricted = to_speed != 31;
uint8_t to_invalid    = to_speed != 0;

if (to_invalid) { /* skip entry */ continue; }
if (to_restricted) { /* read DIFF_DIST and REL_DIST */ }
```

`to_invalid` is true for every speed except 0, so only an entry with **speed
0** is ever acted on, and the branch that applies a turnout restriction is
unreachable for anything else. The captured traffic carries speed 6 turnouts,
which this discards.

It does not change the layout conclusion — the cursor advances identically
either way — but it may mean turnout speed restrictions are not reaching the
loco at all.

Full suite: **104 suites, 2658 checks, 0 failed.** Menu audit passed. Headless
smoke run clean.
