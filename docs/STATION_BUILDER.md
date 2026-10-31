# Building a station in Station Layout (session 459)

Tools ▸ Station Layout has the steps of building one station beside its
drawing (View ▸ **Steps beside the drawing**, shown at first). Everything
a station needs is in one file (`station_<id>.xml` or `.json`): its tags,
lines, points, signals and track circuits, and its KAVACH data (routes,
track profile, TSR). Packet Maker then answers a loco from it
(docs/STATION_KAVACH.md).

## The steps

| # | Step | Its table | What goes in it |
|---|---|---|---|
| 1 | Station | Station | the KAVACH station ID and its centre (m) |
| 2 | Lines | Lines | DM (DN main), UM (UP main), DL1… (DN loops), UL1… (UP loops); each one's tags in order |
| 3 | Tags (RFID) | Tags | every tag as the loco reads it (page_x / page_y) |
| 4 | Points | Points | each turnout: from line1 at location1 to line2 at location2 |
| 5 | Signals | Signals | each signal at its foot tag, and its heads |
| 6 | Track circuits | Track circuits | cid, name, line, startx-endx (or a location), station (optional) |
| 7 | Labels | Texts | free text on the drawing (optional) |
| 8 | Routes (KAVACH) | KAVACH | the Table of Control, track profile, TSR |
| 9 | Check | Checks | every step's problems |

Each step in the list shows its rows and **⚠ its problems**. Choosing a
step shows its table; clicking a problem goes to its row. **Back** and
**Next** walk the steps, and the tabs move with them.

A step's problems are what Checks lists, sorted by step. Each step also
says what only a station being built needs to know:
- no station row;
- no lines;
- a main tag on no line or point (drawn under "(no line)");
- a signal whose foot tag is not placed as a signal foot (placement 1, 2
  or 6);
- a track circuit on a line the lines sheet lacks.

They are reported and never corrected.

## New station…

The panel's button, or Open ▸ New station…, asks for:
- the station ID and centre;
- DN and UP loops and their length;
- the first tag ID and the first TIN.

The dialog says which tags, TINs and metres the station takes up, or why
it is not possible (an ID past 1023, a TIN past 255, a location below
0 m or past 8 388 606 m). The station it builds:

- **DN MAIN** (run Nominal, locations rising) and **UP MAIN** (run
  Reverse), each with these tag pairs:
  - approach, 1200 m before the entry points;
  - home, 200 m before them, with a home signal (3 aspects, calling-on);
  - one at the centre;
  - starter, 150 m inside the far end, with a starter signal;
  - exit, 200 m past the exit points;
  - one 1200 m past them.
- **Loops** beside their main line: a point at each end, a tag pair at
  the entry and a starter at the exit.
- **TINs.** Each main line has three TINs (before, yard, after); each loop
  has its own.

The tags are encoded as the shipped stations have them:
- **TIN direction.** A tag's TIN in a direction is the track it leads
  onto going that way: `tin_nom` is the track on its rising side,
  `tin_rev` the falling side. Lingampalli's DN main (station_527.xml) does
  this at all 9 of its TIN changes.
- **Signal foot placement:** 1 (Signal foot, Nominal) on a line run
  Nominal, 2 (Reverse) on one run Reverse. LPI's DM signals are 1, its UM
  signals 2.
- **The D tag** sits 4 m after its main tag, in the direction the line is
  run.
- **Other fields:** version 1, the station's ID both ways, Station
  section both ways, CRC-30 computed.

Then move the tags to their surveyed locations: drag them, or use the
form, the Tags table, or Shift on a selection.

## The tag form

Select a tag (on the drawing or in the Tags table) and turn on View ▸
**Form beside the drawing**. The form sets the tag's location, TIN
nominal and reverse, station nominal and reverse, and placement. Apply
re-encodes the tag with a fresh CRC-30 as one Undo step. A value that
does not fit its field, or a change that would rename the tag, is
refused. The other fields are in Tag Builder (Edit in Tag Builder…).

## Not yet (the next patches)

The whole plan, patch by patch: docs/STATION_TEST_RIG_PLAN.md.

- **Routes and track profile as steps of their own:** Table-of-Control
  rows and SSP / gradient / LC rows filled in quickly, with lengths
  checked against the route as they are typed. Today they live in the
  KAVACH tab.
- **A firm's PDF sheets opened in the app** (today it is the command line,
  `scripts/kavach_sheets.py`; docs/KAVACH_SHEETS.md).
- **A Run step:** a simulated loco driven through the station, answered
  from this file. Then the same step pointed at a real VCC.
