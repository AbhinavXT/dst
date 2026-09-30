# Patch 53 — which loco is "ours"

The last piece the reject rules were waiting on: an SLRP addressed to another
loco is one the onboard would not process, and there was no way to say so
without knowing which loco this console is watching.

## Learned, not configured

A configured ID is one more thing to set before a run and one more thing to
have wrong. ARP and LSRP carry `SOURCE_LOCO_ID`, so a console that has seen
either has already been told.

`LocoIdentity` tracks it **per source**, because two tabs are two locos rather
than one contradiction.

### `arprecv` is deliberately not learned from

`arp` is what our loco sends; `arprecv` is another loco's ARP arriving at
ours, and its `SOURCE_LOCO_ID` is the other loco. Learning from it would teach
the console the wrong identity on every loco-to-loco approach — precisely when
these rules matter most. There is a test for it, including that a later
`arprecv` cannot unsettle an identity already learned.

Loco id 0 is the unassigned value and is not recorded, or a source carrying
only unassigned frames would look identified.

## Three states, and the two that are not "known"

    Known      one loco seen. Rules may fire.
    Unknown    nothing seen yet.
    Ambiguous  more than one loco on one source — usually a station-side
               capture, or two runs merged into one tab.

The dangerous state was never "unknown". It is **unknown and reporting
anyway**: a console reading an absent identity as zero would flag every frame
as addressed elsewhere, and an operator would learn to ignore the feature
within a minute. So the two unknown states supply nothing at all, and the
console says which one it is in rather than staying quiet — silence reads
exactly like "checked, and fine".

## How it reaches the rules

The learned ID travels **beside** the decoded fields as `OWN_LOCO_ID`, and the
rule is a plain field-to-field comparison:

    <rule clause="32.14.1" field="DEST_LOCO_ID" op="ne_field"
          other="OWN_LOCO_ID" note="addressed to a different loco"/>

The engine needs no notion of identity at all, and needs no "unknown" concept
either: when the loco is not identified the key is simply absent, and
`ne_field` does not fire on an absent comparand. Absence does the work that
would otherwise need a special case.

The finding names what it compared against — `DEST_LOCO_ID = 4712, not
OWN_LOCO_ID` — so the claim can be checked rather than taken on trust.

## Shared across windows

One tracker on `MainWindow`, borrowed by the log tabs' inspector and the
compare window's, for the same reason the bookmark store is shared: a loco
identified in one window is the same loco in the other, and two trackers would
disagree.

The compare window forwards it in `setLocoIdentity()` rather than at
construction — the owner supplies it after the window is built, so a
constructor-time hand-off would always pass null and the panel would silently
learn nothing.

## A limitation worth stating

The console learns from frames it **decodes**, and it decodes on selection,
not on arrival — that is a deliberate existing design decision, not an
oversight. So the identity is learned once you have looked at an ARP or LSRP
on that source. Until then the rules say they are not checking.

Learning at ingest would need decoding in the arrival path, which the codebase
explicitly avoids. A cheap middle option exists — decode only ARP/LSRP, only
until an identity settles, throttled — and is worth doing if the current
behaviour proves annoying in practice.

## Files touched

    locoidentity.{h,cpp}         NEW
    rejectrules.{h,cpp}          ne_field operator
    schema/rejectrules.xml       the DEST_LOCO_ID rule
    fieldinspector.{h,cpp}       observes, then merges the context
    mainwindow.{h,cpp}           owns the tracker, shares it
    comparewindow.{h,cpp}        borrows it
    tests/test_locoidentity.cpp  NEW

## Verification

    119 suites, 3022 checks, 0 failed      (was 118 / 3000)
    menu audit passed
    headless smoke run clean
