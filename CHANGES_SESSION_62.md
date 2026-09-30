# Session 62 — Search bar: by time, by absolute location

A `Find:` row above the change strip, with a mode selector.

## Time mode

Accepts `12:16:04`, `12:16:04.250`, `12:16`, or a full ISO timestamp. A bare
time is resolved against the day the capture was taken. Jumps to the frame in
force at that instant (`indexAtOrBefore` — the frame **at or before** the time,
which is the one that was actually in effect).

Matched against **host receive time**, not the loco RTC. The RTC in the capture
line is 1 s resolution, which cannot separate frames arriving every 10 ms — at
100 Hz a whole second is 100 frames, so an RTC match would be a 100-way tie.

Verified against the capture at its real 10 ms cadence:

```
  before first   -> index    -1  (none)
  exact first    -> index     0  (t=0 ms)
  +1.005 s       -> index   100  (t=1000 ms)
  +30 s          -> index  3000  (t=30000 ms)
  after last     -> index  4693  (t=46930 ms)
```

## Location mode

Accepts `6248`, `6248.5`, `6248 m`. Finds frames whose **target** is within a
metre of that chainage. If nothing is that close it falls back to the single
nearest and says `nearest target N m away`, rather than reporting "no match"
for a location that merely sits between targets.

It also does the thing the operator usually wants next: the searched location
is **marked on the plot** with the allowed speed there, read through the real
curve equation. So a location search answers *what did the curve permit at this
chainage* as well as *which frame*. Off the curve it says `(off curve)` rather
than showing zero, and the axes expand to include the queried location so a
search never silently looks like it did nothing.

## Stepping matches

Enter or `Find / Next` advances through the match set, wrapping, with
`match 3 of 12   12:16:04.250` beside the box. Editing the query invalidates
the set so the next Enter starts fresh rather than stepping stale hits.
Searching releases `Follow live`, for the same reason dragging does.

## Fixed while testing

The query label and the loco label shared a baseline and overprinted whenever
the two markers were near each other (visible with a query at 3000 m and the
loco at 4200 m). The query label now sits one row above.

Build clean; `validate_uba.py` 4694/4694.
