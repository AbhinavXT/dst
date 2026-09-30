# Packet schema — how to add fields and structs

The decoder reads **`kavach.xml`**. You change packet layouts by editing that
file. No C++ needed. The order of lines top-to-bottom is the order of bits on
the wire.

---

## 1. Add a field to an existing struct

Find the `<struct>` (or `<packet>`) and add one `<field/>` line where the field
sits on the wire:

```xml
<field name="MY_NEW_FIELD" bits="5"/>
```

That's it. `name` is what shows in the inspector, `bits` is the width.

### Field options

| attribute   | meaning                                                        | example |
|-------------|---------------------------------------------------------------|---------|
| `name`      | label shown in the inspector                                  | `name="APPR_SIG_DIST"` |
| `bits`      | width on the wire                                              | `bits="15"` |
| `signed`    | `true` = two's-complement signed value                        | `signed="true"` |
| `unit`      | appended to the value (display only)                          | `unit="m"` |
| `enum`      | map the number to a name via an `<enum>` (see §4)             | `enum="speed6"` |
| `id`        | remember this value so a later `when`/`count` can use it      | `id="cls"` |
| `when`      | only read this field if a condition holds (see §3)           | `when="cls==1"` |

To skip reserved bits, use `<pad bits="3"/>` (read and discarded, no row shown).

---

## 2. Add a whole new struct

Copy any `<struct>…</struct>` block, rename it, and list its fields:

```xml
<struct name="MyProfile">
  <field name="COUNT" bits="5" id="cnt"/>
  <repeat name="item" count="cnt">
    <field name="distance" bits="15" unit="m"/>
    <field name="speed"    bits="6"  enum="speed6"/>
  </repeat>
</struct>
```

If the struct is a **sub-packet** of SLRP, also add one line to the
`<subpackets>` block of the packet so the dispatcher knows which type byte maps
to it:

```xml
<case type="8" struct="MyProfile"/>
```

---

## 3. Repeating entries and conditions

**Repeat N times**, where N came from an earlier field tagged `id="cnt"`:

```xml
<field name="COUNT" bits="5" id="cnt"/>
<repeat name="speed" count="cnt">
  ... fields for one entry ...
</repeat>
```

**Conditional field** — read only when a previous `id` matches:

```xml
<field name="class"     bits="1" id="cls"/>
<field name="universal" bits="6" enum="speed6" when="cls==0"/>
<field name="A"         bits="6" enum="speed6" when="cls==1"/>
```

`when` supports `==  !=  >  <  >=  <=` against a whole number.

---

## 4. Value maps (enums)

Define once under `<enums>`, reference by name from any field:

```xml
<enum name="gradDir">
  <map v="0" label="downhill"/>
  <map v="1" label="uphill"/>
</enum>
```

- `<map v="3" label="SR Authority"/>` — exact value to text.
- `<map from="1" to="50" formula="v*5" unit="km/h"/>` — a range with a formula
  (`v*K` or `v+K`) and unit, e.g. value 8 → `40 km/h`.
- `<map from="51" to="61" label="reserved"/>` — a range to one label.

---

## 5. Packets, wire order and CRC

A `<packet>` is a top-level frame. `match="pkt_type==9"` selects it by the first
4 bits. `wire="msb-first"` (radio / Annexure-C) or `wire="lsb-first"`
(RFID / Annexure-D) sets bit order. `crc="anxc_jamcrc"` points at a `<crc>`
definition under `<crcs>` (poly / init / reflect / xor-out / trailer width).

---

## 6. Worked example — the line you'd actually add

Say a new amendment adds a 4-bit `WEATHER_CODE` right after `PKT_DIR` in SLRP.
You add exactly one line:

```xml
<field name="PKT_DIR" bits="2" enum="pktDir" id="pkt_dir"/>
<field name="WEATHER_CODE" bits="4"/>      <!-- new -->
<pad bits="3"/>                            <!-- shrink/adjust padding to stay byte-aligned -->
```

Reload — `WEATHER_CODE` now appears in the inspector, and every field after it
stays correctly aligned because the engine walks bits in order.

---

## What the engine handles today

