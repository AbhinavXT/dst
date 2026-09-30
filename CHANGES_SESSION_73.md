# Session 73 — Find in the Live Loco Console

The Live Loco Console's packet tabs can now be searched. `linfo` alone is
~170 rows, and until now the only way to find one member was to scroll.

## How to use it

- **Open find:** **Find…** (top right), **Ctrl+F**, or **F3**. The bar
  appears under the tabs.
- **Type part of a field name.** Case doesn't matter, and `_` and space
  are the same, so `speed margin` finds `speed_margin_eb`. Every word must
  appear, so `margin eb` narrows it further.
- **Move between matches:** **Enter** or **F3** for next, **Shift+Enter**
  or **Shift+F3** for previous. It wraps at the ends. The count shows as
  "2 of 8".
- **Only matching rows** hides everything else.
- **Values too** also searches the Value column, e.g. to find which field
  holds `127.0.0.1`.
- **Not on this tab?** The bar names the tabs that do have it (e.g. "not
  here — linfo 3") and **Enter** switches there.
- **Close:** **Esc** or **✕**. Tints and hidden rows are cleared.

## Why it holds still on a live stream

The console rebuilds every table from scratch whenever frames arrive (it
refreshes every 250 ms), so a find tied to row numbers would flicker or
jump.
- `TableFindBar` holds the current match **by field name** and re-applies
  itself after each refresh.
- Names aren't unique: `speed_margin_warning` is in LOCO_INFO twice, at the
  top level and in `national_values`. So the match is tracked as (name,
  which occurrence). Following the second one stays on the second one.
- **A refresh never scrolls.** Only Next and Previous move the view, so you
  can read elsewhere in the table while a search is open.
- Matches use the same accent tint as the log find.

## Files

- **New:** `tablefindbar.{h,cpp}`, a reusable find for any Field | Value
  `QTableWidget`. The replay window's tables could use it with a few lines,
  if wanted.
- **New test:** `tests/test_tablefindbar.cpp`.
- **Changed:** `lococonsolewindow.{h,cpp}` (Find button, bar, shortcuts,
  reapply after refresh), `DLConsole.pro`, `tests/tests.pro`,
  `tests/menuaudit.pro`, `tests/menuaudit_main.cpp`.

## Tests

- **`tablefindbar` (36 checks):** the matching rule, on a table of all 171
  LINFO field names from the schema, rebuilt the way the console rebuilds
  it.
  - Count; first match on typing; Next, Previous and wrap.
  - A refresh with a row inserted above keeps the same field.
  - The duplicate `speed_margin_warning` stays on the second copy.
  - A refresh doesn't scroll, while Next does.
  - Only-matching survives refreshes.
  - Values too; the other-tab hint and switch.
  - Leaving a tab and closing leave the table clean.
- **`menuaudit`:** opening Monitor ▸ Live Loco Console gives a window with
  the find bar and Ctrl+F.
- **Checked by eye:** a render of the real console decoding a real `@linfo`
  frame (CRC ok) with "speed margin" found: 8 matches, 2 of 8 current.

## verify.sh

Full run, **14/14 stages green, 0 failed**: validators 11/11, unit suite
126 suites / 3441 checks, menu audit 73 ok, headless smoke alive. Built
with Qt 5.15 only.
