# Session 57 — Braking curve panel (speed vs distance, scrubbable)

Three new files for the braking logic, one new window, and the first session in
this project where the C++ was **actually compiled and run** rather than
reasoned about.

## Build environment — the "no Qt compiler" note is now out of date

Qt5 dev packages install cleanly from the Ubuntu archive in the sandbox
(`apt-get install qtbase5-dev`). The whole project now builds:

```
qmake && make -j8   ->   0 errors, binary produced
```

This changes the validation posture. `engine.py` is still the gate for schema
semantics (it is the reference implementation), but C++ correctness no longer
has to be argued — it can be compiled, linked and executed. The braking maths
below was verified by running it against the real capture, not by inspection.

## What the firmware sources settled

`curve_manager.c` confirms the relation reverse-engineered from the wire last
session, exactly:

```c
crv_range.eqn.A = -(1.0 / (2 * net_accln));
crv_range.eqn.C = ((SQUARE(final_speed) + 2*net_accln*end_loc) / (2 * net_accln));
```

So `location = A*speed² + C`, `a = -1/(2A)`, and `C` carries the stop location.
To plot speed against distance the relation is inverted:
`speed = sqrt((location - C)/A)`.

Two things the sources add that the wire alone could not have told us:

- **`CreateCurveSegmentHorizontalLine` stores `A = 0, C = speed`** — a flat
  speed limit, not a quadratic. Inverting blindly divides by zero, and one NaN
  in a `QPainterPath` takes the whole polyline with it. `A == 0` is branched on
  everywhere and covered by a test.
- **`IsCurveRangeValid` is a memcmp against zero**, so an unused array slot is
  all-zero — not a segment at the origin. `Segment::isEmpty()` reproduces it
  field for field (a real segment may legitimately have `A == 0` and `C == 0`).

**The segment-count question is still open, and the sources deepen it.**
`braking_common_include.h` says `MAX_DEC_UNITS 5` and `curve_manager.h` says
`NUM_OF_CURVES_FOR_EACH_TARGET 2`, i.e. `sizeof(Target_Internal)` = 17 + 2*11*48
= **1073 B**. The wire carries **452 B** = 17 + 9*48 + 3. So the capture is
*not* a whole `Target_Internal`, and `curves_for_target[]` is indexed
`[UBA_EBD]=0, [UBA_SBD]=1` (`uniform_braking.c:162,166`) — two curves, EBD and
SBD. Nine slots cannot be two curves of anything. The emitter (`ui_capture.c`)
was not among the uploaded files; **it is the one file needed to say whether
those nine segments are the EBD curve alone, a flattened subset, or a
purpose-built log struct.** Until then the panel draws what arrives and calls
it one curve.

## New files — logic separated from UI

| file | role |
|---|---|
| `brakingcurves.{h,cpp}` | **No Qt widgets.** `Segment`, `Snapshot`, frame decode, curve inversion, polyline sampling, session walk. Unit-testable and reusable. |
| `brakingcurveplot.{h,cpp}` | The speed-vs-distance chart widget. Paints only; owns no data. |
| `brakingpanel.{h,cpp}` | The window: scrubber, tables, live subscription. |

Menu: **Monitor → Braking Curves…** (`Ctrl+Shift+B`).

## The panel

- **Speed (km/h) on y, track location (m) on x.** Speed is converted only at
  the label; the wire and all maths stay in m/s. Distance is absolute track
  location, unshifted, so it can be correlated against an RFID or MA end point.
- **Time scrubber** across every `@uba` frame in the tab — pick a moment, see
  the curve that was in force then. Prev/next step one frame. `Follow live`
  pins to the newest frame and **auto-releases the moment the operator drags**.
- **Axes locked by default**, fitted once over the whole session. Autoscaling
  per frame makes a stationary curve look like it is moving and a collapsing
  one look stationary — the exact opposite of what a scrubber is for.
- **Ghosts**: N earlier curves drawn faintly behind the current one, so change
  reads as movement rather than as a still frame.
- **One colour per segment**, with junction dots. The deceleration steps are
  the structure of the curve, and they are invisible on a single smooth line.
  Table row `#` matches plot colour order.
- **Hover crosshair** reads out `(distance, allowed speed)` through the real
  curve equation, not interpolated off the polyline. Off the curve it says
  *(off curve)* rather than showing 0 — "not on the curve" and "permitted speed
  zero" are different facts and only one of them is safe to imply.
- **Show frame in log** jumps the main window to the capture line, by
  **timestamp** via the existing `jumpToEntry` — not by row index, which would
  go stale when the model's ring buffer wraps and is unknown on the live path.

## Verified by running it

Against `81_2_15092026_121737.log`:

```
payload 452 bytes -> valid, target 6248.000 m, EOA, 4 active segments
span            loc 3835.120..6348.000 m   speed 0.0000..70.8333 m/s
polylines       4, 192 points, 0 non-finite
monotonic toward target: YES (largest upward step 0.0000 m/s)
horizontal segment (A=0): v=13.8889, finite, no divide-by-zero
empty slot isEmpty: true
```

Junction continuity — the curve has **small real discontinuities** at segment
boundaries, because `start_loc` is not exactly `x(higher_speed)`:

```
junction 0 @ 5959.064 m:  27.7778 -> 27.3686 m/s   (-1.47 km/h)
junction 1 @ 6156.890 m:  19.4444 -> 19.1970 m/s   (-0.89 km/h)
junction 2 @ 6250.549 m:  13.8889 -> 13.9607 m/s   (+0.26 km/h)
```

These are in the firmware's own data. Segments are therefore drawn as
**separate polylines** — bridging them with a connecting line would draw a
curve the firmware does not contain.

The plot was rendered offscreen to PNG and inspected. Three defects were found
and fixed that way, none of which inspection would have caught:

1. y-axis title clipped to "peed (km/h)" — it shared a margin with the tick
   labels; it now has its own gutter.
2. x-axis title clipped at the widget edge — the bottom margin allowed for one
   text row, but there are two.
3. The loco marker was the same blue as segment 0, so it read as part of the
   curve. Now dash-dot in the foreground colour, and labelled.

A bug in the summary table was also caught by running it: "curve end vs target"
searched for the segment end *nearest* the target and reported 2.5 m. The
curve's actual terminus is 6348 m — it runs **100 m past** the 6248 m target —
and the allowed speed where the target actually is comes out at 14.06 m/s
(50.6 km/h), not zero. Both are now stated outright; the old number was
meaningless and reassuring, which is the worst combination.

## Files touched

`DLConsole.pro` (3 sources, 3 headers), `mainwindow.{h,cpp}` (menu action,
slot, include, jump wiring), plus the six new files above.
