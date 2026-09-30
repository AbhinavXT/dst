# Session 32 — the ARP a loco receives from another loco

| File | Change |
|---|---|
| `capturedecoder.{h,cpp}` | header size read from the frame; `CapType::ArpRecv`; CRC by packet bounds |
| `schema/schemadecoder.{h,cpp}` | `captypeTokens()` — one packet may answer to several tokens |
| `schema/schemaencoder.cpp` | packet lookup and `packetNames()` use it |
| `schema/kavach.xml` | `match="captype==arp,arprecv"` |
| `messageheader.cpp` | `arprecv` carries message id 13, like `arp` |
| `lococonsolewindow.cpp` | tab and link-overview row |
| `replaywindow.cpp` | tab; explicitly NOT a source for the loco box |
| `decodeworkbench.cpp`, `framediffwindow.cpp` | offered in the type list, CRC-verifiable |
| `fieldmap.json` | named in the field catalogue |
| `tests/test_arprecv.cpp` | new: 2 suites, 46 checks |
| `tests/test_assertions.cpp`, `test_fieldquery.cpp`, `test_fieldcatalog.cpp` | fixtures made self-consistent |

From a real receive buffer:

```
07 02 0D 00 25 00 00 00 D3 AC 18 10 00 02 20 00 00 06 E0 00 02 00 00 00
00 00 00 00 00 00 08 32 00 EB F8 71 0B 00 00 00 00 00
```

---

# The header is not a fixed size, and the frame says which it is

What a loco sends carries a 2-byte station id on the end of the message
header. What it receives from another loco does not, so the same packet
starts two bytes earlier:

```
sent  02 07 0d 00 27 00 00 00 00 00  D3 AC ...   header 10, frame 39
recv  07 02 0d 00 25 00 00 00        D3 AC ...   header  8, frame 37
```

The decoder took 10 as a constant. Fed a received frame it decoded two bytes
late and reported `PKT_TYPE 1`, `PKT_LENGTH 64`, `SOURCE_LOCO_ID 139264` — a
complete set of plausible values, every one wrong, and no error anywhere.
That is the failure mode this codebase keeps finding, and it is why the fix
is not "add 8 as a second constant".

**The frame states its own shape twice**, and the two statements have to
agree:

| | where | what it counts |
|---|---|---|
| `message_len` | LE, bytes 4..5 | the whole frame, header included |
| `PKT_LENGTH` | bits 4..10 of the packet | the packet |

so `header = message_len - PKT_LENGTH`. Reading `PKT_LENGTH` at a candidate
header size and checking the sum costs nothing and is a self-check. 10 is
tried first, so a frame that could somehow satisfy both keeps the meaning it
has always had.

A frame that satisfies neither is **declined**: the field table says the
lengths disagree rather than showing values from a guessed offset.

## What the CRC was checking

Two assumptions had to go, and the received buffer breaks both at once:

- the span started at byte **10** — wrong by two for this frame
- the stored word was **the last four bytes of what arrived** — and a receive
  buffer has padding after the CRC, so those are zeroes

Both ends now come from the packet: `jamcrc` over `[hdr, hdr+PKT_LENGTH-4)`
against the big-endian word at the end of the packet. The buffer above passes.

A frame that disagrees with its own lengths still gets a CRC verdict from the
old span. That asymmetry is deliberate: a damaged frame is worth reporting as
**CRC FAIL**, which is the verdict an operator acts on, and a guessed span
landing on PASS is vanishingly unlikely — whereas a table of field values from
a guessed offset is all plausible and all wrong.

## Re-verified, not assumed

Every ARP and LSRP frame in `replay/`: **9207 + 3253 = 12,460**, all resolve to
header 10 and give the identical CRC verdict under the new rule and the old.
The received buffer resolves to 8, and cannot verify at 10 — its bytes 8..9 are
`D3 AC`, so unlike the 1840 frames with zeroes there, it discriminates. (The
CRC init is 0, which is why zero-prefixed frames pass at either offset and are
no evidence for one.)

