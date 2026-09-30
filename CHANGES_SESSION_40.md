# Session 40 — the equipment clock

| File | Change |
|---|---|
| `frameclock.{h,cpp}` | new: FRAME_NUM ↔ time of day, skew, and the accept window |
| `mainwindow.cpp` | a status-bar readout, updated per frame and once a second |
| `tests/test_frameclock.cpp` | new: 29 checks |
| `tests/menuaudit.pro` | the new source, which it had been linking without |

---

# The relationship, confirmed rather than assumed

`FRAME_NUM = seconds since midnight + 1`. Checked against the captures before
anything was built on it — 15,800 ARP, LSRP and SLRP frames in `replay/`,
comparing FRAME_NUM against the seconds-since-midnight of each capture
timestamp:

| difference | frames |
|---|---|
| **+1** | **14,839** |
| +0 | 527 |
| +2 | 433 |
| +3 | 1 |

The +1 is the relationship. The ±1 either side of it is the capture timestamp
being truncated to the second: a frame stamped at `.999` lands one second
early. Nothing sits anywhere else.

Re-run over all 12,460 ARP and LSRP frames after the conversion was written:
every one produces a valid time of day, and the largest gap between the
equipment's clock and the capturing laptop's is **0 s** — those runs were in
sync, which is what a healthy setup looks like.

---

# Why a readout and not just a conversion

The loco checks every packet's FRAME_NUM against its own clock and **rejects**
what falls outside a narrow window — more than 4 s old, or more than 2 s in
the future. The stationary end does the same to what the loco sends.

So when this laptop's clock drifts from the equipment's, everything DLConsole
transmits — Packet Maker, replay, the round-trip validator — starts being
discarded at the far end, and **the only symptom is silence**. Nothing on
screen would have said why. That is the gap this fills.

The status bar now carries:

```
Equipment 15:02:31 (+2s)
```

muted inside the window, amber at its edge, red outside it. The tooltip names
the packet the reading came from, its FRAME_NUM, how long ago it arrived, what
this laptop reads, and what the far end will do about the difference.

With nothing observed it says `Equipment clock: —` rather than showing this
laptop's own clock, which would read as agreement between two things that have
not been compared.

## The window is asymmetric, and the code says so

```c
if (TimeDiffInSeconds(pkt, own) <= -2 || TimeDiffInSeconds(pkt, own) > 4)
    reject
```

Four seconds late is accepted; two seconds early is not. A laptop running fast
is therefore rejected sooner than one running slow, and the boundaries are not
inclusive in the same direction — 4 still passes, −2 already fails. Both edges
are pinned in the tests, because an off-by-one here would mean the readout
says "fine" about traffic that is being dropped.

## Midnight

Skew takes the shorter way round the clock face, so 00:00:01 against 23:59:59
reads as +2 s rather than most of a day. Without that the readout would turn
red at exactly the moment it should be quietest.

A FRAME_NUM that is not a time of day at all — 0, or anything past 86400 — is
reported as unreadable rather than wrapped into yesterday.

---

# One thing found on the way

`tests/menuaudit.pro` had been listing sources by hand and did not know about
`frameclock.cpp`, so the audit stopped linking. It builds and passes again, and
the file is listed now.

Full suite: **105 suites, 2687 checks, 0 failed.** Menu audit passed. Headless
smoke run clean.

---

# Not done

- **The station's clock, separately.** The readout follows ARP and LSRP, which
  is the loco's counter. SLRP carries the stationary end's FRAME_NUM in its
  header and `FrameNumberWatch` deliberately ignores it, because that watch
  exists to answer "what number should the Packet Maker build with" and mixing
  two counters into one answer would be wrong. If "controller clock" means the
  wayside's rather than the loco's, that is a second tracked value and a
  second readout — say so and it is a small change.
- **Skew history.** A drift of a second an hour is invisible in a live readout
  and obvious in a plot. The field plot already exists and could take it.

---

# Both clocks

Asked for after the first cut: show the station's frame number as well as the
loco's.

The status bar now carries three readings:

```
Loco 15:02:31 (+1s)    Stn 15:02:30 (0s)    +1s apart
```

Each side is painted by the same function, so the two are presented
identically — the reason for having both is the moment they stop agreeing, and
that is only visible if they look the same when they do agree.

## The third number is the one that matters

`+1s apart` is the loco's counter minus the station's, and it is the
difference the two ends actually check each other on. Each end's gap to *this
laptop* is a diagnostic for DLConsole's own transmissions; the gap between the
two ends is a diagnostic for the system under test, and this laptop is not
part of that system at all.

It is spelled out rather than left to be worked out from the two times,
because subtracting two clock readings at a glance is exactly the sort of
thing that gets a test signed off wrongly. Midnight is wrapped the same way as
the skew, so 00:00:01 against 23:59:59 reads as 2 s apart.

## Kept apart on purpose

`FrameNumberWatch::latest()` still means the **loco's** counter — arp and
lsrp — and the station's lives in `latestStation()`. They are not merged, for
two reasons: the Packet Maker seeds built frames from `latest()`, and a
station number there would build a loco frame the loco rejects; and one merged
"latest" would hide the disagreement that is the whole point of showing both.

`latestFor(locoId)` likewise stays loco-only — an SLRP addressed *to* a loco
does not answer "what is that loco counting".

## A stale assumption in the tests

`test_framenumwatch` had SLRP standing in as its example of "a packet without
a frame number". It never was one: SLRP carries the station's counter in its
header, it simply was not being watched. The check now uses an RFID frame,
which genuinely has none, and asserts that SLRP *does* announce.

Full suite: **105 suites, 2697 checks, 0 failed.** Menu audit passed. Headless
smoke run clean.
