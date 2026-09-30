# Patch 48 — the Time column

Cosmetic, and it pays for itself the first time you scan a column of
timestamps.

## Two problems, both about ink

**The column was drawn in the proportional UI font.** A `1` is narrower than
a `0`, so no two timestamps line up. A column of times is read by scanning
DOWN it, and nothing in a proportional font lines up vertically — "which of
these is 200 ms later" became a character-by-character comparison.

**Consecutive rows share almost every character.** Reading

    11:04:09.100
    11:04:09.140
    11:04:09.980
    11:04:10.000

the eye walks past eight identical characters to reach the two or three that
differ, on every row, for thousands of rows. The information is entirely in
the tail and the ink was almost entirely in the head.

## What it does now

A fixed font, so digits line up down the column; and the leading run of
characters identical to the row above is drawn muted, so the characters that
changed carry the contrast. A second or an hour rolling over lights up a
longer run, which is exactly the row worth noticing.

Nothing is hidden or abbreviated. The full timestamp is still there, still
selected, still copied in full — only its weight changes.

## The parts that are easy to get wrong

- **Compared against the row above IN VIEW ORDER**, not the previous entry in
  the model. Under a filter or a sort, the row above is what the eye actually
  compares against; dimming relative to a row that is not on screen would
  mute the wrong characters.
- **The bookmark bullet is not part of the time.** `LogModel` prefixes a
  bookmarked row's time with `●`. Comparing the rendered strings would make a
  bookmarked row share nothing with its neighbour and light up whole, for
  entirely the wrong reason.
- **Never the entire string.** A burst arriving inside one millisecond is
  normal here, and a row muted end to end reads as disabled rather than as
  identical, so one character always keeps full weight.
- **Nothing is muted on the selected row.** Its background has changed
  underneath it, and a muted-on-highlight colour is the one that fails a
  contrast check in one theme or the other.
- **Background, selection and focus ring are still drawn by the style**, not
  by hand. Painting them manually is how a delegate ends up looking almost
  but not quite like every other cell, and how it stops following the theme.
- **Split runs are laid out from the whole string's rect**, so splitting the
  text does not move it. Otherwise the column would shimmer as rows scroll.

Installed in `LogTableView::configure`, so the log tabs, the compare panes
and anything else built through it agree — a column painted one way in one
window and another way beside it is worse than either.

## A note on the tests

Four of my first expectations were wrong and the code was right: `.140`
against `.100` shares ten characters, not eleven, and `12:00` against `11:59`
shares the leading `1` rather than nothing. Dimming is by CHARACTERS, not by
meaning. Corrected the expectations, and said so in the test text so the next
reader does not re-derive it.

## Files touched

    logtimedelegate.{h,cpp}          NEW
    logtableview.cpp                 installs it
    tests/test_logtimedelegate.cpp   NEW
    DLConsole.pro, tests/*.pro

## Verification

    115 suites, 2944 checks, 0 failed      (was 114 / 2929)
    menu audit passed
    headless smoke run clean

The contrast audit passes unchanged: the muted run uses the existing
`UiColor::muted()` role, which is already measured in both themes, and no
colour literal was added.
