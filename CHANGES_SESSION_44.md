# Patch 44

Three things asked for: a pin chooser nested by packet, a right-click menu in
the compare panes, and Ctrl+C that copies the message and nothing else. Two
bugs found on the way, both in the paths being touched.

---

## 1. The pin chooser nests by packet

**Before.** One flat combo holding 509 field names in a single alphabetical
run. Thirty-five of those names occur in more than one packet — FRAME_NUM is
in five — so the list could not even say which one it was offering.

**Now.** A box to type in, and beside it a **Browse** button whose menu is
nested:

    Seen in this tab (24) ▸
    ─────────────────────
    AAP ▸
    ARP (arp) ▸
    ARP (arprecv) ▸
    ...
    LINFO ▸  →  A – C ▸
                D – H ▸
                ...
    LOCO_SOS (lsos) ▸
    SLRP ▸

Details worth knowing:

- **What this tab has carried comes first**, flat, in its own group. It is a
  few dozen names out of five hundred and nearly always the one wanted.
- **Long groups get a second level.** LINFO has 167 fields; a submenu that
  long scrolls, and a scrolling menu of 167 names is the flat list again with
  extra steps. Above 40 fields it buckets alphabetically, never splitting a
  letter across two buckets.
- **A packet with no named fields says so.** NMSHLTH decodes entirely through
  a flag table. Its submenu holds one disabled line, "(no named fields)",
  rather than opening onto nothing and reading as a bug.
- **Picking a field sets the narrowing.** An operator who picks FRAME_NUM
  from under LSRP has already said which of the five they mean; leaving the
  narrowing on "any packet" after that would pin whichever packet arrived
  last. Picking from the observed-fields group changes nothing, because that
  pick says nothing about packets.
- **Typing still works and got better.** Completion now matches anywhere in a
  name, not just the start, over every name the schema knows.

### Why a menu and not a combo with a tree in it

That was the first plan and it was dropped. A `QComboBox` whose popup is a
`QTreeView` depends on Qt's private popup event filter to decide whether a
click on a group row closes the popup, and the answer differs by whether the
group rows are disabled or merely non-selectable — which also decides whether
the keyboard can land on one and whether clicking the label expands it. None
of that is reachable from a headless test, the target is Windows/Qt 5.15, and
bugs there arrive as photographs. A nested `QMenu` has no private behaviour
to reason about and every level of it is assertable in the suite.

### Bug found: the packet narrowing could never match, for two packets

The narrowing combo was filled from `Decoder::packetNames()` — packet
**names**. `PinBoard::observe` matches a pin against the capture line's
**captype token**. Case-insensitive comparison hid the difference for most
packets. Not for these:

| Narrowed to | Arrives as | Result before |
| --- | --- | --- |
| `LOCO_SOS` | `lsos` | never matched — the pin waited forever |
| `ARP` | `arp`, `arprecv` | matched sent ARP, silently ignored every ARP received from another loco |

Both the menu and the combo are now keyed on the token, labelled
`LOCO_SOS (lsos)` and `ARP (arprecv)` where name and token differ, so the
packet is still named and the token is still visible. Pinning from a
right-clicked row was already correct — it read the token off the entry — so
the combo was the odd one out.

**New schema API.** `Decoder::fieldsByCaptype()` returns one entry per
(packet, token) pair with that packet's fields. It follows
`<subpackets><case struct="…">` into the struct it names, which
`allFieldNames()` never had to do — it swept every struct regardless of owner.
Here it matters: the eight SLRP sub-packets carry most of the fields worth
pinning, and a walk stopping at the `<case>` element would offer an empty
SLRP. Cycle-guarded at the existing 32-level depth limit.

`allFieldNames()` is unchanged and still used elsewhere.

---

## 2. A right-click menu in the compare panes

