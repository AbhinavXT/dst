# Patch 45 — compare panes get the log tab's functionality

Scope as confirmed: no filter proxy (so no filter bar, marker scrollbar or
ribbon), no follow-tail in panes, and bookmarks, decoded fields and the two
row-menu verbs added.

## The panes share the log tab's table setup

`applyDensity`, `applyColumnVisibility` and `applyColumnWidths` were private
methods of `MainWindow`, so any log table elsewhere in the program got
whatever its own construction happened to say. The compare panes hard-coded
44 px rows and their own column widths: setting rows to Comfortable or hiding
the Source column changed the log tabs and left the panes looking like a
different program.

All three now live in **`logtableview.{h,cpp}`** as free functions of Settings
and a view. `MainWindow` forwards to them, so there is one answer in the
program to "how tall is a row".

What the panes gained with it:

- **Multi-row selection.** Shift-click a span, ctrl-click to add. Copying a
  block into a report was a row-at-a-time job before.
- **Density, word wrap, column visibility, stored column widths** — all
  following the same settings as the log tabs. Re-applied on rebind too,
  because `setModel` resets the header.
- **An empty-state placeholder**, distinguishing an unbound pane from a bound
  one whose source has gone quiet.

The copies and the row menu now act on the whole selection: right-clicking
inside a span offers "Copy 12 messages", not one row.

## Decoded fields, not just bytes

The bottom of the window was a hex dump. It is now hex **and** decoded fields,
side by side in a splitter, because the useful comparison across two sources
is usually of decoded values — "LSRP says Stand By here and Staff Responsible
there" — not of hex. With bytes alone the operator had to carry the row back
to a log tab to read it.

The inspector's byte highlight writes into the dump beside it, as in the main
window. Pin and plot requests are **forwarded, not handled**: the window
raises `pinFieldRequested` / `plotFieldRequested` and `MainWindow` opens the
dock, which is how every other window here does it.

`MainWindow::pinField` was split so the narrowing can be supplied rather than
read off its own inspector. A field pinned from a compare pane now lands
narrowed to that pane's source **and** that frame's captype — identical to
pinning from a log tab, rather than the compare window growing its own half
of the logic.

## Bookmarks, on the same store

`Ctrl+B`, `F2`, `Shift+F2`, plus a row-menu entry that reads "Add" or
"Remove" to match the row.

The `BookmarkStore` is **borrowed from MainWindow, not duplicated**: a
bookmark on a frame is the same bookmark whichever window set it, and two
stores would mean a mark visible in one and not the other. Stepping uses the
shared `nextBookmarkedRow`, so the log tabs, the session window and these
panes cannot drift apart.

With no store supplied the action is **disabled**, rather than present and
quietly doing nothing.

## Two more row-menu verbs

- **Load into Packet Maker** — forwarded to `MainWindow` as a buffer.
- **Why this colour?** — the affordance that made the shipped
  `colour_rules.json` bug findable, where decorative rules pinned severity to
  info so real errors rendered as info with nothing on screen saying which
  rule did it. Disabled when no rule set was supplied.

## A test that was passing by luck

The patch-44 compare tests picked their view with
`findChildren<QTableView*>().first()`. That is not pane 0 — the window now
also contains the decoded-fields table, and widget-tree order is not pane
order. The old checks passed anyway because the fixture's two sources produce
the same message text, so copying "the wrong pane" looked correct.

`paneCount()` and `paneView(int)` were added and the suite now addresses panes
by the same index `buildRowMenu` takes. This was found by a new check failing
for the right reason, not by reading.

## Deliberately not done

Per the confirmed scope: no filter bar, marker scrollbar or timeline ribbon —
all three need a `QSortFilterProxyModel` under each pane, which rewrites the
nine direct `qobject_cast<LogModel*>` sites. The shared helpers already take
`(proxy, source)` pairs, so that route stays open. No follow-tail. No export.

Also still absent, as before: "Filter to this source" (a pane already is one
source), detach console (a pane is already a detached view), tab-health
colouring (panes have no tab title).

## Files touched

    logtableview.{h,cpp}           NEW — shared table setup
    comparewindow.{h,cpp}          fields panel, bookmarks, menu verbs, paneView()
    mainwindow.{h,cpp}             forwards to LogTableView, pinFieldNarrowed,
                                   wires the compare window's raised signals
    DLConsole.pro, tests/*.pro     new files
    tests/test_comparetools.cpp    parity, selection, panels, bookmarks

## Verification

    111 suites, 2891 checks, 0 failed      (was 111 / 2875)
    menu audit passed, 24 shortcuts, 58 palette commands
    headless smoke run clean
