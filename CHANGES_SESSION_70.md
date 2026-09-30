# Session 70 — Tools ▸ Loco Configuration…

`loco_config_v12.cpp` as a DLConsole window. You edit a loco's `LOCO_INFO`,
send it to the VCC, and export `loco_info.bin`. The window sits beside the
Firmware Flasher, and like it is single-instance.

## Where it lives

- **Menu:** `Tools ▸ Loco Configuration…`, shortcut **Ctrl+Alt+L**.
- **Code:** new folder `lococonfig/`, pulled into the app, the tests and
  the menu audit through `lococonfig/lococonfig.pri`.
- **Data**, beside `dlconsole.ini`:
  - `loco_configs.json`: the saved configurations, written atomically.
  - `loco_config_history.jsonl`: every send, append-only, holding the exact
    bytes that went out.

## One layout, from the schema

DLConsole already decoded this struct from captures as `@linfo`, via
`<packet name="LINFO">` in `schema/kavach.xml` (370 bytes, same CRC recipe
as v12). The editor reads that **same element** for field order, sizes and
types. There is no second hand-written copy of the struct. When LOCO_INFO
changes, you update `kavach.xml` once and the decoder and the editor both
follow.

- The editor packs the schema's unsigned 8/16/32-bit integers, `float`,
  `char[count]` and section notes. Anything else in LINFO makes the window
  show why it can't load, rather than pack a struct of the wrong shape.
- The two field names that differ from v12 keep the schema's spelling:
  `init_state_machine_atf` and `brake_pressure_used`. The editor's notes
  give the v12 names.
- **Schema fix.** `braking_prints`, `brake_pressure_used` and
  `loco_info_crc` were filed under the `national_values` section, but in v12
  they are LOCO_INFO's own members after the nested structs. A closing
  `<note>` now says so. Both decoders already treat a note as a label row,
  so the `@linfo` view gains one separator row and nothing else changes.
  The golden validators and every decoder suite pass.

## The window

- **App bar.**
  - The Configuration picker holds one saved configuration per loco.
  - Manage: New from v12 defaults, Duplicate, Rename, Delete, Import
    `loco_info.bin`, Reset all to v12 defaults.
  - History.
- **Field table.** Columns are field, value, v12 default, last sent and
  type.
  - A value shows in bold where it differs from v12's default.
  - A ● marks a field that differs from the last send of this
    configuration.
  - Tooltips give the key, the type and byte offset, and a note taken only
    from v12's own comments. No units were invented.
  - Right-click reverts a field to the default or to the last-sent value.
- **Groups and search.** Groups: Loco & wheels, Tachos/slip/skid, Speeds &
  margins, Timeouts, Distances, LC horn, Radio, Network & IDs, DMI &
  buttons, Brake pressures, Train lengths, Hardware & system, Static
  braking, National values (UBA). Plus "Changed from defaults" and
  "Changed since last send". Each group shows how many of its fields
  differ. Search looks across all groups.
- **Editing is checked as you type.**
  - Integers accept decimal or `0x…` and must fit their type
    (u8/u16/u32).
  - NMS and KMS addresses are entered dotted (`127.0.0.1` is stored as
    2130706433, as v12 does).
  - Floats must be finite.
  - APN and description text must be printable ASCII, at most 39
    characters (room for the NUL).
  - A bad value never reaches the configuration; the status line says why.
- **Send bar.**
  - Target IP and port (default 50001).
  - Live summary: "375-byte message · src 28 → dest 2 · msg 120 ·
    loco_info_crc 0x…".
  - `vcc_crc` repeated where you press Send, since it's set by hand and
    must match the VCC build.
  - A plain statement that **the VCC does not reply**: DLConsole records
    what it sent, not whether the loco applied it.
  - Export `loco_info.bin…`, and Send to VCC….
- **Send.**
  - The confirmation lists every field changed since the last send of this
    configuration (old → new), or says it's the first send. It shows
    `vcc_crc` and `loco_info_crc`, and repeats that nothing will confirm
    delivery.
  - The datagram is exactly v12's: 5-byte header + 370-byte body with the
    CRC computed. It goes through `UdpSender`, DLConsole's one transmit
    path.
  - Afterwards the configuration remembers the exact bytes (so "changed
    since last send" survives a restart), and the history gets a record:
    who, when, which configuration, where, both CRCs, the body.
- **History.** A search box and a hex dump of any past send. **Export as
  loco_info.bin** rewrites that send byte-identically. **Load into current
  configuration** restores its values; since the VCC never reports what it
  holds, this is the way to undo.
- **Import `loco_info.bin`** accepts the 370-byte body or the 375-byte
  datagram. A CRC mismatch is reported (stored vs computed) and the file
  loads only if you say yes.
- Edits save by themselves, 0.4 s after the last change, and on close.

## Found in v12: the CRC isn't reproducible

`InitStaticBrakingValues()` fills an **uninitialised** local
`STATIC_BRAKING_CONFIG`, and `strcpy()` writes only the 18 bytes of
`"LOCO_TCAS_BRAKING\0"` into `train_desc[40]`. The remaining 22 bytes are
leftover stack memory; the first build here had the text "punct" in body
bytes 305–309. `loco_info_crc` covers those bytes, so v12 can produce a
different `loco_info.bin` and CRC for identical settings from one build (or
run) to the next.

**Fix in v12:** `memset(&static_braking_config, 0, sizeof static_braking_config);`
at the top of `InitStaticBrakingValues()`. `InitNationalValues()` assigns
every member, so it isn't affected, but the same line there costs nothing.
The editor always zero-fills.

## Tests

- **`lococonfig` (57 checks), anchored on a golden fixture.**
  - `tests/fixtures/loco_info_v12.bin` is the body v12 itself writes. It
    was compiled from your file with the sockets removed and that one local
    zeroed, and is identical across -O0 and -O2 builds.
  - Packing `lococonfig/loco_defaults_v12.json` must equal it byte for
    byte: CRC `0x39F59222`, with `train_desc` zero after its NUL.
  - Parse → pack is identical, header bytes are `1C 02 78 77 01`, and a
    flipped bit is caught by the CRC.
  - Range, text, IPv4, hex and float rules.
  - Every key in the presentation file exists in the schema, and every
    schema field is grouped.
  - The store: round trip, a field that disappeared is reported, a field
    that appeared takes the default, the last configuration can't be
    deleted.
  - The history: round trip, and a torn last line is skipped.
- **`lococonfigrun` (22 checks).**
  - The real window: edits a u32 and an IPv4 field, refuses a u16 of 70000
    with the range in the message, and refuses a bad target IP.
  - It sends to a UDP socket on 127.0.0.1; the datagram equals header +
    body, and the CRC and edits check out on the receiving side.
  - The history holds the same bytes. After a reopen, an unsent edit is
    still marked unsent and the sent one is not.
- **Audits.** `contrastaudit` scans `lococonfig/`. `menuaudit` checks the
  entry is top level in Tools, on Ctrl+Alt+L, not in Transmit, and opens a
  single window.

## verify.sh

Full run on the final tree, **14/14 stages green, 0 failed**:

- golden validators: 11/11 ok (after the LINFO schema note)
- unit suite: 125 suites, 3404 checks, 0 failed (+`lococonfig`, +`lococonfigrun`)
- menu audit: 70 ok, 0 failed
- headless smoke: 500 datagrams, app alive at timeout

Built with Qt 5.15 only; the Qt 6.4 build was not run here. Light and dark
renders of the window were checked by eye.
