# DL Console — session changes (patch 30)

Round-trip validator — the third of the four tools. Can this program rebuild
what the equipment actually sent?

| File | Change |
|---|---|
| `roundtrip.{h,cpp}` | the check (pure), per-type tallies, the report, the worker |
| `roundtripwindow.{h,cpp}` | corpus picker, results table, field attribution, Frame Diff handoff |
| `schema/schemaencoder.cpp` | refuse packets the writer cannot emit instead of mis-emitting them |
| `mainwindow.{h,cpp}` | Tools → Round-trip Validator…, live-log snapshot, diff handoff |
| `tests/test_roundtrip.cpp` | two new suites (81 checks) |

## The question

Everything that transmits — Packet Maker, Field Sweep, and the station
responder to come — builds its frame with `Schema::Encoder::encodeBody()`. The
schema claims `encodeBody()` is the inverse of `parseBody()`, so for any
captured frame

    parseBody(bytes) -> encodeBody(...) == bytes

must hold. Where it does, a frame built here is the same shape as one the
equipment builds. Where it does not, anything built for that type is a guess —
and the failure is **silent**: a mis-encoded frame decodes back to the values
that produced it and carries a valid CRC over its own wrong bytes. Nothing in
the existing tooling would have told you.

## What it found

Over the 130,951 frames in `replay/`:

- **slrp, aap, arp, lsrp, nmsrssi — every frame reproduced byte for byte.**
  Those are the types that matter for transmitting, and they are clean.
- **dmi (10,658 frames) and linfo (10,636) reproduced none.** Both are
  `lsb-first`; the writer is msb-first only and packed them in the wrong bit
  order without complaining.
- **nmshlth** encoded to nothing and reported *no error at all* — its body is a
  single `<eventstream>`, an element the field walk did not recognise and
  silently skipped, leaving a packet with zero fields that "parsed" every
  buffer successfully.
- **rfid, ccsys, dlsys** carry inline `<crc>` elements, which consume bits the
  writer does not know how to produce.

## The refusals (your call, and it removes capability)

`Encoder::packet()` now reports as unsupported:

- any packet whose `wire` is not `msb-first`;
- any element inside a packet the field walk does not handle — `repeat`,
  `eventstream`, `crc`, and anything added later.

That second rule is the one that matters most. The old code named `repeat` and
silently ignored everything else, so an unrecognised element did not stop the
walk — it just made it short, and **every later field was written at the wrong
bit offset** while still producing a frame that decodes and CRCs.

Packet Maker and Field Sweep already surface `unsupported`, so both now decline
`dmi`, `linfo`, `rfid`, `ccsys` and `dlsys` with the reason, where before they
would build a frame. `ccsys` and `dlsys` had in fact been round-tripping — but
by accident of being all byte-aligned 8-bit fields, where the two bit orders
coincide. One 12-bit field added to either and the silence would have come
back.

After the refusals: **26,461 frames where the question applies, 26,461
reproduced.**

## The envelope trap

`parseBody()` reads from bit 0 of whatever it is handed. A captured arp/lsrp
frame carries a 10-byte in-capture envelope (`body_offset="80"`), so handing it
over whole reads the message header as `PKT_TYPE` — and does **not** fail. It
returns plausible values, gates LSRP's `FRAME_NUM & 7` health branch on the
wrong number, and rebuilds something close enough that 903 of 3253 frames
"matched". The validator strips `bodyOffsetBits/8`, and there is a test that
pins the misaligned parse succeeding with a different `L_DOUBTOVER`, so this
cannot be quietly reintroduced.

Worth a separate look: Packet Maker's *Fill from buffer* strips
`MessageHeader::SIZE` (8) rather than the 10 bytes `body_offset` describes.

## Reading the results

Per packet type: frames, reproduced, differ, not parsed, not built, CRC ok.

A type nothing can be built for shows **—** in the reproduced column, not 0. A
zero there would read as a failed check when the correct statement is that the
question was never asked; `notEncodable` is counted separately from `differs`
for the same reason, and `answerable()` excludes it.

Selecting a type names **the fields that own the bytes that differ** — the core
records "body byte 20", and the window turns that into `L_DOUBTOVER` using the
shared decoder. A byte no field claims is reported as unclaimed rather than
guessed at, which is itself a finding.

**Diff a failing frame against its rebuild** splices the rebuilt body back into
the captured envelope and sends both to Frame Diff, so "byte 20 differs" is
read out as a field. That is usually the whole diagnosis.

The report saves as plain text, with the schema file it used, the corpus, the
counts, and a closing paragraph saying what it does **not** prove: a field that
is mislabelled but the right width round-trips perfectly. This checks
reproduction, not meaning.

## Threading

The scan runs on a worker that loads its **own** encoder from the path resolved
on the GUI thread, and never touches `kavachSchema()` or `SessionKeyStore` — a
scan whose first half used a different schema than its second half is evidence
of nothing. Field attribution therefore happens in the window, which has the
shared decoder.

The standard `connect(thread, &QThread::finished, thread, &QObject::deleteLater)`
idiom is deliberately **not** used: the report is emitted from inside `run()`,
so the main loop can process the deletion before the thread has terminated, and
deleting a running `QThread` aborts the process. It showed up only in a full
suite run, never when the suite ran alone. `onFinished()` joins the thread and
then deletes it, and the destructor joins too — a window can be destroyed
without a close event.

## Tests

81 checks across two suites. The core is pure, so every outcome is reachable
without a window: real slrp/lsrp/arp frames reproducing, the misaligned-parse
trap, a frame with a `<pad>` bit set (which cannot be rebuilt, because the
writer emits zero there and the parser skips it), each refusal, the diff-byte
cap, sample bounding, and the worker end to end over a temp file including an
unreadable one.

The window gets its own suite, and it earned it: it caught a real bug where
opening the validator before any traffic arrived left the live-log checkbox
unticked **and never re-ticked it** once traffic came in, so Run stayed greyed
out with no visible reason.

Full suite: **60 suites, 1896 checks, 0 failed.**

## Next

The station responder.

---

# Menu reorganisation (same patch)

| File | Change |
|---|---|
| `mainwindow.cpp` | Tools grouped into five submenus; Edit and View regrouped; the duplicate shortcut resolved |
| `commandpalette.cpp` | harvest lazily-built submenus |
| `tests/menuaudit_main.cpp`, `menuaudit.pro` | audit the new structure; fail on any repeated shortcut |

## The duplicate

`Ctrl+Shift+R` was bound to both **Reload schema** and **Stream session (.dlr)
as live**. Qt answers an ambiguous shortcut by firing *neither* action and
warning on stderr, so both were unreachable from the keyboard and nothing in
the UI said so. Reload schema keeps `Ctrl+Shift+R`; streaming moved to
`Ctrl+Shift+S`.

The audit now walks every action in the menu bar and fails on a repeat, so this
cannot come back quietly. Current count: **21 distinct shortcuts, no clash.**

## Tools: seventeen entries → six

| | |
|---|---|
| **Monitor ▸** | Compare tabs · Live Loco Console · Active Fault Panel · Plot field over time |
| **Inspect ▸** | Decode Workbench · Selected row → Decode Workbench · Frame Diff · Packet Sequence |
| **Transmit ▸** | Packet Maker · Selected row → Packet Maker · Field Sweep |
| **Test cases ▸** | Load test cases · Save observation report · Reset run |
| **Schema ▸** | Reload schema · Use external schema file · Revert to built-in · Round-trip Validator |
| | Session Key… |

The grouping is by what the tool does *to the target*, and the one that earns
its keep is **Transmit**. Packet Maker and Field Sweep were sitting in a flat
list directly above "Selected row → Decode Workbench" — a tool that sends
packets to live signalling equipment, one row from a tool that only reads. Now
everything that transmits is behind one labelled step, and nothing that
transmits is left loose in Tools (the audit checks exactly that).

Round-trip Validator went to **Schema** rather than Transmit: it answers a
question about the schema, and the loop it belongs to is edit `kavach.xml` →
reload → validate against real traffic. It is also read-only, and putting a
read-only tool inside a menu labelled Transmit would undercut the label.

## Edit and View

- **Edit**: the four ways of asking "where is it?" now sit together (Find, Go
  to timestamp, Search all sources, Search recorded sessions), then Copy, then
  a **Bookmarks ▸** submenu holding the toggle and the two F2 navigations.
  Shortcuts are unaffected by the nesting.
- **View**: three sections separated — what you are looking at (merged view),
  how it is laid out (Panels, Columns, Row density), how it is drawn (UTC,
  theme).
