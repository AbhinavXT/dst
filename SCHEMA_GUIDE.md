# Adding fields and structures — `kavachschema.h`

The SLRP packet inspector is **schema-driven**. The whole protocol layout lives
in `kavachschema.h` as data tables; a generic walker turns them into the
Field|Value rows (and the abs-location annotations). To extend the protocol you
edit that one file — no decoder code.

`kavachschema.h` is an *include-fragment*: `capturedecoder.cpp` pulls it in
inside its anonymous namespace, so the tables can reuse the bit cursor and the
existing value formatters (`mapSpeed6`, `sigInfo`, `tcType`, …).

## Add a field to an existing structure

Add one `F(...)` row to that structure's field table. A field is:

```
{ "NAME", bits, kind, fmtFn, tmpl, condField, condVal, role, hide, condOp, condMax }
```

Only `name` and `bits` are required; the rest default. Common cases:

- Plain unsigned 9-bit field:           `{ "TIN", 9 }`
- Distance in metres:                   `{ "APPR_SIG_DIST", 15, K::Met }`
- Signed distance:                      `{ "DIST_PKT_START", 15, K::SMet }`
- Pretty-printed via a formatter:       `{ "AUTHORITY_TYPE", 2, K::U, mapAuthType }`
- Present only when another field == v:  `{ "NEW_MA", 16, K::Met, nullptr, nullptr, "REQ_SHORTEN_MA", 1 }`
- Read but not shown (drives a cond):    `{ "class", 1, K::U, nullptr, nullptr, nullptr, 0, Role::None, true }`

`fmtFn` is any `QString f(quint32)` in the translation unit — reuse the existing
ones rather than writing new strings.

## Add an entry field (inside a repeating sub-packet)

Entry fields render into one composite row per repetition, so give them a
`tmpl` (`%1` = the value):

```
{ "d", 15, K::Met, nullptr, "@%1 m", nullptr, 0, Role::SegLen },
{ "u", 6,  K::U,  mapSpeed6, "universal %1", "class", 0 },
```

### abs-location roles
The walker composes absolute track locations from `LAST_REF_RFID`,
`DIST_PKT_START` and `PKT_DIR`. Tag a field with a `role` so its element gets an
abs annotation:

- `Role::SegLen` — value is a per-segment **length**; lengths chain and the row
  shows `[absStart → absEnd m]` (SSP, gradient).
- `Role::Start` + `Role::Len` — a start distance and a length in the same entry;
  the row shows `[absStart → absEnd m]` (TSR, track condition, turnout). A
  `Start` with no `Len` shows a single `abs=… m` point (LC gate).
- `Role::TagHop` — value is `DIST_NXT_RFID`; hops accumulate from the reference
  RFID and the row shows `abs=… m` (tag linking).

## Add a whole new structure

1. Field table(s):
   ```cpp
   static const FieldDef MYSUB_CNT[]   = { { "MY_CNT", 5 } };
   static const FieldDef MYSUB_ENTRY[] = { { "x", 15, K::Met, nullptr, "@%1 m" }, ... };
   ```
2. Segment list (`Flat` for one-shot fields, `Rep` for counted entries):
   ```cpp
   static const SegmentDef MYSUB_SEG[] = {
       Flat(MYSUB_CNT, KSCH_N(MYSUB_CNT)),
       Rep (MYSUB_ENTRY, KSCH_N(MYSUB_ENTRY), "MY_CNT", "myentry", 0, 31),
   };
   const StructDef MYSUB_STRUCT = { "MyStructure", MYSUB_SEG, KSCH_N(MYSUB_SEG) };
   ```
3. Register it by its `SUB_PKT_TYPE` index in `SLRP_SUB[]`.

A `StructDef` is just an ordered list of segments; `Flat` segments emit one row
per field, `Rep` segments read a count from a previously-decoded field and emit
one composite row per entry. `cmin/cmax` on a `Rep` bound the count (entries are
skipped if the count falls outside the range).

## Conditions
`condField`/`condVal` include a field only when an earlier field equals a value.
For a range (e.g. turnout start/release present only for restricted speeds) use
`condOp = Cond::InRange` with `condVal`..`condMax`.

## What still needs C++
The generic walker covers sequential fields, conditions, counted repeats, enum
formatting and the abs roles above. Genuinely irregular encodings (bit layouts
that depend on arithmetic of several fields, etc.) still warrant a hand-written
formatter function — point a field's `fmtFn` at it. The numeric profile decoder
that feeds the replay's look-ahead lanes (`profileOf`) is separate and is not
schema-driven.

## Verifying a change
Rebuild and open a capture that exercises the structure. The project's habit of
golden-vector checking applies: confirm one known packet decodes field-for-field
before trusting the table. (The SLRP schema here was validated against a real
reverse capture — ref RFID 989 at 162660 m, SSP/gradient chaining to the MA end
at 159940 m, and the tag chain landing on the real downstream tag locations.)
