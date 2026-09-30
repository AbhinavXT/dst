# DL Console — session changes (patch 27)

Captured key material is now kept as a set of immutable snapshots, and both
frame tools let the operator pick which one to use.

| File | Change |
|---|---|
| `sessionkeystore.{h,cpp}` | `KeySnapshot`; snapshot on every change; `verifyMacWith()` |
| `capturedecoder.{h,cpp}` | `describe()` takes a key-set id; the MAC row names the set used |
| `decodeworkbench.{h,cpp}` | session-key picker |
| `packetmakerdialog.{h,cpp}` | captured-key picker that fills the key field |
| `tests/test_keysnapshots.cpp` | new suite (33 checks) |
| `tests/test_keypickers.cpp` | new suite (12 checks) — the two windows, end to end |

## Sets

The live state still moves — last write wins, unchanged. On top of it, every
complete combination of

```
{ auth key set 1, auth key set 2, loco random, stn random, loco id, stn id }
```

is frozen into a `KeySnapshot` with the session key it derives to, the log time
it appeared, and what changed to produce it ("randoms", "stn id", "key set 1",
"ids set by operator"). Change any input and the next one is created.

Two rules that shape the list:

- **Only complete combinations become sets.** A half-filled state — one key set
  and no randoms — is not something you can sign or verify with, so it stays in
  the store's private per-loco state and out of the pickers.
- **A recurring combination reuses its set.** A loco moving 527 → 501 → 527
  returns to the set it already had. Without this, a handover-heavy log would
  bury the picker in duplicates of two real sets.

Bounded at 128, oldest dropped. Ids are never reused, so a stale id left in a
picker resolves to nothing rather than to the wrong key.

## Decode Workbench

A **Session key** combo next to the type picker: *Auto (this frame's loco)* —
the log view's behaviour — or any captured set by name. Choosing one re-decodes
against that set, so a frame from earlier in the log can be checked against the
material that was in force when it was sent, which is the reason to paste an old
frame in there at all.

The MAC row now names the set behind the verdict (`PASS ✓  [key set #3]`).
On a log with several sets, "FAIL" without saying what it was checked against
is not a useful thing to have been told.

## Packet Maker

A **from log** combo beside the session-key field. Choosing a set copies its key
into that field. Deliberately a copy rather than a second source of truth: the
built frame is still signed with whatever the field holds, so a captured key can
be taken and then edited, and the builder keeps one input rather than two.

## Tests

`test_keysnapshots` covers the store: incomplete material produces nothing, each
change produces a set, older sets keep their keys, a recurring combination adds
none, and a frame signed under set #1 passes against #1 and fails against #2 —
through `describe()` as well as directly.

`test_keypickers` drives the two windows headlessly, because a store-level test
proves the sets exist but not that either window offers them or uses the choice.
Both windows subscribe to `changed()`, which fires on every observed frame, so
the case that matters is a new set arriving while a choice is selected: the combo
must grow without resetting the operator's selection out from under them.

Full suite: **56 suites, 1730 checks, 0 failed.**