---

# @arprecv

Decoding never needed the token — the length fields settle it — but the token
is what lets the two be told apart on sight. One line is what this loco said;
the other is what another loco said, and on the wire they are otherwise
identical. `arp_recv` and `rarp` route to the same decoder, as the auth-key
tokens taught.

Direction is **In**, always: this packet is never something this loco sent.

| Surface | What it got |
|---|---|
| Live loco console | its own tab, and a row in the link overview |
| Recorded playback | its own tab |
| Decode Workbench | in the type list, and marked CRC-verifiable so auto-detect can use it |
| Frame diff | in the type list |
| Packet Maker | offered in the packet combo, built exactly as `arp` |

## One packet, two names

The Packet Maker builds from `Schema::Encoder::packetNames()`, so offering
`arprecv` meant the schema had to know the name. The obvious way — a second
`<packet>` element — would have been two copies of twenty-one field
definitions that must stay identical forever. They would not; that is what
the LSRP fault-field session was about.

So `match=` now takes a comma-separated list:

```xml
<packet name="ARP" match="captype==arp,arprecv" ...>
```

`captypeTokens()` parses it, and both halves of the schema use it — the
decoder to select a packet, the encoder to list and look one up. Anything
genuinely a different packet still gets its own element. There is a test that
`arp` and `arprecv` resolve to the same field list, field for field, name and
width, because the whole point is that they cannot drift.

## What deliberately did NOT change

**The track view's loco box.** An `arprecv` carries a position, but it is
another train's, and the box on the track view is this key's own loco. Wired
in by accident it would make one loco appear to jump between two places. The
bit offset would be wrong too — an `arprecv` body starts at bit 64, not 80, so
the hard-coded 131 for `ABS_LOCO_LOC` does not apply to it. Plotting
neighbouring trains is a feature worth having; it is not this one.

**Frame-number watching.** `arprecv` carries `FRAME_NUM` in the same place,
but it is another loco's counter. Watching it alongside this loco's would
compare two unrelated sequences and report a gap whenever either moved.

---

# Three fixtures that were never realistic

`test_assertions`, `test_fieldquery` and `test_fieldcatalog` built synthetic
LSRP frames as 64 zero bytes with the body written at bit 80 — both length
fields left at zero. They decoded only because the offset was assumed. Once
the decoder reads the frame's own lengths, they stopped, and 31 checks failed.

The fixtures were fixed, not the decoder loosened. A test frame that no loco
would send is a test of something no loco does.

---

# Tests

| Suite | Checks | What it pins |
|---|---|---|
| `arprecv` | 32 | the real buffer: envelope, every body field, CRC, and the decline path |
| `arprecvtoken` | 14 | the token, its spellings, direction, and one packet under two names |

The sharpest check flips **only the low five bits of byte 9** — `FRAME_NUM`
bits, inside the packet on the received form and inside the header on the sent
one, and part of no length field either way. It is the single change that
distinguishes a span starting at 8 from one starting at 10. Flipping whole
bytes cannot do it: bytes 8 and the top of 9 carry `PKT_LENGTH`, so corrupting
them makes the frame disagree with itself and it is declined before any CRC is
attempted — which is its own check.

Full suite: **99 suites, 2563 checks, 0 failed.** Menu audit passed. Headless
smoke run clean.

---

# Open

- **The Packet Maker builds `arprecv` byte-identical to `arp`**, because
  DLConsole prepends an 8-byte header to everything it sends — it is a test
  host, not a loco, and never adds the station id. So today the two entries
  differ only in what the frame is called. If a built `arprecv` should carry
  the received shape and a built `arp` the 10-byte transmitted one, that is a
  send-format decision and needs saying.
- **LSRP** shares this envelope and got the same treatment, but no received
  LSRP has been seen. If that direction differs, it will be declined rather
  than misread — which is the safe way to be wrong, but it will need a buffer.
- **Neighbouring trains on the track view**, from `arprecv` positions. The
  data is now decoded and sitting there.
