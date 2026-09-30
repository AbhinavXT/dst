# Session 35 — the clipped tab label

| File | Change |
|---|---|
| `uistyle.{h,cpp}` | `useMeasuredTabs()` — a tab bar measured for the weight it draws at, and tooltips carrying the full name |
| `mainwindow.cpp`, `lococonsolewindow.cpp`, `replaywindow.cpp`, `sessionwindow.cpp`, `dlrplayerdialog.cpp` | all five tab widgets use it |
| `tests/test_tabmetrics.cpp` | 10 checks |

From a photograph of the running console: the selected tab, whose source is
`L1_V1`, rendered as `_1_V1`. The tabs either side — `Fault_L2V2`, `L1_V2` —
drew whole.

---

# READ THIS FIRST: the fix is not proven

I could not reproduce the clipping on this machine. Rendered here, with the
program's own stylesheet, the selected tab measures 78px against a 51px label
and the ink sits 12px clear of each edge. Nothing is cut off.

That does not mean it is not happening on yours. It means the cause is
platform-dependent, and the most likely reason is below — but it is a reading
of the code, not a reproduction, and it should be treated as such until the
next build is looked at.

---

# What is wrong in the code regardless

Patch 30 gave the selected tab `font-weight:600`, deliberately: across a row
of eleven sources a 2px underline alone does not say which one you are
reading.

`QTabBar::tabSizeHint()` measures a label with the **widget's** font — the
normal weight. So every tab is reserved the width of its label unbolded, and
the selected one is then drawn bold into that rect. Qt centres tab text, so an
overrun is cut off at **both** ends — which is exactly the shape of the
reported fault, a leading character lost rather than a trailing ellipsis.

Whether `QStyleSheetStyle` compensates for this depends on the platform style,
and that is where the two of us differ: this is Linux and Fusion, yours is
Windows. It is also where the font differs, and Segoe UI bolds wider than what
is installed here.

`useMeasuredTabs()` installs a tab bar that adds the demi-bold-minus-normal
difference to every tab's width. Every tab, not only the current one —
reserving it per-tab would make the whole bar shuffle sideways on each
selection change, which is a worse fault than the one being fixed. Measured
here it costs 5px a tab.

# The part that works whatever the cause

Every tab now carries its full name as a **tooltip**. A tab that cannot be
fully drawn can still be identified, and that holds whether the cause is the
weight, the font, the style, or something not yet thought of.

Tab text arrives after insertion — a source tab is named when its first
message arrives — and QTabBar has no signal for it. `QEvent::LayoutRequest`
looked like the tidier hook and simply never arrives; `tabLayoutChange()` and
`tabInserted()` between them do.

## Eliding was tried and taken back out

Setting `Qt::ElideRight` would make an over-long label end in an ellipsis
instead of losing a character, which sounds like exactly the right guard. It
also replaces the tab bar's scroll behaviour with shrink-and-ellipsis for
**every** tab at once, so eleven sources become eleven `L1_…`. That is a
bigger change than the fault it guards against and nobody asked for it. It
broke two width checks on the way, which is how it got noticed.

---

# The other possibility, which needs you

If the label is genuinely `_1_V1` — not clipped, just named that — then none
of the above is the fault, and the fault is in the names file or in what
built it. Two things tell them apart in the next build:

1. **Hover the tab.** The tooltip is the untouched string. If it reads
   `_1_V1`, the name is wrong and the rendering is fine.
2. **Widen the window.** A clipped label gains its missing character; a badly
   named one does not.

I would rather say this than ship a confident fix for the wrong fault, having
already shipped one confident fix for this that did not hold.

---

# Tests

`tabmetrics`, 10 checks: that the measured bar reserves the whole
bold-versus-normal difference, that the label therefore fits at the weight it
is drawn, that identical labels get identical widths whichever is selected,
that a rename is re-measured and re-tooltipped, and that calling the helper
after tabs exist declines rather than replacing the bar and dropping them.

They test the property — the label fits — not a pixel count, which would be a
test of this machine's font rather than of the bug.

Full suite: **102 suites, 2606 checks, 0 failed.** Menu audit passed. Headless
smoke run clean.
