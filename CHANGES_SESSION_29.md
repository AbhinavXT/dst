# DL Console — session changes (patch 29)

Field Sweep — the second of the four tools. Walk one field across its values,
send each one, and score what came back.

| File | Change |
|---|---|
| `fieldsweep.{h,cpp}` | plan generation and verdict classification (pure) |
| `fieldsweepdialog.{h,cpp}` | the dialog: build, send, correlate, score |
| `mainwindow.{h,cpp}` | Tools → Field Sweep…, and the live entry feed |
| `tests/test_fieldsweep.cpp` | new suite (46 checks) |

## Not the same thing as PacketVary

`PacketVary` already varies a field from send to send, but it is a rule
evaluated per tick with nowhere to put an answer: it holds a session open, it
does not ask a question. A sweep is the question — *which values of this field
does the target accept?* — so it plans a finite list up front, sends one at a
time, and scores each value by what arrived afterwards.

## The plan

Four modes. **Boundary** is the default because that is where bit-packed field
bugs live: off-by-one at the top of the range, sign handling at -1, the code one
past the last defined enum. It generates min, min+1, midpoint, max-1, max — plus
-1, 0 and 1 for a signed field — deduplicated, so a 1-bit field plans two values
rather than five copies of two.

**Range** (from/to/step), **List** (typed values, decimal or `0x`), and **Enum
codes** — every declared value of the field's enum *plus the first undefined
code*. That last one is the point of the mode: a target that accepts a value the
spec never defined is the bug worth finding, and sweeping only the declared
codes cannot reach it.

Three refusals, all of which would otherwise produce a results table that lies:

- A value that does not fit the field's width. It would be truncated on the way
  to the wire, so the value tested would not be the value in the table.
- A range that expands past 512 values. A 17-bit field swept by 1 is 131072
  sends; the error says the number rather than queueing it.
- A zero step, or a step running away from the end value.

## Scoring

Each sent value opens an answer window (default 400 ms). Entries arriving inside
it are attributed to that step and the step is scored:

| verdict | meaning |
|---|---|
| reply | a captype you listed as an answer arrived |
| fault only | only fault traffic — the target understood the frame and rejected it |
| other traffic | something arrived, but not what was being watched for |
| silent | nothing at all |

"Fault only" is deliberately separate from "silent": a target that answers with
a fault has understood and rejected, which is a different fact from no answer,
and collapsing the two loses the more interesting half.

A build failure **stops** the run rather than skipping the value. A gap in the
results table that looks like a silent target, but was actually a frame that
never left, is the worst thing this tool could report.

## Using it

Tools → Field Sweep…, seeded from the selected log row when there is one — a
sweep that starts from a frame the target already accepted isolates the field
being swept, which a form full of zeros does not. Base values are also editable
by hand, or seedable from a pasted frame.

Header fields only. Sub-packet repeats have per-row values with no single name
to address; that is the same boundary `PacketVary` drew, for the same reason.

MAC key comes from the same captured-key-set picker as the Packet Maker.

**This transmits.** Explicit and opt-in, one datagram per value.

## Tests

Both halves are pure, so the two things that must not be wrong — the values sent
to a live target, and the meaning assigned to what came back — are checkable
without a socket or a window. 46 checks: boundary generation for signed and
unsigned, the 1-bit collapse, every refusal above, enum codes including the
undefined one, and the classification table (including an answer arriving after
the window, which belongs to no step, and a reply alongside a fault, which is
still a reply).

Full suite: **58 suites, 1815 checks, 0 failed.**

## Next

Round-trip validator, then the station responder.