Sequential bit fields (MSB- or LSB-first), signed/unsigned, units, enums with
ranges+formulas, `id` capture, `when` conditions, `repeat count="id"`, byte-
aligned sub-packet dispatch, and per-packet CRC selection. It does **not** yet do
arbitrary arithmetic in `when`/`count` (only an id vs a literal), nested repeats
keyed off mid-entry counts, or cross-field formulas — tell me if a packet needs
those and I'll extend the engine (the schema grammar already has room).

---

## v2 grammar additions (SLRP-full)

These extend the engine to full parity with the hand-written `kschema` tables.
All are validated row-for-row against real captures by `validate_slrp.py`.

### Fields are addressable by name
Every `<field name="X">` stores its value under `X`, so `when=` / `count=` can
reference a field by its name directly (no separate `id=` needed). `id=` still
works as an alias.

### `when` ranges
In addition to `id OP literal`, conditions support an inclusive range:
```xml
<field name="start" bits="15" when="speed in 1..18"/>
```

### Hidden fields — `hide="true"`
Reads and captures the value (so later `when=` can use it) but emits no row /
token. Use for discriminators like a 1-bit `class` or a reserved `LOC_RESET`.

### Entry templates — `tmpl="..."`
Inside `<repeat>`, each field renders through its `tmpl` (with `%s` = the
value), and the tokens are joined into one line. Without `tmpl` a field renders
as `name=value`. Example: `tmpl="@%s m"` → `@639 m`, `tmpl="universal %s"` with
an enum → `universal 80 km/h`.

### Abs-location roles — `role="seglen|start|len|taghop"`
Composes an absolute track location from the header (`LAST_REF_RFID` +
`DIST_PKT_START` + `PKT_DIR`) and the caller's `tagLoc` ref→abs map, then
appends a suffix to the entry:
- `seglen` — running per-segment length → `[a → b m]` (chains across entries)
- `start` + `len` together → `[start → start+len m]`
- `start` alone → `abs=… m`
- `taghop` — running hop distance from the ref → `abs=… m`
Suffixes only appear when the ref RFID is present in `tagLoc`.

### Richer enums
```xml
<enum name="speed6">
  <map v="0" label="0 (dead stop)"/>               <!-- exact value -->
  <map from="1" to="50" formula="v*5" unit="km/h"/> <!-- range: v*K / v+K / v-K -->
  <map from="51" to="61" label="%v (reserved)"/>    <!-- %v = raw value -->
  <map v="62" label="8 km/h (night override)"/>
  <map default="true" label="%v"/>                  <!-- fallback -->
</enum>
```
Label placeholders: `%v` = raw value, `%f` = formula result. A `<map>` with no
`label` but a `formula`/`unit` renders `<result> <unit>`.

### Composite fields — `format="name"`
The one escape hatch for fields that are a bit-decomposition into sparse
sub-tables (currently `sigInfo`). Registered in code:
`engine.py FORMATTERS` and `schemadecoder.cpp Decoder::composite`. Prefer plain
enums; reach for `format=` only when a field genuinely can't be a flat map.

### Sub-packet tail
After the `<subpackets>` loop, a trailing 4 bytes render as `MAC_CODE`; any
other remainder renders as `unparsed (raw)`.

### Validation workflow (unchanged, now mandatory)
1. Edit `kavach.xml`.
2. `python3 validate_slrp.py` — diffs the XML engine vs the `kschema_oracle.py`
   port over every real SLRP frame, in no-abs and abs modes. Must be all-match.
3. Only then trust `schemadecoder.cpp` (it mirrors `engine.py`; no Qt compiler
   in the sandbox, so the Python is the gate).

---

## aap / aep additions

### Packet selection by capture token — `match="captype==X"`
Packets now select on the capture `@token` (e.g. `captype==aap`), passed in by
the caller (`decodeSLRP`→"slrp", `decodeAAP`→"aap", `decodeAep`→"aep"). This
avoids top-nibble `pkt_type` collisions across the 16 types. The old
`pkt_type==N` match still works as a fallback when no captype is given.

### Hex fields — `hex="W"`
Renders a field as `0x` + lowercase hex, zero-padded to `W` digits
(`hex="8"` → `0x0001abcd`). Used by AAP `STN_RND_NUM_RS` (`hex="4"`) and
`MAC_CODE` (`hex="8"`).

