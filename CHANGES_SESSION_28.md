# DL Console — session changes (patch 28)

Frame Diff — the first of the four tools. Field-by-field comparison of two
frames.

| File | Change |
|---|---|
| `framediff.{h,cpp}` | comparison core: alignment, summary, byte offsets, input parsing |
| `framediffwindow.{h,cpp}` | the window |
| `mainwindow.{h,cpp}` | Tools → Frame Diff…, and "Diff these two rows" in the row menu |
| `tests/test_framediff.cpp` | new suite (39 checks) |

## Why field-level

A byte diff of a bit-packed Kavach frame is close to useless. One changed 9-bit
field smears across two bytes; a conditional branch firing (LSRP's
`Loco_Health` group rotates with `FRAME_NUM`) changes which fields exist at all.
The bytes say "six bytes differ"; the answer wanted is "TRAIN_SPEED 40 → 45".

So the comparison is over decoded fields. Byte offsets are still reported, in
the status line, as a sanity check rather than as the headline.

## Alignment

The two field lists are not necessarily parallel — a `<when>` branch, a
different subpacket mix, or a different repeat count changes which rows exist.
Aligning positionally would report every row after an inserted one as changed.

The lists are aligned by an LCS over field names first, so a row present on one
side only is reported as such and the rows after it still line up. Field names
repeat within a frame (every subpacket has a `FRAME_OFFSET`), so the alignment
key carries the occurrence number — otherwise the LCS pairs subpacket 1's field
with subpacket 3's and calls both changed.

Above 2000 rows per side it falls back to positional comparison. That is a guard
against a corrupt length field turning a UI action into a freeze, not an
expected case.

## Using it

- **Row menu → "Diff these two rows"**, enabled only with exactly two rows
  selected. Picking two out of five would be a guess at which two were meant.
- **Tools → Frame Diff…** takes the selection if it is exactly two rows,
  otherwise opens empty to paste into.
- Each side accepts a capture line, or bare hex plus a type. Bare hex is
  reassembled into a capture line and decoded through the same path the log view
  uses, so there is one decode path and not a second subtly different one.
- "Show only differences" is on by default — the reason to open this window is
  one unexpected field.
- The window is reused rather than reopened: comparing frame after frame is the
  normal way to use it.

## Tests

The core is pure, so the interesting cases are all reachable without a window:
insertions on either side, repeated field names, empty sides, length
differences, and every input-parsing rejection.

Real LSRP frames from `replay/` back the end-to-end checks, so the alignment is
exercised against real conditional branches rather than only synthetic rows —
including a frame diffed against itself, which must show nothing.

The window gets its own checks, because a correct core does not mean the table
shows it: the default view must be shorter than the full field list, unticking
"only differences" must show everything, and two identical frames must leave it
empty rather than crash on a diff with nothing in it.

Full suite: **57 suites, 1769 checks, 0 failed.**

## Next

Field sweep, then the round-trip validator, then the station responder.
