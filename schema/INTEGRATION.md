# Wiring the schema engine into DLConsole

This kit is additive — nothing in the current build changes until you opt in.

## Files
- `kavach.xml`        — the schema (edit this to add fields/structs)
- `schemadecoder.h/.cpp` — the C++ engine (Qt DOM based)
- `engine.py`        — reference engine + validator (same logic, runs anywhere)
- `SCHEMA_GUIDE.md`  — the non-coder guide
- `INTEGRATION.md`   — this file

## 1. Add to the build
In `DLConsole.pro`:
```
QT += xml
SOURCES += schemadecoder.cpp
HEADERS += schemadecoder.h
# ship the schema next to the binary (or load by absolute path)
```

## 2. Load the schema once (e.g. in main() or the window ctor)
```cpp
#include "schemadecoder.h"
static Schema::Decoder g_schema;
QString err;
if (!g_schema.load(QStringLiteral(":/kavach.xml"), &err))  // or a file path
    qWarning() << "schema:" << err;
```

## 3. Use it where you want declarative decode
The engine returns the same `QVector<FieldRow>` the inspector already consumes,
so you can route a packet type through it. Conservative approach — keep the
hand-written decoder as default, use the schema only when it claims the frame:

```cpp
// inside describe(), for SLRP:
if (g_schema.isLoaded() && g_schema.handles(b)) {
    r += g_schema.decode(b);        // data-driven
} else {
    r += decodeSLRP(b, tagLoc);     // existing hand-written path
}
```

You can migrate one packet type at a time. The abs_loc annotations added in the
hand-decoder are a display layer on top; if you want those in the schema path
too, say so and I'll add an `<absloc>` hint to the schema grammar.

## 4. Validate any schema change without compiling
```
python3 engine.py path/to/capture.cap      # prints decoded SLRP rows
```
`engine.py` reads the very same `kavach.xml`, so if your edit decodes correctly
there, the C++ engine will behave identically (the two share one algorithm).

## Current scope / honest limits
- Implemented: msb/lsb bit fields, signed, units, enums (value + range+formula),
  `id` capture, `when` (id vs literal), `repeat count="id"`, sub-packet dispatch.
- The C++ enum `formula` supports `v*K` / `v+K` (covers Kavach speed/step maps);
  the Python reference allows any expression. Keep schema formulas to those forms
  so both engines agree.
- CRC parameters are described in `<crcs>` and selected per packet, but the C++
  engine does not yet *verify* CRC (the hand decoder still does). Easy to add a
  `Schema::crcCheck()` next.
- Not yet: arbitrary arithmetic in conditions, nested repeats keyed off a count
  read mid-entry, cross-field derived values. The grammar has room; ask when a
  real packet needs one.

## Round-tripping: parseBody() is the inverse of encodeBody()

`Schema::Encoder` now reads as well as writes. `parseBody(captype, bytes)`
walks the same `<packet>` / `<struct>` DOM as `encodeBody()` — same child
order, same `when` gating, same count-driven `<repeat>` — and returns a
`ParsedPacket` whose `header` and `subs` are exactly what `encodeBody()` and
`PacketBuilder::build()` take as input.

That symmetry is the point: it makes the schema self-checking. For any real
capture,

    parseBody(bytes) -> encodeBody(...) == bytes

holds if and only if every field is present, correctly sized and correctly
gated. A missing field shows up immediately as a length difference, not as
plausible-looking wrong numbers further down.

Two consequences worth knowing when you add a struct:

* `parseBody()` reports any sub-packet that leaves 8 or more unread bits
  before the next one. Sub-packets are byte-length-framed, so a missing field
  does not corrupt what follows — it just leaves a hole, and the hole is now
  named. This is how the missing TagLinking `TIN` was found.
* Because the cursor jumps to `startByte + len`, a struct that over-reads its
  declared length is an error, while one that under-reads is a warning. Both
  are worth chasing before trusting the decode.

Replay the whole corpus after any schema edit — every SLRP frame in
`replay/*.cap` should parse and re-encode byte-identical. `tests/dltests
bufferload` pins one golden frame; the corpus sweep is the broader check.
