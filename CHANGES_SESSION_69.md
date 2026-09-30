# Session 69 — Firmware Flasher: one card per power cycle

Session 68 built the flasher as a multi-card batch. Reading the updater
source showed that can't work on real hardware, so the flasher now flashes
**one card per run**. Press Flash, then power-cycle the chassis while the
flasher waits for the updater. The engine and the wire protocol are still
unmodified.

## What the updater does (from `updater.zip`)

- **The updater only runs at power-on.** `main.c` gives it 5 s
  (`FW_WATCHDOG_DEFAULT_MS`) to hear a firmware packet, otherwise it calls
  `Boot_Switch()`.
- **Every card ends with `Boot_Switch()`,** after a VCC flash and after
  forwarding an IOA image over CAN. `Boot_Switch()` loads the application
  and jumps into it; it does not reset back into the updater. So after one
  card the VCC is running its application. A second card in the same batch
  would never be answered.
- **The board accepts at most 800 KB** (`MAX_IMAGE_SIZE`). A larger META is
  dropped without any reply.
- **COMPLETE means "the RAM copy passed CRC-32 + SHA-256".** It is sent
  before the VCC writes flash, and for an IOA card before the VCC forwards
  the image over CAN. Nothing further comes back to the PC.
- **Every META resets the session**, and replies go to whoever sent last.
  That answers the open "two PCs, one chassis" question: they break each
  other's transfers (ending in IMAGE_FAIL), but the whole-image CRC + SHA
  check stops a mixed image from ever reaching flash.

## Changes

**One card per run.**
- The queue still shows all four cards, each with its image, but exactly
  one is selected, radio-button style. Browsing for or dropping an image on
  a row selects that row. Loading a folder or a profile fills images
  without changing the selection.
- The button reads "Flash Input card" and so on.
- Removed as meaningless with one card:
  - dragging rows to reorder them;
  - "Stop batch on first failure";
  - "Stop after this card";
  - the VCC-before-IOA warning.
- The Summary's "Retry failed + remaining" is now **Try again**: back to
  the queue with the same card selected, and pre-flight runs again.
- Internally the run is still a one-entry `BatchPlan`, so the Summary and
  history code are unchanged and already tested. Batches could come back if
  the application ever gains a reset-into-updater command.

**Press Flash, then power-cycle.**
- The Flashing page shows a caution-coloured prompt with a countdown:
  "Power-cycle the chassis now — waiting for the updater · N s left".
- The first step of the phase bar reads "Waiting for updater" during this
  time.
- The wait comes from a new profile field, **Wait for updater after pressing
  Flash (s)**. Default 60, range 10–600, editable in both modes because it
  depends on how far the operator is from the power switch. It is stored as
  `updater_wait_s` in `flasher_profiles.json`.
- No engine change is needed. The wait is turned into the engine's existing
  `meta_handshake_tries`, as ceil(wait / `poll_give_up_ms`); 60 s is 24
  tries at 2.5 s.
- Abort while waiting asks "Stop waiting for the updater?"; nothing has
  been sent at that point. Abort during a transfer says the updater drops
  the partial image and the card keeps its current firmware.

**Pre-flight.**
- New blocking item: an image over 800 KB is refused, with its size and the
  limit.
- The last item is now the procedure itself ("After pressing Flash,
  power-cycle the chassis — the flasher waits up to N s") instead of "put
  the board in updater mode".
- When the board never answers, the failure text now explains the
  power-cycle procedure before pointing at cable, IP and port.

**Honest outcome wording.**
- COMPLETE is now shown as **Verified**, not "Accepted": in the table,
  Summary, history and CSV.
- The Summary explains what that proves for each card:
  - **VCC:** the image is verified and is being written to the spare flash
    bank, then booted.
  - **IOA cards:** the VCC verified the image and is forwarding it over
    CAN; the PC gets no confirmation from the card itself.
  - Both say to keep the chassis powered until the card is back up.

**Backed out: the live-capture pre-flight warning.**
- With press-then-power-cycle, the board is *expected* to be running its
  application when Flash is pressed, so the warning would fire on every
  run.
- `udpcommunication.{h,cpp}` are back to their Session 67 state. The
  `MainWindow` hook is gone; `mainwindow` now only adds the menu entry and
  the single-instance window.

## Tests

- `flasher` has 116 checks:
  - one selected card at a time, and loading an image doesn't select it;
  - the 800 KB limit, with exactly 800 KB still allowed;
  - two selected cards refused;
  - the updater wait's JSON round trip and clamping;
  - wait → handshake tries (60 s → 24, 11 s → 5);
  - one-card verdict wording for verified and failed;
  - "Verified" wording and the VCC vs IOA explanations.
- `flasherrun` (30 checks) now runs against a **fake board that behaves
  like the real updater**. It is silent until powered on, listens for 5 s,
  resets on every META, sends COMPLETE three times and then leaves for its
  application. Four runs:
  1. Flash pressed, board powered on 0.7 s later: the power-cycle prompt
     was showing while waiting and is gone after, the card is Verified, and
     the board is left in its application.
  2. The next card without a power cycle: never answered, Failed, the
     explanation says to power-cycle, and nothing reached the board.
  3. A card the board rejects: IMAGE_FAIL, and the board stays in the
     updater.
  4. Abort while waiting: Cancelled, with headline "Input card — aborted".
  All four runs are in history.

## Firmware issues seen in the updater (not flasher changes)

1. **`Transmit_AppImage_On_CAN()` retries a failed CAN send forever.** If
   the target IOA card is absent or the bus is faulted, the VCC never
   leaves the updater and never reaches its watchdog check or
   `Boot_Switch()`. It stays down until power-cycled.
2. **The CAN forward has no retransmission.** A lost frame leaves the IOA
   card incomplete; it boots its old image after the 60 s pending window,
   and nothing reports an error.
3. **A new image rolls back after 3 unconfirmed boots**
   (`BOOT_ATTEMPT_THRESHOLD`). "Verified" is therefore not the same as
   "running the new image"; the only end-to-end proof is the card coming
   back up on the new version.

## verify.sh

Full run, **14/14 stages green, 0 failed**:

- golden validators: 11/11 ok
- unit suite: 123 suites, 3324 checks, 0 failed
- menu audit: 65 ok, 0 failed
- headless smoke: 500 datagrams, app alive at timeout

Built with Qt 5.15 only; the Qt 6.4 build was not run here.
