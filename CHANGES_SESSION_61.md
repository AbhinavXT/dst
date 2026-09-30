# Session 61 — The single logged curve is NOT necessarily EBD

## The mislabel this fixes

Session 58 named curve index 0 "EBD" unconditionally. For a single-curve frame
that is a guess dressed as a fact, and `uniform_braking.c:159-167` shows why it
is the wrong guess to bake in:

```c
CURVE mrdt_curve;
if (brk_type == UBA_EB)       mrdt_curve = mrdt_target.curves.curves_for_target[UBA_EBD];
else if (brk_type == UBA_FSB) mrdt_curve = mrdt_target.curves.curves_for_target[UBA_SBD];
```

The firmware picks one of the two by `brk_type`, and `brk_type` is not in the
frame. So a 452 B capture holds EBD *or* SBD, and labelling it EBD inverts the
meaning of the plot whenever the loco is in full-service braking.

`Layout::curveNamesKnown` now gates this. A frame that carries
`curves_for_target[]` itself (1073/1076 B) has real CURVE_TYPE indices and its
curves are named EBD and SBD. A single-curve frame shows **"curve"**, and the
layout string says so outright: *"EBD or SBD, selected by brk_type, not stated
in the frame"*.

## Deceleration signature

Since the frame will not say which brake built the curve, the summary now
reports the only evidence it does carry. `net_accln = brake decel ∓ gradient ×
9.81` (`gradient_manager.c`), so the spread across segments is gradient, not a
different brake, and the largest value is the closest thing to the unloaded
brake rate:

```
curve deceleration    0.9376 … 1.0000 m/s²   (spread = gradient 6.36 ‰)
```

For this capture that reads as base **1.0 m/s²** with a ~6.4 ‰ (1 in 157)
gradient on the outer segments. Against the reference table in
`braking_params_manager.c` — EB 1.0 flat, FSB 0.9 / 0.6 by speed range — that
points to **EBD**. Evidence, not proof: those table values are commented out in
the source, so runtime config may differ.

## Note on multiple targets

Targets are recalculated every 10 ms and each gets both curves, but `@uba`
carries `mrdt_target` — the most restrictive displayed target — not the whole
target set. That is consistent with the capture: one distinct payload across
4694 frames means the MRDT never changed, not that only one target existed.
If per-target curves are wanted in the panel, the emitter has to send them;
nothing in the current frame distinguishes one target from another.

Build clean; `validate_uba.py` 4694/4694.
