# Session 58 — Layout-driven decode, EBD + SBD curves

The re-sent log is byte-identical to the one already mined
(`md5 da054d23…`, 4694 frames, all carrying the same static EOA target), so it
holds no new information about the segment-count question. Rather than stay
blocked on the emitter, the decode is now **driven by the frame size** instead
of by a constant.

## The problem this removes

| | |
|---|---|
| Declared `Target_Internal` | `Target`(17) + `CURVE[2]` × `CURVE_SEGMENT[2*MAX_DEC_UNITS+1 = 11]` = **1073 B** |
| Every captured `@uba` frame | **452 B** = 17 + 9×48 + 3 |

`uniform_braking.c` indexes `curves_for_target[UBA_EBD]=0` and
`[UBA_SBD]=1`, so the full struct is two curves. Nine slots cannot be two
curves of anything, so the emitter is not writing the whole struct — and the
emitter still has not been seen.

Hard-coding either number is wrong. Hard-coding 9 breaks the day the firmware
sends the full struct; hard-coding 11×2 breaks today. Worse, either mistake
parses with the wrong stride and produces a **plausible-looking wrong curve**
rather than an obvious failure.

## What changed

`Braking::detectLayout(payloadBytes)` maps a size onto
`{targetBytes, curves, segmentsPerCurve, trailing}`:

```
   452 B -> 17 + 1 x  9 x 48 + 3    documented   "1 curve x 9 segments (as captured)"
  1073 B -> 17 + 2 x 11 x 48        documented   "full Target_Internal (EBD + SBD)"
  1076 B -> 20 + 2 x 11 x 48        documented   "full Target_Internal, 4-byte enum"
   545 B -> 17 + 1 x 11 x 48        documented   "1 curve x 11 segments"
  other  -> as many whole 48 B segments as fit   UNDOCUMENTED, flagged
```

An unfamiliar size still decodes — as many whole segments as fit after a
17-byte `Target` — but is marked undocumented and surfaces in the panel and
status line with a ⚠. Every number under it is only as trustworthy as the
stride that produced it, so the guess is labelled as a guess rather than
silently blended in with recognised frames.

A frame too short to hold a `Target` plus one whole segment is still refused
outright (verified: a 40 B frame returns `valid = false`).

## Model reshaped

`Snapshot` now holds `QVector<Curve>` rather than a flat segment list.
`Curve` carries the per-curve operations (`activeSegments`, `span`, `speedAt`)
and its name from `CURVE_TYPE` — **EBD** (0), **SBD** (1). `Snapshot::span()`
is the union across curves; `Snapshot::speedAt(loc, out, curveIndex)` takes a
curve, or scans all of them when passed -1.

Panel and plot follow: a **Curve** selector (All / EBD / SBD) that is populated
from what the frames actually contain — not from
`NUM_OF_CURVES_FOR_EACH_TARGET`, because offering an SBD entry no frame carries
would be a dead control. The segment table gains a `curve` column and the
summary reports extent, entry speed, terminus and speed-at-target per curve.

On the chart, **curve 0 is solid and curve 1 dashed**, with a legend that
appears only when there is more than one curve to tell apart. The distinction
is carried by line *style*, not colour, so it survives a greyscale printout and
a colour-blind reader; colour continues to separate the segments *within* a
curve.

## Verified

Whole project builds clean (`qmake && make -j8`, 0 errors). Schema gate
unchanged: `validate_uba.py` **4694/4694**.

The 452 B path, against the real capture:

```
REAL  valid=1  layout='1 curve x 9 segments (as captured)'  documented=1
      EBD  slots=9 active=4  span 3835.120..6348.000 m  0.0000..70.8333 m/s
```

The 1073 B path has **no capture to test against**, so it was exercised with a
synthetic full `Target_Internal` built from the real segments (SBD given a
doubled `A`, i.e. half the deceleration, so the two curves are distinguishable):

```
FULL  valid=1  layout='full Target_Internal (EBD + SBD, 11 segments each)'
      EBD  slots=11 active=4   SBD  slots=11 active=4
      EBD speed @6000 m = 93.34 km/h    SBD @6000 m = 66.00 km/h
      polylines EBD=4 SBD=4
```

Both were rendered offscreen and inspected — `braking_curve_render.png`
(single curve, real data) and `braking_curve_ebd_sbd.png` (EBD solid + SBD
dashed, synthetic).

**This is the honest limit of what can be claimed:** the two-curve path is
mechanically correct and exercised, but no real two-curve frame has ever been
seen. If the emitter turns out to pack the curves differently — interleaved,
SBD first, or a purpose-built log struct rather than the raw `Target_Internal`
— the 1073 B branch is where that would need correcting.

## Still the one open question

`ui_capture.c`, or whichever file calls the capture emit for `@uba`. The single
line that passes the pointer and length is enough. It settles whether those
nine segments are the EBD curve alone, a flattened subset, or a purpose-built
log struct — and it is the last thing standing between this panel and being
provably right rather than defensibly right.
