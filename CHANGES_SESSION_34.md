# Session 34 — the last three status lines

| File | Change |
|---|---|
| `dlrplayerdialog.{h,cpp}` | 9 messages moved onto StatusLine |
| `framediffwindow.{h,cpp}` | 4 messages, all captions |
| `fieldplot.{h,cpp}` | 2 messages, one of them a real warning |
| `tests/test_statusconventions.cpp` | new: 14 checks, plus the audit of what was left |

Open since patch 30: *feedback consistency — 35 status labels across 13 files.
Shared colours now, but each still has its own wording, placement and
lifetime.*

`StatusLine` was written to settle it and eight surfaces adopted it. Three did
not, and they turned out to be the three with the most to gain.

---

# The rule, restated

Four verbs for events and one for a caption:

| | | |
|---|---|---|
| `say()` | neutral progress | transient |
| `ok()` | it worked | transient |
| `warn()` | it worked, but read this | **sticky** |
| `fail()` | it did not work | **sticky** |
| `state()` | what this panel is showing | **sticky** |

Successes expire because a stale success describes a state that has passed —
"Sent 40 frames" ten minutes later reads as current. Failures do not expire,
because the operator decides when they have read them. Captions do not expire
because they are not news.

---

# The replay player

Nine messages, and the interesting one is this:

```cpp
if (m_status->text().startsWith(tr("Seek query never matched"))) {
    // Leave the more informative message in place.
} else {
    m_status->setText(completed ? tr("Finished.") : tr("Stopped."));
}
```

The intent is exactly right — do not let *"Finished."* paper over the reason
the run sent nothing — and the mechanism is a string comparison against the
sentence it is trying to protect. It survives precisely as long as nobody
rewords that sentence, and the thing that would break it is a translation or a
tidy-up, neither of which would look like a change to replay behaviour.

It is now `if (!m_status->isSticky())`. The property is asked about directly
instead of being inferred from the text, which is the whole reason a failure
knows it is a failure.

The rest divide as you would expect once there is a vocabulary to divide them
into: a bad seek query and an exhausted seek are `fail()`; problems found
while loading a session and *"Press Escape again to close"* are `warn()`,
because both are things the operator must read before the next action;
*"Skipping…"* and *"Finished."* are `say()`; a landed seek is `ok()`.

# The frame diff window

All four of its messages are captions, including the results. *"3 frames · 7
fields differ"* describes the table underneath, not something that just
happened, so it is `state()` — sticky, and not coloured as a success.
*"No field differences"* is a finding, not a win.

# The field plot

Its warning existed only as a stylesheet swap:

```cpp
m_status->setStyleSheet(s.rowsNonNumeric > 0 ? UiColor::warningStyle()
                                             : UiColor::mutedStyle());
```

Colour alone is not a signal — a quarter of the contrast work in this program
exists because text was unreadable, and to a colour-blind reader an amber
sentence and a grey one are the same sentence. `warn()` gives it a glyph as
well. Discarded rows are worth the warning: the plot then shows fewer points
than the tab holds, which reads as missing data rather than as a decision.

---

# What was NOT converted, and why

This is the part worth keeping, because the question will come round again.

A status line reports an **event**. The labels below describe **standing
state** or are fixed captions, and giving them expiry and a glyph would be
wrong in both directions: state that expires is worse than no state, and a
tick beside a permanent readout says something just succeeded when nothing
did.

| Where | What it is |
|---|---|
| `mainwindow` bind / disk status | the socket is bound or it is not — true until it changes |
| `filterbar` count | a count of what is showing, recomputed per filter |
| `findbar` count, wrap hint, hold hint | a live readout of the current search |
| `lococonsole` CRC / seq | per-source health indicators |
| `settingsdialog`, `gototimestamp` hints | fixed captions in a form |
| `comparewindow` time label | the pane's current timestamp |

Two sit on the line and were left alone **deliberately**, not by oversight:
`sessionkeydialog`'s explanation and `sessionwindow`'s summary both report the
result of an action *and* describe the state the dialog is now in, and both
are the only text in their panel. Converting them means deciding whether "no
key set loaded" is a failure or a caption, and that is a question about the
workflow rather than about labels.

The audit lives in a comment at the foot of `test_statusconventions.cpp`, next
to the tests, so the next person to ask "why is that one different" finds the
answer where they are already looking.

---

# Tests

`statusconventions`, 14 checks: the five verbs and their lifetimes, that the
displayed text carries a glyph as well as the message, that a failure is not
overwritten by the routine message that follows it, and that the frame diff
window opens with a caption rather than an event.

They pin the **rule**, not the wording. Wording changes; the rule is what an
operator learns.

Full suite: **101 suites, 2596 checks, 0 failed.** Menu audit passed.
Headless smoke run clean.
