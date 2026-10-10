# DLConsole changelog

Newest first. One section per delivered patch; each section is the session
note that shipped with that patch, unchanged apart from its headings moving
down one level.

From patch 87 on, history lives in git (`git log`), and this file gets one
section per patch. The per-session `CHANGES_SESSION_*.md` files it replaces
are in the first commit if the originals are ever needed.


---

<a id="session-197"></a>
## Session 197 — RFID Tag Builder, phase C: adjustment tags, Configuration1.xml, text files

Phase C of the tag builder (`docs/RFID_TAG_BUILDER.md`): the rest of what
tags_sim wrote, done the way it did it, so DLConsole can replace it.

**What the operator sees:**
- **Export** is now a menu:
  - **route.xml…**, as before;
  - **Into Configuration1.xml…**: pick the simulator's file, then where to
    write the result (it may be the same file). The route and its REV route
    replace the routes of the same name, or are added after the last one.
    **Everything else in the file is left byte for byte as it was:** the
    edit is a splice of text, so the hand-kept station, tracks and
    formatting are untouched. The result is read back first and refused
    unless it parses, keeps every other route and holds this one.
  - **Text files…**: tags_sim's `<first>_<last>_rfid.txt`, `_sigID.txt` and
    `_tag_link_info.txt`, written into a folder.
- **route.xml loc** column. After an adjustment tag, the tags' own locations
  are in the numbering it corrects to. tags_sim writes them back in the
  numbering before it, so the simulator sees one continuous line, and this
  column shows where route.xml puts such a tag. route.xml now does the same:
  - nominal routes use `dir_corr_1` and reverse routes `dir_corr_2`;
  - N→N and R→R are shifted, N→R and R→N mirrored;
  - duplicate tags keep their own location.

  An adjustment whose `dir_corr` has no rule for the direction keeps its own
  location. tags_sim wrote **-1** into the file there, so this is a
  deliberate difference; a tooltip says why.

**Checked against tags_sim's own output:**
- **Its six route.xml files that hold adjustment tags** (Gullaguda,
  3.40/23.11, S2S UP and DN), rebuilt from their tags: **every row, attribute
  for attribute, in both directions**.
- **Text files:**
  - SingleDN20.9: all three files line for line (550_991_*).
  - UpSingle20.9: rfid and sigID line for line. tag_link_info has the same
    five stretches plus a sixth. The file on disk predates the tags_sim code
    this was ported from, which writes six.
- **Not reproduced:** one Telapur route.xml lists a tag id twice, and
  tags_sim's dictionary then reorders two of its rows. The checks already
  flag a main tag listed twice.

### Files

New: `rfidexport.{h,cpp}`, `tests/test_session197.cpp`. Changed:
`rfidtag.{h,cpp}` (`routeRows(tags, dir)`, the adjustment, the REV route
built from the forward rows), `tagbuilderwindow.{h,cpp}`, `dlcore.pri`,
`tests/tests.pro`, `tests/test_session196.cpp` (column index),
`docs/RFID_TAG_BUILDER.md`. Local, git-ignored fixtures added:
`tests/fixtures/tags_sim/adjust/` (the six route.xml) and `text/` (two
routes and the text files tags_sim wrote for them).

### Tests

`session197`: 19 checks, plus 4 with the local fixtures.
- **Adjustment, on built routes:** N→N, N→R (mirrored), a dir_corr with no
  rule, reverse R→R, a duplicate after it, and the REV route in route.xml.
- **Text files:** a built route with signals and a duplicate, line for line.
- **Configuration1.xml:**
  - a route replaced in place and its REV added;
  - the bytes before and after unchanged;
  - merging twice changes nothing;
  - a file with no routes, and a route with no direction, are refused.
- *(local fixtures)*:
  - the six adjustment route.xml files, every row;
  - both text-file routes;
  - the real S2S Configuration1.xml merged, the rest unchanged.
- **The window:** the route.xml loc column, Export ▸ Text files, and Export
  ▸ Into Configuration1.xml.

**Not tested:** the merged Configuration1.xml loaded by the RFID simulator.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2, with the local fixtures:
validators **13/13**; `dltests` **246 suites / 6749 checks, 1 failed** (the
`session156` pty check, as at 194); menu audit passed; headless smoke
alive.

---

<a id="session-196"></a>
## Session 196 — RFID Tag Builder, phase B: route checks, the route strip, signals

Phase B of the tag builder (`docs/RFID_TAG_BUILDER.md`). The aim is to put a
whole track scenario together and see what is wrong with it before the
simulator or a loco finds out.

**What the operator sees** in Tools ▸ RFID Tag Builder:
- **The route strip**, above the tables. The route is drawn by absolute
  location:
  - a tick per tag, tall for a main tag and short for its duplicate;
  - the main tags' names, the signals at their foot tags, and an arrow for
    the direction of travel;
  - a tag whose CRC fails in the error colour, an adjustment tag in the
    accent colour, and the selected tag ringed;
  - click a tag to select it.

  Names that would overlap are left off. They are all in the tables.
- **Tags tab**:
  - a new **Δ (m)** column: metres from the tag before, along the direction;
  - **Add duplicate**: the selected main tag's duplicate, right after it and
    4 m further on (the Hafizpet spacing), CRC computed. It is not added
    twice.
  - **Shift…**: every location moved by N m and the CRCs recomputed. It is
    refused as a whole, with nothing moved, if any tag would land outside
    0–8388606 m. Adjustment tags get location-1 only, as tags_sim shifts
    them.
  - **Undo** (Ctrl+Z, and a button that names the step) works for every
    route edit: add, insert, replace, delete, move, duplicate, shift,
    direction, signals.
- **Signals tab** (tags_sim's signals sheet): foot tag, signal, signal id,
  edited in place. **Add signal** adds one at the selected tag.
- **Checks tab**, "Checks (N to look at)". These are observations; a finding
  only says what a loco would do where the specification says so.
  Double-click a finding to select its tag.
  - **To look at:**
    - a CRC-30 that fails ("a loco would not process this tag");
    - a type that is not a tag type;
    - a duplicate not right after its main tag, or with no main tag;
    - a duplicate not further along than its main, or differing from it
      (it names the fields);
    - a main tag listed twice;
    - a tag behind, or level with, the one before it along the direction;
    - a signal whose foot tag is not in the route;
    - a signal on a tag that is not a signal foot for this direction.
  - **Information:**
    - main tags with no duplicate;
    - where the TIN for this direction changes;
    - a signal-foot tag with no signal;
    - what an adjustment tag tells a loco running this way;
    - that LC gate tags have never been seen in a capture.

  Location order is not checked across an adjustment tag, whose numbering
  may legitimately change. With no direction set, the direction-dependent
  checks are skipped, and the first finding says so. **No distance limits
  are graded:** the specification gives none here, so gaps are shown in Δ
  and not judged.

**On the real Hafizpet DN_MAIN route** (local fixtures), read as nominal,
the checks find three things:
- signal S1637_H names tag 904, which is not in this route (it starts at
  910);
- **tag 871D fails its CRC-30**;
- the TIN changes five times along the route (84 → 2 → 4 → 6 → 8 → 81).

**Assumption, named in the code:** the checks read `dir_corr_1` for a loco
running nominal and `dir_corr_2` for reverse, as tags_sim reads them. This
is not confirmed against firmware.

### Files

New: `rfidcheck.{h,cpp}`, `routestrip.{h,cpp}`, `tests/test_session196.cpp`.
Changed: `tagbuilderwindow.{h,cpp}`, `dlcore.pri`, `tests/tests.pro`,
`tests/screenshot_main.cpp` (`SHOT_TAB=checks`), `docs/RFID_TAG_BUILDER.md`.

### Tests

`session196`: 32 checks, plus 1 with the local fixtures. The routes are
built in the test from field values through the schema:
- **Every check**, each on a route made to break it:
  - a clean route has nothing to look at;
  - with no direction, it says what is skipped;
  - the same tags read as reverse;
  - a corrupted tag;
  - two main tags swapped;
  - a lone duplicate and a lone main;
  - a duplicate on another TIN;
  - a TIN change;
  - three kinds of signal mismatch;
  - an adjustment tag (its message, no order finding across it, a blank Δ);
  - an LC gate tag.
- *(local fixtures)* the real DN_MAIN findings above, exactly.
- **The window:**
  - the tabs and their counts, the Δ column, the strip's hit test;
  - Add duplicate, and not twice;
  - Shift, the refused shift, Undo of it, and the undo depth;
  - direction change and its undo;
  - double-click from a finding to its tag;
  - add, edit and delete a signal, with the checks following;
  - the 1100 × 700 fit, and no orphan widgets.

**Not tested:**
- the strip's drawing beyond its hit test (checked by screenshot only);
- the adjustment direction fields against firmware.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2, with the local fixtures:
validators **13/13**; `dltests` **245 suites / 6726 checks, 1 failed** (the
`session156` pty check, as at 194); menu audit passed; headless smoke
alive.

---

<a id="session-195"></a>
## Session 195 — RFID Tag Builder, phase A: tags, routes, route.xml

Asked for by Abhinav: bring the tags_sim tool (PyQt5, used to make the RFID
tags and routes the RFID simulator reads) into DLConsole and make it better,
in four phases A, B, C, D (his order). The plan, the decisions taken and the
state of each phase are in `docs/RFID_TAG_BUILDER.md`. It is kept there so
the work can carry on from another session or account.

**What the operator sees:** **Tools ▸ RFID Tag Builder…** (Ctrl+Alt+B).
- **Tag** (right): the type (9 Normal, 10 LC gate, 11 Adjacent line,
  12 Adjustment/Junction) and every field the schema gives that type.
  Short coded fields are a list of every value, named as the decoder names
  them; locations are numbers in metres. Edit a field and page_x / page_y
  follow, with the CRC-30 computed. Paste page_x / page_y and the fields
  follow, with the stored CRC checked: "CRC-30 stored …, the contents give …:
  a loco would not process this tag". **Recompute CRC** rebuilds the tag
  from its fields.
- **Route** (left): the tags in the order the loco meets them, with the
  CRC-30 result beside each one, plus a name and a direction. Add, Insert
  above, Replace, Delete, Up, Down. The title shows `*` while there are
  changes; closing or opening another route asks first.
- **Open…** reads a DLConsole route (`.tagroute.xml`), a tags_sim
  `route.xml` (both its older flat form and the newer route + REV form), or a
  **Configuration1.xml**. A file with several routes asks which one to open.
  Opening says how many tags fail their CRC.
- **Save…** writes the DLConsole route file: tags, signals, name, direction.
- **Export route.xml…** writes tags_sim's route.xml: the route and its REV
  route, named `DN_<first>_<last>` / `UP_…` and `REV_…` as tags_sim names
  them. It is refused while the direction is not set.

**Found on the way:**
- **Tag 550 of tags_sim's own output fails its CRC-30.** So do **601 of
  the 1208 tags** in the S2S `Configuration1.xml`. tags_sim never says so.
- `Schema::Encoder::enumChoices` finds no enum in kavach.xml, because the
  schema nests them in `<enums>` and the encoder only looks at the top
  level. So Packet Maker and Field sweep never offer named choices. **Not
  changed here** (it would change Packet Maker's defaults). The tag builder
  uses the decoder's labels instead (`Schema::Decoder::enumLabel`, made
  public).

**How:**
- `rfidtag.{h,cpp}`. **page_x is tag bytes 0–7 as a little-endian 64-bit
  number, page_y bytes 8–15.** Fields are read and written through the
  schema's `RFID` packet (Schema::Encoder parse/encode, the `<crc>` element
  for the CRC-30), the same definition the decoder and `validate_rfid.py`
  use. A value too wide for its field is refused by name, not cut short.
- route.xml's columns follow what the real files hold, checked over the
  KAV_CONFIG set with `rfid_oracle.py`:
  - `track_id` is the reverse-direction TIN;
  - `next_rfid_abs_loc` is the next row's location (the last row gives its
    own).
- `scripts/tags_sim_import.py` (standard library only) converts tags_sim's
  `.xlsx` routes, signals sheet included, to `.tagroute.xml`. It converted
  every .xlsx in tags_sim's KAV_CONFIG. DLConsole has no xlsx reader (Qt has
  no public zip API).

### Files

New: `rfidtag.{h,cpp}`, `tagbuilderwindow.{h,cpp}`,
`scripts/tags_sim_import.py`, `docs/RFID_TAG_BUILDER.md`,
`tests/test_session195.cpp`.

**Real KAV_CONFIG files stay local (Abhinav's choice).**
`tests/fixtures/tags_sim/` is git-ignored. On this Mac it holds:
- Hafizpet DN/UP route.xml;
- an S2S route.xml;
- tags_sim's old flat 550_991 route.xml;
- the S2S Configuration1.xml;
- two .xlsx with their converted .tagroute.xml.

`session195` checks against them when they are there. When they are not
(CI, another machine), it prints a NOTE and skips those checks. Only three
real tags (904, 904D, 550) appear in the test source, as hex.

Changed: `dlcore.pri`, `mainwindow.h`, `mainwindow_menus.cpp`,
`mainwindow_tools.cpp`, `schema/schemadecoder.h` (enumLabel public),
`tests/tests.pro`, `tests/screenshot_main.cpp` (`SHOT_WINDOW=tagbuilder`).

### Tests

`session195`: 47 checks with the local fixtures, 34 without:
- **Codec:** page values ↔ bytes ↔ the schema's fields for tag 904, which
  agree with the hand decoder and the route.xml's own values. An LC gate tag
  built from its fields reads back field for field. A 24-bit location is
  refused.
- *(local fixtures)* **Every real tag in them (1,400+)** rebuilds with the same fields
  and a CRC that passes. It rebuilds byte for byte except where its stored
  CRC failed.
- **Files** *(the real ones with the local fixtures)*:
  - both route.xml forms, a Configuration1.xml (20 routes, 1208 tags) and a
    converted .xlsx (42 tags, 9 signals);
  - a route file saves and reads back the same bytes;
  - **the exported route.xml has tags_sim's rows, attribute for attribute, in
    both directions**, for a DN and an UP route;
  - bad rows are skipped and named.
- **The window:** paste, edit, type change, the CRC states, the route
  operations, Save, Export, the warning on failing tags, the refusal with no
  direction, the 1100 × 700 fit, and no orphan widgets.

**Not tested:**
- type 10 (LC gate) against a real tag: none exists in any capture or in
  KAV_CONFIG;
- the exported route.xml loaded into the RFID simulator itself.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2, with the local fixtures:
validators **13/13**; `dltests` **244 suites / 6694 checks, 1 failed** (the
`session156` pty check, as at 194); menu audit passed; headless smoke
alive. After that, the window checks were changed to build their own route,
so they run without the fixtures. `session195` was then rerun alone on Qt 5,
with the fixtures (47 ok) and without them (34 ok); the full gate was not
rerun for that test-only change.

---

<a id="session-194"></a>
## Session 194 — reader direction (@rdir) and the RDSO FRS 18 direction tests

Asked for by Abhinav: a READER_INFO message from the firmware, and a window
with the readers' directions against the RDSO test cases FRS 18.1–18.7
("direction determination based on tag read by RFID reader-1 and reader-2",
SIF-0533 v4.22 pp. 359–362). His choices: the token `@rdir`, 5 bytes LE;
directions 0 U, 1 N, 2 R; check against a picked test case; the tag reported
to SVK from the loco's ARP.

**The message** (`SOS_handoff/04_READER_DIRECTION_LOGGING.md`): `reader_id`
u8, `tag_id` u16 LE, `reader_dir` u8, `movement_dir` u8, one per tag read.
The firmware keeps no per-reader direction (`current_direction` is a local in
`DetermineTrainDirectionReader1/2`, set from a reader's second tag on), so
the README adds one value per reader: U until it decides, back to U where the
reader's count resets. The send call goes in `ProcessRFIDTagReader` after
each reader's function, not inside it, because
`ClearPositionReportWithoutProfile` also calls them with a tag nobody read.

**What the operator sees:** **Tools ▸ Monitor ▸ Reader direction…**
(Ctrl+Alt+E).
- **Observed**, one row per read: #, time, reader, tag ("R2 (102)"),
  Reader-1 / Reader-2 / OVK direction after it, the **tag reported to SVK**
  (the loco's next ARP's `LAST_RFID_TAG`, when it changed before the next
  read; blank otherwise, as in the RDSO tables; a CRC-failed ARP is ignored),
  and which test row it satisfies.
- **R1, R2, …** are the tags ordered by location from `@rfid` when every tag
  has one, else by tag id. The line above the table says which.
- **Test case** picks one of the seven RDSO tables; it is shown under the
  reads with each row "seen at read N" or "not seen in order". The OK / Not OK
  column is left blank for the signatory.
- **From read / to read** bound the test run. Both readers start at U there,
  as at a start of mission, and a later run in the same log can't satisfy
  this one's rows.
- **Copy tables:** both, tab-separated, for the test record. Double-click a
  read to show it in the log.

**Schema:** `READER_INFO` (`captype==rdir`), enum `readerDir`.
`validate_rdir.py` (13th validator) compares the engine with `struct.unpack`
field by field and checks every frame is 5 bytes: 39/39. A wrong `tag_id`
width drops it to 11.

**Fixture:** `tests/rdirgen/make_fixture.py` writes
`schema/fixtures/rdir_synthetic.log`, the seven cases a minute apart.
- `@rdir` come from a simulation of the two firmware functions as written.
- After each input-table step there is a **real** ARP of loco 1 in SR
  (2026-10-08 10:16), with only `LAST_RFID_TAG` set and its JAMCRC
  recomputed. The recipe reproduces the original's stored CRC (`78067F5F`),
  and all 28 pass in C++.
- **Simulated as written, the firmware's logic produces all seven RDSO tables
  row for row.** That is the scripted logic, not a test of the firmware.

### Files

New: `readerdir.{h,cpp}`, `readerdirwindow.{h,cpp}`,
`schema/validate_rdir.py`, `schema/fixtures/rdir_synthetic.log`,
`tests/rdirgen/make_fixture.py`, `tests/test_session194.cpp`. Changed:
`capturedecoder.{h,cpp}` (`CapType::Rdir`), `schema/kavach.xml`,
`mainwindow.h`, `mainwindow_menus.cpp`, `mainwindow_tools.cpp`, `dlcore.pri`,
`tests/tests.pro`, `tests/screenshot_main.cpp` (`SHOT_WINDOW=rdir`,
`SHOT_TEST=18.x`), `CLAUDE.md` (13 validators).

### Tests

`session194` (32 checks):
- the decoder agrees with the schema on all 39 frames, and the 28 ARPs pass
  their CRC; not-5-byte frames are not read; U / N / R;
- every one of 18.1–18.7, bounded to its own run, has every RDSO row seen in
  order;
- 18.1's run against 18.4's table: 2 of 4, row 1 not seen;
- 18.4 read by read;
- R1.. by location from **real** `@rfid` frames (560, 598, 8: not by id);
- the window: the mapping line, 18.7's table all seen with its blank and R4
  rows, the observed row numbering, the status count, Copy, the 18.4
  mismatch, double-click, fits 1100 × 700, no orphan widgets; a log without
  `@rdir` says why.

**Not tested:** the README's code in the firmware (only against stubs); a real
`@rdir` capture.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators **13/13**; `dltests`
**243 suites / 6647 checks, 1 failed** (the `session156` pty check);
menu audit passed; headless smoke alive.

---

<a id="session-193"></a>
## Session 193 — the review's findings on the SoS work, fixed

A review of patches 184–192 (Abhinav asked for it, then for all of it to be
fixed). Measured on his 2026-10-08 field log, 194,348 rows in the tab.

1. **The SoS window froze about 3 s on a day's log.**
   `SosStrip::configFromLog` decoded every `@linfo` in the tab (about
   12,000) to use the newest one, on every rebuild: opening the window,
   changing tab, building an incident pack. It now walks back from the end
   and decodes only the newest. **2,866 ms → 3 ms**, same value (3000 m).
2. **Opening the window from the menu built it twice.** `SosWindow::setSource`
   now returns at once for the tab already shown.
3. **Minimal layout: a threat already on when a log (or a window of it)
   begins was reported as "started" at that first second.** Nothing was
   observed starting there. The first snapshot now gives "Manual SoS from
   loco 2 already on when the log starts, 410 m" (no "added"). Its spell
   carries `onAtLogStart`, and the lanes and reports read "(already on when
   the log starts)". This matters for the lanes and the incident pack, which
   read a window of the log, not only for whole logs.
4. **Minimal layout: DEST_LOCO_SOS said "from station 0".** That layout
   doesn't log the station behind it. It now reads "Station DEST_LOCO_SOS
   (general) from a station".
5. **The lanes coloured collisions by matching the spell's words** with a
   copy of the wording in `laneband.cpp`. That check is now
   `SosLog::isCollisionText`, built from `threatName()` itself, with
   `Spell::isCollision()` beside it, so the two cannot drift apart.

Also measured and fine: the new ARP-mode rule (190) gives 65 mode changes
over the day and none flips back within 10 s. `SosLog::extract` and
`hasSos` take 7 / 6 ms on the day log; `addDmiMoments` 1.35 s for its 10
missions. The firmware's `loco_manager.c:943` sets `sensor_speed` from
km/h × KMPH_TO_MPS, so it is m/s, as the two-loco view's `@sos` trace
assumed (186).

### Files

`sosstrip.cpp`, `soswindow.{h,cpp}`, `soslog.{h,cpp}`, `laneband.cpp`;
tests `test_session193.cpp` (new), `tests.pro`.

### Tests

`session193` (15 checks): the trigger distances from the newest `@linfo`, and
"not known" without one; `setSource` on the shown tab keeps the moment; the
minimal log read from 10:00:30 says loco 2's manual SoS was "already on when
the log starts, 410 m", doesn't call loco 2 added (but does loco 4 later),
and its spell and episode text say so, while the whole log marks nothing;
DEST "from a station"; every threat's text is a collision exactly when the
threat is. `session184`–`187` and `session192` pass unchanged.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 12/12; `dltests`
**242 suites / 6615 checks, 1 failed** (the `session156` pty check);
menu audit passed; headless smoke alive.

---

<a id="session-192"></a>
## Session 192 — the minimal SoS layout (README 03, @sos version 2)

Abhinav wanted less code in the firmware than README 01's.
`SOS_handoff/03_SOS_LOGGING_MINIMAL.md` gives it: one function pasted into
`sos_table_manager.c`, one call at the end of `SOS_Periodic_Maintenance()`,
and one line each in `utility.h` / `utility.c`. No struct changes and no
event calls. It sends one `@sos` line a second, version **2**, carrying the
own loco, the `loco_params` flags and DMI bits, and its in-use loco and
station entries (37 B + 24 per loco + 13 per station, at most 255 B).
DLConsole now reads it as well as version 1.

**What the operator sees.** Everything the SoS work (184–187) built works
on it: the SoS window (strip, table, decision panel, Two logs), the Safety
lane, the run summary, the incident pack, the two-loco view. The status
line says "minimal layout: threats to the second, no reasons or checks".

- **Events are derived** by comparing one second's table with the next:
  - loco added or "left the SoS table";
  - a threat starting, or ending "(reason not logged)";
  - station SoS on / off;
  - "Own ARP status 0 (No Emergency) → 4 (Head-On Collision)".

  The list says "changes", not "decisions".
- **Worked out from the flags,** as `SOS_RecomputeAggregates` picks them:
  the target (a station winning when the loco flags are clear) and each
  loco's "closest of its kind".
- **Not in this layout**, and said so on hover instead of guessed:
  - the nine checks: the columns are blank;
  - the ARP status as received: "–";
  - the ARP's own position: no dashed ghost;
  - brake decisions and end reasons;
  - the own train length: the own loco is an arrow on the strip.
- **Two logs** matches "heard" on the threat the sender's status makes
  here (1 unusual stop, 2 manual SoS, 6 train parted; 0 none), from the send
  time on. The summary says so.

**Schema.** `SOS_SNAPSHOT` branches on its version byte (`<when test="version
== 1">` / `== 2`), and version 2 has two `<repeat>`s (`loco[n] src_*`,
`station[n] stn_*`). The threats byte in the repeat is a plain hex byte:
`<flags>` is not walked inside a `<repeat>`, and the first draft misaligned
every entry after the first.

**Fixtures.** `tests/sosgen` builds twice: as before, and with
`-DSOS_MINIMAL` around README 03's function, byte for byte
(`tests/sosgen/sos_log_min.c`), on the same scenario. The result is
`schema/fixtures/sos_minimal_loco{1,2}.log`. The full-layout fixtures are
unchanged. `validate_sos.py` decodes both: every version-2 frame field by
field, its size, 1946/1946. A wrong `src_tin` width drops it to 1548.

### Files

`soslog.{h,cpp}`, `soswindow.cpp`, `sosrelay.cpp`, `schema/kavach.xml`,
`schema/sos_oracle.py`, `schema/validate_sos.py`,
`schema/fixtures/sos_minimal_loco{1,2}.log` (new),
`tests/sosgen/{sosgen.c,sos_log_min.c,make_fixture.sh}`;
tests `test_session192.cpp` (new), `tests.pro`, `screenshot_main.cpp`
(`SHOT_MINIMAL=1`).

### Tests

`session192` (25 checks), expected values from `sos_oracle.snapshot_v2`
with the same compare-each-second rule:
- the decoder agrees with the schema on all 281 version-2 frames;
- the target at 10:00:30 (loco 2, manual SoS, 410 m) and loco 3's closest,
  worked out; no ARP status, checks or raw position;
- the station winning at 10:03:50;
- the seven spells with the same times as the full layout, each "reason
  not logged";
- the derived words, and no brake decisions;
- the window: status, blank checks with the why, "–" ARP status, closest
  ✓, decision panel, Two logs "matched on the threat";
- `relay()` on the pair;
- the two-loco view and the run summary.

`session184`–`session187` pass unchanged on the full layout.

**Not tested:** README 03's function on the target (only against stubs);
a real `@sos` capture of either layout.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 12/12; `dltests`
**241 suites / 6600 checks, 1 failed** (the `session156` pty check);
menu audit passed; headless smoke alive.

---

<a id="session-191"></a>
## Session 191 — the mission report shows the DMI at each mission's key moments

Asked for by Abhinav. Each mission in the mission report (Ctrl+Alt+M) now
has a **"DMI at key moments"** section: the DMI panel as the loco pilot saw
it, rendered from the tab's own latest `@dmi` at or before each moment, two
to a row, as in the incident pack.

- **The moments**, at most 8 per mission:
  - first: the start of mission (or "Log start"), the first
    Staff_Responsible / On_Sight / Limited / Full_Supervision,
    System_Failure, each EB / FSB application, and the mission's end;
  - then the other mode changes, while there is room.
  One panel per instant, in time order. When some were left out, the
  section says so.
- **Never another mission's screen.** A DMI frame from before the mission
  started is not shown: "No @dmi in this mission at or before this time".
  In `replay/2026-10-08/loco_1_1_08102026_105200.cap`, mission 3 has no
  `@dmi`, and its moments say so instead of showing mission 2's last screen.
- A frame more than 3 s older than its moment carries a caption with its
  age ("The DMI's last frame before this, 12.0 s earlier (11:00:38)").
- `Missions::split()` stays cheap; the panels are rendered by the new
  `Missions::addDmiMoments()`, which the mission report window calls. The
  fault and cab-I/O timelines, which also split, don't pay for them.
- The incident pack's renderer is shared: `IncidentReport::dmiMomentAt()`,
  and `KeyMoment::dmiFrameMs` (when the frame arrived).

### Files

`missionreport.{h,cpp}`, `incidentreport.{h,cpp}`, `runreportwindow.cpp`;
tests `test_session191.cpp` (new), `tests.pro`.

### Tests

`session191`, real frames (session 171's fixture): no panels until
`addDmiMoments` runs; mission 2's moments start with the start of mission
(10:56:29) and include the first Staff_Responsible at 11:00:50, rendered
from mission 2's own frame; in time order; mission 3 lists its moments with
no panel; the HTML has each section, the embedded panels, the labels, and
mission 3's "No @dmi in this mission". `session97`, `session133`,
`session171`, `session175` pass unchanged.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 12/12; `dltests`
**240 suites / 6575 checks, 1 failed** (the `session156` pty check);
menu audit passed; headless smoke alive.

---

<a id="session-190"></a>
## Session 190 — the mode lane shows SR while the loco has no direction or location

Reported by Abhinav: DLConsole did not show SR mode when the direction and
location are zero.

**Cause.** A loco that is not localised (no direction, location 0) sends
**no LSRP** at all, only ARPs, and the mode (lanes, run report, incident
pack, track diagram events) was read from LSRP only. In Abhinav's
2026-10-08 logs, loco 2's whole 16:20 excerpt (56 ARPs, SR, no LSRP) showed
no mode, and loco 1's SR spells after a restart were missing too.

**Fix.** The ARP's `LOCO_MODE` counts when no LSRP has been heard for 3 s.
Where LSRP is heard it still decides, so the two cannot flap at a change.
SR, Stand_By, System_Failure (and Limited_Supervision at 10:14:01 in loco
1's log, which no LSRP carried) now appear as mode segments. The report
headings read "Loco mode (LSRP, else ARP)".

**What changes elsewhere:** more mode changes where a loco runs
unlocalised. In `replay/loco_1_1_26062026_162418.cap` the run report now has
4 changes (was 1: SR → Stand_By → SR at a restart 16:28:12–20, and On_Sight
→ SR at 16:36:46 after its LSRPs stop), and the track diagram counts 25
events it cannot place at 0 m (was 22: those three). Both re-counted with
`schema/engine.py` under the same rule.

### Files

`runreport.{h,cpp}`, `incidentreport.cpp` (heading); tests
`test_session190.cpp` (new), `test_session131.cpp`, `test_session143.cpp`,
`tests.pro`.

### Tests

`session190`, real frames: loco 2 at 16:20 is SR from 16:20:09 with no
change, and the mode lane names it; loco 1 at 10:13 has 7 mode changes,
Stand_By → SR at 10:16:04 and 10:17:28, Limited_Supervision at 10:14:01,
and On_Sight → Full_Supervision at 10:13:24 still from LSRP. `session131`
and `session143` updated to the new counts.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 12/12; `dltests`
**239 suites / 6563 checks, 1 failed** (the `session156` pty check);
menu audit passed; headless smoke alive.

---

<a id="session-189"></a>
## Session 189 — DMI: the speed in the dial can be read in a light theme

Reported by Abhinav: in light mode the speed in the centre of the DMI dial
could not be read. The hub at the dial's centre takes the pointer's colour:
white in Annexure-B colours and in a dark theme, but the theme's **ink**
(black) in a light theme; the digits were always black. Black on black.

The digits are now black or white, whichever stands out from the hub's own
colour (`qGray(hub) >= 128` → black, else white). Annexure-B colours and
dark themes look exactly as before; only the light themes change (white
digits on the dark hub). Nothing else on the panel is restyled.

### Files

`dmipanel.cpp`; tests `test_session189.cpp` (new), `tests.pro`.

### Tests

`session189`: the DMI rendered at 800 × 600, speed 45; the pixels in the
digits' band that differ from the hub's colour are counted. Light theme:
the hub is dark and 25+ digit pixels stand out (0 before the fix, measured).
Annexure-B and dark: light hub, digits stand out, as before.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 12/12; `dltests`
**239 suites / 6563 checks, 1 failed** (the `session156` pty check);
menu audit passed; headless smoke alive.

---

<a id="session-188"></a>
## Session 188 — Loco Configuration: the default vcc_crc is 0x9263FCA8

Asked for by Abhinav. A new configuration in the Loco Configuration editor
now starts with **vcc_crc 0x9263FCA8** (was 0xF6134AC1). Configurations
already saved keep their own value; the send bar and the send confirmation
show the new default as "the default value".

- `lococonfig/loco_defaults.json`: `vcc_crc` 2456026280 (0x9263FCA8), and
  `loco_info_crc` 0xC30842EC for the default body.
- `tests/fixtures/loco_info_default.bin`: the same 4 bytes (offset 269, LE)
  and the body CRC recomputed (JAMCRC, init 0, over body[0 : n-4]; the
  recipe was checked against the old fixture before rewriting it).

**Not regenerated by `sync_linfo.py`.** Neither copy of `loco_config_v12.cpp`
on /Volumes/ABH has `is_eqep_used` / `is_ecap_used`, which the schema has
since `c9e45d9` (that commit edited both files by hand too), so the script
cannot run against the current schema. This patch edits them the same way.
`source_sha256` still names the unedited tool. The tool itself still says
0xF6134AC1 at its line 552 (`loco_info.vcc_crc = ...;  // change crc`): it
was not changed.

### Files

`lococonfig/loco_defaults.json`, `tests/fixtures/loco_info_default.bin`;
tests `test_session159.cpp`, `test_session158.cpp`, `test_lococonfig.cpp`
(the default's value where they pinned it).

### Tests

`lococonfig` (packing the defaults gives the fixture byte for byte, CRC
good), `lococonfigrun`, `session158vcccrc` (the send confirmation shows
`<b>vcc_crc 0x9263FCA8</b>`), `session159` (compare shows it), all pass.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 12/12; `dltests`
**237 suites / 6550 checks, 1 failed** (the `session156` pty check);
menu audit passed; headless smoke alive.

---

<a id="session-187"></a>
## Session 187 — SoS, phase D: two logs, what one loco sent and what the other did with it

The last phase. The SoS window gets a second tab under the table:
**Decisions | Two logs**.

**Two logs.** Pick the other loco's log (any other tab with `@sos`). Every
change of the emergency status it broadcast — its own `@sos`
`own_emergency_status`, which is what its ARP carries — is one row,
followed into this loco's log (`SosLog::relay`):

- **Sent:** when the other loco's log shows the change, and from / to.
- **Heard here:** the first `@sossrc` in this log whose ARP status for that
  loco shows the new value, and how long after. If none does within 20 s,
  it says why: "not within 20 s" (the loco was in the table but no ARP
  showed it), or "loco N not in this loco's SoS table" (out of range,
  rejected, table full).
- **Acted on here:** the first decision about that loco after it (a threat
  starting or ending, the target removed, a brake not applied, a timeout,
  an eviction), how long after, and the decision in words; or "no decision
  about it".
- A summary line counts them. **The delays use the two locos' own RTCs**,
  and say so; a "heard" before "sent" is shown with a minus sign in the
  warning colour, not hidden.
- Double-click a time: that moment in that loco's log (and, for this log,
  the window moves there too).

On the synthetic pair: loco 2's pilot presses SoS at 10:00:09; loco 1 hears
it at 10:00:10 (+1.0 s) and starts the manual SoS (810 m) the same second.
Loco 2 releases it at 10:01:52; loco 1 hears that at once, with no decision
about it, since it had already ended that threat as passed at 10:01:46. The
other way, loco 1's head-on and rear-end statuses come after loco 2's log
ends, and the tab says it was not in loco 2's table.

**Also:** the strip's bottom margin is 6 px taller (the km labels touched
the legend at small heights).

### Files

New: `sosrelay.{h,cpp}`, `tests/test_session187.cpp`. Changed:
`soswindow.{h,cpp}`, `sosstrip.cpp`, `dlcore.pri`, `tests/tests.pro`,
`tests/screenshot_main.cpp` (`SHOT_TWOLOGS=1`).

### Tests

`session187` (21 checks): `relay()` both ways on the synthetic pair (sent,
heard, acted, the decision's words; not tracked); a receiver clock 3 s
behind reads "heard before sent"; the window's tab: the picker offers only
the other `@sos` log, two rows with their cells, "no decision about it",
the summary, double-click to each log and the window following, the view
from loco 2's side, fits 1100 × 700, no orphan widgets.

**Not tested:** two real `@sos` logs from two locos (none exist), so not the
real ARP period or RTC offsets; Windows / Linux rendering (see this push's CI).

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 12/12; `dltests`
**237 suites / 6550 checks, 1 failed** (the `session156` pty check:
macOS-only here, intermittent on Linux CI, see 186); menu audit passed;
headless smoke alive.

---

<a id="session-186"></a>
## Session 186 — SoS, phase C: the two-loco view and the incident pack read @sos

Phase C: the views that already existed now use the firmware's SoS log
(184) when a tab has it.

**Two-loco view.**
- **Events** come from the firmware's own threat spells (manual SoS,
  unusual stop, train parted and station SoS as "SoS"; head-on; rear-end),
  instead of `@lsos`, whenever the log has `@sos`. `@lsos` stays the
  fallback; logs without `@sos` read exactly as before.
- **A loco with `@sos` but no `@dmi` / `@lsrp`** gets its trace from `@sos`:
  own location every second, and `sensor_speed` taken as **m/s** (× 3.6 for
  km/h). That unit is read from v1.2.9's brake manager, which compares it
  with `sos_speed_limit * KMPH_TO_MPS`; it is not confirmed by a frame.
- **Each loco as the other's SoS table had it**: from A's `@sossrc` rows for
  B (and B's for A), dashed in that loco's colour beside its own trace,
  with a legend entry. Where `SOSWithAdjustment` moved it, or the ARP was
  stale, the two lines part. The empty-state text now says "@dmi, @lsrp or
  @sos". The menu tooltip says so too.

**Incident pack.**
- Each firmware SoS threat that **starts** in the window and each **brake
  decision** (applied or not) becomes a key moment ("SoS: Unusual stop from
  loco 3 started", "SoS: Emergency brake applied: …").
- **Every key moment** carries, beside its DMI panel, the SoS window's strip
  at that moment and "what the loco is reacting to" as a list.
- After the brakes: "SoS threats (firmware, @sos)" and "SoS brake decisions
  (firmware, @sosev)" for the window.

**Run summary (windowed: the lanes, the incident pack).** A threat that
started before the window and is still on in it is now listed: the window
is read 10 minutes back and the spells overlapping it are kept. Before, a
window caught only that spell's end and dropped it.

### Files

`twolocoview.{h,cpp}`, `twolocowindow.cpp`, `incidentreport.{h,cpp}`,
`runreport.cpp`, `mainwindow_menus.cpp` (tooltip); tests
`test_session186.cpp` (new), `tests.pro`, `screenshot_main.cpp`
(`SHOT_WINDOW=twolocosos`).

### Tests

`session186` (19 checks), on both synthetic logs: both traces from `@sos`
(281 and 131 samples; 10 000 m, 72 km/h at the start); loco 1's events from
its spells (5 SoS, 1 head-on from 10:02:30, 1 rear-end), none for loco 2;
loco 2 in loco 1's table from 10:00:02 at 11 010 m (970 m ahead) until it
was released, and loco 1 in loco 2's; the gap and plausibility; the window
shows and draws the pair. The incident pack around 10:01:31 (±20 s): the
unusual stop and its brake as key moments, every moment with a strip and
lines, the target's line in the HTML, and 2 threats (the manual SoS from
before the window, still on, and the unusual stop) and 1 decision.
`session97`, `session98`, `session133`, `session172` pass unchanged.

**Not tested:** a real `@sos` capture; the m/s reading of `sensor_speed`
against a real one; Windows / Linux rendering (see this push's CI).

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 12/12; `dltests`
**236 suites / 6529 checks, 1 failed** (the `session156` pty check, see
below); menu audit passed; headless smoke alive.

**CI note.** Patch 184's Linux job failed one check, `session156` "then it
is open, as before", with the macOS message ("Inappropriate ioctl for
device · link open"). Session 163 recorded it as intermittent on Linux; 183
passed it. Nothing in 184–186 touches the serial code. Not fixed here
(scope), and reported.

---

<a id="session-185"></a>
## Session 185 — SoS, phase B: the track strip, and what the loco is reacting to

Phase B of the SoS window (184). Two panes above its table:

- **The track strip** draws the snapshot along a straight line by absolute
  location (increasing to the right, as the track diagram does), **one lane
  per TIN**: the own loco's line in the middle, the others above and below
  it, named. Each train is a bar of its length behind its front with an
  arrow the way it is going: the own loco in the accent colour, the others
  by threat (collision: error, SoS: warning, none: muted), each labelled
  "Loco 2 · manual SoS · 990 m". The **target** is outlined and marked ◎.
  Where `SOSWithAdjustment` moved a loco or turned it round, the ARP's own
  position is drawn **dashed** beside it. Around the own loco's front, the
  **SoS trigger** (warning) and **collision trigger** (error) distances are
  shaded, read from the log's `@linfo`; a log without one says so in the
  legend instead of guessing. Stations are pins on the top edge. Hover any
  of them for the details.
- **What the loco is reacting to**, beside it: `SosLog::decisionLines` —
  the target and its distance (and why 0 m, when it is), the emergency
  status the loco's own ARP broadcasts, the flags that set the SoS speed
  limit, the other flags, the last DEST_LOCO_SOS, what the DMI shows, and
  "Observed: …" (in the warning colour) when a flag and its DMI bit differ.

The table now has the full width under them; the decisions stay at the
bottom.

### Files

New: `sosstrip.{h,cpp}`, `tests/test_session185.cpp`. Changed:
`soswindow.{h,cpp}`, `dlcore.pri`, `tests/tests.pro`.

### Tests

`session185` (22 checks), on the synthetic fixture whose `@linfo` is a real
frame: the trigger distances from it (3000 / 1000 / 500 m); at 10:01:40 the
lanes TIN 102 / 101 (own) / 103, the legend, loco 2 drawn as the target at
990 m, loco 3's ARP position dashed (13480 m nominal), the own loco, the
span; the decision panel's target and own status; at 10:02:32 loco 4 on the
own lane, the target, head-on at 920 m with own status 4; at 10:03:50 the
station's pin and the station as target, own status still 5; a strip with
no `@linfo` and no snapshot; the window fits 1100 × 700 with the strip at
least 150 px tall; no orphan widgets.

**Not tested:** a real `@sos` capture (none exists); Windows / Linux
rendering (see this push's CI).

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 12/12; `dltests`
**235 suites / 6510 checks, 1 failed** (the macOS-only `session156` pty
check); menu audit passed; headless smoke alive. Linux / Windows: see this
push's CI.

---

<a id="session-184"></a>
## Session 184 — SoS, phase A: the firmware's SoS table, decoded, in a window

The first of four phases (A–D), asked for by Abhinav after reading LKAVACH
v1.2.9's `sos_manager.c` / `sos_table_manager.c`. Today no LKAVACH build
logs its SoS state; `SOS_handoff/01_SOS_LOGGING_PACKETS.md` (given to him,
not in this repository) asks for three capture lines and gives the C code
that writes them:

- **`@sos`** — one snapshot: own loco, the target `SOS_RecomputeAggregates`
  picked, the `loco_params` flags and DMI bits, both station slots.
- **`@sossrc`** — one line per in-use source slot, right after its `@sos`
  (same `snap_id`): position as the ARP gave it and as `SOSWithAdjustment`
  left it, distances, threats, and every check re-run with the firmware's
  own functions (same TIN, ranges, adjacent line and where that answer came
  from, station check, would-process, passed).
- **`@sosev`** — one decision: a threat starting or ending *and why*, a
  brake applied or **not** applied *and why*, slots added, evicted, dropped,
  timed out, the table reset, DEST_LOCO_SOS changing, the own unusual-stop
  alarm.

Version 1, little-endian, written byte by byte (no struct padding). This
build reads them; `@lsos` (session 98's guessed layout, never logged) is
left as it was.

**What the operator sees:** **Tools ▸ Monitor ▸ SoS…** (Ctrl+Alt+O).

- A loco log picker, a slider over the snapshots, **◀ / ▶ Decision** to the
  previous / next decision, and **Follow the log** (the DMI's time travel:
  the snapshot at or before the row picked in any tab or replay).
- The **SoS table** at that moment: one row per tracked loco, then the
  stations. The target is bold and says "◎ target". Threat, closest of its
  kind, gap (the real one: the firmware zeroes its own `distance` once a
  source is passed, and the hover says what it read), SoS and collision
  distance, then the nine checks as ✓ / – / ! with the why on hover
  ("ARP said 13480 m nominal; after SOSWithAdjustment 13500 m reverse"),
  then position, direction, TIN, the source's last ARP status and age.
  "!" marks a check that disagrees with itself: adjacency failing open,
  or the station check answering with another loco's station (bug #5).
- **Every decision in words**, those after the cursor muted, the current
  one selected; double-click shows that moment in the tab's log.
- **The Safety lane** draws the firmware's threats between the own-SoS half
  and the brake strip (collisions in the error colour, SoS in the warning
  colour) and its brake decisions as ticks; hover names them. The **run
  summary** gets "SoS threats (firmware, @sos)" and "SoS brake decisions
  (firmware, @sosev)" sections — only when the log has `@sos`, so other
  reports read as before.

### No real capture yet: synthetic frames from the real logging code

`tests/sosgen/` holds the README's `sos_log.h` / `sos_log_impl.c` byte for
byte, stubs for the few firmware names they read, and a scripted two-loco
scenario (manual SoS passed, an unusual stop on an adjacent line behind an
adjustment tag, a head-on, a rear-end, a train parted 4 km away, a station
general SoS, DEST_LOCO_SOS, a Non-Leading reset), at the field's trigger
distances (SoS 3000 m, collision 1000 m, cancel 500 m). Each log starts with
loco 1's **real** `@linfo` frame of 2026-10-08, re-tagged and re-timed, so
the views read those distances from the log as they will from a real one.
`make_fixture.sh` writes
`schema/fixtures/sos_synthetic_loco{1,2}.log`. They prove the layout and the
views, **not the firmware**: the decisions are scripted the way v1.2.9 reads,
with bugs #1, #2, #6, #8 and #10 of README 02 in on purpose so the window has
them to show. The `LB_*` brake codes are placeholders (21–24): the real
values aren't in hand.

### Also

- **The C++ schema engine now sign-extends 32-bit `signed="true"` fields**
  (`toSigned` handled only < 32 bits; `engine.to_signed` always did). No
  field was signed and 32 bits wide before; `@sos`'s int32 distances are.
- Two new schema formatters, in both engines: `dm` (decimetres → "1234.5 m")
  and `x100` (value / 100).
- Station entries in `@sos` are named `stn_*`: the raw-value map is keyed by
  field name, and plain `sos_distance` overwrote the snapshot's own (the
  decoder-vs-schema parity test caught it).
- `CapType::Sos / SosSrc / SosEv`. They are not added to the Loco
  Console's tabs or link rows: the SoS window is their view, and those
  layouts have pinned width checks.

### Files

New: `soslog.{h,cpp}`, `soswindow.{h,cpp}`, `schema/sos_oracle.py`,
`schema/validate_sos.py`, `schema/fixtures/sos_synthetic_loco{1,2}.log`,
`tests/sosgen/{sosgen.c,sos_log.h,sos_log_impl.c,firmware_stubs.h,make_fixture.sh}`,
`tests/sosfixture.h`, `tests/test_session184.cpp`.
Changed: `capturedecoder.{h,cpp}`, `schema/kavach.xml`, `schema/engine.py`,
`schema/schemadecoder.cpp`, `runreport.{h,cpp}`, `laneband.cpp`,
`mainwindow.h`, `mainwindow_menus.cpp`, `mainwindow_tools.cpp`, `dlcore.pri`,
`tests/tests.pro`, `tests/screenshot_main.cpp` (`SHOT_WINDOW=sos`,
`SHOT_AT=HH:mm:ss`), `CLAUDE.md` (12 validators).

### Tests

- `validate_sos.py` (12th validator): every fixture frame decoded by the XML
  engine and compared field by field with `sos_oracle.py` (struct.unpack at
  the README's offsets), plus sizes, each `@sossrc` following its `@sos`
  (popcount of the in-use mask), events naming a snapshot already sent.
  1534/1534. A wrong width in the schema (own_tin 8 bits) drops it to 941.
- `session184` (66 checks): SosLog's decode agrees with the schema's on all
  1272 frames of loco 1; 462 snapshots, 33 events, 777 sources, none
  orphaned; the target and its words at 10:00:30, 10:01:40, 10:02:32,
  10:04:20; loco 3's adjustment and adjacency; the seven spells with their
  times and end reasons; the four brake decisions; event words; another
  layout version not read; the window (opens on the `@sos` tab, the table at
  10:01:40, the hover texts, the current decision, Decision ▶, Follow the
  log, fits 1100 × 700, no orphan widgets); a log without `@sos` says so;
  the run summary and the Safety lane.

**Not tested:** a real LKAVACH capture (none exists); the README's C code
inside the firmware (only against stubs here); Windows / Linux rendering
(see this push's CI).

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators **12/12**; `dltests`
**234 suites / 6488 checks, 1 failed** (the macOS-only `session156` pty
check); menu audit passed; headless smoke alive. Linux / Windows: see this
push's CI.

---

<a id="session-183"></a>
## Session 183 — Linux CI: a long big-number value no longer widens the console

182's diagnostics did their job. With 182 the Linux CI passed `session150`
but still failed `session169`, and the failure message named the part:
`BigNumberPanel 1242` while following the cursor. The mode tile's value,
"4 (Full_Supervision)" at three times the font, set its tile's minimum
width. That is the same failure session 151 fixed for the detail line,
this time on the value. It doesn't depend on following: any long mode
name, live, does it.

- The tile's value may now shrink to 80 px. It takes its full width when
  there is room, and on a narrow window it is cut short, with the full
  value in the tooltip (set with every value since 79). On the Mac the
  panel's minimum goes from 1,074 to 503 px with this value.
- `session169` now checks the width **with the big numbers shown**, so
  the Mac gate catches this case too. Before, it depended on what the
  settings file held.

### Files

`bignumberpanel.cpp`; tests `test_session169.cpp`.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 11/11; `dltests`
**233 suites / 6422 checks, 1 failed** (the macOS-only `session156` pty
check); menu audit passed; headless smoke alive. Linux: see this push's CI.

---

<a id="session-182"></a>
## Session 182 — Linux CI: the Loco Console fits a laptop again

From patch 169 to 181, the **Linux** CI job failed two width checks:

- `session150`: minimum 1158 px with the big numbers shown;
- `session169`: 1262 px while following the cursor.

The limit is 1100. Windows and Qt 6 passed, and the Mac measured 1057 and
1090. Linux renders fonts about 8% wider, which the Mac gate cannot see.
The failures went unnoticed for twelve patches.

- **Cause:** the **readouts row** (1,037 px on the Mac). Its CRC and seq
  chips grow with their counts ("✓ CRC 2593/2593", "seq 4567 · gaps
  120"), and 169 added "Follow cursor" to that row.
- **Fix:** the rate, CRC and seq readouts may now shrink (minimums 60 /
  90 / 90 px). Their text is cut short only on a narrow window, and the
  tooltip carries it in full, as the clock and mission chip already do.
  The row's minimum is 820 px on the Mac, and the console's is 990 px,
  set now by the actions row.
- The Follow checkbox loses its "⏱" glyph: on Linux a fallback emoji font
  can be wide. The tooltip says what it does. The mission chip's minimum
  goes from 120 to 100 px.
- **For next time:** `LocoConsoleWindow::minimumWidths()` lists each
  part's minimum, and `session150` / `session169` put it in their failure
  messages, so a CI failure says what is too wide.

### Files

`lococonsolewindow.{h,cpp}`; tests `test_session150.cpp`,
`test_session169.cpp` (failure messages).

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 11/11; `dltests`
**233 suites / 6422 checks, 1 failed** (the macOS-only `session156` pty
check); menu audit passed; headless smoke alive. Linux: see this push's CI.

---

<a id="session-181"></a>
## Session 181 — Level crossings approached, and the horn

Feature 11, the last of the eleven. The DMI names the level crossing
ahead (`lc.id`, with `lc.manning`, `lc.auto_whistle` and `lc.distance`),
and the DIO records the horn solenoid (@dip1 `horn1/2_solenoid_no`). Put
together, they answer: did the horn sound while the loco approached?

- **An approach** is a spell of @dmi frames naming the same LC. A gap
  over 30 s starts a new one.
- **The horn** counts as sounding while either solenoid feedback reads
  other than its usual value (the cab-inputs rule, 176). Without @dip1 in
  the span it is "not known", not "none".
- **Run summary**: a "Level crossings approached" table (LC, manning,
  auto whistle, from, to, distance shown, horn spells).
- **Mission report**: each mission's approaches, with "none" flagged
  when the horn did not sound.
- What 81_1 shows over the day: four approaches to LC 191 (manned, auto
  whistle 1). The horn sounded during those at 10:35 and 10:50, but
  **not during the 12:02 and 14:54 approaches**. The whole-day mission
  report lists the 14:54 one (698 → 7 m shown) as "horn none". The 12:02
  one falls outside the 200,000 rows a tab holds by default.

### Fixture

`replay/2026-10-08/loco_1_1_08102026_103520.cap` (new, 3,729 lines,
0.61 MB): 81_1 from 10:35:20 to 10:38:30, every packet type, capture lines
unchanged.

### Files

`lccheck.{h,cpp}` (new, in `dlcore.pri`), `runreport.{h,cpp}`,
`missionreport.{h,cpp}`; tests `test_session181.cpp` (new), `tests.pro`.

### Tests

- `session181`, against `schema/engine.py` with the same rules:
  - LC 19 (unmanned) 10:35:34–35, 727 → 713 m, horn at 10:35:35;
  - LC 191 (manned) 10:35:36–10:38:12, 699 → 286 m, horn 10:35:36–42,
    10:35:50–10:36:02, 10:37:17–25 and 10:37:31–10:38:06;
  - the table, and the run summary.
- `session171` (missions), `session143`, `session97`, `session180` pass.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 11/11; `dltests`
**233 suites / 6422 checks, 1 failed** (the macOS-only `session156` pty
check); menu audit passed; headless smoke alive.

---

<a id="session-180"></a>
## Session 180 — RFID tag check

Feature 10. Each RFID tag the loco read, against what else the capture
says about it, as a new section of the run summary:

| Column | From |
|---|---|
| Main / Duplicate | @rfid `duplication`: was the Main Tag read, and was its Duplicate Tag |
| NMS: duplicate missing | NMS `DUPLICATE_MISSING_RFID` named the tag (first time) |
| On an SLRP route | an SLRP route tag list (`tag[i]`) named it |

- **Route tags passed without a read.** Take two consecutive tag reads, A
  then B. If the latest SLRP route list before B holds A and B in that
  order with tags between them, those tags were passed and not read. Only
  the list's order is used; no positions are computed.
- The **RFID lane** gets a warning tick where the NMS reported a tag's
  duplicate missing, named on hover.
- What 81_1 shows: the duplicate tag is read for only a few tags, and the
  NMS reports a duplicate missing 104 times over the day (each sent
  twice). Over the whole day, no route tag was passed without a read (183
  distinct reads).

### Files

`tagcheck.{h,cpp}` (new, in `dlcore.pri`), `runreport.{h,cpp}`,
`laneband.cpp`; tests `test_session180.cpp` (new), `tests.pro`.

### Tests

- `session180`, on the 10:52 excerpt, against `schema/engine.py`:
  - 15 tags in order, each main tag read;
  - duplicates read only for 906, 898 and 900;
  - 7 of the 15 on a route list;
  - the NMS reports tag 18's duplicate missing at 11:12:33;
  - no route tag skipped.
- **The skip rule on real frames, with some left out** (none altered):
  tag 18's two reads and the SLRP frames from 11:12:38 on. That leaves 16
  then 906 under a route list holding 16, 18, 906, so 18 is named as
  passed without a read, at 11:12:49.
- The run summary section; the RFID lane on hover.
- `session143` (report wording), `session164`, `session97` pass.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 11/11; `dltests`
**232 suites / 6413 checks, 1 failed** (the macOS-only `session156` pty
check); menu audit passed; headless smoke alive.

---

<a id="session-179"></a>
## Session 179 — Session keys

Feature 9. NMS health reports the running key set (`CURRENT_RUNNING_KEY`)
and the key sets left (`REMAINING_KEY_NUMBERS`), and the loco logs its key
loads (@auth_keys1/2). In Abhinav's 2026-10-08 logs, around **every
restart** the NMS reports key set 30 with **"0 (No keys)" for a few
seconds**, and then key set 20 with 10 sets left. So "No keys" is the boot
state there, not keys running out.

- **Run summary**, a "Session keys" section: the fewest key sets left;
  the no-keys spells, each marked "(at a start of mission)" when it falls
  within a minute of one; the key loads; the changes of running key set.
- **Lanes**: no-keys spells along the top of the Link lane (accent
  colour), key loads as ticks (ok colour), named on hover.
- **A ready-made watch, "No session keys"**:
  `@nmshlth field:REMAINING_KEY_NUMBERS=0`. Its description says a hit
  next to a restart is the boot. (`~No keys` would split at the space
  into two terms, so the watch tests the number.)
- The section counts "4 spells", not "spell(s)", per session143's rule
  for the run report.

### Files

`runreport.{h,cpp}`, `laneband.cpp`, `watchrules.{h,cpp}`; tests
`test_session179.cpp` (new), `test_session161.cpp` (the new rule's
decoder-side definition), `tests.pro`.

### Tests

- `session179`, on the 10:52 excerpt, against `schema/engine.py`:
  - four no-keys spells (10:52:20, 10:52:46–48, 10:56:28–30,
    11:08:37–38), each within a minute of a start of mission;
  - fewest left 0; seven key-set changes, the first 30 → 20 at 10:52:45;
  - key loads at 10:52:36, 10:52:44 and 11:08:53;
  - the summary sentence;
  - the watch matches exactly the 9 frames reporting 0;
  - the Link lane on hover.
- `session161rules`: the new watch matches exactly the frames the decoder
  says (21 across the replay corpus).

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 11/11; `dltests`
**231 suites / 6403 checks, 1 failed** (the macOS-only `session156` pty
check); menu audit passed; headless smoke alive. A first run failed
`session143` on the "(s)" wording, fixed above.

---

<a id="session-178"></a>
## Session 178 — The loco's own SoS and collision detections

Feature 8. NMS health carries the loco's own SoS (`LOCO_SELF_SOS`: Manual
SoS, Manual SoS end, Unusual stop start, Unusual stop end) and
`COLLISION_DETECTION` ("513 (loco 2, code 1)"). 81_1 has 94 of them over
the day; nothing showed them as spells.

- **Episodes:** a start (Manual SoS / Unusual stop start) runs to its end.
  Repeats of either (the NMS sends each twice, a second apart) are folded.
  A start that is never ended runs to the end of the log, marked "(no end)".
- **Collision detections** become marks; repeats of the same value within
  5 s are folded.
- **Run summary:** a "Loco's own SoS" section (from, to, what) and a
  "Collision detection" section.
- **Lanes:** own-SoS spells along the top half of the Safety lane (accent
  colour), collision detections as ticks (error colour), each named on
  hover. EB/FSB (175) stay along the bottom.

On 81_1 at 11:03:34 the NMS reports a collision detection for loco 2, the
same second loco 2's SoS began as heard on its received ARPs (172), and
the four EBs follow (175).

### Files

`runreport.{h,cpp}`, `laneband.cpp`; tests `test_session178.cpp` (new),
`tests.pro`.

### Tests

- `session178`, against `schema/engine.py`:
  - on the 10:13 excerpt, one unusual stop, 10:13:36–10:13:59 (start and
    end each sent twice, folded), and no collision detection;
  - on the 10:52 excerpt, an unusual stop from 11:08:14 with no end, so it
    runs to 11:13:00 and says "(no end)", and one collision detection,
    loco 2, at 11:03:34 (two frames folded);
  - the run summary section;
  - the Safety lane names both on hover.
- `session164`, `session168`, `session175`, `session97` (lanes, reports)
  pass unchanged.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 11/11; `dltests`
**230 suites / 6390 checks, 1 failed** (the macOS-only `session156` pty
check); menu audit passed; headless smoke alive.

---

<a id="session-177"></a>
## Session 177 — GPS health (Radio and GPS health)

Feature 7. CCSYS carries both GPS receivers' state in every frame:
satellites in view, C/N0, link / PPS status, which GPS is active, and a
view letter. The radio window (173) becomes **Radio and GPS health**, with
three more strips on the same time axis:

- **GPS satellites** and **GPS C/N0** (max, dB-Hz), GPS-1 and GPS-2.
- **GPS problem**: spells where a GPS link or PPS is not ok, or not both
  GPS are active.
- The header counts the **view letter as reported**: GPS-1 "V" in all
  226 frames of the excerpt, and GPS-2 V 219, A 6, No Data 1. In NMEA, V
  usually means "void", but these frames read V with 11–12 satellites in
  view, so the console counts it and does not interpret it. Worth
  checking against the firmware.

**Fixed in 173's code:** several span trackers share one output list
(radio not OK: two CCSYS radios, no active radio, two NMS radios; now the
three GPS ones). Each extended the list's *last* span, which could be
another tracker's. The radio spells in 173's fixture are one frame long,
so it did not show there. Each tracker now extends its own span.

### Files

`radiohealth.{h,cpp}`, `radiohealthwindow.{h,cpp}`, `mainwindow_menus.cpp`
(menu text); tests `test_session177.cpp` (new), `test_session173.cpp`
(8 strips), `tests.pro`.

### Tests

- `session177`, on the 81_1 excerpt, against `schema/engine.py`:
  - satellites and C/N0 from all 226 @ccsys; GPS-1 sees 0–12;
  - 6 problem spells, all at the two restarts;
  - each spell keeps its own end: GPS-2 link+PPS to 11:09:04, only GPS-1
    active to 11:09:00. Before the fix these two swapped;
  - the view counts, the summary, the window's 8 strips, the hover text,
    and it fits a laptop.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 11/11; `dltests`
**229 suites / 6381 checks, 1 failed** (the macOS-only `session156` pty
check); menu audit passed; headless smoke alive.

---

<a id="session-176"></a>
## Session 176 — Cab inputs and outputs on a timeline

Feature 6. The DIO logs (IOA, over serial) record what the driver did and
what the unit drove: 69,200 @dip1 frames in 81_1, with DMI buttons
pressed, the cab switched forward / reverse, the horn, traction cut off.
**Tools ▸ Monitor ▸ Cab inputs and outputs…** draws them on the fault
timeline's chart (174), under the loco's mode:

- **One row per signal that changes in the tab.** Inputs come from
  @dip1 / @dip2 ("DIP1 cab1_reverse"); outputs from @dop1 / @dop2 by pin
  (the `output=` bit, "DOP1 pin 2"). A signal that never changes has no
  row.
- **A bar wherever the signal reads other than its usual value** (the
  value it holds most of the time), saying what it read ("reads 0
  (usually 1)"). That reads the same for normally-open and
  normally-closed contacts and for active-low feedback: a button press,
  the cab in reverse, traction cut off, a brake relay dropping. Which
  value is "pressed" is not interpreted.
- Hover, click to jump, Save image, scrolling as on the fault timeline.
- The chart's label column now fits the longest row name (170–260 px),
  on the fault timeline too.

On the fixture: the horn at 10:13:07; the cab switched to reverse three
times; traction cut off six times, with PVEF and the LE relay; the EB,
FSB, NB and SIFA/VEB relays dropping once at the 10:17 restart.

### Fixture

`replay/2026-10-08/loco_1_1_08102026_101300.cap` (new, 4,903 lines,
0.85 MB): 81_1 from 10:13:00 to 10:17:30, every packet type, capture lines
unchanged.

### Files

`iotimeline.{h,cpp}`, `iotimelinewindow.{h,cpp}` (new, in `dlcore.pri`),
`faulttimelinewindow.{h,cpp}` (label width), `mainwindow.h`,
`mainwindow_menus.cpp`, `mainwindow_tools.cpp`; tests `test_session176.cpp`
(new), `tests.pro`.

### Tests

- `session176`, against `schema/engine.py` with the same rule:
  - 42 changing signals and 126 bars;
  - cab1_reverse 3 bars reading 1 (usually 0);
  - traction_cutoff_fb 6 bars reading 0 (usually 1);
  - the EB relay once, the horn twice;
  - outputs by pin, and no row for a constant signal.
- The window: hover a bar, and it fits a laptop.
- `session174` passes with the wider labels.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 11/11; `dltests`
**228 suites / 6369 checks, 1 failed** (the macOS-only `session156` pty
check); menu audit passed; headless smoke alive.

---

<a id="session-175"></a>
## Session 175 — Brake reasons beside every EB / FSB

Feature 5. The console listed EB / FSB applications (DMI `brake_type` 4 / 3)
without saying why, though the capture often says. Every EB / FSB now
carries the reasons the capture gives, from three places. All of them are
observed, none inferred:

| Source | What |
|---|---|
| NMS | `BRAKE_APPLICATION_REASON` (@nmshlth) within 10 s of the onset: Overspeed, No LP Acknowledge, Loco Specific SoS, MBT selected, ... |
| DMI | the context message and system alarm shown during it: "SOS - Other Loco Manual", "System Fault, Isolate or Restart KAVACH", ... |
| DMI | `collision_loco_id`, when the DMI names another loco |

With none of them, the event says "no reason in the capture".

- **One function, `RunReport::brakeEvents()`**, shared by everything that
  lists brakes:
  - the **Run summary** gets an "EB / FSB applications" section with
    reasons;
  - the **Incident report**'s EB/FSB table gets a reasons column (its
    episodes are the same as before);
  - the **Mission report** lists each mission's brakes with reasons;
  - the **lanes** draw them along the bottom of the Safety lane (EB in
    the error colour, FSB in the warning colour), and hovering one gives
    its type, times and reasons.
- On 81_1 it explains the four EBs at 11:04–11:06: "SOS - Other Loco
  Manual", collision target loco 2. That is loco 2's manual SoS, heard as
  its EMERGENCY_STATUS 2 at the same time (172). The EB at the 10:52:36
  restart carries "System Fault, Isolate or Restart KAVACH" and NMS
  "Overspeed".

### Files

`runreport.{h,cpp}`, `incidentreport.cpp`, `missionreport.{h,cpp}`,
`laneband.cpp`; tests `test_session175.cpp` (new), `tests.pro`.

### Tests

- `session175`, on the 81_1 excerpt, against `schema/engine.py`:
  - five EB spells with their times;
  - the first carries the DMI alarm and NMS "3 (Overspeed)";
  - the other four carry the manual SoS and collision target loco 2, with
    no NMS reason.
- The run summary, the incident report around 11:05 and the mission
  report (one brake before the first start, four in mission 2) each list
  them with reasons.
- The lane band's 15-minute window holds four, and hovering the Safety
  lane at 11:04:20 names them.
- `session97`, `session133`, `session143` (incident and run reports)
  pass unchanged.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 11/11; `dltests`
**227 suites / 6358 checks, 1 failed** (the macOS-only `session156` pty
check); menu audit passed; headless smoke alive.

---

<a id="session-174"></a>
## Session 174 — Fault timeline

Feature 4. 81_1 carries 397 NMS fault frames over the day (up to 10 faults
each), and CCSYS reports the LCU elements going up and down. The Fault
panel (141) shows what is active *now*. **Tools ▸ Monitor ▸ Fault
timeline…** shows the whole tab as a Gantt chart:

- **The mode band** on top: the missions' modes (171: the DMI's, the ARP's
  while the DMI is silent), with every **System_Failure** as a dashed line
  through all rows.
- **One row per card / module**, a bar per fault from raised to cleared.
  An NMS fault clears when a later frame **from the same reporting
  subsystem** no longer lists it (the fault panel's rule); otherwise it
  is open to the end of the log. NMS bars are in the error colour.
- **LCU elements** (CCSYS `lcu_elem_status1/2`: can0, can1, radio1,
  radio2, gps1, gps2) get rows of their own, with a bar while the bit
  reads 0, in the warning colour.
- **Under the chart:** for each System_Failure, the faults raised at that
  moment.
- Hover a bar for the fault and its times; hover the mode band for the
  mode or the System_Failure; click to jump the log; Save image. The
  chart scrolls when the rows outgrow the window.

On the 81_1 excerpt it shows a burst at each restart. At 10:52:36 there
are 15 raised: 9 LCU elements down, the Comm card's 3.3 V and 1.2 V PS,
Radio-1, Radio-2 and GPS-2. There is also a long VCC Mc-4 safety error
from 10:57 to 11:08, while the loco was in Staff_Responsible.

### Files

`faulttimeline.{h,cpp}`, `faulttimelinewindow.{h,cpp}` (new, in
`dlcore.pri`), `mainwindow.h`, `mainwindow_menus.cpp`,
`mainwindow_tools.cpp`; tests `test_session174.cpp` (new), `tests.pro`.

### Tests

- `session174`, on the 81_1 excerpt, against `schema/engine.py` with the
  same rules:
  - 97 bars on 23 rows, 2 open at the end;
  - the first bar is Analogue Card Mc-2's safety error, 10:52:20–22;
  - LCU rows;
  - System_Failure at 10:52:36, 10:56:27 and 11:08:52, with 15, 1 (GSM-1)
    and 9 faults raised;
  - the list under the chart.
- The window: hover a bar, hover the band at a failure, and it fits a
  laptop.

**Gate** (macOS), Qt 5.15.19: validators 11/11; `dltests` **226 suites /
6346 checks, 1 failed** (the macOS-only `session156` pty check); menu audit
passed; smoke alive. Qt 6.11.2: the same, plus 175's half-written suite,
which that run picked up.

---

<a id="session-173"></a>
## Session 173 — Radio health

Feature 3 from the 2026-10-08 logs. In 81_1 the DMI showed no radio for
spells of up to 68 minutes, and the radio data was spread over four packets
nobody read together. **Tools ▸ Monitor ▸ Radio health…** shows them on one
time axis for the current tab:

| Strip | From |
|---|---|
| DMI signal, 0–5 bars | @dmi `signal_strength`; **no-radio spells** shaded behind it; **radio holes announced** ("Approaching Radio Hole" in `alarm_code`) marked along its top |
| Radio not OK | @nmshlth `RADIO_1/2_HEALTH` = 3 (Radio Fail); @ccsys `health_radio1/2` not OK, `active_radio` 0 |
| Temperature | @ccsys radio 1 / 2 and PA 1 / 2 |
| Forward power | @ccsys radio 1 / 2 |
| GSM RSSI | @dlsys GSM-1 / GSM-2 (99 = not known: a gap, not a value) |

- **The question it answers:** was a loss of radio a radio hole the loco
  had been told of? Each no-radio spell is "announced" when a radio-hole
  announcement was on the DMI during it or in the 2 minutes before. The
  header says how many were and were not. That is observed, not judged:
  an unannounced spell is listed, not called a fault.
- A spell ends when the condition stops or the frames go quiet for over
  10 s.
- Hover for every value at that moment; click to jump the log there; Save
  image.
- NMS health fields are events with meanings (eid). They come only as
  display rows, which `collectRowFields()` drops, so they are read
  directly.

### Files

`radiohealth.{h,cpp}`, `radiohealthwindow.{h,cpp}` (new, in `dlcore.pri`),
`mainwindow.h`, `mainwindow_menus.cpp`, `mainwindow_tools.cpp`; tests
`test_session173.cpp` (new), `tests.pro`.

### Tests

- `session173`, on the 81_1 excerpt, against `schema/engine.py` with the
  same spell rule:
  - 747 @dmi;
  - no radio 10:52:00–06, 10:52:20–40, 10:56:34–11:08:23 (the longest,
    11 min 49 s) and 11:08:37–45, none announced;
  - 10 radio-not-OK spells, all within seconds of the two restarts (the
    radios going down);
  - radio 1 at 36–37 °C over 226 @ccsys;
  - GSM-1 RSSI 99 throughout, so "not known".
- No capture holds "Approaching Radio Hole", so the matching rule is
  checked on spans set by hand (no bytes invented): a spell with an
  announcement exactly 2 minutes before, one with an announcement during
  it, and one with neither.
- The window: summary, five strips, the hover text, a click jumps, and it
  fits a laptop.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 11/11; `dltests`
**225 suites / 6332 checks, 1 failed** (the macOS-only `session156` pty
check); menu audit passed; headless smoke 500 datagrams, alive.

---

<a id="session-172"></a>
## Session 172 — Two locos from one log (received ARP)

Feature 2. The two-loco view (98) needed both locos' tabs, and its events
came from @lsos, which these logs don't have. But a loco's **received ARPs**
carry the other loco's ABS_LOCO_LOC, TRAIN_SPEED, LOCO_MODE and
EMERGENCY_STATUS about every 2 s (7,677 of them in 81_1). One log is
enough.

- **Loco B** also offers "**Loco 2, as heard by 81_1**", one entry for each
  other loco in each tab's received ARPs. Its trace is those ARPs.
  Excluded: the loco's own ID (its own ARP coming back, session 170) and
  frames whose CRC fails (3 of 305 in the fixture: a garbled ID or
  position is not another loco). With only one tab open, the window
  starts on that tab and the first loco it heard.
- **Events:** the heard loco's EMERGENCY_STATUS gives SoS (1, 2, 6),
  head-on (4) and rear-end (5) episodes. **Trip and System_Failure** are
  marked for both locos from their modes (any source, not only received
  ARPs): Trip in the error colour, System_Failure muted, with tooltips.
- The gap pairs samples within 3 s (received ARPs come about every 2 s).
  The plausibility warnings are unchanged.

### Files

`twolocoview.{h,cpp}`, `twolocowindow.cpp`; tests `test_session172.cpp`
(new), `tests.pro`.

### Tests

- `session172`, on `replay/2026-10-08/loco_1_1_08102026_105200.cap`, against
  `schema/engine.py` (8-byte received header):
  - loco 1 heard only loco 2; 302 samples (305 minus the 3 that fail
    their CRC, named);
  - located 161,099–161,694 m, 100 km/h at most, mode on each sample;
  - System_Failure from 10:58:33, no Trip; one SoS spell 11:03:33–11:06:41;
  - each gap is B minus A, both known; loco 1's own System_Failure is
    marked too.
- On the 81_2 excerpt, loco 2's heard locos are {1}: its own ARP coming
  back is not listed.
- The window's B picker entry, and its one-tab default.

**Gate** (macOS), 171 and 172 together, Qt 5.15.19 and Qt 6.11.2:
validators 11/11; `dltests` **224 suites / 6315 checks, 1 failed** (the
macOS-only `session156` pty check); menu audit passed; headless smoke
500 datagrams, alive. The first run picked up 172's half-written code and showed that the
new fixtures, sorting first in `replay/`, changed the inputs of
`lococonsolelive`, `slrpcarry` and `session92` (hence the subfolder).

### Found, not fixed

Today's CCSYS frames (81_1) do not round-trip through the Packet Maker's
encoder byte for byte (226 of 400 in `session92`'s corpus before the move).
The older captures' frames do. To be looked at with the radio panel
(feature 3).

---

<a id="session-171"></a>
## Session 171 — Missions, and the full mission report

Abhinav's request (feature 1 of 11 drawn from the 2026-10-08 logs): split a
day's log into missions and give a full mission report. **A mission starts
at a start of mission** (session 168: the first ARP in Stand_By with no
direction, tag or location after any other) **and ends at the next one**
(his rule). The last one runs to the end of the log, and traffic before
the first start is shown as "before the first start of mission".

**Tools ▸ Monitor ▸ Mission report…** (Ctrl+Alt+M) opens the report for
the current tab, with Rebuild, Copy and Save HTML (one file that prints
to PDF from a browser):

- **Summary table**, one row per mission: start, end, length, time from
  start to Staff_Responsible, highest mode, mode at the end,
  System_Failure / Trip / EB / FSB, highest speed, tags read, time without
  radio, NMS faults raised.
- **A section per mission**:
  - start-up phases: Stand_By (ARP), "System Self Test In Progress" and
    "Select Train Config" (DMI alarm messages), the first
    Staff_Responsible / On_Sight / Limited / Full_Supervision, any
    System_Failure;
  - the mode changes and the time spent in each mode;
  - the EB/FSB episodes;
  - speed and located span;
  - the RFID tags in order;
  - the NMS faults raised.
- **Where the modes come from.** The DMI is logged in bursts (in 81_1, a
  42-minute stretch had 36 @dmi frames) and is silent through a
  System_Failure restart. So a mission's modes are the DMI's `loco_mode`
  while it sends, and the loco's own ARP `LOCO_MODE` once the DMI has been
  silent for more than 3 s. Taking both at once would flicker at every
  change, because the ARP lags. Each mission says which it used.
- **A tab that has dropped its oldest rows** (its row capacity, 200,000 by
  default) is named in the report header, with the time it now starts:
  missions before that are not in it.
- Observed over the 81_1 day: most missions end in System_Failure seconds
  before the next start of mission.

The run summary window shows this report too (`RunReportWindow::Kind::Missions`),
so Save, Copy and Rebuild behave the same.

### Fixture

`replay/2026-10-08/loco_1_1_08102026_105200.cap` (new, 4,348 lines,
0.96 MB): 81_1 from 10:52:00 to 11:13:00 loco time, capture lines
unchanged, without the DIO, analog and LINFO lines (80% of the volume,
read by none of this). It sits in a subfolder: the older corpus suites
take "the first loco_1_1 capture" in `replay/`, and a 2026-10-08 name
sorts first. The 170 fixture moved there too. The validators read
`replay/*.cap` only, so they no longer see these two files. All 11 passed
over them before the move.

### Files

`missionreport.{h,cpp}` (new, in `dlcore.pri`), `runreportwindow.{h,cpp}`,
`mainwindow.h`, `mainwindow_menus.cpp`, `mainwindow_tools.cpp`; tests
`test_session171.cpp` (new), `test_contrastaudit.cpp` (the mission report's
HTML may carry colour literals, like the other standalone reports),
`test_session170.cpp` (fixture path), `tests.pro`.

### Tests

- `session171`, against `schema/engine.py`'s reading of the fixture with the
  same rule:
  - before the first start: Limited_Supervision, then Stand_By, then
    System_Failure at 10:52:36, one EB, self test from 10:52:21;
  - mission 1: 10:52:51 to 10:56:29, no @dmi, modes from the ARP,
    System_Failure at 10:56:27;
  - mission 2: train config from 10:56:34, Staff_Responsible at 11:00:50
    (start-up 4 min 21 s), its 15 tags in order, 4 EB, 110 km/h,
    System_Failure at 11:08:52 (ARP, DMI silent);
  - mission 3: from 11:08:56 to the end of the log, Staff_Responsible 14 s
    after the start, then On_Sight and Full_Supervision.
- The HTML (summary, sections, start-up time, mode source, verdict-free
  wording); the window builds it and saves it.
- Opt-in, not in the gate: `DL_MISSION_LOG=<log> DL_SHOTS=<dir>` writes
  the whole log's report. On 81_1: 9 missions in the 200,000 rows the
  tab keeps (13:11 onward).

**Gate**: run together with 172 (see 172).

---

<a id="session-170"></a>
## Session 170 — Received ARP from the loco's own ID: rejected

Abhinav's 81_2 capture of 2026-10-08 shows loco 2 hearing **its own ARP**
as a received ARP (566 times in 27 minutes). The rule was already in
`rejectrules.xml` (session 92: an `arprecv` whose SOURCE_LOCO_ID is the
loco's own, clause "DLConsole"). It was evaluated only in the Field
inspector, on the selected row. Now it acts where received ARPs are used:

- **Live Loco Console**: the received-ARP tab shows the last ARP from
  *another* loco. An own-ID one no longer replaces it. A first row reads
  "rejected: N received ARP(s) carrying this loco's own ID 2, last
  HH:MM:SS: not another loco, not shown here". The own ID comes from the
  loco's ARP / LSRP SOURCE_LOCO_ID. A received ARP that came in before
  the ID was known, and carries it, is withdrawn once the ID is learned.
  While following the cursor, an own-ID received ARP is skipped the same
  way. The Link tab still counts every frame received.
- **Run summary**: received ARPs are judged by the reject rules, in their
  own section: "Received ARP frames matching a reject condition: 43
  matches over 63 frames", with the own-ID spell's times and "the loco
  would not process these as another loco". Before the loco is
  identified the rule stays quiet, as it always has (the first own ARP
  in the excerpt arrives before the loco's first ARP and is not counted).

### Fixture

`replay/loco_2_1_08102026_162008.cap` (new, 2,297 lines, 383 KB): the
first two minutes of 81_2 (16:20:08–16:22:00 loco time), copied
unchanged from the capture lines of the log. It is the first capture in
`replay/` with received ARPs: 44 from loco 2 itself, 19 from loco 1. All
11 validators pass over it.

### Files

`runreport.{h,cpp}`, `lococonsolewindow.{h,cpp}`; tests
`test_session170.cpp` (new), `tests.pro`;
`replay/loco_2_1_08102026_162008.cap`.

### Tests

- `session170`: brute force from the decoder (8-byte received header):
  44 own, 43 of them after the ID is known, 19 from loco 1. The run
  report counts 63 judged and 43 own-ID matches, with no other clause;
  the report wording is checked. The console, before loco 1 is heard:
  all own ones rejected, including the withdrawn first one, and no
  SOURCE_LOCO_ID shown. After: the tab is loco 1's ARP, with "44
  received ARP(s)" above it.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 11/11; `dltests`
**222 suites / 6281 checks, 1 failed** (the macOS-only `session156` pty
check); menu audit passed; headless smoke 500 datagrams, alive.

---

<a id="session-169"></a>
## Session 169 — Live Loco Console: follow the cursor, save a snapshot

The DMI window could show the panel as it stood at any row or replay
cursor (84). The Live Loco Console showed only the latest live frame
of each packet. Now it can follow the cursor too, and save what it shows.

- **⏱ Follow cursor** (on the clock row, remembered as
  `lococonsole/followCursor`). Ticked, every packet tab (RFID, SLRP,
  LSRP, ARP, DMI, NMS, DIO, ...), the big numbers and the cab view show
  each type's **latest frame at or before the moment**. The moment is
  the row selected in any tab, or a replay window's cursor. The same
  rule and the same 10-minute look-back apply as on the DMI. Each tab's
  first row says how old its frame was then ("⏱ frame 14:18:58.000, 4.0 s
  before the moment"), and a type with nothing in the look-back says
  so. The clock readout shows the moment and where it came from. The
  tiles' ages count from the moment. The Link tab and the rates stay
  live. The loco pointed at is selected, including one the console
  never heard live (a replay's).
- **Snapshot…** (actions row) saves one of three things: an image of the
  window (.png); every packet's decoded fields for the selected loco as
  text (.txt, a section per packet, headed with the loco and the
  moment); or the same as CSV (`packet,field,value`, plus each packet's
  frame time when following). This works live or at a moment.
- Underneath: the time-travel moment (`DmiMoment`) now also carries
  every packet type's latest frame per loco (`latest`), from tabs
  (`dmiMomentFromModels`) and replay windows (`dmiMomentAt`). The DMI
  window's frames are unchanged. Nothing is computed unless a window
  follows.

The two new controls sit on different rows: neither row had room for
both at a laptop's width (session 150). The clock readout and the
start-of-mission chip (168) can shrink; their tooltips hold the full text.

### Files

`dmitimetravel.{h,cpp}`, `replaywindow.cpp`, `lococonsolewindow.{h,cpp}`;
tests `test_session169.cpp` (new), `tests.pro`.

### Tests

- `session169`, over `replay/loco_1_1_27062026_140226.cap` as a tab and
  as a replay:
  - the moment holds the latest of each type, at or before it,
    brute-force checked against the tab's rows;
  - the DMI's frames are unchanged;
  - the console follows a tab row: the SLRP tab is that frame field for
    field, with its age row; a later row moves the RFID tab to that
    moment's tag frame;
  - the tiles' ages count from the moment;
  - .txt, .csv and .png snapshots, with their content checked;
  - unticking goes live again and is remembered;
  - a replay's moment has each type once, and the console shows a loco
    it never heard live;
  - the console's minimum width stays ≤ 1100 while following.
- `session84` (DMI time travel), `session150`, `session168`, `session82`,
  `session124` pass.

Not built, from the request's third part: a *drawing* of RFID / SLRP
data beside its fields (the way the DMI draws @dmi). The Track diagram
(131) already draws the tags and MA. A new picture needs its scope agreed.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 11/11; `dltests`
**221 suites / 6270 checks, 1 failed** (the macOS-only `session156` pty
check); menu audit passed; headless smoke 500 datagrams, alive. (A first
Qt 5 run crashed in `session82` on stale test objects built against the
old `lococonsolewindow.h`; recompiled, it passes.)

---

<a id="session-168"></a>
## Session 168 — Start of mission, from ARP

A loco that has just powered up sends ARPs in **Stand_By with no
direction, no RFID tag and no location** (LOCO_MODE 1, MOVEMENT_DIR 0,
LAST_RFID_TAG 0, ABS_LOCO_LOC 0). Abhinav's frame of 2026-10-08
(`@arp_2_1 … 41683`, FRAME_NUM 59899, loco 2) is one. The console now
recognises that state (`CaptureDecoder::isStartOfMission`) and shows it in
four places:

- **On the row**: every such ARP gets a `start of mission` row in its
  decode ("…the loco reports it is at the start of a mission"). ARP
  only. LSRP carries the same fields, but this rule is ARP's.
- **Lanes**: each spell is marked on the MODE lane in the accent colour
  (at least 6 px wide), with a line at its start. Hovering it says when
  it started and what the next ARP reported ("then 2 (Staff_Responsible)
  after 14:07:30"), or that it was still so.
- **Run summary and Incident report**: a "Start of mission (ARP)"
  section (from, last such ARP, then). In the incident report each start
  is also a key moment, so the pack shows the DMI as it was then.
- **Live Loco Console**: a chip at the left of the actions row reads
  "start of mission --", then "⏵ at start of mission since 14:06:46",
  then "mission started 14:06:46 → Staff_Responsible". Its tooltip has
  the full times. The chip can shrink, so the console still fits a
  laptop (session 150's ≤ 1100 px).

A spell begins at the first such ARP after any other ARP (or the first
ARP seen) and ends at the next ARP that is not in that state.

### Files

`capturedecoder.{h,cpp}`, `runreport.{h,cpp}`, `incidentreport.cpp`,
`laneband.{h,cpp}`, `lococonsolewindow.{h,cpp}`; tests
`test_session168.cpp` (new), `tests.pro`.

### Tests

- `session168`: Abhinav's frame decoded bit for bit (CRC passes;
  Stand_By, 0, 0, 0, FRAME_NUM 59899, loco 2) and annotated; each of the
  four fields alone breaks the match; a missing field does too; a
  running-mission ARP and an LSRP are not annotated. Over
  `replay/loco_1_1_27062026_140226.cap`, the expected spells come from
  `schema/engine.py`: 14:06:46–14:07:30 then Staff_Responsible, and
  14:59:44 to the end. The run report finds exactly these; the incident
  report has the key moment and section; the lane band's 15-minute
  window shows the second in the accent colour with its tooltip; the
  console chip steps through its three states, and the console's
  minimum width stays ≤ 1100.
- `session150` (console width), `session82`, `session124` pass.

Six replay captures hold a start of mission; only one is used in the tests.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 11/11; `dltests`
**220 suites / 6245 checks, 1 failed** (the macOS-only `session156` pty
check); menu audit passed; headless smoke 500 datagrams, alive.

---

<a id="session-167"></a>
## Session 167 — DMI window: up to four locos, fields under each panel

The DMI window compared two locos (160) and had one Fields table, for
panel A, at the right. Now:

- **Panels: 1 / 2 / 3 / 4 locos** (a picker in the top bar; it replaces
  the "Two locos" tick box). One to three sit in a row; four make a
  2 × 2 grid (A B over C D).
- **Each panel has its own loco picker** above it ("B · Loco / Ctrl:"),
  its own status line, and starts on a loco no other panel shows. A
  newly seen loco goes to a panel that was sharing one.
- **Following the cursor**, every panel is drawn at the same moment: A on
  the loco pointed at, each other panel staying on its loco if that has
  a frame then, else moving to one that has.
- **Fields ▾ puts a decoded-fields table under each panel**, for the
  frame that panel draws. The "Where each region comes from" notes
  appear once, under all the panels.
- The panels keep their 4:3 shape. When the window is too short for
  them and their tables, the panel area scrolls rather than squashing
  them. The spare height goes to the tables, or below the panels when
  Fields is closed. Four panels need 806 px of width; a narrower window
  scrolls sideways.
- **Save image** saves the panels as laid out: in a row, or the 2 × 2 grid.
- The count is remembered (`dmi/panels`) and carried by File ▸ Export
  settings. A settings file from before this patch with "Two locos" on
  opens two panels; this patch also still writes `dmi/twoLocos` for
  older builds.

A status line wrapped onto two lines used to spill up over the soft
keys when the panel was squeezed. It now always keeps room for two lines.

### Files

`dmipanel.{h,cpp}`, `settingsbundle.cpp`; tests `test_session167.cpp`
(new), `tests.pro`.

### Tests

- `session167`: real @dmi frames from
  `replay/loco_{1,2}_1_27062026_140226.cap`. `replay/` holds @dmi from
  two locos only, so panels C and D are fed real loco-1 / loco-2 frames
  from other moments, re-tagged `@dmi_3_1` / `@dmi_4_1` (bytes
  unchanged). The checks: the picker; the 2 × 2 geometry; each panel on
  a different loco, drawing that loco's frame with its own status; each
  Fields table under its own panel and showing that panel's
  `train_speed`; the notes shown once; at a moment, a panel on A's loco
  moves to a free loco; the 2 × 2 image; three panels in a row with D
  hidden; Fields off hides the tables; the grid fits 1366 px, and a
  narrower window scrolls rather than clipping; the count remembered;
  an older `dmi/twoLocos` opens two panels.
- `session160` (two locos), `session92` (Fields), `session83`,
  `session84`, `session165`, `settingsbundle` pass unchanged.

Not tested: four *real* locos at one moment (no such capture).

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 11/11; `dltests`
**219 suites / 6214 checks, 1 failed** (the macOS-only `session156` pty
check); menu audit passed; headless smoke 500 datagrams, alive.

---

<a id="session-166"></a>
## Session 166 — DMI: a cross over the antenna when there is no radio

Region J (RF) drew five thin grey bars when `signal_strength` was 0,
which reads much like "low" at a glance. Now a **red cross over the
antenna** says plainly that there is no radio signal; with any bar
(1–5) there is no cross. On a stale panel the cross greys out with
everything else. The Fields notes ("Where each region comes from") say
so.

### Files

`dmipanel.cpp`; tests `test_session166.cpp` (new), `tests.pro`.

### Tests

- `session166`, real frames from `replay/loco_1_1_26062026_162418.cap`:
  @dmi #0 (`signal_strength` 0) draws red over the antenna; #289 (5)
  draws none; one bar draws none; stale draws none; the note is there.
- `session83` / `session160` (the DMI's earlier suites) pass unchanged.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 11/11; `dltests`
**218 suites / 6189 checks, 1 failed** (the macOS-only `session156` pty
check, as in 165); menu audit passed; headless smoke 500 datagrams,
alive.

---

<a id="session-165"></a>
## Session 165 — Export / Import settings: everything set up since

An audit of every INI key the code writes against what File ▸ Export
settings carried. Big numbers, Flasher profiles and Loco configurations
were already carried (the big-number tiles since 79, the files since 94);
much added since was not.

### Now carried

| Section | What | New |
|---|---|---|
| Pins, big numbers and the Loco Console | status pins, big-number tiles, pinned fields | **the Loco Console's cab fields, which panels show, change highlighting** |
| **Lanes over the log, custom lanes** (new) | lanes on/off, the custom lanes (164) | all |
| **Watches and saved filters** (new) | the Watch list (ready-made ones included), freeze-on-watch, saved filter presets | all |
| **Log columns, Find, DMI and display options** (new) | visible / hidden columns, widths, row density, UTC times, Find's options, the DMI window's (Annexure colours, follow cursor, two locos), offline thresholds, two-loco warning distance | all |
| Serial profiles, macros and port settings | profiles, macros, per-port / per-adapter | **the terminal's own: echo, line ending, hex view, timestamps, send as hex** |

Theme and appearance, tab tags, window layouts, Firmware Flasher profiles
and Loco configurations (with their locked fields, 158) are as before.

- **Applied at once** on import, no restart: lanes (every tab), the watch
  list, log columns / widths / density, UTC times, offline thresholds.
  Find, the DMI window and the two-loco view read theirs when they open;
  the Loco Console and serial terminal when they open, as before.
- Edit ▸ Undo puts an import back, as before.
- Files from before this patch import as they did (they just lack the new
  sections); a file from this one, read by an older DLConsole, has its
  new sections ignored (that was already the rule).

### Still not carried, on purpose

The machine's or the record's: network port, disk-log paths and quotas,
window positions outside layouts, session keys, histories, the per-tab
row capacity (memory), the query history, the last serial port used. And
**the Flasher's engineer/operator mode**: which mode a station runs in is
that station's decision; an import must not switch an operator PC to
engineer mode.

### Files

`settingsbundle.{h,cpp}`, `mainwindowsession.cpp`, `mainwindow_menus.cpp`
(the UTC action has an object name); tests `test_session165.cpp` (new),
`menuaudit_main.cpp`, `tests.pro`.

### Tests

- `session165`: a console with one value in every newly carried key
  (31 keys, in the shapes the code writes), exported, read, imported into
  another: every key arrives with its value and reads back as bool / int /
  double; the chooser's summaries; the Flasher's mode on the target is
  untouched; nothing of the machine's leaks (UDP port, disk root, row
  capacity, query history, last port); an older file still imports.
- Menu audit: a lanes import puts the custom lane on every tab's band at
  once; Ctrl+Z puts it back.
- `settingsbundle` and `session115` pass unchanged.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 11/11; `dltests`
**217 suites / 6181 checks, 1 failed** (the macOS-only `session156` pty
check); menu audit **174 checks, passed**; headless smoke 500 datagrams,
alive.

---

<a id="session-164"></a>
## Session 164 — Lanes: RFID as spans; custom lanes from any packet field

### 1. RFID, like Mode

- The RFID lane drew a 2 px tick per tag read, with the number beside it,
  and read as "a little line". Now each tag is a **span from its read to
  the next read**, named and coloured per tag, as the Mode lane holds a
  mode until the next change: the last tag read is where the loco last
  knew it was. The last tag's span runs to the end of the window.
- Hover: "RFID tag 18, read at 17:05:51; the last tag read until 17:07:02".

### 2. Custom lanes

- **Right-click the lanes ▸ Add lane ▸ packet ▸ field** (the packets and
  fields this tab carries, as in Plot field over time). **Right-click a
  custom lane ▸ Remove lane.**
- One list for all tabs (decided with Abhinav), saved as `ui/customLanes`.
  A tab whose traffic does not carry that packet does not show the lane.
  The band grows a lane per custom lane; the label column widens to the
  field name (up to 150 px).
- **Drawn by what the values are** (decided with Abhinav):
  - names (enums, flags: "6 (On_Sight)") → named spans, like Mode;
  - a measured number, with a unit ("15 km/h", "1200 m"), or a bare number
    taking more than 8 values → a small line graph, its value on hover.
    A special value among the numbers ("unidentified") is a gap in the
    line, not a reason to draw the lane as spans;
  - bare numbers with few values (flags, counters, statuses) → spans.
  A speed that stays at a few values is still a graph: the first rule
  ("more than 8 distinct values") drew a 0–15 km/h run as spans, and was
  replaced by the unit rule.
- The lane band only covers the newest 15 minutes (or 20,000 rows), as
  before, so a custom lane costs one field's decode over that window.
- MainWindow owns the list and hands it to every tab's band (the bands
  only ask), per the ownership convention.

### Files

`laneband.{h,cpp}`, `mainwindow.h`, `mainwindow_shell.cpp`,
`mainwindow_tabs.cpp`; tests `test_session164.cpp` (new),
`menuaudit_main.cpp`, `tests.pro`.

### Tests

- `session164`, real run `replay/loco_1_1_27062026_170217.cap` (7 tag
  reads, 6 mode changes in the window): between two reads the RFID lane
  is the first tag's span and is painted; the last span runs to the end;
  Add lane offers lsrp TRAIN_SPEED and LOCO_MODE; TRAIN_SPEED is a graph,
  LOCO_MODE spans, a packet the tab lacks is not shown; hovers give the
  value; right-click finds the custom lane; removing restores five lanes.
  Checked by eye on a screenshot.
- Menu audit: a lane added from one tab's band is on every tab's band and
  saved; removed from another tab, gone from all.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 11/11; `dltests`
**216 suites / 6169 checks, 1 failed** (the macOS-only `session156` pty
check); menu audit **171 checks, passed**; headless smoke 500 datagrams,
alive.

### Not tested

The right-click menu itself was not clicked in a test (its two choices
are, through `addableFields` / `customLaneAt` and the signals).

---

<a id="session-163"></a>
## Session 163 — session156 on CI: Windows fixed; the Linux flake made to say where it stops

### 1. Windows: the by-path check runs on Unix only

- **Cause.** `session156` "udev's by-path links give each device its
  socket" builds a fake `/dev/serial/by-path` with `QFile::link`. On
  Windows that makes `.lnk` shortcuts, not symlinks, so the reader found
  nothing. The console itself reads by-path on Linux only
  (`SerialScan::platformLocations`), so the check now runs on Unix only.
  This was the Windows CI failure since patch 156.

### 2. Linux: "then it is open, as before" — not fixed, instrumented

- Failed once on Linux CI (patch 162 run): the terminal still said
  "Opening /dev/pts/0…" with Cancel after 3 s. It passed on the 157, 158
  and 161 runs. **Not reproduced here**: 30 runs, six at a time under
  load, never stuck in Opening.
- From reading the code, one path would leave the window in Opening for
  good: the link closed while opening (its `finishOpen` drops the result,
  so no `openFinished` reaches the window). Nothing found closes it in
  this test, so that is **not confirmed** as the cause.
- The check now also prints the link's own state (open / opening) and the
  time since Open, so the next failure says whether the open never
  finished or the window missed it.
- On macOS the same check fails differently (the pty's DTR/RTS ENOTTY
  overwrites the status; the link itself is open, as the new text shows).
  Mac-only; left as it is.

### Files

`tests/test_session156.cpp`.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 11/11; `dltests`
**215 suites / 6152 checks, 1 failed** (the macOS-only one above, now
reading "… · link open · 3000 ms since Open"); menu audit 168 checks,
passed; headless smoke 500 datagrams, alive.

---

<a id="session-162"></a>
## Session 162 — session160 on Windows CI: the widen check follows the screen

- **Cause.** `session160` "the window widens by a panel" asked for at
  least 300 px more. Windows CI runs the tests on the native platform,
  where a window cannot grow past its screen, so the check failed there
  (Linux and macOS run offscreen, which does not clamp). The feature was
  right; the test pinned a width the CI screen cannot hold.
- **Now** the check is "a panel wider, or as wide as the screen allows"
  (the screen's available width, less 80 px for the frame).
- The same run's `linux-qt6` job was cancelled after six hours in its
  "Qt 6 and tools" install step: an install that hung, not a test.

### Files

`tests/test_session160.cpp`.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 11/11; `dltests`
**215 suites / 6152 checks, 1 failed** (the macOS-only `session156` pty
check); menu audit 168 checks, passed; headless smoke 500 datagrams, alive.

### Not tested

Not run on Windows here: the next CI run is the check.

---

<a id="session-161"></a>
## Session 161 — Watch panel: ready-made watches

- **Ready-made ▾** beside *Watch*: named Kavach conditions to tick on
  instead of writing the query. Each one becomes an ordinary watch (named,
  its condition in the list, beep / bookmark / every-time as usual,
  saved like any other). Unticking removes it (Ctrl+Z puts it back). A
  watch typed by hand with the same condition shows the rule ticked.
- The rules (`watchrules.cpp`), each defined the way the console already
  counts the same thing elsewhere:

| Rule | Condition | As in |
|---|---|---|
| EB applied | `@dmi field:brake_type=4` | incident report EB episodes |
| FSB applied | `@dmi field:brake_type=3` | incident report FSB episodes |
| SoS | `@lsos` | two-loco view SoS events |
| TSR acted on | `field:TSR_STATUS=2` | TSR entries act only at 2 |
| Trip / System failure / Isolation mode | `field:LOCO_MODE=7` / `12` / `13` | locoMode enum |
| CRC failed (DMI, RFID) | `(@dmi OR @rfid) field:CRC~FAIL` | the decoder's CRC row |

- **Two that were offered and are not here.** A watch is a condition on
  one frame. A *mode change* needs the frame before it, and *frame too
  old / too early* needs the arrival time against FRAME_NUM. Neither is
  imitated; the menu says so in a disabled line (the Run summary report
  lists mode changes).
- **CRC is DMI and RFID only.** Every recorded `@ccsys` (2,092) and
  `@dlsys` (1,050) frame fails its CRC check, as the Packet Maker round
  trip already noted. A watch there would fire on every frame and say
  nothing until that question is settled.

### Files

New `watchrules.{h,cpp}` (in `dlcore.pri`); `watchpanel.{h,cpp}`; tests
`test_session161.cpp` (new), `tests.pro`.

### Tests

- `session161rules`: every rule parses. Captures from `replay/` go
  through the real dispatcher until each rule real frames can show has
  been seen; each rule's match count must equal a count taken from the
  decoder's own rendered rows: EB 767, FSB 40, Trip 717, System failure
  44, CRC 2 (the two RFID tags recorded with a zero CRC). All equal.
- `session161panel`: the button and menu; ticking adds one named watch
  with the exact condition, saved; ticking twice adds nothing; a
  hand-typed equal condition shows ticked; unticking removes only that
  watch; the note on what a watch cannot do is there.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 11/11; `dltests`
**215 suites / 6152 checks, 1 failed** (the macOS-only `session156` pty
check); menu audit 168 checks, passed; headless smoke 500 datagrams, alive.

### Not tested

- **SoS, TSR acted on, Isolation**: no capture in `replay/` has an
  `@lsos` frame, a TSR sub-packet, or LOCO_MODE 13. Their conditions parse
  (the query refuses unknown field names), but no real frame shows they
  match. Worth checking on the first capture that has one.

---

<a id="session-160"></a>
## Session 160 — DMI window: two locos side by side

- **Two locos** (a tick in the DMI window's bar) adds a second panel, B,
  beside the first, with its own loco picker ("B:") and its own status
  line. The window widens by a panel rather than halving the first.
  Remembered (`dmi/twoLocos`).
- **Following the cursor**, both panels draw the same moment: A on the
  loco pointed at (as before), B on its chosen loco if that loco has a
  frame then, else on another loco that does. A moment with no frame from
  B's loco leaves B empty and says so; B is never drawn from an old frame.
- **Live**, each panel draws its own loco's latest frame; staleness is
  judged per panel.
- **Save image…** saves both panels side by side (`dmi_two_locos.png`).
- Scope (decided with Abhinav): one window with two panels. The Fields
  pane still shows panel A's frame.

### Files

`dmipanel.{h,cpp}` (drawing and status split into per-panel helpers,
`drawPanel` / `statusFor`); tests `test_session160.cpp` (new), `tests.pro`.

### Tests

`session160`, real frames from `replay/loco_1_1_27062026_140226.cap` and
`replay/loco_2_1_27062026_140226.cap`: one panel by default; Two locos
widens the window and starts B on the other loco; live, each panel draws
its own loco; at a moment, A and B show each loco's frame and B moves off
A's loco; no frame from B's loco leaves B empty with the reason; Save
image holds both; the setting is remembered. Sessions 83, 84 and 92 (the
DMI window's earlier suites) pass unchanged. Checked by eye on a
screenshot: loco 1 at 0 km/h beside loco 2 at 80 km/h, same moment.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 11/11; `dltests`
**213 suites / 6118 checks, 1 failed** (the macOS-only `session156` pty
check); menu audit 168 checks, passed; headless smoke 500 datagrams, alive.

### Not tested

Live two-loco traffic through the dispatcher (frames were fed to the
window directly); the look on Windows.

---

<a id="session-159"></a>
## Session 159 — Loco Configuration: compare two configurations

- **Manage ▸ Compare configurations…** Pick A and B (A starts as the
  configuration open in the editor, B as the next one). The table lists
  the fields whose values differ, A beside B, with each field's group,
  formatted as the editor shows them (hex, dotted IPv4). **Show all
  fields** lists the rest too, the differing ones bold and marked ●.
- **Swap** exchanges A and B; **Copy** puts the table on the clipboard as
  tab-separated text, headed by the two names, with the summary line.
- The summary: "N of M fields differ", or "All M fields are the same: …
  send the same bytes", or "A and B are the same configuration".
- A field locked in either configuration says so in its tooltip.
- Scope (decided with Abhinav): configuration against configuration only.
  Send targets and what was last sent are not compared (this PC's, not
  the loco's). The open configuration is saved first, so it is compared
  as it is on screen.

### Files

New `lococonfig/lococonfigcomparedialog.{h,cpp}`; `lococonfig/lococonfig.pri`,
`lococonfig/lococonfigwindow.{h,cpp}`; tests `test_session159.cpp` (new),
`tests.pro`.

### Tests

`session159`: loco 9 duplicated from loco 7 with its own unit id and
vcc_crc (values from the real `loco_defaults.json`): exactly those two
listed, hex as in the editor; Show all lists every field but the CRC;
identical and same-name pairs are said as such; Copy keeps the A/B order;
the Manage menu has the item.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 11/11; `dltests`
**212 suites / 6103 checks, 1 failed** (the macOS-only `session156` pty
check, as in 157/158); menu audit 168 checks, passed; headless smoke 500
datagrams, alive.

### Not tested

The dialog's look was not checked by eye or screenshot.

---

<a id="session-158"></a>
## Session 158 — Show on DMI; icons follow the theme; vcc_crc in the send box; locked fields

### 1. Right-click an @dmi row ▸ Show on DMI

- In a main-window tab and in a recorded-session (.dlr) tab, the row menu
  of an `@dmi` row has **Show on DMI**. It opens the DMI window (or brings
  the one already open to the front), sets it to follow the cursor, and
  shows the panel as it stood at that row, for that row's loco. Rows that
  are not `@dmi` do not get the item.
- The row's moment is offered again when the item is chosen, so it wins
  even if another row or a replay cursor was pointed at since.
- The window stays in "Follow cursor" afterwards (its checkbox says so),
  as when the operator ticks it by hand.

### 2. Icons follow a theme change

- **Cause.** The rail icons, the log header's More button and the search
  icons were drawn once, when the window was built, in that theme's text
  colour. After switching from a dark theme back to a light one they
  stayed light-on-light.
- **Now** `UiIcons::bind()` sets an icon and draws it again on every
  theme change (through `UiColor::onThemeChange`). Used by the rail, More,
  the command-search field (its muted text colour too) and the Search
  window's query icon.

### 3. vcc_crc in the send confirmation, every time

- Loco Configuration ▸ Send to VCC: the question itself now carries
  **vcc_crc and its value, bold and large**, with what it is (set by hand,
  must match this loco's VCC build) and its state: not sent before / the
  same as the last send / **Changed** since the last send (with the old
  value) / still the default value. Single and bulk sends alike.
- Before, it was one line inside the details text, among the changed
  fields. If the schema's LINFO has no `vcc_crc`, the box says so instead
  of leaving it out. `loco_info_crc` stays in the details.

### 4. Locked fields (Loco Configuration)

- Right-click a field ▸ **Lock field**. Locked, it shows a lock beside its
  name and cannot be edited in the table; Revert to default / last sent
  are off for it. **Unlock field…** asks first.
- Locks belong to the configuration (decided with Abhinav): saved in
  `loco_configs.json` as `locked_fields`, kept by Duplicate, carried by
  Export/Import. A lock on a field the schema no longer has is dropped.
- Loading values in bulk keeps locked values (decided with Abhinav):
  Reset to defaults, Import loco_info.bin, Load the loco's values, and
  loading a past send from History. The status line names the locked
  fields that were kept; the Reset question lists them.
- A **Locked fields** group in the group list, with its count.

### Files

`dmipanel.{h,cpp}`, `mainwindow.cpp`, `sessionwindow.{h,cpp}`,
`uistyle.{h,cpp}`, `mainwindow_shell.cpp`, `searchwindow.cpp`,
`lococonfig/lococonfigcore.{h,cpp}`, `lococonfig/lococonfigmodel.{h,cpp}`,
`lococonfig/lococonfigwindow.{h,cpp}`; tests `test_session158.cpp` (new),
`menuaudit_main.cpp`, `tests.pro`.

### Tests

- `session158icons`: a bound button and label are redrawn in each theme's
  colour, Dark → Light → Nord.
- `session158lockcore`: `keepLocked`; locks saved, reloaded (unknown and
  CRC locks dropped), carried by export/import.
- `session158lockwindow`: a locked field is not editable and setData
  refuses it; Reset to defaults keeps it and says so; unlocking asks (No
  keeps it); the lock survives closing and reopening.
- `session158vcccrc`: the real confirmation box, answered No: vcc_crc in
  the question (bold, large) when never sent, changed, with many changes,
  in a bulk send, and unchanged.
- Menu audit (real MainWindow): Toggle Dark/Light twice, the rail and More
  icons drawn in each theme's text colour; the row menu of a real `@dmi`
  frame (replay/loco_1_1_27062026_170159.cap) has Show on DMI and a plain
  row has not; it opens one DMI window, following, on that frame; a second
  time reuses the window.

**Gate** (macOS), Qt 5.15.19 and Qt 6.11.2: validators 11/11; `dltests`
**211 suites / 6092 checks, 1 failed**; menu audit **168 checks, passed**;
headless smoke 500 datagrams, alive. The one failure is the macOS-only
`session156` pty check noted in patch 157, unchanged.

### Not tested

- Show on DMI in the .dlr window was not clicked in a test (the same
  helper as the main window, which was).
- The new look of the confirmation box was checked by its text, not by
  eye on Windows.

---

<a id="session-157"></a>
## Session 157 — Qt 5 / MinGW build; Find searches what was there; plot menu

### 1. The build on Qt 5.15 with MinGW

- **`CM_DRP_LOCATION_PATHS` not declared** (`serialportscan.cpp`, from
  patch 156). Qt 5.15's MinGW 8.1 defaults `_WIN32_WINNT` to 0x0502 (XP),
  and its `cfgmgr32.h` then leaves this Vista-era property out. Defined
  as its fixed value (0x24) when the header lacks it.
- **`-Werror=mismatched-tags` rejected.** GCC has `-Wmismatched-tags` only
  from GCC 10; older ones refuse `-Werror=` of an unknown warning outright.
  `core/core.pro` now adds it for clang always and for GCC only from 10.
- `messageheader.cpp` includes `<QStringList>` itself.

### 2. Find on a live tab searches what was there when it ran

- **Cause.** Every arriving batch restarted Find's 200 ms debounce. Under
  steady traffic the timer never fired, so the search the operator typed
  waited for a gap in the packets: slow with the cable in, instant with
  it out.
- **Now** a search covers the rows present when it ran. Arriving rows
  start no scan and are not searched until the search is run again
  (typing, or changing an option). Rows falling off the front only
  renumber the matches; the current match stays on its message, or moves
  to the next if its row is gone. A filter, a re-sort or a reset still
  rescans, keeping position.
- The append-only tail scan is gone: nothing sets it now.

### 3. Plot field over time: the field menu is not deleted mid-click

Reported: choosing any field crashed the console. **Not reproduced here**,
on Qt 5.15 or Qt 6.11, offscreen, with AddressSanitizer, through the real
MainWindow and popup menus (28 fields, 14 packet types). One hazard was
found and removed: choosing a field rebuilt the field menu from inside the
click, deleting the submenu and action still delivering it. They are now
detached at once and freed with `deleteLater()`. Whether this was the
reported crash is **not confirmed**.

### Files

`core/core.pro`, `serialportscan.cpp`, `messageheader.cpp`, `findbar.h`,
`findbar.cpp`, `fieldplot.cpp`; tests `test_findlive.cpp`,
`test_comparefind.cpp`, new `test_session157.cpp`, `tests.pro`.

### Tests

- `findlivetraffic` / `test_comparefind`: arriving rows are not searched;
  searching again covers them and agrees with a cold bar. A search typed
  while batches arrive every 50 ms runs (the field report).
- `findliveeviction` (new): an evicted hit drops out, the others move up a
  row, the cursor moves to the next hit, every match is still a match.
- `session157` (new): fields clicked in the real popup menus, Field and
  Add, on `replay/loco_1_1_26062026_162418.cap`.

**Gate** (macOS):
- Qt 5.15.19 and Qt 6.11.2: validators 11/11; `dltests` **207 suites /
  6035 checks, 1 failed**; menu audit passed; headless smoke 500
  datagrams, alive.
- The one failure is `session156` "then it is open, as before", on macOS
  only: DTR/RTS set on a pty reports ENOTTY, and that error overwrites the
  "open" status after an async open. Patch 156's Linux gate had it green.
  Not fixed here.

### Not tested

- The MinGW and GCC < 10 fixes were not compiled with MinGW or old GCC
  (none here); the Windows CI job is MSVC.
- The plot crash was not reproduced; Qt 6.12 (the Qt Creator kit) and
  Windows were not tried.


---

<a id="session-156"></a>
## Session 156 — Serial: the right card after a reconnect; no GUI stalls

Three findings from the review of patch 155, all in the serial path.

### 1. Reconnect could put one card's lines into another card's tab

- **Cause.** Auto-reconnect (106) found a lost adapter by its USB serial
  number, and by its port **name** when it had none. Cheap CH340 / PL2303
  adapters have none, and clone FTDI chips often all report the same one.
  Pull two such adapters and plug them back in the other order: Linux
  swaps `ttyUSB0` / `ttyUSB1` (Windows can renumber COM ports the same
  way), and the "IOA Input" tab carried on with the **Output** card's
  lines, with nothing on screen to say so.
- **Fix, part 1 — know the adapter by where it is plugged in.** At each
  open, a scan records what the port's adapter is known by, strongest
  first:
  1. its USB serial number, **only when no other adapter present has the
     same one**. A shared clone serial identifies nothing and is not kept.
     This also applies to per-adapter settings and to profiles.
  2. its **USB socket**: `/dev/serial/by-path` on Linux, the device's
     location path from SetupAPI / the device tree on Windows. The socket
     stays the same whatever order things are plugged in.
  3. its model (USB VID:PID). After a move to another socket, it takes the
     only free adapter of that model, but only when no other lost port of
     the same model could claim it too.
  4. its name, as before, only for ports known by nothing else (built-in
     UARTs, virtual ports, macOS).

  All lost ports are planned in one go (`SerialScan::planReconnects`), so
  two of them can never be given the same adapter, and a guess that could
  equally fit another port is not made.
- **Fix, part 2 — check it is the same card.** After any reconnect, the
  port's lines are **held** until they show which card it is:
  - The first log line whose packet type the tab had before (`@dop1`,
    `@nmshlth`, …) confirms it. The held lines then go into the tab, in
    order.
  - Five log lines of types the tab never had (or a few, then 3 s of
    quiet) make the port **held as a possibly different card**. It stays
    open and visible in its terminal, but its lines are kept out of the
    console tab. The tab, the terminal and a notification say why, naming
    what it had and what came. **Feed anyway** in the terminal, or Close
    and Open, accepts it.
  - No log line at all within 3 s (a quiet or rebooting card): fed, and the
    tab says it could not be confirmed.
- **A profile that knows its adapter and cannot find it is no longer
  opened by its old name**, which may now be another card. It is reported
  as "its adapter (…) is not plugged in". Profiles also save the USB socket
  of an adapter that has no serial number.
- **Found while testing: a swap could free a live entry.** When a
  reconnected port took a name another port still held, that other
  port's entry lost its only owning pointer and was freed while its
  link's signal handlers still pointed at it. Entries are now owned for
  the manager's lifetime (`m_owned`); names are only an index.

### 2. The search for lost adapters no longer runs on the GUI thread

- Before: every second, **once per lost port**, `QSerialPortInfo::
  availablePorts()` ran on the GUI thread. That takes 50–500 ms on
  Windows, so with three IOA adapters out the console stuttered for as
  long as they stayed out. Opening a port also enumerated on the GUI
  thread to learn its serial number.
- Now `SerialPortScanner` enumerates on a thread of its own. There is one
  scan per tick for every lost port, and asks made while a scan runs are
  folded into one more scan.
- **Found while testing:** a scan that was already running when an adapter
  was pulled still lists it. It was taken as "the adapter is back". Now a
  lost port is only planned from a scan that **began after** the loss. On
  real hardware the stale reopen would have failed and been retried; with
  a device that lingers, it would have reopened a port that was gone.

### 3. Opening a port no longer freezes the window

- `SerialLink::open()` waited for the reader thread to open the port. A
  driver that takes seconds (a Bluetooth COM port, a port another program
  holds) froze the console for that long.
- **New `SerialLink::openAsync()`** opens on the reader thread and returns
  at once, followed by `opened()` or `openFailed()`. A Close (or another
  open) made meanwhile cancels it. A generation counter drops the late
  result, and the close, queued behind the open, closes what it opened.
- **Used by:**
  - the terminal's Open button (it reads **"Opening COM5…"** and becomes
    **Cancel**);
  - Find baud's final open;
  - the profile menu, Open all and the startup auto-open, through
    `SerialManager::openProfilesAsync()`, which finds every profile's
    adapter with one scan and reports once all have finished;
  - auto-reconnect.
- `open()` is unchanged and still blocking, for the tests and auto-baud's
  probing link.

### What the operator sees

- After replugging adapters, each IOA tab carries on with its own card. The
  tab line reads e.g. `── reconnected as /dev/ttyUSB1 (was /dev/ttyUSB0),
  gap 4.2 s, found by the USB socket it is plugged into ──`.
- If a tab would get another card's lines, it gets none:
  - its terminal shows **⚠ … it may be a different card …** with
    **Feed anyway**;
  - the status-bar chip reads **held**;
  - a notification says the same.
- Open, profiles and Open all never freeze the window; the terminal says
  "Opening…" until the driver answers.
- No more stutter while an adapter is unplugged.

### Files

- **New:** `serialportscan.h/.cpp` — `SerialPortSnapshot`,
  `SerialAdapterId`, `SerialScan::{scan, identify, planReconnects,
  locationsFromByPath, platformLocations}`, `SerialPortScanner`.
- `seriallink.h/.cpp`: `openAsync()`, `isOpening()`, `openFailed()`, the
  generation counter.
- `serialmanager.h/.cpp`:
  - scan-thread reconnect, `openAsync()`, `openProfilesAsync()`;
  - the card check (`isVerifying`, `isSuspect`, `feedAnyway`,
    `captureTypeOf`);
  - `m_owned`;
  - `usbSerialFor()` ignores shared serials; new `usbLocationFor()`;
  - `SerialProfile::usbLocation`.
- `serialconsolewindow.h/.cpp`: Open without waiting (`openPortAsync()`),
  Opening / Cancel, the held state and **Feed anyway**.
- `mainwindow.cpp`, `mainwindow_tools.cpp`, `mainwindow_menus.cpp`,
  `mainwindow_status.cpp`: profiles open without waiting, one report;
  a notification and a **held** chip for a held port.
- `serial.pri`: the new files; `win32: LIBS += -lsetupapi -lcfgmgr32
  -ladvapi32`.
- `tests/test_session156.cpp`, `tests/tests.pro` (in the `dl_serial`
  block).

### Tests

`test_session156`, **74 checks**. The ports are real ptys and the
enumerator is a stand-in (there is no USB in the test environment). Real
`@dop1` / `@dop2` / `@nmshlth` lines are taken from
`replay/loco_2_1_29062026_134128.cap`; there is no IOA input capture in
`replay/`, so the "other card" is played by `@nmshlth`.

- **Planner, on its own:**
  - a swap of two CH340s is resolved by socket;
  - clone serials are not kept;
  - a serial that becomes shared is settled by socket;
  - an adapter known by serial is not replaced by whatever has its name;
  - a move to another socket is matched by model, but not when two lost
    ports could claim it;
  - another model in its socket is refused;
  - a built-in UART is matched by name;
  - open ports are never handed out;
  - `/dev/serial/by-path` parsing, with a dangling link skipped.
- **Scanner:**
  - asking returns at once;
  - three asks during a scan make one more scan;
  - it runs off the GUI thread.
- **End to end (two ptys as two CH340s in two sockets):** pulled and
  replugged in the other order, each tab keeps its own card's lines,
  confirmed by the first line, and the tab says it was found by socket.
- **The card check (the old name-only finder, made to pick wrong):**
  - a banner is held, then flushed in order on a known type;
  - five lines of other types are held as a different card, and none
    reaches the tab;
  - the tab says why, and the terminal shows Feed anyway, which works;
  - a few strangers then quiet are held;
  - no line at all is fed with a note;
  - an explicit Open is not checked.
- **GUI thread:**
  - with three adapters out and a 400 ms enumerator, the worst gap
    between 10 ms GUI ticks over 2 s is under 150 ms, with 3–7 scans,
    not one per port per tick;
  - a scan that began before a loss does not reopen the port.
- **Open without waiting:**
  - with the reader thread busy for 600 ms, `openAsync()` returns in under
    100 ms and `open()` takes ≥ 450 ms (the old behaviour, kept for
    contrast);
  - cancel while opening;
  - a missing port reports `openFailed()`;
  - the terminal's button shows Opening / Cancel, then Close;
  - profiles: one found by socket under a new name, one whose adapter is
    missing **not** opened by its old name (which is another pty), and a
    second Open all leaves a running one running.

**Checked against broken code.** Each of these deliberate breaks fails the
suite:
- with socket matching off and the card check always confirming,
  **20 checks fail**;
- with the scan run on the caller's thread and `openAsync()` made to wait,
  **6 fail** (a 401 ms GUI stall; 601 ms and 501 ms opens);
- with the "began after the loss" rule off, the stale-scan check fails
  every time.

That last rule was found by the suite itself: 1 run in 6 failed with
"0 scans in 2 s", because the ports had already been reopened from a
stale scan. After the fix, 10 runs in 10 were green.

**Gate** on Qt 5.15.13 (Linux):
- validators 11/11;
- `dltests` **205 suites / 6024 checks**, 0 failed;
- menu audit 157/157;
- headless smoke alive.

**Gate** on Qt 6.4.2 (Linux): the same, 205 suites / 6024 checks, 0 failed;
menu audit 157/157; smoke alive. No compiler warnings from the touched
files on either.

### Not tested

- **No real USB adapter was used.** Sockets, serial numbers and models
  came from a stand-in enumerator, and the ports were ptys.
- **The Windows socket lookup** (SetupAPI location paths, walking up to
  the USB device for FTDI ports) was compiled and linked with MinGW
  (`x86_64-w64-mingw32-g++`, no warnings), but not run on Windows or built
  with MSVC. The Windows CI job is the first real check. On a machine
  where it finds nothing, ports with no serial number fall back to the
  model, then the name, as before, and the card check still guards the
  tab.
- macOS has no socket lookup (serial number, then model, then name).
- Not done: reacting to Windows device-change messages instead of the
  1-second poll. The poll no longer costs the GUI anything, so it was
  left out.


---

<a id="session-155"></a>
## Session 155 — Compare window aborted on open (Qt 6)

Reported by Abhinav, 2026-10-03: opening Tools ▸ Compare Tabs from a
Qt Creator (debug) build on Qt 6 aborted with `SIGABRT` in
`QObject::connect`, called from `CompareWindow::rebindPane`.

- **Cause.** `rebindPane` connected two lambdas (scroll bar → time-lock,
  selection → inspector) with `Qt::UniqueConnection`. Qt can only check
  uniqueness for a member-function slot. Qt 6 refuses a lambda here:
  - **Debug build:** it asserts, and the window aborts as it binds its
    first pane. This is the crash that was reported.
  - **Release build** (the gate's, and the 6.9 deployment machine's): it
    logs `unique connections require a pointer to member function` and
    **makes no connection**. On Qt 6 the Compare window's time-lock
    scrolling never followed, and selecting a row never reached the
    inspector, the raw panel or the bookmark action.
  - **Qt 5:** it accepted the connect and quietly added one more handler
    on every source switch.
- **Fix.** Each pane keeps its two `QMetaObject::Connection`s, and
  `rebindPane` disconnects them before connecting again.
- **What the operator sees:** on Qt 6, the Compare window opens,
  time-lock works, and a clicked row shows in the inspector. On Qt 5 it
  looks the same as before.
- **Why the gate missed it:** `test_session154` already opened this
  window with sources bound, but the assert is compiled out in release.
  It printed only a warning, which nothing checked.

Tests: `test_session155`, 11 checks, on 400 real lines of
`replay/loco_1_1_27062026_140226.cap`:

- with time-lock on, scrolling the left pane moves the right pane;
- selecting a bookmarked row relabels the bookmark action;
- six source switches leave the scroll-bar receiver count unchanged,
  and time-lock still works after them;
- Qt logs no `QObject::connect` warning;
- a source scan finds no `connect(..., lambda, Qt::UniqueConnection)`.
  This is the one check that catches the debug-only abort in a release
  build.

Against the unfixed code, 5 of the 11 checks fail on Qt 6.

Files: `comparewindow.cpp`, `comparewindow.h`,
`tests/test_session155.cpp`, `tests/tests.pro`.

Gate, on Qt 5.15.19 and on Qt 6.11.2 alike: 11/11 validators, `dltests`
204 suites / 5950 checks, menu audit 157/157, headless smoke: all green.

Not tested: the fix was not re-run in a debug (Qt Creator) build. The
release Qt 6 checks cover the same code path, and the source scan
covers the assert itself.


---

<a id="session-154"></a>
## Session 154 — Compare panes hide Source and Name

Abhinav's decision, 2026-10-03. It was open since session 129, and the
review pass (153) showed the cost.

- **Each Compare pane hides the Source and Name columns.** A pane shows
  one source, named in its picker above, so those columns held one value
  on every row. In two panes at 1100 px they took about 200 px and left
  Message about 35 px, and both panes scrolled sideways. Message now gets
  231 px, with no sideways scroll.
- **The rule changes for Compare only.** `test_comparetools` pinned
  "hidden columns are the setting's business, not the pane's"; it now
  reads: the setting decides, except that a pane always hides Source and
  Name. Message is still never hidden.
- **Kept as they were:**
  - the log tabs follow View ▸ Columns;
  - the Merged window shows Source and Name, since it mixes sources;
  - copies and exports from a pane: no code reads column visibility, so
    a copied row still has every column.

The panes re-hide both columns when rebound to another source: setting
the model resets the header, which is why the widths are re-applied
there too.

Tests: `test_session154`, 12 checks, on 400 real lines of
`replay/loco_1_1_27062026_140226.cap` across two sources:

- in both panes at 1100 px, Source and Name are hidden, while Time and
  Message are kept;
- Message is at least 200 px wide;
- no sideways scroll;
- layout audit;
- Merged still shows Source.

`comparetools` (14 checks) passes with its check updated.

Files: `comparewindow.cpp`, `tests/test_comparetools.cpp`,
`tests/test_session154.cpp`, `tests/tests.pro`.

Gate, on Qt 5.15.19 and on Qt 6.11.2 alike: 11/11 validators, `dltests`
203 suites / 5939 checks, menu audit 157/157, headless smoke: all green.


---

<a id="session-153"></a>
## Session 153 — the review pass after the revamp

Every window was shot again in dark and light, 29 of them, to catch
what had drifted across 36 patches. Two findings, both fixed:

- **The serial terminal was 1480 px wide at its minimum**, over a 1366-px
  laptop. One row held:
  - the port and its refresh;
  - the baud and Find;
  - data bits, parity, stop bits and flow;
  - Low latency, DTR and RTS;
  - Open.

  The line settings (data bits through RTS) now have a row of their own,
  under the port and baud. Now 811 x 461.
- **A checkable group box's switch had no stylesheet rule**, and when off
  it was drawn dark on dark: Packet Maker's "Also send to", "Extra header
  fields" and "Fill from buffer" looked like plain labels. It now shares
  the checkbox's rules: the outlined box, the accent fill when ticked,
  hover and disabled.

**Seen, not changed:**
- **Compare panes:** in two panes at 1100 px, Message (the column that
  matters) gets about 35 px and the panes scroll sideways. Source and
  Name, which hold one value per pane, take 200. This is the open
  decision from session 129, left for Abhinav.
- **Replay (1269 px) and Packet Maker (1127 px)** fit a 1366-px screen,
  as their passes (136, 128) intended.

Tests: `test_session153`, 4 checks:

- on all 11 themes, a group box's switch has the checkbox's rules,
  unticked and ticked;
- Packet Maker shows its switched sections;
- the serial terminal fits a laptop;
- the serial terminal passes the layout audit.

**Screenshot harness:** no new case; the pass used the existing 29.

Files: `serialconsolewindow.cpp`, `uistyle.cpp`,
`tests/test_session153.cpp`, `tests/tests.pro`.

Gate, on Qt 5.15.19 and on Qt 6.11.2 alike: 11/11 validators, `dltests`
202 suites / 5927 checks, menu audit 157/157, headless smoke: all green.


---

<a id="session-152"></a>
## Session 152 — the filter bar, one row or two; Message never scrolls sideways

Asked for after the revamp finished (2026-10-03).

- **The text box keeps 200 px.** On one row, the severity/direction
  chips (321 px) and the count left the filter's text box about 40 px
  wide in any pane under about 960 px: the main window's log tab, and the
  recorded-session window. When one row would leave it less than 200,
  the chips and the count now move to a second row under it. Wide panes
  look as before.
- **The bar's floor is the two-row width**, 583 px (was 846). That 846-px
  floor held every window with a log table at least that wide. The
  recorded-session window's minimum goes from 1146 to 883. In the main
  window the Sources dock now gets its preferred width: its header no
  longer reads "Soure".
- **No sideways scroll from a stored Message width.** The stored column
  widths were applied to every column, including Message, the stretched
  last column. A stored width set on it held until the next resize, so a
  pane narrower than the one it was saved in scrolled sideways: in the
  main window's log tab at 1440 px, Message stayed at 350 px in a 790-px
  view. Message is no longer given a stored width; it fills what is
  left.

Unchanged: every control, its wiring and its order. The chips, the count
and the query-error label keep their parent; only their row changes.

Tests: `test_session152`, 13 checks, on 200 real rows of
`replay/loco_1_1_27062026_140226.cap`:

- one row at 1300 px, chips beside the text box;
- two rows at 790 px, the text box at least 200 px, chips and count on
  the second row and the count shown whole;
- the floor at most 650, and the text box keeps 200 px at the floor;
- layout audit;
- back to one row when wide again;
- a stored 900-px Message width does not make a 700-px table scroll
  sideways.

`layout` and `session144` pass unchanged.

**Screenshot harness:** the main-window shot prints its log table's
geometry and header with `SHOT_DUMP=1`.

Files: `filterbar.h/.cpp`, `logtableview.cpp`,
`tests/test_session152.cpp`, `tests/screenshot_main.cpp`,
`tests/tests.pro`.

Gate, on Qt 5.15.19 and on Qt 6.11.2 alike: 11/11 validators, `dltests`
201 suites / 5923 checks, menu audit 157/157, headless smoke: all green.


---

<a id="session-151"></a>
## Session 151 — UI revamp, the small dialogs; two more Linux fits

**Session key:**

- **The two key sets sit side by side.** Stacked, the dialog was
  533 x 744 at its minimum, over a 768-px screen. Now 996 x 588.
- **Keys in UiStyle's mono**, each field wide enough for all 32 digits.
  `QFont("monospace")` came out proportional on the Mac. The live key
  and the derived key too.
- **"Randoms & ids"**: the & was taken as a mnemonic and showed as
  "Randoms _ids".
- **Derive session key is the primary button.**

**Settings:**

- **The disk-log root takes the row's spare width**, at least 240 px. It
  shrank to its hint and showed "S/LOGS".
- **The rows built as wrapper widgets** (UDP port, Receiver queue cap,
  Disk-log root) lose the default 11-px margins. Those made them taller
  than the others, with their fields 8 px to the right.

**Export:** the column names and their notes are in two columns. They
were one string padded with spaces, which lines up only in a monospace
font.

**Go to timestamp:** unchanged. Checked to fit and pass the layout
audit.

**Two more Linux fits.** CI on Linux failed after 149 and after 150:

- **Braking panel: 1112 px with Linux's fonts.**
  - The tab box's floor is 100 px (was 120).
  - The two spacers are gone.
  - "Show frame in log" is now "Show in log", with the full words in its
    tooltip.
  - Mac minimum: 948 (was 1036).
- **Loco Console with Big numbers: over 1100 px with Linux's fonts**
  (1058 on the Mac). Each tile was as wide as its detail line
  ("FRAME_NUM 50577 · arp · 97d 08h ago"). The detail now wraps instead,
  the tiles share the row evenly, and a long detail takes two lines.
  Tried and dropped: a small minimum width without wrapping, which cut
  the line ("· 97c"). The panel's minimum went from 1038 to 743; the
  window is 996, set by the console's header row.

Tests: `test_session151`, 18 checks:

- Session key:
  - fits;
  - the sets side by side;
  - the escaped &;
  - four mono key fields showing 32 digits;
  - the primary button;
  - layout audit.
- Settings:
  - fits;
  - the path has room;
  - every field starts at the same x;
  - layout audit.
- Export: seven columns, no padded names, each note beside its box,
  layout audit.
- Go to timestamp: small, layout audit.

`session150` now also checks that each tile's detail is shown whole, and
prints the minimum when it fails.

**Screenshot harness:** `SHOT_WINDOW=settings|export|sessionkey|gototime`.
Small dialogs are shot at their own size; `SHOT_SIZE=WxH` forces one.

Files: `sessionkeydialog.cpp`, `settingsdialog.cpp`, `exportdialog.cpp`,
`brakingpanel.cpp`, `bignumberpanel.cpp`, `tests/test_session150.cpp`,
`tests/test_session151.cpp`, `tests/screenshot_main.cpp`,
`tests/tests.pro`.

Gate, on Qt 5.15.19 and on Qt 6.11.2 alike: 11/11 validators, `dltests`
200 suites / 5910 checks, menu audit 157/157, headless smoke: all green.

**This finishes the window-by-window revamp** begun in session 117.


---

<a id="session-150"></a>
## Session 150 — UI revamp, tool windows 27: Big numbers, and every toggle

The Big numbers panel (in the Live Loco Console) was sound:
- large values;
- muted when stale;
- the source and its age under each value;
- the time spent in a state.

Its button was not.

- **A toggle that is on now looks on, app-wide.** A checked push or tool
  button looked exactly like an unchecked one. The Loco Console's "Big
  numbers", "Cab view" and "Record", the DMI panel's "Fields ▸", and
  every other toggle never showed whether it was on. Now an accent fill
  with an accent border.
  - It tints towards the accent (the tab underline, the ticked box), not
    the palette's Highlight, which on the dark themes is itself dark: the
    first try was all but invisible there.
  - The fill is as strong as the button text's contrast floor allows on
    each theme: 4.5:1, or 7:1 in High Contrast.
  - The segmented control and the side rail keep their own checked look.
  - The DMI display itself is drawn, not styled: unchanged.

Tests: `test_session150`, 10 checks:

- on every one of the 11 themes, a checked button is styled, differs
  visibly from an unchecked one, and its text meets the contrast floor;
- the Loco Console with real traffic
  (`replay/loco_1_1_27062026_140226.cap`) and Big numbers on: the panel
  shows, its button is checked, the default tiles, the mode tile reads
  "Trip", fits a laptop, layout audit.

`contrastaudit` (407 checks) passes unchanged.

**Screenshot harness:** `SHOT_WINDOW=loco SHOT_BIG=1`.

Files: `uistyle.cpp`, `tests/test_session150.cpp`,
`tests/screenshot_main.cpp`, `tests/tests.pro`.

Gate, on Qt 5.15.19 and on Qt 6.11.2 alike: 11/11 validators, `dltests`
199 suites / 5891 checks, menu audit 157/157, headless smoke: all green.

CI note: patch 148's run was green on all three jobs (Windows, Linux,
Linux Qt 6), the first since patch 133.


---

<a id="session-149"></a>
## Session 149 — UI revamp, tool windows 26: the Braking panel

- **Fits a laptop.** Its minimum was **2042 px** wide: the one-line
  summary under the scrubber ("20 frames · 20 cycles · … · spanning
  19.0 s") set it, so the window could not be narrower than two screens.
  Now 1036 x 491 on the Mac:
  - the summary wraps;
  - the frame/time readout ("frame 20 of 20 host … rtc … seq …"),
    Changes only and Follow live moved to a line under the slider, which
    now has the width;
  - the top row's tab box has a 120-px floor (was 180), and its two
    spacers are 8 px (were 16).
- **The tables get half the height.** At 3 : 2 they showed 3 of the
  summary's 10 rows under a plot with room to spare. Now even, and 7 in
  view.
- **The Field / Value table gets a share of the width**, up to its
  420-px cap. At 256 px it cut "Value" and scrolled sideways; the values
  now have 228 px.
- **Counts in English:** "up to 1 target each", "1 frame/s", "1
  change" (were "1 targets", "1 frames/s").

Tests: `test_session149`, 15 checks, on the **synthetic** @uba fixture
(`schema/fixtures/uba_synthetic.log`, 20 frames):

- fits;
- the summary wraps and is shown whole;
- "up to 1 target each" and "1 frame/s";
- the readout sits under the slider, which has the width;
- at least six summary rows in view;
- no sideways scroll;
- the values have room;
- layout audit.

**Not tested:** real braking curves. No capture in `replay/` carries
@uba, so the layout was checked on synthetic frames only. No other suite
covers this panel.

**Screenshot harness:** `SHOT_WINDOW=braking` (synthetic @uba), and
`SHOT_ROWS=1`, which prints each row of a main window's central layout
with its minimum width and its items (it found the top row at 1096 px).

Files: `brakingpanel.cpp`, `tests/test_session149.cpp`,
`tests/screenshot_main.cpp`, `tests/tests.pro`.

Gate, on Qt 5.15.19 and on Qt 6.11.2 alike: 11/11 validators, `dltests`
198 suites / 5881 checks, menu audit 157/157, headless smoke: all green.


---

<a id="session-148"></a>
## Session 148 — UI revamp, tool windows 24: Speed/distance

- **0 m is "location not known" here too**, as in the track diagram
  (131), the two-loco view (132) and the incident report (133). Asked
  again for this window and confirmed, 2026-10-03. On the real run, 352
  of 826 samples were at 0 m. Plotted as a place, they stretched the axis
  from 0 km and squeezed the run (153–161 km) against its far end. They
  are left off now, and the status says so: "352 samples at 0 m
  (location not known) left off". The axis, the view, Fit and the CSV
  export cover only the known locations.
- **One `SpeedDistance::knownOnly()` for this window and the incident
  report.** The incident report had its own copy (133). The shared one
  recomputes the span, the direction, the top speed, the overspeed count
  and the distinct targets from the known samples. A target first seen
  at a 0 m sample but also seen later is now kept; the copy dropped it.
  `extract()` is unchanged: the track diagram and the two-loco view
  count the 0 m samples themselves.
- **Counts in English:** "474 samples from @dmi", "1 sample above
  permitted", "3 rows skipped" (were "%1 samples" / "%1 rows" whatever
  the number).
- **The mouse hint fits one line at 1100 px** with the Mac's and Windows'
  fonts. It ends "Click: open the message". With Linux's wider fonts it
  wraps, and is shown whole.

Also: `session147`'s hint check failed on Linux CI. It wraps there, and
that is allowed. It and this patch's check now test that the hint is
shown whole, not that it is one line.

Tests: `test_session148`, 14 checks, on the whole of
`replay/loco_1_1_26062026_162418.cap`:

- `extract()` keeps the 0 m samples;
- `knownOnly()` drops exactly those and counts them;
- the span is 150–162 km, not from 0;
- no target placed relative to 0 m, and the real ones kept;
- the window's axis and view start at the run;
- the status text;
- the hint is shown whole, wrapped or not;
- fits a laptop with room for wider fonts;
- layout audit.

`session81`, `session97`, `session98` and `session133` pass unchanged.

**Screenshot harness:** `SHOT_WINDOW=speeddist` (`SHOT_CAP` picks the run).

Files: `speeddistance.h/.cpp`, `incidentreport.cpp`,
`tests/test_session148.cpp`, `tests/screenshot_main.cpp`,
`tests/tests.pro`.

Gate, on Qt 5.15.19 and on Qt 6.11.2 alike: 11/11 validators, `dltests`
197 suites / 5866 checks, menu audit 157/157, headless smoke: all green.


---

<a id="session-147"></a>
## Session 147 — UI revamp, tool windows 23: Field plot; two more Linux fits

**Field plot:**

- **Round numbers on a wide whole-number axis.** Past a step of 500, the
  value ticks fell back to the range divided by the label count:
  ABS_LOCO_LOC read 36121, 72242, 108363, 144484. They now take the same
  1-2-5 step as a continuous value: 0, 50000, 100000, 150000.
- **At least two labels per lane.** Rounded up, a step could overshoot
  the lane and leave only "0" on its axis. It now steps down the 1-2-5
  ladder until two land.
- **The status counts in English:** "588 points from 588 decoded rows"
  (was "point(s)" / "row(s)").
- **The mouse hint fits one line at 1100 px.** It wrapped to two. Now:
  "Shift+wheel: zoom values", "Drag: zoom to a box", "Click a point:
  open its message".

Unchanged: 0 m in ABS_LOCO_LOC is plotted. This window plots decoded
values as they are; the "0 m = not known" decision was for the location
views.

**Two more Linux fits.** CI after 146 passed the two old failures; two
new checks failed there:

- **Field Sweep:** the base values' value column was clipped by the
  names. The table's minimum now fits the packet's names plus a 24-bit
  value.
- **The Flasher's caution** wraps at 1100 px on Linux, as designed. The
  check is now that it fits its bar: one line, or two.

Tests: `test_session147`, 16 checks:

- 1-2-5 steps for the padded ABS_LOCO_LOC range at 3 to 6 labels, and
  never over-labelled;
- 1..6 still every value;
- on the whole of `replay/loco_1_1_26062026_162418.cap` with two series:
  no "(s)" in the status, "N points from N decoded rows", the hint fits
  one line, fits a laptop with room for wider fonts, layout audit.

`fieldplot` (52 checks) passes unchanged.

**Screenshot harness:** `SHOT_WINDOW=fieldplot` (`SHOT_FIELD`,
`SHOT_FIELD2`).

Files: `fieldplot.cpp`, `fieldsweepdialog.cpp`,
`tests/test_session146.cpp`, `tests/test_session147.cpp`,
`tests/screenshot_main.cpp`, `tests/tests.pro`.

Gate, on Qt 5.15.19 and on Qt 6.11.2 alike: 11/11 validators, `dltests`
196 suites / 5852 checks, menu audit 157/157, headless smoke: all green.


---

<a id="session-146"></a>
## Session 146 — the Linux CI gate, red since patch 134

CI's `linux` job (Qt 5 on Ubuntu) had failed on every push since patch
134. Windows and Qt 6 stayed green, and the local gate runs on the Mac, so
nobody saw it. Two size checks failed:

- **The Flasher's minimum with Linux's fonts was 1144 x 709**, over the
  1100 x 700 that session 134 pins.
  - **Width** came from the Flashing page, hidden behind the queue. Its
    legend ("Acknowledged / Resent this round / Still missing (hatched)")
    and its block-map caption sat on lines that could not shrink. The
    app bar mid-run added the caution beside the target, a 170-px
    profile box, and a real target such as `192.168.100.200:50000`,
    107 px wider than the test's `127.0.0.1:9`, which would have pushed
    it over on any platform.
  - **Height** came from spacing.
- **The recorded-session window's minimum grew on the first row
  clicked** (session 144's own check, on Linux). The raw panel's header
  label widened from "(no row selected)" to a frame's header.

What changed:

- **Flashing page.**
  - The legend entries and the caption stay whole at any normal width
    but are no longer the card's floor.
  - The main card is 10 px between rows, not 14, with 14-px margins
    top and bottom, not 18.
- **App bar.**
  - The caution ("Flashing — keep the chassis powered") wraps to two
    lines only when the bar is short of room. Its stretch keeps it on
    one line whenever it fits.
  - The profile box's floor is 120 px, not 170.
- **Queue.**
  - The SHA-256 line is a size smaller (0.88).
  - Column spacing is 10, not 16, and the card's is 6, not 10.
  - Both pages have 12-px margins top and bottom.
- **Raw bytes panel** (live window and recorded session alike): the
  header label is as wide from the start as with a frame in it.

On the Mac the Flasher's minimum went from 1090 x 691 to 983 x 671 in
all three states (queue, flashing, summary). The screenshots look as
before.

**Not tested:** Linux itself, which is not on this machine (Docker was
not running); CI is the check. The margins are sized to what Linux
added before: +54 px wide, +15 px tall.

Tests: `test_session146`, 14 checks, on the session-134 bench fixture:

- the hash is a size smaller;
- the legend and the caption are not the card's floor;
- the caution is on one line at 1100 px;
- with a real target, the bar's minimum is at most 1000 and the
  window's at most 1100.

`session144`'s width check is now "showing a frame does not widen the
window", which holds on any font.

**Screenshot harness:** `SHOT_WIDE=<px>` and `SHOT_TALL=<px>` print
every widget whose minimum is over that width or height
(`SHOT_HIDDEN=1` includes hidden ones: the Flashing page behind the
queue was the one setting the width).

Files: `flasher/flasherqueuepage.cpp`, `flasher/flasherflashingpage.cpp`,
`flasher/flasherwindow.cpp`, `rawbytespanel.cpp`, `sessionwindow.cpp`,
`tests/test_session144.cpp`, `tests/test_session146.cpp`,
`tests/screenshot_main.cpp`, `tests/tests.pro`.

Gate, on Qt 5.15.19 and on Qt 6.11.2 alike: 11/11 validators, `dltests`
195 suites / 5836 checks, menu audit 157/157, headless smoke: all green.


---

<a id="session-145"></a>
## Session 145 — UI revamp, tool windows 22: Field Sweep

- **Setup in two columns over the results.** Stacked, the Packet, Sweep
  and Send boxes took 787 px at their minimum, more than a 768-px screen.
  They left the results table one row, and the base values one field of
  lsrp's 29. Now Packet is on the left and Sweep + Send on the right. The
  minimum is 958 x 495, and at 1100 x 700 the results show 8 rows and
  the base values 6.
- **The base values span their box.** The column fits the field names
  ("SOURCE_LOCO_..." was cut), and the value takes the rest.
- **Range and List show only in the modes that read them.** Boundary
  values and Enum codes take their values from the field itself.
- **Preview plan and Start sweep sit on one row with the status.** Start
  is the primary button.
- **Results** are in UiStyle's mono, not `systemFont(FixedFont)`, which
  can come out proportional. Every column but Seen fits its text ("Sent"
  read "23:32:03....").
- **Timing** spin boxes keep their size. The MAC key field is wider than
  the key-set box beside it.
- **Counts in English:** "5 values for PKT_TYPE", "5 values sent" (was
  "value(s)").

Tests: `test_session145`, 22 checks, seeded from the first `@lsrp` of
`replay/loco_1_1_27062026_140226.cap`:

- fits;
- at least five base values and five result rows in view;
- names whole;
- the left column;
- mono results with fitted columns;
- Range / List by mode;
- the plan text;
- the primary button;
- the run row;
- layout audit.

`sendguard` passes unchanged.

**Screenshot harness:** `SHOT_WINDOW=fieldsweep` (a real sweep to the
local discard port).

Files: `fieldsweepdialog.h/.cpp`, `tests/test_session145.cpp`,
`tests/screenshot_main.cpp`, `tests/tests.pro`.

Gate: run together with session 146 (committed next), on Qt 5.15.19 and
Qt 6.11.2: all green.


---

<a id="session-144"></a>
## Session 144 — UI revamp, tool windows 21: the recorded-session window

- **The table is set up the way the live log's is**
  (`LogTableView::configure`, as Compare and Merged use it). Its own pixel
  widths (Time 100, Source 60) clipped "16:32:27.1..." and "ime (local",
  and the Time column now has the live log's painter, with fixed-pitch
  digits and the unchanged part muted. Stored column widths and hidden
  columns apply here too, as they do in Compare and Merged.
- **The side panels follow the cursor, focus or not.** They only updated
  when the table had keyboard focus, so Go to timestamp, Next problem and
  the find bar moved the cursor while Raw bytes / Decoded fields kept
  showing the previous frame. They now follow the table whose cursor
  moved.
- **Decoded fields in front**, as in the live window; Raw bytes is one tab
  away.
- **Dock tabs are not elided** ("Raw b...", "Decoded fi..."). Every tab kept
  24 px on its right for a close cross, and dock tabs never have one.
  `QMainWindow > QTabBar::tab` now has 10 px, app-wide, so the live
  window's dock tabs are a little narrower too. Document tabs are
  unchanged.
- **Opens at 1200 x 700, not 1100.** The filter bar (846 px) and the side
  panel with a frame's header in it (290) need 1146, so at 1100 the window
  grew under the operator's hand on the first row clicked. A remembered
  size still wins.
- **The status counts in English**: "400 records from 2 files",
  "1 warning" (was "file(s)", "record(s)", "warning(s)").

Not changed: the filter bar itself. Its text field is squeezed to a stub
at these widths, but the live window's is too. It is the shared widget,
so it gets its own pass if wanted.

Tests: `test_session144`, 19 checks, on 400 real lines of
`replay/loco_1_1_27062026_140226.cap`, written to two `.dlr` files the way
Save writes them and loaded back, with the app stylesheet applied:

- the Time and Source columns fit;
- the Time painter;
- still read-only and multi-select;
- the panels follow a cursor moved without focus;
- Decoded fields in front;
- dock tabs not elided;
- the status text;
- a minimum width within the 1200 default with a frame showing;
- fits a laptop;
- layout audit.

`sessiontools` and `sessionfind` pass unchanged.

**Screenshot harness:** `SHOT_WINDOW=session`, two `.dlr` files from real
traffic.

Files: `sessionwindow.cpp`, `uistyle.cpp`, `tests/screenshot_main.cpp`,
`tests/test_session144.cpp`, `tests/tests.pro`.

Gate, on Qt 5.15.19 and on Qt 6.11.2 alike: 11/11 validators, `dltests`
193 suites / 5800 checks, menu audit 157/157, headless smoke: all green.


---

<a id="session-143"></a>
## Session 143 — UI revamp, tool windows 20: the Run report

- **The span and the per-packet counts sit side by side.** One under the
  other, fourteen packet rows ran down a narrow column, and the page's
  right side stayed empty.
- **Headings count in plain English**, in the run report and the
  incident report alike: "Loco mode (LSRP): 1 change", "3 episodes",
  "8 reads, 6 distinct", "0 matches over 213 frames", "1 sample above
  the permitted speed", "1 comparison". They read "change(s)",
  "episode(s)", "read(s)", "match(es)", "frame(s)", "sample(s)" and
  "comparison(s)".

The window itself (a paper-white report view with Rebuild / Copy / Save
HTML) was already sound. Unlike the incident report, the run report
embeds no images, so session 133's blank-image problem did not apply.

Tests: `test_session143`, 7 checks, on the whole of
`replay/loco_1_1_26062026_162418.cap`:

- neither report has a "(s)" or "(es)" count left;
- "1 change" is singular;
- the side-by-side table;
- the window fits;
- layout audit.

`session81`, `session97` and `session133` pass unchanged.

**Screenshot harness:** `SHOT_WINDOW=runreport`.

Files: `runreport.cpp`, `incidentreport.cpp`, `tests/screenshot_main.cpp`,
`tests/test_session143.cpp`, `tests/tests.pro`.

Gate, on Qt 5.15.19 and on Qt 6.11.2 alike: 11/11 validators, `dltests`
192 suites / 5781 checks, menu audit 157/157, headless smoke: all green.


---

<a id="session-142"></a>
## Session 142 — UI revamp, tool windows 19: the Round-trip validator

**The results get the room.** In a 720-px window the results table showed
4 of its 11 packet types. An empty 90-px file list, and a detail pane at
the same stretch as the table, took the rest. Now it shows 10.

- **The file list appears once it has files.** Empty, it was a blank box
  while the live log was the whole corpus.
- **Results and the selected type's detail share a splitter**, 3 : 1 for
  the results. The detail's floor is 90 px, not 150, and the split can
  be dragged.
- **No percentage text on the progress bar.** Qt draws it in one colour
  over both the fill and the empty groove, and in the light theme it was
  grey on blue. The bar and the status line already say how far along
  the run is.
- **The introduction is muted**, as an explanation rather than part of
  the work.
- **Headers align left. Counts read "45 frames" / "1 frame"** (was
  "frame(s)"), in the detail and in the empty-state message.

Tests: `test_session142`, 11 checks, from a real run over 400 lines of
`replay/loco_1_1_27062026_140226.cap`, with the app stylesheet applied:

- no file list until there are files;
- 11 types listed, at least 9 in view;
- headers align left;
- "45 frames" in the detail;
- results get the larger share, in a splitter;
- no text on the bar;
- fits;
- layout audit.

`roundtrip` and `roundtripwindow` pass unchanged.

**Screenshot harness:** `SHOT_WINDOW=roundtrip`.

Files: `roundtripwindow.cpp`, `tests/screenshot_main.cpp`,
`tests/test_session142.cpp`, `tests/tests.pro`.

Gate, on Qt 5.15.19 and on Qt 6.11.2 alike: 11/11 validators, `dltests`
191 suites / 5774 checks, menu audit 157/157, headless smoke: all green.


---

<a id="session-141"></a>
## Session 141 — UI revamp, tool windows 18: the Fault panel

- **"Save report…" is a button at the end of the panel's header row.**
  It was a toolbar of one action, a whole row above the header.
- **Codes read "0x02".** They read "0X02": the whole string was upper
  cased, including the x. Only the digits are now.
- **The code column is monospaced.** It used `systemFont(FixedFont)`;
  now it uses `UiStyle::monoFont()` (see session 137).
- **Headers align left**, as in the other tool tables.

Not changed: "Active since" and "Last seen" count from when each frame
arrived. That is the right clock for a live panel. Replayed or
bulk-loaded traffic therefore reads "0s / now".

Tests: `test_session141`, 8 checks, fed the whole of
`replay/loco_1_1_26062026_162418.cap`, which leaves 4 faults active, with
the app stylesheet applied:

- no toolbar, and Save report sits in the header row;
- codes read 0x… and are monospaced;
- headers align left;
- fits;
- layout audit.

`faultreport` passes unchanged.

**Screenshot harness:** `SHOT_WINDOW=fault`.

Files: `faultpanelwindow.cpp`, `tests/screenshot_main.cpp`,
`tests/test_session141.cpp`, `tests/tests.pro`.

Gate, on Qt 5.15.19 and on Qt 6.11.2 alike: 11/11 validators, `dltests`
190 suites / 5763 checks, menu audit 157/157, headless smoke: all green.


---

<a id="session-140"></a>
## Session 140 — UI revamp, tool windows 17: Search recorded sessions

- **Query and Search share one row; the dates and filters sit compact on
  the next.** In one grid, the From box shared the query's stretching
  column and ran across half the window.
- **Cancel appears in Search's place while a search runs.** The rest of
  the time it was a disabled, full-width bar under the form. The
  progress bar still shows only while searching.
- **The columns are measured to what they hold.** At 160 px the Time
  column read "2026-10-02 …", and the "Source" header was cut. Headers
  align left.
- **The status reads as English:** "15 hits from 400 records across 1
  file", "Searching 3 files…", "Cancelled after … — 2 hits so far" (was
  "hit(s)", "record(s)", "file(s)").

Tests: `test_session140`, 17 checks. They run a real search over a
fixture archive: a .dlr dated today whose records are 200 real capture
lines, under a temporary disk-log root, with the app stylesheet applied.

- idle, Cancel takes no room;
- the date boxes are a date's width;
- the result text;
- back to Search when done;
- the whole time and every header fit, and headers align left;
- fits;
- layout audit.

`archivesearch` passes unchanged.

**Screenshot harness:** `SHOT_WINDOW=archive` (writes the same fixture
and searches for @lsrp).

Files: `archivesearchwindow.cpp`, `tests/screenshot_main.cpp`,
`tests/test_session140.cpp`, `tests/tests.pro`.

Gate, on Qt 5.15.19 and on Qt 6.11.2 alike: 11/11 validators, `dltests`
189 suites / 5755 checks, menu audit 157/157, headless smoke: all green.


---

<a id="session-139"></a>
## Session 139 — UI revamp, tool windows 16: the Sub-packet window

Packet Maker opens this window to edit one sub-packet's fields.

- **Editors keep their own size.** The form let every editor stretch
  across the window, so in a wide window a one-digit value sat 600 px
  from its name. Editors now stay at their natural size, with a 140-px
  floor, next to their labels.
- **A field's condition is muted after its name**, in place of
  "AUTHORIZED_SPEED  [if AUTHORITY_TYPE==1]". The tooltip reads "Only on
  the wire when AUTHORITY_TYPE==1".
- **Repeat tables say what they need.** An empty one shows "No rows yet
  — this needs at least 1. "+ row" adds one." instead of a blank grid.
  The caption is a section label: "tag rows — 1 to 62 · the count goes
  in ROUTE_RFID_CNT by itself" (was "repeat "tag" (count ROUTE_RFID_CNT,
  auto) — 1..62 rows"). Headers align left. A table's floor is 160 px,
  not 240.
- **The rebuild hides what it removes** before deleting it, the same
  fix as the Flasher's pre-flight rows in session 134.

Tests: `test_session139`, 17 checks, on SLRP sub-packets 0
(MovementAuthority) and 5 (TagLinking), under the app stylesheet:

- editors are ≤ 320 px in a 1100-px window;
- conditional fields are marked and explained;
- both repeat tables say they are empty, with left headers;
- the tag table's caption gives its range and its count field;
- re-targeting leaves nothing stray;
- fits.

`subpacketwindow` and `packetmakerlayout` pass unchanged.

**Screenshot harness:** `SHOT_WINDOW=subpacket` (`SHOT_SUB=<type>`).

Files: `subpacketwindow.cpp`, `tests/screenshot_main.cpp`,
`tests/test_session139.cpp`, `tests/tests.pro`.

Gate, on Qt 5.15.19 and on Qt 6.11.2 alike: 11/11 validators, `dltests`
188 suites / 5738 checks, menu audit 157/157, headless smoke: all green.


---

<a id="session-138"></a>
## Session 138 — UI revamp, tool windows 15: Frame Diff, and every checkbox

**Ticked checkboxes looked unticked in the dark themes.** The app
stylesheet filled a ticked box with the selection highlight. In the dark
themes that is a navy close to the background, and the box has no tick
mark. Every ticked checkbox read as off: Frame Diff's "Show only
differences" (on by default), the Loco Configuration targets, "Follow
new messages", and the rest. A ticked box is now filled with the theme's
accent, which is contrast-audited against the background in every
theme. A new test measures the drawn pixels in all 11 themes: ticked
against the background, and ticked against unticked, are each at least
3:1 (WCAG's floor for a control's state).

**Frame Diff.**

- **Hex and values are monospaced.** The frame boxes and the table used
  `systemFont(FixedFont)`; they now use `UiStyle::monoFont()` (see
  session 137).
- **Each frame box shows a whole frame.** At 90 px, a 39-byte capture
  line showed three of its five lines. The boxes are now seven lines,
  and the input strip is exactly their height.
- **Headers align left**, as in the other tool tables.
- **The counts read as English:** "5 fields differ" / "1 field differs",
  "3 bytes differ" (were "field(s)", "byte(s)").

Tests: `test_session138`, 21 checks:

- the ticked box, in every theme;
- on three real @lsrp lines under the app stylesheet:
  - values and hex are monospaced;
  - each box shows its whole frame;
  - headers align left, and the table keeps the bulk of the window;
  - Show only differences is on;
  - the summary text;
  - fits;
  - layout audit.

`framediff`, `framediffmany`, `framediffwindowcols`, `contrastaudit`,
`uipalette` and `colorblind` pass unchanged.

**Screenshot harness:** `SHOT_WINDOW=framediff`.

Files: `uistyle.cpp`, `framediffwindow.cpp`, `tests/screenshot_main.cpp`,
`tests/test_session138.cpp`, `tests/tests.pro`.

Gate, on Qt 5.15.19 and on Qt 6.11.2 alike: 11/11 validators, `dltests`
187 suites / 5721 checks, menu audit 157/157, headless smoke: all green.


---

<a id="session-137"></a>
## Session 137 — UI revamp, tool windows 14: the Decode Workbench

**Hex in a proportional font.** The Workbench set its hex input, status
line and field table to `QFontDatabase::systemFont(FixedFont)`. On some
platforms (the offscreen plugin, some Linux setups) that returns the
proportional UI face, so pasted hex lost its columns and nothing said
so. `UiStyle::monoFont()` exists for exactly this (session 117): it
checks that the font is fixed-pitch, falls back by family, and follows
the text-size setting. The Workbench uses it now. Three places had the
same call and are fixed with it:

- Replay's position readout (session 136's window);
- the Live Loco Console's table (session 124);
- the main window's raw-bytes panel: its header and hex dump.

`fieldsweepdialog.cpp`, `faultpanelwindow.cpp` and `framediffwindow.cpp`
still make the call. They are fixed in their own passes.

**Layout.**

- **The input box is five lines tall** and scrolls past that. A capture
  line fits in two. At "up to 140 px" it always took the full 140.
- **The session-key combo is sized to its entries.** It used to take
  half the row.
- **The field table's headers align left**, as in the other tool
  tables.

Tests: `test_session137`, 12 checks, on a real @lsrp line, under the app
stylesheet:

- the hex input and the field table are fixed-pitch;
- the frame decodes;
- the input is five lines, and the table gets most of the window;
- the headers align left;
- the key combo is less than half the row;
- the raw-bytes panel's hex is fixed-pitch;
- fits;
- layout audit.

**Screenshot harness:** `SHOT_WINDOW=workbench`.

Files: `decodeworkbench.cpp`, `replaywindow.cpp`, `lococonsolewindow.cpp`,
`rawbytespanel.cpp`, `tests/screenshot_main.cpp`,
`tests/test_session137.cpp`, `tests/tests.pro`.

Gate, on Qt 5.15.19 and on Qt 6.11.2 alike: 11/11 validators, `dltests`
186 suites / 5700 checks, menu audit 157/157, headless smoke: all green.


---

<a id="session-136"></a>
## Session 136 — UI revamp, tool windows 13: Replay

**Replay did not fit a laptop.** The one row of controls ended with the
position readout, which made the window at least 1483 px wide, wider
than a 1366 × 768 screen. Even at that width the readout was cut off
("@1_1"). The minimum is now 1269 px.

- **The readout ends the scroll-slider row**, next to what it reads:
  time, position, record and source. It is selectable.
- **The play and jump buttons show their arrows.** At fixed 36 / 30 px
  the app stylesheet's padding left a sliver of ▶, and the jump ◀ / ▶
  were blank. Each is now sized to its glyph.
- **The Jump combos show their text.** "Frame # (s/midnight)" was cut to
  "Frame # (s/midn", and the value box's hint to "e or HH:MM:SS". The
  field box fits its longest entry, and the value box fits its hint.

Left as it is, on purpose: the timeline's fixed dark canvas. It does not
follow the theme, and that is a recorded decision: `test_contrastaudit`
allows its colour literals as "a fixed dark plot canvas, deliberately
independent of the theme".

**Screenshot harness:** `SHOT_WINDOW=replay` (both locos of a run;
`SHOT_CAP` picks the run).

Tests: `test_session136`, 10 checks, on both locos of
`replay/loco_?_1_27062026_151052.cap`, under the app stylesheet:

- fits 1366 × 768;
- the readout ends the scroll row, reads time, position, record and
  source, and is not cut off;
- the three arrow buttons are wide enough for their glyphs;
- the Jump field shows its text whole;
- layout audit.

`session82` and `session84`, which drive Replay, pass unchanged.

Files: `replaywindow.cpp`, `tests/screenshot_main.cpp`,
`tests/test_session136.cpp`, `tests/tests.pro`.

Gate, on Qt 5.15.19 and on Qt 6.11.2 alike: 11/11 validators, `dltests`
185 suites / 5688 checks, menu audit 157/157, headless smoke: all green.


---

<a id="session-135"></a>
## Session 135 — UI revamp, tool windows 12: Loco Configuration

**The field table gets the height.** The send bar took 304 px of a
720-px window, and the table, which is the window's main content,
showed 6 of its 170 fields. It now shows 10, and the send bar is 264 px
under the app stylesheet. Nothing was removed from the bar.

- **Export loco_info.bin… and Send to VCC… sit at the end of the bar's
  title row.** Beside the notes, they took the right third of the bar
  and wrapped every note to two lines.
- **The summary and vcc_crc share a line.** The summary is shorter, "375
  B · src 28 → dest 2 · msg 120 · loco_info_crc 0x…", with "The
  375-byte datagram…" in its tooltip. vcc_crc reads "set by hand: must
  match this loco's VCC build". At 1100 px it still wraps to a second
  line; at 1280 px it does not.
- **The no-reply note is one sentence:** "The VCC does not reply:
  DLConsole records what it sent, not whether the loco applied it." It
  is still in the send bar.
- **The blocker line shows only when something blocks** (an invalid
  address, no ticked target, a value out of range). Empty, it used to
  cost its height and spacing.
- Bar margins and spacing are slightly tighter.

Tests: `test_session135`, 18 checks, under the app stylesheet (session
134's lesson):

- the bar is ≤ 270 px and the table shows ≥ 9 rows;
- the summary's text and tooltip, on one line with vcc_crc, and
  vcc_crc still says it is set by hand;
- the no-reply note, on one line;
- Send is in the title row;
- the blocker line hidden, shown for a bad address, hidden again;
- fits 1366 × 768;
- layout audit.

`lococonfig`, `lococonfigrun` and `session94` pass unchanged.

**Screenshot harness:** `SHOT_WINDOW=lococonfig`.

Files: `lococonfig/lococonfigwindow.cpp`, `tests/screenshot_main.cpp`,
`tests/test_session135.cpp`, `tests/tests.pro`.

Gate, on Qt 5.15.19 and on Qt 6.11.2 alike: 11/11 validators, `dltests`
184 suites / 5678 checks, menu audit 157/157, headless smoke: all green.


---

<a id="session-134"></a>
## Session 134 — UI revamp, tool windows 11: the Firmware Flasher

**The Flasher did not fit a laptop.** Its minimum size was 1255 × 863,
or 1350 × 863 while flashing, so on a 1366 × 768 screen it ran off the
bottom. It is now 1090 × 694 on every page.

**The flashing page** set the height: five three-line stat cards stacked
(540 px) plus a fixed 150-px log under them.

- **Stat cards** have the value and its caption on one line, so each
  card is one line shorter.
- **The log** is six lines tall and scrolls.
- **The block map's caption** is short ("1450 B per block · hover a cell
  for its offset"), with "From the board's STATUS bitmap" in its
  tooltip. On one line, the full sentence made the centre card at least
  681 px wide.
- **The map's floor** is four rows of cells, not 120 px.
- **The phase stepper's labels** may wrap to two lines, with the time
  under them. They used to be cut off ("Waiting for up…").
- **The card list:** the file name has its own line, elided to the width
  it has. It used to be cut at a fixed 170 px and then clipped again by
  "Waiting" ("KAVACH_…ppimag").
- **The Engineer/Operator toggle is hidden while flashing**, as well as
  disabled. The mode cannot change mid-run, and the amber "keep the
  chassis powered" bar needs the room.

**The queue page.**

- **The file name gets the width.** The fixed columns (180 / 110 / 120 /
  190 px) left the Image column about 100 px in a 1100-px window
  ("KAV…age"). Size, blocks and CRC are now on the Image cell's second
  line ("56.6 KB · 40 blocks · CRC 0x32B2D566"), in place of the folder,
  which the detail strip shows in full. The Card and Image-check columns
  are measured to what they hold.
- **The badge says "Name matches"**: the Card column already names the
  card, and "Name matches Output" was cut to "Name matches Ou…". The full
  sentence is the tooltip; other badges are as before. A row with no
  image gets no badge, where it used to get an empty pill.
- **The detail strip no longer sits on the table's last row**, which
  hid the VCC row. Two causes:
  - an empty "blocker" label took half the action bar, so the procedure
    note wrapped to five lines and the bar grew to 121 px;
  - the wrapped subtitle's and note's height was not in the window's
    minimum.

  The blocker label is now hidden when empty. The subtitle is shorter
  ("One card per run: select it, press Flash, power-cycle"), and so is
  the Modified note ("· re-hashed if the file changes").
- **The SHA-256 has a line of its own** across the strip. Its 64 hex
  digits cannot wrap, and beside its key they made the queue at least
  1185 px wide. It is kept whole, so a copy compares cleanly.
- **Pre-flight comes before the delivery route.** It is what says
  whether Flash will work, and it used to sit below the fold.
- **The stray "⚠" at the pre-flight card's corner is gone.** Each
  refresh took the old checklist rows out of the layout but left them
  visible until deleted. They are now hidden first. The summary page's
  card panels had the same pattern and get the same fix.

**The summary page** shows a one-card failure's explanation once: the
card panel no longer repeats the banner word for word.

**Found by the gate, not by the screenshot.** The detail strip still
overlapped the table by 4 px in the full suite, but not when the new
suite ran alone. A new comma-separated suite list in `dltests` (below)
bisected it to `uipalette`, which leaves the app's stylesheet applied.
The app always runs with that stylesheet, so the overlap was real. The
suite now applies the stylesheet itself.

**Test harness:** `dltests a,b,c` runs those suites in registry order,
to find which earlier suite leaves state that breaks a later one.
`dltests <one>` works as before.

**Screenshot harness:** `SHOT_WINDOW=flasher`, `flasherrun` (a real run
against an address nothing answers, on its power-cycle prompt) and
`flashersum` (that run's summary). `SHOT_DUMP` also lists loose widgets
and table geometry; `SHOT_DEEP=<px>` lists every widget whose minimum
is at least that big.

Tests: `test_session134`, 26 checks, under the app stylesheet:

- fits 1100 × 700, on the queue and while flashing;
- size and CRC sit under the name, and the Image column is ≥ 300 px;
- the badge text and its tooltip, and no badge for an empty row;
- the strip starts below the table, and all four cards are in view;
- the blocker label is hidden when ready;
- pre-flight comes before the route;
- no stray widget after pre-flight refreshes;
- the mode toggle is hidden mid-run and back after;
- the power-cycle prompt shows;
- the card's file name is elided to its width;
- aborting lands on the summary;
- the explanation shows once;
- nothing stray on the summary.

`flasher`, `flasherrun`, `flasherroute` and `session94` pass unchanged.

Not changed: the block map's 14-px cells (the handoff's design). A small
image still fills only a strip of the map.

Files: `flasher/flasherwindow.cpp`, `flasher/flasherqueuepage.cpp`,
`flasher/flasherqueuemodel.cpp`, `flasher/flasherflashingpage.h/.cpp`,
`flasher/flashersummarypage.cpp`, `tests/main.cpp`,
`tests/screenshot_main.cpp`, `tests/test_session134.cpp`,
`tests/tests.pro`.

Gate, on Qt 5.15.19 and on Qt 6.11.2 alike: 11/11 validators, `dltests`
183 suites / 5660 checks, menu audit 157/157, headless smoke: all green.


---

<a id="session-133"></a>
## Session 133 — UI revamp, tool windows 9: the Incident report

**The window showed no pictures.** The report embeds every DMI panel
and the speed plot as `data:` URIs. Browsers show these, so the saved
file was always complete, but the in-app viewer (QTextBrowser) does not
load them. Every image in the window was blank space under its heading.
For the viewer only, `IncidentReportWindow::forViewer()` now registers
each image as a document resource. The saved file is byte-for-byte the
same kind of file as before.

**The speed plot leaves 0 m out** (Abhinav, as for the track diagram and
the two-loco view). The plot is speed against location, and the two
minutes around the rear-end in `replay/loco_1_1_27062026_151052.cap`
include 56 frames at 0 m (not localised). Its axis ran from 0 to 160 km,
with the incident a sliver at the right edge.

- The plot now spans 160.57–160.66 km.
- Targets computed from a 0 m frame are dropped, since they were placed
  relative to 0 m.
- A caption under the plot says "56 of 81 frames report 0 m (not
  localised on an RFID tag) and are left out of the plot".
- A window that is all 0 m says there is no location to plot against.
- Only the plot changes: `Summary::speedTrace` stays whole. The highest
  speed, the overspeed count, the DMI moments, mode changes, brakes and
  raw frames are as before, and the Speed/distance window is untouched.

**Layout.**

- **The DMI moments sit two to a row**, tops aligned. Eight moments one
  under another made a page-long column to scroll past.
- **Each image carries a `width`** (440 for a panel, 760 for the plot)
  as well as the CSS `max-width`: the viewer honours only the attribute.
  Each image is in its own paragraph; inline after a heading, the viewer
  laid the heading out at the image's height.
- **The status line** reads "918 raw frames · 3 DMI moments" (was "raw
  frame(s)").
- **The dialog's OK button** reads "Build report".
- The window opens at 1000 × 720.

**Screenshot harness:** `SHOT_WINDOW=incident` and `incidentdlg`
(`SHOT_CAP`, `SHOT_AT` pick the run and the moment).

Tests: `test_session133`, 22 checks, on the real run around 15:13:10:

- 81 samples kept, 56 at 0 m left out of the plot;
- the plot spans the incident's stretch, and no target is placed from a
  0 m frame;
- the caption is there, and the highest speed is unchanged;
- the file embeds 4 images, each with a width;
- the moments are two to a row;
- the viewer gets no `data:` URI, its resources are the same pixels as
  the panels rendered, and Save HTML still embeds them;
- the status text;
- the dialog's button;
- fits 1366 × 768;
- layout audit.

`test_session97` passes unchanged.

Files: `incidentreport.h/.cpp`, `incidentreportwindow.h/.cpp`,
`incidentreportdialog.cpp`, `tests/screenshot_main.cpp`,
`tests/test_session133.cpp`, `tests/tests.pro`.

Gate, on Qt 5.15.19 and on Qt 6.11.2 alike: 11/11 validators, `dltests`
182 suites / 5634 checks, menu audit 157/157, headless smoke: all green.


---

<a id="session-132"></a>
## Session 132 — UI revamp, tool windows 8: the Two-loco view

**0 m means "location not known" here too** (Abhinav, as for the track
diagram in session 131). Take the real pair
`replay/loco_1_1_27062026_151052.cap` / `loco_2_1_` of the same run:
loco A reports 0 m in 132 of 157 frames, and loco B in 45. Every instant
where one loco was at 0 m and the other at 161 km became a "gap" of
161 km, both on screen and in Save gap CSV. The location lane's scale
also ran from 0 to 161 305 m. Now:

- **The gap:** a gap sample needs both locos' locations known. This run
  has 25 real gaps, where 157 were reported before.
- **The location line breaks** where the location is not known, instead
  of diving to 0 and back.
- **The scale:** both the location scale and the overlap check use known
  samples only.
- **A loco that never localises** is named: "21_1 reports 0 m
  throughout … there is no gap to show."
- **Speed** is drawn as before.

**The same-section warning is now a distance you set.** With the 0 m
frames gone, the old rule (warn when the two location ranges do not
overlap) fired on exactly the case the view is for. In this run, A sits
at 160.57–160.65 km and B runs 160.8–161.3 km about 200 m ahead of it,
and the rule said "check these are on the same section". Before this
patch the rule never fired on real logs, because both ranges included
0 m. Abhinav asked for the limit to be configurable:

- A "Warn if apart by more than [10.0 km]" box in the window, saved in
  `ui/two_loco_warn_apart_km` (default 10 km).
- The view warns when the ranges' nearest points are further apart
  than that, and says by how much.
- 0 shows as "any gap" and keeps the old any-non-overlap rule.
- `TwoLocoView::build()` takes the distance as a new last parameter.
  Its default, 0, keeps direct callers on the old rule.

**The canvas.**

- **A header row** with the legend (A and B, by line colour). When you
  point at the plot it reads both locos at that moment: location (or
  "not localised (0 m)"), speed, and the gap (or "no gap"). It used to
  be a status-line readout of the gap only, which made the status line
  flicker.
- **Lane titles** LOCATION and SPEED.
- **Location on round km ticks** (160.6 / 160.8 / 161.0 km). Before,
  the axis showed two raw metre values.
- **Speed on round km/h ticks** (0 / 40 / 80 / 120). The top label
  used to be clipped ("I00 km/h").
- **A time axis** with round clock ticks. Before, only the two ends
  were labelled. A label slid in from the right edge is dropped if it
  would land on its neighbour.
- **The cursor** puts a dot on each trace where it crosses.
- **SoS/collision event ticks** have tooltips naming each episode.
- **The not-same-section banner** is in the warning colour, not the
  error colour.

**The status line** reads "25 gap samples · A: 132 of 157 frames not
localised (0 m) · B: …" (was "gap sample(s)"). It stays one line: a
warning shows a short line, and its tooltip has the full sentence plus
why the 0 m frames are left out. The window opens at 1100 × 680.

Not changed, and worth knowing: no capture in `replay/` has @lsos
frames, so the event ticks are covered only by session 98's synthetic
frames. The rear-end collision that loco 1's DMI reports in this run
(EMERGENCY_STATUS) is not one of this view's events, which come from
@lsos only, and is not drawn.

**Screenshot harness:** `SHOT_WINDOW=twoloco` (both locos of a run;
`SHOT_CAP` picks the run).

Tests: `test_session132`, 27 checks, on the real pair:

- the 0 m frames are counted;
- 25 gaps, all between known locations, none of 161 km;
- the scale is the run's;
- the CSV holds only the real gaps;
- the ranges are a few hundred metres apart: no warning at 10 km. The
  old rule still holds at 0, and a tighter limit warns with the
  distance;
- a loco that never localises is named, and there is no gap;
- the header readout with both locations known and with A at 0 m;
- pointing at the plot sets the cursor;
- the limit box shows and saves the setting;
- the status text, its tooltip, one line;
- fits 1366 × 768;
- layout audit.

`test_session98` passes unchanged.

Files: `speeddistance.h` (`locationKnown` moved here, so the track
diagram and this view share it; SpeedDistance's own extraction and plot
are unchanged), `trackdiagram.h`, `twolocoview.h/.cpp`,
`twolocowindow.h/.cpp`, `settings.h`, `tests/screenshot_main.cpp`,
`tests/test_session132.cpp`, `tests/tests.pro`.

Gate, on Qt 5.15.19 and on Qt 6.11.2 alike: 11/11 validators, `dltests`
181 suites / 5612 checks, menu audit 157/157, headless smoke: all green.


---

<a id="session-131"></a>
## Session 131 — UI revamp, tool windows 7: the Track diagram

**0 m now means "location not known".** A loco that has not localised
on an RFID tag reports `abs_loco_loc` = 0. In
`replay/loco_1_1_26062026_162418.cap`, 352 of the 842 @dmi frames do:
every frame before the first tag read, and again after the drop to
Stand_By until the next read. The diagram used to treat 0 m as a
place, with three results:

- the span became 0 to 161 253 m;
- every real tag and event (152.7 to 161.3 km) was squeezed into the
  last 5 % of the rail, with their ids printed over each other;
- with the cursor on the last frame, the loco was drawn at 0 m.

The schema gives 0 no meaning, so I asked Abhinav, who decided that
in the track diagram 0 m is "not known". Such samples:

- stay in the trace, so the time cursor still steps through them;
- do not set the span;
- do not place a signal (a signal is placed relative to the loco);
- do not pin an event;
- do not draw the loco while the cursor is on one. The readout says
  "location not known (0 m: not localised)" instead.

An event raised while the location is not known is no longer pinned at
the last known location: that location may be from before a Stand_By,
somewhere the loco no longer was. Such events are counted instead. In
this capture, 22 NMS faults raised at power-up and around Stand_By are
counted this way; before, all 22 were drawn at 0 m. This applies to
the track diagram only. Speed/distance, Two-loco and the incident
report read the trace as before.

**The canvas.**

- **Lanes.** From the top: events, tag ids, tags, the rail, signals
  with their MA ends, signal names, then a km scale and a legend. The
  block is centred in the canvas. Before, everything sat in a 100-px
  band and the rest of the height was empty.
- **Labels never overlap.** Tag ids stack into up to three rows and
  signal names into two. A label that still has no room is left out;
  the mark's tooltip still has it. Labels near an end are slid back
  inside the canvas: "DN MAIN IB-Stop" ran off the right edge.
- **A km scale** with 1/2/5 steps ("155.5 km"). Before, only the two
  ends were labelled, in metres.
- **A legend** for the loco, RFID tag, signal (aspect colour),
  movement authority end, and the three event colours.
- **A readout of the loco at the cursor**: time, location, speed and
  mode. Before, it showed only the time.
- **Tooltips on every mark**: tag id, location and read time; signal,
  location and its MA end; each event's label, location and time; the
  loco. Events had no text anywhere before.
- **The loco's nose** is an arrowhead in the direction of travel, and
  the MA flag points the same way.

**The status line** reads "6 RFID tags · 0 signals · 1 event · not
localised (0 m): 352 of 826 frames, 22 events" (was "tag(s)"). It fits
one line. Its tooltip explains why the 0 m frames are left off the
rail. The window opens at 1100 × 560.

**Screenshot harness:** `SHOT_WINDOW=track`; `SHOT_CAP=<date_time>`
picks the capture.

Tests: `test_session131`, 27 checks, on two whole real captures:

- the 352 frames at 0 m are counted, and the span is 152.7 to
  161.3 km;
- the 0 m samples stay in the trace;
- no event is pinned at 0 m, and the 22 unpinned events are counted;
- the loco is drawn on a known frame and not on a 0 m frame;
- every tag has a tooltip;
- all six tag ids are drawn, and no labels overlap or leave the canvas
  (also with three signals, one at the right edge);
- signals and MA ends have tooltips;
- a log that is all 0 m says there is no location;
- the status text, its tooltip, one line;
- the slider spans every sample;
- fits 1366 × 768;
- layout audit.

`test_session99` passes unchanged.

Files: `trackdiagram.h/.cpp`, `trackdiagramwindow.h/.cpp`,
`tests/screenshot_main.cpp`, `tests/test_session131.cpp`,
`tests/tests.pro`.

Gate, on Qt 5.15.19 and on Qt 6.11.2 alike: 11/11 validators, `dltests`
180 suites / 5585 checks, menu audit 157/157, headless smoke: all green.


---

<a id="session-130"></a>
## Session 130 — UI revamp, tool windows 6: the Merged window

"All sources — chronological" (View ▸ All sources, chronological…, Ctrl+M)
is one log table, so the pass was about making it behave like the other
log tables.

- **It follows the log-table settings now.** The table had its own
  pixel column widths and read only the row density. It ignored the
  operator's hidden columns and stored widths, drew the Time column in
  a proportional font, and cut the "Time (local)" header. It is now set
  up by `LogTableView::configure`, like the log tabs and the Compare
  panes. That gives the same columns, widths measured to the font, and
  the mono Time column with the shared prefix muted. As with Compare,
  the settings are read when the window opens. A window already open
  does not follow a later View ▸ Columns change.
- **An empty table says why.** Before traffic arrives it says "Waiting
  for traffic." If a filter hides every row, it says how many rows the
  filter hides. Before, both cases showed a bare grid.
- **The status bar.** "300 messages from 3 sources" (was "source(s)"),
  then a muted hint: "Double-click a row to open it in its source's
  tab". Before, nothing in the window mentioned that.

Unchanged: the filter bar, follow mode, priming from the open tabs, and
double-click to jump.

**Build:** `tests/tests.pro` builds the serial terminal's suites (101–111,
115, 122) only where Qt Serial Port exists, like suite 85. A Qt without
the module (Qt 6.12 from the online installer, with the module left
unticked) could not build the tests before. That Qt was not gated here.

**Screenshot harness:** `SHOT_WINDOW=merged` (real traffic as three
sources).

Tests: `test_session130`, 21 checks:

- empty, it says it is waiting;
- 300 real frames from three sources: all in one table, in time order,
  no overlay over them;
- a hidden column stays hidden, Message is never hidden;
- the row height follows the density setting;
- the Time column has the mono painter, and its header fits;
- a filter that hides everything says so, and clearing it brings the
  rows back;
- the count text and the hint;
- double-click asks for the row's own tab;
- a live row arrives;
- fits 1366 × 768;
- layout audit.

Files: `mergedwindow.cpp`, `tests/screenshot_main.cpp`,
`tests/test_session130.cpp`, `tests/tests.pro`.

Gate, on Qt 5.15.19 and on Qt 6.11.2 alike: 11/11 validators, `dltests`
179 suites / 5558 checks, menu audit 157/157, headless smoke: all green.


---

<a id="session-129"></a>
## Session 129 — UI revamp, tool windows 5: the Compare window

- **"Pick a source for this pane." sat over full tables.** The overlay
  follows its model's row signals, but `setModel()` emits none, so a pane
  bound to a source that already had rows kept the overlay on top of them.
  This is what the operator saw every time the window opened. The new
  `EmptyState::refresh(view)` is called wherever Compare changes a pane's
  model. The log tabs set the model before attaching the overlay, so they
  never had this bug.
- **The panes get the height.** The panes and the selected row's details
  share one vertical splitter, 3 : 2 in favour of the panes. Before, the
  details had a fixed 280-px cap and a 252-px floor, and at 720 px the
  panes got 264. Now they get 376, and the split can be dragged.
- **No box round boxes.** The raw-bytes panel draws its own "Header" and
  "Raw bytes" boxes, which sat inside a "Raw bytes for selection" box. Both
  details now have section labels: "Raw bytes of the selected row",
  "Decoded fields of the selected row".

Left as it is, on purpose: the panes' columns. Each pane shows one
source, so the Source and Name columns repeat one value on every row. In a
half-width pane they squeeze Message. But "hidden columns are the
setting's business, not the pane's" is a deliberate earlier decision,
pinned by `test_comparetools`. Hiding them is a change for Abhinav to
decide, not part of a layout pass. View's column settings can already hide
them everywhere.

**Layout audit:** a dialog's menu bar, placed by `QLayout::setMenuBar()`,
is no longer reported as loose.

**Screenshot harness:** `SHOT_WINDOW=compare` (real traffic as two
sources).

Tests: `test_session129`, 15 checks:

- `EmptyState::refresh` on a rebound, an unbound and a bare view;
- the window fed 200 real frames as two sources: no overlay over rows,
  Message shown, panes taller than the details;
- no outer box;
- fits 1366 × 768;
- layout audit.

Files: `emptystate.h/.cpp`, `comparewindow.cpp`, `tests/layoutaudit.h`,
`tests/screenshot_main.cpp`; `tests/test_session129.cpp`, `tests/tests.pro`.

Gate, on Qt 5.15.19 and on Qt 6.11.2 alike: 11/11 validators, `dltests`
178 suites / 5537 checks, menu audit 157/157, headless smoke: all green.


---

<a id="session-128"></a>
## Session 128 — UI revamp, tool windows 4: Packet Maker (finished)

Finishes session 125's first pass. The field editors are the reason the
window is open; they now get the space, and the window fits a laptop.

- **Output and Vary per send are tabs** in the pane under the field
  editors. Vary per send used to be a box of its own under the status line,
  costing ~150 px all the time for a table that matters only in interval
  mode. Its tab shows the rule count ("Vary per send (1 rule)"), so a rule
  never runs unseen.
- **The Vary table's columns fit.** The field name takes the spare width
  and the mode column fits "from live". The three number columns are a
  fixed, readable width. "Max (0 = field width)" became "Max", with the
  note as a tooltip. Before, Max took half the table and the combos read
  "from liv".
- **Folded sections are one short line.** "Also send to", "Extra header
  fields" and "Fill from buffer" each cost 36 px folded; now 28.
  `UiStyle::makeFoldable` drops the box's layout margins while folded,
  restores them on unfolding, and caps a folded box at its switch's height.
- **The header form is the wider side** (600 : 460, was 400 : 660). Sub-
  packets are edited in their own window, so here that side is only a
  list. At 400 px the form squeezed FRAME_NUM's editor to two digits behind
  a sideways scrollbar.
- **The editors get the height.** The vertical split starts at 4 : 1; the
  first layout had followed the size hints, giving the editors two rows.
  The lower pane's floor is the tab bar plus four lines; its largest page
  had set it to 150 px.
- **"Session key:"** replaces "Session key (32 hex, optional):"; the hint
  moved into the box's placeholder.

The minimum size went from 1127 × 789 to 1127 × 617, which fits
1366 × 768.

**A crash, found by the gate.** A `QTableWidget` emits `modelReset` while
being destroyed, after `~PacketMakerDialog` has run. The new tab-title slot
then ran on a half-destroyed dialog and wrote to a dying tab widget. That
corrupted the heap, and the next app-wide restyle crashed, in a later
suite (`tabmetrics`) on both Qts. The destructor now disconnects the
table's model first. Checked both ways: without the fix the full suite
crashes every time; with it, it passes.

**Screenshot harness:** `SHOT_DUMP=1` prints each visible group box, tab
widget and splitter with its height and minimum. That is how the 36-px
folds and the 150-px tab floor were found.

Tests: `test_session128`, 31 checks, which also cover session 125:

- the size limits (≤ 700 px tall, ≤ 1366 px wide) and the layout audit;
- the tabs and the rule count;
- the column widths;
- each folded box: fold, unfold, fold back;
- the split shares;
- Build & Verify is the primary action;
- no "&&" in the status;
- closing the dialog, then restyling the app.

Not tested: on a real 1366 × 768 Windows laptop. The offscreen platform
has no title bar, so the ≤ 700 px limit leaves room for one and for the
task bar.

Files: `packetmakerdialog.h/.cpp`, `uistyle.cpp`,
`tests/screenshot_main.cpp`; `tests/test_session128.cpp`, `tests/tests.pro`.

Gate, on Qt 5.15.19 and on Qt 6.11.2 alike: 11/11 validators, `dltests`
177 suites / 5522 checks, menu audit 157/157, headless smoke: all green.


---

<a id="session-127"></a>
## Session 127 — Builds and passes the gate on Qt 6 as well as Qt 5

The code was meant to build on both: there is a `qtcompat` suite, and its
note says the deployment machine runs Qt 6.9. But nothing built it on Qt 6,
and it had drifted. It is now built and gated on both, here and in CI.

**Fixed to compile on Qt 6.** Every fix also compiles on Qt 5.15; only the
font fix needs a version check.

- `logentry.cpp`: `splitRef`/`QStringRef` (gone in Qt 6) → `split`.
- `brakingpanel.cpp`: `QRegExp` (gone) → `QRegularExpression`.
- `dlrplayerdialog.cpp`, `logquery.cpp`: `qBound<qint64>(0, …)` is
  ambiguous on Qt 6; the bounds are now typed instead.
- `logmodel.cpp`, `settingsbundle.cpp`: `size()` is `qsizetype` on Qt 6;
  cast where it meets an `int`.
- `textzoom.cpp`: `QFont::resolve()` is `resolveMask()` on Qt 6.
- `emptystate.cpp`: Qt 6 refuses `findChild<Overlay *>` because Overlay
  has no `Q_OBJECT`. It is now looked up as the `QLabel` it is, which is
  what Qt 5 did anyway.
- `logentry.h`: `fromMSecsSinceEpoch(ms, Qt::UTC)` → `QTimeZone::utc()`.
  The old form is deprecated on Qt 6.9+, and this header is in nearly
  every file, so its warning repeated on every build.
- Tests `session101`/`session104`: `QSerialPort::ParityError` was removed
  in Qt 6. They use `UnknownError`, which is just as recoverable to
  `SerialLink`.
- Test `windowgeometry`: Qt 6's `restoreGeometry` shrinks a window to fit
  its screen. A 900-px window came back 798 px on the 800-px offscreen
  screen. The test window is now 600 × 450, which fits on both Qts.

**A gate hole on macOS, found on the way.** qmake builds `menuaudit` and
the app as `.app` bundles on macOS, but `verify.sh` ran the bare paths.
Bare binaries left over from one build on Oct 1 sat there, so every local
gate since then ran the menu audit and smoke test against that old build.
The validators and unit suite were fresh, and Linux CI was unaffected.
Fixes:

- `menuaudit.pro` builds a bare binary everywhere (`CONFIG -= app_bundle`,
  as `tests.pro` already did).
- `verify.sh` deletes both binaries before building, so a stale one can
  never run.
- The smoke test runs the bundle's binary on macOS.

Re-run on fresh builds, patches 123–126 pass: menu audit 157/157, smoke
alive.

**Gating Qt 6.**

- `verify.sh` takes `VERIFY_OUT`, so a second Qt builds into its own
  folder.
- CI has a new job, `linux-qt6`: the full gate on Ubuntu 24.04's Qt 6.4.

The operator sees nothing new.

Not tested: Qt 6 on Windows (MSVC). CI runs Qt 6 on Linux only, and
locally on macOS (6.11). The `-Wnon-c-typedef-for-linkage` warnings from
`Structures.h` come from clang, not from Qt 6, and are left alone.

Files: `logentry.h/.cpp`, `brakingpanel.cpp`, `dlrplayerdialog.cpp`,
`logquery.cpp`, `logmodel.cpp`, `settingsbundle.cpp`, `textzoom.cpp`,
`emptystate.cpp`, `verify.sh`, `tests/menuaudit.pro`,
`.github/workflows/build.yml`, `CLAUDE.md`; tests `test_session101`,
`test_session104`, `test_windowgeometry`.

Gate, on Qt 5.15.19 and on Qt 6.11.2 alike: 11/11 validators, `dltests`
176 suites / 5491 checks, menu audit 157/157, headless smoke (alive
after 500 datagrams): all green.


---

<a id="session-126"></a>
## Session 126 — `Direction` renamed to `LogDirection`

The log rows' direction tag (`IN` / `OUT` / none), `enum class Direction`
in `logentry.h`, is now `LogDirection`. A bare `Direction` at global scope
is too generic a name for a type every log file includes, and it is the
first step of building on Qt 6 as well as Qt 5 (session 127).

The rename had been started by hand in six files and left half done, so
the tree did not compile. It is now finished everywhere: 22 files,
including five test suites. Kept as they were:

- `LogQuery::Field::Direction` (a query field, scoped);
- `LogModel::ColDirection`;
- the user-visible "Direction" labels.

Also in the hand edits, kept: `schemadecoder.cpp` casts `rows.size()` to
`int` where it initialises an `int` member. On Qt 6 `size()` is
`qsizetype`, and the brace initialiser rejects the narrowing.

No behaviour change; the operator sees nothing.

Files: `logentry.h/.cpp`, `colorrules.h/.cpp`, `logmodel.h/.cpp`,
`logquery.h/.cpp`, `logwriter.cpp`, `savedata.cpp`, `exporter.cpp`,
`filterbar.cpp`, `searchwindow.cpp`, `messagedispatcher.cpp`,
`serialconsolewindow.h/.cpp`, `schema/schemadecoder.cpp`; tests
`test_clipboard`, `test_logquery`, `test_pipeline`, `test_ruleattribution`,
`test_session100`.

Gate: 11/11 validators, `dltests` 176 suites / 5491 checks, menu audit,
headless smoke: all green.


---

<a id="session-125"></a>
## Session 125 — UI revamp, tool windows 4: Packet Maker (first pass)

- **"Build && Verify" showed both ampersands** in seven status messages and
  a tooltip. In a label `&&` is not a mnemonic escape. They now read "Build
  & Verify". The button keeps `&&`, where it is the escape.
- **Switched-off sections cost one line.** "Also send to", "Extra header
  fields" and "Fill from buffer" already folded their bodies away, but kept
  an empty framed box (about 40 px each), taken from the field editors. A
  folded box is now flat: its switch and title, no frame. The new
  `UiStyle::makeFoldable()` does this, with a stylesheet rule for flat
  group boxes.
- **Build & Verify is the window's primary action.** Sending stays behind
  its Arm switch, as designed.

Files: `uistyle.h/.cpp`, `packetmakerdialog.cpp`;
`tests/screenshot_main.cpp` (`SHOT_WINDOW=packet`).

Not done in this pass, because the usage limit was reached:

- no screenshot was taken after the change;
- no `test_session125` suite;
- the dialog's minimum size was measured at 1127 × 801 before the change
  and not re-measured;
- the "Vary per send" table still clips its cells.

Gate: 11/11 validators, `dltests` 176 suites / 5491 checks, menu audit
157, smoke. All green.

---

<a id="session-124"></a>
## Session 124 — UI revamp, tool windows 3: the Live Loco Console

### What the operator sees (Tools ▸ Monitor ▸ Live Loco Console)

- **Durations as people read them**: "96d 20h", not "8369761 s"; also
  "12m 05s", "4.2 s", "850 ms". This applies to the heartbeat lights, the
  Link table's *Last seen* and the window title's "offline …". The format is
  now one shared function, `UiStyle::durationText()`, rather than one per
  window.
- **Heartbeat lights wrap** onto as many lines as they need. They used to
  run off the right edge ("dlsys silent" was cut in half).
- **The window fits a laptop.** One row carried the readouts and every
  action, and its minimum width came to **1,781 px**, wider than a 1366-px
  laptop screen. The readouts and the actions are now two rows, and the
  console opens at the size asked for.
- **CRC and seq are chips.** "CRC 195/204 ok" was painted red: the colour
  said fail while the sentence said ok. It now reads "✕ CRC 9 failed of
  204" in the fail tone, or "✓ CRC 204/204" in ok, so words and tone agree.
  Seq turns warn when there are gaps. RTC and rate are in the mono face.

The DMI panel itself (its own window, and inside the cab view) is **not**
restyled, deliberately: it imitates the real LP-OCIP display, what the loco
pilot saw, and must stay faithful to it.

Files: `uistyle.h/.cpp` (`durationText`), `lococonsolewindow.cpp`,
`cabpanel.h/.cpp` (`LinkLights` wraps: height-for-width layout);
`tests/screenshot_main.cpp` (`SHOT_WINDOW=loco|dmi`, traffic fed after the
window exists, as it would arrive).

### Tests

`tests/test_session124.cpp`, 13 checks:

- each duration format, including the "96d 20h" that "8369761 s" meant;
- the lights wrap when narrow and never set a window's minimum width;
- on 400 real lines from `replay/`: the console's minimum width fits
  1366 px; no orphans; CRC is a chip whose words and tone agree.

Existing Loco Console and live-fields suites pass unchanged.

Gate: 11/11 validators, `dltests` 176 suites / 5491 checks, menu audit
157, smoke. All green.

---

<a id="session-123"></a>
## Session 123 — UI revamp, tool windows 2: Search all sources

### What the operator sees (Edit ▸ Search all sources)

- **The query is one panel**: a search icon, the field, its two switches
  (errors and warnings only, group by tab) and **Search as the window's
  primary action**.
- **The parse tree is in the real mono face.** It asked for
  `font-family: monospace`, a name that does not resolve on Windows, where
  it fell back to whatever the system chose.
- **Timestamps fit and line up.** The columns were fixed pixel widths, and
  "2026-06-27 14:02:27.512" in 150 px clipped, as the main log did before
  session 118. They are now measured to the fonts they are drawn in, with
  timestamps in mono.

### On this machine

Qt 5 had been uninstalled from this Mac between sessions: no `qt@5` and no
`qmake`, and the built binaries could not load their frameworks. It was
reinstalled (`brew install qt@5`, 5.15.19 as before, keg-only). The gate
now runs as `QMAKE=/opt/homebrew/opt/qt@5/bin/qmake ./verify.sh`. Before
this patch's change was gated, the gate was rerun on the reinstalled Qt and
was green.

Files: `searchwindow.cpp`; `tests/screenshot_main.cpp`
(`SHOT_WINDOW=search`).

### Tests

`tests/test_session123.cpp`, 7 checks, on real `replay/` lines:

- no orphans;
- the query is one panel with Search as primary inside it;
- the search finds real rows;
- **every timestamp fits its column, in the mono face**;
- the parse tree is fixed-pitch.

Gate: 11/11 validators, `dltests` 175 suites / 5478 checks, menu audit
157, smoke. All green.

---

<a id="session-122"></a>
## Session 122 — UI revamp, tool windows 1: the serial terminal; a layout audit

The first tool window in the revamp, in order of how much it is used while
debugging.

### What the operator sees (Tools ▸ Serial Port Terminal)

- **The connection as one panel at the top**: profile row and port/line
  settings, with **Open as the window's one primary action**, filled in the
  accent. `UiStyle::makePrimary()` is new; its text is lifted to the
  contrast floor against the accent in every theme.
- **The state as a chip**: Closed (neutral), open (ok), lost/reconnecting
  (warn), errors (fail). It is set by tone in all nine places that set it,
  not by a colour of its own. The chip hugs its text. Counters are in the
  mono face.
- **Macros as chips**: one that asks first (session 109) is in the warn
  tone.
- The Show only / Highlight placeholders no longer repeat their labels.

### Three bugs found while doing it

1. **The status row lost three widgets, by my own hand, mid-patch.** A
   comment added in the middle of a line of `addWidget()` calls commented
   out the rest of the line. The health label, counters and Reset were
   never laid out, and Reset was drawn at the window's top-left corner.
   **Every functional test still passed**, because they read the widgets,
   not where they are. The screenshot showed it.
2. **A port that would not open showed its error in the neutral tone.** The
   failure path called `updateState()` ("Closed", neutral) after the error
   had been coloured, then wrote the error text uncoloured. It now ends in
   the fail tone.
3. **Rebuilt rows left their old widgets drawn**: the macro buttons, and
   the status-bar serial chips (session 102). `deleteLater()` without
   `hide()` leaves a widget drawn, outside any layout, at its parent's
   corner until the event loop deletes it. Both are now hidden first.

### A guard against the whole class

`tests/layoutaudit.h`: **no visible widget may sit outside every layout**.
A QMainWindow is trusted to place its own direct children; everything inside
them is checked. It found bugs 1 and 3. It runs on the serial terminal
(`test_session122`) and **on the real MainWindow** (menu audit), and each
window the revamp touches from here on is added to it.

Files: `uistyle.h/.cpp` (`makePrimary`), `serialconsolewindow.cpp`,
`mainwindow_status.cpp`, `tests/layoutaudit.h` (new),
`tests/screenshot_main.cpp` (`SHOT_WINDOW=serial` draws a tool window).

### Tests

`tests/test_session122.cpp`, 8 checks:

- no orphans, before and after the macro row is rebuilt;
- the connection is one panel with Open inside it; Open is primary;
- the state is a chip with no colour of its own: neutral when closed, fail
  when a port will not open;
- macros are chips, warn for ask-first ones.

Menu audit (+1, 157): no orphans in the main window. The serial suites
(85, 101–111) all pass unchanged.

Gate: 11/11 validators, `dltests` 174 suites / 5471 checks, menu audit
157, smoke. All green.

---

<a id="session-121"></a>
## Session 121 — UI revamp, step 5: lanes over the log

The timeline-first idea from the design study, brought into the "Control
Room" frame: **what happened when, at a glance, above each tab's log.**

### What the operator sees

A band of five lanes under the activity ribbon, over the newest stretch of
the tab's traffic:

- **Mode**: the loco's mode as named segments ("Trip", "Full_Supervision"),
  a stable colour per mode.
- **Safety**: emergency spells (fail colour) and overspeed (warning).
- **RFID**: each tag read as a tick with its number.
- **Link**: gaps where the source went quiet.
- **Faults**: raised (fail) and cleared (ok).

**Hover** a mark for what it is ("Mode Trip at 14:02:27", "RFID tag 902 at
14:02:20", "No traffic 14:03:10 – 14:03:25 (15 s)"). **Click** a lane and
the log jumps to that moment, the same jump as clicking the ribbon. A source
with nothing the lanes can show (text diagnostics) shows no band.
**View ▸ Lanes over the log** turns it off, and the choice is remembered.

### Nothing new is decoded

The band draws `RunReport::summarise()` over a window: the engine the run
summary and the incident pack already use. So the lanes cannot say
something those reports do not. The test checks exactly that against a
direct call.

### Cost, bounded

`summarise()` decodes the frames in its window, so:

- the window is the newest 15 minutes, or the newest 20,000 rows if that
  is shorter;
- it rebuilds at most every 3 s;
- **a band on a tab that is not showing does no work**. It is rebuilt when
  its tab is shown, because the tab switch refreshes it.

Files: `laneband.h/.cpp` (new, in `dlcore.pri`), `mainwindow.h`,
`mainwindow.cpp`, `mainwindow_tabs.cpp`, `mainwindow_shell.cpp` (View menu
entry); `tests/screenshot_main.cpp` (waits past the 3 s debounce).

### Tests

`tests/test_session121.cpp`, 12 checks, on 2,000 real lines from `replay/`:

- **the lanes' summary equals `RunReport::summarise()` over the same
  window** (first mode, mode changes, tag reads);
- a loco's traffic gives a visible band of five lanes;
- hovering the mode lane names the mode;
- a click asks the log to jump to a moment inside the window;
- the window ends at the newest row and spans at most 15 minutes;
- text-only traffic gives no band;
- View off hides it, and on brings it back;
- a hidden band is not rebuilt, and is rebuilt once shown.

Gate: 11/11 validators, `dltests` 173 suites / 5463 checks, menu audit
156, smoke. All green.

---

<a id="session-120"></a>
## Session 120 — UI revamp, step 4: the decoded inspector's summary card

### What the operator sees

Selecting a frame now shows **what it is before what its fields are**. A
card over the field table gives:

- the packet and its size (`LSRP · 39 B`);
- **the CRC as a chip**: `✓ CRC pass` in the ok tone, `✕ CRC fail` in
  the fail tone;
- when the reject rules match, `▲ would not be processed (2)`, with the
  reasons on its tooltip. That is the same wording as the status line, and
  a statement of what a receiver would do, not a verdict;
- **the key values as large tiles**: loco mode by its name ("Trip", not "7
  (Trip)"), speed, location, frame number, last RFID tag. The first three
  present are taken, matched case-insensitively, so the DMI's lower-case
  fields are found too ("On_Sight · 0 km/h · 153.749 km").

The table below is unchanged (right-click to pin, plot, copy, locate; click
a field to light its bytes), except that **values are in the mono face**,
digit under digit, and failed rows use the theme's error colour. They used a
literal `#cc3300` that bypassed the contrast-audited palette.

### Width, carefully

The first version raised the inspector's minimum width: three large tiles
side by side made the right dock grow and squeezed the Sources panel until
its counters clipped. Fixed so that:

- **the card never decides the dock's width**;
- **tiles show only when they fit whole**: the first always, the others
  as room allows, recomputed on resize. A cut "16382" is worse than no tile.
  The full value is on the tooltip.

Each attempt was checked on a screenshot of the real window with traffic.
The second attempt let the tiles share the width equally, and showed every
one of them cut.

### Also

`UiStyle::monoFont()` (session 117) only looks for **installed** fallback
families. Asking Qt for a missing one made it build its whole font alias
table (Qt measured and reported 136 ms).

Files: `fieldinspector.h/.cpp`, `uistyle.cpp`;
`tests/screenshot_main.cpp` (selects an `@lsrp` row in the visible table so
the inspector shows).

### Tests

`tests/test_session120.cpp`, 11 checks, on real frames from `replay/`:

- the LSRP card: `LSRP · 39 B`, then Loco mode=Trip, Speed=0 km/h,
  Location=163821 m, in that order;
- the CRC chip in the ok tone;
- every value mono;
- a DMI frame's lower-case fields are found;
- a text row has no card, and clear hides it;
- **the real LSRP with its last byte changed shows `CRC fail` in the fail
  tone**.

Gate: 11/11 validators, `dltests` 172 suites / 5451 checks, menu audit
156, smoke. All green.

---

<a id="session-119"></a>
## Session 119 — UI revamp, step 3: the Sources panel

### What the operator sees

Each source is now a two-line row: **a health dot, the name, and a short
line under it**. The message count and the silence stay in their columns,
which still sort, in the mono face.

- **Health:**
  - live: a filled dot in the ok colour;
  - late: a ring in the warning colour, and the line says "quiet";
  - offline: a ring with a slash in the error colour, "offline";
  - never heard: a hollow muted ring.

  The shape changes as well as the colour (not colour alone), and late and
  offline also say it in words. The thresholds are the same members as the
  tab-title colouring (Settings ▸ tab amber / red after), so the panel and
  the tabs cannot disagree.
- **Rate:** the line under the name gives each source's current rate
  ("12/s", "0.4/s", "idle"). It comes from how the count moved since the
  last refresh; a trimmed or cleared tab counts as no traffic, not negative
  traffic. When the row shows a friendly name, the line leads with the key.
- **Silence reads as a duration:** "42s", "12m", "14h", "96d". It used to
  be raw seconds, and "53195s" did not fit its column (the first digit was
  clipped).

### How it is built

`SourceRowDelegate` (new, in `dlcore.pri`) paints the rows. The tree, its
items, columns, sorting, filtering, italics for closed tabs and activation
are all unchanged. `refreshSourceList()` sets the health and meta roles.

Files: `sourcerowdelegate.h/.cpp`, `mainwindow.h/.cpp`, `dlcore.pri`;
`tests/screenshot_main.cpp` (lets the window run a few status ticks before
the picture).

### Tests

Menu audit (+1, 156): in the real MainWindow, after one status tick,
**every source row carries a health state and a meta line**, drawn by its
own delegate.

Gate: 11/11 validators, `dltests` 171 suites / 5440 checks, menu audit
156, smoke. All green.

---

<a id="session-118"></a>
## Session 118 — UI revamp, step 2: the main window's frame

The "Control Room" frame, built from session 117's components.

### What the operator sees

- **A status strip across the top**:
  - the name;
  - a **search field that opens the command palette** (every menu command
    by name, Ctrl+P);
  - **the UDP state as a chip**: `● Listening on UDP/50002` in the ok tone,
    `✕ Bind failed: …` in fail;
  - **the serial port chips** (session 102);
  - **the loco and station clocks with their gap**, in the mono face.

  These all moved up from the status bar, which keeps the notices (and the
  undo offers), pins, queue, drops and disk.
- **An icon rail down the left**: all sources chronologically, search all
  sources, DMI, track diagram, two-loco view, incident report, serial
  terminal, Packet Maker and, at the foot, settings. Each is drawn in the
  theme's colours, with its name as tooltip and accessible name. **Every
  rail button is an existing menu command** (found by its menu text), so the
  rail and the menus cannot drift apart, and the menus still have
  everything.
- **A header over the log, in place of the column of buttons beside it**:
  - the source's name, large, with what the tab name does not say ("source
    81 · channel 2", or the friendly name);
  - the message count as a quiet mono readout;
  - Follow newest, Show date, Save log, Detach;
  - a **⋯** menu with Clear this tab, Clear all tabs and Check buffer. It
    follows "Check buffer" renaming itself to "Back to log".

  The log gets the width the column took.
- **The filter's All / Errors / Warns / IN / OUT** are drawn as one
  segmented control.
- **Log columns measured to the font.** Time, Source, Name, Dir and Sev were
  fixed pixel widths (Time 100 px), and "16:32:27.012" plus padding does not
  fit 100 px at a 13 px font: the column read "16:32:27.0…" and its header
  "ime (local". Each is now the wider of a sample value and its header, in
  the fonts they are drawn in. This was a pre-existing problem, found in the
  first screenshot with traffic.

### Nothing removed, only moved

The buttons that stood beside the log keep their objects and their
auto-connected slots. The everyday ones are in the header. The others are
hidden, and the ⋯ items click them. So Undo, the shortcuts, and the tests
that click them by name all work unchanged. The frame is built before the
saved window layout is restored, so a saved layout applies onto it.

### Two traps met on the way

- `QStatusBar::removeWidget()` **hides** the widget it gives back.
- A widget placed straight into a `QToolBar` has its visibility run by the
  toolbar's action.

Together they made the UDP chip invisible in the first screenshots, so it
sits in a container of its own, as the clocks do. The serial chips, which
must come and go, are shown and hidden through their toolbar action. And
the first `currentChanged` arrives before a new tab is registered, so the
header also refreshes on the status tick.

Files: `mainwindow_shell.cpp` (new, in `dlcore.pri`), `mainwindow.h`,
`mainwindow.cpp`, `mainwindow_status.cpp`, `filterbar.cpp`,
`logtableview.cpp`, `mainwindow_tabs.cpp`, `uistyle.cpp`;
`tests/screenshot_main.cpp` and `tests/shot.pro` (the screenshot harness,
outside the gate, now feeds real `replay/` lines so shots show the window in
use).

### Tests

Menu audit (+9, 155 now), on the real MainWindow:

- the fixed strip and rail;
- the rail carries the everyday tools, **each one a menu command too**, each
  with an accessible name;
- the header replaces the button column;
- More holds three items;
- the clocks are in the strip;
- the UDP chip is shown and carries the bind's tone.

Every existing check passes unchanged (clear tab and its undo offer, detach,
pop-outs, the serial chips).

Gate: 11/11 validators, `dltests` 171 suites / 5440 checks, menu audit
155, smoke. All green.

Not yet in this step:

- the sources panel as a list with health dots and rates;
- the decoded inspector's restyle;
- the lane band over the log;
- the tool windows.

Those are the next steps, one patch each.

---

<a id="session-117"></a>
## Session 117 — UI revamp, step 1: the component foundation

Asked for: a full revamp of the UI, keeping every function, because "right
now the app looks like any other Qt desktop app". Direction chosen (asked to
choose, for a tool meant to be the primary way of debugging all loco data):
**"Control Room"**. It is a dense, dark-by-default operator console with an
icon rail, a strip of live status chips, sources with health dots, a log
with an activity ribbon, and an always-open decoded inspector. The
timeline-first idea comes in later as a lane band over the log. The three
mock-ups the choice was made from are on a design canvas.

Scope agreed:

- the main window first, then every tool window step by step, one patch
  each;
- the menu bar kept, slimmer;
- all 11 themes kept and mapped onto the new design.

This step builds the parts. The main window is rebuilt from them next.

### Components, chosen by role

`UiStyle` gains components that any window opts into with one call. The
stylesheet selects on a `dlRole` property, so every theme restyles them,
and, as everywhere in `uistyle.cpp`, no colour is chosen in code:

- **chip** (`makeChip`, `setTone`): a pill for live status
  (`● UDP 50002 · 312/s`), in five tones: neutral, accent, ok, warn,
  fail. **Its text is lifted to the contrast floor against its own tinted
  fill** (`UiColor::withContrast`: 4.5:1, or 7:1 in High Contrast), so a
  chip cannot ship unreadable in any theme.
- **segmented control** (`segmented`): exclusive buttons drawn as one
  group (All | Errors | Warnings | In | Out).
- **rail button** (`makeRailButton`): square and icon-only. Its tooltip is
  also its accessible name, since an icon is not a label.
- **section caption**, **panel** and **strip** surfaces; **`makeMono`**;
  a **spacing scale** (`space()`).
- **`UiIcons`**: twelve icons drawn from paths in the theme's colours
  (log, dmi, track, serial, send, report, settings, search, twoloco, more,
  pin, filter). No image files, and no Qt SVG module added to the build.

### The base, refreshed for every window

- Rounder corners (radius 6).
- Buttons: the border goes quiet, the fill carries the shape, a little more
  room, medium weight.
- Inputs: a touch taller.
- Table headers: a quiet caption with a hairline under it, no separators
  between columns; views sit in a hairline frame.

The tab rules are untouched: they carry the clipping fixes the tab-metric
suite guards.

### A robustness fix found on the way

`UiStyle::monoFont()` trusted the platform's "fixed font", and the
offscreen plugin (and some Linux setups) answers with the **proportional** UI
face. Timestamps, hex and values would then lose their columns silently. It
now checks the face really is fixed-pitch and falls back through known
monospace families.

Files: `uistyle.h`, `uistyle.cpp`.

### Tests

`tests/test_session117.cpp`, 20 checks:

- every chip tone meets the contrast floor **in all 11 themes**; High
  Contrast asks 7:1, the rest 4.5:1; tones differ by fill;
- roles reach the stylesheet, and tone changes in place;
- segmented is exclusive;
- a rail button's accessible name; captions are upper case; panels paint
  their surface; `makeMono` is fixed-pitch;
- the spacing scale;
- every icon draws something and differs from every other; an unknown
  name draws a visible box; icons take the colour asked for.

Gate: 11/11 validators, `dltests` 171 suites / 5440 checks (the contrast
audit and tab metrics included), menu audit, smoke. All green.

---

<a id="session-116"></a>
## Session 116 — DLConsole builds and passes on Windows again; CI on Windows and Linux

Item 4 of the review list. Windows is the primary target, and nothing had
been built there since the library split (patch 88). Now GitHub Actions
builds and tests on **Windows (MSVC 2019, Qt 5.15.2)** and runs the full
gate on **Linux** on every push (`.github/workflows/build.yml`).

**Run 10 is green on both:**

- **Windows**: the library, `DLConsole.exe` at `<build>\DLConsole.exe` (as
  `app.pro`'s `DESTDIR` promises), `dltests` 170 suites / 5234 checks, 0
  failed, and the menu audit passed.
- **Linux**: `verify.sh` complete, with the Linux-only serial paths (pty
  parity refusal, `-lutil`, the low-latency ioctl) passing for the first
  time.

### What the Windows build found, in the order it found it

1. **`__attribute__((packed))` is GCC-only** (`Structures.h`, three
   tests). MSVC stopped on the first wire struct. `#pragma pack(push, 1)`
   already packed them on every compiler, so the attribute is gone, and the
   `static_assert(sizeof == 7)` proves the header on each compiler.
2. **`class LogEntry;` for a `struct`** (`dmipanel.h`,
   `tabpopoutwindow.h`). MSVC mangles the two differently, so
   `MessageDispatcher::entryAppended` was unresolved at link time.
   - Fixed.
   - `-Werror=mismatched-tags` now makes clang and GCC fail on this before
     Windows sees it.
3. **Greedy `\x` in a test literal.** `"LOCO_MODE\x1F1_1"` is
   `LOCO_MODE`, U+01F1, `_1`: **one** part. Clang accepted it, so
   pinboard's "two-part line from an earlier build loads" never tested a
   two-part line. Split, and both parts are now checked. A sweep found no
   other case in code.
4. **Sources read in the Windows code page.** Without `/utf-8`, MSVC turned
   every non-ASCII literal into mojibake: the `▸` in menu paths, `—`
   dashes, the `●`/`✕`/`⚠` status glyphs, `␇`, `“”`. **A real defect on
   screen**, which 18 failing checks showed. `/utf-8` now applies to the
   library and every program.
5. **A file name Windows forbids.** The flasher history test wrote a name
   containing `"`. On Windows the file did not exist and the image never
   loaded. Windows now uses `'`, checking that the comma still makes CSV
   quote the field. Unix keeps the quote-doubling case.
6. **Offscreen draws no text on Windows.** The pixel-counting suites saw 0
   inked pixels. Windows CI runs the tests on the native platform (the
   runner has a desktop), and they pass. The tab-scroll check runs only
   where the window really gets its 1900 px: the runner's screen held it
   to 1028, where scroll arrows are right. It says so in the log rather
   than passing silently.
7. **Logs cut short.** A piped stdout on Windows lost its tail, and then the
   summary. Both test programs now run unbuffered.
8. **The menu audit was a GUI-subsystem program**, so PowerShell got no exit
   code, and a passing audit ("menu audit passed", 138 ok) read as failed.
   It is now a console program, like `dltests`; CI prints its exit code,
   in hex for a crash.

Files: `.github/workflows/build.yml`, `Structures.h`, `dmipanel.h`,
`tabpopoutwindow.h`, `core/core.pro`, `dlcore_link.pri`,
`tests/main.cpp`, `tests/menuaudit_main.cpp`, `tests/menuaudit.pro`,
`tests/test_pinboard.cpp`, `tests/test_flasher.cpp`,
`tests/test_tabmetrics.cpp`, `tests/test_session90.cpp`,
`tests/test_pipeline.cpp`, `tests/test_bookmarks.cpp`, `CLAUDE.md` (the
"not yet built on Windows" note replaced).

Local gate: 11/11 validators, `dltests` 170 suites / 5420 checks, menu
audit 146, smoke. All green.

Not tested on Windows:

- the headless smoke (the app fed live UDP), which runs on Linux and macOS
  only;
- serial on real COM ports: the pty-based serial suites are Unix-only, so
  Windows runs 5234 checks to Linux's 5420;
- an installer or deployment (`windeployqt`).

---

<a id="session-115"></a>
## Session 115 — Serial settings that survive a COM renumber and a new laptop

Items 8 and 9 of the review list.

### 8. Settings follow the adapter, not the COM number

Per-port settings (session 102) were keyed by the port's name. Windows
renumbers COM ports, and when the same adapter came back as COM7 its
settings from COM5 were not found. Profiles (session 105) likewise pointed
at a name.

- **Port settings are also saved under the adapter's USB serial number**
  (`serial/adapters/<serial>`) when it has one, and that copy wins when
  loading: the same card under whatever name it has today. A different
  adapter on the same COM number does not inherit them. Ports with no
  serial number (virtual, built-in UARTs) work by name as before.
- **A profile remembers its adapter** (saved from the terminal). **Open
  all** and opening a profile look it up by serial number, using the same
  enumerator as auto-reconnect (session 106). So "IOA Input", saved on COM5,
  opens on COM7, with its name and settings. A profile without an adapter
  keeps its port name.

### 9. Serial settings in File ▸ Export / Import settings

A new **Serial profiles, macros and port settings** section carries the
profiles with their macros, the default macro row, and each port's and
adapter's settings. The summary line reads e.g. "3 profiles, 2 default
macros, settings for 4 ports".

As with tags, import replaces the section whole and accepts only keys under
its own prefixes, so a hand-edited file cannot use it to set, say, the UDP
port. The bundle's single per-section INI prefix became a list to allow
this.

Files: `serialmanager.h/.cpp`, `serialconsolewindow.cpp`,
`settingsbundle.h/.cpp`.

### Tests

`tests/test_session115.cpp`, 15 checks:

- an adapter's settings saved as COM5 load as COM7, under the new name;
  another adapter on COM7 gets nothing; no serial number means by name; the
  adapter's copy wins;
- a profile keeps its adapter, resolves to where the adapter is now, and
  **Open all opens it there over a real pty** with its name; a profile
  without an adapter keeps its name;
- Serial in Export settings: a profile with a confirm macro ending in CR
  alone, the default macros and an adapter's settings go from one ini to a
  fresh one intact; `udp/port` does not travel.

The existing settings-bundle suite (27 checks) passes unchanged.

Gate: 11/11 validators, `dltests` 170 suites / 5419 checks, menu audit,
smoke. All green.

Not tested: a real adapter renumbered by Windows. The serial number is
stood in for by the injectable enumerator.

---

<a id="session-114"></a>
## Session 114 — Time read alike everywhere; two windows that never received their results

Items 6 and 7 of the review list, plus **two real bugs found while doing
item 7**: archive search and the DLR player's progress readout.

### Archive search never showed a result (a real bug)

`ArchiveSearcher` emits `finished(ArchiveScanResult)` on its worker
thread. The window receives it by queued signal, which needs the type
registered with Qt. It was not. Qt dropped every result with a warning on
stderr ("Cannot queue arguments of type 'ArchiveScanResult'"), so
**Tools ▸ Archive search ran, finished, and showed nothing.**

The existing tests called `scanArchive()` directly, never through the
thread, so nothing caught it. Proved with a receiver on the GUI thread
before the fix, as the window has; fixed with `Q_DECLARE_METATYPE` plus
registration in the searcher's constructor.

### The DLR player's progress never moved (a real bug, same cause)

A sweep for the same pattern found `DlrPlayer::Stats`: declared as a
meta-type, never registered. Every `progress()` update from the player's
thread was dropped, so the readout sat still for the whole playback. Proved
with a real `.dlr` played to a GUI-thread receiver: 0 updates arrived,
while `finished(bool)` arrived, since `bool` needs no registration. It is
now registered in the player's constructor.

The sweep also covered the rest:

- exporter, save, log writer: plain types only;
- flasher and round-trip: already register theirs;
- the UDP receiver: registered only in the app's `main()`, so any other
  program starting one (a test, the menu audit) would have hit the same
  trap. It now registers in its own constructor too.

### 6. The newest time, kept rather than searched for

The filter bar scanned every row on each keystroke to find the newest time
for `last:`. `LogModel::newestMs()` is now kept as rows go in (append,
batch append, restore), and reset by clear and take-all. Trimming drops only
the oldest rows, so it stays right. It is the newest, not the last appended,
since a replay can be out of order.

### 7. Archive search reads time as a tab's filter does

- **Clock times are in the zone the tables show**: the worker is given
  `Settings::showUtc()`.
- **`last:` counts back from the newest record in the files searched.**
  Before, it counted from now, which on last week's archive matched nothing.
  Finding the newest is a full read, so it runs **only when the query uses
  `last:`** (`LogQuery::usesDataEnd()`); other searches cost nothing extra.

Files: `logmodel.h/.cpp`, `filterbar.cpp`, `logquery.h/.cpp`,
`archivesearch.h/.cpp`, `archivesearchwindow.cpp`, `dlrplayer.cpp`,
`udpcommunication.cpp`.

### Tests

`tests/test_session114.cpp`, 17 checks:

- `newestMs`: out-of-order appends, trimming, restore, clear, take-all;
- an archive of real `@lsrp` lines from 27 June, searched **through the
  worker thread to a GUI-thread receiver** (as the window does; a
  `QSignalSpy` connects directly and would hide the bug):
  - results arrive;
  - `last:3m` finds the archive's last 4 records (from now: 0);
  - a local clock window on a past day;
  - the same window by UTC clock with the tables in UTC;
  - a query without `last:` is unchanged;
  - `usesDataEnd()`;
- the DLR player playing that archive: progress updates reach the GUI
  thread.

Gate: 11/11 validators, `dltests` 169 suites / 5404 checks, menu audit,
smoke. All green.

Not tested: the two windows themselves (Archive search, DLR player) with
their widgets. The signals they connect to are tested as they connect to
them.

---

<a id="session-113"></a>
## Session 113 — Tests: run by hand as the gate runs them; session 85 on macOS; no shared-ini leaks

Items 3 and 5 of the review list. Item 3 turned out to be a **wrong
diagnosis of mine**, which this session corrects.

### What really happened in session 105's red gate run

Session 105's notes blamed "the display". That was wrong:

- `verify.sh` already runs every stage offscreen.
- To "reproduce", I re-ran three pixel-reading suites (tabmetrics, flasher,
  presentationmode) **by hand, without offscreen**, as CLAUDE.md's
  one-suite command does. On a real macOS display those suites fail
  regardless, on any build, so comparing against patch 104 showed nothing.
- What actually failed was the OS refusing file access ("Operation not
  permitted", macOS privacy on `~/Desktop`), which stopped the menu audit
  and the smoke app from building.

A correction note now sits in session 105's section.

### Fixes

- **`dltests` and `menuaudit` default to offscreen** when
  `QT_QPA_PLATFORM` is not set. A suite run by hand now behaves as it does
  in the gate. Set the variable explicitly to watch on a real screen.
- **`verify.sh` names an OS file-access refusal.** When a build or the unit
  log shows "Operation not permitted", it prints that this is an
  environment problem, not the code (exercised against a sample log).
- **Session 85's end-to-end serial test runs on macOS** (`openpty` from
  `<util.h>`), not only on Linux: 48 checks here, up from 25. It had never
  run on this machine; it passed apart from one Linux-specific assumption.
  A Linux pty refuses even parity, while a macOS pty accepts it. That check
  is Linux-only now, and macOS checks that what opened is what was asked.
- **Tests no longer leak through the shared ini.** Running session 85 here
  exposed it: its terminal ends in Hex mode and saves that on close, so
  sessions 102 and 110 then found their text sends refused as "not hex".
  - Session 85 now puts back every `serial/` key it changed.
  - Sessions 102 and 110 set the Hex box they depend on rather than
    assuming it.
  - The gate was run twice in a row to confirm nothing carries over.

Files: `tests/main.cpp`, `tests/menuaudit_main.cpp`, `verify.sh`,
`tests/test_session85.cpp`, `tests/test_session102.cpp`,
`tests/test_session110.cpp`, `CHANGELOG.md` (the session 105 correction).

Gate: 11/11 validators, `dltests` 168 suites / 5387 checks (session 85 now
counts in full), menu audit, smoke. All green, twice in a row.

---

<a id="session-112"></a>
## Session 112 — Dispatcher: a lookup that hid a source's tab; NULs checked

Items 1 and 2 of the review list that followed the serial work.

### A lookup that created models, and so hid tabs (a real bug)

`MessageDispatcher::modelForKey()` created a model on a miss. A model made
by a lookup is one that ingest later finds already there, so `created`
comes back false and `tabRequested` never fires. MainWindow, with no tab
for that key, returns early. **That source's traffic was written to disk
but never shown.**

It took very little to trigger. Bookmarks persist across sessions, and
removing one (Bookmarks ▸ Remove) for a source not yet heard from today
did a lookup on its key. When that source's traffic arrived, its tab never
appeared.

**Fix:** `modelForKey()` is now a pure, const lookup that returns null for
an unseen source. `ensureModel()` creates. It is used by ingest, by
`onTabRequested` (restoring a tab at start-up, before its traffic; that is
the tab UI itself, so nothing waits on an announcement), and by test
fixtures. Every other production caller was read: each acts on a tab or
source that already exists.

The menu audit caught the one legitimate creator on the first gate run:
restoring saved tabs.

### NULs on the way into a tab (checked, not a bug)

After patch 107 found Qt 5's `fromUtf8(QByteArray)` stopping at a NUL in
the terminal, the console path was checked as well. `buildEntry` converts
with an explicit length, so an embedded NUL does not cut a line short.
Only trailing NUL padding is stripped, as designed, and the raw bytes keep
everything. A test now holds that in place.

Files: `messagedispatcher.h/.cpp`, `mainwindow_tabs.cpp`; test fixtures in
`test_comparetools`, `test_comparefind`, `test_session82`, `test_session98`
now call `ensureModel`.

### Tests

`tests/test_session112.cpp`, 11 checks, with the real `@lsrp` line from
`replay/`:

- a lookup finds nothing and makes nothing;
- the source's first line then **announces its tab** (before: swallowed);
- the same model on every lookup; `ensureModel` finds or makes;
- an embedded NUL keeps the full length and the tail; trailing padding is
  stripped; raw bytes are intact.

Gate: 11/11 validators, `dltests` 168 suites / 5364 checks, menu audit,
smoke. All green.

---

<a id="session-111"></a>
## Session 111 — Serial: find the baud rate

From the serial test report: "Auto-baud. Try the standard rates for a second
each and pick the one where the most lines decode as @xxx frames." This is
the last item of that report.

### What the operator sees

A **Find** button beside Baud. It closes the port if it is open, then
tries the twelve standard rates for a second each, common ones first
(115200, 9600, 57600, 38400, 19200, …). The status shows `Finding the
baud rate: trying 57600 (3 of 12)…`, and the button becomes **Stop**.

- **It picks the rate where the most lines decode** as `@` capture frames
  (session 104's test). Ties go to the higher share. Most bytes is not
  the test: garbage at a wrong rate can be plentiful.
- **It stops early** once a rate is plainly right: 3 or more lines, at
  least 90% decoding.
- **Then it sets the baud and opens the port at it**, and the terminal says
  why: `── baud: 57600: 12 of 12 lines decode (115200: 0 of 9 · 9600: 0 of
  2) ──`.
- **When no rate decodes**, it claims none: `no rate gave decodable @
  lines (…)`. The card may be silent, or not print `@` lines. A port that
  will not open stops it at once, saying so.
- **The probe never feeds the console.** It reads on a link of its own, so
  the garbage at wrong rates reaches neither the console tab nor the port's
  line health.

### How it is built

`serialautobaud.h/.cpp`: `SerialAutoBaud`, with its own `SerialLink`.
It works through the rates on a dwell timer, counting lines and decoded
lines per rate. `pickBest`, `plainlyRight` and `summary` are static and
tested on their own. In the terminal, `findBaud()` and the Find/Stop
button.

### Tests

`tests/test_session111.cpp`, 21 checks:

- choosing: decoded lines beat raw volume; nothing decodes, so no rate;
  ties go to the share; the plainly-right threshold; the order; the summary
  text.
- **A stand-in card at a fixed baud**: a pty whose far end reads the speed
  the console set (a pty shares one termios between its ends; checked
  first). Like a real card read at the wrong rate, it gives garbage unless
  the speed is 57600, where it prints a real `@dop1` line from `replay/`.
  - The engine finds 57600 and stops before trying 19200; the wrong rates
    show lines that did not decode.
  - A card that never prints `@` lines gives "no rate…".
  - A port that will not open stops it at once.
  - In the terminal: open at 9600 (0% decode), Find, the port is closed
    while it looks, then found, set and opened at 57600. The console tab
    received **nothing** from the probe, and the card's lines now decode
    100%.

Five consecutive runs, no failures. The first version of the test card
barely printed: it restarted its own tick on every call. That was a test
bug, caught because the engine honestly reported "0 of 0".

Gate: 11/11 validators, `dltests` 167 suites / 5353 checks, menu audit,
smoke. All green.

Not tested: a real card at a real wrong baud. Garbage at a wrong rate is
stood in for by a fixed byte pattern.

---

<a id="session-110"></a>
## Session 110 — Serial terminal: Show only and Highlight, in the console's query language

From the serial test report: "Highlight and filter in the terminal view,
e.g. regex highlight and 'show only @dop2'. Reuse your query language so it
matches the main window."

### What the operator sees

A row above the terminal view has **Show only** and **Highlight** boxes.
They use the same language, the same completer and the same error marking
as a tab's filter in Query mode (`LogQuery`, `QueryLineEdit`):

- `@dop2`, `NOT @dop`, `msg:/LINK\s+FAIL/`, `field:SIG_OV=1` (decoded
  from the line, as in the console), `dir:out` (what was sent), and `time:`
  ranges all work.
- **Show only** re-filters what is already on screen, not just what
  arrives next. The view is rebuilt from the last 20,000 lines received.
- **Highlight** marks matching lines with a leading `▶` and a tint (the
  palette highlight, as the raw-bytes panel uses): not colour alone. Both
  boxes work together.
- **Markers always show** (`── opened …`, `── lost …`, `── sending …`):
  what happened to the port is never filtered away.
- **A broken query filters nothing.** It is marked in the box and named
  beside it, never "matches nothing", which would look like a silent card.
- **The log file and the console tab get every line**, whatever the
  terminal shows.
- In **hex view** the two boxes are off, because hex rows are not lines.

### A bug the tests found

`QPlainTextEdit::appendPlainText` gives a new block **the previous block's
format**. After one highlighted line, every line after it came out tinted
(seven instead of two in the test). Every line now sets its own block
format.

### How it is built

The terminal keeps a buffer of what it received: display text, raw bytes,
time, direction. Each line is matched as a `LogEntry` built like the one in
the console tab: the raw line as text (so `field:` decodes it), the port's
tab key for `src:`, and the direction for `dir:`. The queries are parsed
after a 200 ms debounce, and the view is rebuilt from the buffer. Hold
keeps its lines as before, with their highlight.

Files: `serialconsolewindow.h/.cpp`.

### Tests

`tests/test_session110.cpp`, 18 checks, over a pty with real `@dop1` and
`@dop2` lines from `replay/`:

- "show only @dop2" re-filters lines already received and applies to new
  ones;
- markers stay;
- a regex, `NOT`, and `dir:out` (the sent line) work;
- a broken query is named and filters nothing;
- Highlight marks exactly the two `@dop1` lines, with `▶`; it works with
  Show only, and clears;
- the console tab got all six lines;
- hex view turns Show only off.

Gate: 11/11 validators, `dltests` 166 suites / 5332 checks, menu audit,
smoke. All green.

Not tested: the terminal assigns no severities, so `sev:` matches nothing
there (the console's colour rules do that, in the tab). That is why the
placeholder suggests `dir:out` instead.

---

<a id="session-109"></a>
## Session 109 — Serial: macro buttons, per profile, asking before the dangerous ones

From the serial test report: "Macro buttons. A row of user-defined commands
(STATUS?, DIAG?, RESET), saved per profile, with a confirm step on dangerous
ones like the send guard."

### What the operator sees

- **A macro row under the send row**: one button per macro, then
  **Edit…**. With none yet, it reads "none yet — Edit… to add STATUS?,
  DIAG?, RESET". It is titled with the profile's name when one is chosen.
- **A click sends it**:
  - text with the send box's escapes (`\r \n \xHH`) or hex bytes, then
    the macro's own line end (none, CR, LF, CR+LF);
  - nothing goes while the port is closed;
  - a hex macro that is not hex is refused by name, and nothing is sent.
- **Ask first**: such a macro is marked `⚠` on its button and asks "Send
  "RESET" to COM5? … The card acts on it." with **No** as the default.
  Cancelling sends nothing and says so.
- **Edit…**: a table with Button, Sends, Hex, Line end and Ask first,
  plus Add and Remove. Typing a label or text with RESET, REBOOT,
  RESTART, ERASE, FORMAT, FLASH, FACTORY, DELETE, CLEAR, BURN, WRITE or
  PROG(RAM) ticks Ask first by itself. It can be unticked.
- **Saved per profile**: choosing a profile brings up its macros. With no
  profile chosen there is a default set. Save as profile takes the row on
  screen with it.

### How it is built

`SerialMacro` (in `serialmanager.h`) has `bytes()`, `looksDangerous()`
and load/save. Macros are a nested array inside each `serial/profiles`
entry, with the default set in `serial/macros`. Line ends are stored as
hex, because an ini mangles CR and LF as text. In the terminal:
`loadMacros()`, `setMacros()` (which saves), `runMacro()`, the editor, and
a replaceable confirmer for tests.

### Tests

`tests/test_session109.cpp`, 28 checks:

- bytes for text, escapes, hex and line ends; bad hex refused;
- the dangerous-word list against harmless commands;
- two profiles with different macros round-trip, with confirm and the exact
  line end (CR alone, LF alone);
- **over a pty**:
  - buttons appear, the confirm one marked;
  - nothing goes while the port is closed;
  - a click puts `STATUS?\r\n` on the wire;
  - RESET asks: No sends nothing, Yes sends `RESET\r\n`;
  - a plain macro never asks;
  - switching profile, and back to none, switches the row.
- The suite restores the test ini's profiles and macros afterwards.

Gate: 11/11 validators, `dltests` 165 suites / 5314 checks, menu audit,
smoke. All green.

Not tested: the editor dialog itself (modal). Its result goes through
`setMacros()`, which is tested.

---

<a id="session-108"></a>
## Session 108 — Serial: paced file send with progress and Stop

From the serial test report: "Send file is one big write. There's no pacing
or progress, and the whole file is echoed as one giant TX> line, which is
garbage for a binary file." and "Paced file send with progress. Choose chunk
size and delay, send line by line waiting for a prompt, and show a progress
bar with Stop."

### What the operator sees

After choosing the file, **Send file…** asks how to send it (the choices
are remembered):

- **In chunks**: a chunk size (bytes) and a delay between chunks.
- **Line by line**: each line with its own line end. After each one the
  terminal waits for the card's prompt (e.g. `>`) before sending the next,
  giving up after the prompt timeout. With no prompt given, it waits the
  delay between lines instead.

While it runs:

- a **progress bar** (`%p% sent`; the tooltip gives bytes) and a **Stop**
  button sit in the send row;
- the terminal shows `── sending loco.cap: 18234 bytes, 1024-byte chunks
  every 5 ms ──` and, at the end, `── loco.cap: sent 18234 bytes in 0.2 s
  ──`. The file is **not** echoed as `TX>` lines.
- A failure says where it stopped and why:
  - `stopped at 2048 of 18234 bytes`;
  - `no "OK>" from the card within 5.0 s after line 1 of 2; stopped (5 of
    10 bytes sent)`;
  - `the port closed; … sent`.
- A second send while one runs is refused.

"Sent" means the port accepted the bytes. On a slow line, the last chunk
can still be in the driver's write buffer for a moment after the end line
appears. The tests met exactly this on a pty and wait for the bytes to
arrive.

### How it is built

- **`serialfilesender.h/.cpp`**: `SerialSendOptions` (mode, chunk, delay,
  prompt, timeout, `summary()`, saved under `serial/fileSend`);
  `serialSendPieces()`, which cuts the file; and `SerialFileSender`, which
  runs off GUI-thread timers while the link writes on its reader thread
  (session 103). It stops if the port closes, and watches the link's
  received bytes for the prompt.
- **The terminal**: the options dialog, the progress bar and Stop, summary
  lines, and no per-piece echo while a file is going out. A test hook,
  `sendFileData()`, skips the dialogs.

### Tests

`tests/test_session108.cpp`, 26 checks. The file sent is **a real capture
from `replay/`** (`loco_2_1_29062026_134128.cap`), sent back byte for byte
over a pty:

- chunks: sizes, and that they rejoin to the file; lines keep their own
  ends; options are remembered;
- paced chunks arrive **exactly**, take at least the sum of the delays, and
  report progress after every chunk;
- a second start is refused;
- Stop: what went is the start of the file, and nothing goes after;
- **line by line against a card the test plays**: it answers each line with
  `OK>`, and the next line never goes before the prompt;
- no prompt: it gives up naming "after line 1 of 2", having sent only the
  first line;
- the terminal shows start and end lines, not a `TX> @…` line, and the
  card gets the whole file.

Three consecutive runs, no failures.

Gate: 11/11 validators, `dltests` 164 suites / 5286 checks, menu audit,
smoke. All green.

Not tested: the options dialog itself (modal, so the test hook bypasses
it), and a real card's boot loader.

---

<a id="session-107"></a>
## Session 107 — Serial terminal: 16-byte hex rows; control characters and ANSI

From the serial test report: "Hex view prints whatever chunk read returned,
so rows have uneven lengths. Fixed 16-byte rows with an offset and an ASCII
column would be easier to read." and "Control characters and ANSI colour
codes are shown raw."

### What the operator sees

- **Hex view**: `00000010  40 64 6F 70 31 …  … |@dop1_2_1 2026-0|`. Each row
  is 16 bytes with a running offset, a gap after eight, and an ASCII
  column, however the driver split the reads.
  - The last partial row appears once the line has been quiet for 60 ms,
    padded so its ASCII column lines up.
  - The offset carries on from where it stopped.
  - Offsets restart when the port opens, when Hex is switched, and on
    Clear.
  - With timestamps on, a row is stamped with its first byte's time.
- **Text view**: ANSI escape sequences are removed (CSI colour codes, OSC
  titles ending in BEL or ST, two-byte ESC sequences). Other control bytes
  are drawn as Unicode control pictures: `␇` BEL, `␀` NUL, `␛` ESC, `␡`
  DEL. A tab stays a tab.
- **For the eye only.** The console tab, the decoder and line health get the
  line exactly as it came.
- **TX echo**: shows what was sent without its line end turning into
  `␍␊`.

### A bug the tests found

Qt 5's `QString::fromUtf8(QByteArray)` stops at the first NUL, so a line
containing a NUL was cut short on display, at exactly the byte this patch
is meant to show. The display path now passes the length. **Not fixed
here:** serial lines reach the console tab through the shared UDP
pipeline (`MessageDispatcher::ingestLocal` → the general payload-to-text
path), which was not changed. Whether that path has the same truncation,
for UDP as well, is a separate question for its own patch.

Files: `seriallink.h/.cpp` (`serialStripAnsi`, `serialDisplayText`,
`SerialHexDumper`), `serialconsolewindow.h/.cpp`.

### Tests

`tests/test_session107.cpp`, 26 checks:

- The real `@dop1` payload (exactly 16 bytes) as one exact row.
- The whole real line fed in uneven reads: only whole rows; row 0 shows
  `@dop1` in hex and ASCII; row 1 at offset 0x10; first-byte times; the
  padded partial row; the offset carrying on after a flush.
- Escape-code removal: CSI, OSC with BEL, OSC with ST, two-byte ESC, and
  look-alike text kept.
- The control pictures, a tab kept, and a coloured `@dop1` line showing as
  the line.
- Over a pty:
  - the terminal shows the text and `␇`, with no ESC;
  - the console tab gets the raw line;
  - the echo has no `␍`;
  - the hex view gives 16-byte rows from offset 0 across a split write,
    plus the partial row.

Gate: 11/11 validators, `dltests` 163 suites / 5260 checks, menu audit,
smoke. All green.

Not tested: session 85's Linux-only hex check, which waits 150 ms for 3
bytes; the 60 ms partial-row flush is chosen to meet it. ANSI colour is
removed, not rendered.

---

<a id="session-106"></a>
## Session 106 — Serial: auto-reconnect, by USB serial number

From the serial test report: "When the adapter comes back (matched by USB
serial number, not COM number, since Windows renumbers ports), reopen it and
log `── reconnected, gap 4.2 s ──`, so log gaps are visible."

### What the operator sees

- **A port the driver lost** (the adapter pulled, a ReadError/WriteError,
  or an error storm; session 101) is **waited for**, not forgotten. A
  **Close** is not a loss: a port the operator closed stays closed.
- **In the console tab**: `── lost COM5: …; waiting for it to come back ──`,
  then `── reconnected COM5, gap 4.2 s ──`, or `── reconnected as COM7
  (was COM5), gap 4.2 s ──`. The gap is visible where the log is read,
  whether or not a terminal is open.
- **It comes back into the same tab**, under the same profile name, with
  the same settings and Feed console, even when Windows gives it a new COM
  number.
- **The terminal** shows the lost and reconnected lines, follows a rename,
  and while waiting its button reads **Stop waiting**. The status reads
  `◌ COM5 lost — reconnecting when the adapter comes back (found by its
  USB serial number, under any port name)`.
- **The chip** reads `◌ COM5 IOA Input reconnecting…` in the warning
  colour while waiting.
- **Line health continues** across a reconnect, so the outage shows as the
  longest gap.

### How it is built

- `SerialLink::lost(why)` is emitted after `closed()`, only when the
  driver closed the port.
- At each open, `SerialManager` records the adapter's USB serial number
  (`QSerialPortInfo`). While a port is lost, it ticks once a second:
  - It finds the same serial number under any name and reopens.
  - With no serial number (virtual ports, built-in UARTs, ptys), it finds
    the port by name, or on Unix by the path still existing.
  - A port that is enumerated but not yet openable is tried again next
    tick.
- Entries are now shared. A renamed port is **one entry under both
  names**. Its console tab stays keyed by the name it first had, so a
  rename never splits a log across two tabs.
- `close()` and `closeAll()` end a wait.
- `setReconnectIntervalMs()` and `setPortFinder()` exist for tests.

Files: `seriallink.h/.cpp`, `serialmanager.h/.cpp`,
`serialconsolewindow.cpp`, `mainwindow_status.cpp`.

### Tests

`tests/test_session106.cpp`, 22 checks. The enumerator is replaced by a
stand-in, so the adapter can vanish and return on cue. The ports
themselves are two real ptys, and the line is a real `@dop1` from
`replay/`.

- The adapter is pulled: the port is waited for; the tab and the terminal
  say "lost"; the button offers Stop waiting; the manager keeps looking.
- It comes back **under the second pty's name**: the same link reopens with
  its baud and profile name; the tab says "reconnected as … gap"; lines
  from the new name land in the **same** tab, and no second tab is made;
  health continues; the terminal follows.
- Stop waiting ends the search.
- A Close is not a loss.
- With the real enumerator, a pty with no serial number is found again by
  its name.

Six consecutive runs, no failures.

Gate: 11/11 validators, `dltests` 162 suites / 5234 checks, menu audit,
smoke. All green.

Not tested: a real USB adapter unplugged and replugged, and Windows
renumbering a COM port. Matching by serial number is exercised only
through the stand-in enumerator.

---

<a id="session-105"></a>
## Session 105 — Serial: named port profiles

From the serial test report: "Named port profiles. For example 'IOA Input =
COM5 115200 8N1, feed on, auto-open'. Opening one starts all three IOA
cards in one click."

### What the operator sees

- **In the terminal**, a profile row: **Profile** box, **Open at start**,
  **Save as profile…** and **Delete**.
  - Choosing a profile fills in the port, the line settings and Feed console.
    If its port is already running, it shows that port as it runs.
  - Save stores the current settings under a name. An existing name, in any
    case, is replaced.
- **Tools ▸ Serial Profiles**:
  - **Open all profiles** starts every saved card in one click;
  - **Close all serial ports**;
  - one entry per profile, checked while its port runs (clicking a running
    one opens a terminal on it).
  - With none saved, it says to save one from the terminal.
- **Open at start**: those profiles open when DLConsole starts, once the
  window is up. An adapter that is not plugged in becomes a notification
  naming the profile and port (`Serial profile not opened: IOA Spare
  (COM9): …`). The console still comes up, on Ethernet.
- **A port opened from a profile carries its name** on the status chip
  (`● COM5 IOA Input`) and as its console tab's title ("IOA Input").
- **Open all never drops a running port**: one already open is left alone,
  not reopened.

### How it is built

`SerialProfile` (name, `SerialConfig`, feed, auto-open) is stored as the
`serial/profiles` array. `SerialManager` gains `openProfile`,
`openProfiles` (which returns `name (port): why` for each failure),
`setLabel`/`label` and `titleFor`. The terminal labels the port only when
the chosen profile's port is the one being opened. MainWindow gains
`openSerialProfiles(autoOpenOnly)` and the lazily built menu.

Files: `serialmanager.h/.cpp`, `serialconsolewindow.h/.cpp`,
`mainwindow.h/.cpp`, `mainwindow_menus.cpp`, `mainwindow_status.cpp`,
`mainwindow_tools.cpp`.

### Tests

`tests/test_session105.cpp`, 24 checks:

- three profiles round-trip (name, port, baud, feed, auto-open);
  replace-by-name in any case; delete; no stale entries; nameless entries
  skipped;
- **three ptys opened as three profiles in one call**, each with its own
  baud; labels; the tab titled "IOA Input" from a real `@dop1` line; the
  unfed card makes no tab;
- Open all again opens nothing anew;
- a missing adapter is reported by profile name and port, and the others
  still open;
- in the terminal: save, select (settings come back), open labels the port,
  Delete, and opening with no profile chosen leaves no label. The suite
  restores the test ini's profiles afterwards.

Menu audit (+1): with none saved, Tools ▸ Serial Profiles has Open all
disabled and says how to make one.

Gate: 11/11 validators, `dltests` 161 suites / 5212 checks, menu audit,
smoke. All green.

The first gate run went red for an environmental reason. Partway through,
the Mac's log files briefly reported "Operation not permitted", and three
pixel-based GUI suites failed (tabmetrics, flasher, presentationmode). The
same three failed on the **unchanged, already-pushed patch-104 build**,
which had passed the gate earlier. A rerun minutes later was fully green
with no code change: the display, not this patch.

> **Correction (session 113):** not the display. The gate already ran
> offscreen; the three suites were re-run by hand *without* offscreen, where
> they always fail, so that comparison showed nothing. The failure was the
> OS refusing file access ("Operation not permitted"), which stopped two
> builds. See session 113.

Not tested: auto-open at a real start-up with a real adapter. The menu
audit's ini has no profiles, so the start-up path runs with an empty list
only.

---

<a id="session-104"></a>
## Session 104 — Serial: line health per port

From the serial test report: "Line health per port: lines per second,
longest gap, lines that fail to decode, framing/parity error counts. A
wrong baud rate shows up immediately as 100% failing to decode."

### What the operator sees

- **In the terminal**, next to the status line:
  `12.0 lines/s · 100% decode · longest gap 1.2 s · 0 driver errors`.
  Once at least 10 lines have arrived and fewer than half decode, it turns
  the warning colour and adds "(check baud and line settings)".
- **On the status-bar chip**: `⚠ COM5` in the warning colour when the
  lines do not decode, otherwise `● COM5`. The tooltip carries the same
  health line, refreshed each second, so a port at the wrong baud is visible
  with no terminal open.
- **Reset** (in the terminal) clears the health along with the counters.
  Reopening a port starts its health afresh.

### Definitions

- **Decodes**: a well-formed `@` capture line of a type the console knows
  (`CaptureDecoder::parseLine`, `type != Unknown`). It is cheap enough for
  every line of every port, and it is exactly what wrong line settings
  break. A truncated but well-formed frame still counts. An `@` line of an
  unknown type does not.
- **Lines/s**: lines within the last 5 s, divided by 5.
- **Longest gap**: between two lines, since the port opened.
- **Driver errors**: every error the driver reported, including the ones a
  burst folds into a single report (session 101).
- **Health counts every line**, whether or not it is fed to the console.

### Framing and parity, said plainly

Qt 5.15 marks `FramingError` and `ParityError` obsolete and does not emit
them. Separate framing/parity counts would therefore always read 0, which is
worse than none. The decode rate is the signal for wrong settings instead,
and the tooltip says so.

Files: `serialmanager.h/.cpp` (`SerialLineHealth`, `health()`,
`healthText()`), `seriallink.h/.cpp` (`errorCount()`),
`serialconsolewindow.h/.cpp`, `mainwindow*.cpp` (chip refresh).

### Tests

`tests/test_session104.cpp`, 25 checks:

- which real lines decode (`@dop1`, `@lsrp`) and which do not (wrong-baud
  bytes, a prompt, an unknown `@` type);
- the rate, gap and judging arithmetic;
- five errors in a burst count as five;
- over a pty, 12 real lines give 100%, shown in the terminal; 12
  wrong-baud lines give 0%, failing, with the label in the warning colour
  and naming what to check;
- health counts lines that are not fed to the console;
- Reset and reopening start afresh.

Menu audit (+1): wrong-baud bytes on a port in the real MainWindow turn
its chip into `⚠`, with "check baud" in the tooltip. The audit's port does
not feed the console, because a later check counts that window's tabs (the
first run of the gate caught exactly that).

Gate: 11/11 validators, `dltests` 160 suites / 5188 checks, menu audit,
smoke. All green.

Not tested: a real adapter at a genuinely wrong baud rate. The garbage here
is a fixed byte pattern standing in for one.

---

<a id="session-103"></a>
## Session 103 — Serial: read on its own thread; low-latency FTDI

From the serial test report: "Timestamps can lag" and the FTDI latency
timer.

### Why

The port was read on the GUI thread. While the GUI was busy (loading a
replay, rebuilding a big table), bytes waited in the driver and were
stamped when the GUI got round to them. IOA logs are lined up against VCC
logs by time, so a late stamp is a wrong answer. The UDP receiver already
reads on its own thread for this reason.

### What changed

- **`SerialLink` runs a `SerialPortWorker` in a `QThread` of its own**, one
  per port. The `QSerialPort`, the line splitter, the idle timer and the
  error-storm guard (session 101) now live there. Bytes are stamped on that
  thread at the moment they are read, and reach the GUI by queued signal,
  already stamped.
- **The public API is unchanged and synchronous.** `open`, `close`,
  `write`, `setDtr` and `setRts` call into the worker and wait. RX/TX
  counters and the idle time are atomics. Lines already read are delivered
  before a close returns, then the held partial line, then `closed()`.
  When the worker closes on an error, the flushed lines come with that
  error report.
- **Low latency** (`SerialConfig::lowLatency`, default on, a **Low
  latency** box in the terminal, remembered per port). An FTDI adapter
  holds bytes up to its latency timer: 16 ms by default, so up to 16 ms of
  jitter.
  - **Linux:** the link sets `ASYNC_LOW_LATENCY`, which `ftdi_sio` turns
    into a 1 ms timer.
  - **Windows:** the timer can only be set in Device Manager. The terminal
    says where when the port opens (`── low latency: set it in Device
    Manager ▸ … ──`) rather than pretending.
  - **macOS:** reported as not settable.
  - Never fails an open.

Files: `seriallink.h/.cpp`, `serialconsolewindow.h/.cpp`.

### Tests

`tests/test_session103.cpp`, 14 checks. The main one: the card prints a
real `@dop1` line while the **GUI thread is blocked for 600 ms**, and the
stamp is within 200 ms of the print, not 600 ms late. Run 15 times, with no
failures.

Also checked:

- each link reads on a running thread that is not the GUI's;
- writes and counters across the thread;
- the partial line arrives on close, before `closed()`;
- the latency note, and `lowLatency` remembered;
- an open link destroyed without hanging.

Sessions 85, 101 and 102 all pass unchanged on the threaded link.

Gate: 11/11 validators, `dltests` 159 suites / 5163 checks, menu audit
144, smoke. All green.

Not tested: the low-latency ioctl on a real FTDI adapter. It needs Linux
with a ttyUSB device; the pty here reports "not a serial driver that offers
it". Windows and its Device Manager note were not tested.

---

<a id="session-102"></a>
## Session 102 — Serial: background capture, a chip per port, settings per port

From the serial test report: "Closing the window stops the capture" and
"Settings are shared". These are the first of its enhancements, done in
order, one patch each.

### What the operator sees

- **Closing the terminal no longer stops the port.** It keeps feeding the
  console tab until someone presses **Close** (or DLConsole exits). The
  status line says "keeps running when this window closes".
- **A chip per open port in the status bar**, e.g. `● COM5`. Its tooltip
  gives the settings and whether it feeds a tab. Clicking it opens a terminal
  on that port.
- **Reopening Tools ▸ Serial Port Terminal shows the running port**, with
  its own settings, Feed console state and Close button.
- **One window can view several ports.** Choosing another port in the box
  views that one and leaves the first running. The line settings lock only
  while the port being viewed is open.
- **Settings are per port** (`serial/ports/<name>`: line settings and Feed
  console). Three IOA cards keep three sets; whichever window closes last no
  longer overwrites the others. A port with nothing saved yet falls back to
  the old `serial/last`.

### How it is built

- **`serialmanager.h/.cpp`**: owned by MainWindow and handed out, like
  `FrameNumberWatch`. It holds one `SerialLink` per port name (found
  case-insensitively, kept for its own lifetime so a viewer never holds a
  dangling link) and the feed into the dispatcher, which moved out of the
  window. It also handles the tab naming and per-port settings.
- **`serialconsolewindow`**: attaches to a manager's link. Typing in the
  editable port box makes no links; Open makes one. The old
  `SerialConsoleWindow(MessageDispatcher*)` constructor remains as the
  standalone form, with a manager of its own whose ports close with the
  window. Session 85's Linux-only end-to-end test uses it unchanged.
- **MainWindow**: creates the manager after the dispatcher (no port is
  touched), adds `openSerialTerminal(port)`, and `rebuildSerialChips()` on
  `portsChanged`.

### Tests

`tests/test_session102.cpp`, 31 checks, over a pseudo-terminal standing in
for the card, with real `@dop1` lines from `replay/`:

- the line reaches the tab with the window open, and **again after the
  window is closed and deleted**;
- a new terminal attaches to the running port with its settings, offers
  Close, and can send;
- Feed console off from the viewer stops the feed;
- Close closes the port;
- two ports at once, each with its own settings; `showPort`; switching
  views leaves the first running;
- no link per keystroke;
- per-port settings for three ports;
- the standalone window still closes its port.

Menu audit (+4): the chip row is present and hidden at startup. A port
opened in the real MainWindow's manager shows a chip named after it, with
no terminal open, and the chip goes when the port closes.

Gate: 11/11 validators, `dltests` 158 suites / 5149 checks, menu audit
144, smoke. All green.

Not tested: Windows COM ports and a real USB adapter. Session 85's
end-to-end serial test runs only on Linux, so it was not run here.

---

<a id="session-101"></a>
## Session 101 — Serial: three bugs found in testing

The bugs from the serial-interface test report. Its smaller problems and
enhancements are not in this patch (scope to be confirmed).

### 1. A log line printed in two parts was cut in two

`@dop1_1_1 … 0C 17 17 `, a 400 ms pause, then `17 17 0C 0C\r\n` arrived
as two lines, and neither half decodes. The 300 ms idle flush could not
tell "the card went quiet" from "the card is between two printfs".

Now a partial line that starts with `@` is a log line, not a prompt. It is
held for `kLogLineIdleMs` (5 s) instead of 300 ms, so a card that really
stopped mid-line still shows it. Closing the link delivers it at once, as
before. Prompts (`login:`, `x>`) still flush at 300 ms.

### 2. A stray empty line after a flushed prompt

`login:`, then `\r\n` 600 ms later, gave `[login:]` and then `[]`. After
an idle flush, the next CR, LF or CR LF now ends *that* line. A second line
end is still a genuine blank line.

### 3. A read error looped and never closed the port

When a virtual port's far end disappeared, Qt reported `ReadError` (code 8),
not `ResourceError`. The port stayed "open" and fired about 114,000 errors a
second, each one restyling the terminal's status label.

`SerialLink::onError` now closes on `ReadError` and `WriteError` as well as
`ResourceError`. Any error repeating 20 times within 100 ms also closes the
port ("… (repeated 20 times in 100 ms; port closed)"). Within a burst only
the first error and the one that trips the guard are reported, so the label
is styled twice, not 114,000 times. A stray framing or parity error now
and then is reported and leaves the port open.

### Files

`seriallink.h/.cpp`. `SerialLink::onError` is public, for tests only.

### Tests

`tests/test_session101.cpp`, 30 checks. Bug 1 uses the real `@dop1` line
from `replay/loco_2_1_29062026_134128.cap`, split where the tester split it
and checked to rejoin byte for byte. Bug 2 covers CR LF, a lone CR, a lone
LF, a later genuine blank line, and the session-85 behaviour.

Bug 3 uses a real open pseudo-terminal (`openpty`, Unix only): Read, Write
and Resource errors each close it; one parity error does not; a burst of 20
does, with one report and a reason.

Gate: 11/11 validators, `dltests` 157 suites / 5118 checks, menu audit,
smoke. All green.

Not tested: the tester's actual storm, end to end. On macOS, closing a pty's
far end makes Qt report nothing at all (no error, no readyRead), so a
vanished virtual port could not be reproduced here. A real USB adapter on
Windows was not tested either.

---

<a id="session-100"></a>
## Session 100 — Filter query mode: time that works, and a small query engine

Reported: time queries in the filter bar showed "could not read". Asked
for: the filter's query mode made "powerful like a mini querying engine".
Scope confirmed before building: fix time, plus clock-time ranges,
comparisons without a colon, value lists, and a replay-relative `last:`;
a time with no date matches that clock time on any date.

### What was wrong with time

Probed against the real parser, not guessed:

- **Refused:** `after:9:05` and `after:14:2` (one-digit hour/minute),
  `after: 14:02` (the space made it "'after:' has no value"), and
  day-first dates `08-08-2026`, `08/08/2026`.
- **Parsed and asked a different question:** `after:2026-08-08 14:02`
  split at the space, so `14:02` became a separate TEXT term ANDed on.
  It ran without an error and matched only rows whose text contained "14:02".
- **Matched nothing, silently:** every clock time meant *today*. On a
  replay, or any log from another day, `after:14:02` hid every row (and
  `before:` showed every row). That looks just like a quiet bus.

### What the operator sees

Query mode (the **Query** box on any tab's filter bar, and Tools ▸ Search):

| Type | Means |
|---|---|
| `time:14:02` | that whole minute, on any date |
| `time:14:02..14:09`, `time:14:02-14:09` | a clock window; `23:50..00:10` wraps midnight |
| `after:9:05`, `time>=9:05`, `before:14:09` | an upper bound covers the whole unit typed (14:09:45 is "before 14:09", as the Time column reads it) |
| `time:2026-08-08`, `time:08-08-2026` | that whole day |
| `after:08-08-2026 14:02` | date and time, unquoted; T, space or quotes all work |
| `last:15m` | the last 15 minutes **of this data**: counts back from the newest row, so it works on replays |
| `after:-15m`, `after:-1h30m` | relative to now, as before; compound durations |
| `len>64`, `len >= 10`, `sev!=info`, `dir=in` | comparisons without a colon, spaces allowed |
| `TRAIN_SPEED>60`, `FRAME_NUM&7=3`, `SIG_OV=1` | decoded fields without `field:` |
| `src:33_1,21_1`, `sev:warn,error`, `LOCO_MODE=6,7` | any of; with `!=`, none of |
| `len:10..64`, `ABS_LOCO_LOC=163000..164000` | inclusive ranges; either end may be open |

Clock times are read in the zone the Time column shows, and toggling
Local/UTC re-applies the filter. A `Z` or `+05:30` on a dated time
overrides the zone. The timeline ribbon's range drag now writes its stamps
in the displayed zone; before, they were local even in UTC mode.

### Refused, by name, never silently matching nothing

- `TRAN_SPEED>60` → "unknown field 'TRAN_SPEED' — write field:TRAN_SPEED
  to force a decoded field, or quote the term to search text". Unknown names
  are checked against the field catalogue and the active schema.
- `after:15m` → "a duration needs its sign: -15m counts back from now;
  last:15m counts back from the newest row".
- `after:06-27-2026` → month-first is refused, not guessed.
- `time:25:00`, `after:31-02-2026`, a dated range ending before it starts,
  an empty list item, `sev>info`, a mask on a non-decoded field.

Unchanged: a bare `14:02` is still a text search, `-`/`!` still negate, an
unquoted `hex:0a 1b` still splits, `msg:` never splits on commas, and an
invalid query still shows every row plus the error.

### How it is built

- `logquery.h/.cpp`: one `time` node (low/high bounds, epoch or
  ms-since-midnight) replaces `after`/`before`; `after:`, `before:`,
  `time:` and `last:` all build it. `parseTimePoint()` records the
  precision typed, so an upper bound covers the whole unit.
  `setUtc()`/`setDataEnd()` are set before `parse()`. The tokenizer joins a
  term across a space only after a field colon, around an operator, or
  between a date and its time on a time field. Colon-less comparisons are
  rewritten to the colon form and parsed by the same code, so the two
  spellings cannot drift. Lists and ranges expand into ordinary OR/AND
  terms, so each value still gets the catalogue's symbol resolution.
  Local time-of-day per row is cached per 15-minute slot (`thread_local`:
  archive search matches on a worker thread).
- `filterbar.cpp`: passes the column's zone and the newest row's time; re-applies on the UTC
  toggle; new tooltip. `searchwindow.cpp`: the same zone and anchor across
  tabs. `querylineedit.cpp`: `time:` and `last:` completions.
  `mainwindow.cpp`: ribbon stamps in the displayed zone.

### Tests

`tests/test_session100.cpp`, 125 checks: every reported failure, a row
from 27 June 2026 (not today) matched by clock time, the ribbon's local and
UTC stamps round-tripping, unit-precision bounds, midnight wrap, UTC mode,
`last:` against a data end, lists/ranges/colon-less comparisons, decoded
fields on the **real LSRP frame** from
`replay/loco_1_1_27062026_140226.cap` (FRAME_NUM 50548, LOCO_MODE 7,
ABS_LOCO_LOC 163821), each refusal, the old behaviours above, and a real
`FilterBar` over a past-dated model (row counts, and the UTC toggle
re-filtering).

Gate: 11/11 validators, `dltests` 156 suites / 5088 checks, menu audit
140, headless smoke 500 datagrams. All green.

Not tested: the Windows build. Archive search (Tools ▸ Archive search)
gets the new syntax, but its worker does not yet receive the display zone
or a data end. There, clock times are local and `last:` counts from now.
DST edges were not tested (India has none); local time-of-day relies on
zone transitions falling on quarter hours.

---

<a id="session-99"></a>
## Session 99 — Track diagram by absolute location (queued feature 3 of 3)

Tools ▸ Monitor ▸ **Trac&k diagram…** (Ctrl+Alt+K). The last of the three
queued features. One tab, one line (no map, no internet): RFID tags,
signals and the movement authority end against absolute track location,
the loco riding the rail at a draggable time cursor, events pinned where
they happened.

### Scope, confirmed before building

Three calls: a **new standalone window** for one tab (live, loaded, or
recorded — not wired into `ReplayWindow`'s existing `TimelineWidget`, which
already draws rails/RFID/a loco box but is replay-only and has no
signal/MA markers); signals and the MA end **deduped to one marker each**
(the same physical signal is re-reported, at a slightly different computed
location, on every frame while it's ahead); and a **draggable time
scrubber** (a `QSlider` + Play/Pause, not a static picture) — the literal
"loco moving along it over time."

### Signals and MA: both had to be derived, not read

Neither has an absolute-location field anywhere in the schema: `@dmi`
gives `appr_sig_dist` (distance *ahead of the loco right now*) and
`ma_w_r_t_sig` (distance ahead of *that signal* — `dmiAssumptions()`,
dmipanel.cpp). So `signal = loco's location that frame + appr_sig_dist`,
`MA end = signal + ma_w_r_t_sig`, both via `SpeedDistance::targetLocation()`
— the same helper that already places a DMI target. Decoded through
`dmiStateFromCapture()` (dmipanel.h), the live DMI panel's own decode —
not a second copy of its field lookups or the signal-name regex. Deduped
by name, keeping the reading with the **smallest** `appr_sig_dist` (the
closest, and so most accurate, fix); its MA is taken from that *same*
frame, so the two numbers are never stitched from different instants —
tested with two readings of one real signal 1250 m and 1800 m out, with
different MA values, to prove the closer one wins and its own MA comes
along with it, not the farther reading's.

One loco's own `abs_loco_loc` needed none of session 98's anchoring
concern: that was about reconciling two *different* locos' origins; one
loco's location is self-consistent across its own session.

### How it is built

- **`trackdiagram.h/.cpp`** (no UI): `build()` reuses
  `SpeedDistance::extract()` unchanged for the loco's own trace; walks
  `@rfid` frames for tags with a real fix (`CaptureDecoder::decodeRfid`,
  the same "0 and 0x7FFFFF both mean no fix" rule `ReplayWindow`'s track
  axis uses); walks `@dmi` for signals/MA as above; and pins
  `RunReport::summarise()`'s mode changes/overspeed/emergencies and
  `TwoLocoView::extractEvents()`'s SoS/head-on/rear-end (single-tab use —
  pairing is Two-loco view's job, not this module's) by matching each
  event's own row (or, for faults, which carry none, its time) against the
  trace for "the loco's last known location at or before this" — the same
  rule DMI time travel uses for a frame, applied to a location instead.
  Reject-rule findings are **not** pinned: `RunReport::Summary` keeps only
  per-clause counts, not each match's row, so there is nothing to look a
  location up against without changing what `RunReport` outputs — left for
  later rather than guessed at.
- **`trackdiagramwindow.h/.cpp`**: `TrackDiagramCanvas` (one rail, RFID
  diamonds, signal posts coloured by aspect via `dmiLitLamps()` +
  `UiColor::signalLamp()`, a dashed line to the MA flag, event ticks, the
  loco box at the cursor — everything at or before the cursor drawn at
  full strength, everything still ahead faded, so a static repaint still
  shows "moving along it over time" without literally animating) and
  `TrackDiagramWindow` (`QSlider` + Play/Pause `QTimer` at a fixed step,
  not real-time-scaled — noted as a simplification, not claimed as exact).
- Menu audit triggers it alongside Speed vs distance / Run summary report
  — no modal picker, so safe to fire in the offscreen run.

### Tests

- **`session99` (21 checks):** built from **real captures wherever one
  exists** (CLAUDE.md's rule) — a real `@rfid` frame (id 560, location
  152720 m, CRC-30 verified) and a real `@dmi` frame naming a real signal
  ("DN MAIN Dist", 1250 m out, MA 3251 m) from `replay/`, only the
  surrounding climb and the dedup counter-example synthesised. Covers the
  trace/direction, the tag, the dedup (closer wins, MA from the same
  frame), event pinning by location, the span widening to cover tags/
  signals/MA, the canvas's cursor clamping and signal, the window's slider
  range and Play/Pause wiring, image export, and an empty tab.

**`verify.sh`: 0 stages failed.**
- unit suite **155 suites / 4958 checks**;
- menu audit 140 ok;
- smoke alive.

**Not tested:** the Windows/MSVC build and Qt 6.4 (macOS/Qt 5.15.19 again,
the only Qt in this environment); the Play/Pause timer's actual frame
advance over real wall-clock time (only that toggling it starts/stops the
`QTimer`, not a multi-second real run of it); a second loco's track
diagram side by side with this one (explicitly out of scope — that
question belongs to Two-loco view, session 98, not this feature).

All three queued features from CLAUDE.md's list are now built: Incident
report pack (97), Two-loco view (98), Track diagram by absolute location
(99).

---

<a id="session-98"></a>
## Session 98 — Two-loco view (queued feature 2 of 3)

Tools ▸ Monitor ▸ **Two-loco view…** (Ctrl+Alt+T). Pick two tabs: both locos'
location and speed over time, the gap between them, and SoS/collision/
head-on/rear-end events, all on one shared time axis (two locos are rarely
at the same track location at the same moment, so time — not location, as
Speed vs distance uses — is the only axis both sides agree on).

### Scope, confirmed before building

Three calls, confirmed up front: the gap is computed by taking both locos'
`abs_loco_loc` at face value (no RFID-anchoring to a shared origin, unlike
`replaywindow.cpp`'s track-diagram view) — implausible pairings are
**flagged, not corrected**, when the two locos' location ranges never
overlap at all; SoS/collision/head-on/rear-end are drawn as **markers on
the plot**, not a separate table; and this is a **live window only**
(zoom/pan not included in this first cut — the whole span is drawn at
once), no HTML export like the incident pack.

### A real decoding gap, found while building this

`LOCO_SOS` (`@lsos`, schema) already carries everything this feature asked
for — SoS flags, head-on/rear-end flags, `collision_distance` — in one
packet. But `CaptureDecoder` had no `CapType` entry for it: `describe()`
switches on `CapType`, and with no case for "lsos" every `@lsos_` frame
decoded to header rows only (type/seq/CRC), never its payload. Silent:
nothing crashed, the fields were just never there — caught by a test that
asserted on the decoded episode and got an empty one instead of a wrong
one. Fixed the same way `@uba`/`@speed`/`@dip1`/... already work: a
`CapType::Lsos`, the `"lsos"` token mapping, and one `schemaRows(b, "lsos",
...)` case in `describe()`. No new per-field decode — the schema's generic
flat-LE-struct path handles it exactly like its siblings; this is a capture
tag `CaptureDecoder` didn't know yet, not a new wire format.

### How it is built

- **`twolocoview.h/.cpp`** (no UI): `extractEvents()` reads `@lsos` per tab
  — SoS (`is_access_sos_recvd`/`is_unusual_stop_recvd`/`is_train_parted_recvd`,
  tracked together) and head-on/rear-end (their own episodes, `worst` =
  `collision_distance`) — via `RunReport::EpisodeTracker` (now its third use,
  after overspeed/emergency-status in `RunReport` and EB/FSB in
  `IncidentReport`). `build()` reuses `SpeedDistance::extract()` per tab
  unchanged, pairs the two traces' samples by nearest-at-or-before within a
  tolerance (default 2 s), and flags implausibility by checking the two
  locos' location ranges for any overlap at all.
- **`twolocowindow.h/.cpp`**: `TwoLocoCanvas` (two lanes sharing one time
  axis — location with the gap shaded between the two curves, speed below;
  event ticks across both; `UiColor::series/warning/error`, no literal
  colours) and `TwoLocoWindow` (two `QComboBox` source pickers against
  `MessageDispatcher`, mirroring `CompareWindow`'s pattern; Save image/Save
  gap CSV).
- Menu audit checks the action's presence and shortcut only — it opens
  straight into the window (no modal dialog, unlike Incident report), so it
  is safe to `trigger()` there like Speed vs distance and Run summary report.

### A flaky test, and why it was one

`QTest::mouseMove()` on the canvas passed alone but failed inside the full
`dltests` run — not noise: `QTest::mouseMove` warps the *global* cursor and
relies on window stacking to route the event, which is unreliable under
`QT_QPA_PLATFORM=offscreen` once other suites' windows are still alive in
the same process. Replaced with a `QMouseEvent` sent straight to the canvas
via `QApplication::sendEvent()` — same code path (`mouseMoveEvent()`,
`setCursor()`, `cursorChanged`), no dependency on compositing. Stable
across repeated full-suite runs afterward.

### Tests

- **`session98` (23 checks):** `extractEvents()`'s three episode kinds and
  their window; `build()`'s paired traces, gap values, CSV, the tolerance
  excluding a stale match, and the non-overlap flag naming both tabs; the
  canvas holding its pair and reporting a cursor instant; the window
  building from a `MessageDispatcher`'s own models and saving its image and
  gap CSV.
- No new validator: `@lsos` has no golden replay fixture yet (session 97's
  `schema/validate_*.py` corpus doesn't include one), so the `CapType::Lsos`
  fix is covered by this session's synthetic frames only, not a golden one.

**`verify.sh`: 0 stages failed.**
- unit suite **154 suites / 4937 checks**;
- menu audit 139 ok;
- smoke alive.

**Not tested:** the Windows/MSVC build and Qt 6.4 (macOS/Qt 5.15.19 again,
the only Qt in this environment); a real `@lsos` frame from a live capture
or `replay/` (none exists yet — this session's LOCO_SOS fixtures are
synthetic, built the same way session 81/97's DMI fixtures are, from a
schema-correct zero-filled frame with fields rewritten at the decoder's own
bit offsets); the RFID-anchoring question this view deliberately defers
(flagged in CLAUDE.md's queued-feature notes above as the open problem for
a more exact gap).

---

<a id="session-97"></a>
## Session 97 — Incident report pack (queued feature 1 of 3)

Tools ▸ Monitor ▸ **Incident report…** (Ctrl+Alt+I). The first of the three
queued features: pick a moment on one tab, get one self-contained HTML file
about it — the DMI as the loco pilot saw it at the key moments around that
moment, the speed/permitted/target plot, mode changes, EB/FSB applications,
SLRP reject-rule matches, and the raw frames in the window. Same rule as the
existing Run summary report: **OBSERVED only, no verdict.**

### Scope, confirmed before building

Three decisions were confirmed up front rather than assumed: the picker is a
menu action with a time + before/after dialog (not a row right-click or a
row-range selection); the report covers one tab/one loco, like Run summary
report (not every loco at once); output is HTML with images embedded inline
(`RunReportWindow`'s existing pattern — opens in a browser, prints to PDF
from there), not a native PDF writer.

### How it is built — mostly reuse, little new decoding

- **`RunReport::Options` gained a window** (`hasWindow`, `fromMs`, `toMs`).
  `summarise()` now skips rows outside it and passes the window to
  `SpeedDistance::extract()` too, so overspeed/max-speed are windowed as well.
  Unset (the default), nothing changes — Run summary report is untouched.
- **`SpeedDistance::extract()` gained optional `fromMs`/`toMs`**, threaded
  straight into `collectRowFields()`, which already supported a window for
  its other callers.
- **`RunReport::EpisodeTracker`** (open/extend/close a spell from a boolean
  condition — already how overspeed and emergency-status are tracked) moved
  from `runreport.cpp`'s anonymous namespace into `runreport.h` so
  `IncidentReport` can track EB/FSB applications the same way, instead of a
  second copy of the same ten lines.
- **New `incidentreport.h/.cpp`**: assembles a windowed `RunReport::Summary`,
  an EB/FSB episode list (`@dmi brake_type`, schema values 3/4 —
  `FULL_SERVICE_BRAKE`/`EMERGENCY_BRAKE`, a field that already existed), a
  windowed `SpeedDistance::Trace` rendered to PNG via a headless
  `SpeedDistanceCanvas`, and the DMI panel at each key moment rendered via a
  headless `DmiView` fed by `dmiMomentFromModels()` (dmitimetravel.h) — the
  exact "latest @dmi at or before" rule the live DMI window's Follow-cursor
  uses. Key moments: window start, window end, every mode change and every
  EB/FSB onset inside the window, capped at 8 (`maxDmiMoments`) in time
  order — the excess is dropped and the report says so, because rendering a
  DMI panel is not free. Raw frames are capped at 500 (`maxRawFrames`) the
  same way, with a "N more" note.
- **New `incidentreportdialog.h/.cpp`**: a small modal picker (moment +
  before/after, seconds), mirroring `GotoTimestampDialog`'s shape.
- **New `incidentreportwindow.h/.cpp`**: shows the HTML, Rebuild/Copy/Save
  HTML, mirroring `RunReportWindow`.
- **No modal trigger in the menu audit.** Unlike Run summary report and
  Speed vs distance, this action opens a dialog first; the audit checks it
  exists and carries Ctrl+Alt+I, and does not `trigger()` it — doing so would
  block the offscreen run on a dialog nothing can answer.

### Tests

- **`session97` (39 checks):** `RunReport`'s window (included rows, the
  mode-at-window-start, only the in-window mode change, windowed overspeed
  and max speed, unwindowed behaviour unchanged); `SpeedDistance::extract`'s
  window; `IncidentReport::build()` end to end (window bounds, the FSB
  episode's span and name, the plot rendering, raw-frame capping and its "N
  more" count, the four key moments and their DMI panels, an empty window, a
  tab with no `@dmi` at all — `SpeedDistance`'s own LSRP fallback still
  plots, but no DMI panel exists to show); the dialog's clamping and
  before/after defaults; the window building and saving its HTML on
  construction.
- `test_contrastaudit.cpp`: `incidentreport.cpp` added to the standalone-HTML
  colour-literal allowlist, next to `runreport.cpp` — same reason (a document
  with its own white background, no palette to follow).

**`verify.sh`: 0 stages failed.**
- unit suite **153 suites / 4914 checks**;
- menu audit 139 ok;
- smoke alive.

**Not tested:** the Windows/MSVC build (still unbuilt there since the
library split, patch 88) and Qt 6.4; this session's gate ran on macOS/Qt
5.15.19 (Homebrew), the only Qt available in this environment. Rendering of
the DMI panel and the speed plot was only exercised offscreen
(`QT_QPA_PLATFORM=offscreen`) via `QWidget::grab()`, not against a real
on-screen window.

---

<a id="session-96"></a>
## Session 96 — Singletons, 2 and 3 of 4: `TabTags`, `SessionKeyStore`

Same pattern as `FrameNumberWatch`: a public constructor, **no
`instance()`**, one owned by `MainWindow`, and handed to whatever needs it.

### `TabTags` (tab colour and label; 24 uses)

- `MainWindow::m_tabTags`, shared with its **pop-outs**
  (`TabPopoutWindow(…, tags)`) and **loco consoles**
  (`LocoConsoleWindow(…, tags, …)`), so a tag set anywhere redraws
  everywhere, as before.
- A window given none makes its own. It reads the same saved tags (they
  live in the ini), but hears only its own changes.
- `MainWindow::tabTags()` is there for the menu audit.

### `SessionKeyStore` (captured session keys; 19 uses, including the decoder)

- **`MainWindow::m_sessionKeys`** is fed by live traffic as before.
- **`CaptureDecoder::describe(…, keys)`** takes the store for the SLRP
  "MAC (live)" row. **No store, no MAC row.**
- **Handed to:**
  - the main window's field panel;
  - the Decode Workbench, Packet Maker, field sweep, Session Key dialog and
    loco console (constructor);
  - Compare windows (`setSessionKeys()`: field panel and workbench);
  - recorded-session windows (`setSessionKeys()`: their workbench and
    Packet Maker).
- **Two deliberate differences:**
  - **Replay windows and a recorded session's own field panel no longer
    show "MAC (live)".** It checked a recorded frame against today's live
    keys, which says nothing true about that frame. The Decode Workbench,
    opened from a recording, still lets the operator pick a captured key
    set.
  - **A dialog opened with no store** (in tests) gets a private, empty one,
    so its key picker is empty rather than borrowing global state.

### Tests

- **`session96` (6):**
  - two tag hubs signal only their own listeners, but share the saved tag;
  - `describe()` with no store has no MAC row; an empty store says "no
    session key yet";
  - two stores share nothing.
- **The four key suites** (`sessionkeystore`, `keysnapshots`,
  `sessionkeyloco`, `keypickers`) and **`tabtags`** each make their own
  instead of sharing the global.
- **The menu audit** tags through `MainWindow::tabTags()`.

**`verify.sh`: 0 stages failed.**
- unit suite **152 suites / 4898 checks**;
- menu audit 138 ok;
- smoke alive.

---

<a id="session-95"></a>
## Session 95 — Singletons, 1 of 4: `FrameNumberWatch`

The first of the four singletons that came back (cleanup step 5). It holds
the live loco frame number that the Packet Maker seeds frames from, so it
is mutable runtime state. As a process-wide instance it was shared by every
window and every test, which is exactly what the dispatcher's
"kill the singletons" goal was meant to end.

**Now:**
- `FrameNumberWatch` has a public constructor and **no `instance()`**.
- **`MainWindow` owns** the one fed by live traffic (`m_frameWatch`).
- **`PacketMakerDialog(parent, frameWatch)`** is handed it.
  - The Tools-menu and buffer Packet Makers get MainWindow's.
  - A recorded-session window's Packet Maker gets it through
    `SessionWindow::setFrameWatch()`, as before when it read the global.
  - `nullptr` means no live traffic: the seed then comes from the clock
    and says so.
- **`PacketMakerDialog::seedFrameNumber(watch, bits, &fromLive)`** takes the
  watch explicitly.
- **No behaviour change** for the operator.

**Tests:**
- **`session95` (6):** two watches share nothing, and the seed comes from the
  watch handed in, or the clock without one.
- **`framenumwatch` and `packetvary`** now make their own watch instead of
  clearing the global one between checks.

**`verify.sh`: 0 stages failed.**
- unit suite **151 suites / 4892 checks**;
- menu audit 138 ok;
- smoke alive.

---

<a id="session-94"></a>
## Session 94 — Export / import profiles, to use them on another PC

Both windows already had named, saved profiles. They can now be carried to
another PC as a file.

### Loco Configuration: ☰ ▸ Export configurations… / Import configurations…

- **Export:**
  - tick which configurations to include (the one on screen is pre-ticked;
    All / None);
  - saved as a `.dlloco` file: every LOCO_INFO value and all four send
    targets (IP, port, enabled);
  - **not** what was last sent: that is this PC's send history, not the
    loco's.
- **Import:**
  - any `.dlloco`, or a plain copy of another PC's `loco_configs.json`;
  - values the file lacks are filled from the defaults, and fields this
    schema does not have are dropped and named, exactly as loading
    `loco_configs.json` does.

### Firmware Flasher: ☰ (next to ⚙) ▸ Export profiles… / Import profiles…

- **Export:** the chosen profiles as a `.dlflash` file: VCC IP and port,
  the default image per card, pin-by-SHA, transfer tuning and updater wait.
- **Import:** a `.dlflash`, or a copy of `flasher_profiles.json`.
- **Default images are paths on the exporting PC.** After an import, any
  that do not exist here are listed ("Bench 3 — Input: D:\…\input.appimage
  is not on this PC"), so they can be re-pointed with ⚙.
- The menu is disabled while flashing.

### A name already in use

For each clashing name, one question:
- **Replace:** the imported one overwrites the existing one, in its place;
- **Keep both:** the imported one is added as "Name (2)", "Name (3)", …;
- **Skip:** the existing one is kept.

"Do the same for the rest" answers once for the whole file. Cancel stops
the import.

### Refusals, not half-reads

A file is refused, with the reason, when it is:
- the other window's kind of file (named by its `format`);
- broken JSON;
- written by a newer DLConsole (`version` above 1);
- empty.

### Files

- **New:**
  - `profileio.h`: the clash rule and the "Name (n)" naming, shared;
  - `profileiodialogs.{h,cpp}`: the pick-which dialog and the clash question.
- **Loco Configuration:**
  - `lococonfigcore`: `exportConfigs`, `importConfigs`,
    `ConfigStore::importConfig`, `all`;
  - one config's JSON parse is factored out, so loading and importing read
    it identically.
- **Flasher:** `flashercore` gains `exportProfiles`, `importProfiles`,
  `importedImageProblems`, `ProfileStore::importProfile` and `all`.
- **Both windows:** the menu items, plus `exportConfigsTo` /
  `importConfigsFrom` and `exportProfilesTo` / `importProfilesFrom`, which
  do the work and are public for the tests.

### Tests

**`session94` (30):**
- the naming rule;
- the file format: named, versioned, no send history;
- every value and target read back;
- the refusals: wrong kind, broken JSON, newer version; a bare
  `loco_configs.json` is accepted;
- **two windows as two PCs, for configurations and for flasher profiles:**
  export on one, import on the other, and each clash answer (replace,
  keep both, skip, cancel);
- PC B's own file on disk has them afterwards;
- a missing default image is reported against its profile.

**`verify.sh`: 0 stages failed.**
- validators 11/11;
- unit suite **150 suites / 4886 checks**;
- menu audit 138 ok;
- smoke alive.

---

<a id="session-93"></a>
## Session 93 — The intermittent `lococonsolelive` failure: a real clock race

`lococonsolelive` sometimes failed two checks: "a field that changed is
highlighted" and "and in bold while fresh". It was not the test.

**The race:** the loco console's refresh tick reads the clock once. It then
rebuilds the tables, and the rebuild stamped each changed field with a
**second** reading of the clock. On a busy PC that second reading was a
millisecond or more later, so the change looked like it came from the
future (age −1 ms). The highlight, which is judged against the tick's time,
refused it, and the field went unmarked until the next tick, about 250 ms
later.

**Fix:** `refreshTypeTables()` takes the tick's `nowMs`, and changes are
stamped with it.

**Proof:** the suite was run 30 times with two CPU-burning processes
alongside.
- before: **8 of 30** runs failed;
- after: **0 of 30**.

**`verify.sh`: 0 stages failed.**
- unit suite 149 suites / 4856 checks;
- menu audit 138 ok;
- smoke alive.

---

<a id="session-92"></a>
## Session 92 — DMI always drawn; Fields panel; 0–250 dial; the packet maker writes lsb-first; own-ID ARP flagged

### DMI (LP-OCIP) window

- **Signal (D):** the distance is now printed with no aspect too
  ("0000 m"). Patch 91 already drew the post and lamps.
- **Target column (A):** the scale lines, the "Target Distance" label and
  "0000 m" are always drawn. Only the bar needs a distance, and only the
  type needs a type. The column used to vanish when both were 0.
- **Fields ▸** (new, beside Save image) opens a panel to the right of the
  panel with **every decoded field of the `@dmi` frame being drawn**. It
  follows the live frame, or the time-travel moment, and costs nothing
  while closed.
- **"Field sources…" is gone.** It only printed, in the status line, which
  `@dmi` field feeds each region. Those notes now sit under the Fields
  table, headed "Where each region comes from".
- Tag IDs and statuses were already shown by patch 91, from real frames.

### Cab view

- **The speed dial is fixed at 0–250 km/h**, the loco's top speed
  (`kCabDialMaxKmh`), with a minor tick every 10 and a label every 50.
- It used to start at 120 and grow with the speed, so the same needle
  angle meant different speeds.

### Packet maker: lsb-first packets build

**Now buildable:** DMI, RFID, CCSYS, DLSYS, SPEED, DIP1/2 and LOCO_SOS.

**Encoder changes (`schema/schemaencoder.cpp`):**
- **Bit order:** the writer and reader do lsb-first, the exact inverse of
  the decoder.
- **`<crc>` elements** are written as zeros, then computed over the finished
  frame and placed in schema order. `at=` is honoured, so RFID's 30-bit CRC
  inside its last data byte works.
  - The algorithms (`jamcrc`, `rfidcrc30`) are registered for the encoder
    when the program loads, the same functions the decoder uses.
- **`<pad fill="…">`** writes a fixed pattern, and `fill="frame_len"` writes
  the finished frame's size.
  - `kavach.xml`'s DMI now spells out its AA AA sync, the length byte (116)
    and the BB BB end sync this way.
  - The decoder, which just skips a pad, is unaffected.

**Three writer bugs these exposed**, now fixed:
- **A pad inside a `<when>` was always written.** It now obeys the `when`,
  as the decoder does. RFID Adjacent-Line tags (type 11) were shifted by it.
- **`<flags pad="N">` did not skip its N pad bits.**
- **A flags block wider than 63 bits lost bits 64 and up.** The DMI's
  71-bit `alarm_code` is now taken in 32-bit pieces (`alarm_code`,
  `alarm_code#2`, `alarm_code#3`).

**Still refused, with the reason named:**
- ANALOG, LINFO and UBA (float, double and text fields);
- DOP1/2 (`<repeat>`).

CRC fields are not shown as inputs in the form.

**Checked against `replay/`:** every real frame is rebuilt from its own
decoded fields, and every rebuilt frame's CRC passes.

| Type | Rebuilt | Notes |
|---|---|---|
| `@dmi` | 10,658 / 10,658 byte for byte | including the CRC |
| `@rfid` | 173 / 173 | 171 byte for byte; the 2 recorded with a zero CRC come out with a correct one |
| `@ccsys` | 2,092 / 2,092 | the recording's CRCs fail in the decoder too; everything else identical |
| `@dlsys` | 1,050 / 1,050 | as for `@ccsys` |

### Reject rules: an ARP received from our own loco ID

- **New rule:** an `arprecv` whose `SOURCE_LOCO_ID` equals this loco's own
  ID is reported as not processed.
  - It is this loco's own transmission coming back, or a second loco
    configured with the same ID.
  - Clause "DLConsole": no FRS clause is cited yet.
- **Two engine additions** (`rejectrules.{h,cpp}`):
  - an `eq_field` op, which fires when two fields agree;
  - a rule-level `captype=` restriction. The ARP the loco SENDS carries its
    own ID by design and is not judged by it.
- Like 32.14.1, it is quiet until the loco's ID has been learned.
- The field inspector and the run report pass the frame's type.

### Tests

- **`session92` (32):**
  - the dial's range;
  - the target column and signal distance drawn with nothing to show;
  - the Fields panel (opens, lists `target_distance`, closes, the old
    button gone);
  - the four lsb-first types rebuilt frame for frame, with passing CRCs;
  - LINFO still refused;
  - a real `@dmi` built through the packet maker's builder byte for byte,
    and an edited one (speed 87) with AA AA 74 … BB BB and a CRC that
    passes;
  - the own-ID ARP rule firing only on `arprecv`, and only once the loco is
    known.
- **Updated:**
  - `session89`: the distance with no aspect is now expected.
  - `roundtrip`: `@dmi` is now expected to be reproduced, not refused. Its
    fixture is a whole real frame; the old one was truncated.
- **Caught on the way:** a `--` in the new rule's XML comment made
  `rejectrules.xml` fail to load, which would have silently dropped every
  reject rule. The new tests failed on it before it shipped.

**`verify.sh`: 0 stages failed.**
- validators 11/11;
- unit suite **149 suites / 4856 checks**;
- menu audit 138 ok;
- smoke alive.

---

<a id="session-91"></a>
## Session 91 — DMI tag IDs, the signal post with no aspect, four more themes, and a decoder fix

This work was started as "session 89"; the code comments still say so.

### DMI (LP-OCIP) window

- **Tag diagram (M):** each of the last three tags now shows its **ID**
  beside its sleeper, in the colour of its status.
  - Each also has a mark that does not rely on colour: `✓` read (1), `✗`
    missed (2), nothing for none (0), and `?` for status 3, which the spec
    does not name.
  - An empty slot (ID 0, status 0) shows `—`.
  - The track is drawn narrower to make room.
- **Signal (D):** the post, lamps, stem and disc are always drawn. With
  aspect 0 ("Unidentified") every lamp is unlit. The route indicator,
  stencil and distance still wait for a known aspect.
- **Field sources…** notes the tag IDs (`rc`/`rl`/`rll`) and statuses
  (`rcs`/`rls`/`rlls`).

### Decoder fix (`schema/schemadecoder.cpp`)

`Decoder::walkEntry` took its context **by value**. So a `<group>`'s
sub-field values never reached the packet's scope, and every grouped field
was missing from the numeric values and from later conditions. That
included the DMI's RFID IDs and statuses, which is why the tag diagram
could not show them.

It now takes the context by reference, as the Python engine's `walk_entry`
does. The golden validators and the whole suite are unchanged by it.

### Themes: Ocean, Lavender, Rose, Amber

- **Sage's recipe** (tinted greys, no pure white, one accent) in blue,
  violet, rose and warm grey. They are listed with the light themes.
- **Each has its own status colours** (`uicolors.cpp`), as Sepia and Sage
  do. As first written they fell through to Ayu Light's set, which is tuned
  for `#FCFCFC`. On their tinted surfaces, Lavender's muted grey on an
  alternating row was 4.46:1, and the flasher's badge text on its tint was
  4.44–4.47:1 in Ocean, Lavender and Rose.
  - Sage's ok / warning / error clear 5:1 on all four as they are.
  - Muted is each theme's own tinted grey, and accent its own link colour.
  - Each was darkened until the worst surface (window, base, alternating
    row, button) and the badge tint are at least 4.7:1.

### Tests

- **`session89` (31):** the tag IDs and marks, the empty slot, the post
  with no aspect, the four themes, and the decoder carrying grouped values.
  It uses real `@dmi` frames.
- The contrast audit and the flasher's badge check now cover the four new
  themes as well.

**`verify.sh`: 0 stages failed.**
- validators 11/11;
- unit suite **148 suites / 4824 checks**;
- menu audit 138 ok;
- smoke alive.

---

<a id="session-90"></a>
## Session 90 — UDP drops counted by cause

The one drop counter mixed two different faults. It is now two counters:

- **Malformed:** the datagram was bad on arrival. It was empty, oversize
  (over 8000 bytes), shorter than a header, or its `message_len` overran
  the datagram. **A network or sender problem**: this PC could not have
  kept it.
- **Queue full:** a good message was dropped because the GUI thread was
  more than the queue limit (50,000) behind. **A PC problem**: this machine
  could not keep up.

Datagrams addressed to another console are still ignored, and are not drops.

**What the operator sees:**
- **Status bar:** `Drops: 0`, or e.g. `Drops: 3 malformed · 120 queue full`,
  leaving out a cause with none. Its tooltip says what each cause means and
  where to look.
- **Log banner:** one row per cause that grew since the last tick, e.g.
  `<<< 2 malformed datagram(s) discarded — network or sender problem >>>`
  and `<<< 5 message(s) dropped: this PC fell behind (queue full) >>>`.
  There used to be a single "queue full or invalid" row.

**API:**
- `UDPCommunication::malformedCount()` and `queueFullCount()`; the
  `MessageDispatcher` has the same pair.
- `droppedCount()` stays, as their sum.
- The helpers `dropStatusText()`, `dropStatusTooltip()` and
  `dropBannerLines()` are in `udpcommunication.h`.

**Tests — `session90` (16):**
- the status text, tooltip and banner rows;
- **through a real socket on loopback:**
  - a 3-byte datagram, a 9000-byte one and one whose `message_len` overruns
    count as 3 malformed;
  - a misaddressed one is not a drop, and a good one is received;
  - a receiver with a queue limit of 2 and nothing consuming takes 2 and
    counts 3 queue full, then takes messages again once the GUI catches up.

**`verify.sh`: 0 stages failed.**
- validators 11/11;
- unit suite **147 suites / 4469 checks**;
- menu audit 138 ok;
- smoke alive.

---

<a id="session-89"></a>
## Session 89 — `mainwindow.cpp` split

A pure move, with no behaviour change. `mainwindow.cpp` goes from 4597 lines
to 2677; the rest moved to:

| File | What |
|---|---|
| `mainwindow_menus.cpp` | `buildMenus()`: the menu bar, which was 500 lines inside the constructor. It returns the few menus the constructor still needs (`MenuRoots`). |
| `mainwindow_status.cpp` | `buildStatusBar()`, the frame clock, bind state, the status tick, notifications, the drop banner and decode failures |
| `mainwindow_tabs.cpp` | creating, showing and hiding, closing, popping out, tagging and clearing tabs |
| `mainwindow_tools.cpp` | the Tools-menu windows and the hand-offs between them (workbench, packet maker, frame diff, flasher, reports) |

**Checked:**
- menu audit 138 ok (it builds the real window and walks every menu);
- smoke alive;
- unit suite 146 suites / 4453 checks, run after delivery.

---

<a id="session-88"></a>
## Session 88 — Shared sources compiled once: the `dlcore` static library

The app, the unit tests and the menu audit each compiled the same ~106
application sources. They are now compiled once, into a static library, and
every program links it.

### Measured

The same machine, one core (`make -j1`), clean builds:

| Program | Before (patch 87) | After |
|---|---|---|
| Shared library `dlcore` | — | 375 s |
| Unit tests (`dltests`) | 495 s | 178 s |
| Menu audit | 379 s | 5 s |
| App (`DLConsole`) | 374 s | 2 s |
| **Total** | **1248 s** | **560 s (2.2× faster)** |

Not 3×: the unit suite's own ~100 test files still compile either way.
With `make -jN` both columns shrink by about N.

### Layout

- **`dlcore.pri`** is THE list of shared sources: 106 `.cpp`, 111 headers,
  `mainwindow.ui`, plus `flasher.pri`, `lococonfig.pri` and `serial.pri`.
  **A new source file is added here, and only here.**
- **`core/core.pro`** builds `libdlcore.a` (`dlcore.lib` with MSVC) into
  `<build>/core`, with no debug/release subfolder.
- **`dlcore_link.pri`** is included by every program. It gives the headers,
  the Qt modules, the serial switch (the same test the library was built
  with), the resources, the link, and a relink when the library changes.
  - Resources (`images.qrc`, `lococonfig.qrc`) stay in the programs. A `.qrc`
    inside a static library registers itself from an object the linker never
    pulls in.
  - The library is looked for in `core/` next to the program's build folder.
    Elsewhere, pass `qmake DLCORE_DIR=…`.
- **`app/app.pro`** is `main.cpp` plus the library.
- **`DLConsole.pro`** is now a subdirs project: `core`, then `app`.
  - Open it in Qt Creator, or run `qmake && make`, as before.
  - **The program lands in the main build folder, `<build>/DLConsole(.exe)`**,
    where it always did: `app/app.pro` sets `DESTDIR` to it. (Patch 88 as
    first delivered put it in `<build>/app/`; 88.1 moved it back.) Override
    with `qmake DLCONSOLE_BIN_DIR=…`.
  - `qmake CONFIG+=with_tests` also builds `dltests` and `menuaudit`
    (in `<build>/tests`, as `Makefile.dltests` / `Makefile.menuaudit`).
- **`tests/tests.pro`, `menuaudit.pro`, `shot.pro`, `bench.pro`** list only
  their own files and link the library. Each keeps its objects in its own
  `.obj-<target>` folder, so two can share a build folder.
  - **`shot.pro` builds again.** Its private copy of the source list had
    fallen 66 files behind the app's, so it had not linked for a while.
- **`verify.sh`** builds `build-verify/core` first, then links the tests,
  the menu audit and the app (`app/app.pro`) against it. If the library
  fails to build, it says so as its own stage.

### One behaviour difference

The unit tests used to compile the shared sources without
`QT_NO_DEBUG_OUTPUT`; they now get the library, built like the app with
it. No test reads debug output; the counts below are unchanged.

### Checked

- **`verify.sh`, end to end on the new layout: 0 stages failed.**
  - validators 11/11;
  - unit suite **146 suites / 4453 checks**, the same as patch 87;
  - menu audit **138 ok**;
  - headless smoke alive (500 datagrams).
- **Built without the serial module** (`CONFIG+=no_serial`, library and menu
  audit):
  - the library has no serial objects;
  - `menuaudit` does not link `Qt5SerialPort`;
  - the audit is 137 ok, with the serial action present and disabled.
- **The top-level `DLConsole.pro` with `CONFIG+=with_tests`** builds
  `core → app, dltests, menuaudit`.
- **`shot` and `bench`** link.

---

<a id="session-87"></a>
## Session 87 — Git history; one changelog

- **The tree is a git repository.**
  - The first commit is patch 86 exactly as delivered (tag `patch-86`), so
    everything removed afterwards can be recovered.
  - Patches travel as the zip, which includes `.git`, or as a `.bundle`.
- **This file replaces the 64 `CHANGES_SESSION_*.md` notes** and
  `LINFO_CHANGES.md`, which is kept as an undated appendix.
- **Removed `schema/*_v0_backup.*`**: nothing built, imported or tested them.
- **Removed the root `SCHEMA_GUIDE.md`**: it described `kavachschema.h`,
  which is gone. The guide is `schema/SCHEMA_GUIDE.md`.
- **`.gitignore`** gains `__pycache__/`, `*.pyc` and `build-verify/`.

---

<a id="session-86"></a>
## Session 86 — The console never depends on a serial port

Only the IOA logs come over serial. The VCC logs come over Ethernet, and the
console must start and run on them alone.

### 1. At run time: nothing serial until asked

This was already true of session 85, and is now locked by the menu audit:

- **At startup:** DLConsole creates no serial port object, link or terminal.
  - Ethernet reception starts exactly as before.
  - No serial port needs to exist, be free, or be plugged in.
- **Opening Tools ▸ Serial Port Terminal** only makes the window.
  - The port stays closed until **Open** is pressed.
  - Settings are remembered, but a port is never re-opened automatically.
- **A port that fails** (refused setting, cable pulled, adapter gone) closes
  that one terminal and says why. UDP reception and every other window carry on.

### 2. At build time: the serial module is optional

**New `serial.pri`**, included by `DLConsole.pro`, `tests.pro`,
`menuaudit.pro` and `shot.pro`.
- **With Qt's Serial Port module** (the normal case):
  - the terminal is built in and `DL_HAVE_SERIAL` is defined;
  - `test_session85` and its `-lutil` are added.
- **Without it**, or with `qmake CONFIG+=no_serial`:
  - DLConsole builds and runs on Ethernet alone;
  - the binary does not even link `Qt5SerialPort`, so a missing
    `Qt5SerialPort.dll` can never stop the console starting;
  - Tools ▸ Serial Port Terminal is still in the menu, disabled, and its
    tooltip says why.

### 3. Terminal polish found while rendering it

- **The receive view now uses the console's own monospace font**
  (`UiStyle::monoFont()`, which follows View ▸ Text size), so hex columns
  line up. It was the system fixed font before, and did not zoom.
- **The Port box is wider** (240 px), so "COM3 — USB Serial Port" fits.
- **The console tab is named by the port's short name**, e.g. "Serial ttyUSB0"
  rather than "Serial /dev/ttyUSB0". Windows COM names are unchanged.

### 4. Design images

- `docs/images/serial_terminal_light.png`
- `docs/images/serial_terminal_dark.png`

These are the real window, rendered offscreen at 1180×720, open on a
pseudo-terminal standing in for an IOA card:
- it prints real `@dop1`/`@dop2` lines from `replay/`;
- one `DIAG?` has been sent (the `TX>` echo);
- Feed console is on (26 lines fed to the "Serial ttyUSB0" tab).

The Ayu Light and Ayu Dark themes are shown. The view font is DejaVu Sans
Mono here; on Windows it is the system fixed font.

### Files

- **New:** `serial.pri`, `docs/images/serial_terminal_{light,dark}.png`.
- `mainwindow.cpp`: the terminal action is guarded by `DL_HAVE_SERIAL`
  (disabled, with an explanation, otherwise).
- `serialconsolewindow.cpp`: the font, the port width and the short tab name.
- `DLConsole.pro`, `tests/tests.pro`, `tests/menuaudit.pro`, `tests/shot.pro`:
  the serial sources, `QT += serialport` and `test_session85` moved behind
  `serial.pri`.
- `tests/menuaudit_main.cpp` gains three checks:
  - the console starts with no serial port, terminal or link;
  - opening the terminal opens no port;
  - built without serial, the action is present and disabled.

### verify.sh

Each stage run with the script's own commands, **0 stages failed**:
- validators 11/11;
- unit suite **146 suites / 4453 checks**;
- menu audit **138 ok**;
- headless smoke alive (500 datagrams).

**Also built and run WITHOUT the serial module** (`CONFIG+=no_serial`):
- the app starts, with no `libQt5SerialPort` in `ldd`;
- it takes the 500-datagram smoke;
- its menu audit is 137 ok, 0 failed.

**Flake seen once:** `lococonsolelive` failed two timing checks ("a field that
changed is highlighted", "and in bold while fresh") in one full-suite run on
the single-core build box. It passed 3/3 alone and in the next full run. That
suite was not touched here; it is worth a look if it recurs on your machine.

---

<a id="session-85"></a>
## Session 85 — Serial port terminal (QCom-style), feeding the console

Only the VCC logs over Ethernet; the IOA's input, output and analog logs come
out of a serial port. **Tools ▸ Serial Port Terminal… (Ctrl+Alt+S)** opens
one port, shows its traffic like QCom does, and can feed its lines into the
console, where they are handled like UDP traffic.

### 1. Port and line settings

- **Port:**
  - enumerated, showing name and description (e.g. "COM3 — USB Serial Port");
  - ⟳ to look again;
  - a path can be typed if it isn't listed.
- **Baud:** the standard rates, or any rate typed in.
- **Data bits** 5–8.
- **Parity:** None / Even / Odd / Mark / Space.
- **Stop bits:** 1 / 1.5 / 2.
- **Flow control:** None / RTS/CTS / XON/XOFF.
- **DTR and RTS lines.** Under RTS/CTS the driver owns RTS, so the switch is ignored.
- **Open / Close:**
  - the line settings are locked while the port is open;
  - the title and status show what is open, e.g. "COM3 115200 8N1".
- **A refused setting is never silently swapped.** If the driver opens the
  port but refuses a setting (a parity it can't do, say), the port is closed
  again and the status says "opened, but refused 9600 8E1: …". It never runs
  on at some other setting.
- **Cable pulled / adapter gone:** the port closes and the status says why.
- **Several windows can be open at once, one per port** (input, output, analog).
- The last settings are remembered (`serial/…`).

### 2. Receive view

- **Text or Hex.** Hex shows the bytes in the chunks they arrived in.
- **Timestamps.** Each line is stamped with the time its **first** byte
  arrived, so a line that trickles in at 9600 baud belongs to when the card
  started printing it.
- **Line endings.** LF, CR+LF and CR alone all end a line, and a CR/LF pair
  split across two reads is still one line end.
- **Idle flush.** A line with no newline is delivered once the port has been
  quiet for 300 ms after its last byte (a prompt, a card that stopped
  mid-line). A line still arriving is not cut.
- **Show sent** echoes transmissions as `TX> …`.
- **Hold** freezes the view so it can be read. What arrives meanwhile is
  kept, not dropped; the status counts it ("holding 3 lines") and it appears
  in order on release. Logging and feeding carry on while held.
- **Clear.**
- **Log to file…** appends what the view shows to a file (default name
  `serial_<port>_<date>_<time>.log`).
- The view keeps the last 20,000 lines.
- **Counters:** RX bytes, TX bytes, lines fed to the console, with a Reset.

### 3. Send

- **Text** with escapes: `\r \n \t \\ \xHH`.
- **Hex** bytes: `AA 55 0d0A`, `0xAA,0x55`, … Bad hex (an odd digit count,
  a non-hex character) is **refused and nothing is sent**, rather than
  sending something other than what was typed.
- **Line end:** none / CR / LF / CR+LF.
- **History** of the last 30 sends, newest first, remembered.
- **Repeat every N ms.** It stops itself if the port closes.
- **Send file…**

### 4. Feed console

**Feed console** (on by default) puts every received line into the console
through the same pipeline as a UDP datagram, in a tab named "Serial COM3".
So IOA logs get:
- the same decoding (`@dip`, `@dop`, `@analog_top` / `@analog_bottom`, …);
- colour rules, find, query, pins, plots, the DMI time travel;
- recording and `.dlr` session files.

Each line keeps its first-byte time as its arrival time.

How the tab is formed:
- a synthesised header: source 254, destination 101, message 1, and a kvch
  id derived from the port name;
- the kvch id is stable across runs and machines, and "COM3" and "com3" are
  the same port;
- so the same port always lands in the same tab.

Fed lines are not counted as UDP receiver traffic (its queue accounting is
unchanged).

### Files

- **New:**
  - `seriallink.h/.cpp`: the port as a plain object with no window, so the
    mission driver's RFID-over-TTL output can reuse it. It contains:
    - `SerialConfig` with save/load and `summary()`;
    - `SerialLineSplitter`;
    - the hex parse/print and escape helpers.
  - `serialconsolewindow.h/.cpp`: the window.
- **`messagedispatcher.h/.cpp`:** `ingestLocal()` (lines from another route into
  the normal pipeline, with a tab name) and `drainNow()` for tests.
- **`mainwindow.cpp`:** Tools ▸ Serial Port Terminal… (Ctrl+Alt+S).
- **Project files:**
  - `QT += serialport` in `DLConsole.pro`, `tests.pro`, `menuaudit.pro` and `shot.pro`;
  - on Linux, `tests.pro` also links `-lutil` for `openpty()`.

**Windows build:** needs the **Qt Serial Port** module for Qt 5.15. It
ships with the standard Qt 5.15 installs; if qmake reports an unknown module
"serialport", add it in the Qt maintenance tool.

### Tests

**`session85` (48)**, all run in the test suite:

- **Line splitting:**
  - LF, CR+LF and CR alone;
  - a CR/LF split across reads;
  - blank lines;
  - over-long lines split;
  - the idle flush measured from the last byte, not the first.
- **Hex parse and print:** refusals for an odd digit count and for a non-hex character.
- **Escapes.**
- **Line-setting summaries.** Settings save and load unchanged, and nonsense in the ini falls back to 115200 8N1.
- **`ingestLocal`, with real `@dop` lines (the IOA's output log) from `replay/`:**
  - they decode as `@dop`;
  - they land in the "Serial COM3" tab;
  - "COM3" and "com3" give the same tab;
  - the entry's own tab key agrees.
- **End to end through a pseudo-terminal (Linux), with the pty standing in for the card:**
  - a refused parity is refused, not swapped;
  - the port opens at 9600 8N1;
  - a log line split across writes arrives whole, in the terminal and in the console tab, in order;
  - RX and TX counts are exact;
  - CR+LF text, hex bytes and refused hex behave as described;
  - echo and history, newest first;
  - a prompt with no newline appears once the line goes quiet;
  - Hold keeps the line back from the view, the console still gets it, and it appears on release;
  - the hex view;
  - close, and no sending on a closed port;
  - a missing port says why it did not open.

### verify.sh

Each stage run with the script's own commands, **0 stages failed**:
- validators 11/11;
- unit suite **146 suites / 4453 checks** (145 / 4405 before);
- menu audit **136 ok** (Tools ▸ Serial Port Terminal on Ctrl+Alt+S);
- headless smoke alive (500 datagrams).

**Not seen yet:** a real USB–serial adapter or a real IOA card, and Windows
COM ports. The pty checks the port handling, but not a driver's quirks.

---

<a id="session-84"></a>
## Session 84 — DMI time travel

The DMI window can now show the panel **as it was** at any moment of a
recording or of the live log: click a row, or move a replay cursor, and the
panel shows exactly what the loco pilot had on screen then.

### 1. DMI window — ⏱ Follow cursor

A new switch next to the loco selector (remembered as `dmi/followCursor`,
off by default).

- **The rule.** For each loco, the panel is its **latest `@dmi` at or before
  the moment** — the DMI holds a frame until the next replaces it, so that is
  what was displayed.
  - In the tab clicked, "before" is by **row order**, so clicking an `@dmi`
    row shows exactly that frame.
  - In the other tabs it is by **receipt time**.
- **Stale.** A frame more than 3 s before the moment is drawn muted, as a
  stale live frame is, and the status line says "stale, muted".
- **None.** Nothing in the 10 minutes before the moment is "no @dmi" (blank
  panel, said so), not an ancient frame.
- **Which loco.** The loco of the clicked row (or the replay's active source)
  if it has a frame; otherwise the one selected, if it has; otherwise the
  first that has. The selector lists every loco with a frame at the moment.
- **Not mistakable for live.**
  - The title reads `DMI (LP-OCIP) — at HH:MM:SS`.
  - The status line (accent colour) reads e.g.
    `⏱ At 12:58:44.312 · tab Loco 1 · 1_1's @dmi of 12:58:43.900, 0.4 s before`.
- **Live frames** keep arriving underneath and are shown again the moment
  Follow is unticked; they never replace the moment on screen.
- **Save image…** saves the panel at the moment; B5/B6 carry the frame's own
  date and time.

### 2. What drives it

| Where | Moment |
|---|---|
| Main window, any tab | the current row (click, arrow keys, ribbon click, find, jumps) |
| Recorded session (.dlr) window | the current row; only that recording's tabs are searched |
| Replay (.cap) window | the cursor: timeline drag, slider, ← →, playback, event stepping, Event Log; changing the source turns the panel to it |

**Several windows open:** whoever moved last wins.

**Replay window:** new **⏱ DMI at cursor…** button (Ctrl+Alt+D in that
window) opens a DMI window already following that replay.

### 3. Cost

- **Nobody following:** every row change or cursor move only hands the
  broker a small resolver; nothing is scanned.
- **While a DMI window follows, main-window tabs:** a walk back from the row,
  bounded by the 10-minute window, one per tab. A tab is located by binary
  search on time.
- **While a DMI window follows, replay:** `@dmi` positions are indexed per
  source at load, so a cursor move is one binary search per loco.
- **Starting to follow** resolves the last place pointed at, if that window
  is still open.
- **Live tabs that trim:** the clicked row is found again (by pointer, near
  its time). Once it has been trimmed away there is no moment, rather than a
  wrong one.

### Files

- **New:** `dmitimetravel.h/.cpp`
  - the `DmiTimeTravel` broker (`offer` / `follow` / `momentChanged`);
  - `dmiMomentFromModels()`, `logModelRowAfter()`, `logModelRowOf()`,
    `dmiTabResolver()`, `dmiKeyOfText()`.
- `dmipanel.h/.cpp`:
  - follow mode (`setFollowCursor`, `showMoment`, `render`);
  - `dmiStateFromCapture()` (`dmiStateFromLine()` now uses it);
  - `DmiView::setEmptyText()`.
- `mainwindow.h/.cpp`: `offerDmiMoment()` on each tab's current-row change.
- `sessionwindow.cpp`: the same, per recording.
- `replaywindow.h/.cpp`: `dmiMomentAt()`, `openDmi()`, the button, the per-source `@dmi` index.
- Registered in `DLConsole.pro`, `tests/tests.pro`, `tests/menuaudit.pro`,
  `tests/shot.pro` (which also gains `dmipanel.cpp`, missing since session 83)
  and `tests/bench.pro`.

### Tests

**`session84` (51)** — real `@dmi` frames from `replay/`:

- **Helpers.**
  - An `@dmi` line's loco is read from its token.
  - The panel state from a parsed frame equals the one from its line.
- **One tab.**
  - A text row shows the `@dmi` above it.
  - An `@dmi` row shows exactly itself.
  - A frame beyond the 10-minute look-back is "none".
- **Several tabs.**
  - One frame per loco, in key order.
  - Another tab's later frame is not used; the clicked tab's same-time frame wins.
- **Trimming.**
  - The resolver finds the clicked row after a live trim.
  - Trimmed away: no moment.
- **Broker.**
  - Nothing is resolved without a follower.
  - The first follower gets the last offer; every move is delivered while followed; nothing after unfollowing.
  - A closed window's resolver is not run; destroyed followers drop out.
- **Window.**
  - Live by default.
  - Follow picks up the selected row, with status and title naming the moment and the frame's age.
  - Live frames do not overwrite the moment.
  - Turning to loco 2 shows loco 2 at the same moment.
  - A stale frame is muted and said to be; no frame is blank and said to be.
  - Unticking returns to live; the mode is remembered.
- **Replay (two real captures loaded together).**
  - At every cursor, each loco's frame is an `@dmi` of that loco at or before it.
  - Somewhere both locos have a panel.
  - Scrubbing forward never shows an older frame.
  - The button is on Ctrl+Alt+D and opens a DMI window already following.
  - Changing the source turns the panel to it; → moves the panel with the cursor.

**Not tested end to end:** the click in a main-window or session tab. That
hook is a few lines handing `dmiTabResolver()` (tested above) the clicked
row; it was checked by reading, and the app was exercised by the smoke run.

### verify.sh

Each stage run with the script's own commands, **0 stages failed**:

- validators 11/11;
- unit suite **145 suites / 4405 checks** (144 / 4354 before);
- menu audit **134 ok**;
- headless smoke alive (500 datagrams).

`shot.pro` and `bench.pro` were updated but not built (not part of the gate).

---

<a id="session-83"></a>
## Session 83 — DMI (LP-OCIP) window per Annexure-B; fields added to the cab view

### 1. DMI window — Tools ▸ Monitor ▸ DMI (LP-OCIP)… (Ctrl+Alt+D)

The loco pilot's panel drawn from the loco's `@dmi` frames, laid out as
**RDSO/SPN/196/2020 Annexure-B, Amendment-3**:
- the 800 × 600-unit screen scaled to the window, aspect kept;
- each region at the annexure's position and size (B4.2.1), with its font
  sizes in pixels of that screen and its colour codes (Table B.2).

| Region | Shows | From `@dmi` |
|---|---|---|
| A1–A4 | target type, "Target Distance", LOR bar on the A1 scale (0/250/500/1000/2000 m at y 337/272/207/157/107), four-digit value; blank with no target (B4.3.3) | `target_distance_type`, `target_distance` |
| B1 | pointer (130 units, 52-unit hub), speed in black in the hub | `train_speed` |
| B2 | 0–250 km/h over +149°…−149°, 125 divisions of 2 km/h; every 10th labelled (17 × 2), every 5th 15 × 2, rest 6 × 1 | |
| B3 | LGR ring to the permitted speed with a one-division edge mark; DGR to the target speed; LOR from permitted to actual speed when over, BRD while the system brakes. Pointer white within the limit, yellow at it, LOR over it, BRD braking (B4.4.6, B4.10) | `speed_limit_permissible`, `target_speed`, `brake_type` |
| B4 | loco id | `train_id` |
| B5 / B6 | DD-Mmm-YYYY / HH:MM:SS | the frame's DATE_TIME |
| B7 | next lower speed | `target_speed` (while there is a target) |
| B8 | NB / FSB / EB symbol; blank otherwise | `brake_type` |
| B9 | LOC : x.xxx Km | `abs_loco_loc` |
| B10 | mode symbol (SB, SR, LS, FS, OS, Trip, Post Trip, REV, SH, NL, SF) | `loco_mode` |
| B11 | section speed | `section_speed_info` |
| C1–C3 | "Mov. Authority", LBL bar on the C1 scale (0/100/200/250/500/1000/2000/3000/+++ at the annexure's y), five-digit value | `ma_w_r_t_sig` |
| D1 | post with YLW / GRN / YLW / RED, the aspect lit; white disc C / IB / G / A / AG; junction route 1–6 at Table B.4's positions, stencil 1–32; blank when there is no aspect (B4.6.4.1 (d)) | `current_sig_aspect`, `current_sig_info` |
| D2 / D3 | four-digit signal distance; "DN MAIN Adv-Str" in ORG (line number filled into L-X) | `appr_sig_dist`, `current_sig_info` |
| E / F / G | DC X.XX / TL n m / mode in words (Annexure-A1) | `deceleration_constant`, `train_length`, `loco_mode` |
| H | system messages in the annexure's LP-OCIP wording, numbers filled (End of Authority in 60m, Head On Collision with Loco … in … m, Override … in …s, TurnOut / TSR / PSR …, Reverse Mode Expires …, LC Gate …); several alternate every 2 s (B4.6.7 (e)) | `alarm_code` flags + their fields |
| I | context messages likewise (SOS, Over Speed, FSB/EB will be applied in …S, Train Type selected) | `context_values` flags + their fields |
| J | antenna, RF, five bars | `signal_strength` |
| L | Tid-n, Dir-N/R, T Dist-n | `rc`, `movement_dir`, `dist_next_rfid` |
| M | the last three tags on the track diagram | `rc/rcs`, `rl/rls`, `rll/rlls` |
| K | the ten soft keys, as labels | — |

- **Colours.**
  - By default the panel **follows the theme**: the theme's background and
    ink, each Annexure-B hue moved just far enough to reach 3:1 on that
    background (the test holds this on the light theme).
  - **Annexure-B colours** (remembered) draws Table B.2's exact RGB on black,
    as the real panel.
  - Signal lamps and the disc are the railway's colours in both.
- **Monitor only.** The soft keys are labels; nothing is sent back to the
  loco.
- **Stale frame.** An old `@dmi` (over 3 s) is muted and says so.
- **Source selector.** One entry per loco heard.
- **Save image…** writes the panel as a PNG.
- **Assumptions**, listed under **Field sources…** and in `dmiAssumptions()`,
  to confirm against the firmware:
  - C is `ma_w_r_t_sig`;
  - B4 is `train_id`;
  - B7 is `target_speed`;
  - M: tag status 1 = read (DGR), 2 = missed (BRD), otherwise grey;
  - J: `signal_strength` capped at 5.

  "SOS – From Loco …" is shown without a loco id: the frame carries none
  for it, and the collision loco's id is not borrowed.

### 2. Cab view — add your own fields

Right-click the cab view:
- **Add field ▸ \<packet\> ▸ \<field\>**, listing what this loco has
  actually sent, each with its current value;
- **Remove field ▸ …**;
- **Remove all added fields**.

Added fields sit in a band under the cab, three to a row. Each is read from
its freshest source, as the tiles are, and is muted when stale. They are
saved as `lococonsole/cabFields`; adding the same field twice is refused.

### Tests

- **`session83` (34), on 12,000 real `@dmi` frames from `replay/`:**
  - the A1 and C1 scales at the annexure's offsets;
  - lamps for each aspect;
  - Annexure-A1 mode names;
  - H and I wording with their numbers filled;
  - every real frame gives a panel state;
  - the richest frame's regions each equal their own field;
  - date/time format and "DC X.XX";
  - the window: waiting, then the frame, the colour switch, Table B.2
    values, theme hues ≥ 3:1 on the light theme, PNG, one source per loco;
  - cab: fields added (duplicate refused), live values from the real DMI,
    remembered across windows, removed.
- **Menu audit (134 ok):** Tools ▸ Monitor ▸ DMI (LP-OCIP) on Ctrl+Alt+D
  opens the panel.
- **Checked by eye** (offscreen, real frame): the panel in Annexure-B
  colours and in the light theme, against the photo of the bench panel;
  the cab with two added fields. Found and fixed that way: the IB disc was
  drawn in the theme's ink (dark on light themes); it is white, as on the
  post.

### verify.sh

Full run, **0 stages failed**:
- validators 11/11;
- unit suite **144 suites / 4354 checks** (143 / 4320 before);
- menu audit **134 ok**;
- headless smoke alive.

---

<a id="session-82"></a>
## Session 82 — Live Loco Console widgets, tile rules, watch actions, find across tabs, clock history, replay speed

### 1. Big-number tiles

- **Colour rules** (right-click a tile ▸ Colour thresholds…):
  - **Fixed values:** amber at X, red at Y, "bad when high" or "bad when
    low".
  - **Against another field:** amber within a margin of that field's
    value, red above it. The limit moves with the run.
  - A red tile also gets a thicker border, so it reads without telling the
    colours apart. A stale value is muted and never red.
  - The **default Speed tile** is set against `dmi:speed_limit_permissible`
    with a 5 km/h margin. With no fresh DMI (older than 3 s) it gets no
    colour, rather than a guess.
  - Rules are saved as a third `|` part of the tile string. Older builds
    read the first two.
  - Setting a rule is undoable (Ctrl+Z).
- **Sparkline:** the last 60 s under every numeric tile, in the tile's
  current colour.
- **Time in state:** enum tiles (mode, direction, brake) show "for 4 min
  12 s" since the value last changed. The frame clock is excluded.
- **Plot:** double-click a tile (or right-click ▸ Plot over time) opens
  Field over time for that loco. A relative rule's limit field is added as
  a second series, so speed and permitted speed plot together. Jumps from
  that plot reach the main window.

### 2. Loco Console: cab view and lights

- **Cab view** (button beside Big numbers, remembered). A small copy of the
  driver's DMI, drawn from `@dmi`:
  - **Speed dial:** green arc up to the permitted speed, red above it,
    target-speed marker on the rim; the needle and number turn red above
    permitted.
  - **Distance-to-target bar:** 0–1000 m, compressed above 200 m as the
    DMI's own is, turning amber under 200 m, with the distance and **time
    to target** at the current speed.
  - **Mode and brake state**, the brake coloured by level: overspeed
    warning, service brake, emergency brake.
  - **Signal heads for the signal ahead and the next one**, from the SLRP's
    movement authority when an SLRP has been heard in the last 10 s,
    otherwise from the DMI's `current_sig_aspect`. Aspects are drawn as
    lamps:
    - red, yellow, double yellow, green;
    - junction and stencil routes named;
    - calling-on as red plus white;
    - "Unidentified" with no lamp lit;
    - a value with no assigned aspect is named, not drawn.
  - When the DMI frame is older than 3 s everything is muted and says so.
- **Heartbeat lights** (always shown). One light per packet type the loco
  sends, with its rate and a pulse on arrival. Each type is judged by its
  own rhythm:
  - amber when later than twice its usual gap (at least 2 s);
  - grey and "silent N s" past five times (at least 5 s).
- **Card health lights** (always shown), with ✓ ✗ ? glyphs as well as
  colour:
  - the DMI's three mapped health bits: Kavach unit, pulse generator, BIU;
  - one red light per module with an active fault in the latest NMS fault
    report, naming the faults.

  `radio_health_status` and `gps_status` have no mapping in the schema, so
  they get no light.

### 3. Watch panel

- **Actions** (right-click a watch): beep, bookmark the frame that fired it
  (the note is "watch: <name>"), announce every occurrence. They are shown
  as ♪ ⚑ ↻ after the condition and saved with it. Older builds ignore the
  fourth part.
- **Times column:** how many separate occurrences there have been. Matches
  more than 3 s apart start a new one, so a condition true for 300 frames is
  one occurrence. Re-arming clears it.

### 4. Find bar → every tab

- **All tabs** on each tab's find bar sends the current search to Search
  all sources. Each mode is converted to the query language:
  - Text → a quoted phrase, or an escaped regex if it contains a quote;
  - Extended → unescaped first;
  - Regex → `/…/`;
  - Hex → `hex:"…"`;
  - Query → as typed.
  - A search that can't be expressed (a regex containing `/`) is refused
    with the reason.
- **Group by tab** in the search window: each tab's hits under a heading
  with its count, in time order. The status line says in how many tabs.

### 5. Frame clock history

Click any of the three clock labels in the status bar for a live graph of
the **last 30 minutes**: loco − station, loco − this PC, station − this PC,
in seconds. One sample a second; zooming holds while it updates.

### 6. Replay

- **Speed:** Step (the old one record per 200 ms) or 0.25× to 50× in
  **recorded** time. A quiet minute takes a minute at 1× and 1.2 s at 50×.
- **◀ event / event ▶** buttons and `[` `]` jump between the Event Log's
  events, filtered to any event, warnings and errors, or errors. The status
  bar names the event.

### Code

- New: `cabpanel.{h,cpp}`: `CabDisplay`, `LinkLights`, `HealthLights`, and
  the pure `cabStateFrom`, `lampsFor`, `timeToTarget`, `heartbeatState`,
  `healthLightsFrom`.
- `TileRule` in `livefields`.
- `ClockHistory` and `ClockHistoryWindow` in `clockskewalarm`.
- `UiColor::signalLamp()`: railway lamp colours live in `uicolors`, as the
  contrast audit requires.
- Watch occurrences and actions in `watchlist`.
- `FindBar::toLogQuery()`.
- `SearchWindow::setGroupByTab()`.
- `ReplayWindow` speed control and `nextEventIndex()` / `stepEvent()`.

### Tests

- **`session82` (82).** The cab, heartbeat, health and replay parts run on
  **real frames from `replay/`**:
  - tile rules: fixed, bad-when-low, relative, no limit gives no colour,
    damaged rules, older tile strings, round trip, the default Speed rule;
  - the tile panel: levels, a 31-point sparkline trimmed to a minute,
    "for 30 s", then "for 1 min 5 s", then reset on change, stale never
    red, double-click asks to plot, a rule is saved and undone;
  - the cab from a real DMI (speed, permitted, mode, brake), with a real
    SLRP carrying a movement authority, emergency brake level, the lamps,
    time to target;
  - heartbeat thresholds, including a slow packet judged by its own rhythm;
  - health lights, including "nothing heard means nothing green";
  - the real console fed real LSRP, DMI, SLRP and NMS frames: cab shown
    with SLRP signals, a light per type sent, health from the real DMI;
  - watches: occurrences, every-time announcing, actions saved, an older
    watch string loading, re-arm clearing, the bookmark request from the
    panel;
  - find-bar query conversion for all five modes, and the grouped search
    with a real dispatcher (3 hits, then 5 rows grouped);
  - clock history cadence, 30-minute retention, the graph;
  - replay on a real capture: event stepping forward and back, the error
    filter, 50× playing forward.
- **Menu audit (132 ok).** Clicking the frame clock opens the history. Each
  tab's find bar has All tabs, which opens the search grouped. The watch
  panel has its Times column.
- **Checked by eye** (offscreen): the console with the cab view, lights and
  a real GSM-2 fault on real frames; a red tile with its sparkline; the
  clock graph.

### verify.sh

Full run, **0 stages failed**:
- validators 11/11;
- unit suite **143 suites / 4312 checks** (142 / 4230 before);
- menu audit **132 ok**;
- headless smoke alive.

Qt 5.15.13. Not yet seen on Windows.

### Notes

- The `@authkeys` capture line, listed earlier as pending, was already
  implemented (it is a known capture type).
- `replay/` has real `@dmi` (10,658), `@slrp` (3,340) and `@nmsflt` (156)
  frames, but no `@uba`. The braking-curve location scale (session 81)
  still needs a capture with `@uba` to confirm.

### Fix (82a) — tiles froze on the old LSRP after a loco restart

**Reported:** with the Live Loco Console showing values from LSRP, a
restarted loco's Mode, Frame clock and so on stopped updating. The loco
sends ARP again after a restart, but the tiles never moved to it.

**Cause.** A tile tried its sources in the listed order and took the first
one that had ever been heard. The default tiles list `lsrp` before `arp`, and
the LSRP held from before the restart still "had" the field. The tile kept
showing it, muted as stale, and never reached the live ARP.

**Fix.** `LiveFields::byFreshness()`: a tile tries its sources **most
recently received first**, and the listed order only breaks ties. After a
restart the ARP is the freshest, so the tiles follow it; when LSRP resumes,
they go back to LSRP. Plot-from-tile picks its packet the same way.

Status-bar pins are not affected: a pin names one packet type, and it
already shows its age as it goes stale.

**Test** (`session82`, +8). On real frames from `replay/`:
- running on LSRP, the frame clock says "from lsrp";
- after the "restart" (ARPs only), the frame clock and mode equal the
  ARP's values and say "from arp";
- when LSRP resumes, the tiles go back to it.

Run with the old first-listed order, the test fails on exactly the reported
symptom (3 failures). With the fix: unit suite **143 suites / 4320
checks**, menu audit 132 ok, 0 stages failed.

---

<a id="session-81"></a>
## Session 81 — Loco config confirmation, several fields per plot, speed vs distance, export, run summary report, clock-skew alarm

### 1. Loco Configuration — the loco's answer is recorded

The window already compared each `@linfo` with the last send (the "Loco
check" line). What was missing was a record of the answer, and a word when
no answer comes.

- **Recorded.** The first `@linfo` after a send is judged once and appended
  to `loco_config_history.jsonl` as a `"kind":"verification"` line: loco,
  when it answered, match or not, and each differing field ("frame_cycle:
  expected 3, loco has 7").
- **History dialog.** A **Loco confirmed** column: `✓ 7_1 at 14:03:22`, or
  `✗ 7_1: 3 differ` with the fields in its tooltip and in the detail pane.
  A send with no answer says "Not confirmed".
- **Notification.** The main window announces the verdict (Info on a
  match, Warning otherwise), since the window may be behind others when
  the answer arrives.
- **Overdue.** When no `@linfo` has arrived after 3× that loco's own print
  interval (never under 60 s), the check line warns. It also says why that
  may be: the VCC takes the config only in application mode, and `@linfo`
  must reach this PC.
- Verification lines are not sends: `readAll()` skips them without
  counting them as damaged.

### 2. Field over time — several fields on one time axis

- **+ Add field** (the same Packet ▸ Field menu), up to **6** fields. Each
  one has a coloured chip with ✕ to remove it. The first field cannot be
  removed.
- **Lanes by default**: one per field, each with its own value axis and
  title in its colour. **One axis** overlays them, for fields in the same
  unit (e.g. TRAIN_SPEED against a permitted speed). The shared axis is
  titled by that unit, or "value (mixed units)".
- **Shared cursor.** Hovering draws one line through every lane and reads
  every field at that instant (the sample at or before it). With several
  fields each is named by packet, so `lsrp ▸ LOCO_MODE` and
  `arp ▸ LOCO_MODE` are never confused.
- **Zoom behaves as before, for all fields together:**
  - wheel zooms time; Shift+wheel zooms the values of the lane under the
    cursor;
  - a box drag zooms time (and the values of that lane if the box has
    height);
  - zooming in re-reads every field at full resolution.

### 3. Speed vs distance — Tools ▸ Monitor ▸ Speed vs distance… (Ctrl+Alt+V)

- **From `@dmi`**, all from the same frame: `abs_loco_loc`, `train_speed`,
  `speed_limit_permissible`, `target_distance`, `target_speed`,
  `loco_mode`. Speed from one packet paired with location from another
  would pair two instants.
- **Falls back to `@lsrp`** (ABS_LOCO_LOC, TRAIN_SPEED) when a tab has no
  DMI, and says it has no permitted speed or targets.
- **Draws:**
  - actual speed;
  - permitted speed as a dashed step;
  - targets as markers, target_distance ahead in the direction of travel,
    each distinct target once;
  - samples above permitted as red dots, with a count.
- **Direction from the locations** (net change; jumps over 500 m are
  resets). A train counting down has the axis reversed so it always runs
  left to right, and the axis title says so.
- **Braking curve.** The firmware's own `@uba` curve in force at the
  cursor (EBD solid, SBD dash-dot), converted from m/s. It is drawn only
  if it lies within 5 km of the train's locations. Otherwise it is
  reported as not on the same scale, rather than drawn somewhere
  meaningless.
- Same zoom controls as the field plot (distance on the wheel, speed on
  Shift+wheel). Clicking a point jumps to its message. Speed 511
  ("unidentified") is skipped and counted.

### 4. Export — both plot windows

**Export ▸ Save image (PNG)… / Save data in view (CSV)… / Copy image.**
- The image is the canvas, legend and axis titles included, so it stands
  on its own in a report.
- **Field CSV:** one row per distinct sample time in view, one column per
  field (`lsrp.LOCO_MODE`). A cell is empty where that field had no sample
  then; nothing is carried forward into it.
- **Speed–distance CSV:** time, location, speed, permitted, target
  distance and speed, overspeed flag, mode, for the stretch in view.

### 5. Run summary report — Tools ▸ Monitor ▸ Run summary report… (Ctrl+Alt+R)

One pass over the current tab, shown in a window. **Save HTML…** writes one
file that opens in any browser and prints to PDF; **Copy** gives the text.
It reads:

| Section | From |
|---|---|
| Span, duration, rows, rows per packet type | every row |
| Silences over 5 s, with the last packet before each | every row |
| Loco mode: first, then each change, with the schema's names | LSRP LOCO_MODE |
| Emergency status ≠ 0, as episodes | LSRP EMERGENCY_STATUS |
| Highest speed; above-permitted episodes (worst, where) | DMI (or LSRP) |
| RFID tags: each new tag, distinct count | LSRP LAST_RFID_TAG |
| Loco/station clocks outside the accept window, as episodes | SLRP FRAME_NUM against the loco's own LSRP/ARP FRAME_NUM from ≤ 10 s before |
| Faults raised and cleared | NMS fault frames |
| SLRP frames matching a reject condition, by clause | `rejectrules.xml`, with the learned loco identity |

- It says **OBSERVED** and makes no pass/fail judgement: verdicts belong to
  the signatories.
- Long lists are capped at 200 and say how many there were in all.
- Shown as paper (dark ink on white) in every theme. Its colours are
  allowed by the contrast audit as a standalone HTML document, like the
  assertion report.

### 6. Clock-skew alarm

- The status bar already turned the loco–station gap red. Now a
  **notification** says so once per episode:
  - raised when the gap has been outside the window (more than 4 s old,
    or 2 s or more ahead) for **3 s**, since a single late frame is not a
    clock problem;
  - cleared after 3 s back inside, with how long it lasted and the worst
    gap.
- Only compared while both clocks were heard in the last 10 s. No data is
  neither "in step" nor "out".
- The same episodes, computed offline from the capture, are in the run
  report.
- **Covers the loco–station gap only.** This laptop's clock against the
  equipment matters only for what DLConsole sends. It stays in the status
  bar as before.

### Code

- New: `speeddistance.{h,cpp}`, `runreport.{h,cpp}`,
  `runreportwindow.{h,cpp}`, `clockskewalarm.{h,cpp}`.
- `fieldplot`:
  - `collectRowFields()` (several fields of one packet, row by row, raw
    integers);
  - `valueAtOrBefore()` and `seriesToCsv()`;
  - the canvas takes a list of series in lanes or overlaid;
  - the window has add/remove/overlay/export.
- `LocoInfo::Verification`, `SendHistory::appendVerification()` /
  `readVerifications()`; `LocoConfigWindow::verified` signal, `recheck()`.
- `brakingcurves.cpp` is now in the unit-test build (it wasn't before).

### Tests

- **`session81` (70).** Frames with chosen values are made from real
  captures by rewriting fields at the bit spans the decoder reports, then
  decoded again to prove it. Covered:
  - cursor semantics and CSV (empty cells, no carry-forward);
  - the real plot window: add, duplicate refused, lanes vs one axis, the
    limit of 6, remove, first field kept, PNG and CSV export;
  - speed–distance: direction (and resets), target placement, a 60-frame
    run with 5 samples above permitted, targets, stretch CSV, the LSRP
    fallback counting down, zoom, export, the "no @uba" note;
  - skew alarm: hold time, brief excursions, worst gap, no-data, clear
    time, the asymmetric window;
  - report: packet counts, the 12 s silence, the mode change with names,
    the tag, highest speed, the +5 km/h episode, OBSERVED and no verdict,
    every section, capped lists, HTML save;
  - offline skew from real SLRP frames: one episode from the first frame
    9 s out.
- **`locoinfolivecheck` (+8).** The confirmation is written against its
  send, once, and announced once. A verification line is not a damaged
  send. The overdue warning appears. A differing answer is recorded naming
  `frame_cycle` and announced as a mismatch.
- **Menu audit (+6, 127 ok).**
  - Tools ▸ Monitor has Speed vs distance (Ctrl+Alt+V) and Run summary
    report (Ctrl+Alt+R), and both open on the current tab.
  - The skew alarm is running on the real main window, and raises and
    clears.
- **Checked by eye** (offscreen, `DL_SHOTS`): the two-lane plot with the
  shared cursor, the speed–distance view with an overspeed stretch, and
  the report window.

### verify.sh

Full run, **0 stages failed**:
- validators 11/11;
- unit suite **142 suites / 4230 checks** (141 / 4151 before);
- menu audit **127 ok**;
- headless smoke alive.

Qt 5.15.13. Not yet seen on Windows.

---

<a id="session-80"></a>
## Session 80 — Flasher route overlap; Field over time: Packet ▸ Field menu, zoom, legend, axis titles

From four photos: the flasher's delivery route drawn on top of itself, and
the Field-over-time plot unreadable (LOCO_MODE, MOVEMENT_DIR,
SOURCE_LOCO_ID and TIN as solid blue barcodes).

### 1. Flasher — the delivery route no longer overlaps itself

**Cause.** The gap between the diagram's rows was `(height − 3 tiles) / 2`.
On a short window (Windows, laptop screen) the left column was squeezed,
the gap went negative, and the VCC tile landed on the Input / Output /
Analog row. The same squeeze cut the pre-flight list off mid-sentence.

- The row gap is now fixed (one text line, at least 14 px) and never
  derived from the height the layout hands over.
- The diagram refuses to be squeezed (`minimumSizeHint` = what it needs),
  and asks for more when the text size goes up.
- The **left column scrolls** instead of squeezing its cards, so the
  pre-flight list is never clipped either.
- Belt and braces: if it is ever given less height, the tiles shrink (and
  drop their caption) rather than overlap.

### 2. Field over time — Packet ▸ Field (the "nested menu")

**Why the plots were barcodes.** A field *name* was plotted from every
packet in the tab that carries one of that name. 35 names are shared:
LOCO_MODE from the loco's own LSRP was interleaved with LOCO_MODE from
every ARP it received, and SOURCE_LOCO_ID mixed the loco's own ID with other
locos'. The trace jumped between two signals every half-second.

- The chooser is now a button with a **menu per packet type** —
  `lsrp (4,213 rows) ▸ LOCO_MODE` — and a series is one packet's field.
- A field that is also in another packet says so in its tooltip ("Also in:
  arprecv, dmi — a different signal there") and in the status line.
- Opening from a right-click (field inspector, pins, compare) picks the
  packet carrying the field in the most rows; the menu switches it.
- Only rows of the chosen packet are decoded, which is also much faster on
  a busy tab, and the row cap now applies to that packet's rows.

### 3. Field over time — clean, zoomable

- **Step traces for states.** Whole-number fields (modes, directions,
  flags) are drawn as steps, each value held until the next sample. A
  sloped line from mode 2 to mode 6 drew modes 3–5 that never happened.
- **No lines across gaps** longer than the series' own rhythm (10× its
  median interval, at least 3 s).
- **Zoom and pan:**

  | Do | To |
  |---|---|
  | Mouse wheel | zoom time about the cursor |
  | Shift + wheel | zoom values about the cursor |
  | Drag a box | zoom to it (flat drag = time only) |
  | Right-drag | pan |
  | Double-click, **Fit**, or `0` | show everything |
  | `+` / `−`, ← / → | zoom, pan |
  | Right-click | Zoom in / out / Fit |
  | Click a point | jump to its message (as before) |

- **Full resolution when zoomed.** A long tab is sampled (every Nth row) to
  plot the whole of it. Zooming in now re-reads the visible window with
  every row decoded (debounced while wheeling); the status line says
  whether the view is complete or still sampled.
- Values re-fit to what is in view unless the value axis was zoomed.
- Time ticks land on round wall-clock times (1-2-5 steps from 1 ms to
  1 day); milliseconds shown when the view is under a second.
- Sample dots appear only when they can be told apart.

### 4. Field over time — legend and axis titles

- **Legend** above the plot: line swatch, `lsrp ▸ LOCO_MODE [km/h]`, how
  many samples (and how many are in view when zoomed), and "step" for
  state fields.
- **Value axis title**: the field name, with its unit when the schema
  prints one (`TRAIN_SPEED (km/h)`).
- **Time axis title**: `Time (local, 2026-09-23)`.
- **Enum names on the value axis**: where the schema prints `2
  (Staff_Responsible)`, the tick reads `2 Staff_Responsible`.
- **Hover readout**: time to the millisecond, value with unit and enum
  name, and a vertical cursor line.

### Tests

- **`fieldplotzoom` (39), real LSRP and ARP frames interleaved:**
  - capture type read off the tag, agreeing with the full parser;
  - untyped LOCO_MODE interleaves both packets (the bug); typed gives only
    that packet's rows, and only those are decoded;
  - enum names and units taken from the schema's display, none invented;
  - a time window takes exactly its rows; the cap samples the whole span;
  - catalogue: both types, row counts, fields, shared-field lookup;
  - time ticks round and aligned, milliseconds under a second;
  - the real window: Field ▸ per-packet submenus, the shared-field
    tooltip, choosing plots that packet only, zoom in/out/fit, a set
    window, wheel, double-click, box drag, the `0` key.
- **`flasherroute` (8):** the real Flasher window at 900, 560 and 420 px
  high — the diagram always gets its full height, the left column is a
  scroll area, and a bigger font raises the height it insists on.
- **`pinboard`:** updated for the new chooser (a button, not a combo box).
- **Checked by eye** (offscreen renders, `DL_SHOTS=<dir>` writes them): the
  flasher at 560 px with the route intact and the column scrolling; the
  plot fitted and zoomed with legend, axis titles and `7 Trip` on the axis.
  Not yet seen on Windows.

### verify.sh

Full run, **0 stages failed**: validators 11/11, unit suite **141
suites / 4151 checks** (139 / 4104 before; + `fieldplotzoom`,
`flasherroute`), menu audit **121 ok**, headless smoke alive. Qt 5.15.13.

---

<a id="session-79"></a>
## Session 79 — Batches B and C: layouts, crash recovery, undo, settings transfer, colour-blind-safe colours

Everything in session 78's "Next" list. Layouts and crash recovery share one
core (`WorkspaceSnapshot`); undo is used by both, and by the settings import.

### 1. Window layouts — View ▸ Layouts

- **Save current layout as…** asks for a name; a same-named layout is
  replaced after a Yes.
- **Each saved layout** is an item in the menu, the first nine on
  **Ctrl+Alt+1 … 9**; its tooltip says what it holds ("5 tabs (1 hidden),
  2 pop-outs").
- **Delete layout ▸** one submenu item per layout.
- **What a layout holds:** the main window's size and panels, every tab in
  bar order, which are hidden, the tab in front, which tabs are popped out
  and where.
- **What it does not:** tool windows (Loco Console, fault panel, …) and
  per-tab filters. A filter restored unasked hides live traffic, which is
  why the workspace never stored one either.
- **Switching:**
  - tabs the layout does not name are hidden, as a closed tab always is:
    still watched, and back if they speak;
  - a pop-out for a loco not heard yet waits, as at startup.
- **Undoable:** Ctrl+Z after a switch goes back to the arrangement before
  it; a deleted layout goes back in its old place.
- **Stored in** `window_layouts.json` beside `dlconsole.ini`.

### 2. Crash recovery

The disk log already survived a crash. The arrangement did not: workspace,
pop-outs and panels were written only in `closeEvent`.

- **While running:** every 5 s the arrangement is compared with the last
  one written, and `session_recovery.json` is rewritten only if it changed
  (atomically, via `QSaveFile`).
- **The flag:** `session/running` in the ini is set at startup and cleared
  by a clean close (and by the destructor, which a crash never reaches).
- **Next start after a crash, kill or power cut:** the snapshot is written
  into the ordinary workspace/pop-out/layout keys *before* the ordinary
  restore runs. One restore path, not two.
- **Then a warning notification:** "DLConsole did not close cleanly last
  time. Restored the workspace it had: 6 tabs, 1 pop-out, as it was at
  14:02:31." Its detail says the disk log is complete up to the stop.
- **Refuses rather than guesses:** a damaged or foreign recovery file is
  ignored; a file left behind by a *clean* run is stale and is deleted.

### 3. Undo — Edit ▸ Undo (Ctrl+Z), and an offer in the status bar

After a destructive click, **"↶ Undo Clear tab L1_V1"** appears in the status
bar for 15 s. The Edit menu item always names what it will undo.

| Window | Undoable |
|---|---|
| Main | Clear this tab · Clear all tabs · Remove bookmark · Remove all bookmarks · unpin a status-bar value · remove a watch · switch / delete layout · Import settings |
| Loco Configuration | Delete configuration (back in its old place) · Reset all fields to defaults |
| Loco Console | Remove a big number · Reset big numbers |
| Firmware Flasher | Delete profile |

- **One log per window:** Ctrl+Z in the Loco Configuration never brings
  back a tab cleared in the main window ten minutes earlier.
- **Cleared rows come back in front of** anything that arrived since; if
  capacity forbids all of them, the newest of the old rows win, and rows
  that arrived since are never pushed out.
- **Memory:** at most 20 steps and 1,000,000 held log rows per window,
  oldest step dropped first. The newest step always survives, however big.
- **When the thing is gone** (the config was recreated, another config is
  open, a batch is flashing): the step is consumed and the notification
  says it could not be undone, rather than pretending.
- **Confirmations kept** where they were; "This cannot be undone" on Remove
  all bookmarks now says Ctrl+Z brings them back.

### 4. Settings export / import — File menu

One JSON file (`"format": "dlconsole-settings"`), six sections:

| Section | Carries |
|---|---|
| Theme and appearance | theme (and last light/dark pair), colour-blind-safe colours, text size |
| Tab colour tags | every tab's colour and label |
| Pins and big numbers | status-bar pins, big-number tiles, Pinned fields panel |
| Window layouts | `window_layouts.json` |
| Firmware Flasher profiles | `flasher_profiles.json` |
| Loco configurations | `loco_configs.json` |

- **Not carried:** network port, disk-log paths and quotas, window
  positions outside layouts, session keys, history logs.
- **Export:** tick sections (each shown with what it holds, e.g. "2
  profiles: Lab, Shed"); a section with nothing set up is not written.
- **Import:**
  - ticked sections **replace** what the console has, whole — not merged;
  - everything is checked before anything is written (layouts through the
    layout store, flasher profiles through the Flasher's own loader, loco
    configurations for format and a non-empty list); one bad section
    stops the import with nothing changed;
  - a section can only set the INI keys it owns;
  - refused while the Firmware Flasher or Loco Configuration window is
    open, since each holds its own copy and would save it back over the
    import;
  - applied at once: theme and text size, tab tags (tabs and pop-outs),
    status pins, Pinned fields, big numbers in open Loco Consoles;
  - **Ctrl+Z puts back exactly what was there**, including deleting a file
    that did not exist before.

### 5. Colour-blind-safe status colours — View ▸ Theme

- **Off by default;** stored as `ui/colorBlindSafe`, read before the theme
  is applied at startup.
- **On:** ok / warning / error become blue / amber / raspberry (sky /
  yellow / vermillion on dark themes), each moved only as far as the
  theme's contrast floor needs on window, base, alternate row and button.
- **Accent** becomes the text colour, since ok now has accent's blue.
- **Chosen by simulation, not by eye:** under simulated protanopia,
  deuteranopia and tritanopia (Machado 2009), every verdict pair stays ≥ 20
  CIELAB units apart in all seven themes. The theme sets reach **4** (Sepia
  ok vs error, deuteranopia).
- Glyphs (✓ ! ✗) are unchanged and still carry the meaning on their own.

### Code

- New: `undolog.{h,cpp}`, `workspacesnapshot.{h,cpp}` (snapshot, layout
  store, `SessionRecovery`), `settingsbundle.{h,cpp}`,
  `mainwindowsession.cpp` (the main window's side of all five).
- `LogModel::takeAll()` / `restoreOlder()`; `BookmarkStore::replaceAll()`;
  `ConfigStore::insertAt()`; `StatusPins::reload()` and
  `BigNumberPanel::reloadAll()` for import.
- `UiColor::setColorBlindSafe()`; `Settings::colorBlindSafe()`,
  `sessionRunning()`.

### Tests

- **`colorblind` (69):** the simulation itself; with the mode off some
  theme's ok/error are < 10 apart for deuteranopes; with it on, every pair
  ≥ 20 (accent ≥ 12 from each verdict) in normal vision and all three
  deficiencies, every colour ≥ 4.5 : 1 (7 : 1 High Contrast) on four
  surfaces, stylesheet helpers follow the mode, off gives the theme back.
- **`undolog` (25):** order, consumption, a gone target, step and row caps,
  the newest step surviving; the Undo action's text and shortcut;
  `takeAll`/`restoreOlder` order and capacity; bookmarks restored and saved.
- **`workspacesnapshot` (31):** JSON round trip including binary state;
  record format matches `saveWorkspace()`; layout store replace-in-place,
  name cap, re-insert at old index, foreign file refused; recovery after a
  clean run (stale file deleted, nothing adopted), after a crash (tabs,
  active tab, pop-outs, their geometry and dock state adopted), damaged
  file ignored.
- **`settingsbundle` (27):** two temp "machines"; export of every section
  and summaries; import replaces whole; empty big-number list stays empty;
  port untouched; undo restores INI and files; all-or-nothing on a bad
  flasher section; unknown loco format refused; a section cannot set keys
  it does not own.
- **`menuaudit` (+29, 121 ok):** on the real main window — Edit ▸ Undo
  first on Ctrl+Z; Clear this tab then the status-bar offer then Ctrl+Z
  restores every row; save / switch (tab hidden, pop-out opened) / undo /
  delete / undo a layout, Ctrl+Alt+n; a **second real MainWindow** started
  after a simulated crash comes back with the crashed run's tabs, same tab
  in front; a clean close leaves no file and the flag down; tag import and
  its undo; loco-config import refused while that window is open; the
  colour-blind toggle switches and is remembered.
- **Not covered by a test:** Flasher Delete-profile undo (the delete goes
  through a modal dialog the harness does not drive); checked by reading.

### verify.sh

Full run, **14/14 stages green, 0 failed**: validators 11/11, unit suite
**139 suites / 4104 checks** (135 / 3952 before; + `colorblind`, `undolog`,
`workspacesnapshot`, `settingsbundle`), menu audit **121 ok** (92 before),
headless smoke alive. Built with Qt 5.15.13 only; not checked by eye this
session (no display here) — worth a look at View ▸ Layouts, the status-bar
Undo offer and the colour-blind colours in one light and one dark theme.

### Next

- Look over the new UI on Windows / Qt 5.15 (status-bar Undo button
  placement, the two import/export dialogs).
- Optional: let a layout also reopen tool windows (Loco Console, fault
  panel) — deliberately left out, see §1.

---

<a id="session-78"></a>
## Session 78 — Live data, batch A: big numbers, change highlighting, pins, loco check

The first of three batches. B (window layouts, crash recovery, undo) and C
(settings export/import, colour-blind-safe status colours) follow in a new
chat. See "Next" at the end.

### 1. Change highlighting — Live Loco Console

A field whose value just changed is marked: an accent tint and **bold** for
2 s, then the tint fades out by 4 s.
- **Where:** in every packet tab, so a changing value stands out in a
  170-row table.
- **What counts as a change:**
  - Rows are keyed by field name *and* occurrence, since names repeat inside
    some structs.
  - A type's first frame marks nothing, because there is no "before".
  - Switching to another loco resets the history, so its values aren't
    flagged as changes.
- **With Find:** Find's match tint is applied after the change marks, so a
  search result is never hidden by one.
- **Switch:** "Highlight changes" in the top strip, on by default,
  remembered.

### 2. Big-number mode — Live Loco Console, configurable

A **Big numbers** button shows a row of tiles above the packet tabs, each
one field in large type (3× the app font, so text size and F11 make them
bigger still).
- **Defaults:** **Speed**, **Mode**, **Frame clock** and **Brake**, from the
  loco's own LSRP, falling back to ARP when LSRP is absent.
- **Frame clock:** FRAME_NUM (seconds since midnight + 1) is shown as a
  clock, **16:29:16**, with the raw value underneath.
- **Brake:** the raw `Brake_Applied` value. The schema gives its bits no
  meaning; with a mapping it could say "EB".
- **Detail line:** where each value came from ("from lsrp"). A value
  older than 3 s is **muted**, with its age, so a stale number is never
  mistaken for a live one across the room.
- **Configurable:**
  - right-click any field in any tab ▸ **Show as big number**;
  - right-click a tile ▸ Rename, Move left/right, Remove, Reset to
    defaults;
  - the tiles and whether the panel is shown are remembered.

New `bignumberpanel.{h,cpp}`; `livefields.{h,cpp}` holds the field
references it shares with the pins.

### 3. Pin a value to the status bar

Right-click any field in the Live Loco Console ▸ **Pin to status bar**. The
value then shows in the main window's status bar as e.g. "1_1 · Mode: 2
(Staff_Responsible)", whatever window is in front.
- **Updates on its own:** it reads the capture stream directly, so it keeps
  updating with the Loco Console closed.
- **Display:** FRAME_NUM shows as a clock. A value not refreshed for 5 s is
  muted, with its age in the tooltip.
- ✕ unpins. Up to 6 pins, no duplicates, remembered between runs.

The field menu also has **Copy value**. New `statuspins.{h,cpp}`.

### 4. Check what the loco actually holds — Loco Configuration

The VCC prints its LOCO_INFO (`@linfo`) periodically. Each one is compared
with what this configuration last sent, the loco being matched by
`loco_unit_id`. The result shows under "Last sent":
- **✓ Loco 7_1 holds exactly what was last sent** (seen N s ago). This is
  the confirmation a send never got before.
- **Waiting for loco 7_1's next @linfo** since the send at HH:MM:SS. An
  `@linfo` received *before* your send isn't reported as a failure.
- **✗ Loco 7_1 holds something else: 3 fields differ.** The tooltip lists
  each field with the value sent and the value the loco has.
- **For a configuration never sent from here:** whether the loco matches
  it.
- A loco reporting a LOCO_INFO whose `loco_info_crc` doesn't match its
  contents is called out, not compared.
- **Load the loco's values…** copies what the loco reports into the
  editor.
- MainWindow hands the window the dispatcher (`setLiveSource`). Each
  `@linfo` is parsed with the same schema layout as everything else.

Also fixed here: messages read "1 field(s) differ"; they now say "1 field"
or "3 fields", including the Send confirmation from session 70.

### Tests

- **`livefields` (10):**
  - references round-trip; sources round-trip for every type, including
    "nms hlth";
  - indented field names are found;
  - FRAME_NUM → clock, including the ends of the day and bad input;
  - the default tiles.
- **`lococonsolelive` (19):** the real Loco Console fed **two real `@lsrp`
  lines** from the replay files.
  - Big numbers show 50 km/h, 2 (Staff_Responsible), 16:29:16 with
    FRAME_NUM 59357, and Brake 0.
  - After the second frame: 45 km/h and 6 (On_Sight).
  - TRAIN_SPEED is highlighted and bold; an unchanged field is not; the
    first frame marks nothing; switching the highlight off clears marks.
  - Tiles can be added, and tiles and visibility are remembered.
- **`statuspins` (11):**
  - — before data, then "1_1 · Mode: 2 (Staff_Responsible)" from the real
    line;
  - FRAME_NUM as a clock; duplicates refused; at most six; ✕;
  - remembered between runs.
- **`locoinfolivecheck` (10):** the real Loco Configuration window.
  - Matches a never-sent configuration; one edit is reported as 1 field.
  - After a send: "Waiting…"; then ✓ when the matching `@linfo` arrives.
  - A differing `@linfo` gives ✗ 1 field.
  - Another loco (other `loco_unit_id`) is ignored.
  - A bad `loco_info_crc` is called out.
- **`menuaudit`:** the pins are in the main window's status bar and
  reachable from the Loco Console.
- `lococonsolewindow.cpp` and `replaywindow.cpp` are now in the unit-test
  build, which is what made the Loco Console testable.
- **Checked by eye:** the Loco Console with big numbers and a highlighted
  change, in Light and Nord.

### Next (batch B and C, for a new chat)

- **B:** save and switch window layouts (shares its core with crash
  recovery); crash-safe session recovery (the disk log already survives a
  crash; the workspace and pop-outs only save on a clean exit); undo for
  destructive clicks (Clear this tab, Delete configuration, …).
- **C:** settings export/import (themes, tags, flasher profiles, loco
  configurations, layouts, pins); colour-blind-safe status colours.

### verify.sh

Full run, **14/14 stages green, 0 failed**: validators 11/11, unit suite
135 suites / 3952 checks (+ `livefields`, `lococonsolelive`, `statuspins`,
`locoinfolivecheck`), menu audit 92 ok, headless smoke alive. Built with
Qt 5.15 only.

---

<a id="session-77"></a>
## Session 77 — Loco Configuration: bulk send to up to 4 VCCs

### What changed

The send bar has **four target rows** (tick box, IP, port) instead of one IP
and port. **Send** goes to every ticked row with an address.
- **Same bytes to every target:** the same 375-byte datagram, CRC and all.
- **The button says how many:** "Send to VCC…" or "Send to 3 VCCs…".
- **The confirmation lists every target.** With more than one it also says
  *"All 3 receive the same configuration, including loco_unit_id N"*, next
  to the `vcc_crc`. Sending one unit id to what are actually different locos
  is the mistake bulk send makes easy, so it is stated where the operator
  presses Yes.
- **Each target is sent to separately.** `UdpSender`'s multi-target send
  only reports whether *any* target got through, so it isn't used here. A
  failed target is named in the status line with its reason ("Sent to 2 of
  3 — FAILED: 10.0.0.9:50001 (…)"), and the others are still sent to.
- **One history record per target**, each naming its own `ip:port`, so the
  History dialog, CSV and re-export work per VCC as before.
- **Checks before Send:**
  - at least one ticked row with an address;
  - every ticked address a valid IPv4 address (reported by row number);
  - the same `address:port` not listed twice;
  - an unticked row is ignored, whatever is in it.
- "Last sent … to" lists every target of the last send.
- **New** copies the current configuration's targets.

### Saved files

- `loco_configs.json` gains a `targets` array (ip, port, enabled × 4).
- A file from before this session loads its single `target_ip` / `port`
  into row 1.
- Row 1 is still written under the old keys, so an older DLConsole reads
  the file.

### Files

- `lococonfig/lococonfigcore.{h,cpp}`: `SendTarget`, `kMaxTargets`,
  `targetProblem()`, and targets in the JSON (read, write, legacy).
- `lococonfig/lococonfigwindow.{h,cpp}`: the four-row send bar,
  `setTargets()`, per-target send and history, the confirmation, the
  button label.
- `README_FLASHING_AND_CONFIG.md`: the "send the configuration" steps
  describe the targets.
- `tests/test_lococonfig.cpp`.

### Tests

- **`lococonfig` (70, +13):**
  - all four rows round-trip (address, port, ticked);
  - a pre-bulk-send file loads into row 1 and still writes the old key;
  - the rules: nothing to send to, a bad address refused by row, an
    unticked junk row ignored, the same `address:port` twice refused, same
    address on a different port fine.
- **`lococonfigrun` (30, +8):** the real window, three stand-in VCCs on
  127.0.0.1 and an unticked junk row.
  - The button says "Send to 3 VCCs…".
  - All three receive exactly the same datagram.
  - Three history records, each naming its own target.
  - The same target twice blocks Send.
- **Not tested end to end:** one target *failing* while the others succeed.
  On localhost there is no dependable way to make a single UDP send fail.
  The code path (per-target result, failure named in the status line) is
  small and straightforward.
- **Checked by eye:** the send bar with three targets filled in.

### verify.sh

Full run, **14/14 stages green, 0 failed**: validators 11/11, unit suite
131 suites / 3902 checks, menu audit 91 ok, headless smoke alive. Built
with Qt 5.15 only.

---

<a id="session-76"></a>
## Session 76 — Text size, presentation mode, tab colour tags, better pop-outs

### 1. Text size — View ▸ Text size (Ctrl + / Ctrl − / Ctrl 0)

One size for the whole application, from 80 % to 200 %, remembered between
runs. The shortcuts work from any DLConsole window. New `textzoom.{h,cpp}`.

**What scales, together:**
- **The app font:** everything that inherits it.
- **Every widget that sets its own size** (titles, monospace hex and
  values, the flasher's big numbers), keeping its proportion to the rest.
- **Every table's fixed row height,** so larger text is never clipped.
  This covers the log tabs, Live Loco Console, flash queue and field
  tables.
- Windows already open are rescaled in place. Windows opened later come out
  at the same size: `UiStyle::monoFont()` and `Settings::rowHeightFor()`
  apply the factor.
- UiStyle's 9 pt minimum now applies only before zoom starts; otherwise it
  quietly undid 80 % and 90 %.

**Two Qt behaviours found and handled while testing:**
- **With an application style sheet, Qt remembers each widget's font** the
  first time it styles it, and puts that font back every time the sheet is
  re-applied, which zoom must do. Explicitly sized widgets were snapping
  back to their first size on the second zoom. Zoom now updates Qt's
  remembered font (`_q_styleSheetWidgetFont`, the same property in Qt 5
  and 6).
- **"100 % size" is derived per widget and re-derived when something else
  resized it** (e.g. a Row density change), so repeated zooming never
  drifts. Tested with 20 changes back and forth.

### 2. Full screen / presentation — View ▸ Full screen / presentation (F11)

For whichever window is active: the main window or any tool window. New
`presentationmode.{h,cpp}`.
- **In:** full screen; menu bar, status bar and toolbars hidden; text one
  size larger while any window presents (not saved).
- **Out:** F11 again, or **Esc** unless you're typing in a field (Esc in
  the find bar still closes the find bar). Every bar comes back, and the
  window returns to normal or maximised as it was.
- Menu shortcuts keep working while the menu bar is hidden: they are lent
  to the window and handed back afterwards.

### 3. Tab colour tags — right-click a tab ▸ Colour Tag

Eight colours plus an optional short label, remembered per loco. New
`tabtags.{h,cpp}` and `UiColor::tagColor / tagName`.
- **Shown as a dot before the tab name,** plus the label after it
  ("L1_V1 · Brake test"). The name's own colour stays free for **tab
  health** (amber/red when a source goes quiet), so the two never compete.
- **Readable in every theme:** each tag colour is adjusted to at least 3:1
  (the WCAG floor for a graphical object) against the window and the
  background in every theme, and the eight stay distinct. The tests check
  this on all seven themes.

**The tag follows the loco everywhere:**
- its **pop-out window** (icon and title);
- that window's **minimise chip**, which shows the window icon;
- the **Live Loco Console**: dots in the Loco / Ctrl selector, and the
  window's title and icon name the loco being watched ("Live Loco Console —
  7_1 · Brake test").
- **How the Loco Console matches locos:** a tab's key comes from the UDP
  header (source id, kavach id), while the console lists the capture
  line's own loco/ctrl ids. They aren't assumed to be the same numbers.
  Each capture key is mapped to the tab its lines actually arrived on.

### 4. Pop-out windows — "Pop Out to Window", the Detach button, or drag a tab out

`DetachConsoleDialog` is replaced by `TabPopoutWindow`. It's still a **live
copy**: it shares the tab's model, and the tab stays in the main window.
- **Drag a tab out of the tab bar** (up or down past the bar) to pop it
  out where you let go. Dragging *along* the bar still reorders tabs.
- **A proper tool window:** it closes with the main window and minimises to
  a named chip. The old dialog had no owner.
- **Follows Row density and text size, the tabs' hidden columns and column
  widths.** The old dialog used a fixed 20 px row.
- **The tab's tag** is on the window's icon, title and header. It uses the
  tab's own name, not just its key.
- **Follow latest** (starts from the tab's scroll lock), and **Show in main
  window**, which brings the main window forward on that tab.
- **One per tab:** opening it again raises the one that's open.
- **Reopened on start:** pop-outs still open at shutdown come back on the
  next start, in the same place, as soon as that loco's tab appears. One
  you close yourself is forgotten.

Removed: `detachconsoledialog.{h,cpp,ui}` and a stale generated
`tests/ui_detachconsoledialog.h`.

### Tests

- **`textzoom` (17):**
  - app font and explicit sizes scale in proportion;
  - a bold-only font isn't scaled twice;
  - rows 30 → 38 at 125 %, and new tables match;
  - the monospace font scales;
  - no drift after 20 changes;
  - a size set in between becomes the new base;
  - snapping and the ends; 80 % survives a theme change;
  - the presentation boost isn't saved.
- **`presentationmode` (16):**
  - full screen with bars hidden; shortcuts lent and returned;
  - maximised comes back maximised;
  - two windows share one boost;
  - a window destroyed while presenting doesn't leave the boost behind;
  - Esc in a field is left to the field, Esc elsewhere leaves.
- **`tabtags` (23):** store, clear, trim and cap labels; 3:1 contrast and
  eight distinct colours in all seven themes.
- **`tabpopout` (9):**
  - a tool window, sharing the tab's model;
  - row density followed;
  - the label and colour reach the title and icon at once;
  - Show in main window, Follow latest, and closed-by-user.
- **`menuaudit`, through the real main window:**
  - a pop-out open at the last shutdown comes back on start;
  - the tag dot and "L1_V1 · Brake test" are on the tab, and the pop-out
    carries the same name and label;
  - a pop-out you close is forgotten;
  - Detach opens one copy, not two, and it is remembered;
  - Text size Larger/Smaller/Reset shortcuts, and F11, are application-wide.
- **Checked by eye:** the real main window with two tagged tabs at 100 %
  and 125 %, and a tagged pop-out.

### Found along the way

- The pop-out took its title from the dispatcher (the key "21_1") while the
  tab showed the workspace name "L1_V1". It now uses the tab's name.
- A widget's `destroyed()` fires before pointers to it are cleared, so
  presentation mode matches a destroyed window by address.

### verify.sh

Full run, **14/14 stages green, 0 failed**: validators 11/11, unit suite
131 suites / 3882 checks (+ `textzoom`, `presentationmode`, `tabtags`,
`tabpopout`), menu audit 91 ok (+14, through the real main window),
headless smoke alive. Built with Qt 5.15 only.

---

<a id="session-75"></a>
## Session 75 — Named chips for minimised windows; a clear flash-queue selector

### 1. Minimised tool windows: named chips in DLConsole's bottom bar

**What was wrong:** minimised tool windows (Live Loco Console, Firmware
Flasher, …) appeared as small icon-only stubs at the bottom-left of the
window, with no way to tell them apart. Windows draws those stubs for
windows owned by the main window, and DLConsole cannot put text in them.

**Session 74 fixed the wrong thing.** It gave each window a taskbar button;
what was wanted was named chips in DLConsole itself. That change is
**removed** (`taskbarwindows.*` deleted, the `user32` link lines removed).

**Now:** new `minimizeddock.{h,cpp}`, shown at the far left of the main
window's status bar.
- When a tool window is minimised, DLConsole hides it (so Windows draws no
  stub) and adds a **chip with the window's name**.
- **Click the chip** to bring the window back, maximised if it was
  maximised. **✕** closes the window.
- A window that refuses to close (the Flasher mid-flash asks first) keeps
  its chip.
- The chip follows a title change. It disappears if the window is reopened
  another way (e.g. from its menu item) or is destroyed.
- The dock takes no space until something is minimised.
- It is one app-wide filter, so every tool window is covered, including
  future ones. The main window, dialogs, message boxes and menus are
  untouched.

Found while testing, and handled: Qt sends stray Show/Hide events to a
*hidden* window whose state is changed. The first version read the stray
Show as "reopened" and dropped the chip. The dock now leaves a docked
window's state alone until restore, and only treats a Show as real when
the window is actually visible.

### 2. Flash queue: the selected card is obvious

**What was wrong:** every unselected card showed a solid black dot, so the
selected VCC looked no different from the rest.

**Cause:** the queue table has its own style sheet (`background:
transparent`), which made the delegate's palette background transparent.
The style's radio, and then my first redraw too, painted with that
"colour", which comes out black.

**Now the selector is drawn directly**, using the theme's real background
from the application palette:
- **Selected card:** a filled **green** circle with a **✓**, inside a light
  ring so it stands out even on the blue selected-row highlight.
- **Other cards:** an empty circle with a grey outline.
- The green is new `UiColor::selectedMark()`. It starts from a vivid green,
  because `ok()` is darker, tuned for green *text*. `withContrast()`
  adjusts it to at least 3:1 (WCAG's floor for graphical objects) on every
  theme's background, and `test_uicolors` checks that on all seven themes.

### Tests

- **New `minimizeddock` (18 checks):**
  - minimise gives a chip with the window's name and hides the window;
  - the chip follows a title change; clicking restores it;
  - a maximised window comes back maximised;
  - two windows give two chips, told apart;
  - a window reopened another way loses its chip;
  - ✕ closes the window, and a window that refuses keeps its chip;
  - dialogs and the main window are not docked.
- **`flasher`:** renders the real queue page and counts green pixels in
  each selector. The selected card must have them and every other card
  none. This would have caught the black dots.
- **`uicolors`:** the selected mark is ≥ 3:1 on the background in every
  theme.
- **Checked by eye:** the queue in Light and Nord with VCC selected and
  highlighted, as in the bug screenshot; and the main window with two
  minimised tool windows showing as "Live Loco Console" and "Firmware
  Flasher" chips.

### verify.sh

Full run, **14/14 stages green, 0 failed**: validators 11/11, unit suite
127 suites / 3817 checks, menu audit 77 ok (one fewer than session 74: the
taskbar-button check went with the taskbar change), headless smoke alive.
Built with Qt 5.15 only.

---

<a id="session-74"></a>
## Session 74 — More themes, and named minimised windows

### 1. Five new themes, every one held to the contrast floors

**View ▸ Theme** lists them all. The same list is in Settings. The choice
is saved as before.

| Theme | Kind | Body text | What it's for |
|---|---|---|---|
| Light (Ayu) | light | 8.1 : 1 | unchanged |
| **Sepia** | light | 11.5 : 1 | warm paper and brown ink; calm for long reading |
| **Sage** | light | 11.3 : 1 | soft green-grey; restful, no pure white |
| **High contrast** | light | 21 : 1 | black on white, **7 : 1 for every colour**; bright cabs and sunlight |
| Dark (Ayu) | dark | 10 : 1 | unchanged |
| **Nord** | dark | 10.4 : 1 | cool arctic blue-grey |
| **Mocha** | dark | 12.1 : 1 | Catppuccin Mocha: warm dark, pastel accents |

**How contrast is guaranteed, not eyeballed:**
- Each new theme has its own hand-tuned ok / warning / error / muted /
  accent colours (`uicolors.cpp`). Borrowing Ayu's would have failed on a
  sepia background.
- `tests/test_contrastaudit.cpp`, `test_uicolors.cpp` and the flasher's
  badge check now loop over **every** theme (`ThemeUtil::all()`), not just
  Light and Dark. For each theme they check:
  - every text-on-surface palette pair (4.5 : 1);
  - disabled and placeholder text (3 : 1);
  - every semantic colour on the window, base, alternate row and button
    (4.5 : 1);
  - body text ≥ 7 : 1, the row stripe, and the frame greys.
  - **High contrast is held to 7 : 1 (AAA)** for text and every semantic
    colour.
- The palettes were designed with a script running the same checks before
  any code was written. Nord's published red (#BF616A, 3.3 : 1 on a button)
  and one muted grey (4.46 : 1) were lifted until they passed.
- The contrast audit went from ~80 checks to 263, all passing.
- Plot series colours keep their hues but are moved lighter or darker until
  they clear 3 : 1 on the current background
  (`UiColor::withContrast`). The two Ayu themes were already above that, so
  they are unchanged.

**Also:**
- **Toggle Dark/Light** (now in View ▸ Theme) switches between your *last*
  light and last dark theme, so Sepia ⇄ Nord works; the first time it's
  Ayu Light ⇄ Ayu Dark.
- The selected tab in Sepia, Sage and High contrast is a tint of the
  theme's own colours. The Ayu formula gave Sepia a clashing cyan. Ayu Light
  is unchanged.
- Code that needs "is this dark" (the log colour rules' light/dark
  variants, the find tint) now asks `ThemeUtil::isDark()` instead of
  `== Theme::Dark`.
- An unknown theme key in `dlconsole.ini` falls back to Light.
- **Checked by eye:** the Live Loco Console rendered in all seven themes
  with real `@linfo` data and an active find.

### 2. Minimised windows now have a name

**Cause:** every tool window (Live Loco Console, Firmware Flasher, Loco
Configuration, Decode Workbench, …) is owned by the main window. On Windows
an owned window gets no taskbar button, so minimised it is parked as a
tiny title-less caption at the bottom-left of the desktop.

**Fix:** new `taskbarwindows.{h,cpp}`, installed once in `main.cpp`.
- An app-wide filter sets `WS_EX_APPWINDOW` on each tool window as it is
  shown. Each gets **its own taskbar button**, with its title and the
  DLConsole icon, and minimises to the taskbar.
- They stay owned by the main window: they still close with it and stay
  above it.
- Only real windows with a parent qualify. Dialogs, message boxes, menus
  and tooltips are untouched, and so is the main window.
- It is one filter rather than code in each window, so future tool windows
  are covered automatically.
- `win32: LIBS += -luser32` added to the three `.pro` files.

**Could not be run here (Linux):**
- The Windows branch was **compiled and linked with MinGW** against the
  real Windows headers and user32, `-Wall -Wextra` clean.
- The decision logic ("which windows get a button") is unit-tested.
- On Windows, please check that minimising the Live Loco Console puts a
  button with its name on the taskbar.

### Files

- **New:** `taskbarwindows.{h,cpp}`, `tests/test_taskbarwindows.cpp`.
- **Changed:**
  - `theme.h` (five themes, `ThemeUtil::all/label/isDark`, palettes);
  - `uicolors.{h,cpp}` (per-theme semantic sets, `setActiveTheme`,
    `activeTheme`, `withContrast`, series contrast);
  - `uistyle.cpp` (selected-tab tint for the new light themes);
  - `logmodel.cpp` (`isDark`);
  - `mainwindow.{h,cpp}` (View ▸ Theme menu, toggle remembers the last of
    each kind);
  - `settingsdialog.cpp` (all themes);
  - `main.cpp`;
  - `tests/test_contrastaudit.cpp`, `test_uicolors.cpp`, `test_flasher.cpp`,
    `menuaudit_main.cpp`, `screenshot_main.cpp`;
  - the three `.pro` files.

### verify.sh

Full run on the final tree, **14/14 stages green, 0 failed**:
validators 11/11, unit suite 127 suites / 3799 checks (+ `taskbarwindows`,
and every colour suite now covering all seven themes), menu audit 78 ok
(View ▸ Theme lists every theme, exactly one ticked, toggle present; the
Live Loco Console gets a taskbar button), headless smoke alive. Built with
Qt 5.15 only; the Windows-only branch was compile-and-link checked with
MinGW.

---

<a id="session-73"></a>
## Session 73 — Find in the Live Loco Console

The Live Loco Console's packet tabs can now be searched. `linfo` alone is
~170 rows, and until now the only way to find one member was to scroll.

### How to use it

- **Open find:** **Find…** (top right), **Ctrl+F**, or **F3**. The bar
  appears under the tabs.
- **Type part of a field name.** Case doesn't matter, and `_` and space
  are the same, so `speed margin` finds `speed_margin_eb`. Every word must
  appear, so `margin eb` narrows it further.
- **Move between matches:** **Enter** or **F3** for next, **Shift+Enter**
  or **Shift+F3** for previous. It wraps at the ends. The count shows as
  "2 of 8".
- **Only matching rows** hides everything else.
- **Values too** also searches the Value column, e.g. to find which field
  holds `127.0.0.1`.
- **Not on this tab?** The bar names the tabs that do have it (e.g. "not
  here — linfo 3") and **Enter** switches there.
- **Close:** **Esc** or **✕**. Tints and hidden rows are cleared.

### Why it holds still on a live stream

The console rebuilds every table from scratch whenever frames arrive (it
refreshes every 250 ms), so a find tied to row numbers would flicker or
jump.
- `TableFindBar` holds the current match **by field name** and re-applies
  itself after each refresh.
- Names aren't unique: `speed_margin_warning` is in LOCO_INFO twice, at the
  top level and in `national_values`. So the match is tracked as (name,
  which occurrence). Following the second one stays on the second one.
- **A refresh never scrolls.** Only Next and Previous move the view, so you
  can read elsewhere in the table while a search is open.
- Matches use the same accent tint as the log find.

### Files

- **New:** `tablefindbar.{h,cpp}`, a reusable find for any Field | Value
  `QTableWidget`. The replay window's tables could use it with a few lines,
  if wanted.
- **New test:** `tests/test_tablefindbar.cpp`.
- **Changed:** `lococonsolewindow.{h,cpp}` (Find button, bar, shortcuts,
  reapply after refresh), `DLConsole.pro`, `tests/tests.pro`,
  `tests/menuaudit.pro`, `tests/menuaudit_main.cpp`.

### Tests

- **`tablefindbar` (36 checks):** the matching rule, on a table of all 171
  LINFO field names from the schema, rebuilt the way the console rebuilds
  it.
  - Count; first match on typing; Next, Previous and wrap.
  - A refresh with a row inserted above keeps the same field.
  - The duplicate `speed_margin_warning` stays on the second copy.
  - A refresh doesn't scroll, while Next does.
  - Only-matching survives refreshes.
  - Values too; the other-tab hint and switch.
  - Leaving a tab and closing leave the table clean.
- **`menuaudit`:** opening Monitor ▸ Live Loco Console gives a window with
  the find bar and Ctrl+F.
- **Checked by eye:** a render of the real console decoding a real `@linfo`
  frame (CRC ok) with "speed margin" found: 8 matches, 2 of 8 current.

### verify.sh

Full run, **14/14 stages green, 0 failed**: validators 11/11, unit suite
126 suites / 3441 checks, menu audit 73 ok, headless smoke alive. Built
with Qt 5.15 only.

---

<a id="session-72"></a>
## Session 72 — README for flashing and loco configuration

New `README_FLASHING_AND_CONFIG.md` at the project root: a plain-language
guide for operators and engineers.

- **Part 1, Firmware Flasher:** the one rule (press Flash, then
  power-cycle, one card per power cycle), step-by-step flashing, what each
  result means, a common-problems table, Engineer vs Operator mode, history
  and profiles.
- **Part 2, Loco Configuration:** editing and sending a loco's LOCO_INFO,
  what the bold/● marks mean, exporting and importing `loco_info.bin`, and
  undoing a change through History. It states clearly that the VCC does
  not reply.
- **Part 3, engineers:** adding a LOCO_INFO member in three steps, pointing
  to `lococonfig/ADDING_A_FIELD.md`.
- **Where DLConsole keeps its files:** profiles, configurations and the two
  history logs, all beside `dlconsole.ini`.

Every statement was checked against the code as of Session 71: menu names
and shortcuts, button labels, result words, the 5 s updater window and 60 s
default wait, the 800 KB limit, file-name rules, the mode table and file
names. No code changes.

### verify.sh

Full run, **14/14 stages green, 0 failed**: validators 11/11, unit suite 125 suites / 3405 checks, menu audit 70 ok, headless smoke alive.

---

<a id="session-71"></a>
## Session 71 — Adding a LOCO_INFO member: one line and one command

Before this session, adding a member to LOCO_INFO meant editing five places
in DLConsole: `kavach.xml`, `loco_fields.json`, `loco_defaults_v12.json`,
the golden fixture, and sizes hard-coded in the tests. Nothing checked that
the schema matched the C struct, so a field in the wrong place or of the
wrong width would only show up as garbled decodes.

Now it is:

1. add the member to the C struct and give it a value in the loco_config
   tool, as today;
2. add **one `<field>` line** to `<packet name="LINFO">` in
   `schema/kavach.xml`;
3. run `python3 lococonfig/sync_linfo.py path/to/loco_config_vNN.cpp`.

Step-by-step, with the type table: `lococonfig/ADDING_A_FIELD.md`.

### One line holds everything about a field

The editor's per-field data moved onto the field's own schema line, as
attributes neither decoder reads:
- `ui-group`: the editor group.
- `ui-note`: one line of help.
- `ui-format="ipv4"`: edit a `uint32_t` as a dotted address.
- `c-name`: the C member's name, only when it differs. Used for
  `use_init_state_machine_atf` and `braking_pressure_used`.
- `c-member`: on a section note, the nested struct's C member
  (`static_braking_config`, `national_values`).

The decoders already use `group` and `format` for other things, hence the
`ui-` names. The attributes are documented in the comment above the
packet. `lococonfig/loco_fields.json` is gone; groups are listed in struct
order.

### `lococonfig/sync_linfo.py`

It compiles your tool with a stub `winsock2.h`: `socket()` and `sendto()`
are replaced so **nothing is sent**, and the datagram is captured to a
file. A checker is compiled alongside it. Then:

- **Every schema field is checked against the C member** with `offsetof`
  and `sizeof`. It stops at the first disagreement and names it, e.g.
  "the C struct has 2 more byte(s) between brake_pressure_used and
  loco_info_crc: add the new member(s) there", "schema says u8, the C
  member is 2 bytes", or "schema field shunt_sped → C member does not
  exist".
- **The tool runs three times** (-O0 with two stack-junk patterns, and
  -O2). A byte that differs is uninitialised memory:
  - in a string after its NUL it is zeroed, with a warning;
  - anywhere else it is an error naming the member, e.g. "frame_cycle
    (byte 122)".
- **The tool's datagram is checked:** header 28 → 2, message 120, the
  length, the body against `loco_info.bin`, and `loco_info_crc`.
- **It writes the generated files:**
  - `lococonfig/loco_defaults.json`: the tool's values, which are the
    editor's defaults. Marked GENERATED, with the tool's name and SHA-256.
  - `tests/fixtures/loco_info_default.bin`: the golden body.
- `--check` does all of that and writes nothing.

**Run against your unmodified `loco_config_v12.cpp`:** all 171 fields
agree. It flags `train_desc` by itself (the session-70 bug), and produces
the same bytes as the hand-built session-70 fixture, CRC `0x39F59222`.
Each failure message above was confirmed by breaking a copy on purpose.

### Tests no longer hard-code the struct

`test_lococonfig.cpp` takes the size, the CRC offset and the CRC from the
layout and the fixture. Stale generated files fail with "run
sync_linfo.py". An import of a file with the wrong size is refused as
"written for a different LOCO_INFO version". The UI says "Default" instead
of "v12 default", and the Manage menu names the tool the defaults came
from.

### Proved end to end

In a throwaway copy of the tree:
- I added `uint16_t max_brake_cylinder_pressure = 380` to a v13 copy of
  the tool, then one schema line, then ran the script. Nothing else was
  edited.
- The script reported 172 members agreeing and a 372-byte body, and wrote
  the defaults (`source: loco_config_v13.cpp`, value 380) and the fixture.
- The whole unit suite then passed: 125 suites, 3405 checks, 0 failed.

That copy was discarded; this tree still describes v12.

### Files

- **Changed:** `schema/kavach.xml` (LINFO attributes and comment),
  `lococonfig/lococonfigcore.{h,cpp}`, `lococonfig/lococonfigwindow.{h,cpp}`,
  `lococonfig/lococonfigmodel.{h,cpp}`, `lococonfig/lococonfig.{pri,qrc}`,
  `tests/test_lococonfig.cpp`.
- **New:** `lococonfig/sync_linfo.py`, `lococonfig/ADDING_A_FIELD.md`.
- **Renamed/generated:** `lococonfig/loco_defaults.json` (was
  `loco_defaults_v12.json`) and `tests/fixtures/loco_info_default.bin`
  (was `loco_info_v12.bin`), both now written by the script.
- **Removed:** `lococonfig/loco_fields.json`.

### verify.sh

Full run, **14/14 stages green, 0 failed**:

- golden validators: 11/11 ok (the new schema attributes change no decode)
- unit suite: 125 suites, 3405 checks, 0 failed
- menu audit: 70 ok, 0 failed
- headless smoke: 500 datagrams, app alive at timeout

Built with Qt 5.15 only; the Qt 6.4 build was not run here. `sync_linfo.py`
needs python3 and g++ (or `--compiler clang++`); on Windows, MSYS2/MinGW
provides both.

---

<a id="session-70"></a>
## Session 70 — Tools ▸ Loco Configuration…

`loco_config_v12.cpp` as a DLConsole window. You edit a loco's `LOCO_INFO`,
send it to the VCC, and export `loco_info.bin`. The window sits beside the
Firmware Flasher, and like it is single-instance.

### Where it lives

- **Menu:** `Tools ▸ Loco Configuration…`, shortcut **Ctrl+Alt+L**.
- **Code:** new folder `lococonfig/`, pulled into the app, the tests and
  the menu audit through `lococonfig/lococonfig.pri`.
- **Data**, beside `dlconsole.ini`:
  - `loco_configs.json`: the saved configurations, written atomically.
  - `loco_config_history.jsonl`: every send, append-only, holding the exact
    bytes that went out.

### One layout, from the schema

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

### The window

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

### Found in v12: the CRC isn't reproducible

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

### Tests

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

### verify.sh

Full run on the final tree, **14/14 stages green, 0 failed**:

- golden validators: 11/11 ok (after the LINFO schema note)
- unit suite: 125 suites, 3404 checks, 0 failed (+`lococonfig`, +`lococonfigrun`)
- menu audit: 70 ok, 0 failed
- headless smoke: 500 datagrams, app alive at timeout

Built with Qt 5.15 only; the Qt 6.4 build was not run here. Light and dark
renders of the window were checked by eye.

---

<a id="session-69"></a>
## Session 69 — Firmware Flasher: one card per power cycle

Session 68 built the flasher as a multi-card batch. Reading the updater
source showed that can't work on real hardware, so the flasher now flashes
**one card per run**. Press Flash, then power-cycle the chassis while the
flasher waits for the updater. The engine and the wire protocol are still
unmodified.

### What the updater does (from `updater.zip`)

- **The updater only runs at power-on.** `main.c` gives it 5 s
  (`FW_WATCHDOG_DEFAULT_MS`) to hear a firmware packet, otherwise it calls
  `Boot_Switch()`.
- **Every card ends with `Boot_Switch()`,** after a VCC flash and after
  forwarding an IOA image over CAN. `Boot_Switch()` loads the application
  and jumps into it; it does not reset back into the updater. So after one
  card the VCC is running its application. A second card in the same batch
  would never be answered.
- **The board accepts at most 800 KB** (`MAX_IMAGE_SIZE`). A larger META is
  dropped without any reply.
- **COMPLETE means "the RAM copy passed CRC-32 + SHA-256".** It is sent
  before the VCC writes flash, and for an IOA card before the VCC forwards
  the image over CAN. Nothing further comes back to the PC.
- **Every META resets the session**, and replies go to whoever sent last.
  That answers the open "two PCs, one chassis" question: they break each
  other's transfers (ending in IMAGE_FAIL), but the whole-image CRC + SHA
  check stops a mixed image from ever reaching flash.

### Changes

**One card per run.**
- The queue still shows all four cards, each with its image, but exactly
  one is selected, radio-button style. Browsing for or dropping an image on
  a row selects that row. Loading a folder or a profile fills images
  without changing the selection.
- The button reads "Flash Input card" and so on.
- Removed as meaningless with one card:
  - dragging rows to reorder them;
  - "Stop batch on first failure";
  - "Stop after this card";
  - the VCC-before-IOA warning.
- The Summary's "Retry failed + remaining" is now **Try again**: back to
  the queue with the same card selected, and pre-flight runs again.
- Internally the run is still a one-entry `BatchPlan`, so the Summary and
  history code are unchanged and already tested. Batches could come back if
  the application ever gains a reset-into-updater command.

**Press Flash, then power-cycle.**
- The Flashing page shows a caution-coloured prompt with a countdown:
  "Power-cycle the chassis now — waiting for the updater · N s left".
- The first step of the phase bar reads "Waiting for updater" during this
  time.
- The wait comes from a new profile field, **Wait for updater after pressing
  Flash (s)**. Default 60, range 10–600, editable in both modes because it
  depends on how far the operator is from the power switch. It is stored as
  `updater_wait_s` in `flasher_profiles.json`.
- No engine change is needed. The wait is turned into the engine's existing
  `meta_handshake_tries`, as ceil(wait / `poll_give_up_ms`); 60 s is 24
  tries at 2.5 s.
- Abort while waiting asks "Stop waiting for the updater?"; nothing has
  been sent at that point. Abort during a transfer says the updater drops
  the partial image and the card keeps its current firmware.

**Pre-flight.**
- New blocking item: an image over 800 KB is refused, with its size and the
  limit.
- The last item is now the procedure itself ("After pressing Flash,
  power-cycle the chassis — the flasher waits up to N s") instead of "put
  the board in updater mode".
- When the board never answers, the failure text now explains the
  power-cycle procedure before pointing at cable, IP and port.

**Honest outcome wording.**
- COMPLETE is now shown as **Verified**, not "Accepted": in the table,
  Summary, history and CSV.
- The Summary explains what that proves for each card:
  - **VCC:** the image is verified and is being written to the spare flash
    bank, then booted.
  - **IOA cards:** the VCC verified the image and is forwarding it over
    CAN; the PC gets no confirmation from the card itself.
  - Both say to keep the chassis powered until the card is back up.

**Backed out: the live-capture pre-flight warning.**
- With press-then-power-cycle, the board is *expected* to be running its
  application when Flash is pressed, so the warning would fire on every
  run.
- `udpcommunication.{h,cpp}` are back to their Session 67 state. The
  `MainWindow` hook is gone; `mainwindow` now only adds the menu entry and
  the single-instance window.

### Tests

- `flasher` has 116 checks:
  - one selected card at a time, and loading an image doesn't select it;
  - the 800 KB limit, with exactly 800 KB still allowed;
  - two selected cards refused;
  - the updater wait's JSON round trip and clamping;
  - wait → handshake tries (60 s → 24, 11 s → 5);
  - one-card verdict wording for verified and failed;
  - "Verified" wording and the VCC vs IOA explanations.
- `flasherrun` (30 checks) now runs against a **fake board that behaves
  like the real updater**. It is silent until powered on, listens for 5 s,
  resets on every META, sends COMPLETE three times and then leaves for its
  application. Four runs:
  1. Flash pressed, board powered on 0.7 s later: the power-cycle prompt
     was showing while waiting and is gone after, the card is Verified, and
     the board is left in its application.
  2. The next card without a power cycle: never answered, Failed, the
     explanation says to power-cycle, and nothing reached the board.
  3. A card the board rejects: IMAGE_FAIL, and the board stays in the
     updater.
  4. Abort while waiting: Cancelled, with headline "Input card — aborted".
  All four runs are in history.

### Firmware issues seen in the updater (not flasher changes)

1. **`Transmit_AppImage_On_CAN()` retries a failed CAN send forever.** If
   the target IOA card is absent or the bus is faulted, the VCC never
   leaves the updater and never reaches its watchdog check or
   `Boot_Switch()`. It stays down until power-cycled.
2. **The CAN forward has no retransmission.** A lost frame leaves the IOA
   card incomplete; it boots its old image after the 60 s pending window,
   and nothing reports an error.
3. **A new image rolls back after 3 unconfirmed boots**
   (`BOOT_ATTEMPT_THRESHOLD`). "Verified" is therefore not the same as
   "running the new image"; the only end-to-end proof is the card coming
   back up on the new version.

### verify.sh

Full run, **14/14 stages green, 0 failed**:

- golden validators: 11/11 ok
- unit suite: 123 suites, 3324 checks, 0 failed
- menu audit: 65 ok, 0 failed
- headless smoke: 500 datagrams, app alive at timeout

Built with Qt 5.15 only; the Qt 6.4 build was not run here.

---

<a id="session-68"></a>
## Session 68 — Tools ▸ Firmware Flasher…

The Kavach flasher handoff pack, built into DLConsole: flash `.appimage`
builds onto a chassis (VCC + Input/Output/Analog IOA cards) over UDP from a
window of its own. All four pages of the design are in — Queue, Flashing,
Summary, History — plus the Profile editor.

The engine (`flash_engine.*`), the worker (`flashworker.*`) and
`board_sim.py` are the handoff's, **unmodified**; the wire protocol is not
touched. `blockmapwidget.*` is the handoff's with its colours mapped onto
the theme (below). Everything else is new.

### Where it lives

- `Tools ▸ Firmware Flasher…` — top level in Tools, after a separator, next
  to Session Key. **Ctrl+Alt+F** (see "Decisions to confirm").
- A `QMainWindow` like the Decode Workbench, but single-instance: reopening
  raises the existing window. Two windows flashing one chassis would
  interleave META/DATA on the wire.
- New folder `flasher/`, pulled into the app, the test binary and the menu
  audit through one `flasher/flasher.pri` so the three builds can't drift.
- Data beside `dlconsole.ini`: `flasher_profiles.json` (profiles, written
  with QSaveFile) and `flash_history.jsonl` (append-only JSON Lines; a torn
  last line after a power cut costs that line only). Engineer/Operator mode
  is `flasher/mode` in `dlconsole.ini`.

### The pages

**Queue.** Target (IP, port, adapter), delivery-route diagram (cards not in
the batch drawn dashed), pre-flight checklist, the queue table and the action
bar. All four card rows are always present; a card is left out by unticking
it. Rows drag to reorder; a file dropped on a row from Explorer becomes its
image; clicking the Image cell browses; ✕ clears. "Load images from folder…"
puts the newest `.appimage` whose name names exactly one card on that card.
Images are hashed on load (engine CRC-32 + SHA-256) and **re-hashed when the
file changes on disk** (QFileSystemWatcher, 400 ms debounce so a build still
writing isn't hashed half-done). The detail strip shows full path, SHA-256
and modified time for the selected row. Default order Input, Output, Analog,
VCC — the IOA images are relayed through the VCC, so it goes last.

Pre-flight, re-evaluated on every change and every 2 s while visible:
target IP valid; an adapter on the board's subnet; per ticked card — image
chosen, readable, accepted by `FlashEngine::validate()`, and its **name
matches its card** (`LKAVACH` = VCC, `Input`/`Output`/`Analog`); SHA-pin
mismatch; live capture arriving from the target IP; VCC queued before a
relayed IOA card; and always last, the updater-mode reminder. The Flash
button reads "Flash N cards" and is off until ready, with the first blocker
shown beside it.

**Flashing.** Batch column (dot + name + file + status, active row tinted;
Stop after this card / Abort now…), card header with delivery route and
elapsed clock, four-step phase bar (Session → Send + early repair → Repair ·
round N → Accepted, each with its time; the step a card failed in turns
red), the handoff's block map with legend, five stat cards (blocks, repair
round of max, resent, send rate vs target, RTT + poll timeout), and the
session log with Copy / Save .log. The app bar turns into the caution strip
("Flashing — keep the chassis powered" + target) and profile, mode and
settings lock for the run.

**Summary.** Verdict banner in the design's words ("All 3 cards updated",
"Batch stopped — 1 of 3 cards updated" + which card failed and which was
skipped), one panel per card from `FlashResult` (rounds, resent, bytes on
wire, throughput, settled rate, phase times; for a failure the explanation —
IMAGE_FAIL gets the handoff's "wrong build or wrong card type" text), Export
report (.txt), Retry failed + remaining, Open history, Back to queue. The
footer says which batch ID history holds it under, and says so in red if the
history write failed.

**History.** Search (file, chassis, operator, CRC, batch), card / result /
period filters, Export CSV (what the filters show), Refresh. Selecting a row
shows its full session log, SHA-256, phase timing and transfer figures.
Read-only by design. Every card of every batch is recorded, including the
ones that were **Not run** — that is part of what happened to the chassis.

**Profile** (⚙ in the app bar; "New profile…" in the profile menu). Name,
VCC IP, port, a default image per card, pin-by-SHA-256, and the tuning grid
mapped one-to-one onto `TransferTuning` (adaptive/fixed, start/max/min rate,
check-in interval, poll timeout min/max, give-up, META copies, max rounds,
IMAGE_FAIL restarts; block size shown locked). "Start from this bench's last
settled rate": the rate the pacer settled at on the last **accepted** card is
saved to the profile and used, clamped to [min, max], as the next start.
In Operator mode the tuning panel is visible but locked.

### Engineer / Operator

| | Engineer | Operator |
|---|---|---|
| Image name doesn't match its card | warning; Flash asks "flash anyway?" listing the mismatches | **blocks** |
| No adapter on the board's subnet | warning | **blocks** |
| Transfer tuning | editable | shown, locked |

### Changes outside `flasher/`

- `mainwindow.{h,cpp}` — the menu action, `QPointer<FlasherWindow>`, and the
  live-capture probe handed to the window.
- `udpcommunication.{h,cpp}` — `hasRecentTrafficFrom(ipv4, withinMs)`. The
  receiver now keeps each sender's last-seen time for datagrams addressed to
  this console, merged once per `drainSocket()` wake under a mutex (not per
  datagram). Used for the pre-flight warning "the board looks like it's
  running its application, not the updater" (5 s window). Nothing else
  reads it.
- `tests/test_contrastaudit.cpp` — now also scans `flasher/`.
- `tests/menuaudit_main.cpp` — Firmware Flasher is top level in Tools, on
  Ctrl+Alt+F, not in Transmit; Active Fault Panel keeps Ctrl+Shift+F;
  triggering it twice opens one window.
- `DLConsole.pro`, `tests/tests.pro`, `tests/menuaudit.pro` — include
  `flasher/flasher.pri`.

### Decisions to confirm

1. **Shortcut Ctrl+Alt+F, not Ctrl+Shift+F.** Ctrl+Shift+F is Monitor ▸
   Active Fault Panel; on an ambiguous shortcut Qt fires neither action.
   One line in `mainwindow.cpp` if you want something else.
2. **Top level in Tools**, as asked, rather than in Transmit.
3. **Colours.** DLConsole's contrast audit fails on colour literals outside
   uicolors/theme/uistyle, so the handoff's hex tokens are mapped to
   meanings in `flasherstyle.*`: primary/held → accent, accepted → ok,
   active/repair/attention → warning, danger → error, the flashing app bar →
   the caution-banner colours Packet Maker uses. The block map uses the same
   calls (the only change to a handoff Qt file). Follows the theme toggle;
   badges are checked ≥ 4.5:1 in both themes.
4. **"Skip cards that already hold this image" checkbox dropped.** The
   engine always does this at the handshake (the board answers COMPLETE and
   nothing is sent) and can't be told not to without a protocol change, so
   an untickable-in-effect checkbox was replaced by a note. Such cards show
   as **Already held** (recognised from the engine's log line), count as a
   success, and are recorded in history.
5. **"Send from adapter" is advisory.** The engine's socket is unbound, so
   the OS routing table picks the interface; the picker (pre-selected to the
   adapter on the board's subnet) feeds the subnet pre-flight check and the
   tooltip says so. Actually binding needs a small **host-side** engine
   change (bind before sendto — no wire change). Not done; say if you want it.
6. **VCC-before-IOA pre-flight warning** (non-blocking) — a small addition
   not in the design: reflashing the VCC may reboot it mid-batch, and it is
   the relay for the other three. Tied to the firmware open question below.
7. **Retry failed + remaining** goes back to the queue with those cards
   ticked (pre-flight runs again) instead of starting immediately.

### Engine behaviours worked around (engine unmodified)

- `FlashResult::image_crc` is never set by the engine; the PC-computed CRC
  of the loaded image is used everywhere (table, summary, history, report).
- `FlashEngine::run()` clears its cancel flag when it starts, so an Abort in
  the gap between queueing the card and `run()` starting would be lost. The
  window repeats `requestCancel()` on every progress report while an abort
  is pending.
- The engine enters `Phase::Done` on failure as well as success; the phase
  bar keeps the last working phase so it can mark where a card failed.

### Firmware questions from the handoff — still open

Whether flashing the VCC reboots it mid-batch (and whether the relay then
needs time), how a board is put into updater mode from the field, and the
behaviour when two PCs flash one chassis. The UI doesn't depend on the
answers; decision 6 is the only place that anticipates one.

### Tests

- New suite **`flasher`** (107 checks): card names and the name check
  (every mix-up case, case-insensitivity, ambiguous names), image loading
  (engine CRC, SHA-256 agrees with Qt's, block counting, missing/empty
  files), profile JSON round trip and hand-edit tolerance, ProfileStore
  (first run, save/load, rename keeps active, can't delete the last, broken
  file reported), `effectiveTuning` clamping, every BatchPlan rule (stop on
  first failure, carry on, stop after current, abort, abort that loses the
  race to completion, already held), verdict wording, pre-flight in both
  modes, subnet helpers, history (append, torn line, filters, CSV quoting,
  the dialog reading the file), badge contrast in both themes, the queue
  model, and the Queue page headless.
- New suite **`flasherrun`** (25 checks): the real `FlasherWindow`, worker
  thread and engine against an in-process fake updater on 127.0.0.1
  (C++, so the gate needs no Python for it). Three batches: all three cards
  accepted in queue order with history and the settled rate written; the
  VCC re-flashed and reported **Already held**; the Output card rejected with
  IMAGE_FAIL, the batch stopping, the VCC never sent anything and recorded as
  Not run, and the Summary headline matching the design word for word.
- `contrastaudit` extended to `flasher/`; `menuaudit` extended as above.
- App builds with zero warnings (Qt 5.15). No ternaries in the new code.

### verify.sh

Full run, **14/14 stages green, 0 failed**:

- golden validators: 11/11 ok
- unit suite: 123 suites, 3310 checks, 0 failed (was 121 / 3177; +`flasher`,
  +`flasherrun`, +flasher checks in `contrastaudit`)
- menu audit: 65 ok, 0 failed (includes the new Firmware Flasher checks)
- headless smoke: 500 datagrams, app alive at timeout

---

<a id="session-67"></a>
## Session 67 — @speed, @analog_top, @analog_bottom

Three new capture types, recorded now so the mission driver can replay them
to a loco on the bench later.

### @speed — STRUCT_SENSOR_SPEED_DATA, 14 B, packed LE

```c
typedef struct PACK {
    uint8_t  speed_sensor_reader;      // 1 or 2
    uint8_t  sensor_dir;
    uint32_t pulse_from_tachometer1;
    uint32_t pulse_from_tachometer2;
    uint32_t crc;                      // LE
} STRUCT_SENSOR_SPEED_DATA;
```

- Schema `<packet name="SPEED" match="captype==speed">`; rows for all four
  fields plus the CRC verdict.
- **CRC ASSUMED:** the firmware JAMCRC (init 0, reflected) over bytes 0..9,
  stored LE at [10:14] — the convention @ccsys/@dlsys/@linfo already use and
  `validate_ccdl.py` confirms for those. No live @speed frame has been seen.
  If every real frame reads FAIL, the algorithm or span differs; send one
  line and it is settled.
- Only a 14-byte frame is CRC-checked; any other length is shown as
  unchecked, not failed against a guessed span.
- `sensor_dir` values and whether the pulse counts are cumulative or
  per-cycle are not known yet, so both are shown raw.

### @analog_top / @analog_bottom — STRUCT_ANALOG_SENSOR_DATA, 24 B, packed LE

```c
typedef struct PACK {
    float channel1_data; ... float channel6_data;
} STRUCT_ANALOG_SENSOR_DATA;
```

- One schema packet answers to both tags
  (`match="captype==analog_top,analog_bottom"`); they stay distinct capture
  types everywhere else, so top and bottom filter and chart separately.
- No CRC, so none is claimed (`crcChecked` stays false).
- Channels shown `%g` — identical on the C++ and Python sides, including
  `nan` and `inf`. Which pressure each channel carries, and its unit, are
  not known yet, so the channels have no names or units.

### Where the new types appear

Capture-type routing and labels; direction (all three are sensed inputs,
RX); the replay window, loco console and link overview; Frame Diff and the
Decode Workbench token lists.

### Tests

New suite `speedanalog`, 37 checks, all on frames built from the C structs
(no real capture exists yet):
- token routing and direction for all three;
- @speed: each field read from its struct offset, with values whose bytes
  all differ so a byte-order or offset slip changes the number; CRC passes,
  and fails when any of six positions (either end of each field and of the
  CRC) is corrupted; a 13-byte frame is left unchecked;
- @analog_*: 24 bytes, no CRC claimed; 4.75, −0.5, 0, 12345.678, NaN and
  +Inf each read back from the right offset and rendered as expected.

`engine.py` decodes the same frames to the same rows (checked by hand this
session; it goes into a golden validator once real captures exist).

Note: this session's work began in an earlier attempt that was cut off
before it reported. That attempt had the analog struct as six uint32
channels plus a CRC (28 B); it was reworked to the struct above before
anything was built or tested.

### Verified

- `./verify.sh`: **14 / 14 stages ok, exit 0** — unit suite 121 suites /
  3177 checks / 0 failed; menu audit 59 / 0; all 11 golden validators;
  headless smoke alive at timeout.
- Live check: the real app, offscreen, took 1202 synthetic datagrams
  (@speed from both readers, @analog_top, @analog_bottom, plus a short
  @speed and a short @analog_top) and was alive at timeout.

### Needed from the first real captures

1. One real `@speed` line — confirms or refutes the assumed CRC.
2. What `sensor_dir` values mean, and whether the tachometer counts are
   cumulative or per cycle (this decides how the mission driver replays them).
3. The channel-to-pressure mapping and units for the analog channels.

---

<a id="session-66"></a>
## Session 66 — SLRP geometry settled; gate fully green

Confirmed (Abhinav): the start signal sits at LAST_REF_RFID's absolute
location plus DIST_PKT_START, the signed distance counted along the direction
of travel — `refAbs + travel · dps` — and every packet distance, the tag list
included, is counted from the start signal the same way.

That is what `engine.py` and `schemadecoder.cpp` already did. The oracle
(`kschema_oracle.decodeSlrp`) was the stale one on both counts:

| | before | now |
|---|---|---|
| start signal | `ref − dps` (Nominal), `ref + dps` (Reverse) | `ref + travel · dps` |
| tag list from | `refAbs` | the start signal (`blockAbs`) |

No decoder output changes; only the oracle.

Scored against the engine on the 18 mode-B frames that carry absolute
locations (16 with DIST_PKT_START ≠ 0: 12 Nominal, 4 Reverse): the old
oracle differed on all 16; with the confirmed model, 0.

### Verified

`./verify.sh` — **14 / 14 stages ok, exit 0**: all 11 golden validators
(`validate_slrp` 6680/6680 across both modes), unit suite 120 suites /
3140 checks / 0 failed, menu audit 59 / 0, headless smoke alive at timeout.

---

<a id="session-65"></a>
## Session 65 — Gate repair: the Python half had drifted, and nothing saw it

Patch 64 shipped with the unit suite one check red (`contrastaudit`) and the
menu audit one check red (Ctrl+Shift+B). Running the Python golden validators
— which were **not part of the gate** — found four of eleven red:

| validator            | before          | cause                                                    |
|----------------------|-----------------|----------------------------------------------------------|
| `validate_arp_lsrp`  | 0 / 12460       | engine.py: exact-string `match=`, no `&` mask; oracle stale |
| `validate_dmi`       | 0 / 10658       | oracle predates `num coaches`; reads CRC one byte early   |
| `validate_slrp`      | 3324 / 3340 (B) | oracle and engine disagree on abs-location origin (open)  |
| `validate_uba`       | 0 frames        | hardcoded path to a log that was never committed          |

In each of the first three the schema and C++ had moved on purpose and the
Python side did not follow. This patch repairs that and makes the gate one
command, so it cannot happen quietly again.

### `verify.sh` — the whole gate

```
./verify.sh              validators + dltests + menu audit + headless smoke
./verify.sh --py         validators only (seconds)
./verify.sh --no-smoke
```

Exit code = number of failed stages. Builds go to `build-verify/`, nothing
is written into the tree. The smoke stage is the one sessions 54–64 ran by
hand, scripted: the real app offscreen, 500 real capture lines over UDP
(LSRP, DMI, SLRP, ARP) addressed to console id 101, pass = still alive when
the 14 s timeout fires.

### engine.py (the Python reference) — three fixes

1. **`match=` tokens.** `captype==arp,arprecv` (session 43) was compared as one
   string, so ARP never matched anything. Now `captype_tokens()`, same as C++
   `captypeTokens()`.
2. **`&` mask conditions.** `cond_ok("FRAME_NUM & 7 == 1")` looked up a field
   literally named `"FRAME_NUM & 7"`, got 0, and returned false — so all four
   LSRP health groups were skipped and `FRAME_NUM & 1 == 0` held for every
   frame. The Python reference reported *every* LSRP health word as stale.
3. **`<flags order="lsb" expand="all">` were ignored.** Found while fixing 2.
   Without `order="lsb"` the bits are named in read order, so each fault is
   reported as its mirror-image neighbour — B22 SPEED_SENSOR2_FAULT reads as
   B19 SESSION_KEY_MISMATCH. The real captures carry set bits (B22 × 366,
   B16 × 301, B17 × 117, …), so this was live, not theoretical. Now mirrors
   the C++ walker, including the per-bit `1 (FAULT)` / `0 (ok)` rows.

### Oracles brought up to the schema

- **arp_lsrp_oracle.decodeLSRP** — health decoded by group from `FRAME_NUM & 7`,
  LSB-numbered, with its own name table; even frames reported raw as stale.
  Written independently of the XML, so it still checks the engine.
- **dmi_oracle** — reads `num coaches` and takes the CRC over `[3:110]` from
  `[110:114]`. The CRC is the proof: with the byte, every captured frame
  passes; without it, every frame fails.

Result: arp **9207/9207**, lsrp **3253/3253**, dmi **10658/10658**.

### DMI CRC was not being checked in the app at all

The hand decoder only accepted the 115-byte DMI frame. Every one of the 10658
captured frames is 116 bytes, so `crcChecked` stayed false — no FAIL, just no
check, for as long as the emitter has sent `num coaches`. Both lengths are now
accepted, with the CRC word located by length. `crcheader` gains six checks:
the real 116 B frame passes; corrupting the num-coaches byte fails (it is
inside the span); a body corruption fails; a rebuilt 115 B frame still passes
and still fails when corrupted.

### Conditions: refuse, don't guess

One literal parser for every path, C++ and Python alike: decimal or `0x` hex,
optionally negative, **never octal** (`015` is fifteen). Before this, the mask
path took hex and the plain path read `0x0F` as 0.

`Decoder::load()` now runs `validateConditions()` over every `when=` and
`test=` and refuses the schema if one does not parse or names a field no
packet or struct declares. A refused schema leaves the decoder **empty**, the
same guarantee a malformed one already had. `engine.Schema` does the same and
raises. `RejectRules::load()` checks its `when=` guards with the same
`checkCondition()`.

The run-time rule is unchanged on purpose: a declared field the frame does not
carry reads as 0. Sub-packet fields are absent from most frames and the
reject rules depend on that.

All 20 conditions in `kavach.xml` and the one in `rejectrules.xml` pass.

`schemareload` +21 checks (refusal, empty-after-refusal, recovery, grammar,
hex, no-octal, absent-reads-0); `rejectrules` +2.

### Braking panel colours → `UiColor::series()`

The six-colour cycle in `brakingcurveplot.cpp` measured **2.12–3.09 : 1** on
Ayu Light's `#FCFCFC` — five of six under the 3 : 1 floor for graphical
objects. `UiColor::series(i)` keeps the hues and holds a light set lowered to
≥ 3.2 : 1; the dark set is the original cycle (≥ 6.1 : 1 on `#0B0E14`).
`uicolors` checks every entry against Base in both themes. Clears
`contrastaudit`.

### Ctrl+Shift+B → Braking Curves moves to **Ctrl+Shift+U**

Ctrl+Shift+B has meant *Selected row → Decode Workbench* since session 23, in
the main, compare and session windows. Session 57 also gave it to Braking
Curves, and Qt fires neither action on an ambiguous shortcut. U for the
`@uba` stream. **This was a call made here, not by you; revert is one line
(`mainwindow.cpp`, `actBraking`).**

### validate_uba

Looks for real `@uba` captures in `replay/*.cap|*.log` first. With none —
the case today — it runs on `schema/fixtures/uba_synthetic.log` (20 frames,
from `make_uba_fixture.py`, deterministic) and **says so in its PASS line**.
On synthetic frames the layout check is real (engine vs `struct.unpack`); the
curve invariants hold by construction and prove nothing about firmware.
Mutation-checked: a corrupted fixture fails 20/20 on invariants.

### Warnings: 21 → 0

`LogQuery::fieldName` handles `Field::Decoded` (describe() printed an empty
name for `field:` terms); fifteen dead `rfid*` coded-value maps plus
`eventDef` and `beUInt` removed from `capturedecoder.cpp` (RFID decoding has
lived in the schema since it moved); unused `mid` in `uistyle.cpp`; the
misleading indentation went with the condition rewrite.

### Verified

- Unit suite: **120 suites, 3140 checks, 0 failed** (was 3095 / 1 failed).
- Menu audit: **59 ok, 0 failed** (was 1 failed).
- Headless smoke: 500 datagrams incl. 100 DMI through the new CRC path;
  alive at timeout (124), no crash.
- Validators: **10 of 11 green**. App and tests build with 0 warnings.
- `./verify.sh` end to end: **13 of 14 stages ok, exit 1** — the one failure
  is `validate_slrp.py`, below. Expect exit 1 until that is settled.

### Open — one red, deliberately

**`validate_slrp` mode B, 16 frames**, all with `DIST_PKT_START ≠ 0`:

| | start-signal origin | tag list measured from |
|---|---|---|
| `kschema_oracle` | `refAbs − dps` (Nominal) | `refAbs` |
| `engine.py`, `schemadecoder.cpp` | `refAbs + travel · dps` | `blockAbs` |

The engine and C++ agree with each other and their comments describe a
deliberate choice; the oracle is probably just stale. But this is the one
question the oracle exists to settle, so it is left red until the sign is
confirmed against Annex-C rather than "fixed" by making the oracle agree.

Also outstanding, not in this patch's scope: no C++ suite for the braking
code (`brakingcurves`, `brakingcurveplot`, `brakingpanel`); a real `@uba`
capture in `replay/`; one pre-existing test-build warning (`test_quota.cpp:47`,
unused `w`).

---

<a id="session-64"></a>
## Session 64 — Extra header fields in the Packet Maker

Operator-chosen fields can now be placed **after the 8-byte message header and
before the packet** — a `uint16_t station_id`, a `uint32_t loco_id`, or any
other value the receiver expects in its envelope. Tick the row and it goes out;
untick it and the datagram is exactly what it was before.

```
[ 8-byte header ][ extra 0 ][ extra 1 ] ... [ packet (body + MAC + CRC) ]
```

### The panel

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

### Wire rules

- **`message_length` counts the extras.** That is how the header describes its
  own size (`message_length − PKT_LENGTH`); telling a receiver the header is 8
  bytes when it is 10 would make it read the station id as packet. Ticking a
  uint16 `station_id` on arp therefore produces the 10-byte form a loco's own
  radio emits.
- **Not covered by CRC or MAC** — envelope only, same as the header.
- **Unticked rows are skipped**, never zero-filled.

### Refuse, don't guess

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

### Preview

Shows the extra bytes on their own line, what each decodes as, and the total
envelope size. The "RECEIVED form, 8-byte header" note for arp/lsrp is shown
only when no extras are enabled — with extras the operator has chosen another
shape and the note would be wrong.

### Presets and the sequence runner

Presets save the rows under `msg_extras` (name, bytes, value, endian, enabled).
A preset written before this session has no such key and loads with the
default rows unticked — the same bytes it always produced.

The sequence runner applies a preset's extras too (one format, one behaviour),
and **fails the step** rather than sending if they do not validate.

### Code

- `messageheader.{h,cpp}`: `Extra`, `defaultExtras`, `parseExtraValue`,
  `validateExtras`, `encodeExtras`, `extrasSize`, `buildWithExtras`,
  `describeExtras`, JSON helpers. Pure functions; the dialog holds no encoding
  logic of its own. `build()` is unchanged.
- `packetmakerdialog.{h,cpp}`: panel, read/write, validation at Build and at
  send, preview, preset hooks, lock during runs.
- `packetpreset.{h,cpp}`: `extras` field.
- `packetsequencedialog.cpp`: uses `buildWithExtras`; refuses invalid extras.

### Tests

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

### Harness fix

`tests/menuaudit.pro` was missing `brakingcurves`, `brakingcurveplot` and
`brakingpanel` (added to the app in sessions 62–63) and did not link, so the
menu audit had not actually been running since then. Added.

### Verified

- Unit suite: **121 suites, 3095 checks, 1 failed** — see pre-existing below.
- Menu audit: builds and runs again; **1 failure**, pre-existing, see below.
  Packet Maker reachable.
- Headless smoke: app built from `DLConsole.pro`, run offscreen for 12 s under
  200 `@lsrp` datagrams on 50002; no crash, clean timeout exit.
- Panel rendered in Ayu Light and Ayu Dark; Send uses real checkboxes because an
  unchecked item indicator is invisible in the dark theme.

### Pre-existing failures (not from this patch, not fixed here)

1. **contrastaudit** — six `QColor` literals in `brakingcurveplot.cpp:21–26`
   (the segment colour cycle), outside `uicolors`/`theme`/`uistyle`.
2. **Menu audit: Ctrl+Shift+B is bound twice** — `Tools > Monitor > Braking
   Curves…` (`mainwindow.cpp:446`) and `Tools > Inspect > Selected row → Decode
   Workbench`. An ambiguous shortcut in Qt fires neither action. Hidden until
   now because the audit did not link.

Both are small; left for a decision rather than folded into this patch.

---

<a id="session-63"></a>
## Session 63 — Many targets per cycle, and the envelope across them

Every 10 ms the loco recalculates **every target ahead of it** — PSR, EOA, SVL,
TSR … — and builds both curves for each. So one cycle produces several `@uba`
frames, not one, and a single frame is one target out of several: meaningless
on its own. The panel now works in **cycles**.

### Grouping — and its honest limit

The frame carries **no target index and no cycle counter**, so grouping is a
heuristic: a new cycle starts when the inter-frame gap exceeds 5 ms, or when a
`(target_type, location)` pair already in the current cycle appears again.

That is sound for well-formed data and will mis-group if two distinct targets
share both a type and a location, or if a cycle straddles the gap threshold.
Every `Cycle` carries `heuristic = true`, and the summary says so where the
derived numbers are read:

> ⚠ inferred from timing and target identity — the frame carries no target
> index or cycle counter

**Two bytes in the emitter would remove the guess entirely**: a target index and
a target count per cycle. That is the single highest-value change available to
this panel right now, above anything further on the DLConsole side.

### Most-restrictive envelope

`Cycle::mostRestrictive(location)` returns the lowest allowed speed any target
permits there, **and which target imposed it** — because "why am I being braked
here" is answered by the target, not by the number. It drives the hover
readout, the search readout, and a heavy line on the chart.

Drawn **under** the per-target curves, not over them: an envelope painted on
top hides exactly what the operator is looking for. Gaps are left as gaps —
where no target covers a location the path breaks rather than bridging.

### Chart

Colour now separates **targets** when a cycle has more than one (which target
restricts you is the question), and falls back to separating deceleration steps
when there is only one. Line style still separates EBD from SBD. One dashed
marker per target, with the legend naming them rather than stacking labels that
would overprint when targets are metres apart.

Segment table gains a `target` column and lists every target × curve × segment
in the cycle.

### Verified

Synthetic three-target cycles (PSR @5200, EOA @6248, SVL @6900) at the real
10 ms cadence:

```
frames=15 -> cycles=5,  3 targets each
envelope:  5000 m ->  68.31 km/h  imposed by PSR
           5190 m ->  15.27 km/h  imposed by PSR
           6200 m ->  35.27 km/h  imposed by EOA
           6800 m ->  50.91 km/h  imposed by SVL
           4000 m -> (no target covers this)
time-gap split (50 ms between frames) -> 2 cycles, correct
```

Real capture still decodes as before: 4694 frames, one target, one cycle each.
Build clean; `validate_uba.py` 4694/4694.

---

<a id="session-62"></a>
## Session 62 — Search bar: by time, by absolute location

A `Find:` row above the change strip, with a mode selector.

### Time mode

Accepts `12:16:04`, `12:16:04.250`, `12:16`, or a full ISO timestamp. A bare
time is resolved against the day the capture was taken. Jumps to the frame in
force at that instant (`indexAtOrBefore` — the frame **at or before** the time,
which is the one that was actually in effect).

Matched against **host receive time**, not the loco RTC. The RTC in the capture
line is 1 s resolution, which cannot separate frames arriving every 10 ms — at
100 Hz a whole second is 100 frames, so an RTC match would be a 100-way tie.

Verified against the capture at its real 10 ms cadence:

```
  before first   -> index    -1  (none)
  exact first    -> index     0  (t=0 ms)
  +1.005 s       -> index   100  (t=1000 ms)
  +30 s          -> index  3000  (t=30000 ms)
  after last     -> index  4693  (t=46930 ms)
```

### Location mode

Accepts `6248`, `6248.5`, `6248 m`. Finds frames whose **target** is within a
metre of that chainage. If nothing is that close it falls back to the single
nearest and says `nearest target N m away`, rather than reporting "no match"
for a location that merely sits between targets.

It also does the thing the operator usually wants next: the searched location
is **marked on the plot** with the allowed speed there, read through the real
curve equation. So a location search answers *what did the curve permit at this
chainage* as well as *which frame*. Off the curve it says `(off curve)` rather
than showing zero, and the axes expand to include the queried location so a
search never silently looks like it did nothing.

### Stepping matches

Enter or `Find / Next` advances through the match set, wrapping, with
`match 3 of 12   12:16:04.250` beside the box. Editing the query invalidates
the set so the next Enter starts fresh rather than stepping stale hits.
Searching releases `Follow live`, for the same reason dragging does.

### Fixed while testing

The query label and the loco label shared a baseline and overprinted whenever
the two markers were near each other (visible with a query at 3000 m and the
loco at 4200 m). The query label now sits one row above.

Build clean; `validate_uba.py` 4694/4694.

---

<a id="session-61"></a>
## Session 61 — The single logged curve is NOT necessarily EBD

### The mislabel this fixes

Session 58 named curve index 0 "EBD" unconditionally. For a single-curve frame
that is a guess dressed as a fact, and `uniform_braking.c:159-167` shows why it
is the wrong guess to bake in:

```c
CURVE mrdt_curve;
if (brk_type == UBA_EB)       mrdt_curve = mrdt_target.curves.curves_for_target[UBA_EBD];
else if (brk_type == UBA_FSB) mrdt_curve = mrdt_target.curves.curves_for_target[UBA_SBD];
```

The firmware picks one of the two by `brk_type`, and `brk_type` is not in the
frame. So a 452 B capture holds EBD *or* SBD, and labelling it EBD inverts the
meaning of the plot whenever the loco is in full-service braking.

`Layout::curveNamesKnown` now gates this. A frame that carries
`curves_for_target[]` itself (1073/1076 B) has real CURVE_TYPE indices and its
curves are named EBD and SBD. A single-curve frame shows **"curve"**, and the
layout string says so outright: *"EBD or SBD, selected by brk_type, not stated
in the frame"*.

### Deceleration signature

Since the frame will not say which brake built the curve, the summary now
reports the only evidence it does carry. `net_accln = brake decel ∓ gradient ×
9.81` (`gradient_manager.c`), so the spread across segments is gradient, not a
different brake, and the largest value is the closest thing to the unloaded
brake rate:

```
curve deceleration    0.9376 … 1.0000 m/s²   (spread = gradient 6.36 ‰)
```

For this capture that reads as base **1.0 m/s²** with a ~6.4 ‰ (1 in 157)
gradient on the outer segments. Against the reference table in
`braking_params_manager.c` — EB 1.0 flat, FSB 0.9 / 0.6 by speed range — that
points to **EBD**. Evidence, not proof: those table values are commented out in
the source, so runtime config may differ.

### Note on multiple targets

Targets are recalculated every 10 ms and each gets both curves, but `@uba`
carries `mrdt_target` — the most restrictive displayed target — not the whole
target set. That is consistent with the capture: one distinct payload across
4694 frames means the MRDT never changed, not that only one target existed.
If per-target curves are wanted in the panel, the emitter has to send them;
nothing in the current frame distinguishes one target from another.

Build clean; `validate_uba.py` 4694/4694.

---

<a id="session-60"></a>
## Session 60 — 10 ms cycle: rate, gaps, and defaults that were wrong

The loco builds every target on a 10 ms cycle, so `@uba` can arrive at 100
frames/s. Two panel defaults were sized for a slow stream and were wrong.

### What the capture actually shows

Sequence numbers are a **global** capture counter across all message types and
are perfectly monotonic — 2012→8206 over 6195 lines, **zero gaps**. Nothing was
dropped by the logger. (An earlier reading of the uba-only sequence span as
"missing frames" was wrong; the span is just other message types interleaved.)

Emission is not a steady 100 Hz. Over 4694 frames and 67 active seconds:

```
gap with no @uba at all:  12:16:10 .. 12:16:45   (36 s)
frames/s clusters:        ~35-53   |   ~66-79   |   ~95-112
mean while streaming:     70.1 frames/s
```

Three tiers rather than a flat 100 Hz, plus a 36-second hole in the middle of a
102-second capture. Since the global sequence has no gaps, the logger did not
shed these — either the capture emitter declines to emit on some cycles, or
target-building itself stops. Worth a look on your side; the panel now surfaces
it rather than hiding it.

### Fixes

**Ghosts are now spaced by time, not frame index.** The old default drew the 6
frames immediately before the current one — at 100 Hz that is **60 ms** of
history, six curves drawn exactly on top of each other. Ghosts now spread over
a configurable span (`Ghosts: 6 over 5 s`), walking backwards one frame per
time step. This is the difference between the feature working and the feature
appearing to do nothing.

**Snapshot cap raised 20 000 → 200 000.** At 100 Hz the old cap was 3.3 minutes
and would have silently truncated any real run. Now ~33 minutes. The truncation
warning is unchanged.

**Status line reports rate and gaps**: frames/s while streaming, and
`⚠ longest gap N s at HH:MM:SS` whenever the stream stops for a second or more.
A curve stream that stalls for 36 seconds is a finding, and it is invisible if
the panel only ever prints a frame count.

Build clean; `validate_uba.py` 4694/4694.

---

<a id="session-59"></a>
## Session 59 — Navigating 4694 curves

### The finding that drove this

All 4694 `@uba` frames in `81_2_15092026_121737.log` are **byte-identical** —
one distinct payload, FNV-1a fingerprinted. The curve never changes for the
whole 19-minute capture. Scrubbing it frame by frame shows a still picture,
and nothing in the panel said so.

### What was added

**Change index.** `Braking::significantlyDifferent(a, b, locTol=0.5 m,
speedTol=0.1 m/s)` asks the operator's question — did the target move or change
type, did a segment appear or disappear, did a boundary shift — rather than
comparing fingerprints. On a moving train every frame differs in the last
decimal, so fingerprint equality would flag all 4694 as changes and make
"jump to next change" useless. The exact fingerprint is kept separately, for
counting distinct frames.

**Controls.** `|◀` / `▶|` step to the previous/next *change*, skipping runs of
identical frames. `◀` / `▶` still step one frame. A **Changes only** checkbox
remaps the scrubber to address only the frames where the curve changed — with
this capture it collapses to a single position, which is the honest picture.

**Change strip** above the scrubber: a tick per change across the session, plus
a cursor. Click to jump. One tick at the far left says "nothing happens here"
faster than any amount of dragging.

**Status line** leads with it:
`4694 frames · the curve NEVER CHANGES in this capture (4694 identical frames)`
and otherwise `N changes · M distinct frames`.

Live capture extends the index incrementally rather than rebuilding it.

### Verified

```
REAL LOG: frames=4694  distinct payloads=1  changes=1
SYNTH   : frames=300   changes=6 at 0 50 100 150 200 250
          (sub-tolerance jitter correctly ignored)
```

Build clean, `validate_uba.py` 4694/4694.

---

<a id="session-58"></a>
## Session 58 — Layout-driven decode, EBD + SBD curves

The re-sent log is byte-identical to the one already mined
(`md5 da054d23…`, 4694 frames, all carrying the same static EOA target), so it
holds no new information about the segment-count question. Rather than stay
blocked on the emitter, the decode is now **driven by the frame size** instead
of by a constant.

### The problem this removes

| | |
|---|---|
| Declared `Target_Internal` | `Target`(17) + `CURVE[2]` × `CURVE_SEGMENT[2*MAX_DEC_UNITS+1 = 11]` = **1073 B** |
| Every captured `@uba` frame | **452 B** = 17 + 9×48 + 3 |

`uniform_braking.c` indexes `curves_for_target[UBA_EBD]=0` and
`[UBA_SBD]=1`, so the full struct is two curves. Nine slots cannot be two
curves of anything, so the emitter is not writing the whole struct — and the
emitter still has not been seen.

Hard-coding either number is wrong. Hard-coding 9 breaks the day the firmware
sends the full struct; hard-coding 11×2 breaks today. Worse, either mistake
parses with the wrong stride and produces a **plausible-looking wrong curve**
rather than an obvious failure.

### What changed

`Braking::detectLayout(payloadBytes)` maps a size onto
`{targetBytes, curves, segmentsPerCurve, trailing}`:

```
   452 B -> 17 + 1 x  9 x 48 + 3    documented   "1 curve x 9 segments (as captured)"
  1073 B -> 17 + 2 x 11 x 48        documented   "full Target_Internal (EBD + SBD)"
  1076 B -> 20 + 2 x 11 x 48        documented   "full Target_Internal, 4-byte enum"
   545 B -> 17 + 1 x 11 x 48        documented   "1 curve x 11 segments"
  other  -> as many whole 48 B segments as fit   UNDOCUMENTED, flagged
```

An unfamiliar size still decodes — as many whole segments as fit after a
17-byte `Target` — but is marked undocumented and surfaces in the panel and
status line with a ⚠. Every number under it is only as trustworthy as the
stride that produced it, so the guess is labelled as a guess rather than
silently blended in with recognised frames.

A frame too short to hold a `Target` plus one whole segment is still refused
outright (verified: a 40 B frame returns `valid = false`).

### Model reshaped

`Snapshot` now holds `QVector<Curve>` rather than a flat segment list.
`Curve` carries the per-curve operations (`activeSegments`, `span`, `speedAt`)
and its name from `CURVE_TYPE` — **EBD** (0), **SBD** (1). `Snapshot::span()`
is the union across curves; `Snapshot::speedAt(loc, out, curveIndex)` takes a
curve, or scans all of them when passed -1.

Panel and plot follow: a **Curve** selector (All / EBD / SBD) that is populated
from what the frames actually contain — not from
`NUM_OF_CURVES_FOR_EACH_TARGET`, because offering an SBD entry no frame carries
would be a dead control. The segment table gains a `curve` column and the
summary reports extent, entry speed, terminus and speed-at-target per curve.

On the chart, **curve 0 is solid and curve 1 dashed**, with a legend that
appears only when there is more than one curve to tell apart. The distinction
is carried by line *style*, not colour, so it survives a greyscale printout and
a colour-blind reader; colour continues to separate the segments *within* a
curve.

### Verified

Whole project builds clean (`qmake && make -j8`, 0 errors). Schema gate
unchanged: `validate_uba.py` **4694/4694**.

The 452 B path, against the real capture:

```
REAL  valid=1  layout='1 curve x 9 segments (as captured)'  documented=1
      EBD  slots=9 active=4  span 3835.120..6348.000 m  0.0000..70.8333 m/s
```

The 1073 B path has **no capture to test against**, so it was exercised with a
synthetic full `Target_Internal` built from the real segments (SBD given a
doubled `A`, i.e. half the deceleration, so the two curves are distinguishable):

```
FULL  valid=1  layout='full Target_Internal (EBD + SBD, 11 segments each)'
      EBD  slots=11 active=4   SBD  slots=11 active=4
      EBD speed @6000 m = 93.34 km/h    SBD @6000 m = 66.00 km/h
      polylines EBD=4 SBD=4
```

Both were rendered offscreen and inspected — `braking_curve_render.png`
(single curve, real data) and `braking_curve_ebd_sbd.png` (EBD solid + SBD
dashed, synthetic).

**This is the honest limit of what can be claimed:** the two-curve path is
mechanically correct and exercised, but no real two-curve frame has ever been
seen. If the emitter turns out to pack the curves differently — interleaved,
SBD first, or a purpose-built log struct rather than the raw `Target_Internal`
— the 1073 B branch is where that would need correcting.

### Still the one open question

`ui_capture.c`, or whichever file calls the capture emit for `@uba`. The single
line that passes the pointer and length is enough. It settles whether those
nine segments are the EBD curve alone, a flattened subset, or a purpose-built
log struct — and it is the last thing standing between this panel and being
provably right rather than defensibly right.

---

<a id="session-57"></a>
## Session 57 — Braking curve panel (speed vs distance, scrubbable)

Three new files for the braking logic, one new window, and the first session in
this project where the C++ was **actually compiled and run** rather than
reasoned about.

### Build environment — the "no Qt compiler" note is now out of date

Qt5 dev packages install cleanly from the Ubuntu archive in the sandbox
(`apt-get install qtbase5-dev`). The whole project now builds:

```
qmake && make -j8   ->   0 errors, binary produced
```

This changes the validation posture. `engine.py` is still the gate for schema
semantics (it is the reference implementation), but C++ correctness no longer
has to be argued — it can be compiled, linked and executed. The braking maths
below was verified by running it against the real capture, not by inspection.

### What the firmware sources settled

`curve_manager.c` confirms the relation reverse-engineered from the wire last
session, exactly:

```c
crv_range.eqn.A = -(1.0 / (2 * net_accln));
crv_range.eqn.C = ((SQUARE(final_speed) + 2*net_accln*end_loc) / (2 * net_accln));
```

So `location = A*speed² + C`, `a = -1/(2A)`, and `C` carries the stop location.
To plot speed against distance the relation is inverted:
`speed = sqrt((location - C)/A)`.

Two things the sources add that the wire alone could not have told us:

- **`CreateCurveSegmentHorizontalLine` stores `A = 0, C = speed`** — a flat
  speed limit, not a quadratic. Inverting blindly divides by zero, and one NaN
  in a `QPainterPath` takes the whole polyline with it. `A == 0` is branched on
  everywhere and covered by a test.
- **`IsCurveRangeValid` is a memcmp against zero**, so an unused array slot is
  all-zero — not a segment at the origin. `Segment::isEmpty()` reproduces it
  field for field (a real segment may legitimately have `A == 0` and `C == 0`).

**The segment-count question is still open, and the sources deepen it.**
`braking_common_include.h` says `MAX_DEC_UNITS 5` and `curve_manager.h` says
`NUM_OF_CURVES_FOR_EACH_TARGET 2`, i.e. `sizeof(Target_Internal)` = 17 + 2*11*48
= **1073 B**. The wire carries **452 B** = 17 + 9*48 + 3. So the capture is
*not* a whole `Target_Internal`, and `curves_for_target[]` is indexed
`[UBA_EBD]=0, [UBA_SBD]=1` (`uniform_braking.c:162,166`) — two curves, EBD and
SBD. Nine slots cannot be two curves of anything. The emitter (`ui_capture.c`)
was not among the uploaded files; **it is the one file needed to say whether
those nine segments are the EBD curve alone, a flattened subset, or a
purpose-built log struct.** Until then the panel draws what arrives and calls
it one curve.

### New files — logic separated from UI

| file | role |
|---|---|
| `brakingcurves.{h,cpp}` | **No Qt widgets.** `Segment`, `Snapshot`, frame decode, curve inversion, polyline sampling, session walk. Unit-testable and reusable. |
| `brakingcurveplot.{h,cpp}` | The speed-vs-distance chart widget. Paints only; owns no data. |
| `brakingpanel.{h,cpp}` | The window: scrubber, tables, live subscription. |

Menu: **Monitor → Braking Curves…** (`Ctrl+Shift+B`).

### The panel

- **Speed (km/h) on y, track location (m) on x.** Speed is converted only at
  the label; the wire and all maths stay in m/s. Distance is absolute track
  location, unshifted, so it can be correlated against an RFID or MA end point.
- **Time scrubber** across every `@uba` frame in the tab — pick a moment, see
  the curve that was in force then. Prev/next step one frame. `Follow live`
  pins to the newest frame and **auto-releases the moment the operator drags**.
- **Axes locked by default**, fitted once over the whole session. Autoscaling
  per frame makes a stationary curve look like it is moving and a collapsing
  one look stationary — the exact opposite of what a scrubber is for.
- **Ghosts**: N earlier curves drawn faintly behind the current one, so change
  reads as movement rather than as a still frame.
- **One colour per segment**, with junction dots. The deceleration steps are
  the structure of the curve, and they are invisible on a single smooth line.
  Table row `#` matches plot colour order.
- **Hover crosshair** reads out `(distance, allowed speed)` through the real
  curve equation, not interpolated off the polyline. Off the curve it says
  *(off curve)* rather than showing 0 — "not on the curve" and "permitted speed
  zero" are different facts and only one of them is safe to imply.
- **Show frame in log** jumps the main window to the capture line, by
  **timestamp** via the existing `jumpToEntry` — not by row index, which would
  go stale when the model's ring buffer wraps and is unknown on the live path.

### Verified by running it

Against `81_2_15092026_121737.log`:

```
payload 452 bytes -> valid, target 6248.000 m, EOA, 4 active segments
span            loc 3835.120..6348.000 m   speed 0.0000..70.8333 m/s
polylines       4, 192 points, 0 non-finite
monotonic toward target: YES (largest upward step 0.0000 m/s)
horizontal segment (A=0): v=13.8889, finite, no divide-by-zero
empty slot isEmpty: true
```

Junction continuity — the curve has **small real discontinuities** at segment
boundaries, because `start_loc` is not exactly `x(higher_speed)`:

```
junction 0 @ 5959.064 m:  27.7778 -> 27.3686 m/s   (-1.47 km/h)
junction 1 @ 6156.890 m:  19.4444 -> 19.1970 m/s   (-0.89 km/h)
junction 2 @ 6250.549 m:  13.8889 -> 13.9607 m/s   (+0.26 km/h)
```

These are in the firmware's own data. Segments are therefore drawn as
**separate polylines** — bridging them with a connecting line would draw a
curve the firmware does not contain.

The plot was rendered offscreen to PNG and inspected. Three defects were found
and fixed that way, none of which inspection would have caught:

1. y-axis title clipped to "peed (km/h)" — it shared a margin with the tick
   labels; it now has its own gutter.
2. x-axis title clipped at the widget edge — the bottom margin allowed for one
   text row, but there are two.
3. The loco marker was the same blue as segment 0, so it read as part of the
   curve. Now dash-dot in the foreground colour, and labelled.

A bug in the summary table was also caught by running it: "curve end vs target"
searched for the segment end *nearest* the target and reported 2.5 m. The
curve's actual terminus is 6348 m — it runs **100 m past** the 6248 m target —
and the allowed speed where the target actually is comes out at 14.06 m/s
(50.6 km/h), not zero. Both are now stated outright; the old number was
meaningless and reassuring, which is the worst combination.

### Files touched

`DLConsole.pro` (3 sources, 3 headers), `mainwindow.{h,cpp}` (menu action,
slot, include, jump wiring), plus the six new files above.

---

<a id="session-56"></a>
## Session 56 — `@uba` (Target_Internal + braking curve)

New capture type. Schema-driven from day one; no hand decoder was written, so
there is no hand/schema pair to converge later.

### What the wire says

452 B, little-endian throughout, **no message header and no CRC** — the loco
memcpy's the struct out as-is.

```
target_location   double   8 B   @0
target_speed      double   8 B   @8
target_type       enum     1 B   @16
CURVE_SEGMENT[9]           432 B @17     (48 B each: A, C, start_loc,
                                          end_loc, higher_speed, lower_speed)
tail                        3 B  @449
```

Two findings that the quoted C header does not predict, both established from
all 4694 frames of `81_2_15092026_121737.log`:

**`target_type` is ONE byte, not four.** The target build compiles enums to
their smallest type (`-fshort-enums`), so `Target` is 17 B, not 20. This is the
whole ball game: assume 4 and every double after it is read three bytes short
and decodes to `1e-307`-grade garbage that still *looks* like a successful
decode. The first curve double sits at offset 17.

**Nine segment slots, not 22.** 435 B of curve data is exactly `9*48 + 3`. The
build that produced this capture therefore carries ONE curve of
`2*MAX_DEC_UNITS+1 = 9` segments (MAX_DEC_UNITS 4). The header the layout was
quoted from says `MAX_DEC_UNITS 5` and `NUM_OF_CURVES_FOR_EACH_TARGET 2`, which
would be 22 segments and a 1073 B frame. Nine is what arrives. `count="9"` in
`kavach.xml` is the single place to change if the deployed constants move.
A 3-byte tail follows (zero in every frame) — consistent with a 449 B struct
padded to a 4-byte boundary by the framing.

### Curve semantics

Each segment is `x = A*v^2 + C`, so `C` is the stop location and
`A = -1/(2a)` for deceleration `a`. Verified over all 4694 frames:

- `end_loc == A*lower_speed^2 + C` exactly (0 violations)
- `seg[k].end_loc == seg[k-1].start_loc` — the curve is continuous, as the
  struct comment requires (0 violations)

`start_loc` is the clamped segment start (where the next step takes over), not
`x(higher_speed)`. The capture holds one static EOA target at 6248 m with four
populated segments stepping 255 → 100 → 70 → 50 → 0 km/h at ~0.938 m/s²
(1.000 m/s² on the final one).

### Engine additions

- **`type="double"`** — 8-byte IEEE-754, little-endian. `take()` returns at most
  32 bits, so the two halves are read in wire order (low word first).
- **`FLOAT_FORMATTERS` / `compositeFloat()`** — `format="name"` now works on
  reals. The integer `FORMATTERS` could not be reused: they index and divide
  their argument and would misbehave silently on a double. Two entries:
  - `mps` → `13.8889 m/s  (50.0 km/h)`
  - `curveA` → `-0.533292  (decel 0.938 m/s²)`
- **`Decoder::readTyped()` / `fieldBits()`** (C++) — one typed read shared by
  the flat, entry and expand walkers. This closes a **pre-existing parity
  bug**: only `walkFlat` understood `type=`, so a `float` inside a `<repeat>`
  was read as a 0-bit integer, while `engine.py` routes all three through
  `_read()`. No shipped packet hit it (no float lived inside a repeat until
  now), but the divergence was real.

Reals are deliberately not written into `Ctx` on the C++ side: `Ctx` is
integer-valued and rounding a double into it would make `when=`/`count=` mean
something different here than in the reference engine. Do not condition on a
real field.

### Display

One row per segment rather than 54 loose numbers — the ladder is what you read:

```
target_location    6248.000 m
target_speed       0.0000 m/s  (0.0 km/h)
target_type        EOA
seg[0]             A=-0.500000  (decel 1.000 m/s²)   C=6348.000 m   start=6250.549 m   end=6348.000 m   hi=13.8889 m/s  (50.0 km/h)   lo=0.0000 m/s  (0.0 km/h)
seg[1]             A=-0.533292  (decel 0.938 m/s²)   C=6353.422 m   start=6156.890 m   end=6250.549 m   hi=19.4444 m/s  (70.0 km/h)   lo=13.8889 m/s  (50.0 km/h)
...
seg[4]             A=0.000000   C=0.000 m   start=0.000 m   end=0.000 m   hi=0.0000 m/s  (0.0 km/h)   lo=0.0000 m/s  (0.0 km/h)
tail               3 B struct tail (zero in every captured frame)
```

Empty slots are shown rather than hidden — an unused slot is a fact about the
curve, and hiding it would make a 4-segment curve indistinguishable from a
9-segment one that happens to be zeroed.

### Validation

`schema/validate_uba.py` — new. There is no hand decoder to diff against, so
the oracle is the struct itself: every frame is re-read with `struct.unpack`
against the declared C layout and the XML engine must reproduce it row for row,
then the physical invariants above are checked.

```
frames            : 4694
wrong length      : 0
row mismatches    : 0
invariant failures: 0
PASS — 4694/4694 frames match
```

Regression check — every other validator re-run and diffed against the
unpatched tree: `validate_slrp`, `validate_dmi`, `validate_arp_lsrp`,
`validate_dio`, `validate_rfid`, `validate_ccdl` all **byte-identical to
baseline**; `nmsflt` 156/156, `nmshlth` 10615/10615, `nmsrssi` 10615/10615,
`aap_aep` 46/46.

C++ side: still no Qt compiler in the sandbox, so `engine.py` remains the gate.
The new bit logic was additionally cross-checked by lifting `Cursor::take`,
`readTyped`'s double path and `compositeFloat` into a standalone non-Qt harness
(`uba_cpp_parity.cpp`, not part of the build) and diffing its output against
the Python engine for a real frame: identical field for field, cursor ending at
bit 3592 = byte 449, exactly where the 3-byte tail begins.

### Files touched

| file | change |
|---|---|
| `schema/kavach.xml` | `UBA` packet + `ubaTargetType` enum |
| `schema/engine.py` | `type="double"`, `FLOAT_FORMATTERS`, `_fmt_float` format hook |
| `schema/schemadecoder.cpp` | `readTyped`/`fieldBits`/`compositeFloat`; flat, entry and expand walkers routed through them |
| `schema/schemadecoder.h` | `Kind`, `readTyped`, `fieldBits` declarations |
| `schema/validate_uba.py` | new gate |
| `capturedecoder.h` | `CapType::UBA` + frame-recipe note |
| `capturedecoder.cpp` | token, label, direction, `describe()` dispatch |
| `replaywindow.cpp`, `lococonsolewindow.cpp` | tab / link-overview order |
| `framediffwindow.cpp`, `decodeworkbench.cpp` | token lists |

### Open question

Confirm the deployed `MAX_DEC_UNITS` and `NUM_OF_CURVES_FOR_EACH_TARGET`. The
wire says one curve of nine segments; the header says two curves of eleven. If
the header is right for a newer build, the frame will be 1073 B and
`validate_uba.py` will report a length mismatch rather than decoding wrongly.

---

<a id="session-55"></a>
## Patch 55 — two of the three fixed, one needs a sample line

### Fixed: the mojibake in pin labels

Visible in the screenshots: `CUR_SIG_ASPECT · slrp Â· 81_1`. The first middle
dot renders, the second does not.

    QStringLiteral("%1 · %2").arg(p.field, narrowing.join(QLatin1String(" · ")))
                        ^ fine                            ^ broken

The source file is UTF-8, so `·` is two bytes (C2 B7). `QStringLiteral`
decodes them as UTF-8 and gets one character; `QLatin1String` takes them as
two Latin-1 characters and gets `Â·`. Same dot, same line, two constructors.

Both are `QStringLiteral` now. A scan of the whole codebase for non-ASCII
passed through `QLatin1String` or `QLatin1Char` found this one instance and
no others.

### Fixed: the session key dialog reads one loco at a time

`SessionKeyStore` has been per-loco all along — `locos()` lists them and every
accessor takes a `locoId` — but the dialog called them all with the default,
which means "whichever loco is active". On a two-loco capture you got
whichever spoke last, with no way to say otherwise.

The live section now has a **loco selector**: "active loco" (the old
behaviour, still the default) plus one entry per loco the store has seen. It
drives both the displayed session key and what "Load into form" fills in.

The whole load comes from **one** loco. Key sets and randoms are per loco, and
mixing one loco's set with another's random derives a key that never existed
on either link — which is the kind of wrong answer that looks plausible.

The list rebuilds when the store changes but keeps the operator's choice if
that loco is still present. Rebuilding blindly would snap the selection back
to the first loco every time a frame arrived, which on a live capture is
several times a second.

### Not fixed yet: empty auth-key and random tabs

Traced, not guessed. `LocoConsoleWindow` keys every state by
`CaptureLine::key()`, which is `<locoId>_<ctrlId>` taken from the last two
underscore-separated parts of the tag. A type tab shows
`st.latest[type]` for the **selected** key, and prints "(no data yet)" when
that key has no frame of that type.

So a tab is empty whenever the auth-key or random lines carry a different
loco/ctrl id from the loco being viewed — and the code comment beside the tab
list says these come from the LCU, which is exactly the thing likely to tag
them with its own id. They would then appear under their own entry in the loco
selector rather than under 81_1.

The other possibility is that `parseLine` rejects them outright. It requires
the tag to have at least three underscore-separated parts and both trailing
parts to be numbers, so `@authkeys` or `@auth_keys 2026-...` without the
`_<loco>_<ctrl>` suffix is dropped before anything else runs.

Both are one-line fixes but they are different one-line fixes, and guessing
would mean changing routing for every capture type. **One raw auth-key line
and one random-number line from the log settles it.**

### Files touched

    pinpanel.cpp                the separator
    sessionkeydialog.{h,cpp}    per-loco live section

### Verification

    119 suites, 3031 checks, 0 failed
    headless smoke run clean

---

<a id="session-54"></a>
## Patch 54 — the two items that needed reading, not machinery

### The aspect widths, resolved

The review flagged four rules whose value did not fit the field: the document
writes `1001` and `1110` where `kavach.xml` gives the aspect 6 bits.

Reading the **whole** of section 31.23 rather than the four flagged rows
settled it. The document writes every aspect in binary and drops leading zeros
on the short ones:

    0, 1, 10, 100, 101, 110, 111, 1000, 1001, 1010, 1100, 1101, 1110, 1111,
    then 010000, 010111, 011000, 011001, 011111, 100000

So `1001` is **9** and `1110` is **14**. There was never a decimal reading;
my width heuristic was simply wrong, and the unreachable-value guard from
patch 50 is what stopped it reaching the rule file.

Cross-checking all twenty values against the schema's `sigAspect` enum, they
agree on nineteen:

- **9** is spare in the document, and `kavach.xml` has no `v=9` so it falls
  through to Spare. **Encoded**, for both `CUR_SIG_ASPECT` and
  `NEXT_SIG_ASPECT`.
- **14** is the one disagreement. The document calls it Spare; `kavach.xml`
  calls it **AG Marker OFF**, a real aspect. **Not encoded.** One of the two
  is wrong, and a rule here would report a legitimate aspect on live traffic —
  the one failure that teaches an operator to ignore the feature. It needs a
  decision, and it is a one-line addition once made.

### The PKT_TYPE false positives, resolved

All seven rows open by naming the packet under test — `0011` for LC info,
`0111` for the TSR profile — and state the actual condition at the END of the
sentence. The extractor took the first field it saw.

- **31.55.5 / 31.54.x** → the condition is `TSR_STATUS as 3 (Reserved)`.
  `TSR_STATUS` is a 2-bit field and the schema's `tsrStatus` enum agrees that
  3 is Reserved. **Encoded.**
- **31.47.1 / 31.48.1** → `LC_ID_Numeric` and `LC_ID_Alpha_Suffix` as 0. Not
  encoded: in `kavach.xml` these are `id` and `suffix` inside the `lc` repeat,
  local names that each iteration overwrites. A rule keyed on `id` would test
  whichever level crossing happened to be decoded last, and `id` is generic
  enough to collide besides. Expressing these needs per-iteration values,
  which the value map does not currently carry.
- **31.46.2** → a `SUB_PKT_LENGTH` consistency table against `LM_LC_Info_CNT`,
  not a value rule at all.
- **31.6.2 / 31.7** → genuinely PKT_TYPE rules, but **direction-dependent**:
  an onboard rejects `1010` because that is an onboard-to-station type, and a
  station rejects `1001` for the mirror reason. Encoding either without
  knowing which end this console is watching would report every normal packet
  on one side of the link. `LocoIdentity` from patch 53 could answer that — a
  source with a learned own-loco ID is a loco-side capture — but that is a new
  inference and worth agreeing before it becomes a rule.

### Tests

The aspect reading is tested in both directions, which is the only way to know
a binary/decimal call was right: 9 fires, the six-character spares still fire,
and Green, Stop Board and a stencil route do not. Plus an explicit check that
**14 does not fire**, so if someone later adds that rule the test says why it
was left out rather than silently going green.

### Files touched

    schema/rejectrules.xml      3 rules added, with the reasoning above them
    tests/test_rejectrules.cpp

### What remains

Only the four items above that need a decision or new plumbing: aspect 14, the
LC per-iteration fields, the SUB_PKT_LENGTH consistency table, and the
direction-dependent packet-type rules. Every rule the document states plainly
and unambiguously is now in.

### Verification

    119 suites, 3031 checks, 0 failed      (was 119 / 3022)
    headless smoke run clean, rules load with no warning

---

<a id="session-53"></a>
## Patch 53 — which loco is "ours"

The last piece the reject rules were waiting on: an SLRP addressed to another
loco is one the onboard would not process, and there was no way to say so
without knowing which loco this console is watching.

### Learned, not configured

A configured ID is one more thing to set before a run and one more thing to
have wrong. ARP and LSRP carry `SOURCE_LOCO_ID`, so a console that has seen
either has already been told.

`LocoIdentity` tracks it **per source**, because two tabs are two locos rather
than one contradiction.

#### `arprecv` is deliberately not learned from

`arp` is what our loco sends; `arprecv` is another loco's ARP arriving at
ours, and its `SOURCE_LOCO_ID` is the other loco. Learning from it would teach
the console the wrong identity on every loco-to-loco approach — precisely when
these rules matter most. There is a test for it, including that a later
`arprecv` cannot unsettle an identity already learned.

Loco id 0 is the unassigned value and is not recorded, or a source carrying
only unassigned frames would look identified.

### Three states, and the two that are not "known"

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

### How it reaches the rules

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

### Shared across windows

One tracker on `MainWindow`, borrowed by the log tabs' inspector and the
compare window's, for the same reason the bookmark store is shared: a loco
identified in one window is the same loco in the other, and two trackers would
disagree.

The compare window forwards it in `setLocoIdentity()` rather than at
construction — the owner supplies it after the window is built, so a
constructor-time hand-off would always pass null and the panel would silently
learn nothing.

### A limitation worth stating

The console learns from frames it **decodes**, and it decodes on selection,
not on arrival — that is a deliberate existing design decision, not an
oversight. So the identity is learned once you have looked at an ARP or LSRP
on that source. Until then the rules say they are not checking.

Learning at ingest would need decoding in the arrival path, which the codebase
explicitly avoids. A cheap middle option exists — decode only ARP/LSRP, only
until an identity settles, throttled — and is worth doing if the current
behaviour proves annoying in practice.

### Files touched

    locoidentity.{h,cpp}         NEW
    rejectrules.{h,cpp}          ne_field operator
    schema/rejectrules.xml       the DEST_LOCO_ID rule
    fieldinspector.{h,cpp}       observes, then merges the context
    mainwindow.{h,cpp}           owns the tracker, shares it
    comparewindow.{h,cpp}        borrows it
    tests/test_locoidentity.cpp  NEW

### Verification

    119 suites, 3022 checks, 0 failed      (was 118 / 3000)
    menu audit passed
    headless smoke run clean

---

<a id="session-52"></a>
## Patch 52 — conditional reject rules

The FRAME_OFFSET guard, and with it the rule format needed for most of the
document's remaining conditions.

### Why the format had to change

FRAME_OFFSET 14 looked like the document contradicting itself: 31.19.3 sends
`1110` and expects it processed, 31.19.5 says process only below 14. It is not
a contradiction — they describe different sections. Neither clause can be
encoded as a value rule without losing the thing that separates them, and a
value rule for either one would be wrong half the time.

So rules now carry a guard.

### Reusing the schema's condition language

`kavach.xml` already has one — `<field when="AUTHORITY_TYPE==1">`,
`"A in 3..7"`, `"A not in 0..0"`, `"A & 7 == 3"` — with a working evaluator
that has been in use for every conditional field in the schema.

`Decoder::condOk` is now exposed as `Decoder::conditionHolds()` taking a plain
value map, and the reject rules call it. One condition language in the
program, one implementation. A second dialect would be one more thing to learn
and one more thing to drift from the first.

The guard is evaluated against the same decoded map as the value, before it,
and a guard naming a field the frame does not carry reads that field as zero —
which is the schema's existing behaviour, not a second rule invented here.

### The rule

    <rule clause="31.19.5" field="FRAME_OFFSET" op="eq" value="14"
          when="TRAIN_SECTION_TYPE not in 0..1"
          note="frame offset 14 outside a station or absolute-block section"/>

**A reading to confirm.** `secType` is 0 Station Section, 1 Absolute Block,
2 Autoblock, 3 Reserved. "Accepted where the RFID has absolute or station
section" is taken as `TRAIN_SECTION_TYPE in 0..1`, so 14 is reported only in
Autoblock and Reserved. That interpretation is written into the rule file
above the rule itself, so the next person to read it sees the reasoning rather
than a bare number. If it is wrong it is a one-line edit and nothing else
changes.

### The guard is part of the claim

A finding now reads:

    FRAME_OFFSET = 14 (when TRAIN_SECTION_TYPE not in 0..1) — frame offset 14
    outside a station or absolute-block section  [31.19.5]

Without the condition stated, "FRAME_OFFSET = 14" would look plainly wrong to
anyone who knows 14 is fine in a station section, and the operator would be
left deciding whether to believe the console.

### Tests

The guarded rule is checked in both directions, which is the only way to know
a guard works: 14 stands in sections 0 and 1, is reported in section 2, and 13
is unremarkable everywhere. Plus one that would have caught the obvious
mistake — the unconditional rule for offset 15 still fires in a station
section, where 14 does not. A guard that leaked onto its neighbours would pass
every other check here.

### Files touched

    schema/schemadecoder.{h,cpp}  conditionHolds() exposed; condOk forwards
    rejectrules.{h,cpp}           when= parsed, evaluated, and reported
    schema/rejectrules.xml        the FRAME_OFFSET 14 rule
    tests/test_rejectrules.cpp

### Still open

The aspect widths and the seven `PKT_TYPE` false positives still need reading
by hand. The narrative conditions are now expressible for the ones that reduce
to field comparisons; the rest need state the console does not keep. Inferring
our own ID from ARP/LSRP for the `DEST_ONBOARD_ID` rules is the next real
piece, and it is runtime state rather than a rule-file change.

### Verification

    118 suites, 3000 checks, 0 failed      (was 118 / 2993)
    menu audit passed
    headless smoke run clean, rules load with no warning

---

<a id="session-51"></a>
## Patch 51 — reject reasons reach the screen

Patch 50 built the rule set and the evaluator and stopped short of the UI.
This wires it up.

### The plumbing

`CaptureDecoder::describe()` gained an optional `rawValues` out-parameter,
forwarded to `Schema::Decoder::decode`. It turned out to be one change, not
seventeen: every packet type renders through a single `schemaRows()` helper,
so the map is filled in one place and forwarded at each call site.

Merged rather than assigned, because a few types decode twice — an envelope
and a body — and the second call must not erase the first.

Rules evaluate on those numbers and never on the rendered rows. A row's value
is a display string: `"2 (Reverse)"`, `"12.5 m"`, an enum label. Comparing a
clause against that would mean parsing presentation back into a number, and
would break the day someone rewords a label.

### What the operator sees

The field inspector's status line, when any rule fires:

    A receiver would not process this frame — PKT_DIR = 0 — packet direction
    unidentified or spare  [31.16.1]

Reject reasons take precedence over the field/byte-range summary: a frame the
receiver would drop is a more urgent fact than how many of its fields carry
byte ranges. Every rule that fires is listed, not the first.

The wording stays a report. "A receiver would not process this frame", never
"invalid" and never "fail" — and when nothing fires, nothing is said, because
silence means no rule in `rejectrules.xml` matched and that is not the same
claim as the packet being good.

The rules load beside the schema, on the same terms: a failure is a packaging
error worth a warning, not a reason to refuse to start. The console then
reports no reject conditions, which is honest — it has none to check against.

### The test that matters

`rejectrouting` runs a real SLRP capture line — one already in the suite as a
CRC fixture, recorded off equipment — all the way through `describe()` to the
evaluator.

It asserts the frame trips **nothing**. If that ever fires, either a rule is
wrong or the value plumbing is, and both deserve to fail a build: a false
reject reason on a good frame is how an operator stops trusting the feature.

Then it flips `PKT_DIR` to the spare value on the same decoded map and asserts
exactly one rule fires, citing 31.16.4 — which is what proves the first check
was not passing merely because nothing was connected.

### Files touched

    capturedecoder.{h,cpp}      rawValues through describe() and schemaRows()
    fieldinspector.{h,cpp}      findings shown, rejectReasons() accessor
    rejectrules.cpp             loads beside the schema at first use
    tests/test_rejectrules.cpp  rejectrouting suite

### Still open

From the review, unchanged: the aspect widths, the seven `PKT_TYPE` false
positives, the narrative conditions, and the FRAME_OFFSET 14 guard — which
needs a condition expression in the rule format, since it depends on
absolute-location RFID or station section. Inferring our own ID from ARP/LSRP
for the `DEST_ONBOARD_ID` rules is also still to do.

### Verification

    118 suites, 2993 checks, 0 failed      (was 117 / 2985)
    menu audit passed
    headless smoke run clean, rules load with no warning

---

<a id="session-50"></a>
## Patch 50 — reject rules, section A

The engine and the rule set. **Not yet wired to the UI** — see "What's next".

### The rule set

`schema/rejectrules.xml`, external like `kavach.xml` and for the same reason:
a clause read wrongly is fixed by editing a file, not by shipping a build.
Every rule carries its FRS clause, so any reason the console prints can be
checked against the document.

**26 rules**, from section A of the review. Three things were left out on
purpose, and each is stated in the file itself so the next reader does not
think they were forgotten:

- the aspect rules whose width the document contradicts,
- the rows where `PKT_TYPE` names the section rather than the fault,
- the narrative conditions.

#### FRAME_NUM is a range

The document tests `0` and `86401` because they are the boundaries. The field
is seconds since midnight plus one, so anything outside `1..86400` is the same
fault. Encoding only the two tested values would have passed `90000` silently
— which is precisely the failure this feature exists to catch. Encoded as
`op="outside" min="1" max="86400"`, and said out loud in the file.

#### Six rules dropped as unreachable

Checking each value against the field width in `kavach.xml` found six that
cannot occur: `SOURCE_STN_ILC_IBS_ID == 65536` in a 16-bit field (max 65535),
`APPR_STN_ILC_IBS_ID == 65539` in 16 bits, and the four aspect values read as
decimal. Those clauses test the generating tool, not the wire.

The loader now **refuses** such a rule rather than accepting it, because a
rule that can never fire reads on screen as a condition being checked when it
is not. That check is a test.

### The engine

`rejectrules.{h,cpp}`. Two properties worth stating:

**It never says a packet is valid.** An empty result means no rule in the file
matched, which is a different claim — the file holds one section of the
document. The wording is "no reject condition matched".

**It reports every rule that fires, not the first.** A malformed frame usually
trips several, and fixing the one the console mentioned, rebuilding, and
running again to find the next is the loop this removes.

Findings read `PKT_DIR = 0 — packet direction unidentified or spare [31.16.1]`:
the condition, what the frame carried, and the clause. No "invalid", no
"FAIL", no "PASS" — the tooling reports and the signatory decides. There is a
test asserting the absence of that vocabulary.

A malformed rule file is **refused whole**. Half a rule set is worse than a
stale one: it silently stops reporting conditions the operator still believes
are being checked, so the previous set stays loaded.

### Decoder change

`Schema::Decoder::decode` gained an optional `rawValues` out-parameter giving
each field's decoded NUMERIC value. The decoder already built this map to
resolve counts and conditions and then threw it away.

Rules evaluate on those numbers, never on the rendered rows: a row's value is
a display string — `"2 (Reverse)"`, `"12.5 m"`, an enum label — and comparing
against it would mean parsing presentation back into a number, and would break
the day a label is reworded. Existing callers are unaffected; the map is
published only on request.

### What's next

Wiring it to the field inspector needs `CaptureDecoder::describe()` to forward
the raw values too — it is a per-type dispatcher, and threading the map
through it is a real change rather than a signature tweak. Doing that badly to
finish today would put the risky part of this feature in without the care the
rest of it got.

Also still open, from the review: the aspect widths, the seven `PKT_TYPE`
false positives, the narrative conditions, and the FRAME_OFFSET 14 guard —
which needs a condition expression in the rule format, since it depends on
absolute-location RFID or station section.

### Files touched

    schema/rejectrules.xml       NEW — 26 rules, each citing its clause
    rejectrules.{h,cpp}          NEW — loader and evaluator
    schema/schemadecoder.{h,cpp} optional rawValues out-parameter
    tests/test_rejectrules.cpp   NEW
    images.qrc, *.pro

### Verification

    117 suites, 2985 checks, 0 failed      (was 115 / 2944)
    headless smoke run clean

---

<a id="session-49"></a>
## Patch 49 — Decode Workbench got the envelope, not the frame

### The report

Opening a non-capture row in the Decode Workbench handed it the wrong bytes.
The row read

    2026-09-09T01:55:04.269  21_2  INFO  02 07 0D 00 27 00 00 00 00 00 D3 AD …

and what arrived at the workbench began

    15 65 97 76 00 02 00 30 32 20 30 37 20 30 44 …

### What those bytes actually were

Not the datetime, as it looked. Two things stuck together:

- `15 65 97 76 00 02 00` — the transport header.
- `30 32 20 30 37 20 30 44` — the ASCII **codes** of the characters
  `0`, `2`, `space`, `0`, `7`, `space`, `0`, `D`. The frame written out as
  text, then hex-encoded a second time. `20 0A` on the end is the trailing
  space and newline.

So the workbench was decoding a description of the frame rather than the
frame.

### Cause

`MessageDispatcher::buildEntry` keeps `rawBytes` as the datagram verbatim:
transport header followed by payload. `entryBufferText` preferred `rawBytes`
for every row that is not an `@`-capture line.

That is correct when the payload is binary — then `rawBytes` is the only
frame there is, and its header belongs with it. It is wrong when the payload
is a frame the backend already printed as text, because then there are two
candidates and it picked the wrapper.

### Fix

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

### Tests

New suite `printedframe`, built on the reported row and its datagram. It
checks the header is gone, that `30 32 20 30 37` is gone with it, and then
runs the result through `PacketMakerDialog::parseBuffer` — the point is not
that the string looks better but that it decodes, so the check is the
composition rather than the halves.

Also covered: a prose message still yields `rawBytes` header and all; a
sentence containing `DE AD` is not mistaken for a frame; seven bytes is below
the bar; a ragged token disqualifies; and an `@`-capture line still wins over
everything.

### Note

Two of my first expectations in this suite were wrong again — the reported
frame is 39 bytes, not the 38 I counted. Worth stating plainly: the code was
right both times and the test was wrong, which is the good direction for that
error to run, but it is twice in two patches that I have miscounted by hand
what the machine could count.

### Files touched

    logentry.cpp                looksLikeHexDump, entryBufferText ordering
    tests/test_openbuffer.cpp   printedframe suite

Every caller benefits: Decode Workbench, Packet Maker, Frame Diff, the
compare row menu and the session window all go through `entryBufferText`.

### Verification

    116 suites, 2955 checks, 0 failed      (was 115 / 2944)
    menu audit passed
    headless smoke run clean

---

<a id="session-48"></a>
## Patch 48 — the Time column

Cosmetic, and it pays for itself the first time you scan a column of
timestamps.

### Two problems, both about ink

**The column was drawn in the proportional UI font.** A `1` is narrower than
a `0`, so no two timestamps line up. A column of times is read by scanning
DOWN it, and nothing in a proportional font lines up vertically — "which of
these is 200 ms later" became a character-by-character comparison.

**Consecutive rows share almost every character.** Reading

    11:04:09.100
    11:04:09.140
    11:04:09.980
    11:04:10.000

the eye walks past eight identical characters to reach the two or three that
differ, on every row, for thousands of rows. The information is entirely in
the tail and the ink was almost entirely in the head.

### What it does now

A fixed font, so digits line up down the column; and the leading run of
characters identical to the row above is drawn muted, so the characters that
changed carry the contrast. A second or an hour rolling over lights up a
longer run, which is exactly the row worth noticing.

Nothing is hidden or abbreviated. The full timestamp is still there, still
selected, still copied in full — only its weight changes.

### The parts that are easy to get wrong

- **Compared against the row above IN VIEW ORDER**, not the previous entry in
  the model. Under a filter or a sort, the row above is what the eye actually
  compares against; dimming relative to a row that is not on screen would
  mute the wrong characters.
- **The bookmark bullet is not part of the time.** `LogModel` prefixes a
  bookmarked row's time with `●`. Comparing the rendered strings would make a
  bookmarked row share nothing with its neighbour and light up whole, for
  entirely the wrong reason.
- **Never the entire string.** A burst arriving inside one millisecond is
  normal here, and a row muted end to end reads as disabled rather than as
  identical, so one character always keeps full weight.
- **Nothing is muted on the selected row.** Its background has changed
  underneath it, and a muted-on-highlight colour is the one that fails a
  contrast check in one theme or the other.
- **Background, selection and focus ring are still drawn by the style**, not
  by hand. Painting them manually is how a delegate ends up looking almost
  but not quite like every other cell, and how it stops following the theme.
- **Split runs are laid out from the whole string's rect**, so splitting the
  text does not move it. Otherwise the column would shimmer as rows scroll.

Installed in `LogTableView::configure`, so the log tabs, the compare panes
and anything else built through it agree — a column painted one way in one
window and another way beside it is worse than either.

### A note on the tests

Four of my first expectations were wrong and the code was right: `.140`
against `.100` shares ten characters, not eleven, and `12:00` against `11:59`
shares the leading `1` rather than nothing. Dimming is by CHARACTERS, not by
meaning. Corrected the expectations, and said so in the test text so the next
reader does not re-derive it.

### Files touched

    logtimedelegate.{h,cpp}          NEW
    logtableview.cpp                 installs it
    tests/test_logtimedelegate.cpp   NEW
    DLConsole.pro, tests/*.pro

### Verification

    115 suites, 2944 checks, 0 failed      (was 114 / 2929)
    menu audit passed
    headless smoke run clean

The contrast audit passes unchanged: the muted run uses the existing
`UiColor::muted()` role, which is already measured in both themes, and no
colour literal was added.

---

<a id="session-47"></a>
## Patch 47 — Find under live traffic

### The report

> When packets are continuously incoming the find takes a lot of time, and as
> soon as I disconnect the ethernet and packets stop, the find becomes fast.

### The cause

`FindBar` treated **every** model change as a renumbering:

    connect(model, rowsInserted,  scheduleRebuild);   // m_scanDirty = true
    connect(model, rowsRemoved,   scheduleRebuild);
    connect(model, modelReset,    scheduleRebuild);
    connect(model, layoutChanged, scheduleRebuild);

For three of those four that is correct and necessary. A filter change, a
re-sort, or a row falling off the front all mean row 400 is a different
message than it was, so every match row already found is a number pointing at
the wrong thing and the answer has to be thrown away.

**An append is not that.** Rows land at the end, every row already scanned
keeps its number, and keeps its verdict with it. Marking the scan dirty threw
that away anyway, so each arriving batch caused a full scan of every row in
the tab. Cost grew with the square of the session length while traffic ran,
and dropped to nothing the instant it stopped — exactly the symptom.

The 200 ms debounce hid it at low rates and made it worse at high ones: under
steady traffic the timer kept restarting, so scans landed in bursts on a
model that had grown since the last one.

### The fix

Inserts are now distinguished from the changes that really do renumber. When
rows arrive at or past the end of what was last scanned, the existing matches
are kept and only the tail `[previousRows, rowCount)` is scanned.

Guarded by the same standard as the existing narrowing path — the conditions
are load-bearing:

- **Appended at the end.** `first >= m_lastScanRowsTotal`, with no valid
  parent. An insert in the middle renumbers and falls back to a full scan.
- **The previous scan was not capped.** A capped scan never reached the end,
  so "past what we scanned" is not "past what exists".
- **The same search.** Compared on the box's text **as typed**, not on the
  decoded pattern.

That last point matters more than it looks. Narrowing compares the decoded
plain text because it rests on one pattern containing another. Appending
needs only "is this the same search", which the raw text answers for every
mode — so Regex and Hex, whose decoded text is empty and whose per-row test
is the most expensive of the five, are covered too. Deciding it on the
decoded form would have quietly excluded them.

Anything else — filter, sort, removal, reset, a changed pattern — still
forces the full scan it always did.

### Measured

Instrumented row counts, 20 000 rows already present, then 100 batches of 50:

    before   2,252,500 rows scanned
    after        5,000 rows scanned

The instrumentation was removed before shipping; it is quoted here because
"it feels faster" is not a measurement. The ratio is not fixed — the old
behaviour was quadratic in session length and the new one is linear, so it
widens the longer a session runs. On a tab that has been up for hours the
difference is far larger than 450×.

### Tests

New suite `findlivetraffic`. The point of risk in an incremental scan is a
subtly different answer, so the central check is agreement: a second bar that
never saw the appends and has nothing to reuse scans the same model from
scratch, and must produce the identical match list. Also covered: hits
accumulating across many batches in row order, a changed pattern rescanning
rows the previous search had dismissed, a filter invalidating everything
(match rows still in range AND still genuinely matching), and a row arriving
under a Regex search being picked up.

### Files touched

    findbar.{h,cpp}            append path, m_scanAppendOnly, m_scanRawText
    tests/test_findlive.cpp    NEW
    tests/tests.pro

### Verification

    114 suites, 2929 checks, 0 failed      (was 113 / 2917)
    menu audit passed
    headless smoke run clean

---

<a id="session-46"></a>
## Patch 46 — the field index

### What it answers

"Which packets carry FRAME_NUM?"

The schema has always known this and nothing showed it. Thirty-five field
names occur in more than one packet and FRAME_NUM is in five, which is the
fact that makes narrowing necessary — and the pin chooser could not tell you:
it says a packet HAS a field, never how many others do. So "narrow this pin
to a packet" was a decision made without the one number that decides it.

**Tools ▸ Schema ▸ Field index…**, or right-click any decoded field and
choose "Which packets carry X?", which opens the dialog already filtered to
that field.

### The table

    Field              In   Packets
    FRAME_NUM           5   ARP (arp), ARP (arprecv), LSRP, SLRP, …
    SUB_PKT_TYPE        1   SLRP
    LOCO_ID             3   …

- **In** is the point of the table, so counts above one are coloured and the
  column sorts numerically rather than as text.
- **Packets** names them rather than only counting, with the captype token
  shown where it differs from the packet name — `LOCO_SOS (lsos)`,
  `ARP (arprecv)` — because the token is what a pin narrows on and what a
  capture line actually says.
- The filter matches **field names and packet names**, so "what does LSRP
  carry" is the same table read from the other end.
- **"Only fields carried by more than one packet"** is the subset where
  narrowing matters at all, and the checkbox says how many there are.

Built entirely from `Schema::Decoder::fieldsByCaptype()`, added in patch 44
for the nested pin chooser — this is the second reader of it and needed no
new schema code. The dialog holds no traffic and works with no session
loaded.

### Schema reload

The index is rebuilt on reload, because the decoder loop is edit `kavach.xml`
→ reload → look again, and that is exactly when someone has this dialog open.
A field index still showing the previous schema would be worse than none.

### Reachable from both windows

The compare window grew a decoded-fields panel in patch 45, so it forwards
the same request. Neither window opens the dialog itself — both raise a
signal and `MainWindow` opens it, as with pin, plot and Packet Maker.

### Files touched

    fieldindexdialog.{h,cpp}   NEW
    fieldinspector.{h,cpp}     locateFieldRequested
    comparewindow.{h,cpp}      forwards it
    mainwindow.{h,cpp}         Schema menu entry, opener, reload refresh
    tests/test_fieldindex.cpp  NEW — counting, filtering, pre-fill, no-schema
    DLConsole.pro, tests/*.pro

### Verification

    113 suites, 2917 checks, 0 failed      (was 111 / 2891)
    menu audit passed, 24 shortcuts, 59 palette commands
    headless smoke run clean

Note for the audit build: a new Q_OBJECT class needs its header in
menuaudit.pro's HEADERS as well as the source in SOURCES, or moc never runs
on it and the link fails on staticMetaObject. Caught by the audit.

---

<a id="session-45"></a>
## Patch 45 — compare panes get the log tab's functionality

Scope as confirmed: no filter proxy (so no filter bar, marker scrollbar or
ribbon), no follow-tail in panes, and bookmarks, decoded fields and the two
row-menu verbs added.

### The panes share the log tab's table setup

`applyDensity`, `applyColumnVisibility` and `applyColumnWidths` were private
methods of `MainWindow`, so any log table elsewhere in the program got
whatever its own construction happened to say. The compare panes hard-coded
44 px rows and their own column widths: setting rows to Comfortable or hiding
the Source column changed the log tabs and left the panes looking like a
different program.

All three now live in **`logtableview.{h,cpp}`** as free functions of Settings
and a view. `MainWindow` forwards to them, so there is one answer in the
program to "how tall is a row".

What the panes gained with it:

- **Multi-row selection.** Shift-click a span, ctrl-click to add. Copying a
  block into a report was a row-at-a-time job before.
- **Density, word wrap, column visibility, stored column widths** — all
  following the same settings as the log tabs. Re-applied on rebind too,
  because `setModel` resets the header.
- **An empty-state placeholder**, distinguishing an unbound pane from a bound
  one whose source has gone quiet.

The copies and the row menu now act on the whole selection: right-clicking
inside a span offers "Copy 12 messages", not one row.

### Decoded fields, not just bytes

The bottom of the window was a hex dump. It is now hex **and** decoded fields,
side by side in a splitter, because the useful comparison across two sources
is usually of decoded values — "LSRP says Stand By here and Staff Responsible
there" — not of hex. With bytes alone the operator had to carry the row back
to a log tab to read it.

The inspector's byte highlight writes into the dump beside it, as in the main
window. Pin and plot requests are **forwarded, not handled**: the window
raises `pinFieldRequested` / `plotFieldRequested` and `MainWindow` opens the
dock, which is how every other window here does it.

`MainWindow::pinField` was split so the narrowing can be supplied rather than
read off its own inspector. A field pinned from a compare pane now lands
narrowed to that pane's source **and** that frame's captype — identical to
pinning from a log tab, rather than the compare window growing its own half
of the logic.

### Bookmarks, on the same store

`Ctrl+B`, `F2`, `Shift+F2`, plus a row-menu entry that reads "Add" or
"Remove" to match the row.

The `BookmarkStore` is **borrowed from MainWindow, not duplicated**: a
bookmark on a frame is the same bookmark whichever window set it, and two
stores would mean a mark visible in one and not the other. Stepping uses the
shared `nextBookmarkedRow`, so the log tabs, the session window and these
panes cannot drift apart.

With no store supplied the action is **disabled**, rather than present and
quietly doing nothing.

### Two more row-menu verbs

- **Load into Packet Maker** — forwarded to `MainWindow` as a buffer.
- **Why this colour?** — the affordance that made the shipped
  `colour_rules.json` bug findable, where decorative rules pinned severity to
  info so real errors rendered as info with nothing on screen saying which
  rule did it. Disabled when no rule set was supplied.

### A test that was passing by luck

The patch-44 compare tests picked their view with
`findChildren<QTableView*>().first()`. That is not pane 0 — the window now
also contains the decoded-fields table, and widget-tree order is not pane
order. The old checks passed anyway because the fixture's two sources produce
the same message text, so copying "the wrong pane" looked correct.

`paneCount()` and `paneView(int)` were added and the suite now addresses panes
by the same index `buildRowMenu` takes. This was found by a new check failing
for the right reason, not by reading.

### Deliberately not done

Per the confirmed scope: no filter bar, marker scrollbar or timeline ribbon —
all three need a `QSortFilterProxyModel` under each pane, which rewrites the
nine direct `qobject_cast<LogModel*>` sites. The shared helpers already take
`(proxy, source)` pairs, so that route stays open. No follow-tail. No export.

Also still absent, as before: "Filter to this source" (a pane already is one
source), detach console (a pane is already a detached view), tab-health
colouring (panes have no tab title).

### Files touched

    logtableview.{h,cpp}           NEW — shared table setup
    comparewindow.{h,cpp}          fields panel, bookmarks, menu verbs, paneView()
    mainwindow.{h,cpp}             forwards to LogTableView, pinFieldNarrowed,
                                   wires the compare window's raised signals
    DLConsole.pro, tests/*.pro     new files
    tests/test_comparetools.cpp    parity, selection, panels, bookmarks

### Verification

    111 suites, 2891 checks, 0 failed      (was 111 / 2875)
    menu audit passed, 24 shortcuts, 58 palette commands
    headless smoke run clean

---

<a id="session-44"></a>
## Patch 44

Three things asked for: a pin chooser nested by packet, a right-click menu in
the compare panes, and Ctrl+C that copies the message and nothing else. Two
bugs found on the way, both in the paths being touched.

---

### 1. The pin chooser nests by packet

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

#### Why a menu and not a combo with a tree in it

That was the first plan and it was dropped. A `QComboBox` whose popup is a
`QTreeView` depends on Qt's private popup event filter to decide whether a
click on a group row closes the popup, and the answer differs by whether the
group rows are disabled or merely non-selectable — which also decides whether
the keyboard can land on one and whether clicking the label expands it. None
of that is reachable from a headless test, the target is Windows/Qt 5.15, and
bugs there arrive as photographs. A nested `QMenu` has no private behaviour
to reason about and every level of it is assertable in the suite.

#### Bug found: the packet narrowing could never match, for two packets

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

### 2. A right-click menu in the compare panes

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

### 3. Ctrl+C copies the message

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

### Bug found: "Pin this field" and "Plot this field" were never connected

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

### Files touched

    schema/schemadecoder.{h,cpp}   fieldsByCaptype()
    pinpanel.{h,cpp}               nested browse menu, captype-keyed narrowing
    comparewindow.{h,cpp}          row menu, copy actions, buildRowMenu()
    logentry.{h,cpp}               formatMessagesForClipboard()
    mainwindow.{h,cpp}             Ctrl+C / Ctrl+Alt+C, connect fix, chooser feed
    tests/test_pinboard.cpp        pinchoices rewritten for the nested shape
    tests/test_comparetools.cpp    new comparerowmenu suite
    tests/test_clipboard.cpp       messages-only copy

### Verification

    111 suites, 2875 checks, 0 failed      (was 110 / 2822)
    menu audit passed, 24 shortcuts, 58 palette commands
    headless smoke run clean — and now free of the connect warning above

New coverage: the captype split and its two previously-unreachable packets;
sub-packet fields reaching the chooser; bucket boundaries losing and
duplicating nothing; picking a field setting the narrowing; the compare row
menu's contents, shortcuts and actual clipboard output; messages-only copy
including tab and newline flattening.

### Not done

- The compare panes are still single-selection (see above).
- Session and merged windows still have no copy action. They had none before;
  say the word and they get the same two keys.

---

<a id="session-43"></a>
## Session 43 — watches

| File | Change |
|---|---|
| `watchlist.{h,cpp}` | new: conditions that watch live traffic |
| `watchpanel.{h,cpp}` | new: the panel |
| `mainwindow.cpp` | the dock, the ingest hook, the announcement |
| `tests/test_watchlist.cpp` | new: 33 checks |

A pin says what a field **is**. A watch says **tell me when it becomes this**.

Both halves already existed and did not know about each other: the query
language parses these conditions, and the assertion engine reports observed /
not observed over a recording. This is those two joined and pointed at live
traffic — the case that matters during an acceptance run, where the operator
is driving the DMI and cannot also be reading the log.

```
field:LOCO_MODE~Staff_Responsible        fires when the mode changes to it
field:TSR_STATUS=2                       fires when TSR entries go live
sev:error AND src:33_1                   fires on the station's first error
```

---

## Why a watch fires once

`field:LOCO_MODE~Staff_Responsible` is true on the frame the mode changes and
on every frame after it. Reporting each one would bury the transition in its
own aftermath, which is the opposite of what was asked for.

So a watch fires on the **first** frame that matches, keeps that frame as
evidence, and goes quiet. The question was *when did this happen*, and it has
been answered. **Re-arm** forgets what was seen and keeps the condition, which
is what an operator wants between two runs of the same test.

Repeating watches exist for the other question — *how often* — and count
instead.

### It reports observed, not passed

A watch says a condition was **observed**, with the frame and the time. Not a
verdict. Same discipline as the assertion engine and for the same reason:
pass and fail belong to the signatory, and tooling that offers them invites
them to be copied without being read.

### A bad expression stays on the board

A query that will not parse is **added anyway**, with its error beside it. A
typo that silently vanished would look like a watch that never fired, which is
the one thing a watch must never be mistaken for.

---

## Two deliberate differences from the pin board

**Every frame, not a sample.** Pins update from the last entry of a batch,
because a pinned value only has to be current. A watch exists for the one
frame where the condition held, and a sampled watch would miss exactly the
event it was armed for. This is affordable because schema decoding is lazy and
cached per entry — a watch with no `field:` term costs a string comparison,
and one with a field term shares its decode with anything else that asked.

**A closed panel is still watching.** Pins stop updating when their window is
closed, since nothing is reading them. Closing a window is not disarming, and
a watch that quietly stopped watching would be worse than no watch at all.

### Firing is announced, not just displayed

Through the notification centre, and the panel is raised. The panel may well
be behind another window — that is the situation a watch exists for. If it
were only ever read by someone already looking at it, a pin would have done.

---

## Tests

33 checks: that a malformed query is kept with its error rather than dropped;
that a watch fires once and holds its evidence; that a condition which stays
true is still one event; disarming; that re-arming discards the evidence and
keeps the condition; a field condition against a real captured frame; and the
round trip through settings.

Full suite: **110 suites, 2803 checks, 0 failed.** Menu audit passed. Headless
smoke run clean.

---

## Not done

- **A watch that stops the capture.** "Stop when this happens" is the obvious
  next step and is a different kind of thing: it acts on the session rather
  than reporting on it, and getting it wrong loses traffic that cannot be
  recovered. Worth doing deliberately, not as an extra checkbox.
- **Watches over a recording.** The assertion engine already covers that
  ground from the other direction. Whether these should be one feature with
  two modes, or stay two, is a question about how test cases are written
  rather than about code.

---

## Clicking a pin goes to the frame

Asked: does clicking a pin take me to the packet where the value became that?

It did not. Double-clicking switched to the **tab** and left the operator to
find the frame themselves, which answers "which source" when the question is
"which packet". The watch panel already jumped to its evidence; pins never
recorded which frame produced the value.

They do now. Double-click, or right-click → **Go to the frame where it became
`2 (Staff Responsible)`**, and the log selects that frame.

### Which frame, exactly

The one where the value **became** what it is — not the newest one carrying
it. A frame repeating the same value does not move the target, because the
value became what it is once, and landing on the most recent frame would
answer a different question every second. Before a pin has ever changed, the
target is the frame it was first seen in, which is still the frame that
established the value.

### Two timestamps, and using the wrong one lands nowhere

A pin now keeps the **log** timestamp of those frames as well as the wall
clock. They are not the same: one is when this program saw the value, the
other is when the equipment sent it, and a replayed session makes the
difference obvious. The jump goes by log time, because that is what the log is
ordered by.

The test uses deliberately unrelated numbers for the two, so a confusion
between them cannot pass unnoticed.

### Stored as timestamps, not as frames

A pin is set up once and left for the length of a run. Holding the entries it
has passed through would keep them alive after the model evicted them — a slow
leak in exactly the long session where a pin is most useful. Timestamps cost
sixteen bytes and are what `jumpToEntry` wants anyway.

The honest limit: a frame the model has since evicted is not found, and the
tab comes forward without a selection. That is as much as can be done once the
row is gone.

A pin that has seen nothing offers no jump at all, and says so rather than
moving somewhere arbitrary.

Full suite: **110 suites, 2810 checks, 0 failed.** Menu audit passed. Headless
smoke run clean.

---

## Freezing the board

The two panels built this session answer halves of one question. A watch says
**when** something happened. A pinned board says what a field is **now** — and
"now" is the wrong tense the moment something happens: by the time the
operator looks up from the DMI, now has moved on and the state they wanted is
three hundred frames back.

So the board can be **frozen**, and a watch can freeze it.

```
Frozen 15:02:31 (watch: mode left Stand By) — values held, updates paused.
```

`Freeze` holds it by hand. The **on watch** checkbox holds it automatically
the moment any watch fires, naming which one. What is captured is the state of
every pinned field at the instant the condition was met — which is the state a
test record wants, and the state that is gone a few seconds later.

### Held, not cleared

A frozen board still shows every value, when each was last seen, when each
last changed, and which frame established it. Nothing is lost by freezing
except the updates, and those resume on thaw with the next frame carrying the
field. Freezing is therefore always safe to do and never costs information —
which is what makes it usable in a hurry, which is the only time it will be
used.

### The first freeze wins

A second watch firing does not move the instant the first one captured. The
board froze because something happened; the something that happened first is
the one being investigated.

### The button never lies

An automatic freeze checks the Freeze button as well. A frozen board looks
exactly like a live board whose traffic has stopped, and the button is the one
control that says which — a frozen board under a button reading "Freeze"
would be a lie told by the thing meant to report it. The status line says so
too, as a warning rather than a note, because mistaking a held reading for a
current one is how a stale value gets written down.

### Tests

12 more checks: that a frozen board takes nothing in, that not even the
sighting count moves, that values and origin frames survive the freeze, that
thawing resumes on the next carrying frame, and that a second freeze does not
displace the first.

Full suite: **110 suites, 2822 checks, 0 failed.** Menu audit passed. Headless
smoke run clean.

---

## One thing checked rather than built

"Copy a frame as a `@` capture line" was on my own list of things worth adding.
It already exists: a captured row's text **is** the capture line, and the row
menu's Copy hands it over whole, ready to paste into the Decode Workbench.
Verified against `replay/` rather than assumed, and then not built.

---

<a id="session-42"></a>
## Session 42 — what the loco will not act on

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

## The three notes

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

### Notes are additions

The fields stay exactly as they were. What is on the wire is what an
acceptance test reports on, so a note that quietly replaced the decode would
be a bad trade — there is a check for that.

### And nothing fires when there is nothing to say

`TSR_STATUS == 2` produces no note, because the entries are read and a line on
every packet is noise rather than information. A real `REF_PROF_ID` likewise.
An ARP gets none of it at all.

---

## What is deliberately NOT noted

The loco also drops any SSP, gradient or TSR entry whose absolute location
computes to **zero or less**. That would be a useful note and it cannot be
made honestly: the calculation needs the reference tag's absolute location,
which lives in the loco's tag queue, not in anything on the wire. A note
derived from a guessed tag table would be confidently wrong every time the
table differed, which is worse than saying nothing.

The reasons that survived are the ones judgeable from the frame alone.

---

## A small lesson from the test

The first version of the test's helper matched note rows by the prefix
`"loco "`. It also caught the ARP's own `loco` field, and reported a note on a
packet that has none — a test failure caused entirely by the test.

Matching the two exact labels fixed it, and the reason is written next to the
helper: a prefix match over a field-name space you do not control will one day
catch a field somebody else added.

Full suite: **109 suites, 2761 checks, 0 failed.** Menu audit passed. Headless
smoke run clean.

---

## Pinning a field from any packet

Asked for: pin anything from the capture packets and watch its value.

Most of it was already there — the chooser lists what the current tab has
carried, then **every field the schema knows**, across all packets and structs,
509 of them. The second group is the useful one: the field worth watching is
often one that has not arrived yet.

What was missing is the reason that list is dangerous.

### The same name in five packets

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

### Pinning from a row narrows to what you clicked

Right-clicking a field in the inspector now pins it to that frame's **packet
and** source. The operator is pointing at a field in one packet of one source;
a pin that then took the same field name from a different packet would be
answering a question they did not ask.

### Old pins still load

Pins are stored as `field · source · packet`. A two-part line from the
previous build loads as "any packet", which is exactly what it meant when it
was written.

### Tests

Nine more checks: that two pins on `FRAME_NUM` narrowed to different packets
are two pins and not a duplicate, that each sees only its own packet, and that
their values differ — which is the whole reason for separating them. Plus the
unnarrowed behaviour, pinned deliberately so it is a choice on record rather
than a surprise, and the two-part round trip.

Full suite: **109 suites, 2770 checks, 0 failed.** Menu audit passed. Headless
smoke run clean.

---

<a id="session-41"></a>
## Session 41 — pinned fields

| File | Change |
|---|---|
| `pinboard.{h,cpp}` | new: what a pin is, and how it follows traffic |
| `pinpanel.{h,cpp}` | new: the dock |
| `mainwindow.cpp` | the dock, and one call on the ingest path |
| `settings.h` | pins survive a restart |
| `tests/test_pinboard.cpp` | new: 30 checks |

Watching one field meant finding it again in every frame. LOCO_MODE is
somewhere in the middle of an LSRP; to see it change you scroll, decode, read,
and three more frames have arrived. The value was decoded already — the only
reason it was hard to watch is that nothing held it still.

---

## What a pin shows

Four columns, because the useful reading is all four at once:

| Field | Now | Was | Changed |
|---|---|---|---|
| LOCO_MODE · 1_1 | 2 (Staff Responsible) | 1 (Stand By) | 12 s |

**"Was" is half the point.** A value on its own says less than a value with
what it used to be and when it stopped being that. And the age matters even
when nothing changed: a value with no age beside it cannot be told apart from
one that stopped arriving ten minutes ago, which is the difference between
"the loco is in Stand By" and "the loco stopped talking while in Stand By".

Values are the **display** strings, so an enum reads as its name rather than
the digit behind it — the reason to look at the field in the first place.

### Narrowing to a source

A pin can be tied to one tab key. With two locos on air an unnarrowed pin
shows whichever spoke last, which is a reading of nothing. Narrowed, another
source's value for the same field cannot overwrite it, and is not even counted
as a sighting.

Unnarrowed pins record which source supplied the current value, so a reading
can be traced back; double-clicking one switches to that tab.

### Never seen, which is not zero

A pin that no frame has carried says `not seen yet` rather than showing blank
or nothing. It usually means the field is spelled differently or lives in a
packet that source does not send, and the tooltip says so. Blank would have
been indistinguishable from a field whose value is empty.

---

## Sampled, and it says so

Pins are updated from the **last entry of each delivered batch**, not from
every entry.

Decoding every arriving frame for every pin would put schema decoding on the
ingest path, which is the one place in this program that must not get slower —
the batching in `onEntriesAppended` exists for exactly that reason, and this
would have undone it.

What the sampling costs, stated rather than left to be discovered: a field
that changes and changes back within one batch shows neither transition. For
the fields anyone pins — a mode, a location, a status word — that is invisible.
For a field toggling every frame it would be wrong. The comment on
`PinBoard::observe` says this where someone will read it before relying on it.

The panel also does no work at all when it is closed or empty, which is the
default: the dock is opt-in from the Panels menu.

---

## Tests

30 checks on the parts that go wrong quietly rather than loudly:

- the first sighting is **not** a change — there was nothing to change from,
  and "changed 0 s ago" is a claim an operator acts on
- the same value again is not a change either, but the last-seen time still
  moves, which is what tells a live value from a stalled one
- a narrowed pin ignores other sources completely
- a field no frame carries stays `seen == 0` with no value, distinct from zero
- pins round-trip through settings, and blank lines in a hand-edited ini do
  not become half a pin

Full suite: **106 suites, 2727 checks, 0 failed.** Menu audit passed. Headless
smoke run clean.

---

## Not done

- **Pin from the decode panel.** Right-clicking a field row and choosing "pin"
  is how anyone would expect to do this; the field chooser is the long way
  round. It needs a context menu in two other windows, so it is its own change.
- **Pin history.** The plot already knows how to walk a field across a model.
  A pinned field and a plotted field are the same field asked about over two
  different spans, and they do not know about each other yet.

---

## Pinning from where you are already pointing

Both follow-ons from the first cut, done.

### Right-click a decoded field

The field inspector's rows now carry a context menu — **Pin `LOCO_MODE`**,
**Plot `LOCO_MODE` over time**, **Copy value**. The actions are named with the
field in them, because a bare "Pin" beside a table of forty rows leaves the
operator checking which row they were on.

The inspector **raises a request rather than acting on it**. It knows which
field was clicked and nothing else — not which tab it came from, not where the
pin board lives, not whether the plot wants a model. Opening windows is
MainWindow's job everywhere else in this program and there was no reason for
it to stop being here.

**A pin made this way is narrowed to the tab it was clicked in.** An operator
pointing at a value in one source's frame means *that* source's value; an
unnarrowed pin would then start showing whichever loco spoke last, which is
not what they asked for and would look like the pin was broken. The dock is
also shown and raised, because a pin that lands in a hidden panel is a click
that appeared to do nothing.

### A pinned field and a plotted field are the same field

They were two features that did not know about each other — one asking what a
field says now, the other what it said over the session. Right-clicking a pin
offers **Plot over time**, against the source the pin is following rather than
whichever tab happens to be in front.

`FieldPlotWindow::plotField()` opens straight onto a named field. A field the
chooser has never listed is **added and selected**, not refused: the list is
what the first few hundred rows happened to carry, which is not the same as
what the session contains. Silently plotting whatever the chooser was on
instead would have drawn the wrong field with nothing saying so — which is the
failure this codebase keeps finding in other forms.

### Tests

`pinfromrow`, 7 checks: the inspector raises both requests and its table
actually answers a right-click — without that policy the menu never appears
and the whole path is unreachable while every other check still passes. Then
that the plot opens on the field it was given, including one the sample never
saw.

Full suite: **107 suites, 2734 checks, 0 failed.** Menu audit passed. Headless
smoke run clean.

---

## The panel floats

Asked for after seeing it docked: make it a window.

```cpp
m_pinDock->setFloating(true);
m_pinDock->resize(420, 260);
```

A pinned field is watched **while something else is being done** — a run driven
from the DMI, another window in front — so the one place it must not be is
folded into the side of the window whose log is being scrolled. Floating, it
can sit on a second screen, or beside the console rather than inside it.

It is still a `QDockWidget`, so dragging it back into the window works and
`restoreState()` remembers that choice for the next session. Floating is a
default, not a decision made on the operator's behalf.

**Still hidden until asked for.** An empty panel was not worth the width; an
empty *window* is worth even less. It opens from View → Panels → Pinned
fields, or by right-clicking a field and pinning it, which shows and raises it.

**Placed beside the console on first show**, not at whatever corner Qt picks.
On first show rather than at construction, because the main window has no
final geometry during construction — `restoreLayout()` has not run — so
anything computed then would be measured against the wrong rectangle. After
that it is the operator's to move.

### One thing that behaves differently now

Pins only update while the panel is visible: the ingest hook checks
`isVisible()` before decoding anything, so a closed panel costs nothing. That
was true when it was a dock and is still true as a window. A pin left set up
with the window closed will show what arrives after it is reopened, not a gap
filled in retrospectively.

### Where these are checked

In the **menu audit**, not `dltests`: MainWindow is not in the test binary, and
every one of these is a property of how the window sets the dock up. Four
checks — it exists, it floats, it is not shown unasked, and it can still be
docked.

Full suite: **107 suites, 2734 checks, 0 failed.** Menu audit passed, now 5
checks longer. Headless smoke run clean.

---

<a id="session-40"></a>
## Session 40 — the equipment clock

| File | Change |
|---|---|
| `frameclock.{h,cpp}` | new: FRAME_NUM ↔ time of day, skew, and the accept window |
| `mainwindow.cpp` | a status-bar readout, updated per frame and once a second |
| `tests/test_frameclock.cpp` | new: 29 checks |
| `tests/menuaudit.pro` | the new source, which it had been linking without |

---

## The relationship, confirmed rather than assumed

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

## Why a readout and not just a conversion

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

### The window is asymmetric, and the code says so

```c
if (TimeDiffInSeconds(pkt, own) <= -2 || TimeDiffInSeconds(pkt, own) > 4)
    reject
```

Four seconds late is accepted; two seconds early is not. A laptop running fast
is therefore rejected sooner than one running slow, and the boundaries are not
inclusive in the same direction — 4 still passes, −2 already fails. Both edges
are pinned in the tests, because an off-by-one here would mean the readout
says "fine" about traffic that is being dropped.

### Midnight

Skew takes the shorter way round the clock face, so 00:00:01 against 23:59:59
reads as +2 s rather than most of a day. Without that the readout would turn
red at exactly the moment it should be quietest.

A FRAME_NUM that is not a time of day at all — 0, or anything past 86400 — is
reported as unreadable rather than wrapped into yesterday.

---

## One thing found on the way

`tests/menuaudit.pro` had been listing sources by hand and did not know about
`frameclock.cpp`, so the audit stopped linking. It builds and passes again, and
the file is listed now.

Full suite: **105 suites, 2687 checks, 0 failed.** Menu audit passed. Headless
smoke run clean.

---

## Not done

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

## Both clocks

Asked for after the first cut: show the station's frame number as well as the
loco's.

The status bar now carries three readings:

```
Loco 15:02:31 (+1s)    Stn 15:02:30 (0s)    +1s apart
```

Each side is painted by the same function, so the two are presented
identically — the reason for having both is the moment they stop agreeing, and
that is only visible if they look the same when they do agree.

### The third number is the one that matters

`+1s apart` is the loco's counter minus the station's, and it is the
difference the two ends actually check each other on. Each end's gap to *this
laptop* is a diagnostic for DLConsole's own transmissions; the gap between the
two ends is a diagnostic for the system under test, and this laptop is not
part of that system at all.

It is spelled out rather than left to be worked out from the two times,
because subtracting two clock readings at a glance is exactly the sort of
thing that gets a test signed off wrongly. Midnight is wrapped the same way as
the skew, so 00:00:01 against 23:59:59 reads as 2 s apart.

### Kept apart on purpose

`FrameNumberWatch::latest()` still means the **loco's** counter — arp and
lsrp — and the station's lives in `latestStation()`. They are not merged, for
two reasons: the Packet Maker seeds built frames from `latest()`, and a
station number there would build a loco frame the loco rejects; and one merged
"latest" would hide the disagreement that is the whole point of showing both.

`latestFor(locoId)` likewise stays loco-only — an SLRP addressed *to* a loco
does not answer "what is that loco counting".

### A stale assumption in the tests

`test_framenumwatch` had SLRP standing in as its example of "a packet without
a frame number". It never was one: SLRP carries the station's counter in its
header, it simply was not being watched. The check now uses an RFID frame,
which genuinely has none, and asserts that SLRP *does* announce.

Full suite: **105 suites, 2697 checks, 0 failed.** Menu audit passed. Headless
smoke run clean.

---

<a id="session-39"></a>
## Session 39 — turnout entries are fixed width

| File | Change |
|---|---|
| `schema/kavach.xml` | turnout `start`/`release` no longer conditional; LC id 0 named |
| `schema/SLRP_FIELD_RULES.md` | new: the whole SLRP field map, from the loco firmware |
| `tests/test_turnoutwidth.cpp` | new: 11 checks |

From reviewing `fly.c`, the loco's own receiver for the regular packet.

---

## What the firmware says

The schema was right about almost everything: the 13-byte header and its ten
fields, sub-packet framing including the 8-byte CRC+MAC tail reserve, Movement
Authority with all four of its conditional fields, SSP, LC gate, track
condition, TSR, and tag linking — field for field and width for width.
Segment-length versus reference-offset is right in every case too.

One thing was wrong.

```xml
<!-- before -->
<field name="start"   bits="15" when="speed in 1..18"/>
<field name="release" bits="12" when="speed in 1..18"/>
```

`ProcessTurnout` consumes those two fields for **every** entry. Both of its
branches advance the cursor by `TO_DIFF_DIST_BITS + TO_REL_DIST_BITS` — one
reads the pair, the other skips it — so a turnout entry is always 5 + 15 + 12
bits whatever the speed.

Under the conditional reading an entry with a speed outside 1..18 was 27 bits
short, and **every entry after it in the sub-packet** was read from the wrong
bit.

### Why no capture ever showed it

All five turnout sub-packets in `replay/` carry speed 6 entries only, which is
inside 1..18, where the two readings agree exactly. Re-checked after the
change: all five decode identically to before.

That is the useful shape of this bug. It was not that the captures disagreed
with the schema — it is that the captures could not tell, and would not have
told us until a turnout with an unrestricted speed arrived during an
acceptance run.

So the test constructs the frame the traffic has not yet produced: an
out-of-range speed FIRST, then a normal entry after it. Under the old rule six
of its eleven checks fail; under the new rule none do. A test that passes
either way would have proved nothing, so that was checked both ways.

---

## LC entry with id 0

`ProcessLCGate` consumes such an entry's bits and skips it — it is a
placeholder, not a level crossing numbered 0. It now renders as
`0 (unused slot)` rather than `id=0`.

Track condition already reads correctly: type 0 maps to `0 (Not used)`.

---

## The reference

`schema/SLRP_FIELD_RULES.md` now holds the whole thing next to the schema it
justifies: framing, the acceptance rules a packet has to survive before any
field is used, the geometry that turns a distance into a location, and the
field layout of all eight sub-packets.

Two parts of it are worth knowing even where the schema is already right:

- **Segment length versus reference offset.** SSP and gradient entries CHAIN —
  each distance is the length of that segment, and entry *n* starts where
  *n−1* ended. LC, track condition, TSR and turnout are offsets from the
  reference tag. Same 15-bit field, different meaning.
- **What the loco discards.** Entries whose computed start or end comes out
  ≤ 0 are dropped; TSR entries are only acted on when `TSR_STATUS == 2`; and
  the placeholder entries above. A frame can be perfectly valid and still have
  most of its content ignored.

---

## For the firmware team, not for us

`ProcessTurnout`:

```c
uint8_t to_restricted = to_speed != 31;
uint8_t to_invalid    = to_speed != 0;

if (to_invalid) { /* skip entry */ continue; }
if (to_restricted) { /* read DIFF_DIST and REL_DIST */ }
```

`to_invalid` is true for every speed except 0, so only an entry with **speed
0** is ever acted on, and the branch that applies a turnout restriction is
unreachable for anything else. The captured traffic carries speed 6 turnouts,
which this discards.

It does not change the layout conclusion — the cursor advances identically
either way — but it may mean turnout speed restrictions are not reaching the
loco at all.

Full suite: **104 suites, 2658 checks, 0 failed.** Menu audit passed. Headless
smoke run clean.

---

<a id="session-38"></a>
## Session 38 — tabs: smaller, unbolded, selected by fill

| File | Change |
|---|---|
| `uistyle.cpp` | tab font a point smaller and normal weight; selection is a background fill |
| `tests/test_tabmetrics.cpp` | 32 checks, including the eleven-tab case that broke it |

---

## The photograph settled it

Every tab lost characters at **both** ends — `Fault_L1V1` as `ault_L1V`,
`L1_V1` as `_1_V1` — and not only the selected one. That is not eliding, which
would show an ellipsis, and not the close cross, which would only eat the
right. It is a tab bar drawing a centred label into a rect too small for it.

Which is what a `QTabBar` does when it cannot fit its tabs: it **shrinks**
them. And it could not fit them because the console had just been reopened and
brought back **eleven saved tabs at once**.

So the fault was never really about the selected tab. It was about width, and
every fix so far had been spending width rather than saving it.

---

## What changed

Both as you asked:

```css
QTabBar::tab           { font-size:<base − 1>pt; font-weight:normal; padding:5px 24px 5px 10px; }
QTabBar::tab:selected  { background:<fill>; color:<text>; border-bottom:2px solid <accent>; }
```

**A point smaller.** The tab bar is the one place where the number of items is
decided by how many sources are open rather than by design, so it is the one
place worth buying width back with size. Floored at 8pt — a label too small to
read is a worse trade than one that does not quite fit.

**Normal weight, filled selection.** A filled tab among unfilled ones is at
least as findable as a bold word among plain ones, and unlike weight it costs
no width. The accent underline stays, so selection is said twice.

Measured with your eleven tab names:

| | before | now |
|---|---|---|
| width for eleven tabs | did not fit | **1308px** |
| tightest label's spare room | negative — that is the crop | **60px** |
| scroll arrows at 1900px | — | none |

At 1280px it scrolls, which is correct: at that point they genuinely do not
fit.

---

## The thread through patches 35–38

Every one of these came from the same decision, made in patch 30, that the
selected tab should be **heavier**:

| | what happened |
|---|---|
| 35 | bold label overran a rect measured unbolded → clipped |
| 35 | reserving the difference in C++ widened tabs but not the bar → arrows in a half-empty bar |
| 36 | those arrows were blank, a real separate fault in the sheet |
| 37 | levelled the weight, reserved the close cross in the sheet |
| 38 | the weight cue removed entirely, and a point of size with it |

The lesson is narrow and worth writing down: **a selection cue that changes
the label's width is a cue that has to be paid for in layout, and Qt will not
be told about it after the fact.** Colour and fill cost nothing. Weight and
size cost width, and the bill arrives somewhere else.

---

## Tests

32 checks. The ones that matter here:

- eleven tabs — a full workspace reopened — fit 1900px with no scrollers, and
  **no tab is narrower than the name it carries**, which is the exact state
  that drops characters off both ends
- selecting a tab does not change any tab's width
- the selected tab is filled differently from its neighbours by a margin wide
  enough to find at a glance, sampled from a render rather than asserted about
  the stylesheet
- the label sits clear of both edges of its tab
- a bar that really is too narrow scrolls, and its arrows have ink in them

Full suite: **102 suites, 2628 checks, 0 failed.** Menu audit passed. Headless
smoke run clean.

---

<a id="session-37"></a>
## Session 37 — arrows in a half-empty bar

| File | Change |
|---|---|
| `uistyle.cpp` | one font weight for every tab; the width arithmetic removed |
| `uistyle.h` | `useMeasuredTabs()` → `useTabTooltips()`, which is now all it does |
| five window files | renamed call |
| `tests/test_tabmetrics.cpp` | rewritten: 15 checks on the properties, not the pixels |

Reported: the scroll arrows appear with more than half the tab bar still
empty.

---

## It was patch 35, and it was mine

Reproduced here on the first try. Eight tabs, a 1900px window:

```
bar width 612    tabs laid out 644    →  arrows, and 1288px of nothing beside them
```

The bar had already been sized — 612px — before the tabs were widened to 644.
Widening a tab does not widen the bar that Qt has already measured, so the bar
concluded it was overflowing and scrolled, in a window nearly three times
wider than it needed.

I tried to close that by overriding the bar's `sizeHint()` to sum the widened
tabs. It changed nothing: the hint still came back 612 while the laid-out
rects still totalled 644. At that point I was reverse-engineering which of
Qt's several size paths `QStyleSheetStyle` was actually consulting, which is
not a position to ship a third fix from.

## So the gap is gone instead of managed

Every tab is now drawn at **one weight**:

```css
QTabBar::tab           { … font-weight:600; … }
QTabBar::tab:selected  { background:…; color:…; border-bottom:2px solid accent; }
```

What is measured is what is drawn, tab width goes back to being Qt's business,
and all three faults reported since patch 30 have the same root removed:

| Reported | Cause |
|---|---|
| `L1_V1` drew as `_1_V1` | bold label overran a rect measured unbolded; Qt centres tab text, so it was cut at both ends |
| arrows in a half-empty bar | the fix for that widened tabs but not the bar |
| the arrows were blank boxes | the sheet's generic `QToolButton` rule styled them, and a styled tool button loses its arrow |

The third was a real, separate fault in the sheet and its fix from patch 36
stays. The first two were one mistake, made twice.

### What the selected tab lost, and what it kept

It no longer stands out by weight. It still stands out by **background**, by
**text colour**, and by a **2px accent underline** — which, looking at the
photographs, is what was doing most of the work anyway.

That is the trade: a slightly quieter selection cue, in exchange for measured
and drawn agreeing. Patch 30 was right that eleven tabs need a clear cue. It
was wrong to spend the one cue that changes the label's width.

---

## Verified across three widths

| Window | Bar | Scrollers | Selected label |
|---|---|---|---|
| 1900px | 580 | none | 12px clear of both edges |
| 700px | 580 | none | 12px clear |
| 300px | 300 | two, with arrows drawn | 12px clear |

Scrolling now happens when, and only when, the tabs genuinely do not fit.

---

## Tests

Rewritten around what actually matters, 15 checks:

- eight tabs need less than the window, the bar asks for at least the room its
  tabs occupy, and **no arrows are shown** — the regression guard
- the selected label is wider than nothing and touches neither edge of its
  tab, measured from a render rather than from arithmetic
- selecting a tab does not change any tab's width, which is the invariant that
  keeps measured and drawn together
- a bar that really is too narrow does scroll, and each arrow has ink in it
- the full name is on every tooltip, and follows a rename

The old suite tested that the bar reserved a specific extra width. That was a
test of the wrong fix, and it passed while the bug it caused was on screen.

---

## `Fault_L1_V1` still cropped

Reported alongside the arrows, and a different fault from them. Not reproduced
here either: on this machine that tab gets 127px for a 113px label.

### What the pattern says

`Fault_L2V2` drew whole in the earlier photograph; `Fault_L1_V1` crops. Two
characters apart. That is the shape of a **fixed reserve that is missing**,
not of a shortfall that grows with the label: a short name survives on its
12px of padding, a long one runs into whatever sits at the right-hand end of
the tab and is cut.

What sits there is the **close cross**. Whether a stylesheet-styled
`::close-button` counts towards the tab's size hint is up to the platform
style, and the platform style is what differs between these machines — as it
did for the clipping and for the arrows.

### The reserve is stated, not computed

```css
QTabBar::tab { padding: 6px 28px 6px 12px; }   /* 12 padding + 4 margin + 12 cross */
```

Every tab gets 16px more room on its right than before, on every platform.

The important part is **where** it is stated. This is the same reserve that
patch 35 computed in C++ and applied to the laid-out tabs, which is what put
scroll arrows in a half-empty bar — the bar had already been sized without it.
In the stylesheet, sizing and drawing read the same rule, so they cannot come
apart. It is the same lesson as the font weight, applied to the other end of
the tab.

Nine tabs including `Fault_L1_V1` now want 851px, so a 1900px window still
shows them all without scrolling.

Three added checks: a closable tab is wider than its label by at least the
cross plus its padding, long name or short, and a longer name still gets a
proportionally wider tab.

**Still unverified on your machine**, like the two before it. If the crop
survives this, the thing that would settle it is whether the tab shows an
ellipsis or simply loses characters — an ellipsis means Qt knows it does not
fit and is eliding, missing characters means it thinks it fits and is drawing
past the edge. They have different causes and I have been guessing between
them.

Full suite: **102 suites, 2614 checks, 0 failed.** Menu audit passed.

---

<a id="session-36"></a>
## Session 36 — the two blank tabs

| File | Change |
|---|---|
| `uistyle.cpp` | a rule for the tab bar's scroll buttons |
| `tests/test_tabmetrics.cpp` | +3 checks: the arrows are drawn |

Reported after patch 35: the labels draw whole now, but *"I get these 2 blank
tabs at the end of the rest of the tabs."*

---

## They are not tabs

They are the tab bar's **scroll buttons**, which appear when the tabs no
longer fit. They are `QToolButton`s living inside the bar, so the sheet's
generic rule caught them:

```css
QPushButton, QToolButton {
  background:…; border:1px solid …; border-radius:6px;
  padding:5px 12px; }
```

Two things follow. Twelve pixels of horizontal padding on a sixteen-pixel-wide
button leaves nothing to draw into; and once a stylesheet has styled a tool
button, Qt stops drawing its arrow primitive altogether. What is left is a
bordered, rounded, empty box — which is precisely a blank tab.

Rendered here with the shipped sheet, the two scrollers contained **1 and 451**
ink pixels: one entirely empty, the other only its own border. With a rule of
their own they contain 35 and 99 — a disabled left arrow and an enabled right
one, which is what a bar scrolled to its start should show.

### Why now

Patch 35 made every tab wider by the bold-versus-normal difference, to stop
the selected label being clipped. That is the change that pushed the bar past
the window width, so the scrollers appeared for the first time.

So this is my doing, in the sense that it took a latent stylesheet fault and
made it visible. The fault has been in the sheet since the tab rules were
written; it needed an overflowing tab bar to show, and until last week the
tabs were narrow enough not to overflow.

---

## The fix

```css
QTabBar::scroller { width:32px; }
QTabBar QToolButton {
  background:…; color:…; border:1px solid …; border-radius:0px;
  padding:0px; margin:0px; min-width:16px; }
```

Zero padding and no radius, so the button is a plain square with room for its
arrow, and hover/pressed/disabled follow the same palette as every other
button.

Worth noting what was NOT done: the arrows are left to the platform style
rather than replaced with images of our own. An `image:` rule would have
guaranteed the same arrow everywhere, and would also mean shipping a pair of
PNGs that are wrong at every scale factor except the one they were drawn for.

---

## Tests

Three checks: the overflowing bar grows exactly two scroll buttons, and each
has ink drawn inside it.

**Ink, not appearance.** The fault was that nothing was drawn at all, so
counting pixels that differ from the button's own background is the property
that matters. Asserting *which* arrow, or how many pixels of it, would be a
test of the platform style rather than of the bug — and the platform style is
the thing that differs between my machine and yours, which is what made the
clipping in patch 35 so slow to pin down.

Full suite: **102 suites, 2609 checks, 0 failed.** Menu audit passed.
Headless smoke run clean.

---

## Patch 35, confirmed

The label fix worked: `L1_V1` draws whole in the photograph. Recorded here
because that patch shipped explicitly unverified — I could not reproduce the
clipping on Linux and said the fix was a reading of the code rather than a
tested one. It was the right reading.

---

<a id="session-35"></a>
## Session 35 — the clipped tab label

| File | Change |
|---|---|
| `uistyle.{h,cpp}` | `useMeasuredTabs()` — a tab bar measured for the weight it draws at, and tooltips carrying the full name |
| `mainwindow.cpp`, `lococonsolewindow.cpp`, `replaywindow.cpp`, `sessionwindow.cpp`, `dlrplayerdialog.cpp` | all five tab widgets use it |
| `tests/test_tabmetrics.cpp` | 10 checks |

From a photograph of the running console: the selected tab, whose source is
`L1_V1`, rendered as `_1_V1`. The tabs either side — `Fault_L2V2`, `L1_V2` —
drew whole.

---

## READ THIS FIRST: the fix is not proven

I could not reproduce the clipping on this machine. Rendered here, with the
program's own stylesheet, the selected tab measures 78px against a 51px label
and the ink sits 12px clear of each edge. Nothing is cut off.

That does not mean it is not happening on yours. It means the cause is
platform-dependent, and the most likely reason is below — but it is a reading
of the code, not a reproduction, and it should be treated as such until the
next build is looked at.

---

## What is wrong in the code regardless

Patch 30 gave the selected tab `font-weight:600`, deliberately: across a row
of eleven sources a 2px underline alone does not say which one you are
reading.

`QTabBar::tabSizeHint()` measures a label with the **widget's** font — the
normal weight. So every tab is reserved the width of its label unbolded, and
the selected one is then drawn bold into that rect. Qt centres tab text, so an
overrun is cut off at **both** ends — which is exactly the shape of the
reported fault, a leading character lost rather than a trailing ellipsis.

Whether `QStyleSheetStyle` compensates for this depends on the platform style,
and that is where the two of us differ: this is Linux and Fusion, yours is
Windows. It is also where the font differs, and Segoe UI bolds wider than what
is installed here.

`useMeasuredTabs()` installs a tab bar that adds the demi-bold-minus-normal
difference to every tab's width. Every tab, not only the current one —
reserving it per-tab would make the whole bar shuffle sideways on each
selection change, which is a worse fault than the one being fixed. Measured
here it costs 5px a tab.

## The part that works whatever the cause

Every tab now carries its full name as a **tooltip**. A tab that cannot be
fully drawn can still be identified, and that holds whether the cause is the
weight, the font, the style, or something not yet thought of.

Tab text arrives after insertion — a source tab is named when its first
message arrives — and QTabBar has no signal for it. `QEvent::LayoutRequest`
looked like the tidier hook and simply never arrives; `tabLayoutChange()` and
`tabInserted()` between them do.

### Eliding was tried and taken back out

Setting `Qt::ElideRight` would make an over-long label end in an ellipsis
instead of losing a character, which sounds like exactly the right guard. It
also replaces the tab bar's scroll behaviour with shrink-and-ellipsis for
**every** tab at once, so eleven sources become eleven `L1_…`. That is a
bigger change than the fault it guards against and nobody asked for it. It
broke two width checks on the way, which is how it got noticed.

---

## The other possibility, which needs you

If the label is genuinely `_1_V1` — not clipped, just named that — then none
of the above is the fault, and the fault is in the names file or in what
built it. Two things tell them apart in the next build:

1. **Hover the tab.** The tooltip is the untouched string. If it reads
   `_1_V1`, the name is wrong and the rendering is fine.
2. **Widen the window.** A clipped label gains its missing character; a badly
   named one does not.

I would rather say this than ship a confident fix for the wrong fault, having
already shipped one confident fix for this that did not hold.

---

## Tests

`tabmetrics`, 10 checks: that the measured bar reserves the whole
bold-versus-normal difference, that the label therefore fits at the weight it
is drawn, that identical labels get identical widths whichever is selected,
that a rename is re-measured and re-tooltipped, and that calling the helper
after tabs exist declines rather than replacing the bar and dropping them.

They test the property — the label fits — not a pixel count, which would be a
test of this machine's font rather than of the bug.

Full suite: **102 suites, 2606 checks, 0 failed.** Menu audit passed. Headless
smoke run clean.

---

<a id="session-34"></a>
## Session 34 — the last three status lines

| File | Change |
|---|---|
| `dlrplayerdialog.{h,cpp}` | 9 messages moved onto StatusLine |
| `framediffwindow.{h,cpp}` | 4 messages, all captions |
| `fieldplot.{h,cpp}` | 2 messages, one of them a real warning |
| `tests/test_statusconventions.cpp` | new: 14 checks, plus the audit of what was left |

Open since patch 30: *feedback consistency — 35 status labels across 13 files.
Shared colours now, but each still has its own wording, placement and
lifetime.*

`StatusLine` was written to settle it and eight surfaces adopted it. Three did
not, and they turned out to be the three with the most to gain.

---

## The rule, restated

Four verbs for events and one for a caption:

| | | |
|---|---|---|
| `say()` | neutral progress | transient |
| `ok()` | it worked | transient |
| `warn()` | it worked, but read this | **sticky** |
| `fail()` | it did not work | **sticky** |
| `state()` | what this panel is showing | **sticky** |

Successes expire because a stale success describes a state that has passed —
"Sent 40 frames" ten minutes later reads as current. Failures do not expire,
because the operator decides when they have read them. Captions do not expire
because they are not news.

---

## The replay player

Nine messages, and the interesting one is this:

```cpp
if (m_status->text().startsWith(tr("Seek query never matched"))) {
    // Leave the more informative message in place.
} else {
    m_status->setText(completed ? tr("Finished.") : tr("Stopped."));
}
```

The intent is exactly right — do not let *"Finished."* paper over the reason
the run sent nothing — and the mechanism is a string comparison against the
sentence it is trying to protect. It survives precisely as long as nobody
rewords that sentence, and the thing that would break it is a translation or a
tidy-up, neither of which would look like a change to replay behaviour.

It is now `if (!m_status->isSticky())`. The property is asked about directly
instead of being inferred from the text, which is the whole reason a failure
knows it is a failure.

The rest divide as you would expect once there is a vocabulary to divide them
into: a bad seek query and an exhausted seek are `fail()`; problems found
while loading a session and *"Press Escape again to close"* are `warn()`,
because both are things the operator must read before the next action;
*"Skipping…"* and *"Finished."* are `say()`; a landed seek is `ok()`.

## The frame diff window

All four of its messages are captions, including the results. *"3 frames · 7
fields differ"* describes the table underneath, not something that just
happened, so it is `state()` — sticky, and not coloured as a success.
*"No field differences"* is a finding, not a win.

## The field plot

Its warning existed only as a stylesheet swap:

```cpp
m_status->setStyleSheet(s.rowsNonNumeric > 0 ? UiColor::warningStyle()
                                             : UiColor::mutedStyle());
```

Colour alone is not a signal — a quarter of the contrast work in this program
exists because text was unreadable, and to a colour-blind reader an amber
sentence and a grey one are the same sentence. `warn()` gives it a glyph as
well. Discarded rows are worth the warning: the plot then shows fewer points
than the tab holds, which reads as missing data rather than as a decision.

---

## What was NOT converted, and why

This is the part worth keeping, because the question will come round again.

A status line reports an **event**. The labels below describe **standing
state** or are fixed captions, and giving them expiry and a glyph would be
wrong in both directions: state that expires is worse than no state, and a
tick beside a permanent readout says something just succeeded when nothing
did.

| Where | What it is |
|---|---|
| `mainwindow` bind / disk status | the socket is bound or it is not — true until it changes |
| `filterbar` count | a count of what is showing, recomputed per filter |
| `findbar` count, wrap hint, hold hint | a live readout of the current search |
| `lococonsole` CRC / seq | per-source health indicators |
| `settingsdialog`, `gototimestamp` hints | fixed captions in a form |
| `comparewindow` time label | the pane's current timestamp |

Two sit on the line and were left alone **deliberately**, not by oversight:
`sessionkeydialog`'s explanation and `sessionwindow`'s summary both report the
result of an action *and* describe the state the dialog is now in, and both
are the only text in their panel. Converting them means deciding whether "no
key set loaded" is a failure or a caption, and that is a question about the
workflow rather than about labels.

The audit lives in a comment at the foot of `test_statusconventions.cpp`, next
to the tests, so the next person to ask "why is that one different" finds the
answer where they are already looking.

---

## Tests

`statusconventions`, 14 checks: the five verbs and their lifetimes, that the
displayed text carries a glyph as well as the message, that a failure is not
overwritten by the routine message that follows it, and that the frame diff
window opens with a caption rather than an event.

They pin the **rule**, not the wording. Wording changes; the rule is what an
operator learns.

Full suite: **101 suites, 2596 checks, 0 failed.** Menu audit passed.
Headless smoke run clean.

---

<a id="session-33"></a>
## Session 33 — Find in a compare pane

| File | Change |
|---|---|
| `findbar.{h,cpp}` | the bar follows the view's current model; `refreshModelBinding()` |
| `comparewindow.cpp` | tells its find bar when a pane is rebound |
| `tests/test_comparefind.cpp` | new: 11 checks |

Reported: *the console crashes as soon as I write anything in the find field
in a compare tab.*

---

## The crash

It was real, it was in patch 30, and the patch 31 rewrite of `scanMatches()`
had already removed it — so this session is the test that proves it and stops
it coming back, plus a second fault the reproduction turned up.

The old scan:

```cpp
auto *sortProxy = qobject_cast<QSortFilterProxyModel*>(proxy);
const LogModel *lm = sortProxy ? qobject_cast<const LogModel*>(sortProxy->sourceModel())
                               : qobject_cast<const LogModel*>(proxy);   // bound directly
...
} else if (lm && colSel == LogModel::ColMessage) {
    const QModelIndex sIdx = sortProxy->mapToSource(sortProxy->index(r, 0));
```

The `lm` line handles a view with no proxy. The line that uses it does not.
A **compare pane binds its LogModel straight to the view** — that is why the
window exists in the shape it does, and half a dozen places cast
`view->model()` to `LogModel` — so `sortProxy` is null, `lm` is not, and the
default column is Message. Every branch that could run dereferenced null.
First keystroke, every time. The query branch had the same line.

The rewrite in patch 31 routed both through `entryAtProxyRow()`, which was
already handling proxy and no-proxy because the compare window needed that
when its find bars were added. The crash went with it — silently, which is
its own small lesson: a fix nobody knew they were making is a fix nobody
wrote a test for.

There is a test now. It builds a real `CompareWindow`, binds a real source,
types, and drives every search mode against a model with no proxy. Reaching
the end of the suite is most of the check — a crash takes the process, not
the assertion.

---

## What the reproduction turned up

A compare pane's view is built **empty**. It is given a model when a source is
picked, and a different one every time the operator changes that pick. The
find bar wires its model-change connections in its constructor, which for a
log tab is fine — the tab's model exists first — and for a compare pane means
wiring to nothing at all.

Two consequences, both live until this patch:

- **Find never re-scanned.** New traffic arriving in the pane did not reach
  the bar, so the count sat at whatever it first reported while the log grew
  underneath it. Retyping the query was the only way to refresh it.
- **A source switch left the old matches in place.** Match positions are row
  numbers, and a row number means something different once the view is showing
  another model. Navigating them took the operator to unrelated messages.

`ensureModelWiring()` re-points the connections at whatever the view shows now
and drops the match set when the model underneath actually changed. It is a
pointer comparison, so it is called on every scan and on `activate()` without
being worth thinking about.

### The one thing Qt would not tell us

`QAbstractItemView::setModel()` emits nothing. A widget cannot notice that the
view it watches has been handed a different model, and polling for it would
mean a timer running forever against something that changes a handful of times
a session.

So the compare window says so: `rebindPane()` already re-wires the scroll-bar
and selection connections after `setModel()`, since the selection model is
invalidated there, and it now tells the find bar too. One call, in the one
place in the program that swaps a model under a view that is already on
screen.

---

## Tests

`comparefind`, 11 checks: that a pane binds a LogModel with no proxy at all,
that typing into it finds what is there, that all five modes survive it —
including Hex reading `rawBytes` and a query evaluating fields, the two that
need the entry rather than a rendered cell — that a message arriving is picked
up without retyping, and that switching the pane's source empties the match
set rather than carrying row numbers across.

Full suite: **100 suites, 2574 checks, 0 failed.** Menu audit passed.
Headless smoke run clean.

---

## Also this session

Received LSRP: **dropped**, on your word that it does not arrive. The decode
path still handles it if it ever does — the header size comes from the frame
either way — but nothing has been built on the assumption that it will.

---

## The Packet Maker's two ARP entries

Left open at the end of patch 32: whether a built `arprecv` should carry the
8-byte received shape and a built `arp` the 10-byte transmitted one, since
today both come out identical.

The answer is in what the program is. **DLConsole is always the peer
transmitting to the loco**, so what it sends is by definition what the loco
receives — and the received form is the 8-byte header it already builds, with
the default `src=7 dest=2` that the real receive buffer shows as `07 02`. The
10-byte form with the station id belongs to a loco's own radio, which this
program does not emit. So there is nothing to change: both entries are
correct, and identical for a reason.

What was missing is that nothing said so. Two combo entries producing
byte-identical dumps reads as a bug, and the way that gets resolved is an
operator diffing two hex dumps to find out. `MessageHeader::shapeNote()` now
says it once, in the preview, under the header line:

> this is the RECEIVED form: 8-byte header, no station id. A loco's own
> transmission carries a 2-byte station id and a 10-byte header, which
> DLConsole does not send.

The test pins the identity rather than a difference, with the reason attached,
so that nobody later "fixes" it into putting a station id on a frame the loco
is meant to receive without one.

Full suite: **100 suites, 2582 checks, 0 failed.**

---

<a id="session-32"></a>
## Session 32 — the ARP a loco receives from another loco

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

## The header is not a fixed size, and the frame says which it is

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

### What the CRC was checking

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

### Re-verified, not assumed

Every ARP and LSRP frame in `replay/`: **9207 + 3253 = 12,460**, all resolve to
header 10 and give the identical CRC verdict under the new rule and the old.
The received buffer resolves to 8, and cannot verify at 10 — its bytes 8..9 are
`D3 AC`, so unlike the 1840 frames with zeroes there, it discriminates. (The
CRC init is 0, which is why zero-prefixed frames pass at either offset and are
no evidence for one.)

---

## @arprecv

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

### One packet, two names

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

### What deliberately did NOT change

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

## Three fixtures that were never realistic

`test_assertions`, `test_fieldquery` and `test_fieldcatalog` built synthetic
LSRP frames as 64 zero bytes with the body written at bit 80 — both length
fields left at zero. They decoded only because the offset was assumed. Once
the decoder reads the frame's own lengths, they stopped, and 31 checks failed.

The fixtures were fixed, not the decoder loosened. A test frame that no loco
would send is a test of something no loco does.

---

## Tests

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

## Open

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

---

<a id="session-31"></a>
## Session 31 — Find: extended search, and a window instead of a strip

| File | Change |
|---|---|
| `findbar.{h,cpp}` | search modes, extended escapes, hex-byte search, two layouts |
| `logquery.{h,cpp}` | `parseHexPattern()` — one strict parser, shared with `hex:` |
| `settings.h` | find mode, wrap, scan limit, advanced-open |
| `tests/test_findmodes.cpp` | new: 6 suites, 91 checks |
| `tests/test_findextras.cpp` | controls looked up by objectName, not by label |
| `tests/tests.pro` | new file |

Two things were asked for: the extended search mode left open at the end of
patch 30, and a real window in place of the one-line strip.

---

## Five checkboxes were five answers to one question

Find had `Aa`, `W`, `.*` and `Query` as independent boxes, and the scan
resolved the contradictions at the moment it ran — `m_regexCb->setEnabled(!m_queryMode)`
and two more like it, buried inside `scanMatches()`. Ticking Query and regex
together was possible; which one won was decided by a line nobody reading the
UI could see, and the only clue was a control greying out after the fact.

They are now a **mode**, because they are five ways of asking the same
question:

| Mode | What the text means |
|---|---|
| Text | plain substring, honouring Case and Whole word |
| Extended | the same, after `\n \r \t \0 \xHH \uHHHH \\` are turned into what they name |
| Regular expression | a pattern the operator writes |
| Hex bytes | bytes of the datagram — `0a 1b`, `0A:1B`, `0a1b` are the same |
| Query | the boolean language, identical to the filter bar's |

`syncModeControls()` decides once per mode change which controls still mean
something, and a help line under the selector says what the mode accepts.
That line is the actual fix for discoverability: the query language was
previously findable only by hovering a checkbox and reading a tooltip.

### Extended mode refuses escapes it does not know

`\s` in extended mode searches for a backslash and an `s`, finds nothing, and
says nothing about why — that is what most editors do. Here an unrecognised
escape is an **error**, named, with the offset marked in the input box:

> `\s is not an escape this mode knows — try \n \r \t \0 \xHH \uHHHH or \\`

Same for a trailing lone backslash, and for `\x` or `\u` with the wrong number
of digits. This is the same reasoning as the round-trip validator in patch 30:
in this program the failure that costs an afternoon is the silent one.

### Hex searches the frame, not the rendering

Hex mode matches against `rawBytes` — the datagram as it arrived — rather than
against whatever the Message column happens to render. So the column selector
is switched off in that mode, and, like Query, it needs a `LogModel` behind
the view to have anything to match against.

### One hex parser, not two

The query language already had `hex:`, and its parser accepted any
`isLetterOrNumber()` character before handing the result to
`QByteArray::fromHex()`. So `hex:0g1b` passed the check and became whatever
`fromHex` made of it, with no error — a different pattern from the one asked
for, no matches, and nothing on screen saying so.

`parseHexPattern()` now lives in `logquery.cpp` and is used by both `hex:` and
the Find window. It skips the separators an operator actually produces (space,
`:`, `-`, `,`, `.`, `_`), and anything else that is not a hex digit is an error
naming the character and its position. An odd digit count says so in the terms
the mistake was made in — *"bytes come in pairs of digits — 5 digits is one
short"* — because the cause is nearly always a byte typed as one digit, and
the operator needs to find the one they dropped rather than retype the line.

---

## The strip and the window are the same widget

Detaching already reparented the bar rather than building a second find UI,
and that stays: one set of state, one scan, one cursor. What changed is that
the two shapes no longer have to be the same shape.

**Docked** it is still one line above the table, where every pixel is spoken
for — short labels, glyph buttons, no advanced section.

**Undocked** it is a form:

- the input gets a **row of its own**. In the strip it was whatever width was
  left after nine controls had taken theirs, which for a query of any length
  meant typing into a slot
- mode and column on their own row, with the help line under them
- the options spelled out — *Case sensitive*, *Whole word only*,
  *Highlight all matches*, rather than `Aa`, `W`, `Mark`
- an **Advanced** section, closed by default, holding the two things that
  change how the search *behaves* rather than what it looks for

`applyShape()` moves the same widgets between the two layouts and is the only
place either arrangement is described. Labels are set there too, which is why
the tests now look controls up by `objectName` — matching on text would
quietly have become a test of which shape the bar opened in.

One consequence worth stating: a child widget left out of a layout keeps its
last geometry rather than disappearing, so everything the strip does not place
is hidden explicitly. There is a check for exactly that, because the failure
would be the advanced panel floating over the table.

### Advanced: wrap, and the scan limit

**Wrap around** was unconditional. It is still the default, but it can be
turned off, and the reason is particular to a log rather than to a text
editor: a recording is read in order to be reported on, and wrapping silently
is how the same three matches get read twice and written up as six. With it
off, Next stops at the last match and says `(last match)` rather than doing
nothing — a dead button reads as a bug.

**Scan limit** was a compiled-in 200,000 rows. The cap has a real purpose —
the scan runs on the GUI thread — but the number was a guess, and worse, the
count label quoted `kScanRowCap` whether or not that was the limit actually
used. It is now a choice (20k / 200k / 1M / every row), and the label quotes
the number the scan really walked.

### A regex that would not compile said nothing

`scanMatches()` had `if (!re.isValid()) return;` with a comment saying *"Bad
regex — show 0 matches but don't crash"*. The operator saw `no matches`, which
is the same thing a correct pattern with no hits says. Every mode now reports
its parse failure through the input box's existing error marking —
`QueryLineEdit` has carried an offset-aware error display since the query
language landed, and Regex, Extended and Hex now use it too.

---

## The bug this uncovered: a window parented to itself

`setDetached(true)` built its host as `new QDialog(window())`, then reparented
the bar **into** that host. For a bar with a parent widget that is fine. For a
bar with no parent of its own, `window()` returns *the bar itself* — so the
host's parent became the bar, and the bar's parent became the host.

A cycle in the widget tree. `QWidget::nativeParentWidget()` walks parents
until it finds a native window, and Qt calls it on every reparent, so it
never returns. The application hangs. It does not crash, which is worse: the
first run of the new `findwindow` suite sat at 100% CPU for eight minutes
producing no output at all, and it took a `gdb` stack to see that the top
frame was `nativeParentWidget` under `QBoxLayout::insertWidget`.

The host now anchors on **the view's window** — the console this bar searches,
which is where a find window belongs anyway — and falls back to no parent
rather than to anything that could be the bar.

This was reachable before this session and not reached, because the only bars
that detached had a parent widget. A test that constructed one without a
parent found it immediately.

---

## Tests

| Suite | Checks | What it pins |
|---|---|---|
| `findescapes` | 24 | every escape, and every way of writing one wrong |
| `findhexpattern` | 13 | separators, odd digits, non-hex, shared with `hex:` |
| `findmodes` | 23 | each mode finds what it should and ignores what it should |
| `findwindow` | 20 | wrap on and off, the shapes, the advanced section |
| `findscanlimit` | 7 | the cap is used and the label quotes the real number |
| `finddirectmodel` | 4 | modes needing an entry are quiet when there is no LogModel |

`test_findextras` keeps its 11 checks with the lookup changed.

Full suite: **97 suites, 2517 checks, 0 failed.** Menu audit passed
(23 shortcuts, 55 commands reachable). Headless smoke run clean.

---

## Not done

- **`\d` decimal and `\o` octal escapes.** Notepad++ has both. `\xHH` covers
  the case that comes up here, and three ways of writing the same byte is
  three things to get wrong.
- **Hex search with wildcards** (`0a ?? 2c`). Worth having, but it is a
  different matcher — a byte-mask scan rather than `QByteArray::contains` —
  and it belongs with the field-aware work rather than bolted to Find.
- **Searching rows the filter has hidden.** Find still searches what the
  filter leaves visible, which remains the right default; an "include
  filtered-out rows" option would mean scanning the source model and
  navigating to rows the view cannot show.

---

<a id="session-30"></a>
## DL Console — session changes (patch 30)

Round-trip validator — the third of the four tools. Can this program rebuild
what the equipment actually sent?

| File | Change |
|---|---|
| `roundtrip.{h,cpp}` | the check (pure), per-type tallies, the report, the worker |
| `roundtripwindow.{h,cpp}` | corpus picker, results table, field attribution, Frame Diff handoff |
| `schema/schemaencoder.cpp` | refuse packets the writer cannot emit instead of mis-emitting them |
| `mainwindow.{h,cpp}` | Tools → Round-trip Validator…, live-log snapshot, diff handoff |
| `tests/test_roundtrip.cpp` | two new suites (81 checks) |

### The question

Everything that transmits — Packet Maker, Field Sweep, and the station
responder to come — builds its frame with `Schema::Encoder::encodeBody()`. The
schema claims `encodeBody()` is the inverse of `parseBody()`, so for any
captured frame

    parseBody(bytes) -> encodeBody(...) == bytes

must hold. Where it does, a frame built here is the same shape as one the
equipment builds. Where it does not, anything built for that type is a guess —
and the failure is **silent**: a mis-encoded frame decodes back to the values
that produced it and carries a valid CRC over its own wrong bytes. Nothing in
the existing tooling would have told you.

### What it found

Over the 130,951 frames in `replay/`:

- **slrp, aap, arp, lsrp, nmsrssi — every frame reproduced byte for byte.**
  Those are the types that matter for transmitting, and they are clean.
- **dmi (10,658 frames) and linfo (10,636) reproduced none.** Both are
  `lsb-first`; the writer is msb-first only and packed them in the wrong bit
  order without complaining.
- **nmshlth** encoded to nothing and reported *no error at all* — its body is a
  single `<eventstream>`, an element the field walk did not recognise and
  silently skipped, leaving a packet with zero fields that "parsed" every
  buffer successfully.
- **rfid, ccsys, dlsys** carry inline `<crc>` elements, which consume bits the
  writer does not know how to produce.

### The refusals (your call, and it removes capability)

`Encoder::packet()` now reports as unsupported:

- any packet whose `wire` is not `msb-first`;
- any element inside a packet the field walk does not handle — `repeat`,
  `eventstream`, `crc`, and anything added later.

That second rule is the one that matters most. The old code named `repeat` and
silently ignored everything else, so an unrecognised element did not stop the
walk — it just made it short, and **every later field was written at the wrong
bit offset** while still producing a frame that decodes and CRCs.

Packet Maker and Field Sweep already surface `unsupported`, so both now decline
`dmi`, `linfo`, `rfid`, `ccsys` and `dlsys` with the reason, where before they
would build a frame. `ccsys` and `dlsys` had in fact been round-tripping — but
by accident of being all byte-aligned 8-bit fields, where the two bit orders
coincide. One 12-bit field added to either and the silence would have come
back.

After the refusals: **26,461 frames where the question applies, 26,461
reproduced.**

### The envelope trap

`parseBody()` reads from bit 0 of whatever it is handed. A captured arp/lsrp
frame carries a 10-byte in-capture envelope (`body_offset="80"`), so handing it
over whole reads the message header as `PKT_TYPE` — and does **not** fail. It
returns plausible values, gates LSRP's `FRAME_NUM & 7` health branch on the
wrong number, and rebuilds something close enough that 903 of 3253 frames
"matched". The validator strips `bodyOffsetBits/8`, and there is a test that
pins the misaligned parse succeeding with a different `L_DOUBTOVER`, so this
cannot be quietly reintroduced.

Worth a separate look: Packet Maker's *Fill from buffer* strips
`MessageHeader::SIZE` (8) rather than the 10 bytes `body_offset` describes.

### Reading the results

Per packet type: frames, reproduced, differ, not parsed, not built, CRC ok.

A type nothing can be built for shows **—** in the reproduced column, not 0. A
zero there would read as a failed check when the correct statement is that the
question was never asked; `notEncodable` is counted separately from `differs`
for the same reason, and `answerable()` excludes it.

Selecting a type names **the fields that own the bytes that differ** — the core
records "body byte 20", and the window turns that into `L_DOUBTOVER` using the
shared decoder. A byte no field claims is reported as unclaimed rather than
guessed at, which is itself a finding.

**Diff a failing frame against its rebuild** splices the rebuilt body back into
the captured envelope and sends both to Frame Diff, so "byte 20 differs" is
read out as a field. That is usually the whole diagnosis.

The report saves as plain text, with the schema file it used, the corpus, the
counts, and a closing paragraph saying what it does **not** prove: a field that
is mislabelled but the right width round-trips perfectly. This checks
reproduction, not meaning.

### Threading

The scan runs on a worker that loads its **own** encoder from the path resolved
on the GUI thread, and never touches `kavachSchema()` or `SessionKeyStore` — a
scan whose first half used a different schema than its second half is evidence
of nothing. Field attribution therefore happens in the window, which has the
shared decoder.

The standard `connect(thread, &QThread::finished, thread, &QObject::deleteLater)`
idiom is deliberately **not** used: the report is emitted from inside `run()`,
so the main loop can process the deletion before the thread has terminated, and
deleting a running `QThread` aborts the process. It showed up only in a full
suite run, never when the suite ran alone. `onFinished()` joins the thread and
then deletes it, and the destructor joins too — a window can be destroyed
without a close event.

### Tests

81 checks across two suites. The core is pure, so every outcome is reachable
without a window: real slrp/lsrp/arp frames reproducing, the misaligned-parse
trap, a frame with a `<pad>` bit set (which cannot be rebuilt, because the
writer emits zero there and the parser skips it), each refusal, the diff-byte
cap, sample bounding, and the worker end to end over a temp file including an
unreadable one.

The window gets its own suite, and it earned it: it caught a real bug where
opening the validator before any traffic arrived left the live-log checkbox
unticked **and never re-ticked it** once traffic came in, so Run stayed greyed
out with no visible reason.

Full suite: **60 suites, 1896 checks, 0 failed.**

### Next

The station responder.

---

## Menu reorganisation (same patch)

| File | Change |
|---|---|
| `mainwindow.cpp` | Tools grouped into five submenus; Edit and View regrouped; the duplicate shortcut resolved |
| `commandpalette.cpp` | harvest lazily-built submenus |
| `tests/menuaudit_main.cpp`, `menuaudit.pro` | audit the new structure; fail on any repeated shortcut |

### The duplicate

`Ctrl+Shift+R` was bound to both **Reload schema** and **Stream session (.dlr)
as live**. Qt answers an ambiguous shortcut by firing *neither* action and
warning on stderr, so both were unreachable from the keyboard and nothing in
the UI said so. Reload schema keeps `Ctrl+Shift+R`; streaming moved to
`Ctrl+Shift+S`.

The audit now walks every action in the menu bar and fails on a repeat, so this
cannot come back quietly. Current count: **21 distinct shortcuts, no clash.**

### Tools: seventeen entries → six

| | |
|---|---|
| **Monitor ▸** | Compare tabs · Live Loco Console · Active Fault Panel · Plot field over time |
| **Inspect ▸** | Decode Workbench · Selected row → Decode Workbench · Frame Diff · Packet Sequence |
| **Transmit ▸** | Packet Maker · Selected row → Packet Maker · Field Sweep |
| **Test cases ▸** | Load test cases · Save observation report · Reset run |
| **Schema ▸** | Reload schema · Use external schema file · Revert to built-in · Round-trip Validator |
| | Session Key… |

The grouping is by what the tool does *to the target*, and the one that earns
its keep is **Transmit**. Packet Maker and Field Sweep were sitting in a flat
list directly above "Selected row → Decode Workbench" — a tool that sends
packets to live signalling equipment, one row from a tool that only reads. Now
everything that transmits is behind one labelled step, and nothing that
transmits is left loose in Tools (the audit checks exactly that).

Round-trip Validator went to **Schema** rather than Transmit: it answers a
question about the schema, and the loop it belongs to is edit `kavach.xml` →
reload → validate against real traffic. It is also read-only, and putting a
read-only tool inside a menu labelled Transmit would undercut the label.

### Edit and View

- **Edit**: the four ways of asking "where is it?" now sit together (Find, Go
  to timestamp, Search all sources, Search recorded sessions), then Copy, then
  a **Bookmarks ▸** submenu holding the toggle and the two F2 navigations.
  Shortcuts are unaffected by the nesting.
- **View**: three sections separated — what you are looking at (merged view),
  how it is laid out (Panels, Columns, Row density), how it is drawn (UTC,
  theme).
- **File**: *Stream session as live* moved here from Tools, next to *Open
  recorded session*. Both choose a source of traffic; neither is an operation
  on traffic already loaded.

### Depth costs nothing here

Grouping is only free because `harvestCommands()` recurses into submenus, so
`Ctrl+P` still reaches every action by name in one flat search. That made a
pre-existing gap worth closing: the Panels submenu builds its contents on
`aboutToShow`, which the palette never emitted, so the five dock toggles and
Reset panel layout were invisible to it. The harvest now emits it first.
**53 commands reachable from the palette**, up from 47.

Full suite: 60 suites, 1896 checks, 0 failed. Menu audit: 45 checks, passed.

---

## Fill-from-buffer read the wrong bytes (same patch)

| File | Change |
|---|---|
| `packetmakerdialog.{h,cpp}` | `splitBuffer()` — locate the body from the schema, not from `MessageHeader::SIZE` |
| `fieldsweepdialog.cpp` | seed through the same helper (it was stripping nothing at all) |
| `tests/test_openbuffer.cpp` | new `splitbuffer` suite, 23 checks |

### What was wrong

The Packet Maker stripped `MessageHeader::SIZE` (8 bytes) from a pasted
capture. An arp or lsrp body starts **10** bytes in — the schema says so in
`body_offset="80"`: the 8-byte message header plus two more bytes before the
body. Every field in the form was therefore read two bytes out of step.

It did not fail. `parseBody()` reads from bit 0 of whatever it is handed, so
the misaligned buffer returned plausible values — `PKT_TYPE` 0 instead of 10,
LSRP's `FRAME_NUM & 7` health branch gated on the wrong number — and the form
filled normally.

Field Sweep was worse: `seedFromBuffer()` handed the **whole captured
datagram** to `parseBody()`, stripping nothing. It then transmits from those
values, which is the reason this was worth doing first.

### The fix

One shared `PacketMakerDialog::splitBuffer()`, used by both dialogs. The
schema's `body_offset` is the authority, because it is what the decoder uses —
the two paths now agree by construction rather than by both happening to
subtract 8:

- message header recognised (right `message_id`, `message_length` accounts for
  the buffer) **and** `body_offset > 0` → strip `body_offset/8`, and report the
  envelope as *8-byte header + N bytes before the body*;
- header recognised, `body_offset == 0` → strip the 8;
- no header but `body_offset > 0` → read as a bare body **and say so**, since a
  truncated paste would otherwise be wrong by a constant offset with nothing
  visible to show it;
- shorter than the envelope → hand back nothing rather than a truncated body
  that would parse as a real one.

`slrp` is unaffected: its capture carries no envelope, `body_offset` is 0, and
nothing is stripped.

### Tests

The suite pins the difference rather than just the new behaviour: the old
8-byte strip is run alongside the correct one, and the check is that it *did
not fail* — it just read a different packet type. That is the failure mode
worth remembering.

Full suite: **61 suites, 1919 checks, 0 failed.** Menu audit passed.

---

## UI, part 1: colours that survive the dark theme

| File | Change |
|---|---|
| `uicolors.{h,cpp}` | five semantic colours resolved from the active palette, a contrast metric, a theme-change hook |
| 15 UI files | hex literals replaced with the meaning they stood for |
| `tests/test_uicolors.cpp` | 42 checks — every colour, both themes, arithmetic not opinion |

### What was wrong

Twenty-six hex literals across nine files, nearly all picked against the light
palette. Several were below the 4.5:1 that normal text needs:

| | was | on | ratio |
|---|---|---|---|
| warning | `#b9770e` | light window | **3.20 : 1** |
| error | `#e05a52` | dark window | **3.76 : 1** |
| muted | `#80868e` | light window | **3.19 : 1** |
| ok | `#46c07a` | white (used in *both* themes) | **2.43 : 1** |

The Loco Console and the Decode Workbench were using dark-theme greens and reds
in the light theme, which is where that last one comes from.

Second half of the problem: only `FaultPanelWindow` implemented `changeEvent`.
Toggle the theme with any other window open and it kept its old colours until
closed and reopened.

### What it does now

`UiColor` gives five meanings — ok, warning, error, muted, accent — plus the
caution-banner trio, each a dark/light pair chosen so that **both** clear 4.5:1
against the window *and* the base of their own palette. This is not a new
scheme: `FaultPanelWindow` already did exactly this for itself, and that window
now draws from the shared module rather than its own copy.

`UiColor::onThemeChange(widget, fn)` re-runs a widget's colouring on a palette
change, so windows that colour themselves once — the Round-trip Validator's
results table, the caution banners in Packet Sequence and the DLR player — no
longer go stale after a toggle.

### Deliberately left alone

- The exported HTML in `testassertions.cpp` and `faultpanelwindow.cpp`: those
  are standalone documents with their own white background, read in a browser
  or on paper. They have no palette to follow.
- `ReplayWindow`'s fixed dark canvas: it is a plot, not a surface of the app.
- The severity colours in `logmodel.cpp` and `mainwindow.cpp`: already
  theme-aware, and they belong to the ColorRules rendering system rather than
  to chrome. Folding them in blindly would merge two things that are separate
  on purpose.

### Why the tests are arithmetic

Colour regresses without anyone noticing — somebody nudges a value to taste, it
looks fine on their monitor, and the warning text is unreadable on a laptop in
daylight. So the suite computes WCAG contrast for every semantic colour against
both surfaces in both themes and fails below 4.5:1, after first checking the
metric itself against known references (black on white is 21:1, `#767676` on
white is 4.54:1). The theme-change hook is tested by toggling the palette and
counting repaints.

Full suite: **62 suites, 1961 checks, 0 failed.** Menu audit passed.

Next in the UI pass: feedback consistency, empty states, keyboard flow — still
to be looked at before anything is promised about them.

---

## UI, part 1b: the shape of the window

| File | Change |
|---|---|
| `uistyle.{h,cpp}` | one stylesheet, every value derived from the active palette |
| `theme.h` | palette reworked to feed it |
| `main.cpp`, `mainwindow.cpp` | apply at startup and on every theme toggle |
| `tests/shot.pro`, `tests/screenshot_main.cpp` | throwaway harness that renders the main window to PNG in both themes |

Where `uicolors` says what a colour *means*, this is what the window *looks
like*. Fusion out of the box is functional and cramped: controls tight against
their frames, square-cornered group boxes with a hard 1px border, table headers
that look like buttons, and every dialog picking its own margins.

The rule is the same as in `uicolors`, and it is the important part: **there is
not one hex literal in the sheet.** Every colour is read from the palette in
force when `sheet()` is built, and `apply()` runs again on each theme toggle. A
stylesheet with baked-in colours silently overrides the palette, which is how
Qt applications end up with a dark mode that works everywhere except the four
widgets someone styled by hand — and unlike a wrong palette colour, that
breakage cannot be fixed from the theme.

`monoFont()` exists because `"monospace"` does not resolve as a family name on
Windows and falls back to whatever the system picks, which is why the Packet
Maker's buffer box was in a different face from every other hex view.

The screenshot harness is not part of the suite — it renders the main window in
both themes so the result can actually be looked at, since no assertion tells
you whether a window looks assembled or designed. Its compiled output is
gitignored.

---

## UI, part 2: Escape, and the dialogs that transmit

| File | Change |
|---|---|
| `sendguard.h` | the rule, as two small functions |
| `packetmakerdialog`, `fieldsweepdialog`, `packetsequencedialog` | `reject()` and `closeEvent()` overrides |
| `tests/test_sendguard.cpp` | 9 checks |

### What was wrong

A `QDialog` closes on Escape by default. Packet Maker, Field Sweep and Packet
Sequence are `QDialog`s, opened with `WA_DeleteOnClose`. So during a run,
Escape called `reject()`, the window was destroyed, the `UdpSender` died with
it — and the transmission stopped with no confirmation and nothing left on
screen.

No crash: `UdpSender` is timer-based, so destruction stops it cleanly. The cost
is the other half. A sweep is merely annoying to lose. A **packet sequence**
stopped halfway means the equipment saw a partial run, and the window that says
which step it reached is exactly what the operator needs next — and it is gone.

### The rule

While a run is in flight, **Escape stops the run and leaves the window open.**
One keypress does one thing, and the result stays readable. A second Escape
closes, because by then nothing is running. The status line says so.

The close box asks first, and the question names the consequence rather than
just asking whether you are sure:

> *The sequence is still running and is transmitting. Close anyway? The run
> stops where it is.*

Two free functions rather than a shared base class: these three dialogs have
nothing else in common, and giving them a common parent to share four lines
would couple them for no other reason.

### Coverage, honestly

The suite tests `interceptReject()` directly for both the running and idle
cases, and drives real Escape key events through the actual dialogs — but only
in the **idle** state, because entering the running state means transmitting,
and a test suite should not put packets on the wire to check a keypress. The
running path is covered at the predicate, not end to end.

### Still open in the UI pass

- **Feedback consistency.** 35 status labels across 13 files, each with its own
  wording and placement. The colours are now shared; the conventions are not.
- **Empty states.** 21 tables and lists, and a blank one looks the same whether
  nothing has arrived yet or the scan found nothing. Only a few say which.

Full suite: **64 suites, 1996 checks, 0 failed.** Menu audit passed.

### Escape, part 2: the rule applies to work, not just to sending

Extended to the two remaining dialogs that have something in flight:

- **Round-trip Validator** — a scan over a large corpus runs for minutes on a
  worker thread. Escape used to abandon it and destroy the window; now it
  cancels the scan and the results stay readable.
- **DLR player** — Escape stops the replay and keeps the window. No
  confirmation on close, because this one feeds the live log rather than the
  wire, but losing the window mid-replay still leaves a half-fed log with
  nothing on screen saying where it stopped.

I checked the other twelve `QDialog` subclasses. `ArchiveSearchWindow` is a
`QWidget`, so Escape does nothing to it. `SearchWindow` and `ExportDialog` hold
no worker or timer. The rest — Settings, Export, Goto, Compare, Command
Palette, Session Key, SubPacket, Detach — have nothing running, and for those
Escape-means-cancel is the behaviour people expect.

The point of extending it is consistency: an operator who learns "Escape stops
the thing" in the Packet Maker should not have to discover an exception in the
validator.

Full suite: **65 suites, 1998 checks, 0 failed.**

---

## UI, part 3: Ayu Dark, empty states, workspace restore

| File | Change |
|---|---|
| `theme.h`, `uicolors.cpp` | the dark theme is Ayu Dark |
| `emptystate.{h,cpp}` | a centred line over any empty item view, said by the view's owner |
| `mainwindow`, `roundtripwindow`, `searchwindow` | empty states wired in |
| `settings.h`, `mainwindow.{h,cpp}` | the tabs that were open last time, restored |
| `uicolors.{h,cpp}` | theme notification reworked from an event filter to a registry |
| `tests/` | `test_emptystate.cpp`, `test_workspace.cpp` (32 checks) |

### Ayu Dark

The published scheme rather than an approximation of it: `#0B0E14` as Base,
`#0F131A` as Window, `#BFBDB6` foreground, `#39BAE6` for links. The semantic
colours are ayu's own accents — string green `#AAD94C` for ok, func orange
`#FFB454` for warning, `#D95757` for error, tag blue for accent. The near-black
background buys headroom: everything clears the contrast floor by 4:1 or more,
where the previous dark set only just made it.

Two deliberate departures, both because this is a log console and not a code
editor:

- Ayu's line highlight `#11151C` is 1.06:1 against the background. That is
  right for marking one line in an editor and useless as an alternating row
  colour in a table of ten thousand — the same mistake the light palette used
  to make at 1.04:1. `AlternateBase` opens up to `#171D27`.
- Ayu's `ui.fg` `#565B66` for disabled text is 2.84:1, below the 3:1 floor the
  palette suite enforces. Disabled is not invisible; an operator still has to
  read a greyed-out field to know what it says. Lifted to `#6C7380`.

### Empty states

A blank table meant three different things — nothing has arrived, the filter
matched nothing, the search found nothing — and the operator had to work it out
from context. During acceptance testing "no rows" versus "no traffic" is not a
detail, it is the observation.

The message is a callable, not a fixed string, because the important case is a
log tab: it asks the **source** model rather than the proxy, so

> *Waiting for traffic from Loco 1 / Ctrl 1.*

and

> *No rows match the filter. 40,318 message(s) are hidden by it.*

are different sentences. Telling an operator "waiting for traffic" while forty
thousand rows sit behind a filter is worse than the blank it replaced. Also
wired into the three docks, the validator's table (three states: no scan yet,
scan read nothing, scan recognised no packet type) and the search results.

### Workspace restore

Tab order, hidden tabs included, and the active tab. Restored **empty**, before
any traffic arrives, so a packet for a restored source lands in the tab that is
already on screen instead of creating a second one — and the empty state is
what makes a restored-but-silent tab read as informative rather than broken.

Filters are deliberately not restored. A filter reinstated at startup hides
live traffic, and "why is nothing arriving" is a far worse first minute than
retyping a filter. `ui/restore_workspace` turns the whole thing off.

### Two bugs the tests caught

**A recursive repaint that presented as a segfault.** The theme callback called
`setStyleSheet()`, which re-polishes the widget, which delivered another palette
change back to it — recursion until the stack ran out, with nothing in the
crash pointing at the cause. Guarded, and then the guard exposed the real
design flaw: Qt delivers `PaletteChange` only to polished widgets whose own
effective palette changed, so the empty-state overlay — a child of a viewport —
never received one at all. The event filter is now a registry that
`ThemeUtil::apply()` drives directly, which is both correct for nested widgets
and cheaper than an application-wide filter in a window handling thousands of
rows a second.

**A greedy hex escape.** `"key\x1fname\x1f1"` in a C++ literal ends in the
single character U+01F1, not a separator followed by `'1'` — hex escapes
consume as many hex digits as they can. It compiled, ran, and wrote workspace
records that nothing could read back. Both the code and the first version of
the test had it; the records are now built with `join()` and a named `QChar`.

Full suite: **67 suites, 2030 checks, 0 failed.** Menu audit passed. Two
consecutive headless runs, to exercise save-then-restore.

### Still open in the UI pass

- **Feedback consistency** — 35 status labels across 13 files, shared colours
  but no shared conventions.
- **Jump between decode failures** — the minimap shows where they are; there is
  still no key that steps through them.

---

## UI, part 4: Ayu Light, and three things a screenshot found

| File | Change |
|---|---|
| `theme.h` | the light theme is Ayu Light; `Mid` lifted so indicators are visible in the dark |
| `uicolors.cpp` | light semantic colours are ayu's hues, darkened until legible |
| `uistyle.cpp` | check and radio indicators styled from the palette |
| `logmodel.cpp` | severity row colours from `UiColor` instead of `"red"` and `"darkorange"` |
| `mainwindow.cpp` | the startup panel's secondary paragraph is readable |

### Ayu Light

The sibling of the dark theme rather than a generic grey: `#FCFCFC` Base,
`#F3F4F5` Window, ayu's `#E7E8E9` rule line as the alternating row.

Ayu Light's published values do not survive as UI text — the string green is
2.4:1 against its own background and the accent orange is 1.9:1, because they
are syntax colours read in short runs, not labels. Keeping the **hue** and
taking the lightness down is what makes the two themes read as one scheme:
`#4F6B00` ok, `#9C5A00` warning, `#C03A3A` error, `#1A6FA8` accent. Text is
`#4A4F54` rather than ayu's `#5C6166` — same neutral, a step darker, because
this window is hex and timestamps scanned for hours.

### What the screenshots showed

I rendered the main window in both themes and looked at it, which found three
things no assertion had:

- **The log's own severity colours were the worst offenders in the program.**
  `logmodel.cpp` returned the named colours `"red"` and `"darkorange"` for the
  light theme: 4.0:1 and **2.1:1** against a white row. That is the surface
  read the most, and the entire point of the colour is to make a bad row catch
  the eye. Now from `UiColor`, which also means they follow the theme instead
  of being two hardcoded branches.
- **An unticked checkbox was invisible in the dark theme.** Fusion outlines
  indicators with `Mid`, which at ayu's `#2A303A` is 1.4:1 against the window —
  "Follow newest" looked like a label with a stray gap in front of it. `Mid`
  is lifted, and indicators are now styled explicitly from the palette so this
  does not depend on what the base style happens to do.
- **The startup panel's third paragraph was grey on grey.** It used
  `color:palette(mid)` — a *frame* colour, `#DCDEE0` on the light panel. It is
  the disabled-text role now: dim, still readable.

Worth saying plainly: the contrast suite passed through all three of these,
because it checks the colours the module defines and these were literals
elsewhere. Rendering the window and looking at it is not a substitute for the
tests, and the tests are not a substitute for it.

Full suite: **67 suites, 2030 checks, 0 failed.** Menu audit passed.

---

## UI, part 5: stepping between problems

| File | Change |
|---|---|
| `markerscrollbar.{h,cpp}` | `nextMarkedRow()` — the next error or warning, as a proxy row |
| `mainwindow.{h,cpp}` | Edit → Next problem (F4) / Previous problem (Shift+F4) |
| `tests/test_problemnav.cpp` | 19 checks |

The minimap has always drawn where the errors and warnings are. There was no
way to move between them, so finding the three bad frames in a two-hour capture
meant dragging the scrollbar and squinting at the marks.

`nextMarkedRow()` asks the same question `binRowMarks()` answers for drawing,
one row at a time — same proxy-to-source mapping, same severity test — so what
the operator lands on is always a row the minimap has a mark for. The two
cannot drift apart, because they are the same code path.

Three decisions worth stating:

- **It returns proxy rows, not source rows.** That is what the view selects
  with, and returning a source row would scroll to a plausible-looking wrong
  line whenever a filter was active. There is a test for exactly that.
- **It does not wrap.** Wrapping past the end without saying so puts the
  operator somewhere they did not ask to be, and in a log this long they may
  not notice they are back at the top. Instead the status bar says *"No further
  errors or warnings below this row."* — silence would be indistinguishable
  from a key that did not register.
- **F4, not F3.** F3 and Shift+F3 belong to the find bar. The audit now counts
  23 distinct shortcuts, no clash.

With no row selected, forward starts above the top and backward starts below
the bottom, so the first press always finds the nearest problem in the
direction asked for rather than doing nothing.

Full suite: **68 suites, 2049 checks, 0 failed.** Menu audit passed.

### Still open in the UI pass

**Feedback consistency** — 35 status labels across 13 files. Shared colours
now, but each still has its own wording, placement and lifetime.

---

## UI, part 6: the field-over-time plot

| File | Change |
|---|---|
| `fieldplot.{h,cpp}` | axis ticks on real values, readable axis text, a hover readout |
| `tests/test_fieldplot.cpp` | new `fieldplotticks` suite, 31 checks |

From a photograph of the tool in use, plotting `LOCO_MODE`.

### The axis was labelled in values the field cannot hold

`LOCO_MODE` takes 1, 2, 4 and 6. The axis divided the range into quarters and
labelled them **2.25, 3.5, 4.75** — numbers that field can never be, on a plot
whose only job is to show what it was.

`niceTicks()` puts ticks on a 1-2-5 step, and when every sample in the series is
a whole number it forces the step to a whole number too. Where the range fits in
a handful of steps of 1 — an enum, which is most of the interesting fields here
— it labels every value: `1 2 3 4 5 6`, not `2 4 6` with the value the trace
actually sits on left unlabelled.

Continuous fields keep their fractions: forcing integers on a 0..1 field would
collapse the axis to two labels. The step is snapped to itself rather than
accumulated, or a 0.1 step renders `0.30000000000000004`.

### The axis text could not be read

Both the value labels and the timestamps were drawn in `palette().mid()` — the
**frame** role, `#DCDEE0` on the light theme, about 1.2:1 against the plot
background. That is the same mistake as the startup panel's `palette(mid)`
paragraph, in the one place where the numbers are the entire point.

Everything structural now comes from `UiColor::muted()` at three alphas: full
for text, 120 for the frame, 60 for gridlines. Readable in both themes without
the grid competing with the trace.

The time axis also gains three interior labels and gridlines, so a step in the
trace can be read against a time instead of counted in pixels. End labels are
pulled inside the plot rather than centred on their tick, so neither runs off
the widget.

### You could not see the values

Hovering circled a point and told you nothing about it. There is a readout now —
time and value, in a box that keeps itself inside the plot when the point is
near an edge.

Full suite: **69 suites, 2080 checks, 0 failed.**

---

## UI, part 7: app-wide contrast, and a guard against losing it again

| File | Change |
|---|---|
| `tests/test_contrastaudit.cpp` | 77 checks: the whole palette in both themes, and a scan of the source |
| `uicolors.{h,cpp}` | `frame()` and `grid()`; semantic colours re-fitted to their worst surface |
| `theme.h` | disabled text fitted against the alternating row |
| `timelineribbon.cpp`, `mainwindow.cpp` | the last colour literals and frame-role-as-text |

### Two halves, because the failures came in two kinds

**The palette.** Every pair of roles that can end up as text on a surface, in
both themes: window text, base text, alternating-row text, button text,
selection, tooltip, links, placeholder, disabled — plus the five semantic
colours against all four surfaces they are drawn on.

That last part found five real failures immediately. The colours had been
fitted against the *base*, and half the text in this program sits on an
**alternating row**, which is darker:

| | on the alternating row |
|---|---|
| light warning `#9C5A00` | 4.42 : 1 |
| light error `#C03A3A` | 4.38 : 1 |
| light accent `#1A6FA8` | 4.41 : 1 |
| dark error `#D95757` | 4.39 : 1 |
| light disabled text `#8A9199` | **2.60 : 1** |

All now fitted to their worst case rather than their best.

**The source.** Every contrast bug found in this program came from a literal
somewhere the arithmetic did not look — `"red"` and `"darkorange"` in the log
rows, `palette(mid)` in the startup panel, `palette().mid()` in the field plot
and the timeline. No numeric test finds those, because the numbers are not in
the module.

So the suite reads the source tree and fails on two patterns:

- **a colour literal** outside `uicolors`, `theme.h`, `uistyle.cpp`, and the
  three files that write standalone documents or a fixed plot canvas;
- **a frame role used for drawing** — `palette().mid()`, `midlight()`,
  `dark()`, `shadow()`. Mid is `#DCDEE0` on the light theme, about 1.2:1
  against a white surface. Text goes in `UiColor::muted()`; lines go in the new
  `UiColor::frame()` and `UiColor::grid()`.

A source-scanning test is unusual and worth justifying: the rule it enforces is
one a reviewer would otherwise have to remember on every paint routine, and
three separate reviews here did not.

It found the timeline ribbon still drawing its end labels and its "No messages
yet" text in Mid, and still using three hardcoded mark colours that had been
tuned for the dark theme and used in both — the same marks the minimap and the
log rows draw, now from the same place.

Full suite: **70 suites, 2157 checks, 0 failed.** Menu audit passed.

### Tabs

From the same photograph: eleven source tabs, and the labels washed out.

- **Unselected tab text** was `mix(text, window, 0.30)` — about 3.9:1, under
  the floor. It is `UiColor::muted()` now, which is the colour the audit fits
  for exactly this job, so it cannot drift below the floor without the suite
  failing.
- **Which tab is current** has to be obvious across a row of eleven. A
  one-pixel border in the "subtle" tone was not; the selected tab now carries
  a 2px accent edge along its top and a heavier weight.
- **The close cross** was the worst of it: measured on the dark theme, Qt's
  default came out at RGB(36,36,37) — all but invisible beside a label at 5:1.

That last one is worth recording as a Qt trap. Setting an icon on the close
button does nothing once a stylesheet is in force: `QStyleSheetStyle` draws the
`close-button` subcontrol itself and ignores the icon. I built the painted-icon
route first, measured the render, and found it had changed nothing. The cross
is a resource now, one per theme, matching `UiColor::muted()` — measured back
out of the render at RGB(139,148,156) on dark and RGB(100,107,114) on light,
which is what it should be. The icon-painting API was removed rather than left
beside the one that works.

---

## Tools in the recorded-session window

| File | Change |
|---|---|
| `sessionwindow.{h,cpp}` | a Tools menu and a row context menu |
| `tests/test_sessiontools.cpp` | 17 checks |

The archive is where the careful work happens: a live console is *watched*, a
recording is *read*. Until now, decoding one frame out of a capture meant
re-opening it in the live console — which is the wrong way round, and also the
dangerous way round, because the live models feed `LogWriter`, so anything
loaded into them is re-recorded into today's archive.

**Tools** — Decode Workbench, Selected row → Decode Workbench (Ctrl+Shift+B),
Frame Diff, Plot field over time (Ctrl+Shift+P), and below a separator,
Selected row → Packet Maker. Right-clicking a row offers the same three that
act on it, with "Compare these two frames" enabled only when exactly two rows
are selected.

Flat, not grouped: five entries do not need the submenus Tools needed at
seventeen. The shortcuts match the live window's, so the same fingers work in
both — they are window-scoped, so there is no clash.

Nothing new was written. The same `DecodeWorkbench`, `PacketMakerDialog` and
`FrameDiffWindow`, handed the same `entryBufferText()` the live log hands them,
so a frame read out of a recording decodes exactly as it did when it arrived.

### What is deliberately not offered

- **Field Sweep** and **Packet Sequence**: both transmit repeatedly against
  live equipment. A window whose whole premise is "this is yesterday" is the
  last place to start one by accident.
- **The round-trip validator**: it already reads `.dlr` files directly, so an
  entry point here would be a second route to the same thing.

Packet Maker *is* offered, because reproducing a captured frame is a real
reason to open a recording — but it sits alone under a separator, since it is
the only entry here that leads somewhere that can send. There is a test on that
placement, not just on its presence.

Full suite: **71 suites, 2174 checks, 0 failed.** Menu audit passed.

---

## Packet Maker: FRAME_NUM seeded from the clock

| File | Change |
|---|---|
| `packetmakerdialog.{h,cpp}` | `secondsSinceMidnight()`, seeded into FRAME_NUM, plus a **Now** button |
| `tests/test_packetvary.cpp` | `framenumseed` and `framenumform`, 13 checks |

FRAME_NUM now starts at the seconds elapsed since local midnight — the form the
equipment's own frames carry — and stays fully editable.

Zero was a poor default for a reason beyond looking synthetic: LSRP's health
group rotates on `FRAME_NUM & 7`, so a frame number of zero pins every built
frame to the `& 7 == 0` branch. The one branch you happen to exercise becomes
the only branch you ever send.

**Seeded, not driven.** The value is written once when the form is built and
nothing rewrites it afterwards, so an edit stays edited. Filling from a buffer
or loading a preset still wins, because both write every header editor after
the form is built — there is a test that loads a real captured LSRP frame and
checks the form ends up holding the captured frame number rather than the
clock.

**The Now button** exists because the clock moves while the dialog sits open,
and a frame number several minutes old may be stale by the time it is sent.
Rewriting the field behind the operator would undo a deliberate edit, so
instead there is a button that says what it does — and pressing it clears the
built frame and disables Send, because the form no longer matches what was
verified.

Two things worth your confirmation:

- **Local time, not UTC.** It matches the RTC the equipment stamps its own
  frames with and the clock the log shows by default. If SIF 0533 defines the
  frame number against UTC, this is one line to change.
- **A narrow field wraps.** 86,399 fits comfortably in FRAME_NUM's 17 bits, so
  this never bites today; the width is passed in and the modulo is explicit so
  that a narrower field somewhere would wrap as a stated behaviour rather than
  overflow silently.

Full suite: **73 suites, 2187 checks, 0 failed.**

---

## Find: whole word, and marking every match

| File | Change |
|---|---|
| `findbar.{h,cpp}` | whole-word toggle, Mark-all toggle |
| `logmodel.{h,cpp}` | `setFindHits()` — tint the rows the find bar matched |
| 11 UI files | named CSS colours in stylesheets replaced |
| `tests/test_findextras.cpp`, `test_contrastaudit.cpp` | 12 new checks |

Prompted by a photo of Notepad++'s Find dialog. Comparing it against what the
find bar already had, most of it was present — next/previous, a live count
("3 of 47"), case sensitivity, regex, a column selector, wrap with a
"(wrapped)" hint, and query mode. Two things were not.

**Whole word.** "STN" matched "STN_ID", which on this traffic is most of the
log. Implemented as a regex around the escaped text rather than as a second
matching path, so there is no hand-rolled boundary test to get subtly wrong.
With regex already on the box disables itself, because the pattern is then the
operator's to write and silently wrapping it in `\b` would not be.

**Mark all**, on by default. Every matching row is tinted, not just the one the
cursor sits on. A count tells you how many; the tint tells you *where* — near
the top, spread evenly, or bunched around one minute — which on a hundred
thousand rows is usually the actual question.

The hits are held as **entry pointers, not row numbers**. A tab evicts from the
front once full, so a remembered row number silently becomes a different
message a few seconds later. There is a test that marks a row, appends twenty
more entries to renumber everything, and checks the same *message* is still the
one tinted.

### What the audit missed, and now doesn't

Wiring this up turned up `"color: darkblue; font-style: italic;"` in the find
bar itself — and then eighteen more named colours across eleven files:
`"color: gray"` is 3.5:1 on the light base, `darkblue` is 1.4:1 on the dark
one. The contrast audit had been looking for hex literals and `QColor(...)`
only, so a stylesheet spelling the colour as an English word walked straight
past it. All converted; the audit now flags `color: <name>` too.

That is the third distinct spelling of the same bug. The audit is worth having
precisely because each time it has been a spelling nobody thought to check.

### Not done

- **Extended search mode** (`\n`, `\t`, `\x..`). Worth discussing separately:
  in this program the interesting escape would be a hex *byte* pattern against
  the frame, which is a different search than a text one and probably belongs
  in the query language rather than in the find bar.
- **"In selection"** — the find bar searches what the filter leaves visible,
  which is the closer analogue here.
- **Find All in a list** — that is what Search all sources (Ctrl+Shift+A)
  already does, with a results table.

Full suite: **74 suites, 2199 checks, 0 failed.**

### Detaching it

The find bar now has a **⧉** button that floats it into a small window, and a
**⧈** that puts it back.

Detaching **reparents the same widget** rather than opening a second find UI.
That is the whole decision: a separate dialog with its own copy of the matching
logic would be two things to keep in step — same query, same cursor, same
match list, same rescan-on-new-traffic — and on a live log they would not stay
in step for long.

Details that matter more than they look:

- **It comes back to the same seat.** The layout and index are remembered at
  detach; re-docking inserts it there. A bar that returns to the bottom of the
  tab is worse than one that never detached.
- **`Qt::Tool`**, so it stays above the console it searches without taking a
  taskbar entry — which is what makes it usable while scrolling the log behind
  it, and the reason to want it floating at all.
- **Esc re-docks first, then closes.** Otherwise Esc leaves a window on screen
  with nothing to search.
- **Ctrl+F while floating raises the existing window** instead of opening
  another.
- **If the tab closes while the bar is floating**, the bar is a child of its
  own window and would outlive the view it searches — a dangling pointer the
  next keystroke walks into. It re-docks and goes with the tab.

The test that took the longest to get right was the one counting host windows:
the old host is `deleteLater`'d, because it is destroyed from inside its own
`finished()` signal, and `processEvents()` does not run deferred deletions
posted outside an event loop. Without an explicit flush the check counts a
window already on its way out.

Full suite: **75 suites, 2213 checks, 0 failed.**

---

## UI, part 8: one vocabulary for status messages

| File | Change |
|---|---|
| `statusline.{h,cpp}` | four verbs and one rule about time |
| `packetmakerdialog`, `fieldsweepdialog`, `packetsequencedialog`, `roundtripwindow` | converted |
| `tests/test_statusline.cpp` | 29 checks |
| `tests/test_contrastaudit.cpp` | the hex check now allows whitespace after the colon |

Thirty-five status labels across thirteen files, each with its own wording,
colour and idea of how long a message should stay. The colours were unified in
part 1; the conventions were not, and those are what an operator reads.

Three things were wrong in the same way everywhere:

- **Success and failure looked the same.** Both were plain text, so the reader
  had to parse the sentence to find out which had happened.
- **Nothing expired.** "Sent 40 datagrams" sat there ten minutes later, reading
  as current state.
- **A failure could be wiped by the message before it.** "Sending…" started no
  timer of its own, but where one existed the failure that followed inherited
  the countdown.

### The rule

    say()   neutral progress          transient
    ok()    it worked                 transient
    warn()  it worked, but read this  STICKY
    fail()  it did not work           STICKY

Transient messages clear after six seconds — long enough to read a sentence,
short enough not to be mistaken for current state. Warnings and failures never
clear on a timer; the operator decides when they have read them by doing the
next thing. Setting a sticky message stops any timer left armed by an earlier
transient one, which is the bug above, and there is a test for exactly that
sequence.

Each kind also carries a glyph — ✓, ⚠, ✕. Colour alone is not a signal: for a
colour-blind reader a red sentence and a green one are the same sentence, and a
program that has just spent this much effort on contrast should not then encode
its most important distinction in hue alone.

The round-trip validator's summary is now sticky whenever anything failed to
reproduce. That line **is** the finding, and a finding that clears itself after
six seconds is a finding nobody acted on.

### A fourth spelling

Converting these turned up `"color: #cc3300"` — with a space — in six more
files. The audit's hex check looked for `color:#` exactly, so every one of them
walked past it. That is the fourth distinct spelling of this bug: hex without a
space, hex with a space, `QColor("#...")`, and English colour names. The check
is a regex now.

Still on `setText` with a hand-rolled colour: the search windows, the session
key dialog, the decode workbench and the field inspector. They use a different
shape (`setStatus()` helpers) and are worth converting, but not by mechanical
substitution — each has its own notion of what is sticky.

Full suite: **76 suites, 2242 checks, 0 failed.** Menu audit passed.

### The rest of them, and a fifth verb

The five surfaces left on hand-rolled status — both search windows, the decode
workbench, the field inspector, and the archive scan summary — are converted.
Doing it properly turned up a distinction the four verbs could not express.

Every one of those panels says things like *"Select a row to decode it"*,
*"Paste a hex frame"*, *"18 field(s), 2 with byte ranges"*. None of that is
news. It is the **caption** for what the panel is currently showing, and a
caption must not expire: a panel that explains itself for six seconds and then
goes blank is worse than one that never explained itself.

So `state()` — neutral, sticky, no glyph, because a caption is not a verdict
and ✓ or ⚠ would imply one. The other four remain for events. The rule is now:

    state() what this panel is showing   sticky, no glyph
    say()   progress, in flight          transient
    ok()    it worked                    transient
    warn()  qualified                    sticky
    fail()  it did not work              sticky

Two call sites changed meaning rather than just colour, and both were carrying
a `bool isError` that had been doing the work of three answers:

- **Search results hitting the cap** were styled as an error. They are a
  qualification: the results are real, there are simply more. Now a sticky
  warning, because "184 hits" means something very different when it is really
  "the first 184".
- **An archive scan that was cancelled, capped, or hit unreadable files** was
  merely muted. Same reasoning, same fix.

Full suite: **77 suites, 2251 checks, 0 failed.** Menu audit passed.

---

## The two open builder items

| File | Change |
|---|---|
| `packetbuilder.{h,cpp}` | encode against the schema in force; verify what was built |
| `tests/test_packetbuilder.cpp` | new `buildverify` suite, 7 checks |

### It builds against the schema you are decoding with

`PacketBuilder` hard-loaded `:/schema/kavach.xml`. Load an external kavach.xml
and the log decoded with your file while every builder carried on encoding
against the resource — two definitions live in one session, with nothing on
screen saying so.

It now takes `Settings::schemaPath()` when one is set, falls back to the
built-in copy if that file will not load (a broken edit leaves the Packet Maker
working rather than empty), and says so in the build notes when it does.
`schemaPath()` is exposed because "which schema produced this frame" is the
first question when a frame is not what was expected.

### The self-verify now verifies

`roundTripped` was `!decoded.isEmpty()` — does the built frame decode to
*something*. A frame packed in the wrong bit order decodes to something. So
does one whose values were silently truncated to fit. Both passed, which is
how a wrong frame could leave this program.

It re-parses the built body and compares every requested field against what the
frame carries. Two rules, and the difference between them is the whole check:

- **Signed fields compare masked.** They are written from the caller's raw bit
  pattern and read back negative — `DIST_PKT_START` goes in as 31368 and comes
  out as −1400, the same fifteen bits. Comparing decimals would report every
  signed field as a fault, and the first version of this did exactly that: the
  real-captured-SLRP test failed the moment the check went in.
- **Unsigned fields compare as written, unmasked.** Masking here would defeat
  the point. Asking for `0x7FFFFFFF` in a four-bit field writes 15, and
  `0x7FFFFFFF` masked to four bits is *also* 15 — so a masked comparison calls
  a truncation a match. My second version had that bug too, and the test for
  over-wide values is what found it.

The failure names the fields and prints asked-against-carried, because "the
frame is not what you asked for" without saying which part sends the operator
back through every editor by hand.

Full suite: **78 suites, 2258 checks, 0 failed.**

### Ctrl+F opens the window

Asked for directly: a Find *dialog* on Ctrl+F, the way an editor does it.

Rather than replacing the bar, `activate()` opens whichever shape was used
last — the floating window by default, the in-tab bar for anyone who docks it.
Both are the same widget, so nothing had to be duplicated to get there.

The subtle part is what counts as *choosing*. `close()` re-docks a floating bar
on its way out, so that re-dock must not be recorded as a preference — Esc
means done searching, not "I would rather have the bar". Without that
distinction Find would open as a window the first time and as a bar for ever
after, which is exactly the kind of behaviour nobody reports because it looks
like the program just deciding. A `m_closing` flag draws the line, and the test
walks the whole sequence: default window, Esc, still a window, dock
deliberately, now a bar, detach, back to a window.

The floating window's title also names the tab — **Find — Loco 1 / Ctrl 1**.
There is a find bar per tab, so a window labelled only "Find" can be searching
a tab that is not even on screen.

The preference lives in `ui/find_detached`.

Full suite: **79 suites, 2266 checks, 0 failed.**

---

## Window frames, and room to work in the Packet Maker

| File | Change |
|---|---|
| `windowgeometry.{h,cpp}` | `makeResizableWindow()` |
| 15 tool windows | call it |
| `packetmakerdialog.{h,cpp}` | hide the sub-packet pane where it does not apply; fold the paste box; split the height |
| `tests/test_windowframes.cpp`, `test_packetvary.cpp` | 20 checks |

### No maximise button

Reported from use, and the cause is worth writing down: several of this
program's real work surfaces are `QDialog`s, and **a QDialog does not get
minimise or maximise on most window managers**. They already set `Qt::Window`,
which makes them independent windows — but the buttons come from a separate
hint set, and nothing in the code said so, which is why it looked done.

`WindowGeometry::makeResizableWindow()` adds `Qt::Window`,
`WindowMinMaxButtonsHint`, `WindowCloseButtonHint` and `WindowSystemMenuHint`
without disturbing flags already set, and it is called before the first show
because changing flags on a visible widget hides it. Applied to all fifteen
tool windows: Packet Maker, Field Sweep, Packet Sequence, Round-trip Validator,
Decode Workbench, fault panel, Loco Console, both search windows, Compare,
session viewer, sub-packet editor, DLR player, merged view and the field plot.

The test asserts the bug first — a plain `QDialog` has no maximise hint — so
the check would still mean something if the helper were ever reduced to a
no-op.

### The header form had nowhere to go

Two structural causes, not cosmetics.

**The sub-packet pane took a third of the width for every packet type.** Only
SLRP and AEP have sub-packets. For LSRP, ARP, DMI and everything else it was an
empty list sitting beside a squeezed form — and LINFO has 171 header fields.
The pane is now hidden where the packet has none, and the splitter hands its
width to the form.

**"Fill from buffer" held about 140 px permanently** for a paste box used once
at the start of a session. It folds now, and starts folded. Worth noting: a
checkable `QGroupBox` only *disables* its children, so the contents had to go
into an inner widget that is genuinely hidden — ticking the box without that
would have looked identical and freed nothing.

The middle and the preview also share the remaining height through a splitter
rather than the preview taking a fixed 200 px slice, so a 171-field form can
take the space instead.

Full suite: **81 suites, 2286 checks, 0 failed.** Menu audit passed.

---

## Maximise (properly), and Frame Diff with more than two frames

| File | Change |
|---|---|
| `windowgeometry.cpp` | replace the window TYPE, not OR into it |
| `framediff.{h,cpp}` | `compareMany()`, `summarizeMany()`, `oddOnesOut()` |
| `framediffwindow.{h,cpp}` | 2–6 frames, one column each |
| `mainwindow`, `sessionwindow` | send N selected rows to the diff |
| `tests/` | 23 new checks |

### Why maximise worked in some windows and not others

Reported precisely: it worked in the Decode Workbench and Frame Diff, and did
nothing in the Packet Maker. That difference is the whole diagnosis — those two
are `QMainWindow`s and the Packet Maker is a `QDialog`.

`Qt::Dialog` is `Qt::Window | 0x2`. So `flags | Qt::Window` — which is what the
helper did — leaves a QDialog still **typed** as a dialog, and a dialog is a
transient window that most window managers refuse to maximise however many
hints it carries. The button can be present and do nothing.

The type bits are masked out before the new type goes in. The test now asserts
`(flags & Qt::WindowType_Mask) == Qt::Window`, which is the thing that was
actually wrong; checking only for the maximise hint passed the whole time.

### Comparing three frames

Two frames answer "what changed". Three answer "**which of these is the odd one
out**", and "does this field change every frame or only at the transition" —
questions that otherwise mean comparing in pairs and holding the third in your
head. Two to six frames now, one column each, added and removed from the
window or sent straight from a multi-row selection in the log or a recording.

With three or more, the minority value is coloured and the majority left plain,
so the odd frame is visible without reading every column. `oddOnesOut()`
requires a **majority, not a plurality**: with values 40, 40, 45, 45 there is no
odd one out, and naming one would be inventing an answer.

Alignment is by field **name and occurrence**, not the LCS the pairwise path
uses. LCS does not generalise to N lists, and the usual dodge — aligning
everything against the first frame — makes the answer depend on which frame you
pasted first. A diff should not have that property, and there is a test that
reorders the frames and checks the counts do not move. `compare()` is untouched,
so a two-frame diff keeps the alignment that copes with a `<when>` branch
shifting every row after it.

Byte offsets stay a two-frame idea. With four frames, "bytes 3 and 9 differ"
does not say between which of them, so that line only appears for a pair.

Full suite: **83 suites, 2310 checks, 0 failed.** Menu audit passed.

### Find in a recording

The recorded-session window had every filtering tool except the one for "where
does this word appear". It has the same `FindBar` the live tabs use, per tab,
with the same shape preference — Ctrl+F opens it as a floating window or as a
bar, whichever was used last — and the floating window's title names the source
it is searching.

Find sits under **Edit**, not Tools. It is not a tool you open; it is how you
move around what is already on screen, and Edit is where an operator looks for
it. The test checks the menu entry as well as the shortcut, because a shortcut
that appears in no menu is one only its author knows about.

Full suite: **84 suites, 2317 checks, 0 failed.**

### The rest of the navigation, in a recording

F4 / Shift+F4, bookmarks (Ctrl+B, F2, Shift+F2) and Go to timestamp (Ctrl+G),
all under **Edit**, all on the same keys as the live window.

None of it is a second implementation. Three shared pieces came out of
MainWindow to make that true:

- **`nextBookmarkedRow()`** joins `nextMarkedRow()` in `markerscrollbar.cpp`.
  MainWindow's `stepBookmark()` now calls it too, so the two windows walk rows
  through one function.
- **`GotoTimestamp::spanOf()` and `resolveRow()`** came out of a 90-line slot.
  The tie-breaking is the part worth testing — "first at or after T" keeps the
  earliest row of a tied group, "last at or before" the highest — and it was
  not reachable from a test while it lived inside `onActionGotoTimestamp()`. It
  is now, and the fallback-to-nearest is pinned.
- **Bookmarks share the live store**, keyed by (tabKey, epochMs), so a row
  marked while reviewing a recording is the same row marked live. Stored marks
  are re-applied after loading, since the flag lives on `LogEntry` for cheap
  painting while the truth lives in the store — without that, a mark from a
  previous sitting comes back invisible and F2 walks straight past it.

One thing to watch when reading the diff: extracting the goto logic
accidentally swallowed `snapshotProxy()`, the export helper that happened to
sit between two functions in an anonymous namespace. The compiler caught it
immediately and it is restored, but it is the kind of edit that would have been
a silent loss in a language with looser linking.

Full suite: **85 suites, 2337 checks, 0 failed.** Menu audit passed.

---

## Packet Maker: one frame, up to four destinations

| File | Change |
|---|---|
| `udpsender.{h,cpp}` | `Target` list, `writeAll()`, `resolveAll()` |
| `packetmakerdialog.{h,cpp}` | three more destination rows, folded by default |
| `tests/test_multidest.cpp` | 17 checks |

A station talks to more than one loco. The decision that shapes the rest:
**sending the same frame to four peers is one transmission with four
recipients, not four transmissions.** So a tick builds ONE frame, writes it to
every target, and the send index advances **once**.

That is not a detail. `FRAME_NUM`, the MAC and the message-header `seq_num` all
derive from the send index, so advancing it per destination would hand each
peer a stream with holes in it — 1, 5, 9 — from a sender that believes it is
sending consecutively. There is a test that runs an interval to two targets and
checks the frame index goes 0, 1, 2.

For the same reason the status line counts **frames, not datagrams**, once more
than one destination is in play. "Sent 40" meaning forty frames to four places
and "Sent 40" meaning forty separate frames are different facts.

### Partial failure

Deliberate, and worth disagreeing with if you see it differently:

- A destination that will not resolve is **reported and dropped** at start. If
  none resolve, the run refuses to start rather than running a timer that can
  only emit errors — which would look like it was working.
- A write that fails mid-run is reported and the others carry on. The run stops
  only when **every** destination fails. One unreachable peer must not silence
  the rest; an unreported failure must not look like success.
- The same address twice is one destination. Typing it twice means one peer,
  and honouring it literally would double every datagram without saying so.

### The dialog

The existing Dest/Port pair is destination 1. Three more sit in an **Also send
to** group that starts folded, so a single-peer send looks and behaves exactly
as it did. Each has its own tick box: a host typed and left unticked is a
destination the operator prepared and did not choose, and sending to it anyway
is the worst kind of surprise in a tool that transmits.

The confirmation names every destination rather than counting them — "send to 4
places?" asks the operator to trust a number they cannot check.

Field Sweep and Packet Sequence are untouched: the single-destination overloads
remain and forward to the list form.

Full suite: **86 suites, 2354 checks, 0 failed.**

---

## FRAME_NUM follows the live traffic

| File | Change |
|---|---|
| `framenumberwatch.{h,cpp}` | the frame number ARP and LSRP are currently carrying |
| `mainwindow.cpp` | feed it from the ingest path |
| `packetmakerdialog.{h,cpp}` | seed from it, follow it, stop following when edited |
| `tests/test_framenumwatch.cpp` | 24 checks |

Seconds-since-midnight was the right *shape* and the wrong *number*. It is
whatever this laptop's clock says; the counter a peer checks a frame against is
whatever the equipment says. A frame built minutes away from the live value is
one a peer may treat as stale or replayed, and nothing on screen would explain
why it was ignored.

ARP and LSRP carry `FRAME_NUM` and one or the other arrives constantly, so the
live value is simply there to be read.

### How it reads it

The bit offset is **worked out from the schema** — walk the header summing
widths until `FRAME_NUM`, add the in-capture envelope — and then cached per
packet type. So a schema edit that moves the field moves this too, and an
observation costs a bounded bit read rather than a full decode. That matters
because it runs on the ingest path for every frame; the common case (a packet
that is neither ARP nor LSRP) costs two prefix compares and no parsing.

The test that earns its place checks the watch reads **the same number the
schema's own parser finds** in a real captured frame, rather than checking it
against a constant I typed in.

**Latest means most recent, not largest.** `FRAME_NUM` wraps, and "biggest so
far" would stick at the top of the range for the rest of the session the first
time it did.

### In the Packet Maker

The field is seeded from the live value, falls back to the clock when nothing
has been seen, and **says which it got** — a number taken from traffic and a
number taken from this laptop are different claims and must not look alike.

A **Follow live** box, ticked by default, keeps it tracking as frames arrive.
It stops the moment the operator types in the field, because an edit is a
decision and overwriting it a second later would be the worst behaviour
available here. It also holds off while an interval send is running, rather
than moving the number under a send in flight.

One trap worth recording: the spin-box editor's `valueChanged` fires for the
program's own writes as well as the operator's, so without a guard the first
live update would switch following off and the feature would have worked
exactly once — looking, from the outside, like it worked.

Live updates go into the editor only. The built frame is deliberately left
stale, so Send stays disabled until the operator rebuilds with the new number.

Full suite: **87 suites, 2378 checks, 0 failed.**

### The selected tab was a floating box

From a photo of the console in use: the current tab drew as a bordered
rectangle sitting above the content rather than as the tab that is open.

The cause was the "join the tab to the page" trick I had written two sessions
earlier — a full border on the selected tab with its bottom edge painted in the
page's colour, and the pane pulled up a pixel to sit under it. That only works
when the tab's fill and the pane's fill are the same colour and the two overlap
by exactly one pixel. Here they were not: the tab filled with Base and the pane
did not, so the seam showed and the tab read as a box.

Rebuilding the render locally reproduced it exactly, which is the part worth
noting — I had rendered the tab bar when I wrote that code and called it good,
but I rendered a bare `QTabWidget` rather than one with a page and a pane in
it, so the seam had nothing to show against.

The current tab is now marked the way editors and browsers mark one: a solid
accent underline, a page-coloured fill, heavier text, and **no border**.
Nothing depends on pixel-perfect overlap, so there is no seam to get wrong.

Full suite: **87 suites, 2378 checks, 0 failed.**

---

## Crash on startup after a session with tabs open

Reported as "untick save log automatically, close, and the next start
crashes". The setting was incidental — it was the *close* that mattered.

`restoreWorkspace()` was called immediately after `restoreLayout()`, which
reads as exactly the right place: restore the window, then restore its tabs.
It is thirty lines too early. `buildOrShowTab()` asks `m_dispatcher` for the
tab's model, and the dispatcher is not constructed until further down the
constructor — so the call dereferenced a null pointer and the process died
before the window appeared.

It survived every test I ran because **a console that has never seen traffic
saves an empty workspace and restores nothing**. The crash needs a real session
with tabs open to happen at all, and every headless run I have been doing ends
before a single packet arrives. Changing a setting sent the operator through a
close-and-reopen with tabs on screen, which is the first time the path was ever
taken.

The call moved to after the pipeline is built, and `restoreWorkspace()` now
refuses loudly if it is ever called early again rather than crashing.

### Making the audit catch it

`menuaudit` builds a real `MainWindow`, so seeding a workspace into the ini
before construction reproduces the exact condition. The check is **not** "it
did not crash" — with the null guard in place, the wrong ordering no longer
crashes, it just silently restores nothing, which looks fine until you notice
your tabs are gone. So the audit asserts both saved tabs actually come back.

Verified by putting the bug back: the audit fails with *"both saved tabs were
restored"*, and passes again with the fix.

Full suite: **87 suites, 2378 checks, 0 failed.** Both disk-logging settings
start cleanly.

### The increment section ignored the live frame number

Reported from use, and correct: the seeded rule was **increment from 0**. That
advances a counter of *our* own, which drifts away from the equipment's the
moment it starts — and the frame number a peer checks a frame against is the
one in the ARP and LSRP arriving now, not one this program invented. The
follow-live work applied to the form field and stopped at the vary table.

`PacketVary::Rule` gains a **Live** mode: every send carries the number
currently observed. The seeded rule for FRAME_NUM uses it, so an operator who
touches nothing gets the right behaviour, and `from live` is in the mode list
for any field.

Two decisions inside it:

- **The live value is passed in, not read.** `valueFor(sendIndex, bits,
  liveValue)` — `packetvariation.cpp` stays a pure value calculation with no
  knowledge of the ingest singleton, and the Packet Maker reads the watch once
  per send. A file that computes values from an index should not be reaching
  into live state to do it.
- **Live is the second exception** to "a rule is a pure function of the send
  index", alongside Random, and the header says so. It is a better-founded
  exception than Random: the value wanted is not one this program chooses at
  all.

With nothing observed the rule falls back to its `start`, which is seeded from
the same place the form's field is — so a cold start sends a plausible number
rather than zeros labelled as live.

Full suite: **88 suites, 2391 checks, 0 failed.**

#### Repeated frames, and the question I could not answer

A live rule with no step sends the observed number for as long as it stands. If
the loco sends at 1 Hz and this runs at 200 ms, four consecutive frames carry
the same number — fine if a peer only checks freshness, wrong if it requires
each frame to advance. That is a question about SIF 0533 and not one to settle
by guessing, so it is now a **choice** rather than an assumption:

- **step 0** (the default, and the literal reading): send what the equipment is
  sending.
- **step n**: add n per send *since the observed value last moved*.

The second is the part a plain counter cannot do. It advances between
observations and **re-anchors** every time the equipment moves on, so it cannot
drift — which is the whole failure the increment-from-zero rule had. There is a
test that walks it: 4242, then +3 within the same observation, then 5000 the
moment the equipment moves, rather than continuing from 4245.

`LiveFrame` carries the observed value and the count of sends against it. The
counting lives in the Packet Maker, which knows what a send is; the arithmetic
lives in `packetvariation.cpp`, which stays a pure calculation.

Full suite: **88 suites, 2397 checks, 0 failed.**

---

## The frame was stale by the time it went out

From the field: the loco's frame number moves on while the Packet Maker sits
there, and a frame more than about four seconds behind its counter is rejected.

Three causes, all of them the same shape — something between deciding to send
and the datagram leaving.

**Send Once transmitted whatever Build left behind.** It now rebuilds at the
moment of sending. Nothing else in the form can have changed since the last
successful build without disabling Send, so this only ever refreshes what moves
on its own — the frame number, and the MAC and CRC computed over it.

**The confirmation dialogs are gone**, from both Send Once and Start Interval.
That was asked for, and it is also part of the fix: a modal between the click
and the datagram is dead time in which the number goes stale, guarding a
decision the operator had already made by pressing Send. Both were removed
rather than one — two send buttons that ask differently is a worse trap than
either answer. What replaces the interval one is a status line stating what is
going out and where, and a button that reads **Stop**.

**The build now follows the number rather than the widget.** `readHeader()`
reads FRAME_NUM from the watch while Follow live is on, instead of from the
editor. The editor is only a display of it and is deliberately frozen during a
send, so reading the widget sent whatever was on screen when the run started.

### Continuous rebuild

When the observed frame number moves, the dialog rebuilds in place: new frame,
new MAC, new CRC, preview refreshed, Send still armed. Showing a new number
beside a frame built for the old one would be the worst of both.

It only does this once the operator has built at least once — before that, Send
is disabled and rebuilding on its own would be answering a question nobody
asked. If the rebuild fails, Send is disarmed rather than left armed with a
stale frame behind a fresh-looking number. The message is transient, because
this happens about once a second and a sticky one would bury whatever was being
read.

Full suite: **89 suites, 2401 checks, 0 failed.**

### Arm / disarm

The confirmations went because a modal is dead time in which the frame number
goes stale. That left Send as a single unguarded click, so the guard moved
somewhere it costs nothing: **arming is a separate, earlier act, and after it
the send is immediate.**

- Disarmed, both send buttons are dead however good the build is.
- The switch labels itself with its state — *"Arm to enable sending"* against
  *"ARMED — sends go out immediately"*, in the warning colour. A tick box that
  reads the same either way is one you have to look at twice, on a control that
  transmits.
- **Stop stays reachable while a run is in flight**, whatever the switch says.
  Disarming must never be able to strand a transmission.

What disarms, and what does not, is the part worth stating: arming refers to
the frame **in front of the operator**, so anything that replaces that frame
clears it — a different packet type, a buffer, a preset. Rebuilding for a new
frame number does **not**, because that is the same frame with the number it
should have had. Disarming on that would clear the switch about once a second
and guard nothing, and there is a test that pins it.

Full suite: **90 suites, 2412 checks, 0 failed.**

---

## Tools in the compare window

| File | Change |
|---|---|
| `comparewindow.{h,cpp}` | Edit and Tools menus, a find bar per pane |
| `findbar.cpp` | works with a view that binds a LogModel directly |
| `gototimestampdialog.{h,cpp}` | same |
| `tests/test_comparetools.cpp` | 14 checks |

A compare window is where a difference between two sources is actually
studied, and it had no way to search, no way to jump to a time and no way to
open a row — so that work moved back to a single tab, which is the thing the
window exists to avoid.

**Edit**: Find (Ctrl+F), Go to timestamp (Ctrl+G), Next/Previous problem
(F4 / Shift+F4). **Tools**: Selected row → Decode Workbench (Ctrl+Shift+B), and
one entry specific to this window — *Diff the selected rows across panes*,
since the panes already hold two sources and comparing what is selected in each
is the question the window was opened to ask. Every tool acts on the pane that
last had a selection; guessing wrong would send the operator somewhere they
were not looking.

Going to a timestamp is worth more here than anywhere else: with time-lock on,
one jump carries every pane to its nearest message.

### What made it cheap

The panes **bind their LogModel directly** — half a dozen places cast
`view->model()` to `LogModel` and would not survive a filter proxy being
slipped underneath them. Rather than give this window tools of its own, the
shared ones learned to accept either shape:

- `FindBar` resolves rows through a proxy when there is one and straight
  through the model when there is not.
- `GotoTimestamp::spanOf/resolveRow/epochAtProxyRow` now take the model the
  *view* holds, matching what `nextMarkedRow()` already did.

So the compare window, the log tabs and the session viewer run the same find,
the same problem-stepping and the same time resolution. Three copies of "go to
a time" would have agreed on the day they were written and not much longer.

Full suite: **91 suites, 2426 checks, 0 failed.** Menu audit passed.

---

<a id="session-29"></a>
## DL Console — session changes (patch 29)

Field Sweep — the second of the four tools. Walk one field across its values,
send each one, and score what came back.

| File | Change |
|---|---|
| `fieldsweep.{h,cpp}` | plan generation and verdict classification (pure) |
| `fieldsweepdialog.{h,cpp}` | the dialog: build, send, correlate, score |
| `mainwindow.{h,cpp}` | Tools → Field Sweep…, and the live entry feed |
| `tests/test_fieldsweep.cpp` | new suite (46 checks) |

### Not the same thing as PacketVary

`PacketVary` already varies a field from send to send, but it is a rule
evaluated per tick with nowhere to put an answer: it holds a session open, it
does not ask a question. A sweep is the question — *which values of this field
does the target accept?* — so it plans a finite list up front, sends one at a
time, and scores each value by what arrived afterwards.

### The plan

Four modes. **Boundary** is the default because that is where bit-packed field
bugs live: off-by-one at the top of the range, sign handling at -1, the code one
past the last defined enum. It generates min, min+1, midpoint, max-1, max — plus
-1, 0 and 1 for a signed field — deduplicated, so a 1-bit field plans two values
rather than five copies of two.

**Range** (from/to/step), **List** (typed values, decimal or `0x`), and **Enum
codes** — every declared value of the field's enum *plus the first undefined
code*. That last one is the point of the mode: a target that accepts a value the
spec never defined is the bug worth finding, and sweeping only the declared
codes cannot reach it.

Three refusals, all of which would otherwise produce a results table that lies:

- A value that does not fit the field's width. It would be truncated on the way
  to the wire, so the value tested would not be the value in the table.
- A range that expands past 512 values. A 17-bit field swept by 1 is 131072
  sends; the error says the number rather than queueing it.
- A zero step, or a step running away from the end value.

### Scoring

Each sent value opens an answer window (default 400 ms). Entries arriving inside
it are attributed to that step and the step is scored:

| verdict | meaning |
|---|---|
| reply | a captype you listed as an answer arrived |
| fault only | only fault traffic — the target understood the frame and rejected it |
| other traffic | something arrived, but not what was being watched for |
| silent | nothing at all |

"Fault only" is deliberately separate from "silent": a target that answers with
a fault has understood and rejected, which is a different fact from no answer,
and collapsing the two loses the more interesting half.

A build failure **stops** the run rather than skipping the value. A gap in the
results table that looks like a silent target, but was actually a frame that
never left, is the worst thing this tool could report.

### Using it

Tools → Field Sweep…, seeded from the selected log row when there is one — a
sweep that starts from a frame the target already accepted isolates the field
being swept, which a form full of zeros does not. Base values are also editable
by hand, or seedable from a pasted frame.

Header fields only. Sub-packet repeats have per-row values with no single name
to address; that is the same boundary `PacketVary` drew, for the same reason.

MAC key comes from the same captured-key-set picker as the Packet Maker.

**This transmits.** Explicit and opt-in, one datagram per value.

### Tests

Both halves are pure, so the two things that must not be wrong — the values sent
to a live target, and the meaning assigned to what came back — are checkable
without a socket or a window. 46 checks: boundary generation for signed and
unsigned, the 1-bit collapse, every refusal above, enum codes including the
undefined one, and the classification table (including an answer arriving after
the window, which belongs to no step, and a reply alongside a fault, which is
still a reply).

Full suite: **58 suites, 1815 checks, 0 failed.**

### Next

Round-trip validator, then the station responder.

---

<a id="session-28"></a>
## DL Console — session changes (patch 28)

Frame Diff — the first of the four tools. Field-by-field comparison of two
frames.

| File | Change |
|---|---|
| `framediff.{h,cpp}` | comparison core: alignment, summary, byte offsets, input parsing |
| `framediffwindow.{h,cpp}` | the window |
| `mainwindow.{h,cpp}` | Tools → Frame Diff…, and "Diff these two rows" in the row menu |
| `tests/test_framediff.cpp` | new suite (39 checks) |

### Why field-level

A byte diff of a bit-packed Kavach frame is close to useless. One changed 9-bit
field smears across two bytes; a conditional branch firing (LSRP's
`Loco_Health` group rotates with `FRAME_NUM`) changes which fields exist at all.
The bytes say "six bytes differ"; the answer wanted is "TRAIN_SPEED 40 → 45".

So the comparison is over decoded fields. Byte offsets are still reported, in
the status line, as a sanity check rather than as the headline.

### Alignment

The two field lists are not necessarily parallel — a `<when>` branch, a
different subpacket mix, or a different repeat count changes which rows exist.
Aligning positionally would report every row after an inserted one as changed.

The lists are aligned by an LCS over field names first, so a row present on one
side only is reported as such and the rows after it still line up. Field names
repeat within a frame (every subpacket has a `FRAME_OFFSET`), so the alignment
key carries the occurrence number — otherwise the LCS pairs subpacket 1's field
with subpacket 3's and calls both changed.

Above 2000 rows per side it falls back to positional comparison. That is a guard
against a corrupt length field turning a UI action into a freeze, not an
expected case.

### Using it

- **Row menu → "Diff these two rows"**, enabled only with exactly two rows
  selected. Picking two out of five would be a guess at which two were meant.
- **Tools → Frame Diff…** takes the selection if it is exactly two rows,
  otherwise opens empty to paste into.
- Each side accepts a capture line, or bare hex plus a type. Bare hex is
  reassembled into a capture line and decoded through the same path the log view
  uses, so there is one decode path and not a second subtly different one.
- "Show only differences" is on by default — the reason to open this window is
  one unexpected field.
- The window is reused rather than reopened: comparing frame after frame is the
  normal way to use it.

### Tests

The core is pure, so the interesting cases are all reachable without a window:
insertions on either side, repeated field names, empty sides, length
differences, and every input-parsing rejection.

Real LSRP frames from `replay/` back the end-to-end checks, so the alignment is
exercised against real conditional branches rather than only synthetic rows —
including a frame diffed against itself, which must show nothing.

The window gets its own checks, because a correct core does not mean the table
shows it: the default view must be shorter than the full field list, unticking
"only differences" must show everything, and two identical frames must leave it
empty rather than crash on a diff with nothing in it.

Full suite: **57 suites, 1769 checks, 0 failed.**

### Next

Field sweep, then the round-trip validator, then the station responder.

---

<a id="session-27"></a>
## DL Console — session changes (patch 27)

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

### Sets

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

### Decode Workbench

A **Session key** combo next to the type picker: *Auto (this frame's loco)* —
the log view's behaviour — or any captured set by name. Choosing one re-decodes
against that set, so a frame from earlier in the log can be checked against the
material that was in force when it was sent, which is the reason to paste an old
frame in there at all.

The MAC row now names the set behind the verdict (`PASS ✓  [key set #3]`).
On a log with several sets, "FAIL" without saying what it was checked against
is not a useful thing to have been told.

### Packet Maker

A **from log** combo beside the session-key field. Choosing a set copies its key
into that field. Deliberately a copy rather than a second source of truth: the
built frame is still signed with whatever the field holds, so a captured key can
be taken and then edited, and the builder keeps one input rather than two.

### Tests

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

---

<a id="session-26"></a>
## DL Console — session changes (patch 26)

Session key material is now tracked per loco, and the station id follows
last-write-wins like everything else.

| File | Change |
|---|---|
| `sessionkeystore.{h,cpp}` | one `LocoState` per loco; station id last-write-wins; `stationIdFromFrame()` |
| `capturedecoder.cpp` | `verifyMac()` is given the frame's loco id |
| `sessionkeydialog.cpp` | live status line uses `statusAll()` |
| `tests/test_sessionkeyloco.cpp` | new suite (28 checks) |
| `tests/test_sessionkeystore.cpp` | station-id block reworked for the new rule |

### Per loco

The store held one global state, so a capture carrying two locos mixed them:
loco 2's `@rand_num` overwrote loco 1's randoms, and the MAC badge on a loco 1
frame was then checked against a key derived from loco 2's material. Your
`replay/` directory is exactly this shape — loco 1 and loco 2 captures, stations
501 and 527 — so it was not hypothetical.

State is now keyed by the loco id in the capture tag (`@slrp_1_1` → loco 1).
Tags with no loco id file under -1, so a single-source log behaves as before.

Accessors take an optional `locoId`; `-1` (the default) means "the loco whose
state changed most recently", which keeps every existing call site correct on a
single-loco capture. `locos()` and `activeLoco()` expose the rest.

`verifyMac()` takes the loco explicitly, and `describe()` passes `c.locoId` —
the loco the frame was *captured on*, not whichever loco happened to transmit
last. On a two-loco log that is the difference between a real FAIL and a
meaningless one.

### Station id

Was first-write-wins: `m_stnFromStation` latched the first `SOURCE_STN_ID` seen
and never updated, so a loco handing over from station 501 to 527 kept the old
id — and the id parity (`loco_id + stn_id`) is what picks which key of the set
is used, so a stale station id silently selects the wrong key.

Now every station packet updates that loco's station id. One exception, which is
a judgement call worth knowing about: the ctrl id on a `@rand_num` tag is **not**
a station id (it is the ctrl index, typically 1). It is still accepted as a
placeholder until a station packet supplies a real `SOURCE_STN_ID`, but it no
longer overwrites one afterwards — otherwise the next `@rand_num` would drop the
id from 527 back to 1 and flip the parity. Last-write-wins holds among real
station ids.

#### Cost

The old latch existed to avoid a schema decode per frame. Reading the id from
every station packet would have made that per-frame cost real, so the id is now
read straight from its bit range instead:

```
SLRP:  PKT_TYPE(4) + PKT_LENGTH(10) + FRAME_NUM(17) -> bit 31, 16 bits
AAP:   PKT_TYPE(4) + PKT_LENGTH( 7) + FRAME_NUM(17) -> bit 28, 16 bits
```

(`PKT_LENGTH` is the field whose width differs between the two.) Measured over
all 130,951 lines in `replay/`: **34 ms** total for the whole observe pass.

Hand-rolled bit offsets drift away from the schema, so `test_sessionkeystore`
pins `stationIdFromFrame()` against a frame the schema *encoder* built — if a
field width changes, the test fails rather than the store quietly reading
garbage.

### Verified against replay/

Feeding every line of every `.cap`: two locos tracked separately, each ending on
station 527, having followed 501 → 527 during the capture.

Full suite: **54 suites, 1685 checks, 0 failed.**

### The window rule now runs on the log's clock

`rederive()` passed `QDateTime::currentDateTime()` into `derive()`, so the
set-1-vs-set-2 decision was made against the wall clock. Live that is the same
thing as the frame time. Replaying a month-old `.cap` it is not: "now" falls
outside both windows, the rule drops to set 2, and every MAC on that log is then
checked against the wrong key — silently, because falling back to set 2 is also
what the rule does legitimately.

Each `LocoState` now carries `lastSeen`, the RTC of the newest frame that fed
it, and the derivation is evaluated at that time. `logTime()` exposes it.
`QDateTime::currentDateTime()` remains the fallback for a stream with no usable
RTC, and the Session Key dialog's own "Now:" field is untouched — that one is
the operator asking a hypothetical, which is a different question.

`test_sessionkeyloco` places a capture a year in the past with set 1's window
around it and asserts set 1 is chosen. Reverting to the wall clock fails three
of its checks.

### Still true, not changed

- The MAC badge is computed when a row is inspected, so after a re-key an older
  SLRP row reads FAIL rather than "no longer verifiable". That follows from
  last-write-wins.

---

<a id="session-24"></a>
## DL Console — session changes (patch 24)

One fix on top of patch 23: the ARP / LSRP CRC.

| File | Change |
|---|---|
| `capturedecoder.cpp` | ARP/LSRP CRC trailer read big-endian, not little-endian |
| `schema/kavach.xml` | `anxc_jamcrc` declaration corrected (`xor_out`, `trailer_endian`) — descriptive only |
| `tests/test_crcheader.cpp` | new suite (19 checks), wired into `tests.pro` |

### The bug

`verifyCrc()` had the span right — JAMCRC from byte 10, the `PKT_TYPE` nibble,
skipping the 10-byte message header, which is a transport envelope and is not
covered — and ran it to the 4-byte trailer. But it compared against the trailer
read **little-endian**:

```cpp
c.crcOk = (jamcrc(b, 10, n - 14) == last4le());   // was
c.crcOk = (jamcrc(b, 10, n - 14) == last4be());   // now
```

Annexure-C packets store the word big-endian, like the rest of the frame, which
is msb-first throughout. AAP / SLRP / AEP were already `last4be()`; these two
were the odd ones out, so they reported FAIL on every frame.

### Verification

Not from the spec — from `replay/*.cap`, every frame in the directory:

| type | frames | `start=10` BE | `start=10` LE | `start=0` BE | `start=8` BE |
|---|---|---|---|---|---|
| LSRP | 3253 | **3253** | 0 | 0 | 0 |
| ARP | 9207 | **9207** | 0 | 0 | 91/200 |
| SLRP | 3340 | 0 | 0 | **all** | 0 |
| AAP | 46 | 0 | 0 | **all** | 0 |

The `start=8` column is a trap worth naming: the CRC init is 0, so leading zero
bytes do not change the result, and ARP bytes 8..9 are frequently `00 00`. A
span starting at byte 8 therefore passes for those frames by accident. It is not
evidence for that offset — and it is also a nice independent confirmation that
the init really is 0.

Byte 10 as the start is confirmed structurally too: at byte 10 the LSRP frame
reads `0xA3` → `PKT_TYPE = 0xA = 10`, matching `message_id = 10` in the header,
and matching the schema's `body_offset="80"`. At byte 8 it would read 0.

### Tests

`tests/test_crcheader.cpp` pins real frames from `replay/`: two LSRP, two ARP,
plus an SLRP and an AAP so a future edit to this branch cannot quietly take the
whole-frame types with it. Each verdict asserts `crcChecked` alongside `crcOk`,
so "passes" cannot mean "never checked". Corrupting a body byte must fail;
corrupting a **message header** byte must still pass — that check is what pins
the span start, and it is the half a wrong offset would break.

Confirmed the suite bites: reverted to `last4le()` and 4 checks fail; restored
and the full suite is 53 suites, 1653 checks, 0 failed.

---

<a id="session-23"></a>
## DL Console — session changes (patch 23)

Two changes on top of `DLConsole-patched-22`. Both are drop-in: rebuild with
`qmake && make`. Generated files (`Makefile`, `*.o`, `moc_*.cpp`, `qrc_*.cpp`,
`ui_*.h`) are stripped from the archive; they come back from the build.

Files changed:

| File | Change |
|---|---|
| `findbar.{h,cpp}` | view hold, model-driven rescan keeps position |
| `mainwindow.{h,cpp}` | scroll lock respects the hold; open-buffer plumbing, menus, Check Buffer buttons |
| `rawbytespanel.{h,cpp}` | hex-dump context menu |
| `logentry.{h,cpp}` | `entryBufferText()` |
| `decodeworkbench.{h,cpp}` | `loadBuffer()` |
| `packetmakerdialog.{h,cpp}` | `loadBuffer()`, `isSending()`, capture-preamble fix in `parseBuffer()` |
| `tests/` | `test_findhold.cpp`, `test_openbuffer.cpp`, wired into `tests.pro`; `menuaudit_main.cpp` checks the new Tools entries |

---

### 1. Find no longer fights scroll lock

**Symptom:** with scroll lock on and traffic arriving, the viewport alternated
between the found row and the end of the log.

**Two independent causes, both fixed:**

1. `MainWindow::onEntriesAppended()` called `view->scrollToBottom()` once per
   arriving batch whenever `scrollLock` was set, with no idea that Find had
   parked the view on a match. Find scrolled to the match, the next batch
   scrolled to the tail, ~30 times a second.

2. `FindBar` re-scans on `rowsInserted` through its 200 ms debounce, and
   `rebuildMatches()` unconditionally reset the cursor to match #0 and jumped
   there. On a live tab that dragged the operator back to the first match every
   200 ms — with *or without* scroll lock. This half was invisible behind the
   first one, and fixing only the first would have left it.

**What changed:**

- `FindBar::holdsView()` — true while the bar is visible and parked on a match.
  `holdChanged(bool)` fires on the transition.
- The debounce now tells apart a **user** rescan (typing, toggling a checkbox →
  scan and jump to the first match) from a **model** rescan (new traffic →
  `rescanKeepingPosition()`: re-scan, keep the cursor on the same entry, never
  move the viewport).
- `rescanKeepingPosition()` anchors on the `LogEntry`, not the row number.
  Rows renumber for two ordinary reasons — the model evicts the oldest entries
  at the per-tab cap, and a filter can drop rows above the cursor — so a
  remembered row number silently becomes a different message.
- `onEntriesAppended()` skips `scrollToBottom()` while the hold is on.
- Releasing the hold (Esc / closing the bar / clearing the text) emits
  `holdChanged(false)` and the tab snaps to the tail in one hop. Toggling scroll
  lock back on also catches up immediately, unless Find is holding.
- The bar shows a `following paused` hint when the tab has scroll lock on, so a
  deliberately frozen tab is explained rather than looking broken.

**Rule:** a request to look at one specific row is the more specific request, so
it wins over following the tail; Esc hands the view back.

### 2. Open a buffer directly in the Decode Workbench / Packet Maker

Previously the only route from a captured frame to either tool was copying hex
out of one window and pasting it into another.

**Four entry points, one code path:**

- **Log row right-click** → *Open in Decode Workbench* / *Open in Packet Maker*.
  Acts on the row under the cursor, not on the selection: each target window
  holds exactly one frame. Greyed out with a reason when the row has no bytes.
- **Raw bytes panel right-click** → the same two, plus *Copy hex* (bytes only,
  without the offset column and ASCII gutter).
- **Tools menu** → *Selected row → Decode Workbench* (`Ctrl+Shift+B`) and
  *Selected row → Packet Maker*. In the menu bar on purpose: the command palette
  (`Ctrl+P`) harvests the menu bar, so a context-menu-only action is invisible
  to it.
- **Check Buffer page** → two buttons beside *Convert*, for hex that arrives from
  tshark or out of somebody's email.

**What gets sent:** `entryBufferText()` (in `logentry.h`). A capture row already
*is* valid input for both tools — `@slrp_1_1 <ts> <seq> <hex…>` — and its `@tag`
names the packet type, which is the one thing neither tool can infer reliably,
so a capture row is passed through whole. Anything else falls back to the raw
datagram as spaced upper-case hex, message header included: trimming it is a
judgement the operator can make and the function cannot.

**Window reuse:** both targets reuse the window last opened this way while it is
still alive — right-clicking row after row should not bury the desktop. The
Tools → *Decode Workbench…* / *Packet Maker…* entries still open fresh,
independent windows, as before. One exception: a Packet Maker that is mid-send
(`isSending()`) is never reused, because rewriting its form under a live
interval send would change what goes on the wire without the operator saying so.

**Bug found on the way:** `PacketMakerDialog::parseBuffer()` mangled a pasted
capture line. The line carries a decimal sequence number between the timestamp
and the bytes, and a seq like `6933` is also perfectly good hex, so the
"one unbroken hex run" branch ate it and prepended `0x69 0x33` to the frame — a
two-byte shift that decodes into plausible-looking nonsense. The preamble is now
recognised the way `CaptureDecoder::parseLine()` recognises it (`@tag`, ISO
timestamp, numeric sequence) rather than by position, so `@slrp 91 99 C5` keeps
all of its bytes.

---

### Tests

`tests/test_findhold.cpp` (18 checks) pins both halves of change 1: that
`holdsView()` is true while parked and false after Esc, that `holdChanged` fires
on the transitions, and that an insert burst leaves the cursor on the same entry
and the viewport where it was. The 200 ms debounce is waited out rather than
poked, so the timer path itself is under test.

`tests/test_openbuffer.cpp` (15 checks) pins `entryBufferText()` (capture line
passed through, raw datagram hexed, null and empty entries yield nothing) and
`parseBuffer()` against the capture-preamble case above.

Full suite: **52 suites, 1634 checks, 0 failed**, plus `menuaudit`, which now
also asserts the two new Tools entries exist.

### Known, untouched

`Ctrl+Shift+R` is bound twice in `mainwindow.cpp` — *Reload schema* and
*Stream session (.dlr) as live…*. Qt resolves an ambiguous shortcut by firing
neither, so both are dead as shortcuts today. Not changed here because picking
which one keeps the binding is your call.

---

<a id="sessions-1-22"></a>
## Before patch 23 — the first session changes

Three changes were applied on top of the original DL Console source. All are
drop-in: rebuild with `qmake && make` (the generated `Makefile`, `*.o`,
`moc_*.cpp`, `qrc_*.cpp` and `ui_*.h` were stripped from this archive because
they are regenerated by the build).

Files changed:
- `replaywindow.cpp`  (two unrelated changes: bug fix + profile lanes)
- `faultpanelwindow.cpp`
- `faultpanelwindow.h`

---

### 1. Replay: combined captures no longer collapse the spatial axis
File: `replaywindow.cpp`

Symptom: loading two loco captures together gave a negative track start
(e.g. -6.65 km) and squashed all data to the right edge.

Root cause: a null/garbage RFID frame (`01 00 00 ...`, all-zero tag) decodes
as a *valid* RFID with `absLoc = 0`. In `loadFiles` pass-2 it set
`lastPos[k] = 0`, poisoning the per-key carry-forward; the zeros dragged the
axis floor to ~0. (loco_2's capture contains such a frame, so loading loco_2
alone was also affected.)

Fix:
- New `rfidHasFix()` guard — an RFID only updates position when `absLoc` is a
  real fix (not `0`, not the 23-bit sentinel `0x7FFFFF`), mirroring the
  existing `locoLocation()` guard for arp/lsrp.
- Null balises no longer place a phantom marker on the track.
- `recomputeRange()` ignores non-positive positions in Spatial mode — a
  defensive invariant so a stray zero can never blow up the axis again.

### 2. Replay: SLRP look-ahead shown as Kavach-4.0 stacked colour lanes
File: `replaywindow.cpp`

The combined speed-vs-distance graph was replaced with stacked, colour-coded
lanes (one band per overlay), matching the Kavach 4.0 "performance" view:

| Lane | Colour | Source field |
|------|--------|--------------|
| MA   | green  (127,255,0)  | `SlrpProfile.haveMA / maWrtSig / authType / authSpeed` |
| SSP  | red    (255,99,132) | `SlrpProfile.ssp` (km/h labels) |
| GRAD | amber  (255,206,86) | `SlrpProfile.grad` (`g<value>` + up/down) |
| TAGS | blue   (54,162,235) | `SlrpProfile.tags` (next tag id) |
| TSR  | orange (245,124,0)  | `SlrpProfile.tsr` (only when present) |

Lanes appear only when that overlay has data; each has a left-gutter label and
shares the `ref` distance-origin marker.

Not done: TO (take-over) is not drawn — `SlrpProfile` has no TO field; it would
require extending the SLRP decoder in `capturedecoder.cpp` first.

### 3. Active Faults window: follows the app theme
Files: `faultpanelwindow.cpp`, `faultpanelwindow.h`

The fault window hardcoded a dark skin and so was a dark island in Light mode.
It now derives every surface colour from the active `QPalette` (table, labels,
combo, checkbox, empty-state, borders), with three theme-tuned semantic accents
(red active / green "no faults" / grey cleared). A `changeEvent` override
re-skins it live when the theme is toggled in Settings.

### 4. Replay: profile lane geometry corrected (Annexure-C composition)
File: `replaywindow.cpp`

The stacked lanes were plotting the decoded fields at their raw values, but
those fields are not axis positions, so every segment collapsed onto the next
("mushed"). They are now composed per Annexure-C before drawing:

- SSP / GRAD: `.d` is a per-segment LENGTH -> running sum from the block origin
  (segment i ends where segment i+1 begins).
- TAGS: `.d` is DIST_NXT_RFID (per-hop; first hop from LAST_REF_RFID) -> accumulate.
- TSR (and LC/TO/TC when added): `.d` is a start distance from ref, `.len` the length.
- MA: green bar from the block origin to MA_W_R_T_SIG (authority end vs ref).
- Block origin = DIST_PKT_START relative to LAST_REF_RFID (ref = 0).

Validated against a real loco_1_1 SLRP packet (ref 910): SSP 5x110 km/h chains
0->890->1890->...->4890 m, gradient g7, tags 910/400/402/404/406/408/410...,
and the MA end (4890 m) exactly matches the SSP/grad cumulative end.

Note on sign: spec reads "+DIST_PKT_START"; it is 0 in every sampled capture so
the sign isn't exercised. If a non-zero case shifts the block the wrong way,
negate `blockOrigin` (one line, commented in source).

### 5. Inspector tree: absolute locations for SLRP look-ahead elements
Files: `capturedecoder.h`, `capturedecoder.cpp`, `replaywindow.h`, `replaywindow.cpp`

The packet inspector now annotates each look-ahead element with its absolute
track location, derived from the reference RFID's abs_loc (looked up from the
loaded RFID history), DIST_PKT_START and PKT_DIR:

- SSP / GRAD  -> "[<start> -> <end> m]"   (segment lengths chained from block origin)
- TO / TSR / TC / LC -> "[<start> -> <end> m]"  (start + length from block origin; LC is a point)
- Tag linking -> per-tag "abs=<loc> m", accumulated from the reference RFID
- Tag linking -> the 1-bit field is now labelled DUP_TAG_DIR ("Nominal(+)" /
  "Reverse(-)") instead of the opaque "flag".

Direction handling: travel runs with rising abs_loc in nominal and falling
abs_loc in reverse (verified - in a real reverse capture, ref RFID 989 at
162660 m and the tag chain 991/1/3 land on 162233/162021/161741, i.e. ref minus
the cumulative DIST_NXT_RFID). The block origin is ref -/+ DIST_PKT_START per
direction, matching change 4.

Plumbing: describe() takes an optional `QHash<int,qint64>* tagLoc` (unique ->
abs_loc); ReplayWindow builds it during load and passes it. Callers without a
map (e.g. the live loco console) simply omit the abs annotations.

### 6. SLRP inspector is now schema-driven (kavachschema.h)
Files: `kavachschema.h` (new), `capturedecoder.cpp`, `DLConsole.pro`

The hand-written SLRP sub-decoders (subMA/subSSP/.../subTSR + SlrpGeo) were
replaced by a declarative schema. The protocol layout now lives entirely in
`kavachschema.h` as field/structure tables; a small generic walker turns the
tables into the inspector rows, including the conditional fields, counted
repeats, enum formatting and the abs-location annotations from change 5.

Adding a field is one line in a table; adding a structure is a table + a
SegmentDef + one entry in `SLRP_SUB[]`. See `SCHEMA_GUIDE.md`.

`decodeSLRP()` is now a thin delegate to `kschema::decodeSlrp()`. The schema
reuses the existing value formatters (mapSpeed6, sigInfo, tcType, ...) via
function pointers, so output is unchanged. Validated against the real ref-989
reverse packet: header, MA, SSP (4 segs chaining 162660->159940), gradient (5),
all 11 tags with cumulative abs, and the Fouling-Mark track condition all
reproduce the previous hand-written output exactly. The numeric lane decoder
(`profileOf`) is untouched.

Minor: the rare TagLinking tail now lists ADJ_LINE_CNT and any line TINs as
fields rather than the old "(none)" string; cosmetic only.

### 7. TagLinking: the "trailing pad byte" is a 9-bit TIN
Files: `schema/kavach.xml`, `schema/kschema_oracle.py`

Type-5 TagLinking sub-packets always declared one byte more than the modelled
fields could account for, which looked like a trailing pad byte. It is not
padding — it is an always-present 9-bit TIN for the line the tag chain runs on,
emitted after the ADJ_LINE_CNT line-TIN rows and present even when
ADJ_LINE_CNT==0.

Evidence, from 19 real type-5 sub-packets (12 distinct shapes) across the
loco_1_1 / loco_2_1 replay captures and the 81_2 station log:

- Modelled without the field, the leftover after the struct was 9..16 bits —
  always exactly 8 more than byte alignment needs. Types 0/1/2/4/6 in the same
  frames were never short.
- The bits are not zero and not random: 52 / 58 / 83 at station 527, 110 at
  station 501, stable per route.
- When ADJ_LINE_CNT==1 the adjacent TIN reads 84 and this field reads 83 —
  neighbouring line numbers, which padding would not be.
- With the field added, the leftover is 0..7 bits of ordinary alignment in all
  19 samples.

If Annexure-C words this as "ADJ_LINE_CNT+1 line TINs" the wire bytes are
identical; the comment in `kavach.xml` says how to switch to that reading.

Because the SLRP inspector, the encoder and the Packet Maker all read the same
`kavach.xml`, the one-line addition fixes decode, encode and the builder form
together. `profileOf()` is unaffected — it stops after the tag repeat and jumps
by the declared length.

### 8. Packet Maker: fill every field from a pasted buffer
Files: `schema/schemaencoder.{h,cpp}`, `packetmakerdialog.{h,cpp}`,
`tests/test_bufferload.cpp`, `tests/tests.pro`

`Schema::Encoder::parseBody()` is the read side of `encodeBody()`: it walks the
SAME DOM in the same order, with the same `when` gating and the same
count-driven repeats, and returns a `ParsedPacket` holding exactly the
`header` hash and `QVector<SubEntry>` the encoder and `PacketBuilder` take as
input. So a captured frame can be pasted in, edited field by field, and rebuilt.

The Packet Maker gained a "Fill from buffer" box. It accepts bare
space-separated hex, a whole `@slrp_2_1 <ts> <seq> 91 B9 …` capture line, a
0x/comma C array, or one unbroken hex run; non-byte tokens are dropped, so the
tag, timestamp and sequence number need no special parsing. It reads the
captype from the `@tag` when there is one, otherwise guesses via
`detectCaptype()` (a packet whose PKT_LENGTH agrees with the buffer size wins),
otherwise uses the combo. If the buffer still carries the 8-byte message header
it is stripped — but only when message_id matches the captype AND
message_length accounts for the whole buffer — and src/dest/seq are copied into
the header controls.

Nothing is trusted: PKT_LENGTH, the MAC and the CRC in the pasted bytes are
discarded, and Build && Verify recomputes them and self-verifies as before.
Send stays disabled until that passes.

Diagnostics: when a sub-packet leaves 8 or more unread bits before the next one,
that is reported as "the struct is missing a field" rather than silently
skipped — it is the symptom the TagLinking TIN produced, and it is now
surfaced instead of accumulating.

Validated by replaying every SLRP frame available: 3390 frames from the replay
captures and the two uploaded logs parse and re-encode BYTE-IDENTICAL, with no
warnings, including all 19 that carry TagLinking. Deleting the TIN line from the
schema makes exactly the tag-linking frames fail, each one byte short — the same
defect reported two independent ways.

---

### Tools → Stream session (.dlr) as live  (Ctrl+Shift+R)

`dlrplayer.{h,cpp}` + `dlrplayerdialog.{h,cpp}`. Re-transmits the datagrams in
one or more .dlr archives over UDP at their recorded inter-arrival timing, so
the whole live path runs on known traffic: socket drain, dest-id filter,
backpressure counter, the ~30 ms dispatcher batching, colour rules, LogWriter,
the .log and .dlr recorders, Loco Console, session key derivation.

It sends on a real socket rather than injecting downstream. SessionWindow
already replays a .dlr as a strict consumer, which is the right shape for
reading yesterday's traffic and the wrong shape for testing, because
everything interesting about the live path is upstream of the models. Nothing
downstream is told it is a replay — the moment a component can tell, that
component is no longer the one being tested.

Verified this is safe to do verbatim: across both uploaded archives,
destination_id is 101 (kThisConsoleId) on all 9566 records and message_len
always equals wireLen - sizeof(STRUCT_MESSAGE_HEADER), so a resent record
passes every check in `UDPCommunication::drainSocket()` and lands in the tab it
came from.

The consequence is stated permanently in the dialog rather than as a
click-through: replayed packets ARE live traffic, so they are recorded into
today's files and interleave with anything real. That is the point when testing
recording and a hazard on a console someone is watching; the target port is
editable so a second instance can be the sink instead.

Multiple archives are k-way merged on arrivalMs, streaming (one look-ahead
record per file, never the whole archive in RAM). A session is one .dlr per
tab and the two from one recording start on the same millisecond and interleave
throughout, so playing them back to back would destroy exactly the cross-source
ordering worth reproducing.

Scheduling is absolute against a monotonic clock, not a sleep per gap: at
~43 rec/s a 1 ms per-record bias would drift visibly inside a minute. Controls:
speed 0.25x–25x (live-adjustable) plus a burst mode with no pacing at all,
which is the setting that pushes the receiver into its drop path; an idle-gap
cap so a quiet archive does not replay its dead air; seek; loop. Progress
reports max observed lateness, which is the honest measure of whether the
timing claim still holds.

Measured on the two uploaded archives (9566 records, 221.6 s span):
  25x     → 9566/9566 delivered, 0 send errors, 9.26 s wall, max lateness 1 ms
  burst   → 9566/9566 delivered, 0 send errors, 0.44 s wall (~222k pkt/s)

`tests/test_dlrplayer.cpp`: 50 checks over byte fidelity, header survival
(dest/kvch/message_len), merge order across two archives, same-millisecond
grouping, gap reproduction, the speed multiplier, burst ignoring the timeline,
gap capping, seeking, prompt stop from inside a 30 s gap, and a truncated
archive still playing its intact prefix.

---

### Seek to next matching record

The .dlr player can now seek by content, not just by time offset. A time
offset assumes you know when the thing happened; usually you know what it
was, and at 1x a four-minute archive is a four-minute wait to find out.

The seek field takes a **LogQuery** — the same language as the filter bar and
archive search — so the expression that found the row is the expression that
seeks to it: `sev:error`, `src:21_2 RFID`, `field:TRAIN_SPEED>60`,
`"DISTANCE TO TAG"`. Reusing it rather than inventing a second matcher means
the two can never disagree about what a query means.

Semantics: records before the first match are read and discarded, not sent.
Once one matches, the seek is armed and everything from there plays normally,
matching or not — the point is to reach an event and watch what follows, not
to filter down to hits. `Skip to next match` does the same jump mid-playback
and breaks a wait rather than queueing behind it, so it is immediate even
parked in a 20 s idle gap.

Pacing is now anchored rather than pass-based. The anchor is re-set whenever
the stream jumps, which is the part that quietly breaks: without it the
virtual clock has leapt forward while the wall clock has not, every remaining
record reads as overdue, and the rest of the archive floods out in one burst —
still delivering every packet, so a test that only counted datagrams would
pass. `dlrseek` checks the timing after landing, not just the count.

The ColorRules copy follows ArchiveSearcher's precedent: classification runs
per record on the worker thread, so borrowing a pointer to a MainWindow member
would be a live cross-thread read that a rules reload could trip.

An unparseable query is refused before playback starts and validated as typed.
A query that parses but never matches emits `seekExhausted` — that run sends
nothing, which is otherwise indistinguishable from a broken player.

Measured on the two uploaded archives (9566 records, 221.6 s):
  seek "[0.023912]" at 1x → landed at 60367 ms after examining 1327 records,
    in 5.1 s wall — the position it names is 60.4 s into the archive, so the
    seek did in five seconds what real-time playback would take a minute to
    reach, and pacing resumed at true speed from there
  seek "DISTANCE TO TAG" at 25x → landed at 0 ms, 3 skipped, 9563 sent,
    9563 received, 9566 accounted for

Also worth recording: at burst speed a naive receiver drops packets (8805 of
9563 arrived in one test harness, even with a 16 MB receive buffer). That is
burst mode working as intended — it exists to overrun a receiver — but it
means burst runs measure the consumer, not the player.

`tests/test_dlrplayer.cpp` gains the `dlrseek` suite: 43 checks over query
validation and refusal, landing position and matched text, non-matching
records still playing after the match, re-anchored pacing, a query with no
hits, seeking on a header field across merged archives, and skip-to-next-match
across a 20 s gap with a prompt stop.

---

### Decoded fields panel in the archive viewer and the streaming dialog

Both now carry the same RawBytesPanel + FieldInspector pair the live console
has, tabbed together and byte-linked — clicking a decoded field highlights the
bytes that produced it.

**SessionWindow** had the bytes and no way to decode them, which undercut the
reason for storing bytes rather than text: re-running a corrected decoder over
an old capture meant exporting the frame and pasting it into the workbench.

**DlrPlayerDialog** gains it behind a "Show decode" toggle, hidden by default.
The player keeps a one-record tap that is only armed while the panel is
visible and following, and the dialog samples it on the existing ~100 ms
progress tick. Decoding every record was never an option — burst mode moves
~200k records/s and a panel renders maybe ten — and sampling costs one
QByteArray copy per record inside a lock that was already being taken for
stats, versus a schema walk per record.

The record a content seek lands on is captured regardless of the tap, and
shown regardless of whether the panel is following. That frame is the reason
the seek was run; it must not depend on the panel being open at the right
moment. "Follow the stream" can be unchecked to hold it while playback
continues.

Sharing the widgets rather than growing a third decode view is the point: a
frame has to read the same in the archive, in the replay, and in the live tab,
or a replay stops being evidence of what the live view will show.

Worth knowing about what the panel can say: FieldInspector decodes from the
capture line in the entry's TEXT (`@slrp_2_1 <ts> <seq> 91 B9 …`), not from
the datagram bytes, because captype comes from that token. Plain text
diagnostics get a plain "not a capture frame" note rather than an error, and
are not counted as decode failures. In the two uploaded archives 1136 of 9566
records are capture frames (dop1/dop2 725+725 across both, dmi 222, nmshlth
221, nmsrssi 221, linfo 221, arp 98, slrp 50, lsrp 48); the remaining 6944 are
text and will correctly show the note.

`tests/test_dlrplayer.cpp` gains `dlrtap`: 29 checks that the tap is off by
default and captures nothing then, that when on it holds the LAST record sent
with its own archived arrival time (not the wall clock), that a tapped record
rebuilds an entry with source/kvch/time/rawBytes intact, that a seek landing
is captured with the tap OFF and is not overwritten by what follows, and that
a capture line survives the round trip through the tap still parsing as one
with its type token and frame bytes.

---

### Packet Maker: interval rebuild, per-send variation, field filter, diff, presets, sequences

**Interval send no longer repeats one buffer.** `UdpSender::startInterval()`
now takes a `FrameFn` and asks for the frame each tick. Before, the
message-header seq advanced but the packet body did not — FRAME_NUM stayed
put, and the MAC and CRC computed over that body were byte-identical on every
datagram, so a peer using frame number for freshness saw a stream that never
moved. The old behaviour is still reachable as `startIntervalFixed()`, because
some tests genuinely want identical bytes twice; it is now a choice rather
than a defect. A build that fails mid-run stops the run instead of falling
back to stale bytes.

**`packetvariation.{h,cpp}` — per-send field rules.** Increment, sweep, and
random, each clamped to the field's declared bit width. A rule is a pure
function of the send index (random excepted, deliberately), so send N is
computable without replaying 0..N-1 — which matters when a run is checked
against a capture afterwards. Increment works in the span rather than masking
a runaway counter, so the sequence stays contiguous and a receiver watching
for gaps does not see phantom ones. When a packet has FRAME_NUM the table
pre-seeds an increment rule, so the common case needs no setup and the
mechanism explains itself. PKT_LENGTH and MAC_CODE are excluded from the field
list: the builder recomputes them, so varying them would be overwritten.
Variation is applied to a copy of the form's values, never written back.

**Field filter over the header form.** linfo carries 171 header fields, dmi 88,
ccsys 56. A filter box plus "Changed only".

**Diff against the loaded buffer.** The pair to Fill from buffer: what was
parsed is kept as a reference, changed fields are tinted, and Diff… lists
every difference in spec order with both values. Sub-packets are compared by
shape first — a different set of types is more useful than a hundred field
diffs against the wrong sub-packet. The reference is dropped on a packet-type
change, since it would otherwise diff against a different layout.

**`packetpreset.{h,cpp}` — presets as a shared format.** Its own type rather
than save/load methods on the dialog, because two things read it now: the
dialog, and the sequence runner, which has no widgets to load into. Two
writers of one format is how a format drifts. The SESSION KEY is deliberately
not stored — it is a shared secret and a preset is a file that gets mailed
around. Fields in a preset that the current schema does not define are
reported, not dropped silently.

**`packetsequencedialog.{h,cpp}` — Tools → Packet Sequence.** A list of presets
with a repeat count, an interval within the step, and a delay after it. Steps
are FILE REFERENCES, reloaded at Run: a sequence that inlined its presets
would go stale the moment one was corrected, and the correction would silently
not apply. Every step is built and self-verified through PacketBuilder exactly
as Build does, and a step that fails to build stops the run rather than being
skipped — a sequence that silently omitted a frame would still report success
while testing something other than what was asked.

Tests: `packetvary` (19), `packetpreset` (31), `intervalsend` (12). The
interval checks are about the bytes DIFFERING, because the original defect was
invisible in a datagram count — every packet arrived, on time, with an
advancing envelope sequence. Included: the trailing CRC differs between
consecutive sends (a frame with a moved FRAME_NUM and a stale CRC would be
internally inconsistent), a preset with no rules still produces identical
sends, and a failing build stops on the first refusal rather than retrying.
Full suite 49 suites, 1579 checks, 0 failed.

---

### Packet Maker: readable change marks, and the sub-packet editor as its own window

**The "changed field" mark was unreadable.** It hardcoded a dark amber
background and left the foreground alone, which under a light palette is
dark-on-dark: the field was marked and simultaneously made illegible, which is
worse than not marking it. The tint is now derived from `QPalette::Base` and
always sets foreground and background together, never one without the other,
so the failure is impossible rather than merely unlikely. Clearing restores an
empty stylesheet rather than guessing at "normal" colours, so a field that
stops being changed looks exactly like one that never was. The label also
carries a ● marker — colour alone fails for anyone who cannot distinguish it,
and the marker survives a theme the tint was not written for.

**`subpacketwindow.{h,cpp}` — the sub-packet editor detaches.** It was sharing
the right half of a splitter with a dialog that also had to show the header
form, the buffer box, the preview, the vary table and two rows of buttons.
Sub-packets are where the volume is — a repeat table wants to be wide and tall
and was getting neither. Clicking a sub-packet (or Add…, or Edit fields…) now
opens a 720×620 window with the full field form and a 240px-minimum repeat
table. The list pane is just a list, which also gives the header form room.

One window, retargeted as the selection moves, not one per sub-packet: a
packet can carry eight, and eight stacked windows is a worse problem than the
one being solved. It edits the sub-packet vector in place through a pointer
rather than holding a copy to be applied on OK — the old inline editor
committed on every selection change precisely so nothing could be silently
lost, and that property was worth keeping. Closing commits; there is no cancel,
because there never was one.

That trade swaps a layout problem for a lifetime one. The window holds a
pointer and an index, so removing a row, loading a preset or switching packet
type can leave it pointing at a different sub-packet that now occupies that
index — which would not crash, it would quietly write MovementAuthority values
into a TagLinking. Every path that mutates the vector now clears the target
first, and out-of-range indices are refused at `setTarget` rather than stored.

The field-editor factory is shared with the Packet Maker rather than
duplicated: two factories would drift on enums, signedness and hex handling,
and the sub-packet editor is exactly where that would bite.

`tests/test_subpacketwindow.cpp`: 22 checks, aimed at the lifetime hazard
rather than the widgets — targeting refuses out-of-range and negative indices,
commit with no target changes nothing, a targeted commit round-trips its own
sub-packet and leaves the other one alone, retargeting neither reorders nor
drops entries, and a commit after the vector shrinks under the window does not
write through the stale index. Also fixed a spurious
`QFormLayout::takeAt: Invalid index 0` warning from the drain-until-null idiom,
here and in the header form.

Full suite 50 suites, 1601 checks, 0 failed.

---

<a id="undated-linfo"></a>
## Appendix — undated note: @linfo (LOCO_INFO) live, replay and CRC

Shipped as `LINFO_CHANGES.md` alongside one of the patches; which one was not
recorded, so it is kept here rather than guessed into the sequence.

Full project tree. Build as usual:  qmake && make   (or open DLConsole.pro)

Changed vs your upload:
- capturedecoder.cpp    : loco_info_crc recipe in parseLine() -> jamcrc(b,0,n-4)==last4le()
- capturedecoder.h      : recipe documented in header comment
- lococonsolewindow.cpp : CapType::Linfo added to kTabOrder + kLinkOrder (live view)
- schema/kavach.xml     : LINFO comment updated (CRC validated; golden 0xC7C020BC)

Excluded from this zip (unchanged, you already have them):
- replay/*.cap  and  SAVED_LOGS/*.log   (your capture data, ~19 MB)
- *.o objects, the stale DLConsole.app Mac binary, __pycache__  (regenerated on build)

Effect: @linfo now appears in the live console (was replay-only) and the CRC
row reads PASS instead of n/a in both views.
