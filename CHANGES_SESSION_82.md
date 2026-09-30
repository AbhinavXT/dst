# Session 82 — Live Loco Console widgets, tile rules, watch actions, find across tabs, clock history, replay speed

## 1. Big-number tiles

- **Colour rules** (right-click a tile ▸ Colour thresholds…):
  - **Fixed values:** amber at X, red at Y, "bad when high" or "bad when
    low".
  - **Against another field:** amber within a margin of that field's
    value, red above it. The limit moves with the run.
  - A red tile also gets a thicker border, so it reads without telling the
    colours apart. A stale value is muted and never red.
  - The **default Speed tile** is set against `dmi:speed_limit_permissible`
    with a 5 km/h margin. With no fresh DMI (older than 3 s) it gets no
    colour, rather than a guess.
  - Rules are saved as a third `|` part of the tile string. Older builds
    read the first two.
  - Setting a rule is undoable (Ctrl+Z).
- **Sparkline:** the last 60 s under every numeric tile, in the tile's
  current colour.
- **Time in state:** enum tiles (mode, direction, brake) show "for 4 min
  12 s" since the value last changed. The frame clock is excluded.
- **Plot:** double-click a tile (or right-click ▸ Plot over time) opens
  Field over time for that loco. A relative rule's limit field is added as
  a second series, so speed and permitted speed plot together. Jumps from
  that plot reach the main window.

## 2. Loco Console: cab view and lights

- **Cab view** (button beside Big numbers, remembered). A small copy of the
  driver's DMI, drawn from `@dmi`:
  - **Speed dial:** green arc up to the permitted speed, red above it,
    target-speed marker on the rim; the needle and number turn red above
    permitted.
  - **Distance-to-target bar:** 0–1000 m, compressed above 200 m as the
    DMI's own is, turning amber under 200 m, with the distance and **time
    to target** at the current speed.
  - **Mode and brake state**, the brake coloured by level: overspeed
    warning, service brake, emergency brake.
  - **Signal heads for the signal ahead and the next one**, from the SLRP's
    movement authority when an SLRP has been heard in the last 10 s,
    otherwise from the DMI's `current_sig_aspect`. Aspects are drawn as
    lamps:
    - red, yellow, double yellow, green;
    - junction and stencil routes named;
    - calling-on as red plus white;
    - "Unidentified" with no lamp lit;
    - a value with no assigned aspect is named, not drawn.
  - When the DMI frame is older than 3 s everything is muted and says so.
- **Heartbeat lights** (always shown). One light per packet type the loco
  sends, with its rate and a pulse on arrival. Each type is judged by its
  own rhythm:
  - amber when later than twice its usual gap (at least 2 s);
  - grey and "silent N s" past five times (at least 5 s).
- **Card health lights** (always shown), with ✓ ✗ ? glyphs as well as
  colour:
  - the DMI's three mapped health bits: Kavach unit, pulse generator, BIU;
  - one red light per module with an active fault in the latest NMS fault
    report, naming the faults.

  `radio_health_status` and `gps_status` have no mapping in the schema, so
  they get no light.

## 3. Watch panel

- **Actions** (right-click a watch): beep, bookmark the frame that fired it
  (the note is "watch: <name>"), announce every occurrence. They are shown
  as ♪ ⚑ ↻ after the condition and saved with it. Older builds ignore the
  fourth part.
- **Times column:** how many separate occurrences there have been. Matches
  more than 3 s apart start a new one, so a condition true for 300 frames is
  one occurrence. Re-arming clears it.

## 4. Find bar → every tab

- **All tabs** on each tab's find bar sends the current search to Search
  all sources. Each mode is converted to the query language:
  - Text → a quoted phrase, or an escaped regex if it contains a quote;
  - Extended → unescaped first;
  - Regex → `/…/`;
  - Hex → `hex:"…"`;
  - Query → as typed.
  - A search that can't be expressed (a regex containing `/`) is refused
    with the reason.
- **Group by tab** in the search window: each tab's hits under a heading
  with its count, in time order. The status line says in how many tabs.

## 5. Frame clock history

Click any of the three clock labels in the status bar for a live graph of
the **last 30 minutes**: loco − station, loco − this PC, station − this PC,
in seconds. One sample a second; zooming holds while it updates.

## 6. Replay

- **Speed:** Step (the old one record per 200 ms) or 0.25× to 50× in
  **recorded** time. A quiet minute takes a minute at 1× and 1.2 s at 50×.
