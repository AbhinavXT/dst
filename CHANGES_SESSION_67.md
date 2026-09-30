# Session 67 — @speed, @analog_top, @analog_bottom

Three new capture types, recorded now so the mission driver can replay them
to a loco on the bench later.

## @speed — STRUCT_SENSOR_SPEED_DATA, 14 B, packed LE

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

## @analog_top / @analog_bottom — STRUCT_ANALOG_SENSOR_DATA, 24 B, packed LE

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

## Where the new types appear

Capture-type routing and labels; direction (all three are sensed inputs,
RX); the replay window, loco console and link overview; Frame Diff and the
Decode Workbench token lists.

## Tests

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

## Verified

- `./verify.sh`: **14 / 14 stages ok, exit 0** — unit suite 121 suites /
  3177 checks / 0 failed; menu audit 59 / 0; all 11 golden validators;
  headless smoke alive at timeout.
- Live check: the real app, offscreen, took 1202 synthetic datagrams
  (@speed from both readers, @analog_top, @analog_bottom, plus a short
  @speed and a short @analog_top) and was alive at timeout.

## Needed from the first real captures

1. One real `@speed` line — confirms or refutes the assumed CRC.
2. What `sensor_dir` values mean, and whether the tachometer counts are
   cumulative or per cycle (this decides how the mission driver replays them).
3. The channel-to-pressure mapping and units for the analog channels.
