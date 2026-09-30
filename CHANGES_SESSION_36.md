# Session 36 — the two blank tabs

| File | Change |
|---|---|
| `uistyle.cpp` | a rule for the tab bar's scroll buttons |
| `tests/test_tabmetrics.cpp` | +3 checks: the arrows are drawn |

Reported after patch 35: the labels draw whole now, but *"I get these 2 blank
tabs at the end of the rest of the tabs."*

---

# They are not tabs

They are the tab bar's **scroll buttons**, which appear when the tabs no
longer fit. They are `QToolButton`s living inside the bar, so the sheet's
generic rule caught them:

```css
QPushButton, QToolButton {
  background:…; border:1px solid …; border-radius:6px;
  padding:5px 12px; }
```

Two things follow. Twelve pixels of horizontal padding on a sixteen-pixel-wide
button leaves nothing to draw into; and once a stylesheet has styled a tool
button, Qt stops drawing its arrow primitive altogether. What is left is a
bordered, rounded, empty box — which is precisely a blank tab.

Rendered here with the shipped sheet, the two scrollers contained **1 and 451**
ink pixels: one entirely empty, the other only its own border. With a rule of
their own they contain 35 and 99 — a disabled left arrow and an enabled right
one, which is what a bar scrolled to its start should show.

## Why now

Patch 35 made every tab wider by the bold-versus-normal difference, to stop
the selected label being clipped. That is the change that pushed the bar past
the window width, so the scrollers appeared for the first time.

So this is my doing, in the sense that it took a latent stylesheet fault and
made it visible. The fault has been in the sheet since the tab rules were
written; it needed an overflowing tab bar to show, and until last week the
tabs were narrow enough not to overflow.

---

# The fix

```css
QTabBar::scroller { width:32px; }
QTabBar QToolButton {
  background:…; color:…; border:1px solid …; border-radius:0px;
  padding:0px; margin:0px; min-width:16px; }
```

Zero padding and no radius, so the button is a plain square with room for its
arrow, and hover/pressed/disabled follow the same palette as every other
button.

Worth noting what was NOT done: the arrows are left to the platform style
rather than replaced with images of our own. An `image:` rule would have
guaranteed the same arrow everywhere, and would also mean shipping a pair of
PNGs that are wrong at every scale factor except the one they were drawn for.

---

# Tests

Three checks: the overflowing bar grows exactly two scroll buttons, and each
has ink drawn inside it.

**Ink, not appearance.** The fault was that nothing was drawn at all, so
counting pixels that differ from the button's own background is the property
that matters. Asserting *which* arrow, or how many pixels of it, would be a
test of the platform style rather than of the bug — and the platform style is
the thing that differs between my machine and yours, which is what made the
clipping in patch 35 so slow to pin down.

Full suite: **102 suites, 2609 checks, 0 failed.** Menu audit passed.
Headless smoke run clean.

---

# Patch 35, confirmed

The label fix worked: `L1_V1` draws whole in the photograph. Recorded here
because that patch shipped explicitly unverified — I could not reproduce the
clipping on Linux and said the fix was a reading of the code rather than a
tested one. It was the right reading.
