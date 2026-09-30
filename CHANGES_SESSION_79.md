# Session 79 — Batches B and C: layouts, crash recovery, undo, settings transfer, colour-blind-safe colours

Everything in session 78's "Next" list. Layouts and crash recovery share one
core (`WorkspaceSnapshot`); undo is used by both, and by the settings import.

## 1. Window layouts — View ▸ Layouts

- **Save current layout as…** asks for a name; a same-named layout is
  replaced after a Yes.
- **Each saved layout** is an item in the menu, the first nine on
  **Ctrl+Alt+1 … 9**; its tooltip says what it holds ("5 tabs (1 hidden),
  2 pop-outs").
- **Delete layout ▸** one submenu item per layout.
- **What a layout holds:** the main window's size and panels, every tab in
  bar order, which are hidden, the tab in front, which tabs are popped out
  and where.
- **What it does not:** tool windows (Loco Console, fault panel, …) and
  per-tab filters. A filter restored unasked hides live traffic, which is
  why the workspace never stored one either.
- **Switching:**
  - tabs the layout does not name are hidden, as a closed tab always is:
    still watched, and back if they speak;
  - a pop-out for a loco not heard yet waits, as at startup.
- **Undoable:** Ctrl+Z after a switch goes back to the arrangement before
  it; a deleted layout goes back in its old place.
- **Stored in** `window_layouts.json` beside `dlconsole.ini`.

## 2. Crash recovery

The disk log already survived a crash. The arrangement did not: workspace,
pop-outs and panels were written only in `closeEvent`.

- **While running:** every 5 s the arrangement is compared with the last
  one written, and `session_recovery.json` is rewritten only if it changed
  (atomically, via `QSaveFile`).
- **The flag:** `session/running` in the ini is set at startup and cleared
  by a clean close (and by the destructor, which a crash never reaches).
- **Next start after a crash, kill or power cut:** the snapshot is written
  into the ordinary workspace/pop-out/layout keys *before* the ordinary
  restore runs. One restore path, not two.
- **Then a warning notification:** "DLConsole did not close cleanly last
  time. Restored the workspace it had: 6 tabs, 1 pop-out, as it was at
  14:02:31." Its detail says the disk log is complete up to the stop.
- **Refuses rather than guesses:** a damaged or foreign recovery file is
  ignored; a file left behind by a *clean* run is stale and is deleted.

## 3. Undo — Edit ▸ Undo (Ctrl+Z), and an offer in the status bar

After a destructive click, **"↶ Undo Clear tab L1_V1"** appears in the status
bar for 15 s. The Edit menu item always names what it will undo.

| Window | Undoable |
|---|---|
| Main | Clear this tab · Clear all tabs · Remove bookmark · Remove all bookmarks · unpin a status-bar value · remove a watch · switch / delete layout · Import settings |
| Loco Configuration | Delete configuration (back in its old place) · Reset all fields to defaults |
| Loco Console | Remove a big number · Reset big numbers |
| Firmware Flasher | Delete profile |

- **One log per window:** Ctrl+Z in the Loco Configuration never brings
  back a tab cleared in the main window ten minutes earlier.
- **Cleared rows come back in front of** anything that arrived since; if
  capacity forbids all of them, the newest of the old rows win, and rows
  that arrived since are never pushed out.
- **Memory:** at most 20 steps and 1,000,000 held log rows per window,
  oldest step dropped first. The newest step always survives, however big.
- **When the thing is gone** (the config was recreated, another config is
  open, a batch is flashing): the step is consumed and the notification
  says it could not be undone, rather than pretending.
- **Confirmations kept** where they were; "This cannot be undone" on Remove
  all bookmarks now says Ctrl+Z brings them back.

## 4. Settings export / import — File menu

One JSON file (`"format": "dlconsole-settings"`), six sections:

| Section | Carries |
|---|---|
| Theme and appearance | theme (and last light/dark pair), colour-blind-safe colours, text size |
| Tab colour tags | every tab's colour and label |
| Pins and big numbers | status-bar pins, big-number tiles, Pinned fields panel |
| Window layouts | `window_layouts.json` |
| Firmware Flasher profiles | `flasher_profiles.json` |
| Loco configurations | `loco_configs.json` |

- **Not carried:** network port, disk-log paths and quotas, window
  positions outside layouts, session keys, history logs.
- **Export:** tick sections (each shown with what it holds, e.g. "2
  profiles: Lab, Shed"); a section with nothing set up is not written.
- **Import:**
  - ticked sections **replace** what the console has, whole — not merged;
  - everything is checked before anything is written (layouts through the
    layout store, flasher profiles through the Flasher's own loader, loco
    configurations for format and a non-empty list); one bad section
    stops the import with nothing changed;
  - a section can only set the INI keys it owns;
  - refused while the Firmware Flasher or Loco Configuration window is
    open, since each holds its own copy and would save it back over the
    import;
  - applied at once: theme and text size, tab tags (tabs and pop-outs),
    status pins, Pinned fields, big numbers in open Loco Consoles;
  - **Ctrl+Z puts back exactly what was there**, including deleting a file
    that did not exist before.

## 5. Colour-blind-safe status colours — View ▸ Theme

- **Off by default;** stored as `ui/colorBlindSafe`, read before the theme
  is applied at startup.
- **On:** ok / warning / error become blue / amber / raspberry (sky /
  yellow / vermillion on dark themes), each moved only as far as the
  theme's contrast floor needs on window, base, alternate row and button.
- **Accent** becomes the text colour, since ok now has accent's blue.
- **Chosen by simulation, not by eye:** under simulated protanopia,
  deuteranopia and tritanopia (Machado 2009), every verdict pair stays ≥ 20
  CIELAB units apart in all seven themes. The theme sets reach **4** (Sepia
  ok vs error, deuteranopia).
- Glyphs (✓ ! ✗) are unchanged and still carry the meaning on their own.

## Code

- New: `undolog.{h,cpp}`, `workspacesnapshot.{h,cpp}` (snapshot, layout
  store, `SessionRecovery`), `settingsbundle.{h,cpp}`,
  `mainwindowsession.cpp` (the main window's side of all five).
- `LogModel::takeAll()` / `restoreOlder()`; `BookmarkStore::replaceAll()`;
  `ConfigStore::insertAt()`; `StatusPins::reload()` and
  `BigNumberPanel::reloadAll()` for import.
- `UiColor::setColorBlindSafe()`; `Settings::colorBlindSafe()`,
  `sessionRunning()`.

## Tests

- **`colorblind` (69):** the simulation itself; with the mode off some
  theme's ok/error are < 10 apart for deuteranopes; with it on, every pair
  ≥ 20 (accent ≥ 12 from each verdict) in normal vision and all three
  deficiencies, every colour ≥ 4.5 : 1 (7 : 1 High Contrast) on four
  surfaces, stylesheet helpers follow the mode, off gives the theme back.
- **`undolog` (25):** order, consumption, a gone target, step and row caps,
  the newest step surviving; the Undo action's text and shortcut;
  `takeAll`/`restoreOlder` order and capacity; bookmarks restored and saved.
- **`workspacesnapshot` (31):** JSON round trip including binary state;
  record format matches `saveWorkspace()`; layout store replace-in-place,
  name cap, re-insert at old index, foreign file refused; recovery after a
  clean run (stale file deleted, nothing adopted), after a crash (tabs,
  active tab, pop-outs, their geometry and dock state adopted), damaged
  file ignored.
- **`settingsbundle` (27):** two temp "machines"; export of every section
  and summaries; import replaces whole; empty big-number list stays empty;
  port untouched; undo restores INI and files; all-or-nothing on a bad
  flasher section; unknown loco format refused; a section cannot set keys
  it does not own.
- **`menuaudit` (+29, 121 ok):** on the real main window — Edit ▸ Undo
  first on Ctrl+Z; Clear this tab then the status-bar offer then Ctrl+Z
  restores every row; save / switch (tab hidden, pop-out opened) / undo /
  delete / undo a layout, Ctrl+Alt+n; a **second real MainWindow** started
  after a simulated crash comes back with the crashed run's tabs, same tab
  in front; a clean close leaves no file and the flag down; tag import and
  its undo; loco-config import refused while that window is open; the
  colour-blind toggle switches and is remembered.
- **Not covered by a test:** Flasher Delete-profile undo (the delete goes
  through a modal dialog the harness does not drive); checked by reading.

## verify.sh

Full run, **14/14 stages green, 0 failed**: validators 11/11, unit suite
**139 suites / 4104 checks** (135 / 3952 before; + `colorblind`, `undolog`,
`workspacesnapshot`, `settingsbundle`), menu audit **121 ok** (92 before),
headless smoke alive. Built with Qt 5.15.13 only; not checked by eye this
session (no display here) — worth a look at View ▸ Layouts, the status-bar
Undo offer and the colour-blind colours in one light and one dark theme.

## Next

- Look over the new UI on Windows / Qt 5.15 (status-bar Undo button
  placement, the two import/export dialogs).
- Optional: let a layout also reopen tool windows (Loco Console, fault
  panel) — deliberately left out, see §1.
