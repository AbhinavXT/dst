# Session 56 — `@uba` (Target_Internal + braking curve)

New capture type. Schema-driven from day one; no hand decoder was written, so
there is no hand/schema pair to converge later.

## What the wire says

452 B, little-endian throughout, **no message header and no CRC** — the loco
memcpy's the struct out as-is.

```
target_location   double   8 B   @0
target_speed      double   8 B   @8
target_type       enum     1 B   @16
CURVE_SEGMENT[9]           432 B @17     (48 B each: A, C, start_loc,
                                          end_loc, higher_speed, lower_speed)
tail                        3 B  @449
```

Two findings that the quoted C header does not predict, both established from
all 4694 frames of `81_2_15092026_121737.log`:

**`target_type` is ONE byte, not four.** The target build compiles enums to
their smallest type (`-fshort-enums`), so `Target` is 17 B, not 20. This is the
whole ball game: assume 4 and every double after it is read three bytes short
and decodes to `1e-307`-grade garbage that still *looks* like a successful
decode. The first curve double sits at offset 17.

**Nine segment slots, not 22.** 435 B of curve data is exactly `9*48 + 3`. The
build that produced this capture therefore carries ONE curve of
`2*MAX_DEC_UNITS+1 = 9` segments (MAX_DEC_UNITS 4). The header the layout was
quoted from says `MAX_DEC_UNITS 5` and `NUM_OF_CURVES_FOR_EACH_TARGET 2`, which
would be 22 segments and a 1073 B frame. Nine is what arrives. `count="9"` in
`kavach.xml` is the single place to change if the deployed constants move.
A 3-byte tail follows (zero in every frame) — consistent with a 449 B struct
padded to a 4-byte boundary by the framing.

## Curve semantics

Each segment is `x = A*v^2 + C`, so `C` is the stop location and
`A = -1/(2a)` for deceleration `a`. Verified over all 4694 frames:

- `end_loc == A*lower_speed^2 + C` exactly (0 violations)
- `seg[k].end_loc == seg[k-1].start_loc` — the curve is continuous, as the
  struct comment requires (0 violations)

`start_loc` is the clamped segment start (where the next step takes over), not
`x(higher_speed)`. The capture holds one static EOA target at 6248 m with four
populated segments stepping 255 → 100 → 70 → 50 → 0 km/h at ~0.938 m/s²
(1.000 m/s² on the final one).

## Engine additions

- **`type="double"`** — 8-byte IEEE-754, little-endian. `take()` returns at most
  32 bits, so the two halves are read in wire order (low word first).
- **`FLOAT_FORMATTERS` / `compositeFloat()`** — `format="name"` now works on
  reals. The integer `FORMATTERS` could not be reused: they index and divide
  their argument and would misbehave silently on a double. Two entries:
  - `mps` → `13.8889 m/s  (50.0 km/h)`
  - `curveA` → `-0.533292  (decel 0.938 m/s²)`
- **`Decoder::readTyped()` / `fieldBits()`** (C++) — one typed read shared by
  the flat, entry and expand walkers. This closes a **pre-existing parity
  bug**: only `walkFlat` understood `type=`, so a `float` inside a `<repeat>`
  was read as a 0-bit integer, while `engine.py` routes all three through
  `_read()`. No shipped packet hit it (no float lived inside a repeat until
  now), but the divergence was real.

Reals are deliberately not written into `Ctx` on the C++ side: `Ctx` is
integer-valued and rounding a double into it would make `when=`/`count=` mean
something different here than in the reference engine. Do not condition on a
real field.

## Display

One row per segment rather than 54 loose numbers — the ladder is what you read:

```
target_location    6248.000 m
target_speed       0.0000 m/s  (0.0 km/h)
target_type        EOA
seg[0]             A=-0.500000  (decel 1.000 m/s²)   C=6348.000 m   start=6250.549 m   end=6348.000 m   hi=13.8889 m/s  (50.0 km/h)   lo=0.0000 m/s  (0.0 km/h)
seg[1]             A=-0.533292  (decel 0.938 m/s²)   C=6353.422 m   start=6156.890 m   end=6250.549 m   hi=19.4444 m/s  (70.0 km/h)   lo=13.8889 m/s  (50.0 km/h)
...
seg[4]             A=0.000000   C=0.000 m   start=0.000 m   end=0.000 m   hi=0.0000 m/s  (0.0 km/h)   lo=0.0000 m/s  (0.0 km/h)
tail               3 B struct tail (zero in every captured frame)
```

Empty slots are shown rather than hidden — an unused slot is a fact about the
curve, and hiding it would make a 4-segment curve indistinguishable from a
9-segment one that happens to be zeroed.

## Validation

`schema/validate_uba.py` — new. There is no hand decoder to diff against, so
the oracle is the struct itself: every frame is re-read with `struct.unpack`
against the declared C layout and the XML engine must reproduce it row for row,
then the physical invariants above are checked.

```
frames            : 4694
wrong length      : 0
row mismatches    : 0
invariant failures: 0
PASS — 4694/4694 frames match
```

Regression check — every other validator re-run and diffed against the
unpatched tree: `validate_slrp`, `validate_dmi`, `validate_arp_lsrp`,
`validate_dio`, `validate_rfid`, `validate_ccdl` all **byte-identical to
baseline**; `nmsflt` 156/156, `nmshlth` 10615/10615, `nmsrssi` 10615/10615,
`aap_aep` 46/46.

C++ side: still no Qt compiler in the sandbox, so `engine.py` remains the gate.
The new bit logic was additionally cross-checked by lifting `Cursor::take`,
`readTyped`'s double path and `compositeFloat` into a standalone non-Qt harness
(`uba_cpp_parity.cpp`, not part of the build) and diffing its output against
the Python engine for a real frame: identical field for field, cursor ending at
bit 3592 = byte 449, exactly where the 3-byte tail begins.

## Files touched

| file | change |
|---|---|
| `schema/kavach.xml` | `UBA` packet + `ubaTargetType` enum |
| `schema/engine.py` | `type="double"`, `FLOAT_FORMATTERS`, `_fmt_float` format hook |
| `schema/schemadecoder.cpp` | `readTyped`/`fieldBits`/`compositeFloat`; flat, entry and expand walkers routed through them |
| `schema/schemadecoder.h` | `Kind`, `readTyped`, `fieldBits` declarations |
| `schema/validate_uba.py` | new gate |
| `capturedecoder.h` | `CapType::UBA` + frame-recipe note |
| `capturedecoder.cpp` | token, label, direction, `describe()` dispatch |
| `replaywindow.cpp`, `lococonsolewindow.cpp` | tab / link-overview order |
| `framediffwindow.cpp`, `decodeworkbench.cpp` | token lists |

## Open question

Confirm the deployed `MAX_DEC_UNITS` and `NUM_OF_CURVES_FOR_EACH_TARGET`. The
wire says one curve of nine segments; the header says two curves of eleven. If
the header is right for a newer build, the frame will be 1073 B and
`validate_uba.py` will report a length mismatch rather than decoding wrongly.
