# Session 63 — Many targets per cycle, and the envelope across them

Every 10 ms the loco recalculates **every target ahead of it** — PSR, EOA, SVL,
TSR … — and builds both curves for each. So one cycle produces several `@uba`
frames, not one, and a single frame is one target out of several: meaningless
on its own. The panel now works in **cycles**.

## Grouping — and its honest limit

The frame carries **no target index and no cycle counter**, so grouping is a
heuristic: a new cycle starts when the inter-frame gap exceeds 5 ms, or when a
`(target_type, location)` pair already in the current cycle appears again.

That is sound for well-formed data and will mis-group if two distinct targets
share both a type and a location, or if a cycle straddles the gap threshold.
Every `Cycle` carries `heuristic = true`, and the summary says so where the
derived numbers are read:

> ⚠ inferred from timing and target identity — the frame carries no target
> index or cycle counter

**Two bytes in the emitter would remove the guess entirely**: a target index and
a target count per cycle. That is the single highest-value change available to
this panel right now, above anything further on the DLConsole side.

## Most-restrictive envelope

`Cycle::mostRestrictive(location)` returns the lowest allowed speed any target
permits there, **and which target imposed it** — because "why am I being braked
here" is answered by the target, not by the number. It drives the hover
readout, the search readout, and a heavy line on the chart.

Drawn **under** the per-target curves, not over them: an envelope painted on
top hides exactly what the operator is looking for. Gaps are left as gaps —
where no target covers a location the path breaks rather than bridging.

## Chart

Colour now separates **targets** when a cycle has more than one (which target
restricts you is the question), and falls back to separating deceleration steps
when there is only one. Line style still separates EBD from SBD. One dashed
marker per target, with the legend naming them rather than stacking labels that
would overprint when targets are metres apart.

Segment table gains a `target` column and lists every target × curve × segment
in the cycle.

## Verified

Synthetic three-target cycles (PSR @5200, EOA @6248, SVL @6900) at the real
10 ms cadence:

```
frames=15 -> cycles=5,  3 targets each
envelope:  5000 m ->  68.31 km/h  imposed by PSR
           5190 m ->  15.27 km/h  imposed by PSR
           6200 m ->  35.27 km/h  imposed by EOA
           6800 m ->  50.91 km/h  imposed by SVL
           4000 m -> (no target covers this)
time-gap split (50 ms between frames) -> 2 cycles, correct
```

Real capture still decodes as before: 4694 frames, one target, one cycle each.
Build clean; `validate_uba.py` 4694/4694.
