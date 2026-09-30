# Session 81 — Loco config confirmation, several fields per plot, speed vs distance, export, run summary report, clock-skew alarm

## 1. Loco Configuration — the loco's answer is recorded

The window already compared each `@linfo` with the last send (the "Loco
check" line). What was missing was a record of the answer, and a word when
no answer comes.

- **Recorded.** The first `@linfo` after a send is judged once and appended
  to `loco_config_history.jsonl` as a `"kind":"verification"` line: loco,
  when it answered, match or not, and each differing field ("frame_cycle:
  expected 3, loco has 7").
- **History dialog.** A **Loco confirmed** column: `✓ 7_1 at 14:03:22`, or
  `✗ 7_1: 3 differ` with the fields in its tooltip and in the detail pane.
  A send with no answer says "Not confirmed".
- **Notification.** The main window announces the verdict (Info on a
  match, Warning otherwise), since the window may be behind others when
  the answer arrives.
- **Overdue.** When no `@linfo` has arrived after 3× that loco's own print
  interval (never under 60 s), the check line warns. It also says why that
  may be: the VCC takes the config only in application mode, and `@linfo`
  must reach this PC.
- Verification lines are not sends: `readAll()` skips them without
  counting them as damaged.

## 2. Field over time — several fields on one time axis

- **+ Add field** (the same Packet ▸ Field menu), up to **6** fields. Each
  one has a coloured chip with ✕ to remove it. The first field cannot be
  removed.
- **Lanes by default**: one per field, each with its own value axis and
  title in its colour. **One axis** overlays them, for fields in the same
  unit (e.g. TRAIN_SPEED against a permitted speed). The shared axis is
  titled by that unit, or "value (mixed units)".
- **Shared cursor.** Hovering draws one line through every lane and reads
  every field at that instant (the sample at or before it). With several
  fields each is named by packet, so `lsrp ▸ LOCO_MODE` and
  `arp ▸ LOCO_MODE` are never confused.
- **Zoom behaves as before, for all fields together:**
  - wheel zooms time; Shift+wheel zooms the values of the lane under the
    cursor;
  - a box drag zooms time (and the values of that lane if the box has
    height);
  - zooming in re-reads every field at full resolution.

## 3. Speed vs distance — Tools ▸ Monitor ▸ Speed vs distance… (Ctrl+Alt+V)

- **From `@dmi`**, all from the same frame: `abs_loco_loc`, `train_speed`,
  `speed_limit_permissible`, `target_distance`, `target_speed`,
  `loco_mode`. Speed from one packet paired with location from another
  would pair two instants.
- **Falls back to `@lsrp`** (ABS_LOCO_LOC, TRAIN_SPEED) when a tab has no
  DMI, and says it has no permitted speed or targets.
- **Draws:**
  - actual speed;
  - permitted speed as a dashed step;
  - targets as markers, target_distance ahead in the direction of travel,
    each distinct target once;
  - samples above permitted as red dots, with a count.
- **Direction from the locations** (net change; jumps over 500 m are
  resets). A train counting down has the axis reversed so it always runs
  left to right, and the axis title says so.
- **Braking curve.** The firmware's own `@uba` curve in force at the
  cursor (EBD solid, SBD dash-dot), converted from m/s. It is drawn only
  if it lies within 5 km of the train's locations. Otherwise it is
  reported as not on the same scale, rather than drawn somewhere
  meaningless.
- Same zoom controls as the field plot (distance on the wheel, speed on
  Shift+wheel). Clicking a point jumps to its message. Speed 511
  ("unidentified") is skipped and counted.

## 4. Export — both plot windows

**Export ▸ Save image (PNG)… / Save data in view (CSV)… / Copy image.**
- The image is the canvas, legend and axis titles included, so it stands
  on its own in a report.
- **Field CSV:** one row per distinct sample time in view, one column per
  field (`lsrp.LOCO_MODE`). A cell is empty where that field had no sample
  then; nothing is carried forward into it.
- **Speed–distance CSV:** time, location, speed, permitted, target
  distance and speed, overspeed flag, mode, for the stretch in view.

## 5. Run summary report — Tools ▸ Monitor ▸ Run summary report… (Ctrl+Alt+R)

One pass over the current tab, shown in a window. **Save HTML…** writes one
file that opens in any browser and prints to PDF; **Copy** gives the text.
It reads:

| Section | From |
|---|---|
| Span, duration, rows, rows per packet type | every row |
| Silences over 5 s, with the last packet before each | every row |
| Loco mode: first, then each change, with the schema's names | LSRP LOCO_MODE |
| Emergency status ≠ 0, as episodes | LSRP EMERGENCY_STATUS |
| Highest speed; above-permitted episodes (worst, where) | DMI (or LSRP) |
| RFID tags: each new tag, distinct count | LSRP LAST_RFID_TAG |
| Loco/station clocks outside the accept window, as episodes | SLRP FRAME_NUM against the loco's own LSRP/ARP FRAME_NUM from ≤ 10 s before |
| Faults raised and cleared | NMS fault frames |
| SLRP frames matching a reject condition, by clause | `rejectrules.xml`, with the learned loco identity |

- It says **OBSERVED** and makes no pass/fail judgement: verdicts belong to
  the signatories.
- Long lists are capped at 200 and say how many there were in all.
- Shown as paper (dark ink on white) in every theme. Its colours are
  allowed by the contrast audit as a standalone HTML document, like the
  assertion report.

## 6. Clock-skew alarm

- The status bar already turned the loco–station gap red. Now a
  **notification** says so once per episode:
  - raised when the gap has been outside the window (more than 4 s old,
    or 2 s or more ahead) for **3 s**, since a single late frame is not a
    clock problem;
  - cleared after 3 s back inside, with how long it lasted and the worst
    gap.
- Only compared while both clocks were heard in the last 10 s. No data is
  neither "in step" nor "out".
- The same episodes, computed offline from the capture, are in the run
  report.
- **Covers the loco–station gap only.** This laptop's clock against the
  equipment matters only for what DLConsole sends. It stays in the status
  bar as before.

## Code

- New: `speeddistance.{h,cpp}`, `runreport.{h,cpp}`,
  `runreportwindow.{h,cpp}`, `clockskewalarm.{h,cpp}`.
- `fieldplot`:
  - `collectRowFields()` (several fields of one packet, row by row, raw
    integers);
  - `valueAtOrBefore()` and `seriesToCsv()`;
  - the canvas takes a list of series in lanes or overlaid;
  - the window has add/remove/overlay/export.
- `LocoInfo::Verification`, `SendHistory::appendVerification()` /
  `readVerifications()`; `LocoConfigWindow::verified` signal, `recheck()`.
- `brakingcurves.cpp` is now in the unit-test build (it wasn't before).

## Tests

- **`session81` (70).** Frames with chosen values are made from real
  captures by rewriting fields at the bit spans the decoder reports, then
  decoded again to prove it. Covered:
  - cursor semantics and CSV (empty cells, no carry-forward);
  - the real plot window: add, duplicate refused, lanes vs one axis, the
    limit of 6, remove, first field kept, PNG and CSV export;
  - speed–distance: direction (and resets), target placement, a 60-frame
    run with 5 samples above permitted, targets, stretch CSV, the LSRP
    fallback counting down, zoom, export, the "no @uba" note;
  - skew alarm: hold time, brief excursions, worst gap, no-data, clear
    time, the asymmetric window;
  - report: packet counts, the 12 s silence, the mode change with names,
    the tag, highest speed, the +5 km/h episode, OBSERVED and no verdict,
    every section, capped lists, HTML save;
  - offline skew from real SLRP frames: one episode from the first frame
    9 s out.
- **`locoinfolivecheck` (+8).** The confirmation is written against its
  send, once, and announced once. A verification line is not a damaged
  send. The overdue warning appears. A differing answer is recorded naming
  `frame_cycle` and announced as a mismatch.
- **Menu audit (+6, 127 ok).**
  - Tools ▸ Monitor has Speed vs distance (Ctrl+Alt+V) and Run summary
    report (Ctrl+Alt+R), and both open on the current tab.
  - The skew alarm is running on the real main window, and raises and
    clears.
- **Checked by eye** (offscreen, `DL_SHOTS`): the two-lane plot with the
  shared cursor, the speed–distance view with an overspeed stretch, and
  the report window.

## verify.sh

Full run, **0 stages failed**:
- validators 11/11;
- unit suite **142 suites / 4230 checks** (141 / 4151 before);
- menu audit **127 ok**;
- headless smoke alive.

Qt 5.15.13. Not yet seen on Windows.
