# Patch 51 — reject reasons reach the screen

Patch 50 built the rule set and the evaluator and stopped short of the UI.
This wires it up.

## The plumbing

`CaptureDecoder::describe()` gained an optional `rawValues` out-parameter,
forwarded to `Schema::Decoder::decode`. It turned out to be one change, not
seventeen: every packet type renders through a single `schemaRows()` helper,
so the map is filled in one place and forwarded at each call site.

Merged rather than assigned, because a few types decode twice — an envelope
and a body — and the second call must not erase the first.

Rules evaluate on those numbers and never on the rendered rows. A row's value
is a display string: `"2 (Reverse)"`, `"12.5 m"`, an enum label. Comparing a
clause against that would mean parsing presentation back into a number, and
would break the day someone rewords a label.

## What the operator sees

The field inspector's status line, when any rule fires:

    A receiver would not process this frame — PKT_DIR = 0 — packet direction
    unidentified or spare  [31.16.1]

Reject reasons take precedence over the field/byte-range summary: a frame the
receiver would drop is a more urgent fact than how many of its fields carry
byte ranges. Every rule that fires is listed, not the first.

The wording stays a report. "A receiver would not process this frame", never
"invalid" and never "fail" — and when nothing fires, nothing is said, because
silence means no rule in `rejectrules.xml` matched and that is not the same
claim as the packet being good.

The rules load beside the schema, on the same terms: a failure is a packaging
error worth a warning, not a reason to refuse to start. The console then
reports no reject conditions, which is honest — it has none to check against.

## The test that matters

`rejectrouting` runs a real SLRP capture line — one already in the suite as a
CRC fixture, recorded off equipment — all the way through `describe()` to the
evaluator.

It asserts the frame trips **nothing**. If that ever fires, either a rule is
wrong or the value plumbing is, and both deserve to fail a build: a false
reject reason on a good frame is how an operator stops trusting the feature.

Then it flips `PKT_DIR` to the spare value on the same decoded map and asserts
exactly one rule fires, citing 31.16.4 — which is what proves the first check
was not passing merely because nothing was connected.

## Files touched

    capturedecoder.{h,cpp}      rawValues through describe() and schemaRows()
    fieldinspector.{h,cpp}      findings shown, rejectReasons() accessor
    rejectrules.cpp             loads beside the schema at first use
    tests/test_rejectrules.cpp  rejectrouting suite

## Still open

From the review, unchanged: the aspect widths, the seven `PKT_TYPE` false
positives, the narrative conditions, and the FRAME_OFFSET 14 guard — which
needs a condition expression in the rule format, since it depends on
absolute-location RFID or station section. Inferring our own ID from ARP/LSRP
for the `DEST_ONBOARD_ID` rules is also still to do.

## Verification

    118 suites, 2993 checks, 0 failed      (was 117 / 2985)
    menu audit passed
    headless smoke run clean, rules load with no warning