The panes had no context menu and no copy of any kind, so the only way to get
a row out of this window was to find it again in its own tab — which is the
thing the window exists to avoid.

    Copy message                     Ctrl+C
    Copy row (tab-separated)         Ctrl+Alt+C
    Copy row with header
    Copy bytes
    ─────────────────────────────
    Open in Decode Workbench
    Diff the selected rows across panes

- Right-clicking a row **selects it first**, so every action acts on the row
  being pointed at rather than on whatever was selected before.
- **Copy bytes** gives the capture line, which is the input format the Decode
  Workbench and Packet Maker take.
- Actions that cannot work say why: a row carrying no bytes disables the two
  that need them; "diff across panes" is disabled until a second pane has a
  selection.
- The same two copies are in the window's Edit menu, on the same keys.

The panes remain single-selection. Making them extended would touch the
time-lock path, so "copy N messages" there is always one row. Say the word if
you want multi-row selection in the panes.

---

## 3. Ctrl+C copies the message

`Ctrl+C` used to paste `Time⇥Source⇥Direction⇥Severity⇥Message`. That is the
right thing for a report and the wrong thing for everything else: a capture
line pasted into the Decode Workbench, into a mail, or back into this program
had four columns to strip off by hand, every time.

| Key | Copies |
| --- | --- |
| `Ctrl+C` | the message text, one line per row |
| `Ctrl+Alt+C` | time, source, direction, severity, message — tab-separated |

Both are in the Edit menu and in both right-click menus, in the main window
and the compare window, on the same keys in each. "Copy rows with header" is
unchanged.

**Not Ctrl+Shift+C**, which was the obvious choice and is already
Tools ▸ Monitor ▸ Compare tabs. The menu audit caught the clash; Qt would have
reported an ambiguous overload and fired neither action.

---

## Bug found: "Pin this field" and "Plot this field" were never connected

`m_fieldPanel` is constructed at `mainwindow.cpp:775`. Both connects were at
716–718, fifty lines earlier, where it was still null:

    connect(m_fieldPanel, &FieldInspector::pinFieldRequested, …);   // nullptr
    connect(m_fieldPanel, &FieldInspector::plotFieldRequested, …);  // nullptr

Qt refuses a connect with a null sender and says so on stderr —
`QObject::connect(FieldInspector, MainWindow): invalid nullptr parameter` —
which nobody reads in a release build. The effect: right-clicking a decoded
field and choosing **Pin this field** or **Plot this field** did nothing at
all, for the whole life of the program.

Both connects moved to immediately after the inspector is constructed. The
inspector's other two connects were already there and always worked, which is
why byte-range highlighting and decode-failure reporting were fine.

This was found by reading the smoke-run output rather than by a test, and it
is the pin path, which is why it is fixed here rather than deferred. The
regression check is the smoke run itself: if either connect moves back above
the construction, the warning returns.

---

## Files touched

    schema/schemadecoder.{h,cpp}   fieldsByCaptype()
    pinpanel.{h,cpp}               nested browse menu, captype-keyed narrowing
    comparewindow.{h,cpp}          row menu, copy actions, buildRowMenu()
    logentry.{h,cpp}               formatMessagesForClipboard()
    mainwindow.{h,cpp}             Ctrl+C / Ctrl+Alt+C, connect fix, chooser feed
    tests/test_pinboard.cpp        pinchoices rewritten for the nested shape
    tests/test_comparetools.cpp    new comparerowmenu suite
    tests/test_clipboard.cpp       messages-only copy

## Verification

    111 suites, 2875 checks, 0 failed      (was 110 / 2822)
    menu audit passed, 24 shortcuts, 58 palette commands
    headless smoke run clean — and now free of the connect warning above

New coverage: the captype split and its two previously-unreachable packets;
sub-packet fields reaching the chooser; bucket boundaries losing and
duplicating nothing; picking a field setting the narrowing; the compare row
menu's contents, shortcuts and actual clipboard output; messages-only copy
including tab and newline flattening.

## Not done

- The compare panes are still single-selection (see above).
- Session and merged windows still have no copy action. They had none before;
  say the word and they get the same two keys.