- **File**: *Stream session as live* moved here from Tools, next to *Open
  recorded session*. Both choose a source of traffic; neither is an operation
  on traffic already loaded.

## Depth costs nothing here

Grouping is only free because `harvestCommands()` recurses into submenus, so
`Ctrl+P` still reaches every action by name in one flat search. That made a
pre-existing gap worth closing: the Panels submenu builds its contents on
`aboutToShow`, which the palette never emitted, so the five dock toggles and
Reset panel layout were invisible to it. The harvest now emits it first.
**53 commands reachable from the palette**, up from 47.

Full suite: 60 suites, 1896 checks, 0 failed. Menu audit: 45 checks, passed.

---

# Fill-from-buffer read the wrong bytes (same patch)

| File | Change |
|---|---|
| `packetmakerdialog.{h,cpp}` | `splitBuffer()` — locate the body from the schema, not from `MessageHeader::SIZE` |
| `fieldsweepdialog.cpp` | seed through the same helper (it was stripping nothing at all) |
| `tests/test_openbuffer.cpp` | new `splitbuffer` suite, 23 checks |

## What was wrong

The Packet Maker stripped `MessageHeader::SIZE` (8 bytes) from a pasted
capture. An arp or lsrp body starts **10** bytes in — the schema says so in
`body_offset="80"`: the 8-byte message header plus two more bytes before the
body. Every field in the form was therefore read two bytes out of step.

It did not fail. `parseBody()` reads from bit 0 of whatever it is handed, so
the misaligned buffer returned plausible values — `PKT_TYPE` 0 instead of 10,
LSRP's `FRAME_NUM & 7` health branch gated on the wrong number — and the form
filled normally.

Field Sweep was worse: `seedFromBuffer()` handed the **whole captured
datagram** to `parseBody()`, stripping nothing. It then transmits from those
values, which is the reason this was worth doing first.

## The fix

One shared `PacketMakerDialog::splitBuffer()`, used by both dialogs. The
schema's `body_offset` is the authority, because it is what the decoder uses —
the two paths now agree by construction rather than by both happening to
subtract 8:

- message header recognised (right `message_id`, `message_length` accounts for
  the buffer) **and** `body_offset > 0` → strip `body_offset/8`, and report the
  envelope as *8-byte header + N bytes before the body*;
- header recognised, `body_offset == 0` → strip the 8;
- no header but `body_offset > 0` → read as a bare body **and say so**, since a
  truncated paste would otherwise be wrong by a constant offset with nothing
  visible to show it;
- shorter than the envelope → hand back nothing rather than a truncated body
  that would parse as a real one.

`slrp` is unaffected: its capture carries no envelope, `body_offset` is 0, and
nothing is stripped.

## Tests

The suite pins the difference rather than just the new behaviour: the old
8-byte strip is run alongside the correct one, and the check is that it *did
not fail* — it just read a different packet type. That is the failure mode
worth remembering.

Full suite: **61 suites, 1919 checks, 0 failed.** Menu audit passed.

---

# UI, part 1: colours that survive the dark theme

| File | Change |
|---|---|
| `uicolors.{h,cpp}` | five semantic colours resolved from the active palette, a contrast metric, a theme-change hook |
| 15 UI files | hex literals replaced with the meaning they stood for |
| `tests/test_uicolors.cpp` | 42 checks — every colour, both themes, arithmetic not opinion |

## What was wrong

Twenty-six hex literals across nine files, nearly all picked against the light
palette. Several were below the 4.5:1 that normal text needs:

| | was | on | ratio |
|---|---|---|---|
| warning | `#b9770e` | light window | **3.20 : 1** |
| error | `#e05a52` | dark window | **3.76 : 1** |
| muted | `#80868e` | light window | **3.19 : 1** |
| ok | `#46c07a` | white (used in *both* themes) | **2.43 : 1** |

The Loco Console and the Decode Workbench were using dark-theme greens and reds
in the light theme, which is where that last one comes from.

Second half of the problem: only `FaultPanelWindow` implemented `changeEvent`.
Toggle the theme with any other window open and it kept its old colours until
closed and reopened.

## What it does now

`UiColor` gives five meanings — ok, warning, error, muted, accent — plus the
caution-banner trio, each a dark/light pair chosen so that **both** clear 4.5:1
against the window *and* the base of their own palette. This is not a new
scheme: `FaultPanelWindow` already did exactly this for itself, and that window
now draws from the shared module rather than its own copy.

`UiColor::onThemeChange(widget, fn)` re-runs a widget's colouring on a palette
change, so windows that colour themselves once — the Round-trip Validator's
results table, the caution banners in Packet Sequence and the DLR player — no
longer go stale after a toggle.

## Deliberately left alone

- The exported HTML in `testassertions.cpp` and `faultpanelwindow.cpp`: those
  are standalone documents with their own white background, read in a browser
  or on paper. They have no palette to follow.
- `ReplayWindow`'s fixed dark canvas: it is a plot, not a surface of the app.
- The severity colours in `logmodel.cpp` and `mainwindow.cpp`: already
  theme-aware, and they belong to the ColorRules rendering system rather than
  to chrome. Folding them in blindly would merge two things that are separate
  on purpose.

## Why the tests are arithmetic

Colour regresses without anyone noticing — somebody nudges a value to taste, it
looks fine on their monitor, and the warning text is unreadable on a laptop in
daylight. So the suite computes WCAG contrast for every semantic colour against
both surfaces in both themes and fails below 4.5:1, after first checking the
metric itself against known references (black on white is 21:1, `#767676` on
white is 4.54:1). The theme-change hook is tested by toggling the palette and
counting repaints.

Full suite: **62 suites, 1961 checks, 0 failed.** Menu audit passed.

Next in the UI pass: feedback consistency, empty states, keyboard flow — still
to be looked at before anything is promised about them.

---

# UI, part 1b: the shape of the window

| File | Change |
|---|---|
| `uistyle.{h,cpp}` | one stylesheet, every value derived from the active palette |
| `theme.h` | palette reworked to feed it |
| `main.cpp`, `mainwindow.cpp` | apply at startup and on every theme toggle |
| `tests/shot.pro`, `tests/screenshot_main.cpp` | throwaway harness that renders the main window to PNG in both themes |

Where `uicolors` says what a colour *means*, this is what the window *looks
like*. Fusion out of the box is functional and cramped: controls tight against
their frames, square-cornered group boxes with a hard 1px border, table headers
that look like buttons, and every dialog picking its own margins.

The rule is the same as in `uicolors`, and it is the important part: **there is
not one hex literal in the sheet.** Every colour is read from the palette in
force when `sheet()` is built, and `apply()` runs again on each theme toggle. A
stylesheet with baked-in colours silently overrides the palette, which is how
Qt applications end up with a dark mode that works everywhere except the four
widgets someone styled by hand — and unlike a wrong palette colour, that
breakage cannot be fixed from the theme.

`monoFont()` exists because `"monospace"` does not resolve as a family name on
Windows and falls back to whatever the system picks, which is why the Packet
Maker's buffer box was in a different face from every other hex view.

The screenshot harness is not part of the suite — it renders the main window in
both themes so the result can actually be looked at, since no assertion tells
you whether a window looks assembled or designed. Its compiled output is
gitignored.

---

# UI, part 2: Escape, and the dialogs that transmit

| File | Change |
|---|---|
| `sendguard.h` | the rule, as two small functions |
| `packetmakerdialog`, `fieldsweepdialog`, `packetsequencedialog` | `reject()` and `closeEvent()` overrides |
| `tests/test_sendguard.cpp` | 9 checks |

## What was wrong

A `QDialog` closes on Escape by default. Packet Maker, Field Sweep and Packet
Sequence are `QDialog`s, opened with `WA_DeleteOnClose`. So during a run,
Escape called `reject()`, the window was destroyed, the `UdpSender` died with
it — and the transmission stopped with no confirmation and nothing left on
screen.

No crash: `UdpSender` is timer-based, so destruction stops it cleanly. The cost
is the other half. A sweep is merely annoying to lose. A **packet sequence**
stopped halfway means the equipment saw a partial run, and the window that says
which step it reached is exactly what the operator needs next — and it is gone.

## The rule

While a run is in flight, **Escape stops the run and leaves the window open.**
One keypress does one thing, and the result stays readable. A second Escape
closes, because by then nothing is running. The status line says so.

The close box asks first, and the question names the consequence rather than
just asking whether you are sure:

