# Session 64 — Extra header fields in the Packet Maker

Operator-chosen fields can now be placed **after the 8-byte message header and
before the packet** — a `uint16_t station_id`, a `uint32_t loco_id`, or any
other value the receiver expects in its envelope. Tick the row and it goes out;
untick it and the datagram is exactly what it was before.

```
[ 8-byte header ][ extra 0 ][ extra 1 ] ... [ packet (body + MAC + CRC) ]
```

## The panel

"Extra header fields (after header, before packet)", under the Message header
box. Folded by default like the other occasional panels, but its **title always
states what is enabled** — `— none`, `— +2 B: station_id=1234 (0x04d2, u16 LE)`,
or `— INVALID: …` — so a field can never be going out from behind a closed box.

| Column     | Meaning                                                   |
|------------|-----------------------------------------------------------|
| Send       | only ticked rows go on the wire                           |
| Name       | free text, used in previews and error messages            |
| Type       | `uint8_t` / `uint16_t` / `uint32_t`                       |
| Value      | decimal or `0x`-hex, unsigned                             |
| Byte order | little-endian (default, matches the header) / big-endian  |

Starts with two rows, both unticked: `station_id` (uint16) and `loco_id`
(uint32). Add field / Remove field for anything else. Rows go out in table
order.

## Wire rules

- **`message_length` counts the extras.** That is how the header describes its
  own size (`message_length − PKT_LENGTH`); telling a receiver the header is 8
  bytes when it is 10 would make it read the station id as packet. Ticking a
  uint16 `station_id` on arp therefore produces the 10-byte form a loco's own
  radio emits.
- **Not covered by CRC or MAC** — envelope only, same as the header.
- **Unticked rows are skipped**, never zero-filled.

## Refuse, don't guess

- A value that does not fit its type (70000 in a uint16), a negative, or junk
  **fails Build**, naming the field. Nothing is truncated.
- Send Once and Start Interval check again before sending. An invalid envelope
  is empty by construction (`buildWithExtras`), and every caller treats empty as
  "do not send" — never as "send the packet bare".
- An unticked row with a bad value blocks nothing: it is not going out.
- **Editing the table after a Build disables Send** until the next Build, same
  as any other edit: what the preview showed is what Send would have sent.
- **Locked while an interval run is active.** The run keeps the envelope it
  started with (snapshotted with src/dst), and a locked table says so instead
  of letting an edit look as if it took effect.

## Preview

Shows the extra bytes on their own line, what each decodes as, and the total
envelope size. The "RECEIVED form, 8-byte header" note for arp/lsrp is shown
only when no extras are enabled — with extras the operator has chosen another
shape and the note would be wrong.

## Presets and the sequence runner

Presets save the rows under `msg_extras` (name, bytes, value, endian, enabled).
A preset written before this session has no such key and loads with the
default rows unticked — the same bytes it always produced.

The sequence runner applies a preset's extras too (one format, one behaviour),
and **fails the step** rather than sending if they do not validate.

## Code

- `messageheader.{h,cpp}`: `Extra`, `defaultExtras`, `parseExtraValue`,
  `validateExtras`, `encodeExtras`, `extrasSize`, `buildWithExtras`,
  `describeExtras`, JSON helpers. Pure functions; the dialog holds no encoding
  logic of its own. `build()` is unchanged.
- `packetmakerdialog.{h,cpp}`: panel, read/write, validation at Build and at
  send, preview, preset hooks, lock during runs.
- `packetpreset.{h,cpp}`: `extras` field.
- `packetsequencedialog.cpp`: uses `buildWithExtras`; refuses invalid extras.

## Tests

New suite `headerextras`, 64 checks:

- defaults unticked and byte-identical to the plain header
- value parsing: decimal/hex, every width's max and max+1, negative, junk,
  empty, unsupported width
- exact bytes: `07 02 09 00 2b 00 02 01 | 02 01` for slrp + station_id,
  both rows, reordered rows, BE per row, uint8, skipped middle row, arp 10-byte
  form, no-header types
- invalid enabled rows block, invalid disabled rows do not
- preset round-trip (uint32 max included) and pre-session-64 presets
- dialog: panel present and folded, title states enabled fields and INVALID
- **end to end over loopback UDP**: Build → Arm → Send Once, read the real
  datagram: message_id, message_length = whole datagram, extras right after
  the header; untick loco_id → Send disabled until rebuild → 4 bytes fewer,
  length follows, packet bytes identical; out-of-range value → Build fails,
  Send stays dead

## Harness fix

`tests/menuaudit.pro` was missing `brakingcurves`, `brakingcurveplot` and
`brakingpanel` (added to the app in sessions 62–63) and did not link, so the
menu audit had not actually been running since then. Added.

## Verified

- Unit suite: **121 suites, 3095 checks, 1 failed** — see pre-existing below.
- Menu audit: builds and runs again; **1 failure**, pre-existing, see below.
  Packet Maker reachable.
- Headless smoke: app built from `DLConsole.pro`, run offscreen for 12 s under
  200 `@lsrp` datagrams on 50002; no crash, clean timeout exit.
- Panel rendered in Ayu Light and Ayu Dark; Send uses real checkboxes because an
  unchecked item indicator is invisible in the dark theme.

## Pre-existing failures (not from this patch, not fixed here)

1. **contrastaudit** — six `QColor` literals in `brakingcurveplot.cpp:21–26`
   (the segment colour cycle), outside `uicolors`/`theme`/`uistyle`.
2. **Menu audit: Ctrl+Shift+B is bound twice** — `Tools > Monitor > Braking
   Curves…` (`mainwindow.cpp:446`) and `Tools > Inspect > Selected row → Decode
   Workbench`. An ambiguous shortcut in Qt fires neither action. Hidden until
   now because the audit did not link.

Both are small; left for a decision rather than folded into this patch.
