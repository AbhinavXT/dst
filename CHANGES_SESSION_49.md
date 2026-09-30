# Patch 49 — Decode Workbench got the envelope, not the frame

## The report

Opening a non-capture row in the Decode Workbench handed it the wrong bytes.
The row read

    2026-09-09T01:55:04.269  21_2  INFO  02 07 0D 00 27 00 00 00 00 00 D3 AD …

and what arrived at the workbench began

    15 65 97 76 00 02 00 30 32 20 30 37 20 30 44 …

## What those bytes actually were

Not the datetime, as it looked. Two things stuck together:

- `15 65 97 76 00 02 00` — the transport header.
- `30 32 20 30 37 20 30 44` — the ASCII **codes** of the characters
  `0`, `2`, `space`, `0`, `7`, `space`, `0`, `D`. The frame written out as
  text, then hex-encoded a second time. `20 0A` on the end is the trailing
  space and newline.

So the workbench was decoding a description of the frame rather than the
frame.

## Cause

`MessageDispatcher::buildEntry` keeps `rawBytes` as the datagram verbatim:
transport header followed by payload. `entryBufferText` preferred `rawBytes`
for every row that is not an `@`-capture line.

That is correct when the payload is binary — then `rawBytes` is the only
frame there is, and its header belongs with it. It is wrong when the payload
is a frame the backend already printed as text, because then there are two
candidates and it picked the wrapper.

## Fix

A message that already IS a hex dump now wins over `rawBytes`. Order:

1. `@`-capture line → the line, as before.
2. message that parses as a hex dump → **the message**.
3. otherwise → `rawBytes` as hex, as before.

The test for (2) is deliberately strict, because a false positive is silent:
the workbench would decode the wrong bytes with nothing on screen saying so.
Every whitespace-separated token must be exactly two hex digits, and there
must be at least eight of them — `DE AD` inside a sentence is not a packet,
and no frame worth decoding is two bytes long. A ragged token disqualifies
the whole message.

## Tests

New suite `printedframe`, built on the reported row and its datagram. It
checks the header is gone, that `30 32 20 30 37` is gone with it, and then
runs the result through `PacketMakerDialog::parseBuffer` — the point is not
that the string looks better but that it decodes, so the check is the
composition rather than the halves.

Also covered: a prose message still yields `rawBytes` header and all; a
sentence containing `DE AD` is not mistaken for a frame; seven bytes is below
the bar; a ragged token disqualifies; and an `@`-capture line still wins over
everything.

## Note

Two of my first expectations in this suite were wrong again — the reported
frame is 39 bytes, not the 38 I counted. Worth stating plainly: the code was
right both times and the test was wrong, which is the good direction for that
error to run, but it is twice in two patches that I have miscounted by hand
what the machine could count.

## Files touched

    logentry.cpp                looksLikeHexDump, entryBufferText ordering
    tests/test_openbuffer.cpp   printedframe suite

Every caller benefits: Decode Workbench, Packet Maker, Frame Diff, the
compare row menu and the session window all go through `entryBufferText`.

## Verification

    116 suites, 2955 checks, 0 failed      (was 115 / 2944)
    menu audit passed
    headless smoke run clean
