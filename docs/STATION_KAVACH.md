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

Since session 429 every tag of a name the layout lists more than once is
drawn where it is (Section_30: all three blocks). A route is drawn through
the tags its linking distances lead to: it starts at the entry tag whose
whole MA agrees best with the layout (S43-A900 with its MA to A904 in the
first block, the only one holding tag 904), each next tag the one nearest
where its distance says. A loco's MA (session 428) runs through the block
the loco is in. Clicking, dragging, the Tag Builder, Shift, Set a field,
Delete and Pick act on the tag clicked, not the first of its name. A signal
whose foot tag is listed more than once: the signals sheet's k-th row for
it at the k-th such tag when there are as many rows as tags (Section_30
lists S50, S48, S43 once per block), else at every one.

## The station live on the drawing (session 428)

While Packet Maker answers as the station, **Station… ▸ Show on the
drawing** opens the layout it answers from in Station Layout and draws the
station as it is now:

- each signal lit at the aspect set in Station… (over the file's). Change
  one there and the drawing follows at once. Hover it: *KAVACH, live: Red*;
- each loco the station has heard, at the ABS_LOCO_LOC of its last LSRP, on
  the line of the tag it last read, pointing the way it runs, its train
  drawn TRAIN_LENGTH behind it;
- the MA it was last sent: a green band from the loco along the MA's tags to
  a bar where the MA ends. A loco ahead (session 427) shows as the MA ending
  100 m short of its rear;
- the SoS sent, in the loco's label and colour: *Loco 1 · MA 536 m · SoS
  Rear-end*; a SPAD (session 426) is *MA 0 · SoS SPAD*, with no MA drawn.

A line under the toolbar says it in words: *Live from Packet Maker, station
527: loco 1 at 160.661 km, MA 1236 m, SoS Rear-end; loco 2 at 161.246 km,
MA 1670 m*. It follows every answer. Stop answering, and the drawing goes
back to the layout alone. The MA's start is placed from the ref tag (of a
number the layout lists more than once, its place nearest the loco).

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

## The station's own signalling (sessions 441 on)

`stationinterlock.{h,cpp}`. Packet Maker, answering as the station's routes,
keeps where every loco is from each **ARP and LSRP** it hears.

- **Track circuits occupied by the locos** (session 441). A loco occupies
  each track circuit of its line (the line of the tag it last read, from the
  lines sheet) that its train covers: ABS_LOCO_LOC back TRAIN_LENGTH against
  MOVEMENT_DIR. The layout's circuits sheet (the station XML's `<circuits>`)
  gives each circuit's line and ends. Station… ▸ Occupied says *by loco 1*;
  the drawing shows it red. A loco whose tag is on no line occupies nothing,
  said. A circuit occupied by hand (436) stays so too.
- **Routes locked and released** (session 442; Station… ▸ Options ▸
  Interlocking, on when the layout has track circuits). A route set locks its
  points; a train in it (its first circuit occupied) puts its signal back to
  Red; once every circuit of it is clear again it is released. A signal
  cleared by hand goes back to Red as a train passes it. Each step is said in
  Station…, under the answers.
- **Automatic signals** (session 443). A signal every route from which is an
  Autoblock section with track circuits works itself: Red while the section
  ahead is occupied, else one step up from the signal at its end (Red →
  Yellow → Double Yellow → Green).
- **Conflicts** (session 444). A route is not set while a circuit of it is
  occupied or a route set from another signal runs over one of its circuits;
  a signal off whose route is not free (a point the wrong way, a circuit
  occupied) is held at Red, said why.
- **SoS** (session 445). Head-on (6): a loco coming the other way on the same
  line, its front ahead within the MA (the MA cut 100 m short of it), or one
  reporting EMERGENCY_STATUS 4. Shunt violation (7): past a Red signal in
  Shunting mode. Foreign RFID (1): a tag the layout does not hold, on no
  route, read by a loco already placed. Station… sends Station General SoS
  (8) to every loco, or any of them to one loco.
- **Shortened MA** (session 446; Options ▸ Ask the loco before shortening its
  MA, off by default). A signal gone back: REQ_SHORTEN_MA 1 and NEW_MA while
  the loco keeps its MA; the shorter MA once it grants it (NEW_MA_REPLY 1);
  the one it holds while it rejects it (2).
- **Hand-over** (session 447). A route's *Next station* (Routes column,
  `"next_stn"`): while the MA reaches it, NEXT_STN_COMM 1 and
  APPR_STN_ILC_IBS_ID. An ARP to another station (APPROACHING_STN_ID) is left
  unanswered, the loco still placed.
- **Train length** (session 448; Options ▸ Send train-length times, off by
  default). A loco's front and then its rear over a track-circuit joint of
  its line: the times (interpolated between its frames) go out as
  TRN_LEN_INFO Start, then End.
- **As station 527** (session 449; Options). *Keep the profile into the next
  section while it covers the MA* (same REF_PROF_ID, the MA alone: SLRP 38156);
  *Full profile after the ack: N more* (527: 3 on 8 Oct, none in June).

## Not yet

Not modelled: Temporary Single Line working, the absolute-block last stop
signal's MA in metres (give the block section as a route), when station 527 moved the ref
tag (0 to 9 frames late, no rule seen: DLConsole moves it at once), track
locking of a point and flank protection (the data names neither), a train
across a point counted on both lines. Not tested
against a live loco: the engine's answers are checked against real station
frames only. A firm's Table of Control is read by
`scripts/kavach_sheets.py` (session 455; docs/KAVACH_SHEETS.md). A TSR can
be given by OHE poles from the station's TSR route table (session 457:
KAVACH tab ▸ TSR ▸ TSR by OHE poles…). Several stations answer side by side
from a stretch.json (session 458: Packet Maker's layout field; a dry run in
Station Layout ▸ Open ▸ Run a loco over a stretch…). The layout itself can be
checked against that drawing: Station Layout ▸ Open ▸ Compare with ▸ A
tag-layout drawing (session 430). The drawing shows the aspects the file starts
with on the KAVACH tab; Packet Maker's live ones under Station… ▸ Show on the
drawing (session 428).