Validated by `validate_aap_aep.py` (XML engine vs the decodeAAP/decodeAep hand
decoders) — 46/46 real frames match. Note: schema flat rows carry a uniform
2-space indent; the aap/aep hand decoders used bare names, so the only visible
change is that indent.

---

## dip / dop additions (digital I/O)

These are LSB-first bit-flag tables at byte 0 with **no CRC trailer**.

### No-CRC frames — `trailer_bytes="0"`
By default a packet reserves a 4-byte CRC (cursor stops at `(n-4)*8`). Set
`trailer_bytes="0"` for frames with no trailer so the whole frame is decodable.

### Fixed-count repeats — `count="16"`
`<repeat count="N">` accepts a literal integer (not just a field name) for
fixed-size arrays like the 16 dop pins.

### Entry index base — `index0="true"`
Entry labels normally start at 1 (`pin[1]`). `index0="true"` makes them start
at 0 (`pin[0]`) to match the dop hand decoder.

### Entry token separator — `sep=" "`
Entry tokens normally join with two spaces. `sep=" "` uses a single space (dop
renders `a=1 b=1 fault=1 trans=0 output=1`).

dip flags are just named 1-bit `<field>`s; sparse gaps (dip2) use `<pad>`.
Validated by `validate_dio.py` vs the decodeDigitalIn1/In2/Out hand decoders:
**115618/115618** real frames match.

---

## arp / lsrp additions (body past a binary header)

ARP/LSRP carry a 10-byte little-endian message header; the msb-first body
starts at bit 80. The header itself (msg.src_id/dest_id/message_id/seq_num)
is still emitted by `describe()` in C++ for now (shared, trivial) — moving it
into an XML `<header>` waits for byte-mode (the NMS cluster needs LE/BE bytes
anyway).

### Body offset — `body_offset="80"`
Starts the body cursor at the given bit, skipping a fixed-size binary header.

### Field groups — `<group name="X" sep=" ">`
Reads several sub-fields and emits them as ONE row, joining the per-field
`tmpl` tokens with `sep`. Used for the DMS lat/long:
```xml
<group name="Longitude" sep=" ">
  <field name="lon_d" bits="9" signed="true" tmpl="%s&#176;"/>
  <field name="lon_m" bits="6" tmpl="%s'"/>
  <field name="lon_s" bits="6" tmpl="%s&quot;"/>
</group>
```
renders `Longitude   77° 12' 30"`. Group children also write to the parent
scope, so later `when=` conditions can reference them.

Validated by `validate_arp_lsrp.py` vs the decodeARP/decodeLSRP hand decoders
(body only): **10619/10619** real frames match.

---

## nmsrssi (byte-oriented body) — and the "byte-mode" non-event

NMS frames are byte-oriented big-endian (Annexure-G). The key realization,
same as dip/dop: a byte-aligned **big-endian** multibyte field is just an
**msb-first** read — `beU16` == `take(16)`, a 24-bit id == `take(24)`. So no
new "byte cursor" was needed. (Little-endian multibyte, when we hit ccsys/dlsys,
is likewise just an lsb-first read at a byte boundary.)

