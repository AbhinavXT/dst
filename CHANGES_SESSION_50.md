# Patch 50 — reject rules, section A

The engine and the rule set. **Not yet wired to the UI** — see "What's next".

## The rule set

`schema/rejectrules.xml`, external like `kavach.xml` and for the same reason:
a clause read wrongly is fixed by editing a file, not by shipping a build.
Every rule carries its FRS clause, so any reason the console prints can be
checked against the document.

**26 rules**, from section A of the review. Three things were left out on
purpose, and each is stated in the file itself so the next reader does not
think they were forgotten:

- the aspect rules whose width the document contradicts,
- the rows where `PKT_TYPE` names the section rather than the fault,
- the narrative conditions.

### FRAME_NUM is a range

The document tests `0` and `86401` because they are the boundaries. The field
is seconds since midnight plus one, so anything outside `1..86400` is the same
fault. Encoding only the two tested values would have passed `90000` silently
— which is precisely the failure this feature exists to catch. Encoded as
`op="outside" min="1" max="86400"`, and said out loud in the file.

### Six rules dropped as unreachable

Checking each value against the field width in `kavach.xml` found six that
cannot occur: `SOURCE_STN_ILC_IBS_ID == 65536` in a 16-bit field (max 65535),
`APPR_STN_ILC_IBS_ID == 65539` in 16 bits, and the four aspect values read as
decimal. Those clauses test the generating tool, not the wire.

The loader now **refuses** such a rule rather than accepting it, because a
rule that can never fire reads on screen as a condition being checked when it
is not. That check is a test.

## The engine

`rejectrules.{h,cpp}`. Two properties worth stating:

**It never says a packet is valid.** An empty result means no rule in the file
matched, which is a different claim — the file holds one section of the
document. The wording is "no reject condition matched".

**It reports every rule that fires, not the first.** A malformed frame usually
trips several, and fixing the one the console mentioned, rebuilding, and
running again to find the next is the loop this removes.

Findings read `PKT_DIR = 0 — packet direction unidentified or spare [31.16.1]`:
the condition, what the frame carried, and the clause. No "invalid", no
"FAIL", no "PASS" — the tooling reports and the signatory decides. There is a
test asserting the absence of that vocabulary.

A malformed rule file is **refused whole**. Half a rule set is worse than a
stale one: it silently stops reporting conditions the operator still believes
are being checked, so the previous set stays loaded.

## Decoder change

`Schema::Decoder::decode` gained an optional `rawValues` out-parameter giving
each field's decoded NUMERIC value. The decoder already built this map to
resolve counts and conditions and then threw it away.

Rules evaluate on those numbers, never on the rendered rows: a row's value is
a display string — `"2 (Reverse)"`, `"12.5 m"`, an enum label — and comparing
against it would mean parsing presentation back into a number, and would break
the day a label is reworded. Existing callers are unaffected; the map is
published only on request.

## What's next

Wiring it to the field inspector needs `CaptureDecoder::describe()` to forward
the raw values too — it is a per-type dispatcher, and threading the map
through it is a real change rather than a signature tweak. Doing that badly to
finish today would put the risky part of this feature in without the care the
rest of it got.

Also still open, from the review: the aspect widths, the seven `PKT_TYPE`
false positives, the narrative conditions, and the FRAME_OFFSET 14 guard —
which needs a condition expression in the rule format, since it depends on
absolute-location RFID or station section.

## Files touched

    schema/rejectrules.xml       NEW — 26 rules, each citing its clause
    rejectrules.{h,cpp}          NEW — loader and evaluator
    schema/schemadecoder.{h,cpp} optional rawValues out-parameter
    tests/test_rejectrules.cpp   NEW
    images.qrc, *.pro

## Verification

    117 suites, 2985 checks, 0 failed      (was 115 / 2944)
    headless smoke run clean
