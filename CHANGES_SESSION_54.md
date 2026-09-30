# Patch 54 — the two items that needed reading, not machinery

## The aspect widths, resolved

The review flagged four rules whose value did not fit the field: the document
writes `1001` and `1110` where `kavach.xml` gives the aspect 6 bits.

Reading the **whole** of section 31.23 rather than the four flagged rows
settled it. The document writes every aspect in binary and drops leading zeros
on the short ones:

    0, 1, 10, 100, 101, 110, 111, 1000, 1001, 1010, 1100, 1101, 1110, 1111,
    then 010000, 010111, 011000, 011001, 011111, 100000

So `1001` is **9** and `1110` is **14**. There was never a decimal reading;
my width heuristic was simply wrong, and the unreachable-value guard from
patch 50 is what stopped it reaching the rule file.

Cross-checking all twenty values against the schema's `sigAspect` enum, they
agree on nineteen:

- **9** is spare in the document, and `kavach.xml` has no `v=9` so it falls
  through to Spare. **Encoded**, for both `CUR_SIG_ASPECT` and
  `NEXT_SIG_ASPECT`.
- **14** is the one disagreement. The document calls it Spare; `kavach.xml`
  calls it **AG Marker OFF**, a real aspect. **Not encoded.** One of the two
  is wrong, and a rule here would report a legitimate aspect on live traffic —
  the one failure that teaches an operator to ignore the feature. It needs a
  decision, and it is a one-line addition once made.

## The PKT_TYPE false positives, resolved

All seven rows open by naming the packet under test — `0011` for LC info,
`0111` for the TSR profile — and state the actual condition at the END of the
sentence. The extractor took the first field it saw.

- **31.55.5 / 31.54.x** → the condition is `TSR_STATUS as 3 (Reserved)`.
  `TSR_STATUS` is a 2-bit field and the schema's `tsrStatus` enum agrees that
  3 is Reserved. **Encoded.**
- **31.47.1 / 31.48.1** → `LC_ID_Numeric` and `LC_ID_Alpha_Suffix` as 0. Not
  encoded: in `kavach.xml` these are `id` and `suffix` inside the `lc` repeat,
  local names that each iteration overwrites. A rule keyed on `id` would test
  whichever level crossing happened to be decoded last, and `id` is generic
  enough to collide besides. Expressing these needs per-iteration values,
  which the value map does not currently carry.
- **31.46.2** → a `SUB_PKT_LENGTH` consistency table against `LM_LC_Info_CNT`,
  not a value rule at all.
- **31.6.2 / 31.7** → genuinely PKT_TYPE rules, but **direction-dependent**:
  an onboard rejects `1010` because that is an onboard-to-station type, and a
  station rejects `1001` for the mirror reason. Encoding either without
  knowing which end this console is watching would report every normal packet
  on one side of the link. `LocoIdentity` from patch 53 could answer that — a
  source with a learned own-loco ID is a loco-side capture — but that is a new
  inference and worth agreeing before it becomes a rule.

## Tests

The aspect reading is tested in both directions, which is the only way to know
a binary/decimal call was right: 9 fires, the six-character spares still fire,
and Green, Stop Board and a stencil route do not. Plus an explicit check that
**14 does not fire**, so if someone later adds that rule the test says why it
was left out rather than silently going green.

## Files touched

    schema/rejectrules.xml      3 rules added, with the reasoning above them
    tests/test_rejectrules.cpp

## What remains

Only the four items above that need a decision or new plumbing: aspect 14, the
LC per-iteration fields, the SUB_PKT_LENGTH consistency table, and the
direction-dependent packet-type rules. Every rule the document states plainly
and unambiguously is now in.

## Verification

    119 suites, 3031 checks, 0 failed      (was 119 / 3022)
    headless smoke run clean, rules load with no warning
