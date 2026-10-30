# A Stationary KAVACH made of data (session 422)

DLConsole can answer a running loco as its station would: each LSRP gets
the SLRP the station would send, made from the station's **signals and
routes** and their **live state** (aspects, points, what is occupied).
The loco, the VCC and the DMI see a station that behaves; the operator
changes a signal and sees what the loco does.

## Using it

1. Packet Maker ▸ **Send log** ▸ *Answer the loco: ARP → AAP, LSRP →*
   **the station's routes (KAVACH data in the layout)**.
2. The AAP preset (as for session 378) and a **station layout with KAVACH
   data** (below). Tick the box, then **Arm**.
3. **Station…** opens the live state: each signal's aspect, each point
   Normal / Reverse, the track circuits and TINs occupied, each route's
   authority (FS / OS / SR), each TSR on or off. Changed there, the next
   LSRP is answered with it. Under it, the last answer to each loco and
   what the state does not agree with.

Each answer is a send-log row: *answers LSRP 40380: route S43-A900 DN Auto,
A900 Yellow → 2 sections, MA 1956 m; ref tag 906, APPR_SIG_DIST 916 m;
profile 2 acknowledged: MA only*.

## How the station answers

- **The route** is the one whose entry signal's foot tag the loco has just
  read (it has passed that signal); else the one it was on; else any route
  holding the tag. Between routes from the same signal (main / loop), the
  one whose points are set.
- **The MA, in sections** (Annexure-I I.2.11.16). The route the loco is on
  is section 1; each route on from its exit signal one more. The aspect of
  the signal ahead (the route's exit signal) gives how many:
  Red, calling-on, stop board: **1**; every Yellow: **2**;
  Double Yellow: **3**; Green: **4**. `MA_W_R_T_SIG` is the distance from
  the start signal to the foot of the last section's exit signal.
  Calling-on gives OS authority at the route's `os_speed`.
- **The profile** (SSP, gradient, LC gates, turnouts, tag linking, track
  conditions, TSR): the routes of those sections end to end, **cut at the
  MA end**. When it changes (another route, another aspect, a TSR switched)
  it gets a new `REF_PROF_ID` and is sent in full until the loco's
  `LAST_REF_PROFILE_NUM` acknowledges it; the MA alone after that.
- **The ref tag** is the last tag the loco read, when the route holds it.
  `DIST_PKT_START` is minus its distance from the start signal;
  `APPR_SIG_DIST` is from it to the signal ahead.
- **The checks.** A point lying the wrong way, an occupied track circuit
  or TIN, a Green or Double Yellow with Red ahead, an aspect wanting more
  sections than there are routes: each is **said**, and the answer is
  still sent as the aspects are set. The aspects are the operator's, as the
  interlocking's would be.
- **Refusals.** A value too wide for its field, more rows than a
  sub-packet holds, an SSP or gradient that does not cover its route, a
  route that does not start where the one before ends, a tag on no route:
  not sent, and the reason said.

This is what station 527 did in `replay/2026-10-08/loco_1_1_08102026_10{3520,5200}.cap`:
the suite `session422` rebuilds its SLRPs from
`tests/fixtures/station_kavach/section30_527.json`, field for field, and
the answer to LSRP 40380 byte for byte, MAC included.

**Where the real station did otherwise:** it moved the ref and the route a
few frames after the loco read a tag (DLConsole: at once); at the section
change into a Red signal it kept the profile it had sent, whose stretch
covered the new MA (DLConsole: a new profile); it sent a full profile once
or twice more after the acknowledgement (DLConsole: never).

## The file

A Station Layout JSON (Tools ▸ Station Layout) with a `"kavach"` block, or
an `.xlsx` with a `kavach` sheet holding the same JSON one line per row in
column A. Values are the schema's raw codes (`schema/kavach.xml`): the field
names in rows are the SLRP sub-packet fields'. (The `//` comments below are for
reading only: JSON has none. The fixture is a file that loads.)

```json
"kavach": {
  "station_id": 527, "stn_version": 2,
  "sections": { "11": 3 },                    // optional: aspect -> sections
  "signals": [
    { "name": "S43", "foot_tag": 18, "sig_info": 66400, "aspect": 2 }
  ],
  "routes": [
    {
      "name": "S43-A900 DN Auto", "entry": "S43", "exit": "A900",
      "pkt_dir": 1,                            // 1 Nominal, 2 Reverse
      "section_type": 2,                       // secType: 0 station, 1 absolute, 2 auto
      "authority": 2, "os_speed": 20,          // authType; speed6 (20 = 100 km/h)
      "points_normal": ["P11"], "points_reverse": [],
      "track_circuits": ["3T"], "tins": ["N-84"],
      "tags": [ { "dist": 0, "tag": 18 }, { "dist": 200, "tag": 906 },
                { "dist": 656, "tag": 898, "dup_dir": 0 }, { "dist": 260, "tag": 900 } ],
      "dist_dup_tag": 4, "adj_tins": [84], "tin": 83,
      "loc_reset": { "start": 100, "adj_dir": 1, "correction": 160000 },   // optional
      "ssp":      [ { "d": 1116, "u": 16 } ],             // or a, b, c (class A/B/C)
      "gradient": [ { "d": 1116, "dir": 1, "value": 6 } ],
      "lc":       [ { "d": 506, "id": 191, "suffix": 1, "manned": 0, "class": 1, "awEn": 1, "awType": 0 } ],
      "turnouts": [ { "speed": 6, "start": 120, "release": 300 } ],
      "track_conditions": [ { "type": 2, "sd": 100, "ln": 1016 } ],
      "tsr":      [ { "id": 7, "dist": 300, "len": 400, "u": 6, "whistle": 0 } ]
    }
  ]
}
```

One route is one row of the KAVACH Table of Control, entry signal → exit
signal. `tags[0]` is the entry signal's foot tag at 0 m and the last tag
the exit signal's; each `dist` is the linking distance from the tag before.
Every distance in a route counts from its entry signal's foot. `ssp` and
`gradient` rows are segment lengths and must add up to the route's length;
`lc`, `turnouts`, `track_conditions` and `tsr` rows are placed by their
start (`d`, `start`, `sd`, `dist`).

## Not yet

Not modelled: the TSR route table's OHE poles (TSR rows are placed by
distance), Temporary Single Line working, train length, shortened MA,
SoS, the next station's communication, the absolute-block last stop
signal's MA in metres (give the block section as a route). Not tested
against a live loco: the engine's answers are checked against real station
frames only. No editor yet: the file is written by hand (or from a Table of
Control, a later patch).