> *The sequence is still running and is transmitting. Close anyway? The run
> stops where it is.*

Two free functions rather than a shared base class: these three dialogs have
nothing else in common, and giving them a common parent to share four lines
would couple them for no other reason.

## Coverage, honestly

The suite tests `interceptReject()` directly for both the running and idle
cases, and drives real Escape key events through the actual dialogs — but only
in the **idle** state, because entering the running state means transmitting,
and a test suite should not put packets on the wire to check a keypress. The
running path is covered at the predicate, not end to end.

## Still open in the UI pass

- **Feedback consistency.** 35 status labels across 13 files, each with its own
  wording and placement. The colours are now shared; the conventions are not.
- **Empty states.** 21 tables and lists, and a blank one looks the same whether
  nothing has arrived yet or the scan found nothing. Only a few say which.

Full suite: **64 suites, 1996 checks, 0 failed.** Menu audit passed.

## Escape, part 2: the rule applies to work, not just to sending

Extended to the two remaining dialogs that have something in flight:

- **Round-trip Validator** — a scan over a large corpus runs for minutes on a
  worker thread. Escape used to abandon it and destroy the window; now it
  cancels the scan and the results stay readable.
- **DLR player** — Escape stops the replay and keeps the window. No
  confirmation on close, because this one feeds the live log rather than the
  wire, but losing the window mid-replay still leaves a half-fed log with
  nothing on screen saying where it stopped.

I checked the other twelve `QDialog` subclasses. `ArchiveSearchWindow` is a
`QWidget`, so Escape does nothing to it. `SearchWindow` and `ExportDialog` hold
no worker or timer. The rest — Settings, Export, Goto, Compare, Command
Palette, Session Key, SubPacket, Detach — have nothing running, and for those
Escape-means-cancel is the behaviour people expect.

The point of extending it is consistency: an operator who learns "Escape stops
the thing" in the Packet Maker should not have to discover an exception in the
validator.

Full suite: **65 suites, 1998 checks, 0 failed.**

---

# UI, part 3: Ayu Dark, empty states, workspace restore

| File | Change |
|---|---|
| `theme.h`, `uicolors.cpp` | the dark theme is Ayu Dark |
| `emptystate.{h,cpp}` | a centred line over any empty item view, said by the view's owner |
| `mainwindow`, `roundtripwindow`, `searchwindow` | empty states wired in |
| `settings.h`, `mainwindow.{h,cpp}` | the tabs that were open last time, restored |
| `uicolors.{h,cpp}` | theme notification reworked from an event filter to a registry |
| `tests/` | `test_emptystate.cpp`, `test_workspace.cpp` (32 checks) |

## Ayu Dark

The published scheme rather than an approximation of it: `#0B0E14` as Base,
`#0F131A` as Window, `#BFBDB6` foreground, `#39BAE6` for links. The semantic
colours are ayu's own accents — string green `#AAD94C` for ok, func orange
`#FFB454` for warning, `#D95757` for error, tag blue for accent. The near-black
background buys headroom: everything clears the contrast floor by 4:1 or more,
where the previous dark set only just made it.

Two deliberate departures, both because this is a log console and not a code
editor:

- Ayu's line highlight `#11151C` is 1.06:1 against the background. That is
  right for marking one line in an editor and useless as an alternating row
  colour in a table of ten thousand — the same mistake the light palette used
  to make at 1.04:1. `AlternateBase` opens up to `#171D27`.
- Ayu's `ui.fg` `#565B66` for disabled text is 2.84:1, below the 3:1 floor the
  palette suite enforces. Disabled is not invisible; an operator still has to
  read a greyed-out field to know what it says. Lifted to `#6C7380`.

## Empty states

A blank table meant three different things — nothing has arrived, the filter
matched nothing, the search found nothing — and the operator had to work it out
from context. During acceptance testing "no rows" versus "no traffic" is not a
detail, it is the observation.

The message is a callable, not a fixed string, because the important case is a
log tab: it asks the **source** model rather than the proxy, so

> *Waiting for traffic from Loco 1 / Ctrl 1.*

and

> *No rows match the filter. 40,318 message(s) are hidden by it.*

are different sentences. Telling an operator "waiting for traffic" while forty
thousand rows sit behind a filter is worse than the blank it replaced. Also
wired into the three docks, the validator's table (three states: no scan yet,
scan read nothing, scan recognised no packet type) and the search results.

## Workspace restore

Tab order, hidden tabs included, and the active tab. Restored **empty**, before
any traffic arrives, so a packet for a restored source lands in the tab that is
already on screen instead of creating a second one — and the empty state is
what makes a restored-but-silent tab read as informative rather than broken.

Filters are deliberately not restored. A filter reinstated at startup hides
live traffic, and "why is nothing arriving" is a far worse first minute than
retyping a filter. `ui/restore_workspace` turns the whole thing off.

## Two bugs the tests caught

**A recursive repaint that presented as a segfault.** The theme callback called
`setStyleSheet()`, which re-polishes the widget, which delivered another palette
change back to it — recursion until the stack ran out, with nothing in the
crash pointing at the cause. Guarded, and then the guard exposed the real
design flaw: Qt delivers `PaletteChange` only to polished widgets whose own
effective palette changed, so the empty-state overlay — a child of a viewport —
never received one at all. The event filter is now a registry that
`ThemeUtil::apply()` drives directly, which is both correct for nested widgets
and cheaper than an application-wide filter in a window handling thousands of
rows a second.

**A greedy hex escape.** `"key\x1fname\x1f1"` in a C++ literal ends in the
single character U+01F1, not a separator followed by `'1'` — hex escapes
consume as many hex digits as they can. It compiled, ran, and wrote workspace
records that nothing could read back. Both the code and the first version of
the test had it; the records are now built with `join()` and a named `QChar`.

Full suite: **67 suites, 2030 checks, 0 failed.** Menu audit passed. Two
consecutive headless runs, to exercise save-then-restore.

## Still open in the UI pass

- **Feedback consistency** — 35 status labels across 13 files, shared colours
  but no shared conventions.
- **Jump between decode failures** — the minimap shows where they are; there is
  still no key that steps through them.

---

# UI, part 4: Ayu Light, and three things a screenshot found

| File | Change |
|---|---|
| `theme.h` | the light theme is Ayu Light; `Mid` lifted so indicators are visible in the dark |
| `uicolors.cpp` | light semantic colours are ayu's hues, darkened until legible |
| `uistyle.cpp` | check and radio indicators styled from the palette |
| `logmodel.cpp` | severity row colours from `UiColor` instead of `"red"` and `"darkorange"` |
| `mainwindow.cpp` | the startup panel's secondary paragraph is readable |

## Ayu Light

The sibling of the dark theme rather than a generic grey: `#FCFCFC` Base,
`#F3F4F5` Window, ayu's `#E7E8E9` rule line as the alternating row.

Ayu Light's published values do not survive as UI text — the string green is
2.4:1 against its own background and the accent orange is 1.9:1, because they
are syntax colours read in short runs, not labels. Keeping the **hue** and
taking the lightness down is what makes the two themes read as one scheme:
`#4F6B00` ok, `#9C5A00` warning, `#C03A3A` error, `#1A6FA8` accent. Text is
`#4A4F54` rather than ayu's `#5C6166` — same neutral, a step darker, because
this window is hex and timestamps scanned for hours.

## What the screenshots showed

I rendered the main window in both themes and looked at it, which found three
things no assertion had:

- **The log's own severity colours were the worst offenders in the program.**
  `logmodel.cpp` returned the named colours `"red"` and `"darkorange"` for the
  light theme: 4.0:1 and **2.1:1** against a white row. That is the surface
  read the most, and the entire point of the colour is to make a bad row catch
  the eye. Now from `UiColor`, which also means they follow the theme instead
  of being two hardcoded branches.
- **An unticked checkbox was invisible in the dark theme.** Fusion outlines
  indicators with `Mid`, which at ayu's `#2A303A` is 1.4:1 against the window —
  "Follow newest" looked like a label with a stray gap in front of it. `Mid`
  is lifted, and indicators are now styled explicitly from the palette so this
  does not depend on what the base style happens to do.
- **The startup panel's third paragraph was grey on grey.** It used
  `color:palette(mid)` — a *frame* colour, `#DCDEE0` on the light panel. It is
  the disabled-text role now: dim, still readable.

Worth saying plainly: the contrast suite passed through all three of these,
because it checks the colours the module defines and these were literals
elsewhere. Rendering the window and looking at it is not a substitute for the
tests, and the tests are not a substitute for it.

