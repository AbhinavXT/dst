# Session 60 — 10 ms cycle: rate, gaps, and defaults that were wrong

The loco builds every target on a 10 ms cycle, so `@uba` can arrive at 100
frames/s. Two panel defaults were sized for a slow stream and were wrong.

## What the capture actually shows

Sequence numbers are a **global** capture counter across all message types and
are perfectly monotonic — 2012→8206 over 6195 lines, **zero gaps**. Nothing was
dropped by the logger. (An earlier reading of the uba-only sequence span as
"missing frames" was wrong; the span is just other message types interleaved.)

Emission is not a steady 100 Hz. Over 4694 frames and 67 active seconds:

```
gap with no @uba at all:  12:16:10 .. 12:16:45   (36 s)
frames/s clusters:        ~35-53   |   ~66-79   |   ~95-112
mean while streaming:     70.1 frames/s
```

Three tiers rather than a flat 100 Hz, plus a 36-second hole in the middle of a
102-second capture. Since the global sequence has no gaps, the logger did not
shed these — either the capture emitter declines to emit on some cycles, or
target-building itself stops. Worth a look on your side; the panel now surfaces
it rather than hiding it.

## Fixes

**Ghosts are now spaced by time, not frame index.** The old default drew the 6
frames immediately before the current one — at 100 Hz that is **60 ms** of
history, six curves drawn exactly on top of each other. Ghosts now spread over
a configurable span (`Ghosts: 6 over 5 s`), walking backwards one frame per
time step. This is the difference between the feature working and the feature
appearing to do nothing.

**Snapshot cap raised 20 000 → 200 000.** At 100 Hz the old cap was 3.3 minutes
and would have silently truncated any real run. Now ~33 minutes. The truncation
warning is unchanged.

**Status line reports rate and gaps**: frames/s while streaming, and
`⚠ longest gap N s at HH:MM:SS` whenever the stream stops for a second or more.
A curve stream that stalls for 36 seconds is a finding, and it is invisible if
the panel only ever prints a frame count.

Build clean; `validate_uba.py` 4694/4694.
