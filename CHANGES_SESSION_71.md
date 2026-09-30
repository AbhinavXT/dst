# Session 71 — Adding a LOCO_INFO member: one line and one command

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

## One line holds everything about a field

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

## `lococonfig/sync_linfo.py`

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

## Tests no longer hard-code the struct

`test_lococonfig.cpp` takes the size, the CRC offset and the CRC from the
layout and the fixture. Stale generated files fail with "run
sync_linfo.py". An import of a file with the wrong size is refused as
"written for a different LOCO_INFO version". The UI says "Default" instead
of "v12 default", and the Manage menu names the tool the defaults came
from.

## Proved end to end

In a throwaway copy of the tree:
- I added `uint16_t max_brake_cylinder_pressure = 380` to a v13 copy of
  the tool, then one schema line, then ran the script. Nothing else was
  edited.
- The script reported 172 members agreeing and a 372-byte body, and wrote
  the defaults (`source: loco_config_v13.cpp`, value 380) and the fixture.
- The whole unit suite then passed: 125 suites, 3405 checks, 0 failed.

That copy was discarded; this tree still describes v12.

## Files

- **Changed:** `schema/kavach.xml` (LINFO attributes and comment),
  `lococonfig/lococonfigcore.{h,cpp}`, `lococonfig/lococonfigwindow.{h,cpp}`,
  `lococonfig/lococonfigmodel.{h,cpp}`, `lococonfig/lococonfig.{pri,qrc}`,
  `tests/test_lococonfig.cpp`.
- **New:** `lococonfig/sync_linfo.py`, `lococonfig/ADDING_A_FIELD.md`.
- **Renamed/generated:** `lococonfig/loco_defaults.json` (was
  `loco_defaults_v12.json`) and `tests/fixtures/loco_info_default.bin`
  (was `loco_info_v12.bin`), both now written by the script.
- **Removed:** `lococonfig/loco_fields.json`.

## verify.sh

Full run, **14/14 stages green, 0 failed**:

- golden validators: 11/11 ok (the new schema attributes change no decode)
- unit suite: 125 suites, 3405 checks, 0 failed
- menu audit: 70 ok, 0 failed
- headless smoke: 500 datagrams, app alive at timeout

Built with Qt 5.15 only; the Qt 6.4 build was not run here. `sync_linfo.py`
needs python3 and g++ (or `--compiler clang++`); on Windows, MSYS2/MinGW
provides both.