Full suite: **67 suites, 2030 checks, 0 failed.** Menu audit passed.

---

# UI, part 5: stepping between problems

| File | Change |
|---|---|
| `markerscrollbar.{h,cpp}` | `nextMarkedRow()` — the next error or warning, as a proxy row |
| `mainwindow.{h,cpp}` | Edit → Next problem (F4) / Previous problem (Shift+F4) |
| `tests/test_problemnav.cpp` | 19 checks |

The minimap has always drawn where the errors and warnings are. There was no
way to move between them, so finding the three bad frames in a two-hour capture
meant dragging the scrollbar and squinting at the marks.

`nextMarkedRow()` asks the same question `binRowMarks()` answers for drawing,
one row at a time — same proxy-to-source mapping, same severity test — so what
the operator lands on is always a row the minimap has a mark for. The two
cannot drift apart, because they are the same code path.

Three decisions worth stating:

- **It returns proxy rows, not source rows.** That is what the view selects
  with, and returning a source row would scroll to a plausible-looking wrong
  line whenever a filter was active. There is a test for exactly that.
- **It does not wrap.** Wrapping past the end without saying so puts the
  operator somewhere they did not ask to be, and in a log this long they may
  not notice they are back at the top. Instead the status bar says *"No further
  errors or warnings below this row."* — silence would be indistinguishable
  from a key that did not register.
- **F4, not F3.** F3 and Shift+F3 belong to the find bar. The audit now counts
  23 distinct shortcuts, no clash.

With no row selected, forward starts above the top and backward starts below
the bottom, so the first press always finds the nearest problem in the
direction asked for rather than doing nothing.

Full suite: **68 suites, 2049 checks, 0 failed.** Menu audit passed.

## Still open in the UI pass

**Feedback consistency** — 35 status labels across 13 files. Shared colours
now, but each still has its own wording, placement and lifetime.

---

# UI, part 6: the field-over-time plot

| File | Change |
|---|---|
| `fieldplot.{h,cpp}` | axis ticks on real values, readable axis text, a hover readout |
| `tests/test_fieldplot.cpp` | new `fieldplotticks` suite, 31 checks |

From a photograph of the tool in use, plotting `LOCO_MODE`.

## The axis was labelled in values the field cannot hold

`LOCO_MODE` takes 1, 2, 4 and 6. The axis divided the range into quarters and
labelled them **2.25, 3.5, 4.75** — numbers that field can never be, on a plot
whose only job is to show what it was.

`niceTicks()` puts ticks on a 1-2-5 step, and when every sample in the series is
a whole number it forces the step to a whole number too. Where the range fits in
a handful of steps of 1 — an enum, which is most of the interesting fields here
— it labels every value: `1 2 3 4 5 6`, not `2 4 6` with the value the trace
actually sits on left unlabelled.

Continuous fields keep their fractions: forcing integers on a 0..1 field would
collapse the axis to two labels. The step is snapped to itself rather than
accumulated, or a 0.1 step renders `0.30000000000000004`.

## The axis text could not be read

Both the value labels and the timestamps were drawn in `palette().mid()` — the
**frame** role, `#DCDEE0` on the light theme, about 1.2:1 against the plot
background. That is the same mistake as the startup panel's `palette(mid)`
paragraph, in the one place where the numbers are the entire point.

Everything structural now comes from `UiColor::muted()` at three alphas: full
for text, 120 for the frame, 60 for gridlines. Readable in both themes without
the grid competing with the trace.

The time axis also gains three interior labels and gridlines, so a step in the
trace can be read against a time instead of counted in pixels. End labels are
pulled inside the plot rather than centred on their tick, so neither runs off
the widget.

## You could not see the values

Hovering circled a point and told you nothing about it. There is a readout now —
time and value, in a box that keeps itself inside the plot when the point is
near an edge.

Full suite: **69 suites, 2080 checks, 0 failed.**

---

# UI, part 7: app-wide contrast, and a guard against losing it again

| File | Change |
|---|---|
| `tests/test_contrastaudit.cpp` | 77 checks: the whole palette in both themes, and a scan of the source |
| `uicolors.{h,cpp}` | `frame()` and `grid()`; semantic colours re-fitted to their worst surface |
| `theme.h` | disabled text fitted against the alternating row |
| `timelineribbon.cpp`, `mainwindow.cpp` | the last colour literals and frame-role-as-text |

## Two halves, because the failures came in two kinds

**The palette.** Every pair of roles that can end up as text on a surface, in
both themes: window text, base text, alternating-row text, button text,
selection, tooltip, links, placeholder, disabled — plus the five semantic
colours against all four surfaces they are drawn on.

That last part found five real failures immediately. The colours had been
fitted against the *base*, and half the text in this program sits on an
**alternating row**, which is darker:

| | on the alternating row |
|---|---|
| light warning `#9C5A00` | 4.42 : 1 |
| light error `#C03A3A` | 4.38 : 1 |
| light accent `#1A6FA8` | 4.41 : 1 |
| dark error `#D95757` | 4.39 : 1 |
| light disabled text `#8A9199` | **2.60 : 1** |

All now fitted to their worst case rather than their best.

**The source.** Every contrast bug found in this program came from a literal
somewhere the arithmetic did not look — `"red"` and `"darkorange"` in the log
rows, `palette(mid)` in the startup panel, `palette().mid()` in the field plot
and the timeline. No numeric test finds those, because the numbers are not in
the module.

So the suite reads the source tree and fails on two patterns:

- **a colour literal** outside `uicolors`, `theme.h`, `uistyle.cpp`, and the
  three files that write standalone documents or a fixed plot canvas;
- **a frame role used for drawing** — `palette().mid()`, `midlight()`,
  `dark()`, `shadow()`. Mid is `#DCDEE0` on the light theme, about 1.2:1
  against a white surface. Text goes in `UiColor::muted()`; lines go in the new
  `UiColor::frame()` and `UiColor::grid()`.

A source-scanning test is unusual and worth justifying: the rule it enforces is
one a reviewer would otherwise have to remember on every paint routine, and
three separate reviews here did not.

It found the timeline ribbon still drawing its end labels and its "No messages
yet" text in Mid, and still using three hardcoded mark colours that had been
tuned for the dark theme and used in both — the same marks the minimap and the
log rows draw, now from the same place.

Full suite: **70 suites, 2157 checks, 0 failed.** Menu audit passed.

## Tabs

From the same photograph: eleven source tabs, and the labels washed out.

- **Unselected tab text** was `mix(text, window, 0.30)` — about 3.9:1, under
  the floor. It is `UiColor::muted()` now, which is the colour the audit fits
  for exactly this job, so it cannot drift below the floor without the suite
  failing.
- **Which tab is current** has to be obvious across a row of eleven. A
  one-pixel border in the "subtle" tone was not; the selected tab now carries
  a 2px accent edge along its top and a heavier weight.
- **The close cross** was the worst of it: measured on the dark theme, Qt's
  default came out at RGB(36,36,37) — all but invisible beside a label at 5:1.

That last one is worth recording as a Qt trap. Setting an icon on the close
button does nothing once a stylesheet is in force: `QStyleSheetStyle` draws the
`close-button` subcontrol itself and ignores the icon. I built the painted-icon
route first, measured the render, and found it had changed nothing. The cross
is a resource now, one per theme, matching `UiColor::muted()` — measured back
out of the render at RGB(139,148,156) on dark and RGB(100,107,114) on light,
which is what it should be. The icon-painting API was removed rather than left
beside the one that works.

---

# Tools in the recorded-session window

| File | Change |
|---|---|
| `sessionwindow.{h,cpp}` | a Tools menu and a row context menu |
| `tests/test_sessiontools.cpp` | 17 checks |

The archive is where the careful work happens: a live console is *watched*, a
recording is *read*. Until now, decoding one frame out of a capture meant
re-opening it in the live console — which is the wrong way round, and also the
dangerous way round, because the live models feed `LogWriter`, so anything
loaded into them is re-recorded into today's archive.

**Tools** — Decode Workbench, Selected row → Decode Workbench (Ctrl+Shift+B),
Frame Diff, Plot field over time (Ctrl+Shift+P), and below a separator,
Selected row → Packet Maker. Right-clicking a row offers the same three that
act on it, with "Compare these two frames" enabled only when exactly two rows
are selected.

Flat, not grouped: five entries do not need the submenus Tools needed at
seventeen. The shortcuts match the live window's, so the same fingers work in
both — they are window-scoped, so there is no clash.

