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
- **SPAD** (session 426). When the loco's `ABS_LOCO_LOC` is past the foot
  of its route's exit signal while that shows Red (or stop board,
  unidentified; calling-on aside), the answer is **SoS SPAD**:
  `DEST_LOCO_SOS` 4, `AUTHORITY_TYPE` 0, `MA_W_R_T_SIG` 0,
  `DIST_PKT_START` 0, `NEXT_SIG_ASPECT` 0, `APPR_SIG_DIST` from the ref tag
  to the signal, the MA alone, the profile id kept. Where the ref tag is
  comes from the layout's own tags (their abs_loc; of a number listed
  more than once, the place nearest the loco): a layout without tags
  cannot see a SPAD. This is station 527's answer to loco 1 standing 1 m
  past A904 in `replay/loco_1_1_27062026_140226.cap`, every frame of it
  (MAC aside: that capture has no session keys). What set it off is not in
  the capture (it starts with the SoS on); "past the foot" is our reading.
- **A loco ahead (Rear-end)** (session 427). Another loco the station has
  heard, ahead on the MA's routes and going the same way, cuts the MA to
  its `ABS_LOCO_LOC` less its `TRAIN_LENGTH` less **100 m** (never below 0;
  the profile stays the sections'). A loco reporting `EMERGENCY_STATUS` 5
  (Rear-End Collision) is answered `DEST_LOCO_SOS` 5; the cut is made
  whether or not it does (Abhinav's choice: the capture cannot tell what
  set the SoS off). This is station 527's answer to loco 1 standing 400 m
  behind loco 2 in section S50-S48
  (`replay/loco_{1,2}_1_27062026_151052.cap`): MA 1236 … 1295 m as loco 2
  drew away, each from loco 2's last LSRP heard. A loco coming the other
  way (head-on) is not modelled.
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

## Editing it (session 423)

Tools ▸ Station Layout, tab **KAVACH**. A layout without KAVACH data shows
**Start KAVACH data from this layout**: the layout's station id and its
signals (name, foot tag) at Red, no routes yet.

- **Top:** station id, STN version, and *MA sections by aspect* where they
  differ from Annexure-I's (`11=3, 10=2`; empty: Red 1, Yellow 2, Double
  Yellow 3, Green 4). **Remove KAVACH data** takes the block out.
- **Signals:** name, foot tag, signal info, the aspect it starts with.
  Renaming a signal renames it in the routes too.
- **Routes:** one Table-of-Control row each. Direction, section, authority
  and OS speed are lists of the schema's codes; points, track circuits and
  TINs are names separated by commas; *Loc reset* is `start, direction,
  correction` or empty. The length is worked out from the tags.
  **Add route** starts at the selected route's exit signal (the next
  section on); **Copy route** copies one with its tags and profile (a loop
  beside a main line).
- **The selected route:** its tags (linking distance from the tag before;
  *From entry* worked out), then SSP, gradient, LC gates, turnouts, track
  conditions and TSR. Their columns are the SLRP sub-packet's own fields;
  hover a heading for its width and codes. An empty cell is a field not
  given. **Add row** copies the selected row. The SSP and gradient tab
  titles show when the rows do not add up to the route's length.

Each edit is one Undo step. What the data would not do is listed under
**Checks**, as `KAVACH: …`. That includes tags a signal or route names that
the layout's own tags do not hold (when it has tags). A word where a number
goes is refused, the reason shown, and the table put back as it was.
Save writes the `.json` (or the `kavach` sheet of an exported `.xlsx`).

## On the drawing (session 424)

While the **KAVACH** tab is shown, the drawing above it shows the route
selected and the MA the station would give from it, with the aspects as
the file starts them (*Aspect at start* in Signals):

- the route selected: a strong halo along its tags, entry to exit;
- each MA section on from it (the routes the aspect of its exit signal
  takes in, as the station answers): a fainter halo;
- the MA's end: a bar across the line at the last section's exit foot tag,
  and its length (*MA 1956 m*) under the gap dimensions;
- each KAVACH signal lit by its aspect (red; yellow; two yellows; green;
  red with white for calling-on). A KAVACH signal the signals sheet does
  not hold is drawn at its foot tag too. Hover a signal for its aspect,
  a halo for its route and section.

The line over the route's tables says the same: *A900 Yellow: 2 sections
→ MA 1956 m*, and, when there are fewer routes on than the aspect
wants, how many it wanted. Change an aspect, or select another route,
and the drawing follows. Another tab shows the layout alone. Export ▸
PDF / PNG draws what is shown, the route included.

**Checks** also says where the layout's tags do not lie as a route's
linking distances say: *route A900-A904 DN Auto: tag 904 is 200 m on from
tag 902; the layout's tags lie 9800 m back*. A tag number the layout lists
more than once (Section_30.xlsx repeats one 5 km block three times) agrees
when any of its tags does. Said, not corrected.

The drawing places each tag name once, as the layout's own drawing does:
a route through a tag listed more than once is drawn through one of its
places, so it can jump from block to block; hovering it says which tags.

## Picking a route's tags on the drawing (session 425)

Under the route's tables, **Pick on drawing**: then click the route's tags
on the drawing above, in the order the loco reads them. Each is added after
the route's last tag; its linking distance is the difference of the two
tags' locations in the layout, in the route's direction (Nominal: rising
locations). Clicking the route's last tag again takes it off. Picking the
exit signal's foot tag ends it; so do Esc, another tab and Measure. Each
pick is one Undo step; the status line says what it did.

- A route without an entry tag yet takes the first tag picked as it.
- Refused, by name: a tag already in the route; a tag that does not lie on
  from the last one in the route's direction; a pick with no route
  selected; a last tag that is not in the layout's tags (type the distance).
- A tag number the layout lists more than once (Section_30's blocks): the
  last tag's place nearest the clicked tag is the one measured from.
- A duplicate tag ("…D") picks its main tag. *Duplicate dir* is left 0:
  set it in the table.

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
SoS other than SPAD and Rear-end (head-on, shunt, foreign RFID, station
general), the next station's
communication, the absolute-block last stop
signal's MA in metres (give the block section as a route). Not tested
against a live loco: the engine's answers are checked against real station
frames only. No import from a firm's Table-of-Control sheet yet (waiting for
a real sheet to read). The drawing shows the aspects the file starts
with, not Packet Maker ▸ Station…'s live ones.
