# Session 43 — watches

| File | Change |
|---|---|
| `watchlist.{h,cpp}` | new: conditions that watch live traffic |
| `watchpanel.{h,cpp}` | new: the panel |
| `mainwindow.cpp` | the dock, the ingest hook, the announcement |
| `tests/test_watchlist.cpp` | new: 33 checks |

A pin says what a field **is**. A watch says **tell me when it becomes this**.

Both halves already existed and did not know about each other: the query
language parses these conditions, and the assertion engine reports observed /
not observed over a recording. This is those two joined and pointed at live
traffic — the case that matters during an acceptance run, where the operator
is driving the DMI and cannot also be reading the log.

```
field:LOCO_MODE~Staff_Responsible        fires when the mode changes to it
field:TSR_STATUS=2                       fires when TSR entries go live
sev:error AND src:33_1                   fires on the station's first error
```

---

# Why a watch fires once

`field:LOCO_MODE~Staff_Responsible` is true on the frame the mode changes and
on every frame after it. Reporting each one would bury the transition in its
own aftermath, which is the opposite of what was asked for.

So a watch fires on the **first** frame that matches, keeps that frame as
evidence, and goes quiet. The question was *when did this happen*, and it has
been answered. **Re-arm** forgets what was seen and keeps the condition, which
is what an operator wants between two runs of the same test.

Repeating watches exist for the other question — *how often* — and count
instead.

## It reports observed, not passed

A watch says a condition was **observed**, with the frame and the time. Not a
verdict. Same discipline as the assertion engine and for the same reason:
pass and fail belong to the signatory, and tooling that offers them invites
them to be copied without being read.

## A bad expression stays on the board

A query that will not parse is **added anyway**, with its error beside it. A
typo that silently vanished would look like a watch that never fired, which is
the one thing a watch must never be mistaken for.

---

# Two deliberate differences from the pin board

**Every frame, not a sample.** Pins update from the last entry of a batch,
because a pinned value only has to be current. A watch exists for the one
frame where the condition held, and a sampled watch would miss exactly the
event it was armed for. This is affordable because schema decoding is lazy and
cached per entry — a watch with no `field:` term costs a string comparison,
and one with a field term shares its decode with anything else that asked.

**A closed panel is still watching.** Pins stop updating when their window is
closed, since nothing is reading them. Closing a window is not disarming, and
a watch that quietly stopped watching would be worse than no watch at all.

## Firing is announced, not just displayed

Through the notification centre, and the panel is raised. The panel may well
be behind another window — that is the situation a watch exists for. If it
were only ever read by someone already looking at it, a pin would have done.

---

# Tests

33 checks: that a malformed query is kept with its error rather than dropped;
that a watch fires once and holds its evidence; that a condition which stays
true is still one event; disarming; that re-arming discards the evidence and
keeps the condition; a field condition against a real captured frame; and the
round trip through settings.

Full suite: **110 suites, 2803 checks, 0 failed.** Menu audit passed. Headless
smoke run clean.

---

# Not done

- **A watch that stops the capture.** "Stop when this happens" is the obvious
  next step and is a different kind of thing: it acts on the session rather
  than reporting on it, and getting it wrong loses traffic that cannot be
  recovered. Worth doing deliberately, not as an extra checkbox.
- **Watches over a recording.** The assertion engine already covers that
  ground from the other direction. Whether these should be one feature with
  two modes, or stay two, is a question about how test cases are written
  rather than about code.

---

# Clicking a pin goes to the frame

Asked: does clicking a pin take me to the packet where the value became that?

It did not. Double-clicking switched to the **tab** and left the operator to
find the frame themselves, which answers "which source" when the question is
"which packet". The watch panel already jumped to its evidence; pins never
recorded which frame produced the value.

They do now. Double-click, or right-click → **Go to the frame where it became
`2 (Staff Responsible)`**, and the log selects that frame.

## Which frame, exactly

The one where the value **became** what it is — not the newest one carrying
it. A frame repeating the same value does not move the target, because the
value became what it is once, and landing on the most recent frame would
answer a different question every second. Before a pin has ever changed, the
target is the frame it was first seen in, which is still the frame that
established the value.

## Two timestamps, and using the wrong one lands nowhere

A pin now keeps the **log** timestamp of those frames as well as the wall
clock. They are not the same: one is when this program saw the value, the
other is when the equipment sent it, and a replayed session makes the
difference obvious. The jump goes by log time, because that is what the log is
ordered by.

The test uses deliberately unrelated numbers for the two, so a confusion
between them cannot pass unnoticed.

## Stored as timestamps, not as frames

A pin is set up once and left for the length of a run. Holding the entries it
has passed through would keep them alive after the model evicted them — a slow
leak in exactly the long session where a pin is most useful. Timestamps cost
sixteen bytes and are what `jumpToEntry` wants anyway.

The honest limit: a frame the model has since evicted is not found, and the
tab comes forward without a selection. That is as much as can be done once the
row is gone.

A pin that has seen nothing offers no jump at all, and says so rather than
moving somewhere arbitrary.

Full suite: **110 suites, 2810 checks, 0 failed.** Menu audit passed. Headless
smoke run clean.

---

# Freezing the board

The two panels built this session answer halves of one question. A watch says
**when** something happened. A pinned board says what a field is **now** — and
"now" is the wrong tense the moment something happens: by the time the
operator looks up from the DMI, now has moved on and the state they wanted is
three hundred frames back.

So the board can be **frozen**, and a watch can freeze it.

```
Frozen 15:02:31 (watch: mode left Stand By) — values held, updates paused.
```

`Freeze` holds it by hand. The **on watch** checkbox holds it automatically
the moment any watch fires, naming which one. What is captured is the state of
every pinned field at the instant the condition was met — which is the state a
test record wants, and the state that is gone a few seconds later.

## Held, not cleared

A frozen board still shows every value, when each was last seen, when each
last changed, and which frame established it. Nothing is lost by freezing
except the updates, and those resume on thaw with the next frame carrying the
field. Freezing is therefore always safe to do and never costs information —
which is what makes it usable in a hurry, which is the only time it will be
used.

## The first freeze wins

A second watch firing does not move the instant the first one captured. The
board froze because something happened; the something that happened first is
the one being investigated.

## The button never lies

An automatic freeze checks the Freeze button as well. A frozen board looks
exactly like a live board whose traffic has stopped, and the button is the one
control that says which — a frozen board under a button reading "Freeze"
would be a lie told by the thing meant to report it. The status line says so
too, as a warning rather than a note, because mistaking a held reading for a
current one is how a stale value gets written down.

## Tests

12 more checks: that a frozen board takes nothing in, that not even the
sighting count moves, that values and origin frames survive the freeze, that
thawing resumes on the next carrying frame, and that a second freeze does not
displace the first.

Full suite: **110 suites, 2822 checks, 0 failed.** Menu audit passed. Headless
smoke run clean.

---

# One thing checked rather than built

"Copy a frame as a `@` capture line" was on my own list of things worth adding.
It already exists: a captured row's text **is** the capture line, and the row
menu's Copy hands it over whole, ready to paste into the Decode Workbench.
Verified against `replay/` rather than assumed, and then not built.
