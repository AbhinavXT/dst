# Adding a member to LOCO_INFO

In DLConsole, a new LOCO_INFO member takes **one line in `schema/kavach.xml`
and one command.** The capture decoder (`@linfo`) and the Loco
Configuration editor both read that line; the command checks it against
your C struct and takes the default value from your tool.

## From the Loco Configuration window (session 307)

**Manage ▸ Fields ▸ Add a field…** (or right-click a row ▸ *Add a field
after this one…*) does step 2 for you. You give the name, the C type, the
field it follows, the group, a note and a default. It shows the exact line
and LOCO_INFO's new size, then writes the line into the kavach.xml DLConsole
is using, keeping a backup beside it (`kavach.xml.bak-<date>-<time>`). The
decoder reloads, and the window reopens on the new layout.

**Manage ▸ Fields ▸ Remove a field…** (or *Remove this field from
LOCO_INFO…*) takes a line out the same way. `loco_info_crc` and `vcc_crc`
are not offered.

The built-in kavach.xml cannot be changed. The first time, the window
offers to save an editable copy and use it from then on (for decoding
too; Schema ▸ Revert to built-in schema goes back).

**It does not change the firmware or the tool.** Do step 1, then step 3 as
below; `sync_linfo.py` checks that the C struct really has the member at
that place.

## 1. The C side (as you do today)

Add the member to `LOCO_INFO` in the firmware and in the loco_config tool,
and give it a value in the tool's `main()`. For example, in
`loco_config_v13.cpp`:

```c
    uint8_t braking_pressure_used;
    uint16_t max_brake_cylinder_pressure;   // new
    uint32_t loco_info_crc;
...
    loco_info.max_brake_cylinder_pressure = 380;
```

## 2. One line in `schema/kavach.xml`

In `<packet name="LINFO">`, at **the same position** as in the struct:

```xml
    <field name="brake_pressure_used"          bits="8" ui-group="Brake pressures" .../>
    <field name="max_brake_cylinder_pressure"  bits="16" ui-group="Brake pressures" ui-note="kPa"/>
    <field name="loco_info_crc"                bits="32" hexup="8"/>
```

| C type | schema |
|---|---|
| `uint8_t` / `uint16_t` / `uint32_t` | `bits="8"` / `"16"` / `"32"` |
| `float` | `type="float" prec="4"` |
| `char x[N]` | `type="char" count="N"` |

Optional attributes, which only the editor reads:
- `ui-group="…"`: the editor group. A new name makes a new group; with no
  group the field shows under "Other".
- `ui-note="…"`: one line of help in the editor.
- `ui-format="ipv4"`: edit a `uint32_t` as a dotted address.
- `hexup="8"`: show it in hex, in the decoder and the editor.
- `c-name="…"`: only if the C member's name differs from `name=`.
- `ui-default="…"`: the editor's default until `sync_linfo.py` writes the
  tool's value into `loco_defaults.json`, which then wins.

Inside `static_braking_config` or `national_values`, put the line under
that section's `<note>`.

## 3. Run the sync script

```sh
python3 lococonfig/sync_linfo.py path/to/loco_config_v13.cpp
```

It compiles your tool with the network stubbed out, so it **sends
nothing**. Then it:

- **Checks every schema field against the C member** (`offsetof` and
  `sizeof`). If they disagree it stops at the first mismatch and names it:
  - `the C struct has 2 more byte(s) between brake_pressure_used and
    loco_info_crc than the schema: add the new member(s) there`
  - `the schema says u8 (1 byte(s)), the C member is 2 byte(s)`
  - `schema field shunt_sped -> C member "shunt_sped" does not exist`
- **Runs the tool twice with different junk on the stack.** Any member the
  tool leaves uninitialised shows up as bytes that differ, and is reported
  by name. The bytes after a string's NUL are zeroed, with a warning.
- **Checks the tool's datagram:** header 28 → 2, message 120, the length,
  and `loco_info_crc`.
- **Writes the generated files:** `lococonfig/loco_defaults.json` (your
  tool's values, used as the editor's defaults) and
  `tests/fixtures/loco_info_default.bin` (the golden body).

`--check` does all of this but writes nothing.

## 4. Build and `verify.sh`

The `lococonfig` tests pack the new defaults and compare them with the
tool's bytes. Nothing in the tests names the struct's size, member count
or CRC, so they need no edits.

## What happens to existing data

- **Saved configurations** (`loco_configs.json`) gain the new field at its
  default. A field removed from the schema is dropped, and the window says
  so.
- **Old `loco_info.bin` files and old history entries** have the old size.
  Import refuses them as "written for a different LOCO_INFO version"
  rather than misreading them. The history still shows and re-exports
  their exact bytes.
- **Captures from a loco running older firmware** decode against the new
  layout, the same as for any schema change.
