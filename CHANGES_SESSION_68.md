# Session 68 — Tools ▸ Firmware Flasher…

The Kavach flasher handoff pack, built into DLConsole: flash `.appimage`
builds onto a chassis (VCC + Input/Output/Analog IOA cards) over UDP from a
window of its own. All four pages of the design are in — Queue, Flashing,
Summary, History — plus the Profile editor.

The engine (`flash_engine.*`), the worker (`flashworker.*`) and
`board_sim.py` are the handoff's, **unmodified**; the wire protocol is not
touched. `blockmapwidget.*` is the handoff's with its colours mapped onto
the theme (below). Everything else is new.

## Where it lives

- `Tools ▸ Firmware Flasher…` — top level in Tools, after a separator, next
  to Session Key. **Ctrl+Alt+F** (see "Decisions to confirm").
- A `QMainWindow` like the Decode Workbench, but single-instance: reopening
  raises the existing window. Two windows flashing one chassis would
  interleave META/DATA on the wire.
- New folder `flasher/`, pulled into the app, the test binary and the menu
  audit through one `flasher/flasher.pri` so the three builds can't drift.
- Data beside `dlconsole.ini`: `flasher_profiles.json` (profiles, written
  with QSaveFile) and `flash_history.jsonl` (append-only JSON Lines; a torn
  last line after a power cut costs that line only). Engineer/Operator mode
  is `flasher/mode` in `dlconsole.ini`.

## The pages

**Queue.** Target (IP, port, adapter), delivery-route diagram (cards not in
the batch drawn dashed), pre-flight checklist, the queue table and the action
bar. All four card rows are always present; a card is left out by unticking
it. Rows drag to reorder; a file dropped on a row from Explorer becomes its
image; clicking the Image cell browses; ✕ clears. "Load images from folder…"
puts the newest `.appimage` whose name names exactly one card on that card.
Images are hashed on load (engine CRC-32 + SHA-256) and **re-hashed when the
file changes on disk** (QFileSystemWatcher, 400 ms debounce so a build still
writing isn't hashed half-done). The detail strip shows full path, SHA-256
and modified time for the selected row. Default order Input, Output, Analog,
VCC — the IOA images are relayed through the VCC, so it goes last.

Pre-flight, re-evaluated on every change and every 2 s while visible:
target IP valid; an adapter on the board's subnet; per ticked card — image
chosen, readable, accepted by `FlashEngine::validate()`, and its **name
matches its card** (`LKAVACH` = VCC, `Input`/`Output`/`Analog`); SHA-pin
mismatch; live capture arriving from the target IP; VCC queued before a
relayed IOA card; and always last, the updater-mode reminder. The Flash
button reads "Flash N cards" and is off until ready, with the first blocker
shown beside it.

**Flashing.** Batch column (dot + name + file + status, active row tinted;
Stop after this card / Abort now…), card header with delivery route and
elapsed clock, four-step phase bar (Session → Send + early repair → Repair ·
round N → Accepted, each with its time; the step a card failed in turns
red), the handoff's block map with legend, five stat cards (blocks, repair
round of max, resent, send rate vs target, RTT + poll timeout), and the
session log with Copy / Save .log. The app bar turns into the caution strip
("Flashing — keep the chassis powered" + target) and profile, mode and
settings lock for the run.

