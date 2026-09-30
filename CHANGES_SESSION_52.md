# Patch 52 — conditional reject rules

The FRAME_OFFSET guard, and with it the rule format needed for most of the
document's remaining conditions.

## Why the format had to change

FRAME_OFFSET 14 looked like the document contradicting itself: 31.19.3 sends
`1110` and expects it processed, 31.19.5 says process only below 14. It is not
a contradiction — they describe different sections. Neither clause can be
encoded as a value rule without losing the thing that separates them, and a
value rule for either one would be wrong half the time.

So rules now carry a guard.

## Reusing the schema's condition language

`kavach.xml` already has one — `<field when="AUTHORITY_TYPE==1">`,
`"A in 3..7"`, `"A not in 0..0"`, `"A & 7 == 3"` — with a working evaluator
that has been in use for every conditional field in the schema.

`Decoder::condOk` is now exposed as `Decoder::conditionHolds()` taking a plain
value map, and the reject rules call it. One condition language in the
program, one implementation. A second dialect would be one more thing to learn
and one more thing to drift from the first.

The guard is evaluated against the same decoded map as the value, before it,
and a guard naming a field the frame does not carry reads that field as zero —
which is the schema's existing behaviour, not a second rule invented here.

## The rule

    <rule clause="31.19.5" field="FRAME_OFFSET" op="eq" value="14"
          when="TRAIN_SECTION_TYPE not in 0..1"
          note="frame offset 14 outside a station or absolute-block section"/>

**A reading to confirm.** `secType` is 0 Station Section, 1 Absolute Block,
2 Autoblock, 3 Reserved. "Accepted where the RFID has absolute or station
section" is taken as `TRAIN_SECTION_TYPE in 0..1`, so 14 is reported only in
Autoblock and Reserved. That interpretation is written into the rule file
above the rule itself, so the next person to read it sees the reasoning rather
than a bare number. If it is wrong it is a one-line edit and nothing else
changes.

## The guard is part of the claim

A finding now reads:

    FRAME_OFFSET = 14 (when TRAIN_SECTION_TYPE not in 0..1) — frame offset 14
    outside a station or absolute-block section  [31.19.5]

Without the condition stated, "FRAME_OFFSET = 14" would look plainly wrong to
anyone who knows 14 is fine in a station section, and the operator would be
left deciding whether to believe the console.

## Tests

The guarded rule is checked in both directions, which is the only way to know
a guard works: 14 stands in sections 0 and 1, is reported in section 2, and 13
is unremarkable everywhere. Plus one that would have caught the obvious
mistake — the unconditional rule for offset 15 still fires in a station
section, where 14 does not. A guard that leaked onto its neighbours would pass
every other check here.

## Files touched

    schema/schemadecoder.{h,cpp}  conditionHolds() exposed; condOk forwards
    rejectrules.{h,cpp}           when= parsed, evaluated, and reported
    schema/rejectrules.xml        the FRAME_OFFSET 14 rule
    tests/test_rejectrules.cpp

## Still open

The aspect widths and the seven `PKT_TYPE` false positives still need reading
by hand. The narrative conditions are now expressible for the ones that reduce
to field comparisons; the rest need state the console does not keep. Inferring
our own ID from ARP/LSRP for the `DEST_ONBOARD_ID` rules is the next real
piece, and it is runtime state rather than a rule-file change.

## Verification

    118 suites, 3000 checks, 0 failed      (was 118 / 2993)
    menu audit passed
    headless smoke run clean, rules load with no warning
