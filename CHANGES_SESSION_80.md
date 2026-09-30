# Session 80 — Flasher route overlap; Field over time: Packet ▸ Field menu, zoom, legend, axis titles

From four photos: the flasher's delivery route drawn on top of itself, and
the Field-over-time plot unreadable (LOCO_MODE, MOVEMENT_DIR,
SOURCE_LOCO_ID and TIN as solid blue barcodes).

## 1. Flasher — the delivery route no longer overlaps itself

**Cause.** The gap between the diagram's rows was `(height − 3 tiles) / 2`.
On a short window (Windows, laptop screen) the left column was squeezed,
the gap went negative, and the VCC tile landed on the Input / Output /
Analog row. The same squeeze cut the pre-flight list off mid-sentence.

- The row gap is now fixed (one text line, at least 14 px) and never
  derived from the height the layout hands over.
- The diagram refuses to be squeezed (`minimumSizeHint` = what it needs),
  and asks for more when the text size goes up.
- The **left column scrolls** instead of squeezing its cards, so the
  pre-flight list is never clipped either.
- Belt and braces: if it is ever given less height, the tiles shrink (and
  drop their caption) rather than overlap.

## 2. Field over time — Packet ▸ Field (the "nested menu")

**Why the plots were barcodes.** A field *name* was plotted from every
packet in the tab that carries one of that name. 35 names are shared:
LOCO_MODE from the loco's own LSRP was interleaved with LOCO_MODE from
every ARP it received, and SOURCE_LOCO_ID mixed the loco's own ID with other
locos'. The trace jumped between two signals every half-second.

- The chooser is now a button with a **menu per packet type** —
  `lsrp (4,213 rows) ▸ LOCO_MODE` — and a series is one packet's field.
- A field that is also in another packet says so in its tooltip ("Also in:
  arprecv, dmi — a different signal there") and in the status line.
- Opening from a right-click (field inspector, pins, compare) picks the
  packet carrying the field in the most rows; the menu switches it.
- Only rows of the chosen packet are decoded, which is also much faster on
  a busy tab, and the row cap now applies to that packet's rows.

## 3. Field over time — clean, zoomable

- **Step traces for states.** Whole-number fields (modes, directions,
  flags) are drawn as steps, each value held until the next sample. A
  sloped line from mode 2 to mode 6 drew modes 3–5 that never happened.
- **No lines across gaps** longer than the series' own rhythm (10× its
  median interval, at least 3 s).
- **Zoom and pan:**

  | Do | To |
  |---|---|
  | Mouse wheel | zoom time about the cursor |
  | Shift + wheel | zoom values about the cursor |
  | Drag a box | zoom to it (flat drag = time only) |
  | Right-drag | pan |
  | Double-click, **Fit**, or `0` | show everything |
  | `+` / `−`, ← / → | zoom, pan |
  | Right-click | Zoom in / out / Fit |
  | Click a point | jump to its message (as before) |

- **Full resolution when zoomed.** A long tab is sampled (every Nth row) to
  plot the whole of it. Zooming in now re-reads the visible window with
  every row decoded (debounced while wheeling); the status line says
  whether the view is complete or still sampled.
- Values re-fit to what is in view unless the value axis was zoomed.
- Time ticks land on round wall-clock times (1-2-5 steps from 1 ms to
  1 day); milliseconds shown when the view is under a second.
- Sample dots appear only when they can be told apart.

## 4. Field over time — legend and axis titles

- **Legend** above the plot: line swatch, `lsrp ▸ LOCO_MODE [km/h]`, how
  many samples (and how many are in view when zoomed), and "step" for
  state fields.
- **Value axis title**: the field name, with its unit when the schema
  prints one (`TRAIN_SPEED (km/h)`).
- **Time axis title**: `Time (local, 2026-09-23)`.
- **Enum names on the value axis**: where the schema prints `2
  (Staff_Responsible)`, the tick reads `2 Staff_Responsible`.
- **Hover readout**: time to the millisecond, value with unit and enum
  name, and a vertical cursor line.

## Tests

- **`fieldplotzoom` (39), real LSRP and ARP frames interleaved:**
  - capture type read off the tag, agreeing with the full parser;
  - untyped LOCO_MODE interleaves both packets (the bug); typed gives only
    that packet's rows, and only those are decoded;
  - enum names and units taken from the schema's display, none invented;
  - a time window takes exactly its rows; the cap samples the whole span;
  - catalogue: both types, row counts, fields, shared-field lookup;
  - time ticks round and aligned, milliseconds under a second;
  - the real window: Field ▸ per-packet submenus, the shared-field
    tooltip, choosing plots that packet only, zoom in/out/fit, a set
    window, wheel, double-click, box drag, the `0` key.
- **`flasherroute` (8):** the real Flasher window at 900, 560 and 420 px
  high — the diagram always gets its full height, the left column is a
  scroll area, and a bigger font raises the height it insists on.
- **`pinboard`:** updated for the new chooser (a button, not a combo box).
- **Checked by eye** (offscreen renders, `DL_SHOTS=<dir>` writes them): the
  flasher at 560 px with the route intact and the column scrolling; the
  plot fitted and zoomed with legend, axis titles and `7 Trip` on the axis.
  Not yet seen on Windows.

## verify.sh

Full run, **0 stages failed**: validators 11/11, unit suite **141
suites / 4151 checks** (139 / 4104 before; + `fieldplotzoom`,
`flasherroute`), menu audit **121 ok**, headless smoke alive. Qt 5.15.13.
