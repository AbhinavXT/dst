# Session 38 — tabs: smaller, unbolded, selected by fill

| File | Change |
|---|---|
| `uistyle.cpp` | tab font a point smaller and normal weight; selection is a background fill |
| `tests/test_tabmetrics.cpp` | 32 checks, including the eleven-tab case that broke it |

---

# The photograph settled it

Every tab lost characters at **both** ends — `Fault_L1V1` as `ault_L1V`,
`L1_V1` as `_1_V1` — and not only the selected one. That is not eliding, which
would show an ellipsis, and not the close cross, which would only eat the
right. It is a tab bar drawing a centred label into a rect too small for it.

Which is what a `QTabBar` does when it cannot fit its tabs: it **shrinks**
them. And it could not fit them because the console had just been reopened and
brought back **eleven saved tabs at once**.

So the fault was never really about the selected tab. It was about width, and
every fix so far had been spending width rather than saving it.

---

# What changed

Both as you asked:

```css
QTabBar::tab           { font-size:<base − 1>pt; font-weight:normal; padding:5px 24px 5px 10px; }
QTabBar::tab:selected  { background:<fill>; color:<text>; border-bottom:2px solid <accent>; }
```

**A point smaller.** The tab bar is the one place where the number of items is
decided by how many sources are open rather than by design, so it is the one
place worth buying width back with size. Floored at 8pt — a label too small to
read is a worse trade than one that does not quite fit.

**Normal weight, filled selection.** A filled tab among unfilled ones is at
least as findable as a bold word among plain ones, and unlike weight it costs
no width. The accent underline stays, so selection is said twice.

Measured with your eleven tab names:

| | before | now |
|---|---|---|
| width for eleven tabs | did not fit | **1308px** |
| tightest label's spare room | negative — that is the crop | **60px** |
| scroll arrows at 1900px | — | none |

At 1280px it scrolls, which is correct: at that point they genuinely do not
fit.

---

# The thread through patches 35–38

Every one of these came from the same decision, made in patch 30, that the
selected tab should be **heavier**:

| | what happened |
|---|---|
| 35 | bold label overran a rect measured unbolded → clipped |
| 35 | reserving the difference in C++ widened tabs but not the bar → arrows in a half-empty bar |
| 36 | those arrows were blank, a real separate fault in the sheet |
| 37 | levelled the weight, reserved the close cross in the sheet |
| 38 | the weight cue removed entirely, and a point of size with it |

The lesson is narrow and worth writing down: **a selection cue that changes
the label's width is a cue that has to be paid for in layout, and Qt will not
be told about it after the fact.** Colour and fill cost nothing. Weight and
size cost width, and the bill arrives somewhere else.

---

# Tests

32 checks. The ones that matter here:

- eleven tabs — a full workspace reopened — fit 1900px with no scrollers, and
  **no tab is narrower than the name it carries**, which is the exact state
  that drops characters off both ends
- selecting a tab does not change any tab's width
- the selected tab is filled differently from its neighbours by a margin wide
  enough to find at a glance, sampled from a render rather than asserted about
  the stylesheet
- the label sits clear of both edges of its tab
- a bar that really is too narrow scrolls, and its arrows have ink in them

Full suite: **102 suites, 2628 checks, 0 failed.** Menu audit passed. Headless
smoke run clean.
