# RFID Tag Builder — plan and handoff

Work asked for by Abhinav on 2026-10-10: bring the old **tags_sim** tool
(PyQt5, `~/Downloads/tags_sim.zip`) into DLConsole and make it better:
make custom tags, build whole track scenarios from different tag types, and
import/export. Done as four patches, in order: A, B, C, D (his choice).

**If you are picking this up in a new session or account:** read this file,
then the CHANGELOG sections for the patches marked done below, then carry on
with the first phase not marked done. Update the "Status" lines here in the
same commit as each patch.

## What tags_sim does (reference)

- A tag is 128 bits shown as two 16-hex-digit halves, `page_x` and `page_y`.
  **page_x is the little-endian 64-bit word of tag bytes 0-7, page_y of bytes
  8-15**, printed most significant digit first. Checked: tags_sim tags decode
  field for field through `schema/rfid_oracle.py` with
  `frame = reader_byte + x.to_bytes(8,'little') + y.to_bytes(8,'little')`.
- Types 9 Normal, 10 LC gate, 11 Adjacent line, 12 Adjustment/Junction. The
  bit layouts are the schema's `RFID` packet (`schema/kavach.xml`), same as
  tags_sim's tables. CRC-30 Anx-D (poly 0x2030B9C7) at tag bit 98.
- Route spreadsheet (`.xlsx`): sheet `tags` (Tag, `page_x="…" page_y="…"`)
  and sheet `signals` (sig_foot_tag, signal, sig_id).
- On save it writes `<first>_<last>route.xml`: `<route>` with two
  `<route_data route_name dir>` (the route and its REV twin), each a list of
  `<rfid_data rfid_id tag_name tag_type abs_loc next_rfid_abs_loc track_id
  page_x page_y/>`. Duplicate tags ("550D") are listed with the main ones;
  direction is guessed from whether abs_loc rises (dir 1, DN) or falls (dir 2,
  UP); adjustment tags shift the abs_loc written for the tags after them.
- The simulator reads `Configuration1.xml`: `<LOCO>` with `<station>`,
  `<tracks>` and many `<route_data>` blocks (same `rfid_data` rows). It is
  copied to the RFID simulators (192.168.25.18 / .79,
  `/home/KAVACH/RFID_SIM/LocoTcasSimulator/XML`). DLConsole only exports it;
  it does not copy it anywhere.
- Found while reading: tag 550 in tags_sim's `output/550_991_route.xml` fails
  its CRC-30. tags_sim never says so.

## Decisions taken (defaults; Abhinav did not answer these, change if he says)

- xlsx: no native reader in DLConsole (Qt has no public zip API). A stdlib
  Python script, `scripts/tags_sim_import.py`, converts tags_sim `.xlsx`
  routes to DLConsole's own route file.
- Fixtures: real KAV_CONFIG files are in `tests/fixtures/tags_sim/`, which
  is **git-ignored** (Abhinav, 2026-10-10: keep the real configuration out
  of git). Tests use them when present and print a NOTE and skip when not;
  window tests build their own routes from field values. Copy them from
  `~/Downloads/tags_sim.zip` (KAV_CONFIG/Hafizpet, KAV_CONFIG/S2S,
  KAV_CONFIG/S2S_Configuration1.xml, output/550_991_route.xml) to run the
  full set.
- Configuration1.xml: equivalent XML, not byte for byte (decided in C).
- No scp / upload to the simulator boxes. No turnout logic (owned by another
  person).

## Phases

### A — tag codec, tag editor, route.xml import/export
Status: **done, patch 195** (CHANGELOG session 195). Notes for B:
`RfidTag::Route` already carries `signalList` (read and saved, not yet
editable in the window). `Schema::Encoder::enumChoices` returns nothing for
kavach.xml (enums sit in `<enums>`), so use `RfidTag::enumLabel` (the
decoder's). Window test hooks: `TagBuilderWindow::setPages / setField /
setType / loadFile(path, routeIndex)`.

- `rfidtag.{h,cpp}`: page_x/page_y <-> 16 tag bytes <-> field values, via the
  schema's `RFID` packet (Schema::Encoder parse/encode; CRC computed by the
  `<crc>` element). The encoder refuses rather than guesses.
- Route model: ordered tags (name, 16 bytes) + signals + name + direction.
  Read tags_sim `route.xml` (both the older flat `<route_data>` and the
  newer `<route>` forms) and DLConsole's own route file; write both.
- Tools ▸ RFID Tag Builder…: one-tag editor (type, fields with enum names,
  page_x/page_y both ways, CRC state) and the route's tag list
  (add/insert/delete/move, import/export).

### B — scenario editor and checks
Status: **done, patch 196** (CHANGELOG session 196). Undo via `UndoLog`
(each route edit goes through `TagBuilderWindow::changed()`); checks in
`rfidcheck.{h,cpp}`; picture in `routestrip.{h,cpp}` (its own widget, not
`trackdiagram`, which is built around a log).

- Signals table, explicit direction, shift all by ±N m, undo.
- Checks (observed / what the loco would do, never "invalid"): CRC-30;
  main/duplicate pairs; abs_loc order along the direction; tag spacing;
  signal-foot tags vs signals; TIN continuity; adjustment tags' effect; LC
  gate distance (type 10 UNVALIDATED, no real frame).
- Track preview (reuse `trackdiagram` if it fits).

### C — Configuration1.xml, adjustment, text exports
Status: **done, patch 197** (CHANGELOG session 197). Adjustment in
`RfidTag::routeRows(tags, dir)` (comment there has the rules); text files and
the Configuration1.xml splice in `rfidexport.{h,cpp}`. Ground truth (local):
`tests/fixtures/tags_sim/adjust/` and `text/`, copied from the zip's
KAV_CONFIG (3.40804_908route.xml, the "23.11_N->N…" one renamed without `>`,
S2S UP/DN `…plus1000m…route.xml`, Gullaguda DN MAIN/LOOP route.xml) and
`output/` (SingleDN20.9.xlsx, UpSingle20.9.xlsx converted, 550_991_*.txt,
991_175_*.txt).

### D — plan vs run
Status: **done, patch 198** (CHANGELOG session 198). `planrun.{h,cpp}`;
the window's Run tab. The window now takes the `MessageDispatcher`.

## After D

All four phases are built. Open threads, none started:
- `Schema::Encoder::enumChoices` finds no enum in kavach.xml (they sit in
  `<enums>`), so Packet Maker and Field sweep never offer named choices.
  Left alone on purpose (fixing it changes Packet Maker's defaults); ask
  Abhinav.
- The checks read `dir_corr_1` for nominal and `dir_corr_2` for reverse,
  as tags_sim does: not confirmed against firmware.
- Nothing has been loaded into the RFID simulator itself yet.
- Pushing: patches 195-198 were committed locally; the auto-mode classifier
  blocked `git push`. The remote also has a WRONG `patch-195` tag (it points
  at patch 194's commit) from a failed first attempt; fix with
  `git push origin main && git push -f origin patch-195 && git push origin
  patch-196 patch-197 patch-198`.
