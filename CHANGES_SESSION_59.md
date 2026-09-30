# Session 59 — Navigating 4694 curves

## The finding that drove this

All 4694 `@uba` frames in `81_2_15092026_121737.log` are **byte-identical** —
one distinct payload, FNV-1a fingerprinted. The curve never changes for the
whole 19-minute capture. Scrubbing it frame by frame shows a still picture,
and nothing in the panel said so.

## What was added

**Change index.** `Braking::significantlyDifferent(a, b, locTol=0.5 m,
speedTol=0.1 m/s)` asks the operator's question — did the target move or change
type, did a segment appear or disappear, did a boundary shift — rather than
comparing fingerprints. On a moving train every frame differs in the last
decimal, so fingerprint equality would flag all 4694 as changes and make
"jump to next change" useless. The exact fingerprint is kept separately, for
counting distinct frames.

**Controls.** `|◀` / `▶|` step to the previous/next *change*, skipping runs of
identical frames. `◀` / `▶` still step one frame. A **Changes only** checkbox
remaps the scrubber to address only the frames where the curve changed — with
this capture it collapses to a single position, which is the honest picture.

**Change strip** above the scrubber: a tick per change across the session, plus
a cursor. Click to jump. One tick at the far left says "nothing happens here"
faster than any amount of dragging.

**Status line** leads with it:
`4694 frames · the curve NEVER CHANGES in this capture (4694 identical frames)`
and otherwise `N changes · M distinct frames`.

Live capture extends the index incrementally rather than rebuilding it.

## Verified

```
REAL LOG: frames=4694  distinct payloads=1  changes=1
SYNTH   : frames=300   changes=6 at 0 50 100 150 200 250
          (sub-tolerance jitter correctly ignored)
```

Build clean, `validate_uba.py` 4694/4694.