Nothing new was written. The same `DecodeWorkbench`, `PacketMakerDialog` and
`FrameDiffWindow`, handed the same `entryBufferText()` the live log hands them,
so a frame read out of a recording decodes exactly as it did when it arrived.

## What is deliberately not offered

- **Field Sweep** and **Packet Sequence**: both transmit repeatedly against
  live equipment. A window whose whole premise is "this is yesterday" is the
  last place to start one by accident.
- **The round-trip validator**: it already reads `.dlr` files directly, so an
  entry point here would be a second route to the same thing.

Packet Maker *is* offered, because reproducing a captured frame is a real
reason to open a recording — but it sits alone under a separator, since it is
the only entry here that leads somewhere that can send. There is a test on that
placement, not just on its presence.

Full suite: **71 suites, 2174 checks, 0 failed.** Menu audit passed.

---

# Packet Maker: FRAME_NUM seeded from the clock

| File | Change |
|---|---|
| `packetmakerdialog.{h,cpp}` | `secondsSinceMidnight()`, seeded into FRAME_NUM, plus a **Now** button |
| `tests/test_packetvary.cpp` | `framenumseed` and `framenumform`, 13 checks |

FRAME_NUM now starts at the seconds elapsed since local midnight — the form the
equipment's own frames carry — and stays fully editable.

Zero was a poor default for a reason beyond looking synthetic: LSRP's health
group rotates on `FRAME_NUM & 7`, so a frame number of zero pins every built
frame to the `& 7 == 0` branch. The one branch you happen to exercise becomes
the only branch you ever send.

**Seeded, not driven.** The value is written once when the form is built and
nothing rewrites it afterwards, so an edit stays edited. Filling from a buffer
or loading a preset still wins, because both write every header editor after
the form is built — there is a test that loads a real captured LSRP frame and
checks the form ends up holding the captured frame number rather than the
clock.

**The Now button** exists because the clock moves while the dialog sits open,
and a frame number several minutes old may be stale by the time it is sent.
Rewriting the field behind the operator would undo a deliberate edit, so
instead there is a button that says what it does — and pressing it clears the
built frame and disables Send, because the form no longer matches what was
verified.

Two things worth your confirmation:

- **Local time, not UTC.** It matches the RTC the equipment stamps its own
  frames with and the clock the log shows by default. If SIF 0533 defines the
  frame number against UTC, this is one line to change.
- **A narrow field wraps.** 86,399 fits comfortably in FRAME_NUM's 17 bits, so
  this never bites today; the width is passed in and the modulo is explicit so
  that a narrower field somewhere would wrap as a stated behaviour rather than
  overflow silently.

Full suite: **73 suites, 2187 checks, 0 failed.**

---

# Find: whole word, and marking every match

| File | Change |
|---|---|
| `findbar.{h,cpp}` | whole-word toggle, Mark-all toggle |
| `logmodel.{h,cpp}` | `setFindHits()` — tint the rows the find bar matched |
| 11 UI files | named CSS colours in stylesheets replaced |
| `tests/test_findextras.cpp`, `test_contrastaudit.cpp` | 12 new checks |

Prompted by a photo of Notepad++'s Find dialog. Comparing it against what the
find bar already had, most of it was present — next/previous, a live count
("3 of 47"), case sensitivity, regex, a column selector, wrap with a
"(wrapped)" hint, and query mode. Two things were not.

**Whole word.** "STN" matched "STN_ID", which on this traffic is most of the
log. Implemented as a regex around the escaped text rather than as a second
matching path, so there is no hand-rolled boundary test to get subtly wrong.
With regex already on the box disables itself, because the pattern is then the
operator's to write and silently wrapping it in `\b` would not be.

**Mark all**, on by default. Every matching row is tinted, not just the one the
cursor sits on. A count tells you how many; the tint tells you *where* — near
the top, spread evenly, or bunched around one minute — which on a hundred
thousand rows is usually the actual question.

The hits are held as **entry pointers, not row numbers**. A tab evicts from the
front once full, so a remembered row number silently becomes a different
message a few seconds later. There is a test that marks a row, appends twenty
more entries to renumber everything, and checks the same *message* is still the
one tinted.

## What the audit missed, and now doesn't

Wiring this up turned up `"color: darkblue; font-style: italic;"` in the find
bar itself — and then eighteen more named colours across eleven files:
`"color: gray"` is 3.5:1 on the light base, `darkblue` is 1.4:1 on the dark
one. The contrast audit had been looking for hex literals and `QColor(...)`
only, so a stylesheet spelling the colour as an English word walked straight
past it. All converted; the audit now flags `color: <name>` too.

That is the third distinct spelling of the same bug. The audit is worth having
precisely because each time it has been a spelling nobody thought to check.

## Not done

- **Extended search mode** (`\n`, `\t`, `\x..`). Worth discussing separately:
  in this program the interesting escape would be a hex *byte* pattern against
  the frame, which is a different search than a text one and probably belongs
  in the query language rather than in the find bar.
- **"In selection"** — the find bar searches what the filter leaves visible,
  which is the closer analogue here.
- **Find All in a list** — that is what Search all sources (Ctrl+Shift+A)
  already does, with a results table.

Full suite: **74 suites, 2199 checks, 0 failed.**

## Detaching it

The find bar now has a **⧉** button that floats it into a small window, and a
**⧈** that puts it back.

Detaching **reparents the same widget** rather than opening a second find UI.
That is the whole decision: a separate dialog with its own copy of the matching
logic would be two things to keep in step — same query, same cursor, same
match list, same rescan-on-new-traffic — and on a live log they would not stay
in step for long.

Details that matter more than they look:

- **It comes back to the same seat.** The layout and index are remembered at
  detach; re-docking inserts it there. A bar that returns to the bottom of the
  tab is worse than one that never detached.
- **`Qt::Tool`**, so it stays above the console it searches without taking a
  taskbar entry — which is what makes it usable while scrolling the log behind
  it, and the reason to want it floating at all.
- **Esc re-docks first, then closes.** Otherwise Esc leaves a window on screen
  with nothing to search.
- **Ctrl+F while floating raises the existing window** instead of opening
  another.
- **If the tab closes while the bar is floating**, the bar is a child of its
  own window and would outlive the view it searches — a dangling pointer the
  next keystroke walks into. It re-docks and goes with the tab.

The test that took the longest to get right was the one counting host windows:
the old host is `deleteLater`'d, because it is destroyed from inside its own
`finished()` signal, and `processEvents()` does not run deferred deletions
posted outside an event loop. Without an explicit flush the check counts a
window already on its way out.

Full suite: **75 suites, 2213 checks, 0 failed.**

---

# UI, part 8: one vocabulary for status messages

| File | Change |
|---|---|
| `statusline.{h,cpp}` | four verbs and one rule about time |
| `packetmakerdialog`, `fieldsweepdialog`, `packetsequencedialog`, `roundtripwindow` | converted |
| `tests/test_statusline.cpp` | 29 checks |
| `tests/test_contrastaudit.cpp` | the hex check now allows whitespace after the colon |

Thirty-five status labels across thirteen files, each with its own wording,
colour and idea of how long a message should stay. The colours were unified in
part 1; the conventions were not, and those are what an operator reads.

Three things were wrong in the same way everywhere:

- **Success and failure looked the same.** Both were plain text, so the reader
  had to parse the sentence to find out which had happened.
- **Nothing expired.** "Sent 40 datagrams" sat there ten minutes later, reading
  as current state.
- **A failure could be wiped by the message before it.** "Sending…" started no
  timer of its own, but where one existed the failure that followed inherited
  the countdown.

## The rule

    say()   neutral progress          transient
    ok()    it worked                 transient
    warn()  it worked, but read this  STICKY
    fail()  it did not work           STICKY

Transient messages clear after six seconds — long enough to read a sentence,
short enough not to be mistaken for current state. Warnings and failures never
clear on a timer; the operator decides when they have read them by doing the
next thing. Setting a sticky message stops any timer left armed by an earlier
transient one, which is the bug above, and there is a test for exactly that
sequence.

Each kind also carries a glyph — ✓, ⚠, ✕. Colour alone is not a signal: for a
colour-blind reader a red sentence and a green one are the same sentence, and a
program that has just spent this much effort on contrast should not then encode
its most important distinction in hue alone.

The round-trip validator's summary is now sticky whenever anything failed to
reproduce. That line **is** the finding, and a finding that clears itself after
six seconds is a finding nobody acted on.

## A fourth spelling

