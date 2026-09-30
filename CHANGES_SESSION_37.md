# Session 37 — arrows in a half-empty bar

| File | Change |
|---|---|
| `uistyle.cpp` | one font weight for every tab; the width arithmetic removed |
| `uistyle.h` | `useMeasuredTabs()` → `useTabTooltips()`, which is now all it does |
| five window files | renamed call |
| `tests/test_tabmetrics.cpp` | rewritten: 15 checks on the properties, not the pixels |

Reported: the scroll arrows appear with more than half the tab bar still
empty.

---

# It was patch 35, and it was mine

Reproduced here on the first try. Eight tabs, a 1900px window:

```
bar width 612    tabs laid out 644    →  arrows, and 1288px of nothing beside them
```

The bar had already been sized — 612px — before the tabs were widened to 644.
Widening a tab does not widen the bar that Qt has already measured, so the bar
concluded it was overflowing and scrolled, in a window nearly three times
wider than it needed.

I tried to close that by overriding the bar's `sizeHint()` to sum the widened
tabs. It changed nothing: the hint still came back 612 while the laid-out
rects still totalled 644. At that point I was reverse-engineering which of
Qt's several size paths `QStyleSheetStyle` was actually consulting, which is
not a position to ship a third fix from.

# So the gap is gone instead of managed

Every tab is now drawn at **one weight**:

```css
QTabBar::tab           { … font-weight:600; … }
QTabBar::tab:selected  { background:…; color:…; border-bottom:2px solid accent; }
```

What is measured is what is drawn, tab width goes back to being Qt's business,
and all three faults reported since patch 30 have the same root removed:

| Reported | Cause |
|---|---|
| `L1_V1` drew as `_1_V1` | bold label overran a rect measured unbolded; Qt centres tab text, so it was cut at both ends |
| arrows in a half-empty bar | the fix for that widened tabs but not the bar |
| the arrows were blank boxes | the sheet's generic `QToolButton` rule styled them, and a styled tool button loses its arrow |

The third was a real, separate fault in the sheet and its fix from patch 36
stays. The first two were one mistake, made twice.

## What the selected tab lost, and what it kept

It no longer stands out by weight. It still stands out by **background**, by
**text colour**, and by a **2px accent underline** — which, looking at the
photographs, is what was doing most of the work anyway.

That is the trade: a slightly quieter selection cue, in exchange for measured
and drawn agreeing. Patch 30 was right that eleven tabs need a clear cue. It
was wrong to spend the one cue that changes the label's width.

---

# Verified across three widths

| Window | Bar | Scrollers | Selected label |
|---|---|---|---|
| 1900px | 580 | none | 12px clear of both edges |
| 700px | 580 | none | 12px clear |
| 300px | 300 | two, with arrows drawn | 12px clear |

Scrolling now happens when, and only when, the tabs genuinely do not fit.

---

# Tests

Rewritten around what actually matters, 15 checks:

- eight tabs need less than the window, the bar asks for at least the room its
  tabs occupy, and **no arrows are shown** — the regression guard
- the selected label is wider than nothing and touches neither edge of its
  tab, measured from a render rather than from arithmetic
- selecting a tab does not change any tab's width, which is the invariant that
  keeps measured and drawn together
- a bar that really is too narrow does scroll, and each arrow has ink in it
- the full name is on every tooltip, and follows a rename

The old suite tested that the bar reserved a specific extra width. That was a
test of the wrong fix, and it passed while the bug it caused was on screen.

---

# `Fault_L1_V1` still cropped

Reported alongside the arrows, and a different fault from them. Not reproduced
here either: on this machine that tab gets 127px for a 113px label.

## What the pattern says

`Fault_L2V2` drew whole in the earlier photograph; `Fault_L1_V1` crops. Two
characters apart. That is the shape of a **fixed reserve that is missing**,
not of a shortfall that grows with the label: a short name survives on its
12px of padding, a long one runs into whatever sits at the right-hand end of
the tab and is cut.

What sits there is the **close cross**. Whether a stylesheet-styled
`::close-button` counts towards the tab's size hint is up to the platform
style, and the platform style is what differs between these machines — as it
did for the clipping and for the arrows.

## The reserve is stated, not computed

```css
QTabBar::tab { padding: 6px 28px 6px 12px; }   /* 12 padding + 4 margin + 12 cross */
```

Every tab gets 16px more room on its right than before, on every platform.

The important part is **where** it is stated. This is the same reserve that
patch 35 computed in C++ and applied to the laid-out tabs, which is what put
scroll arrows in a half-empty bar — the bar had already been sized without it.
In the stylesheet, sizing and drawing read the same rule, so they cannot come
apart. It is the same lesson as the font weight, applied to the other end of
the tab.

Nine tabs including `Fault_L1_V1` now want 851px, so a 1900px window still
shows them all without scrolling.

Three added checks: a closable tab is wider than its label by at least the
cross plus its padding, long name or short, and a longer name still gets a
proportionally wider tab.

**Still unverified on your machine**, like the two before it. If the crop
survives this, the thing that would settle it is whether the tab shows an
ellipsis or simply loses characters — an ellipsis means Qt knows it does not
fit and is eliding, missing characters means it thinks it fits and is drawing
past the edge. They have different causes and I have been guessing between
them.

Full suite: **102 suites, 2614 checks, 0 failed.** Menu audit passed.
