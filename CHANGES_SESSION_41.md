# Session 41 — pinned fields

| File | Change |
|---|---|
| `pinboard.{h,cpp}` | new: what a pin is, and how it follows traffic |
| `pinpanel.{h,cpp}` | new: the dock |
| `mainwindow.cpp` | the dock, and one call on the ingest path |
| `settings.h` | pins survive a restart |
| `tests/test_pinboard.cpp` | new: 30 checks |

Watching one field meant finding it again in every frame. LOCO_MODE is
somewhere in the middle of an LSRP; to see it change you scroll, decode, read,
and three more frames have arrived. The value was decoded already — the only
reason it was hard to watch is that nothing held it still.

---

# What a pin shows

Four columns, because the useful reading is all four at once:

| Field | Now | Was | Changed |
|---|---|---|---|
| LOCO_MODE · 1_1 | 2 (Staff Responsible) | 1 (Stand By) | 12 s |

**"Was" is half the point.** A value on its own says less than a value with
what it used to be and when it stopped being that. And the age matters even
when nothing changed: a value with no age beside it cannot be told apart from
one that stopped arriving ten minutes ago, which is the difference between
"the loco is in Stand By" and "the loco stopped talking while in Stand By".

Values are the **display** strings, so an enum reads as its name rather than
the digit behind it — the reason to look at the field in the first place.

## Narrowing to a source

A pin can be tied to one tab key. With two locos on air an unnarrowed pin
shows whichever spoke last, which is a reading of nothing. Narrowed, another
source's value for the same field cannot overwrite it, and is not even counted
as a sighting.

Unnarrowed pins record which source supplied the current value, so a reading
can be traced back; double-clicking one switches to that tab.

## Never seen, which is not zero

A pin that no frame has carried says `not seen yet` rather than showing blank
or nothing. It usually means the field is spelled differently or lives in a
packet that source does not send, and the tooltip says so. Blank would have
been indistinguishable from a field whose value is empty.

---

# Sampled, and it says so

Pins are updated from the **last entry of each delivered batch**, not from
every entry.

Decoding every arriving frame for every pin would put schema decoding on the
ingest path, which is the one place in this program that must not get slower —
the batching in `onEntriesAppended` exists for exactly that reason, and this
would have undone it.

What the sampling costs, stated rather than left to be discovered: a field
that changes and changes back within one batch shows neither transition. For
the fields anyone pins — a mode, a location, a status word — that is invisible.
For a field toggling every frame it would be wrong. The comment on
`PinBoard::observe` says this where someone will read it before relying on it.

The panel also does no work at all when it is closed or empty, which is the
default: the dock is opt-in from the Panels menu.

---

# Tests

30 checks on the parts that go wrong quietly rather than loudly:

- the first sighting is **not** a change — there was nothing to change from,
  and "changed 0 s ago" is a claim an operator acts on
- the same value again is not a change either, but the last-seen time still
  moves, which is what tells a live value from a stalled one
- a narrowed pin ignores other sources completely
- a field no frame carries stays `seen == 0` with no value, distinct from zero
- pins round-trip through settings, and blank lines in a hand-edited ini do
  not become half a pin

Full suite: **106 suites, 2727 checks, 0 failed.** Menu audit passed. Headless
smoke run clean.

---

# Not done

- **Pin from the decode panel.** Right-clicking a field row and choosing "pin"
  is how anyone would expect to do this; the field chooser is the long way
  round. It needs a context menu in two other windows, so it is its own change.
- **Pin history.** The plot already knows how to walk a field across a model.
  A pinned field and a plotted field are the same field asked about over two
  different spans, and they do not know about each other yet.

---

# Pinning from where you are already pointing

Both follow-ons from the first cut, done.

## Right-click a decoded field

The field inspector's rows now carry a context menu — **Pin `LOCO_MODE`**,
**Plot `LOCO_MODE` over time**, **Copy value**. The actions are named with the
field in them, because a bare "Pin" beside a table of forty rows leaves the
operator checking which row they were on.

The inspector **raises a request rather than acting on it**. It knows which
field was clicked and nothing else — not which tab it came from, not where the
pin board lives, not whether the plot wants a model. Opening windows is
MainWindow's job everywhere else in this program and there was no reason for
it to stop being here.

**A pin made this way is narrowed to the tab it was clicked in.** An operator
pointing at a value in one source's frame means *that* source's value; an
unnarrowed pin would then start showing whichever loco spoke last, which is
not what they asked for and would look like the pin was broken. The dock is
also shown and raised, because a pin that lands in a hidden panel is a click
that appeared to do nothing.

## A pinned field and a plotted field are the same field

They were two features that did not know about each other — one asking what a
field says now, the other what it said over the session. Right-clicking a pin
offers **Plot over time**, against the source the pin is following rather than
whichever tab happens to be in front.

`FieldPlotWindow::plotField()` opens straight onto a named field. A field the
chooser has never listed is **added and selected**, not refused: the list is
what the first few hundred rows happened to carry, which is not the same as
what the session contains. Silently plotting whatever the chooser was on
instead would have drawn the wrong field with nothing saying so — which is the
failure this codebase keeps finding in other forms.

## Tests

`pinfromrow`, 7 checks: the inspector raises both requests and its table
actually answers a right-click — without that policy the menu never appears
and the whole path is unreachable while every other check still passes. Then
that the plot opens on the field it was given, including one the sample never
saw.

Full suite: **107 suites, 2734 checks, 0 failed.** Menu audit passed. Headless
smoke run clean.

---

# The panel floats

Asked for after seeing it docked: make it a window.

```cpp
m_pinDock->setFloating(true);
m_pinDock->resize(420, 260);
```

A pinned field is watched **while something else is being done** — a run driven
from the DMI, another window in front — so the one place it must not be is
folded into the side of the window whose log is being scrolled. Floating, it
can sit on a second screen, or beside the console rather than inside it.

It is still a `QDockWidget`, so dragging it back into the window works and
`restoreState()` remembers that choice for the next session. Floating is a
default, not a decision made on the operator's behalf.

**Still hidden until asked for.** An empty panel was not worth the width; an
empty *window* is worth even less. It opens from View → Panels → Pinned
fields, or by right-clicking a field and pinning it, which shows and raises it.

**Placed beside the console on first show**, not at whatever corner Qt picks.
On first show rather than at construction, because the main window has no
final geometry during construction — `restoreLayout()` has not run — so
anything computed then would be measured against the wrong rectangle. After
that it is the operator's to move.

## One thing that behaves differently now

Pins only update while the panel is visible: the ingest hook checks
`isVisible()` before decoding anything, so a closed panel costs nothing. That
was true when it was a dock and is still true as a window. A pin left set up
with the window closed will show what arrives after it is reopened, not a gap
filled in retrospectively.

## Where these are checked

In the **menu audit**, not `dltests`: MainWindow is not in the test binary, and
every one of these is a property of how the window sets the dock up. Four
checks — it exists, it floats, it is not shown unasked, and it can still be
docked.

Full suite: **107 suites, 2734 checks, 0 failed.** Menu audit passed, now 5
checks longer. Headless smoke run clean.