Converting these turned up `"color: #cc3300"` — with a space — in six more
files. The audit's hex check looked for `color:#` exactly, so every one of them
walked past it. That is the fourth distinct spelling of this bug: hex without a
space, hex with a space, `QColor("#...")`, and English colour names. The check
is a regex now.

Still on `setText` with a hand-rolled colour: the search windows, the session
key dialog, the decode workbench and the field inspector. They use a different
shape (`setStatus()` helpers) and are worth converting, but not by mechanical
substitution — each has its own notion of what is sticky.

Full suite: **76 suites, 2242 checks, 0 failed.** Menu audit passed.

## The rest of them, and a fifth verb

The five surfaces left on hand-rolled status — both search windows, the decode
workbench, the field inspector, and the archive scan summary — are converted.
Doing it properly turned up a distinction the four verbs could not express.

Every one of those panels says things like *"Select a row to decode it"*,
*"Paste a hex frame"*, *"18 field(s), 2 with byte ranges"*. None of that is
news. It is the **caption** for what the panel is currently showing, and a
caption must not expire: a panel that explains itself for six seconds and then
goes blank is worse than one that never explained itself.

So `state()` — neutral, sticky, no glyph, because a caption is not a verdict
and ✓ or ⚠ would imply one. The other four remain for events. The rule is now:

    state() what this panel is showing   sticky, no glyph
    say()   progress, in flight          transient
    ok()    it worked                    transient
    warn()  qualified                    sticky
    fail()  it did not work              sticky

Two call sites changed meaning rather than just colour, and both were carrying
a `bool isError` that had been doing the work of three answers:

- **Search results hitting the cap** were styled as an error. They are a
  qualification: the results are real, there are simply more. Now a sticky
  warning, because "184 hits" means something very different when it is really
  "the first 184".
- **An archive scan that was cancelled, capped, or hit unreadable files** was
  merely muted. Same reasoning, same fix.

Full suite: **77 suites, 2251 checks, 0 failed.** Menu audit passed.

---

# The two open builder items

| File | Change |
|---|---|
| `packetbuilder.{h,cpp}` | encode against the schema in force; verify what was built |
| `tests/test_packetbuilder.cpp` | new `buildverify` suite, 7 checks |

## It builds against the schema you are decoding with

`PacketBuilder` hard-loaded `:/schema/kavach.xml`. Load an external kavach.xml
and the log decoded with your file while every builder carried on encoding
against the resource — two definitions live in one session, with nothing on
screen saying so.

It now takes `Settings::schemaPath()` when one is set, falls back to the
built-in copy if that file will not load (a broken edit leaves the Packet Maker
working rather than empty), and says so in the build notes when it does.
`schemaPath()` is exposed because "which schema produced this frame" is the
first question when a frame is not what was expected.

## The self-verify now verifies

`roundTripped` was `!decoded.isEmpty()` — does the built frame decode to
*something*. A frame packed in the wrong bit order decodes to something. So
does one whose values were silently truncated to fit. Both passed, which is
how a wrong frame could leave this program.

It re-parses the built body and compares every requested field against what the
frame carries. Two rules, and the difference between them is the whole check:

- **Signed fields compare masked.** They are written from the caller's raw bit
  pattern and read back negative — `DIST_PKT_START` goes in as 31368 and comes
  out as −1400, the same fifteen bits. Comparing decimals would report every
  signed field as a fault, and the first version of this did exactly that: the
  real-captured-SLRP test failed the moment the check went in.
- **Unsigned fields compare as written, unmasked.** Masking here would defeat
  the point. Asking for `0x7FFFFFFF` in a four-bit field writes 15, and
  `0x7FFFFFFF` masked to four bits is *also* 15 — so a masked comparison calls
  a truncation a match. My second version had that bug too, and the test for
  over-wide values is what found it.

The failure names the fields and prints asked-against-carried, because "the
frame is not what you asked for" without saying which part sends the operator
back through every editor by hand.

Full suite: **78 suites, 2258 checks, 0 failed.**

## Ctrl+F opens the window

Asked for directly: a Find *dialog* on Ctrl+F, the way an editor does it.

Rather than replacing the bar, `activate()` opens whichever shape was used
last — the floating window by default, the in-tab bar for anyone who docks it.
Both are the same widget, so nothing had to be duplicated to get there.

The subtle part is what counts as *choosing*. `close()` re-docks a floating bar
on its way out, so that re-dock must not be recorded as a preference — Esc
means done searching, not "I would rather have the bar". Without that
distinction Find would open as a window the first time and as a bar for ever
after, which is exactly the kind of behaviour nobody reports because it looks
like the program just deciding. A `m_closing` flag draws the line, and the test
walks the whole sequence: default window, Esc, still a window, dock
deliberately, now a bar, detach, back to a window.

The floating window's title also names the tab — **Find — Loco 1 / Ctrl 1**.
There is a find bar per tab, so a window labelled only "Find" can be searching
a tab that is not even on screen.

The preference lives in `ui/find_detached`.

Full suite: **79 suites, 2266 checks, 0 failed.**

---

# Window frames, and room to work in the Packet Maker

| File | Change |
|---|---|
| `windowgeometry.{h,cpp}` | `makeResizableWindow()` |
| 15 tool windows | call it |
| `packetmakerdialog.{h,cpp}` | hide the sub-packet pane where it does not apply; fold the paste box; split the height |
| `tests/test_windowframes.cpp`, `test_packetvary.cpp` | 20 checks |

## No maximise button

Reported from use, and the cause is worth writing down: several of this
program's real work surfaces are `QDialog`s, and **a QDialog does not get
minimise or maximise on most window managers**. They already set `Qt::Window`,
which makes them independent windows — but the buttons come from a separate
hint set, and nothing in the code said so, which is why it looked done.

`WindowGeometry::makeResizableWindow()` adds `Qt::Window`,
`WindowMinMaxButtonsHint`, `WindowCloseButtonHint` and `WindowSystemMenuHint`
without disturbing flags already set, and it is called before the first show
because changing flags on a visible widget hides it. Applied to all fifteen
tool windows: Packet Maker, Field Sweep, Packet Sequence, Round-trip Validator,
Decode Workbench, fault panel, Loco Console, both search windows, Compare,
session viewer, sub-packet editor, DLR player, merged view and the field plot.

The test asserts the bug first — a plain `QDialog` has no maximise hint — so
the check would still mean something if the helper were ever reduced to a
no-op.

## The header form had nowhere to go

Two structural causes, not cosmetics.

**The sub-packet pane took a third of the width for every packet type.** Only
SLRP and AEP have sub-packets. For LSRP, ARP, DMI and everything else it was an
empty list sitting beside a squeezed form — and LINFO has 171 header fields.
The pane is now hidden where the packet has none, and the splitter hands its
width to the form.

**"Fill from buffer" held about 140 px permanently** for a paste box used once
at the start of a session. It folds now, and starts folded. Worth noting: a
checkable `QGroupBox` only *disables* its children, so the contents had to go
into an inner widget that is genuinely hidden — ticking the box without that
would have looked identical and freed nothing.

The middle and the preview also share the remaining height through a splitter
rather than the preview taking a fixed 200 px slice, so a 171-field form can
take the space instead.

Full suite: **81 suites, 2286 checks, 0 failed.** Menu audit passed.

---

# Maximise (properly), and Frame Diff with more than two frames

| File | Change |
|---|---|
| `windowgeometry.cpp` | replace the window TYPE, not OR into it |
| `framediff.{h,cpp}` | `compareMany()`, `summarizeMany()`, `oddOnesOut()` |
| `framediffwindow.{h,cpp}` | 2–6 frames, one column each |
| `mainwindow`, `sessionwindow` | send N selected rows to the diff |
| `tests/` | 23 new checks |

## Why maximise worked in some windows and not others

Reported precisely: it worked in the Decode Workbench and Frame Diff, and did
nothing in the Packet Maker. That difference is the whole diagnosis — those two
are `QMainWindow`s and the Packet Maker is a `QDialog`.

`Qt::Dialog` is `Qt::Window | 0x2`. So `flags | Qt::Window` — which is what the
helper did — leaves a QDialog still **typed** as a dialog, and a dialog is a
transient window that most window managers refuse to maximise however many
hints it carries. The button can be present and do nothing.

The type bits are masked out before the new type goes in. The test now asserts
`(flags & Qt::WindowType_Mask) == Qt::Window`, which is the thing that was
actually wrong; checking only for the maximise hint passed the whole time.

## Comparing three frames

