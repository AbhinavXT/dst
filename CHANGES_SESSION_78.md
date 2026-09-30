# Session 78 — Live data, batch A: big numbers, change highlighting, pins, loco check

The first of three batches. B (window layouts, crash recovery, undo) and C
(settings export/import, colour-blind-safe status colours) follow in a new
chat. See "Next" at the end.

## 1. Change highlighting — Live Loco Console

A field whose value just changed is marked: an accent tint and **bold** for
2 s, then the tint fades out by 4 s.
- **Where:** in every packet tab, so a changing value stands out in a
  170-row table.
- **What counts as a change:**
  - Rows are keyed by field name *and* occurrence, since names repeat inside
    some structs.
  - A type's first frame marks nothing, because there is no "before".
  - Switching to another loco resets the history, so its values aren't
    flagged as changes.
- **With Find:** Find's match tint is applied after the change marks, so a
  search result is never hidden by one.
- **Switch:** "Highlight changes" in the top strip, on by default,
  remembered.

## 2. Big-number mode — Live Loco Console, configurable

A **Big numbers** button shows a row of tiles above the packet tabs, each
one field in large type (3× the app font, so text size and F11 make them
bigger still).
- **Defaults:** **Speed**, **Mode**, **Frame clock** and **Brake**, from the
  loco's own LSRP, falling back to ARP when LSRP is absent.
- **Frame clock:** FRAME_NUM (seconds since midnight + 1) is shown as a
  clock, **16:29:16**, with the raw value underneath.
- **Brake:** the raw `Brake_Applied` value. The schema gives its bits no
  meaning; with a mapping it could say "EB".
- **Detail line:** where each value came from ("from lsrp"). A value
  older than 3 s is **muted**, with its age, so a stale number is never
  mistaken for a live one across the room.
- **Configurable:**
  - right-click any field in any tab ▸ **Show as big number**;
  - right-click a tile ▸ Rename, Move left/right, Remove, Reset to
    defaults;
  - the tiles and whether the panel is shown are remembered.

New `bignumberpanel.{h,cpp}`; `livefields.{h,cpp}` holds the field
references it shares with the pins.

## 3. Pin a value to the status bar

Right-click any field in the Live Loco Console ▸ **Pin to status bar**. The
value then shows in the main window's status bar as e.g. "1_1 · Mode: 2
(Staff_Responsible)", whatever window is in front.
- **Updates on its own:** it reads the capture stream directly, so it keeps
  updating with the Loco Console closed.
- **Display:** FRAME_NUM shows as a clock. A value not refreshed for 5 s is
  muted, with its age in the tooltip.
- ✕ unpins. Up to 6 pins, no duplicates, remembered between runs.

The field menu also has **Copy value**. New `statuspins.{h,cpp}`.

## 4. Check what the loco actually holds — Loco Configuration

The VCC prints its LOCO_INFO (`@linfo`) periodically. Each one is compared
with what this configuration last sent, the loco being matched by
`loco_unit_id`. The result shows under "Last sent":
- **✓ Loco 7_1 holds exactly what was last sent** (seen N s ago). This is
  the confirmation a send never got before.
- **Waiting for loco 7_1's next @linfo** since the send at HH:MM:SS. An
  `@linfo` received *before* your send isn't reported as a failure.
- **✗ Loco 7_1 holds something else: 3 fields differ.** The tooltip lists
  each field with the value sent and the value the loco has.
- **For a configuration never sent from here:** whether the loco matches
  it.
- A loco reporting a LOCO_INFO whose `loco_info_crc` doesn't match its
  contents is called out, not compared.
- **Load the loco's values…** copies what the loco reports into the
  editor.
- MainWindow hands the window the dispatcher (`setLiveSource`). Each
  `@linfo` is parsed with the same schema layout as everything else.

Also fixed here: messages read "1 field(s) differ"; they now say "1 field"
or "3 fields", including the Send confirmation from session 70.

## Tests

- **`livefields` (10):**
  - references round-trip; sources round-trip for every type, including
    "nms hlth";
  - indented field names are found;
  - FRAME_NUM → clock, including the ends of the day and bad input;
  - the default tiles.
- **`lococonsolelive` (19):** the real Loco Console fed **two real `@lsrp`
  lines** from the replay files.
  - Big numbers show 50 km/h, 2 (Staff_Responsible), 16:29:16 with
    FRAME_NUM 59357, and Brake 0.
  - After the second frame: 45 km/h and 6 (On_Sight).
  - TRAIN_SPEED is highlighted and bold; an unchanged field is not; the
    first frame marks nothing; switching the highlight off clears marks.
  - Tiles can be added, and tiles and visibility are remembered.
- **`statuspins` (11):**
  - — before data, then "1_1 · Mode: 2 (Staff_Responsible)" from the real
    line;
  - FRAME_NUM as a clock; duplicates refused; at most six; ✕;
  - remembered between runs.
- **`locoinfolivecheck` (10):** the real Loco Configuration window.
  - Matches a never-sent configuration; one edit is reported as 1 field.
  - After a send: "Waiting…"; then ✓ when the matching `@linfo` arrives.
  - A differing `@linfo` gives ✗ 1 field.
  - Another loco (other `loco_unit_id`) is ignored.
  - A bad `loco_info_crc` is called out.
- **`menuaudit`:** the pins are in the main window's status bar and
  reachable from the Loco Console.
- `lococonsolewindow.cpp` and `replaywindow.cpp` are now in the unit-test
  build, which is what made the Loco Console testable.
- **Checked by eye:** the Loco Console with big numbers and a highlighted
  change, in Light and Nord.

## Next (batch B and C, for a new chat)

- **B:** save and switch window layouts (shares its core with crash
  recovery); crash-safe session recovery (the disk log already survives a
  crash; the workspace and pop-outs only save on a clean exit); undo for
  destructive clicks (Clear this tab, Delete configuration, …).
- **C:** settings export/import (themes, tags, flasher profiles, loco
  configurations, layouts, pins); colour-blind-safe status colours.

## verify.sh

Full run, **14/14 stages green, 0 failed**: validators 11/11, unit suite
135 suites / 3952 checks (+ `livefields`, `lococonsolelive`, `statuspins`,
`locoinfolivecheck`), menu audit 92 ok, headless smoke alive. Built with
Qt 5.15 only.