- **◀ event / event ▶** buttons and `[` `]` jump between the Event Log's
  events, filtered to any event, warnings and errors, or errors. The status
  bar names the event.

## Code

- New: `cabpanel.{h,cpp}`: `CabDisplay`, `LinkLights`, `HealthLights`, and
  the pure `cabStateFrom`, `lampsFor`, `timeToTarget`, `heartbeatState`,
  `healthLightsFrom`.
- `TileRule` in `livefields`.
- `ClockHistory` and `ClockHistoryWindow` in `clockskewalarm`.
- `UiColor::signalLamp()`: railway lamp colours live in `uicolors`, as the
  contrast audit requires.
- Watch occurrences and actions in `watchlist`.
- `FindBar::toLogQuery()`.
- `SearchWindow::setGroupByTab()`.
- `ReplayWindow` speed control and `nextEventIndex()` / `stepEvent()`.

## Tests

- **`session82` (82).** The cab, heartbeat, health and replay parts run on
  **real frames from `replay/`**:
  - tile rules: fixed, bad-when-low, relative, no limit gives no colour,
    damaged rules, older tile strings, round trip, the default Speed rule;
  - the tile panel: levels, a 31-point sparkline trimmed to a minute,
    "for 30 s", then "for 1 min 5 s", then reset on change, stale never
    red, double-click asks to plot, a rule is saved and undone;
  - the cab from a real DMI (speed, permitted, mode, brake), with a real
    SLRP carrying a movement authority, emergency brake level, the lamps,
    time to target;
  - heartbeat thresholds, including a slow packet judged by its own rhythm;
  - health lights, including "nothing heard means nothing green";
  - the real console fed real LSRP, DMI, SLRP and NMS frames: cab shown
    with SLRP signals, a light per type sent, health from the real DMI;
  - watches: occurrences, every-time announcing, actions saved, an older
    watch string loading, re-arm clearing, the bookmark request from the
    panel;
  - find-bar query conversion for all five modes, and the grouped search
    with a real dispatcher (3 hits, then 5 rows grouped);
  - clock history cadence, 30-minute retention, the graph;
  - replay on a real capture: event stepping forward and back, the error
    filter, 50× playing forward.
- **Menu audit (132 ok).** Clicking the frame clock opens the history. Each
  tab's find bar has All tabs, which opens the search grouped. The watch
  panel has its Times column.
- **Checked by eye** (offscreen): the console with the cab view, lights and
  a real GSM-2 fault on real frames; a red tile with its sparkline; the
  clock graph.

## verify.sh

Full run, **0 stages failed**:
- validators 11/11;
- unit suite **143 suites / 4312 checks** (142 / 4230 before);
- menu audit **132 ok**;
- headless smoke alive.

Qt 5.15.13. Not yet seen on Windows.

## Notes

- The `@authkeys` capture line, listed earlier as pending, was already
  implemented (it is a known capture type).
- `replay/` has real `@dmi` (10,658), `@slrp` (3,340) and `@nmsflt` (156)
  frames, but no `@uba`. The braking-curve location scale (session 81)
  still needs a capture with `@uba` to confirm.

## Fix (82a) — tiles froze on the old LSRP after a loco restart

**Reported:** with the Live Loco Console showing values from LSRP, a
restarted loco's Mode, Frame clock and so on stopped updating. The loco
sends ARP again after a restart, but the tiles never moved to it.

**Cause.** A tile tried its sources in the listed order and took the first
one that had ever been heard. The default tiles list `lsrp` before `arp`, and
the LSRP held from before the restart still "had" the field. The tile kept
showing it, muted as stale, and never reached the live ARP.

**Fix.** `LiveFields::byFreshness()`: a tile tries its sources **most
recently received first**, and the listed order only breaks ties. After a
restart the ARP is the freshest, so the tiles follow it; when LSRP resumes,
they go back to LSRP. Plot-from-tile picks its packet the same way.

Status-bar pins are not affected: a pin names one packet type, and it
already shows its age as it goes stale.

**Test** (`session82`, +8). On real frames from `replay/`:
- running on LSRP, the frame clock says "from lsrp";
- after the "restart" (ARPs only), the frame clock and mode equal the
  ARP's values and say "from arp";
- when LSRP resumes, the tiles go back to it.

Run with the old first-listed order, the test fails on exactly the reported
symptom (3 failures). With the fix: unit suite **143 suites / 4320
checks**, menu audit 132 ok, 0 stages failed.