Two frames answer "what changed". Three answer "**which of these is the odd one
out**", and "does this field change every frame or only at the transition" —
questions that otherwise mean comparing in pairs and holding the third in your
head. Two to six frames now, one column each, added and removed from the
window or sent straight from a multi-row selection in the log or a recording.

With three or more, the minority value is coloured and the majority left plain,
so the odd frame is visible without reading every column. `oddOnesOut()`
requires a **majority, not a plurality**: with values 40, 40, 45, 45 there is no
odd one out, and naming one would be inventing an answer.

Alignment is by field **name and occurrence**, not the LCS the pairwise path
uses. LCS does not generalise to N lists, and the usual dodge — aligning
everything against the first frame — makes the answer depend on which frame you
pasted first. A diff should not have that property, and there is a test that
reorders the frames and checks the counts do not move. `compare()` is untouched,
so a two-frame diff keeps the alignment that copes with a `<when>` branch
shifting every row after it.

Byte offsets stay a two-frame idea. With four frames, "bytes 3 and 9 differ"
does not say between which of them, so that line only appears for a pair.

Full suite: **83 suites, 2310 checks, 0 failed.** Menu audit passed.

## Find in a recording

The recorded-session window had every filtering tool except the one for "where
does this word appear". It has the same `FindBar` the live tabs use, per tab,
with the same shape preference — Ctrl+F opens it as a floating window or as a
bar, whichever was used last — and the floating window's title names the source
it is searching.

Find sits under **Edit**, not Tools. It is not a tool you open; it is how you
move around what is already on screen, and Edit is where an operator looks for
it. The test checks the menu entry as well as the shortcut, because a shortcut
that appears in no menu is one only its author knows about.

Full suite: **84 suites, 2317 checks, 0 failed.**

## The rest of the navigation, in a recording

F4 / Shift+F4, bookmarks (Ctrl+B, F2, Shift+F2) and Go to timestamp (Ctrl+G),
all under **Edit**, all on the same keys as the live window.

None of it is a second implementation. Three shared pieces came out of
MainWindow to make that true:

- **`nextBookmarkedRow()`** joins `nextMarkedRow()` in `markerscrollbar.cpp`.
  MainWindow's `stepBookmark()` now calls it too, so the two windows walk rows
  through one function.
- **`GotoTimestamp::spanOf()` and `resolveRow()`** came out of a 90-line slot.
  The tie-breaking is the part worth testing — "first at or after T" keeps the
  earliest row of a tied group, "last at or before" the highest — and it was
  not reachable from a test while it lived inside `onActionGotoTimestamp()`. It
  is now, and the fallback-to-nearest is pinned.
- **Bookmarks share the live store**, keyed by (tabKey, epochMs), so a row
  marked while reviewing a recording is the same row marked live. Stored marks
  are re-applied after loading, since the flag lives on `LogEntry` for cheap
  painting while the truth lives in the store — without that, a mark from a
  previous sitting comes back invisible and F2 walks straight past it.

One thing to watch when reading the diff: extracting the goto logic
accidentally swallowed `snapshotProxy()`, the export helper that happened to
sit between two functions in an anonymous namespace. The compiler caught it
immediately and it is restored, but it is the kind of edit that would have been
a silent loss in a language with looser linking.

Full suite: **85 suites, 2337 checks, 0 failed.** Menu audit passed.

---

# Packet Maker: one frame, up to four destinations

| File | Change |
|---|---|
| `udpsender.{h,cpp}` | `Target` list, `writeAll()`, `resolveAll()` |
| `packetmakerdialog.{h,cpp}` | three more destination rows, folded by default |
| `tests/test_multidest.cpp` | 17 checks |

A station talks to more than one loco. The decision that shapes the rest:
**sending the same frame to four peers is one transmission with four
recipients, not four transmissions.** So a tick builds ONE frame, writes it to
every target, and the send index advances **once**.

That is not a detail. `FRAME_NUM`, the MAC and the message-header `seq_num` all
derive from the send index, so advancing it per destination would hand each
peer a stream with holes in it — 1, 5, 9 — from a sender that believes it is
sending consecutively. There is a test that runs an interval to two targets and
checks the frame index goes 0, 1, 2.

For the same reason the status line counts **frames, not datagrams**, once more
than one destination is in play. "Sent 40" meaning forty frames to four places
and "Sent 40" meaning forty separate frames are different facts.

## Partial failure

Deliberate, and worth disagreeing with if you see it differently:

- A destination that will not resolve is **reported and dropped** at start. If
  none resolve, the run refuses to start rather than running a timer that can
  only emit errors — which would look like it was working.
- A write that fails mid-run is reported and the others carry on. The run stops
  only when **every** destination fails. One unreachable peer must not silence
  the rest; an unreported failure must not look like success.
- The same address twice is one destination. Typing it twice means one peer,
  and honouring it literally would double every datagram without saying so.

## The dialog

The existing Dest/Port pair is destination 1. Three more sit in an **Also send
to** group that starts folded, so a single-peer send looks and behaves exactly
as it did. Each has its own tick box: a host typed and left unticked is a
destination the operator prepared and did not choose, and sending to it anyway
is the worst kind of surprise in a tool that transmits.

The confirmation names every destination rather than counting them — "send to 4
places?" asks the operator to trust a number they cannot check.

Field Sweep and Packet Sequence are untouched: the single-destination overloads
remain and forward to the list form.

Full suite: **86 suites, 2354 checks, 0 failed.**

---

# FRAME_NUM follows the live traffic

| File | Change |
|---|---|
| `framenumberwatch.{h,cpp}` | the frame number ARP and LSRP are currently carrying |
| `mainwindow.cpp` | feed it from the ingest path |
| `packetmakerdialog.{h,cpp}` | seed from it, follow it, stop following when edited |
| `tests/test_framenumwatch.cpp` | 24 checks |

Seconds-since-midnight was the right *shape* and the wrong *number*. It is
whatever this laptop's clock says; the counter a peer checks a frame against is
whatever the equipment says. A frame built minutes away from the live value is
one a peer may treat as stale or replayed, and nothing on screen would explain
why it was ignored.

ARP and LSRP carry `FRAME_NUM` and one or the other arrives constantly, so the
live value is simply there to be read.

## How it reads it

The bit offset is **worked out from the schema** — walk the header summing
widths until `FRAME_NUM`, add the in-capture envelope — and then cached per
packet type. So a schema edit that moves the field moves this too, and an
observation costs a bounded bit read rather than a full decode. That matters
because it runs on the ingest path for every frame; the common case (a packet
that is neither ARP nor LSRP) costs two prefix compares and no parsing.

The test that earns its place checks the watch reads **the same number the
schema's own parser finds** in a real captured frame, rather than checking it
against a constant I typed in.

**Latest means most recent, not largest.** `FRAME_NUM` wraps, and "biggest so
far" would stick at the top of the range for the rest of the session the first
time it did.

## In the Packet Maker

The field is seeded from the live value, falls back to the clock when nothing
has been seen, and **says which it got** — a number taken from traffic and a
number taken from this laptop are different claims and must not look alike.

A **Follow live** box, ticked by default, keeps it tracking as frames arrive.
It stops the moment the operator types in the field, because an edit is a
decision and overwriting it a second later would be the worst behaviour
available here. It also holds off while an interval send is running, rather
than moving the number under a send in flight.

One trap worth recording: the spin-box editor's `valueChanged` fires for the
program's own writes as well as the operator's, so without a guard the first
live update would switch following off and the feature would have worked
exactly once — looking, from the outside, like it worked.

Live updates go into the editor only. The built frame is deliberately left
stale, so Send stays disabled until the operator rebuilds with the new number.

Full suite: **87 suites, 2378 checks, 0 failed.**

## The selected tab was a floating box

From a photo of the console in use: the current tab drew as a bordered
rectangle sitting above the content rather than as the tab that is open.

The cause was the "join the tab to the page" trick I had written two sessions
earlier — a full border on the selected tab with its bottom edge painted in the
page's colour, and the pane pulled up a pixel to sit under it. That only works
when the tab's fill and the pane's fill are the same colour and the two overlap
by exactly one pixel. Here they were not: the tab filled with Base and the pane
did not, so the seam showed and the tab read as a box.

Rebuilding the render locally reproduced it exactly, which is the part worth
noting — I had rendered the tab bar when I wrote that code and called it good,
but I rendered a bare `QTabWidget` rather than one with a page and a pane in
it, so the seam had nothing to show against.