The 8-byte transport prefix + NMS common header (bytes 8..26) stay in C++
(`appendNmsHeader`, shared by all three NMS types, like ARP's msg.* envelope);
the schema decodes the **body** at `body_offset="216"` (byte 27). Validated by
`validate_nmsrssi.py` vs the describe() body block: **10397/10397** match.

Still genuinely new for the rest of the cluster (not yet built): eid value maps
(nmshlth/ccsys/dlsys), the id→size event-table (nmshlth), counted fault entries
+ name tables (nmsflt), and embedded CRC-at-offset (ccsys/dlsys).

---

## nmshlth — the event-stream construct

NMS health is a tagged variable-length record stream: a count byte, then a
sequence of `(id:2B, value:size B)` where each id's **size / sign / name** come
from a lookup table. New grammar:

### Event tables — `<eventtables>` / `<eventtable>` / `<event>`
```xml
<eventtables>
  <eventtable name="nmsHealth">
    <event id="1"  bytes="1"               name="RADIO_1_HEALTH"/>
    <event id="5"  bytes="1" signed="true" name="RADIO_1_TEMP"/>
    <event id="17" bytes="2"               name="SKAVACH_REGULAR_PKT_OFFSET"/>
    ...
  </eventtable>
</eventtables>
```

### Event streams — `<eventstream>`
```xml
<eventstream name="event" count_bits="8" id_bits="16"
             table="nmsHealth" meaning="nmsHealth"/>
```
Reads a count, then per record an id and a value sized by the table. Each row is
keyed by the event name (or `EVENT_<id>` if unknown) with value
`<num>  (<meaning>)`.

### Value-meaning hooks — `meaning="..."`
Per-eid meanings (`nmsHealthMeaning`: enums, units, `v*0.1 W` scaling, hex,
bitfield splits) are genuinely heterogeneous code, so they stay as ONE
registered hook rather than ~50 XML enums. Register in code:
`engine.py MEANINGS` (Python) / `Decoder::registerMeaning(...)` (C++, wired in
`kavachSchema()`). The structural walk (count/id/size/name) is fully data-driven;
adding or resizing an event is now an `<event>` line.

Validated by `validate_nmshlth.py` — the event-size table is parsed from the C++
source (independent of the XML), so the test checks both the `<eventtable>`
values and the stream walk: **10397/10397** frames match.

---

## nmsflt — expand repeats + name enums

Fault entries are `(module:1, code_type:1, fault_id:2 BE)`, each rendered as
THREE separate rows. Two additions:

### Expanding repeats — `<repeat expand="true">`
Each entry's child fields become their own rows keyed `name[i] childname`
(e.g. `fault[1] module`, `fault[1] code_type`, `fault[1] fault_id`), instead of
one joined row. The break guard uses the summed child width, matching the hand
decoder's "stop if a full entry won't fit" behaviour.

### Uppercase hex — `hexup="W"`
Like `hex` but uppercases the whole token (`code_type` -> `0X02`), matching the
NMS hand decoders' `.toUpper()`.

### Name tables as enums
`nmsModuleName` (43) and `nmsFaultInputName` (66) are pure id->name tables, so
they're plain `<enum>`s (`nmsModule`, `nmsFaultInput`), generated from source.
Unlike `nmsHealthMeaning` these needed no hook — they're data.

Validated by `validate_nmsflt.py` vs the describe() NmsFault block: **71/71**.

---

## ccsys / dlsys — LE byte fields, eid meanings, embedded CRCs

LE byte-aligned fields == lsb-first reads (`wire="lsb-first"`), so no new cursor.
Three additions:

### Per-field meaning — `meaning="hook" eid="N"`
Renders a field's value through a meaning hook **alone** (no number prefix),
e.g. `<field name="radio1_temp" bits="16" signed="true" meaning="nmsHealth"
eid="5"/>` -> `23 °C`. (Distinct from `<eventstream>`, which shows
`value  (meaning)`.)

### Embedded CRC — `<crc>`
```xml
<crc name="cc_crc" algo="jamcrc" from="0" len="40"/>
```
Reads a stored LE u32 at the cursor, computes `algo(frame, from, len)`, and
renders `0X........  PASS|FAIL`. CRC algorithms are registered hooks
(`crc_algos.py` / `Decoder::registerCrc`, wired in `kavachSchema()` to the
existing `jamcrc`). ccsys has two (`from=0 len=40` and `len=48`); dlsys one.

### lcu bytes via `<group>`
The `lcu_ctrl*` / `lcu_elem_status*` bitfield bytes are plain `<group>`s of
named 1-bit fields (`sep=" "`), no special formatter needed.

Validated by `validate_ccdl.py` vs decodeCcSys/decodeDlSys (CRC verdict
included): **2083/2083** (ccsys 1783, dlsys 300).

---

## rfid — Annexure-D balise tag (one packet, four bodies)

The RFID tag (`wire="lsb-first"`, `trailer_bytes="0"`) is one frame whose body
shape depends on the 4-bit `type` field (9 Normal, 10 LC gate, 11 Adjacent-Line,
12 Adjustment/Junction). Adding it needed three small, reusable grammar features.

### Type-switched bodies — `<when test="…">`
A `<when>` block walks its flat children only if the test holds; otherwise it
consumes **no bits**. Because the cursor is monotonic, this is exactly a wire
body that is present-or-absent:
```xml
<when test="type==9">
  <field name="station" .../>
  <field name="section" .../>
  ...
</when>
<when test="type==12">       <!-- different body, same cursor -->
  <field name="abs_loc_2" bits="23" enum="rfidAbs"/>
  ...
</when>
```
`<pad>`s inside a `<when>` stay scoped to that branch. All four bodies happen to
finish at the same tag bit (98), where the CRC sits.

### Negated range — `… not in LO..HI`
`when="type not in 9..12"` is the complement of the four known bodies — used for
the null/unknown tag, which only renders header + a literal note + CRC.

### Literal annotation row — `<note>`
Emits a fixed Field|Value row with no bit consumption:
```xml
<when test="type not in 9..12">
  <note name="body" text="(unknown tag type — header + CRC only)"/>
</when>
```

### Complementary header field
`abs_loc` is the first body field for types 9/10/11 but is labelled `abs_loc_1`
for type 12 (which has a second location later). Two `<field>`s with mutually
exclusive `when=` cover both without branching the whole header:
```xml
<field name="abs_loc"   bits="23" enum="rfidAbs" when="type!=12"/>
<field name="abs_loc_1" bits="23" enum="rfidAbs" when="type==12"/>
```

### CRC at an explicit bit, narrower than 32 — `<crc at="BIT" bits="N">`
The CRC-30 is stored 30 bits wide at a fixed tag-bit offset (not at the running
cursor). `at=` seeks the cursor to that bit first; `bits=` reads N (default 32):
```xml
<crc name="CRC-30" algo="rfidcrc30" from="1" len="13" bits="30" at="106"/>
```
`rfidcrc30` (registered in `crc_algos.py` / `kavachSchema()`) masks the last of
the 13 covered bytes to its low 2 bits before running the firmware CRC-30, so the
stored 30-bit value and the computed value line up.

### Coverage note
Types 9 / 11 / 12 and the null tag have real frames and are golden-validated.
**Type 10 (LC gate) has no frames in any capture** — it is encoded from the hand
decoder but cannot be validated, and is flagged as such in `kavach.xml` and
`validate_rfid.py`. The one behavioural change from the hand decoder: type-12
rows now follow **wire order** (`abs_loc_1, tin, abs_loc_2, …`) because the
schema walks one monotonic cursor; the C++ fallback `describe()` was reordered to
match.

Validated by `validate_rfid.py` vs decodeRfid: **93/93** (type 9: 87, type 11: 4,
type 12: 1, null: 1).

---

## dmi — Annexure-F regular frame (the last structural type)

The DMI regular frame is a 115-byte `AA AA .. BB BB` packet (`wire="lsb-first"`,
`trailer_bytes="0"`). Its body is a long mixed-width run: byte-aligned LE values,
LSB-first bitfields that cross byte boundaries, two single-bit flag vectors, and
re-alignment between blocks. It needed three new grammar primitives plus three
formatters.

### Byte re-alignment — `<align/>`
Two inline runs end mid-byte (body bits 505 and 607). `<align/>` rounds the
cursor up to the next byte boundary — exactly the hand decoder's `DmiCursor::
align()`. It is placed before every block the hand decoder aligns (most are
already aligned and so are no-ops, kept for faithfulness; two do real work):
```xml
<align/>
<flags name="context_values" table="dmiCtx" bits="14" pad="2"/>
```

### Single-bit flag vectors — `<flags>` + `<flagtables>`
The 71-bit alarm and 14-bit context fields map set bits to names joined with
"; " (or "(none)"). Names live in the schema as data, so adding a flag is a
data edit, not a code change:
```xml
<flagtables>
  <flagtable name="dmiAlarm"><flag name="System Fault, ..."/> ... </flagtable>
</flagtables>
...
<flags name="alarm_code" table="dmiAlarm" bits="71" pad="9"/>
```
`bits` flags are read LSB-first (bit i -> table entry i), then `pad` bits are
consumed.

### Computed rows — `<row>`
Some rows combine fields in non-wire order or need formatting the cursor can't do
itself. `<row>` emits a row from already-stored ids and consumes no bits.
Tokens: `{id}` -> value, `{id:0Nd}` -> zero-padded width-N integer.
```xml
<!-- lc.id shows num then a wire-earlier suffix byte -->
<field name="lc_alpha" bits="8"  id="lc_alpha" hide="true"/>
<field name="lc_num"   bits="16" id="lc_num"   hide="true"/>
<row   name="lc.id"    tmpl="{lc_num} suf={lc_alpha}"/>
<!-- packed datetime -->
<row name="rtc" tmpl="{dd:02d}-{mo:02d}-{yy} {hh:02d}:{mi:02d}:{ss:02d}"/>
```
Read the contributing fields as `hide="true"` ids first, then compose.

### Formatters
Three additions to the `format="..."` registry (`engine.FORMATTERS` /
`Decoder::composite`): `absKm` ("N m (N.NNN km)"), `decel` ("DC X.YY"), and
`sigAspect` (the 6-bit aspect map with the 32-63 stencil-route arithmetic). The
existing `sigInfo` formatter is reused for `current_sig_info`.

### CRC and one display change
`<crc algo="jamcrc" from="3" len="106" bits="32" at="872"/>` — JAMCRC over wire
bytes 3..108, stored LE at byte 109. The one behavioural change from the hand
decoder: the CRC row now uses the canonical `0X........  PASS|FAIL` form; the old
DMI-only `(calc 0x........)` debug suffix is dropped (and the C++ `decodeDmiReg`
fallback was updated to match).

Validated by `validate_dmi.py` vs decodeDmiReg over every captured frame:
**9190/9190**.

---

## uba — packed doubles (the first real-valued packet)

`@uba` is a raw C struct memcpy'd onto the wire: no header, no CRC, and its
payload is almost entirely 8-byte IEEE-754 doubles at unaligned offsets.

### `type="double"`
8-byte IEEE-754, little-endian (an lsb-first read at a byte boundary). The bit
cursor returns at most 32 bits, so both engines read the two halves in wire
order — low word first — and reassemble. `type="float"` (4-byte) is unchanged.

### `format=` on reals — `FLOAT_FORMATTERS`
The `format="name"` hook now works on `float`/`double` fields, resolved against
a **separate** registry (`engine.FLOAT_FORMATTERS` / `compositeFloat()` in
`schemadecoder.cpp`). The integer `FORMATTERS` could not be reused: they index
and divide their argument and would misbehave silently on a double. A float
formatter owns the whole token, units included, so `prec`/`unit` are ignored
when `format` is present. Registered today:

- `mps` — `13.8889 m/s  (50.0 km/h)`
- `curveA` — `-0.533292  (decel 0.938 m/s²)`, for the UBA curve coefficient
  (segments are `x = A*v² + C`, so `a = -1/(2A)`)

### Typed fields work inside `<repeat>` now
`type=` used to be understood only by the flat walker, so a float inside a
repeat was read as a 0-bit integer — a silent divergence from `engine.py`,
which routes all three walkers through `_read()`. The C++ engine now shares one
`readTyped()` across the flat, entry and expand walkers, and the expand break
guard sizes entries with `fieldBits()` instead of summing `bits=` attributes.

**Do not condition on a real field.** `when=`/`count=` resolve against an
integer-valued context; the C++ engine deliberately does not insert reals into
it, because rounding a double in would make the condition mean something
different than it does in the reference engine.

### One-byte enums
`target_type` is 1 byte on the wire, not 4: the target build uses
`-fshort-enums`. Nothing in the grammar changes (`bits="8" enum="..."`), but it
is worth stating, because assuming 4 shifts every following double by three
bytes and still produces a full table of plausible-looking rows.

Validated by `validate_uba.py` — no hand decoder exists to diff against, so the
oracle is `struct.unpack` against the declared C layout plus the curve's own
physical invariants: **4694/4694** real frames match.

---

## Migration status

All 17 structural packet types are now schema-driven: slrp, aap, aep, arp, lsrp,
nmshlth, nmsrssi, nmsflt, ccsys, dlsys, dip1/2, dop1/2, rfid, dmi, **uba**. The
remaining `CapType` values (Dlt, Biu, Brk) have no decoder and no frames in any
capture, so they stay on the raw-hex fallback until ground truth exists. The only
encoded-but-unvalidated path is the RFID type-10 LC gate tag (no frames captured).
