# Session 31 — Find: extended search, and a window instead of a strip

| File | Change |
|---|---|
| `findbar.{h,cpp}` | search modes, extended escapes, hex-byte search, two layouts |
| `logquery.{h,cpp}` | `parseHexPattern()` — one strict parser, shared with `hex:` |
| `settings.h` | find mode, wrap, scan limit, advanced-open |
| `tests/test_findmodes.cpp` | new: 6 suites, 91 checks |
| `tests/test_findextras.cpp` | controls looked up by objectName, not by label |
| `tests/tests.pro` | new file |

Two things were asked for: the extended search mode left open at the end of
patch 30, and a real window in place of the one-line strip.

---

# Five checkboxes were five answers to one question

Find had `Aa`, `W`, `.*` and `Query` as independent boxes, and the scan
resolved the contradictions at the moment it ran — `m_regexCb->setEnabled(!m_queryMode)`
and two more like it, buried inside `scanMatches()`. Ticking Query and regex
together was possible; which one won was decided by a line nobody reading the
UI could see, and the only clue was a control greying out after the fact.

They are now a **mode**, because they are five ways of asking the same
question:

| Mode | What the text means |
|---|---|
| Text | plain substring, honouring Case and Whole word |
| Extended | the same, after `\n \r \t \0 \xHH \uHHHH \\` are turned into what they name |
| Regular expression | a pattern the operator writes |
| Hex bytes | bytes of the datagram — `0a 1b`, `0A:1B`, `0a1b` are the same |
| Query | the boolean language, identical to the filter bar's |

`syncModeControls()` decides once per mode change which controls still mean
something, and a help line under the selector says what the mode accepts.
That line is the actual fix for discoverability: the query language was
previously findable only by hovering a checkbox and reading a tooltip.

## Extended mode refuses escapes it does not know

`\s` in extended mode searches for a backslash and an `s`, finds nothing, and
says nothing about why — that is what most editors do. Here an unrecognised
escape is an **error**, named, with the offset marked in the input box:

> `\s is not an escape this mode knows — try \n \r \t \0 \xHH \uHHHH or \\`

Same for a trailing lone backslash, and for `\x` or `\u` with the wrong number
of digits. This is the same reasoning as the round-trip validator in patch 30:
in this program the failure that costs an afternoon is the silent one.

## Hex searches the frame, not the rendering

Hex mode matches against `rawBytes` — the datagram as it arrived — rather than
against whatever the Message column happens to render. So the column selector
is switched off in that mode, and, like Query, it needs a `LogModel` behind
the view to have anything to match against.

## One hex parser, not two

The query language already had `hex:`, and its parser accepted any
`isLetterOrNumber()` character before handing the result to
`QByteArray::fromHex()`. So `hex:0g1b` passed the check and became whatever
`fromHex` made of it, with no error — a different pattern from the one asked
for, no matches, and nothing on screen saying so.

`parseHexPattern()` now lives in `logquery.cpp` and is used by both `hex:` and
the Find window. It skips the separators an operator actually produces (space,
`:`, `-`, `,`, `.`, `_`), and anything else that is not a hex digit is an error
naming the character and its position. An odd digit count says so in the terms
the mistake was made in — *"bytes come in pairs of digits — 5 digits is one
short"* — because the cause is nearly always a byte typed as one digit, and
the operator needs to find the one they dropped rather than retype the line.

---

# The strip and the window are the same widget

Detaching already reparented the bar rather than building a second find UI,
and that stays: one set of state, one scan, one cursor. What changed is that
the two shapes no longer have to be the same shape.

**Docked** it is still one line above the table, where every pixel is spoken
for — short labels, glyph buttons, no advanced section.

**Undocked** it is a form:

- the input gets a **row of its own**. In the strip it was whatever width was
  left after nine controls had taken theirs, which for a query of any length
  meant typing into a slot
- mode and column on their own row, with the help line under them
- the options spelled out — *Case sensitive*, *Whole word only*,
  *Highlight all matches*, rather than `Aa`, `W`, `Mark`