The current tab is now marked the way editors and browsers mark one: a solid
accent underline, a page-coloured fill, heavier text, and **no border**.
Nothing depends on pixel-perfect overlap, so there is no seam to get wrong.

Full suite: **87 suites, 2378 checks, 0 failed.**

---

# Crash on startup after a session with tabs open

Reported as "untick save log automatically, close, and the next start
crashes". The setting was incidental — it was the *close* that mattered.

`restoreWorkspace()` was called immediately after `restoreLayout()`, which
reads as exactly the right place: restore the window, then restore its tabs.
It is thirty lines too early. `buildOrShowTab()` asks `m_dispatcher` for the
tab's model, and the dispatcher is not constructed until further down the
constructor — so the call dereferenced a null pointer and the process died
before the window appeared.

It survived every test I ran because **a console that has never seen traffic
saves an empty workspace and restores nothing**. The crash needs a real session
with tabs open to happen at all, and every headless run I have been doing ends
before a single packet arrives. Changing a setting sent the operator through a
close-and-reopen with tabs on screen, which is the first time the path was ever
taken.

The call moved to after the pipeline is built, and `restoreWorkspace()` now
refuses loudly if it is ever called early again rather than crashing.

## Making the audit catch it

`menuaudit` builds a real `MainWindow`, so seeding a workspace into the ini
before construction reproduces the exact condition. The check is **not** "it
did not crash" — with the null guard in place, the wrong ordering no longer
crashes, it just silently restores nothing, which looks fine until you notice
your tabs are gone. So the audit asserts both saved tabs actually come back.

Verified by putting the bug back: the audit fails with *"both saved tabs were
restored"*, and passes again with the fix.

Full suite: **87 suites, 2378 checks, 0 failed.** Both disk-logging settings
start cleanly.

## The increment section ignored the live frame number

Reported from use, and correct: the seeded rule was **increment from 0**. That
advances a counter of *our* own, which drifts away from the equipment's the
moment it starts — and the frame number a peer checks a frame against is the
one in the ARP and LSRP arriving now, not one this program invented. The
follow-live work applied to the form field and stopped at the vary table.

`PacketVary::Rule` gains a **Live** mode: every send carries the number
currently observed. The seeded rule for FRAME_NUM uses it, so an operator who
touches nothing gets the right behaviour, and `from live` is in the mode list
for any field.

Two decisions inside it:

- **The live value is passed in, not read.** `valueFor(sendIndex, bits,
  liveValue)` — `packetvariation.cpp` stays a pure value calculation with no
  knowledge of the ingest singleton, and the Packet Maker reads the watch once
  per send. A file that computes values from an index should not be reaching
  into live state to do it.
- **Live is the second exception** to "a rule is a pure function of the send
  index", alongside Random, and the header says so. It is a better-founded
  exception than Random: the value wanted is not one this program chooses at
  all.

With nothing observed the rule falls back to its `start`, which is seeded from
the same place the form's field is — so a cold start sends a plausible number
rather than zeros labelled as live.

Full suite: **88 suites, 2391 checks, 0 failed.**

### Repeated frames, and the question I could not answer

A live rule with no step sends the observed number for as long as it stands. If
the loco sends at 1 Hz and this runs at 200 ms, four consecutive frames carry
the same number — fine if a peer only checks freshness, wrong if it requires
each frame to advance. That is a question about SIF 0533 and not one to settle
by guessing, so it is now a **choice** rather than an assumption:

- **step 0** (the default, and the literal reading): send what the equipment is
  sending.
- **step n**: add n per send *since the observed value last moved*.

The second is the part a plain counter cannot do. It advances between
observations and **re-anchors** every time the equipment moves on, so it cannot
drift — which is the whole failure the increment-from-zero rule had. There is a
test that walks it: 4242, then +3 within the same observation, then 5000 the
moment the equipment moves, rather than continuing from 4245.

`LiveFrame` carries the observed value and the count of sends against it. The
counting lives in the Packet Maker, which knows what a send is; the arithmetic
lives in `packetvariation.cpp`, which stays a pure calculation.

Full suite: **88 suites, 2397 checks, 0 failed.**

---

# The frame was stale by the time it went out

From the field: the loco's frame number moves on while the Packet Maker sits
there, and a frame more than about four seconds behind its counter is rejected.

Three causes, all of them the same shape — something between deciding to send
and the datagram leaving.

**Send Once transmitted whatever Build left behind.** It now rebuilds at the
moment of sending. Nothing else in the form can have changed since the last
successful build without disabling Send, so this only ever refreshes what moves
on its own — the frame number, and the MAC and CRC computed over it.

**The confirmation dialogs are gone**, from both Send Once and Start Interval.
That was asked for, and it is also part of the fix: a modal between the click
and the datagram is dead time in which the number goes stale, guarding a
decision the operator had already made by pressing Send. Both were removed
rather than one — two send buttons that ask differently is a worse trap than
either answer. What replaces the interval one is a status line stating what is
going out and where, and a button that reads **Stop**.

**The build now follows the number rather than the widget.** `readHeader()`
reads FRAME_NUM from the watch while Follow live is on, instead of from the
editor. The editor is only a display of it and is deliberately frozen during a
send, so reading the widget sent whatever was on screen when the run started.

## Continuous rebuild

When the observed frame number moves, the dialog rebuilds in place: new frame,
new MAC, new CRC, preview refreshed, Send still armed. Showing a new number
beside a frame built for the old one would be the worst of both.

It only does this once the operator has built at least once — before that, Send
is disabled and rebuilding on its own would be answering a question nobody
asked. If the rebuild fails, Send is disarmed rather than left armed with a
stale frame behind a fresh-looking number. The message is transient, because
this happens about once a second and a sticky one would bury whatever was being
read.

Full suite: **89 suites, 2401 checks, 0 failed.**

## Arm / disarm

The confirmations went because a modal is dead time in which the frame number
goes stale. That left Send as a single unguarded click, so the guard moved
somewhere it costs nothing: **arming is a separate, earlier act, and after it
the send is immediate.**

- Disarmed, both send buttons are dead however good the build is.
- The switch labels itself with its state — *"Arm to enable sending"* against
  *"ARMED — sends go out immediately"*, in the warning colour. A tick box that
  reads the same either way is one you have to look at twice, on a control that
  transmits.
- **Stop stays reachable while a run is in flight**, whatever the switch says.
  Disarming must never be able to strand a transmission.

What disarms, and what does not, is the part worth stating: arming refers to
the frame **in front of the operator**, so anything that replaces that frame
clears it — a different packet type, a buffer, a preset. Rebuilding for a new
frame number does **not**, because that is the same frame with the number it
should have had. Disarming on that would clear the switch about once a second
and guard nothing, and there is a test that pins it.

Full suite: **90 suites, 2412 checks, 0 failed.**

---

# Tools in the compare window

| File | Change |
|---|---|
| `comparewindow.{h,cpp}` | Edit and Tools menus, a find bar per pane |
| `findbar.cpp` | works with a view that binds a LogModel directly |
| `gototimestampdialog.{h,cpp}` | same |
| `tests/test_comparetools.cpp` | 14 checks |

A compare window is where a difference between two sources is actually
studied, and it had no way to search, no way to jump to a time and no way to
open a row — so that work moved back to a single tab, which is the thing the
window exists to avoid.

**Edit**: Find (Ctrl+F), Go to timestamp (Ctrl+G), Next/Previous problem
(F4 / Shift+F4). **Tools**: Selected row → Decode Workbench (Ctrl+Shift+B), and
one entry specific to this window — *Diff the selected rows across panes*,
since the panes already hold two sources and comparing what is selected in each
is the question the window was opened to ask. Every tool acts on the pane that
last had a selection; guessing wrong would send the operator somewhere they
were not looking.

Going to a timestamp is worth more here than anywhere else: with time-lock on,
one jump carries every pane to its nearest message.

## What made it cheap

The panes **bind their LogModel directly** — half a dozen places cast
`view->model()` to `LogModel` and would not survive a filter proxy being
slipped underneath them. Rather than give this window tools of its own, the
shared ones learned to accept either shape:

- `FindBar` resolves rows through a proxy when there is one and straight
  through the model when there is not.
- `GotoTimestamp::spanOf/resolveRow/epochAtProxyRow` now take the model the
  *view* holds, matching what `nextMarkedRow()` already did.

So the compare window, the log tabs and the session viewer run the same find,
the same problem-stepping and the same time resolution. Three copies of "go to
a time" would have agreed on the day they were written and not much longer.

Full suite: **91 suites, 2426 checks, 0 failed.** Menu audit passed.
