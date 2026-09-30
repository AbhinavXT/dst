# Patch 47 — Find under live traffic

## The report

> When packets are continuously incoming the find takes a lot of time, and as
> soon as I disconnect the ethernet and packets stop, the find becomes fast.

## The cause

`FindBar` treated **every** model change as a renumbering:

    connect(model, rowsInserted,  scheduleRebuild);   // m_scanDirty = true
    connect(model, rowsRemoved,   scheduleRebuild);
    connect(model, modelReset,    scheduleRebuild);
    connect(model, layoutChanged, scheduleRebuild);

For three of those four that is correct and necessary. A filter change, a
re-sort, or a row falling off the front all mean row 400 is a different
message than it was, so every match row already found is a number pointing at
the wrong thing and the answer has to be thrown away.

**An append is not that.** Rows land at the end, every row already scanned
keeps its number, and keeps its verdict with it. Marking the scan dirty threw
that away anyway, so each arriving batch caused a full scan of every row in
the tab. Cost grew with the square of the session length while traffic ran,
and dropped to nothing the instant it stopped — exactly the symptom.

The 200 ms debounce hid it at low rates and made it worse at high ones: under
steady traffic the timer kept restarting, so scans landed in bursts on a
model that had grown since the last one.

## The fix

Inserts are now distinguished from the changes that really do renumber. When
rows arrive at or past the end of what was last scanned, the existing matches
are kept and only the tail `[previousRows, rowCount)` is scanned.

Guarded by the same standard as the existing narrowing path — the conditions
are load-bearing:

- **Appended at the end.** `first >= m_lastScanRowsTotal`, with no valid
  parent. An insert in the middle renumbers and falls back to a full scan.
- **The previous scan was not capped.** A capped scan never reached the end,
  so "past what we scanned" is not "past what exists".
- **The same search.** Compared on the box's text **as typed**, not on the
  decoded pattern.

That last point matters more than it looks. Narrowing compares the decoded
plain text because it rests on one pattern containing another. Appending
needs only "is this the same search", which the raw text answers for every
mode — so Regex and Hex, whose decoded text is empty and whose per-row test
is the most expensive of the five, are covered too. Deciding it on the
decoded form would have quietly excluded them.

Anything else — filter, sort, removal, reset, a changed pattern — still
forces the full scan it always did.

## Measured

Instrumented row counts, 20 000 rows already present, then 100 batches of 50:

    before   2,252,500 rows scanned
    after        5,000 rows scanned

The instrumentation was removed before shipping; it is quoted here because
"it feels faster" is not a measurement. The ratio is not fixed — the old
behaviour was quadratic in session length and the new one is linear, so it
widens the longer a session runs. On a tab that has been up for hours the
difference is far larger than 450×.

## Tests

New suite `findlivetraffic`. The point of risk in an incremental scan is a
subtly different answer, so the central check is agreement: a second bar that
never saw the appends and has nothing to reuse scans the same model from
scratch, and must produce the identical match list. Also covered: hits
accumulating across many batches in row order, a changed pattern rescanning
rows the previous search had dismissed, a filter invalidating everything
(match rows still in range AND still genuinely matching), and a row arriving
under a Regex search being picked up.

## Files touched

    findbar.{h,cpp}            append path, m_scanAppendOnly, m_scanRawText
    tests/test_findlive.cpp    NEW
    tests/tests.pro

## Verification

    114 suites, 2929 checks, 0 failed      (was 113 / 2917)
    menu audit passed
    headless smoke run clean