- an **Advanced** section, closed by default, holding the two things that
  change how the search *behaves* rather than what it looks for

`applyShape()` moves the same widgets between the two layouts and is the only
place either arrangement is described. Labels are set there too, which is why
the tests now look controls up by `objectName` — matching on text would
quietly have become a test of which shape the bar opened in.

One consequence worth stating: a child widget left out of a layout keeps its
last geometry rather than disappearing, so everything the strip does not place
is hidden explicitly. There is a check for exactly that, because the failure
would be the advanced panel floating over the table.

## Advanced: wrap, and the scan limit

**Wrap around** was unconditional. It is still the default, but it can be
turned off, and the reason is particular to a log rather than to a text
editor: a recording is read in order to be reported on, and wrapping silently
is how the same three matches get read twice and written up as six. With it
off, Next stops at the last match and says `(last match)` rather than doing
nothing — a dead button reads as a bug.

**Scan limit** was a compiled-in 200,000 rows. The cap has a real purpose —
the scan runs on the GUI thread — but the number was a guess, and worse, the
count label quoted `kScanRowCap` whether or not that was the limit actually
used. It is now a choice (20k / 200k / 1M / every row), and the label quotes
the number the scan really walked.

## A regex that would not compile said nothing

`scanMatches()` had `if (!re.isValid()) return;` with a comment saying *"Bad
regex — show 0 matches but don't crash"*. The operator saw `no matches`, which
is the same thing a correct pattern with no hits says. Every mode now reports
its parse failure through the input box's existing error marking —
`QueryLineEdit` has carried an offset-aware error display since the query
language landed, and Regex, Extended and Hex now use it too.

---

# The bug this uncovered: a window parented to itself

`setDetached(true)` built its host as `new QDialog(window())`, then reparented
the bar **into** that host. For a bar with a parent widget that is fine. For a
bar with no parent of its own, `window()` returns *the bar itself* — so the
host's parent became the bar, and the bar's parent became the host.

A cycle in the widget tree. `QWidget::nativeParentWidget()` walks parents
until it finds a native window, and Qt calls it on every reparent, so it
never returns. The application hangs. It does not crash, which is worse: the
first run of the new `findwindow` suite sat at 100% CPU for eight minutes
producing no output at all, and it took a `gdb` stack to see that the top
frame was `nativeParentWidget` under `QBoxLayout::insertWidget`.

The host now anchors on **the view's window** — the console this bar searches,
which is where a find window belongs anyway — and falls back to no parent
rather than to anything that could be the bar.

This was reachable before this session and not reached, because the only bars
that detached had a parent widget. A test that constructed one without a
parent found it immediately.

---

# Tests

| Suite | Checks | What it pins |
|---|---|---|
| `findescapes` | 24 | every escape, and every way of writing one wrong |
| `findhexpattern` | 13 | separators, odd digits, non-hex, shared with `hex:` |
| `findmodes` | 23 | each mode finds what it should and ignores what it should |
| `findwindow` | 20 | wrap on and off, the shapes, the advanced section |
| `findscanlimit` | 7 | the cap is used and the label quotes the real number |
| `finddirectmodel` | 4 | modes needing an entry are quiet when there is no LogModel |

`test_findextras` keeps its 11 checks with the lookup changed.

Full suite: **97 suites, 2517 checks, 0 failed.** Menu audit passed
(23 shortcuts, 55 commands reachable). Headless smoke run clean.

---

# Not done

- **`\d` decimal and `\o` octal escapes.** Notepad++ has both. `\xHH` covers
  the case that comes up here, and three ways of writing the same byte is
  three things to get wrong.
- **Hex search with wildcards** (`0a ?? 2c`). Worth having, but it is a
  different matcher — a byte-mask scan rather than `QByteArray::contains` —
  and it belongs with the field-aware work rather than bolted to Find.
- **Searching rows the filter has hidden.** Find still searches what the
  filter leaves visible, which remains the right default; an "include
  filtered-out rows" option would mean scanning the source model and
  navigating to rows the view cannot show.
