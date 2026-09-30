# DL Console — session changes (patch 23)

Two changes on top of `DLConsole-patched-22`. Both are drop-in: rebuild with
`qmake && make`. Generated files (`Makefile`, `*.o`, `moc_*.cpp`, `qrc_*.cpp`,
`ui_*.h`) are stripped from the archive; they come back from the build.

Files changed:

| File | Change |
|---|---|
| `findbar.{h,cpp}` | view hold, model-driven rescan keeps position |
| `mainwindow.{h,cpp}` | scroll lock respects the hold; open-buffer plumbing, menus, Check Buffer buttons |
| `rawbytespanel.{h,cpp}` | hex-dump context menu |
| `logentry.{h,cpp}` | `entryBufferText()` |
| `decodeworkbench.{h,cpp}` | `loadBuffer()` |
| `packetmakerdialog.{h,cpp}` | `loadBuffer()`, `isSending()`, capture-preamble fix in `parseBuffer()` |
| `tests/` | `test_findhold.cpp`, `test_openbuffer.cpp`, wired into `tests.pro`; `menuaudit_main.cpp` checks the new Tools entries |

---

## 1. Find no longer fights scroll lock

**Symptom:** with scroll lock on and traffic arriving, the viewport alternated
between the found row and the end of the log.

**Two independent causes, both fixed:**

1. `MainWindow::onEntriesAppended()` called `view->scrollToBottom()` once per
   arriving batch whenever `scrollLock` was set, with no idea that Find had
   parked the view on a match. Find scrolled to the match, the next batch
   scrolled to the tail, ~30 times a second.

2. `FindBar` re-scans on `rowsInserted` through its 200 ms debounce, and
   `rebuildMatches()` unconditionally reset the cursor to match #0 and jumped
   there. On a live tab that dragged the operator back to the first match every
   200 ms — with *or without* scroll lock. This half was invisible behind the
   first one, and fixing only the first would have left it.

**What changed:**

- `FindBar::holdsView()` — true while the bar is visible and parked on a match.
  `holdChanged(bool)` fires on the transition.
- The debounce now tells apart a **user** rescan (typing, toggling a checkbox →
  scan and jump to the first match) from a **model** rescan (new traffic →
  `rescanKeepingPosition()`: re-scan, keep the cursor on the same entry, never
  move the viewport).
- `rescanKeepingPosition()` anchors on the `LogEntry`, not the row number.
  Rows renumber for two ordinary reasons — the model evicts the oldest entries
  at the per-tab cap, and a filter can drop rows above the cursor — so a
  remembered row number silently becomes a different message.
- `onEntriesAppended()` skips `scrollToBottom()` while the hold is on.
- Releasing the hold (Esc / closing the bar / clearing the text) emits
  `holdChanged(false)` and the tab snaps to the tail in one hop. Toggling scroll
  lock back on also catches up immediately, unless Find is holding.
- The bar shows a `following paused` hint when the tab has scroll lock on, so a
  deliberately frozen tab is explained rather than looking broken.

**Rule:** a request to look at one specific row is the more specific request, so
it wins over following the tail; Esc hands the view back.

## 2. Open a buffer directly in the Decode Workbench / Packet Maker

Previously the only route from a captured frame to either tool was copying hex
out of one window and pasting it into another.

**Four entry points, one code path:**

- **Log row right-click** → *Open in Decode Workbench* / *Open in Packet Maker*.
  Acts on the row under the cursor, not on the selection: each target window
  holds exactly one frame. Greyed out with a reason when the row has no bytes.
- **Raw bytes panel right-click** → the same two, plus *Copy hex* (bytes only,
  without the offset column and ASCII gutter).
- **Tools menu** → *Selected row → Decode Workbench* (`Ctrl+Shift+B`) and
  *Selected row → Packet Maker*. In the menu bar on purpose: the command palette
  (`Ctrl+P`) harvests the menu bar, so a context-menu-only action is invisible
  to it.
- **Check Buffer page** → two buttons beside *Convert*, for hex that arrives from
  tshark or out of somebody's email.

**What gets sent:** `entryBufferText()` (in `logentry.h`). A capture row already
*is* valid input for both tools — `@slrp_1_1 <ts> <seq> <hex…>` — and its `@tag`
names the packet type, which is the one thing neither tool can infer reliably,
so a capture row is passed through whole. Anything else falls back to the raw
datagram as spaced upper-case hex, message header included: trimming it is a
judgement the operator can make and the function cannot.

**Window reuse:** both targets reuse the window last opened this way while it is
still alive — right-clicking row after row should not bury the desktop. The
Tools → *Decode Workbench…* / *Packet Maker…* entries still open fresh,
independent windows, as before. One exception: a Packet Maker that is mid-send
(`isSending()`) is never reused, because rewriting its form under a live
interval send would change what goes on the wire without the operator saying so.

**Bug found on the way:** `PacketMakerDialog::parseBuffer()` mangled a pasted
capture line. The line carries a decimal sequence number between the timestamp
and the bytes, and a seq like `6933` is also perfectly good hex, so the
"one unbroken hex run" branch ate it and prepended `0x69 0x33` to the frame — a
two-byte shift that decodes into plausible-looking nonsense. The preamble is now
recognised the way `CaptureDecoder::parseLine()` recognises it (`@tag`, ISO
timestamp, numeric sequence) rather than by position, so `@slrp 91 99 C5` keeps
all of its bytes.

---

## Tests

`tests/test_findhold.cpp` (18 checks) pins both halves of change 1: that
`holdsView()` is true while parked and false after Esc, that `holdChanged` fires
on the transitions, and that an insert burst leaves the cursor on the same entry
and the viewport where it was. The 200 ms debounce is waited out rather than
poked, so the timer path itself is under test.

`tests/test_openbuffer.cpp` (15 checks) pins `entryBufferText()` (capture line
passed through, raw datagram hexed, null and empty entries yield nothing) and
`parseBuffer()` against the capture-preamble case above.

Full suite: **52 suites, 1634 checks, 0 failed**, plus `menuaudit`, which now
also asserts the two new Tools entries exist.

## Known, untouched

`Ctrl+Shift+R` is bound twice in `mainwindow.cpp` — *Reload schema* and
*Stream session (.dlr) as live…*. Qt resolves an ambiguous shortcut by firing
neither, so both are dead as shortcuts today. Not changed here because picking
which one keeps the binding is your call.
