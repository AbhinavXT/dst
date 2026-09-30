# Session 42 — what the loco will not act on

| File | Change |
|---|---|
| `capturedecoder.cpp` | `locoWillIgnore()` — three notes added to an SLRP decode |
| `tests/test_locowillignore.cpp` | new: 14 checks |

The first of the features that came out of reviewing `fly.c`. A frame can
decode perfectly and still have most of its content thrown away at the far
end, and none of that is visible in a field table: an LC entry with id 0 and
one with id 7 look equally real, and a TSR list looks the same whether the
loco reads it or not.

---

# The three notes

| when | note |
|---|---|
| `TSR_STATUS != 2` | the TSR entries are decoded, displayed, and ignored — `ProcessTSR` walks the list only at 2 |
| LC slots with id 0, TC slots of type 0 | placeholders the loco consumes and skips, counted rather than listed |
| `REF_PROF_ID == 0` | the station does not know the route ahead, so the loco **keeps** the profile it holds |

That last one is the one worth having. `REF_PROF_ID 0` is not a missing value
— it is a deliberate instruction, and a packet that changes nothing otherwise
looks like a packet that did nothing. It is 26% of the captured SLRP traffic,
so it is a state an operator meets regularly and has no way to read.

The placeholders are **counted, not listed**: on a packet with eight LC slots
and one gate in them, seven separate notes would bury the one line worth
reading.

## Notes are additions

The fields stay exactly as they were. What is on the wire is what an
acceptance test reports on, so a note that quietly replaced the decode would
be a bad trade — there is a check for that.

## And nothing fires when there is nothing to say

`TSR_STATUS == 2` produces no note, because the entries are read and a line on
every packet is noise rather than information. A real `REF_PROF_ID` likewise.
An ARP gets none of it at all.

---

# What is deliberately NOT noted

The loco also drops any SSP, gradient or TSR entry whose absolute location
computes to **zero or less**. That would be a useful note and it cannot be
made honestly: the calculation needs the reference tag's absolute location,
which lives in the loco's tag queue, not in anything on the wire. A note
derived from a guessed tag table would be confidently wrong every time the
table differed, which is worse than saying nothing.

The reasons that survived are the ones judgeable from the frame alone.

---

# A small lesson from the test

The first version of the test's helper matched note rows by the prefix
`"loco "`. It also caught the ARP's own `loco` field, and reported a note on a
packet that has none — a test failure caused entirely by the test.

Matching the two exact labels fixed it, and the reason is written next to the
helper: a prefix match over a field-name space you do not control will one day
catch a field somebody else added.

Full suite: **109 suites, 2761 checks, 0 failed.** Menu audit passed. Headless
smoke run clean.

---

# Pinning a field from any packet

Asked for: pin anything from the capture packets and watch its value.

Most of it was already there — the chooser lists what the current tab has
carried, then **every field the schema knows**, across all packets and structs,
509 of them. The second group is the useful one: the field worth watching is
often one that has not arrived yet.

What was missing is the reason that list is dangerous.

## The same name in five packets

`FRAME_NUM` is defined in **arp, arprecv, lsrp, slrp, aap and aep**. So is
`PKT_TYPE`, and `PKT_LENGTH`. Thirty-five of the 509 names appear in more than
one packet — `TIN` in three, `train_length` in three, `SOURCE_STN_ID` in two.

A pin matched on the name alone therefore shows **whichever packet arrived
last**. For `FRAME_NUM` that means alternating between the loco's counter and
the station's — two different clocks, one row, changing every frame for no
reason the operator can see. Narrowing by source does not help: both come from
the same source.

So a pin now narrows by **packet** as well as by source:

```
FRAME_NUM · slrp · 33_1        the station's counter
FRAME_NUM · arp  · 33_1        the loco's
```

Both can sit on the board at once, which is the point — they are the two
numbers session 40 put in the status bar, now watchable side by side with
their previous values and change times.

The label shows whatever a pin is narrowed to, because two pins on one field
narrowed differently are the normal case and a label showing only the field
would make them look like a duplicate.

## Pinning from a row narrows to what you clicked

Right-clicking a field in the inspector now pins it to that frame's **packet
and** source. The operator is pointing at a field in one packet of one source;
a pin that then took the same field name from a different packet would be
answering a question they did not ask.

## Old pins still load

Pins are stored as `field · source · packet`. A two-part line from the
previous build loads as "any packet", which is exactly what it meant when it
was written.

## Tests

Nine more checks: that two pins on `FRAME_NUM` narrowed to different packets
are two pins and not a duplicate, that each sees only its own packet, and that
their values differ — which is the whole reason for separating them. Plus the
unnarrowed behaviour, pinned deliberately so it is a choice on record rather
than a surprise, and the two-part round trip.

Full suite: **109 suites, 2770 checks, 0 failed.** Menu audit passed. Headless
smoke run clean.