**Summary.** Verdict banner in the design's words ("All 3 cards updated",
"Batch stopped — 1 of 3 cards updated" + which card failed and which was
skipped), one panel per card from `FlashResult` (rounds, resent, bytes on
wire, throughput, settled rate, phase times; for a failure the explanation —
IMAGE_FAIL gets the handoff's "wrong build or wrong card type" text), Export
report (.txt), Retry failed + remaining, Open history, Back to queue. The
footer says which batch ID history holds it under, and says so in red if the
history write failed.

**History.** Search (file, chassis, operator, CRC, batch), card / result /
period filters, Export CSV (what the filters show), Refresh. Selecting a row
shows its full session log, SHA-256, phase timing and transfer figures.
Read-only by design. Every card of every batch is recorded, including the
ones that were **Not run** — that is part of what happened to the chassis.

**Profile** (⚙ in the app bar; "New profile…" in the profile menu). Name,
VCC IP, port, a default image per card, pin-by-SHA-256, and the tuning grid
mapped one-to-one onto `TransferTuning` (adaptive/fixed, start/max/min rate,
check-in interval, poll timeout min/max, give-up, META copies, max rounds,
IMAGE_FAIL restarts; block size shown locked). "Start from this bench's last
settled rate": the rate the pacer settled at on the last **accepted** card is
saved to the profile and used, clamped to [min, max], as the next start.
In Operator mode the tuning panel is visible but locked.

## Engineer / Operator

| | Engineer | Operator |
|---|---|---|
| Image name doesn't match its card | warning; Flash asks "flash anyway?" listing the mismatches | **blocks** |
| No adapter on the board's subnet | warning | **blocks** |
| Transfer tuning | editable | shown, locked |

## Changes outside `flasher/`

- `mainwindow.{h,cpp}` — the menu action, `QPointer<FlasherWindow>`, and the
  live-capture probe handed to the window.
- `udpcommunication.{h,cpp}` — `hasRecentTrafficFrom(ipv4, withinMs)`. The
  receiver now keeps each sender's last-seen time for datagrams addressed to
  this console, merged once per `drainSocket()` wake under a mutex (not per
  datagram). Used for the pre-flight warning "the board looks like it's
  running its application, not the updater" (5 s window). Nothing else
  reads it.
- `tests/test_contrastaudit.cpp` — now also scans `flasher/`.
- `tests/menuaudit_main.cpp` — Firmware Flasher is top level in Tools, on
  Ctrl+Alt+F, not in Transmit; Active Fault Panel keeps Ctrl+Shift+F;
  triggering it twice opens one window.
- `DLConsole.pro`, `tests/tests.pro`, `tests/menuaudit.pro` — include
  `flasher/flasher.pri`.

## Decisions to confirm

1. **Shortcut Ctrl+Alt+F, not Ctrl+Shift+F.** Ctrl+Shift+F is Monitor ▸
   Active Fault Panel; on an ambiguous shortcut Qt fires neither action.
   One line in `mainwindow.cpp` if you want something else.
2. **Top level in Tools**, as asked, rather than in Transmit.
3. **Colours.** DLConsole's contrast audit fails on colour literals outside
   uicolors/theme/uistyle, so the handoff's hex tokens are mapped to
   meanings in `flasherstyle.*`: primary/held → accent, accepted → ok,
   active/repair/attention → warning, danger → error, the flashing app bar →
   the caution-banner colours Packet Maker uses. The block map uses the same
   calls (the only change to a handoff Qt file). Follows the theme toggle;
   badges are checked ≥ 4.5:1 in both themes.
4. **"Skip cards that already hold this image" checkbox dropped.** The
   engine always does this at the handshake (the board answers COMPLETE and
   nothing is sent) and can't be told not to without a protocol change, so
   an untickable-in-effect checkbox was replaced by a note. Such cards show
   as **Already held** (recognised from the engine's log line), count as a
   success, and are recorded in history.
5. **"Send from adapter" is advisory.** The engine's socket is unbound, so
   the OS routing table picks the interface; the picker (pre-selected to the
   adapter on the board's subnet) feeds the subnet pre-flight check and the
   tooltip says so. Actually binding needs a small **host-side** engine
   change (bind before sendto — no wire change). Not done; say if you want it.
6. **VCC-before-IOA pre-flight warning** (non-blocking) — a small addition
   not in the design: reflashing the VCC may reboot it mid-batch, and it is
   the relay for the other three. Tied to the firmware open question below.
7. **Retry failed + remaining** goes back to the queue with those cards
   ticked (pre-flight runs again) instead of starting immediately.

## Engine behaviours worked around (engine unmodified)

- `FlashResult::image_crc` is never set by the engine; the PC-computed CRC
  of the loaded image is used everywhere (table, summary, history, report).
- `FlashEngine::run()` clears its cancel flag when it starts, so an Abort in
  the gap between queueing the card and `run()` starting would be lost. The
  window repeats `requestCancel()` on every progress report while an abort
  is pending.
- The engine enters `Phase::Done` on failure as well as success; the phase
  bar keeps the last working phase so it can mark where a card failed.

## Firmware questions from the handoff — still open

Whether flashing the VCC reboots it mid-batch (and whether the relay then
needs time), how a board is put into updater mode from the field, and the
behaviour when two PCs flash one chassis. The UI doesn't depend on the
answers; decision 6 is the only place that anticipates one.

## Tests

- New suite **`flasher`** (107 checks): card names and the name check
  (every mix-up case, case-insensitivity, ambiguous names), image loading
  (engine CRC, SHA-256 agrees with Qt's, block counting, missing/empty
  files), profile JSON round trip and hand-edit tolerance, ProfileStore
  (first run, save/load, rename keeps active, can't delete the last, broken
  file reported), `effectiveTuning` clamping, every BatchPlan rule (stop on
  first failure, carry on, stop after current, abort, abort that loses the
  race to completion, already held), verdict wording, pre-flight in both
  modes, subnet helpers, history (append, torn line, filters, CSV quoting,
  the dialog reading the file), badge contrast in both themes, the queue
  model, and the Queue page headless.
- New suite **`flasherrun`** (25 checks): the real `FlasherWindow`, worker
  thread and engine against an in-process fake updater on 127.0.0.1
  (C++, so the gate needs no Python for it). Three batches: all three cards
  accepted in queue order with history and the settled rate written; the
  VCC re-flashed and reported **Already held**; the Output card rejected with
  IMAGE_FAIL, the batch stopping, the VCC never sent anything and recorded as
  Not run, and the Summary headline matching the design word for word.
- `contrastaudit` extended to `flasher/`; `menuaudit` extended as above.
- App builds with zero warnings (Qt 5.15). No ternaries in the new code.

## verify.sh

Full run, **14/14 stages green, 0 failed**:

- golden validators: 11/11 ok
- unit suite: 123 suites, 3310 checks, 0 failed (was 121 / 3177; +`flasher`,
  +`flasherrun`, +flasher checks in `contrastaudit`)
- menu audit: 65 ok, 0 failed (includes the new Firmware Flasher checks)
- headless smoke: 500 datagrams, app alive at timeout
