# Session 84 — DMI time travel

The DMI window can now show the panel **as it was** at any moment of a
recording or of the live log: click a row, or move a replay cursor, and the
panel shows exactly what the loco pilot had on screen then.

## 1. DMI window — ⏱ Follow cursor

A new switch next to the loco selector (remembered as `dmi/followCursor`,
off by default).

- **The rule.** For each loco, the panel is its **latest `@dmi` at or before
  the moment** — the DMI holds a frame until the next replaces it, so that is
  what was displayed.
  - In the tab clicked, "before" is by **row order**, so clicking an `@dmi`
    row shows exactly that frame.
  - In the other tabs it is by **receipt time**.
- **Stale.** A frame more than 3 s before the moment is drawn muted, as a
  stale live frame is, and the status line says "stale, muted".
- **None.** Nothing in the 10 minutes before the moment is "no @dmi" (blank
  panel, said so), not an ancient frame.
- **Which loco.** The loco of the clicked row (or the replay's active source)
  if it has a frame; otherwise the one selected, if it has; otherwise the
  first that has. The selector lists every loco with a frame at the moment.
- **Not mistakable for live.**
  - The title reads `DMI (LP-OCIP) — at HH:MM:SS`.
  - The status line (accent colour) reads e.g.
    `⏱ At 12:58:44.312 · tab Loco 1 · 1_1's @dmi of 12:58:43.900, 0.4 s before`.
- **Live frames** keep arriving underneath and are shown again the moment
  Follow is unticked; they never replace the moment on screen.
- **Save image…** saves the panel at the moment; B5/B6 carry the frame's own
  date and time.

## 2. What drives it

| Where | Moment |
|---|---|
| Main window, any tab | the current row (click, arrow keys, ribbon click, find, jumps) |
| Recorded session (.dlr) window | the current row; only that recording's tabs are searched |
| Replay (.cap) window | the cursor: timeline drag, slider, ← →, playback, event stepping, Event Log; changing the source turns the panel to it |

**Several windows open:** whoever moved last wins.

**Replay window:** new **⏱ DMI at cursor…** button (Ctrl+Alt+D in that
window) opens a DMI window already following that replay.

## 3. Cost

- **Nobody following:** every row change or cursor move only hands the
  broker a small resolver; nothing is scanned.
- **While a DMI window follows, main-window tabs:** a walk back from the row,
  bounded by the 10-minute window, one per tab. A tab is located by binary
  search on time.
- **While a DMI window follows, replay:** `@dmi` positions are indexed per
  source at load, so a cursor move is one binary search per loco.
- **Starting to follow** resolves the last place pointed at, if that window
  is still open.
- **Live tabs that trim:** the clicked row is found again (by pointer, near
  its time). Once it has been trimmed away there is no moment, rather than a
  wrong one.

## Files

- **New:** `dmitimetravel.h/.cpp`
  - the `DmiTimeTravel` broker (`offer` / `follow` / `momentChanged`);
  - `dmiMomentFromModels()`, `logModelRowAfter()`, `logModelRowOf()`,
    `dmiTabResolver()`, `dmiKeyOfText()`.
- `dmipanel.h/.cpp`:
  - follow mode (`setFollowCursor`, `showMoment`, `render`);
  - `dmiStateFromCapture()` (`dmiStateFromLine()` now uses it);
  - `DmiView::setEmptyText()`.
- `mainwindow.h/.cpp`: `offerDmiMoment()` on each tab's current-row change.
- `sessionwindow.cpp`: the same, per recording.
- `replaywindow.h/.cpp`: `dmiMomentAt()`, `openDmi()`, the button, the per-source `@dmi` index.
- Registered in `DLConsole.pro`, `tests/tests.pro`, `tests/menuaudit.pro`,
  `tests/shot.pro` (which also gains `dmipanel.cpp`, missing since session 83)
  and `tests/bench.pro`.

## Tests

**`session84` (51)** — real `@dmi` frames from `replay/`:

- **Helpers.**
  - An `@dmi` line's loco is read from its token.
  - The panel state from a parsed frame equals the one from its line.
- **One tab.**
  - A text row shows the `@dmi` above it.
  - An `@dmi` row shows exactly itself.
  - A frame beyond the 10-minute look-back is "none".
- **Several tabs.**
  - One frame per loco, in key order.
  - Another tab's later frame is not used; the clicked tab's same-time frame wins.
- **Trimming.**
  - The resolver finds the clicked row after a live trim.
  - Trimmed away: no moment.
- **Broker.**
  - Nothing is resolved without a follower.
  - The first follower gets the last offer; every move is delivered while followed; nothing after unfollowing.
  - A closed window's resolver is not run; destroyed followers drop out.
- **Window.**
  - Live by default.
  - Follow picks up the selected row, with status and title naming the moment and the frame's age.
  - Live frames do not overwrite the moment.
  - Turning to loco 2 shows loco 2 at the same moment.
  - A stale frame is muted and said to be; no frame is blank and said to be.
  - Unticking returns to live; the mode is remembered.
- **Replay (two real captures loaded together).**
  - At every cursor, each loco's frame is an `@dmi` of that loco at or before it.
  - Somewhere both locos have a panel.
  - Scrubbing forward never shows an older frame.
  - The button is on Ctrl+Alt+D and opens a DMI window already following.
  - Changing the source turns the panel to it; → moves the panel with the cursor.

**Not tested end to end:** the click in a main-window or session tab. That
hook is a few lines handing `dmiTabResolver()` (tested above) the clicked
row; it was checked by reading, and the app was exercised by the smoke run.

## verify.sh

Each stage run with the script's own commands, **0 stages failed**:

- validators 11/11;
- unit suite **145 suites / 4405 checks** (144 / 4354 before);
- menu audit **134 ok**;
- headless smoke alive (500 datagrams).

`shot.pro` and `bench.pro` were updated but not built (not part of the gate).
