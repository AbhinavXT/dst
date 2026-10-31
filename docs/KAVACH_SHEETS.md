# A firm's KAVACH sheets into DLConsole (session 454)

A firm hands a station over as PDF sheets: the RFID tag layout (a
drawing), the **RFID tag data**, the **Table of Control**, the **track
profile**, the gradient plan and sometimes a TSR route table.
`scripts/kavach_sheets.py` reads them into a Station Layout file
(`station_<id>.json`). Station Layout opens that file like any other, and
the station can then answer a loco (docs/STATION_KAVACH.md).

    python3 scripts/kavach_sheets.py "NPL-TLPR IB" [MORE FOLDERS] [-o OUTDIR]

There is one folder per station or IB section, as the firm sends them; the
sheets are recognised by name (`*TAG_DATA*`, `*TABLE_OF_CONTROL*`, ...).
Reading a PDF needs poppler's `pdftotext` (`brew install poppler`,
`apt install poppler-utils`; on Windows, poppler's zip). The `.html` that
`pdftotext -bbox` writes is read as well, so a sheet can be converted on one
machine and read on another. Standard library Python only.

## What is read

**RFID tag data** (session 454). Every tag column goes in exactly as its
**PAGE-X / PAGE-Y** are printed: those words are what gets programmed. The
tag is named by its column label (134M → `134`, 134D → `134D`).

Each printed field row is then checked against the bits its own bit
positions name (`X38-X16`, `Y6-Y0 & X63-X55`, the LC tag's `Y0-X63`). This
needs no table of field names, so it works the same for normal, LC,
adjacent-line and junction tags. Checked as well:

- the CRC-30;
- the column's label against the unique id and the duplicate bit;
- each main tag against its duplicate: every field the same but the
  location (equal, or 4 m apart).

The station is the ID most of its normal tags carry in the nominal
direction. Its location is not on the sheets, so the file puts it at the
middle of its normal tags, for the drawing only; the file's `comment` says
so.

**Table of Control** (session 455). Every route row becomes a Stationary
KAVACH route (docs/STATION_KAVACH.md), with these parts:

- **Name and signals.** It is named `entry-exit line`. The entry signal's
  foot tag is the row's "Entry Signal Foot Tag"; the exit signal's is the
  last tag of the sequence.
- **The RFID sequence**, in either of the two ways HBL's sheets write it:
  - `(0, R-148), (774, R-150)`: each distance is from the tag before.
  - `(R-134, 746), (R-136, 250)`: each distance runs to the tag after, and
    the last one to the exit signal's foot tag, which the cell does not
    name. That tag is found as the exit signal's foot tag elsewhere in the
    ToC, else the "Last Stop Signal" table's border tag at that distance,
    else the tag the sheets place there.
- **Direction:** from the tags' locations (rising: Nominal).
- **Section type and authority.** Station Section 0, Absolute Block 1,
  Auto 2. A calling-on route gets OS authority, its speed from "Authorized
  Speed In OS".
- **Points, track circuits and TINs.** Points come Normal and Reverse;
  track circuits only those to be clear (a calling-on route's
  "Occupied" one is not modelled); TINs include the overlap's (a bracketed
  note is dropped).
- **The route's TIN:** its entry foot tag's TIN, the way it runs.
- **`dup_dir`:** each duplicate tag's side, 4 m on or back, from the tag
  data.
- **Hand-over.** A block or auto route ending on the table's border tag
  hands over to its next station (`next_stn`), with the location reset
  when the table gives one. GGD's S6 resets 107 m at 1611 m: the junction
  tag R-454's two locations apart.
- **What is not made a route:** a row with no exit signal that ends
  nowhere ("S1 → -", Red / Nil, 15 km/h, whose sequence stops after one
  link), and a row without a sequence. These are said in the notes.

Signals and lines for the drawing come from the routes: each tag goes on
the line most routes through it name, in location order. **Folders given
together are read as one stretch.** A route that runs into the next section
ends on that section's tag, which is copied in, and its exit signal is named
from that section's ToC ("S2 ID (501)").

Also checked:

- the Distance column against the sum of the links;
- the MA sections against the exit signal's aspect;
- the ToC's Station ID against the tags';
- a border RFID that is the station's own last stop signal's foot tag (SKP).

Station Layout's own KAVACH checks then compare the linking distances with
the tags' locations; a junction tag counts at either of its two locations
(session 455).

**Track profile** (session 456). Each row goes onto the route with the same
entry and exit signal. They are matched on a signal key that drops the
stations a name is written with: `S6 TLPR/50D LPI` matches `S6/50D`,
`S30 D` matches `S30D`, `S7/104D OF LC.22` matches `S7/104D`. A calling-on
route over the same tags takes the main route's rows.

- **SSP:** each `(U, speed, length)`, the speed / 5.
- **Gradient:** the **average** rows, as station 527 sends them. Rise is
  uphill, fall and Level are downhill, and the value is 1000/N truncated
  (F 1 in 209 → 4, Level → 0).
- **Turnout:** speed / 5, start ("-": at the entry signal), and its length
  as the release.
- **LC gate:** distance, id and suffix, manned, class, auto whistling.
- **Track conditions:** Annexure-C's types, one row per "<distance> From
  <signal>". A type given without a length goes as 0 m, and that is said.
- **Coverage:** an SSP or gradient that does not cover its route exactly is
  left out and said, since the station would refuse the whole answer.

Station 527's own SLRPs of 26 Jun 2026 carry exactly these speed and
gradient rows for S50-S48 and S48-S43. Its track conditions and turnouts
were other than the sheets'.

**TSR route table** (session 457). Read from its `.docx`, which holds real
table cells: the PDF's six-line pole lists come apart line by line. Each
row becomes a TSR route in the KAVACH data's `tsr_routes` with these parts:

- its id;
- the routes it runs over (the entry, then each exit the row lists, route
  by route);
- its OHE poles at their distances from the start tag. The first pole is at
  the row's start distance; each later pole adds its span. LPI's 159/30 is
  42 + 45 + 45 + 54 = 186 m from R-002.

In Station Layout, KAVACH tab ▸ TSR ▸ **TSR by OHE poles…** picks a TSR
route, two poles, the speed (5 km/h steps), the TSR id and whistle. A TSR
row goes onto each route the stretch between them covers, cut to it, to the
metre, as one Undo step. Packet Maker's station sends it (TSR_STATUS 2), and
the Station panel switches it on and off as before.

## What the sheets do not agree on

Nothing is corrected. Each disagreement is one line in the file's
`sheet_notes`. Station Layout lists them under **Checks** as `Sheet: ...`,
next to its own checks (a CRC-30 that does not hold, a tag named for
another). Each line names the sheet, its page and the tag. Where the
printed fields make other page words, the line gives those words and says
whether their CRC-30 is the one printed. A bit position that names no bit
of a tag (HBL's LC sheets print `Y131` and `Y-14-Y12`) is said, and that
row is not checked: whether `Y131` means Y31 or Y13 is not ours to guess.

Seen on HBL's 2023 sheets (GGD … LPI, 712 tags):

- **SKP tag data 2.0.0** (the version approved on 5 Jul 2023):
  - 384M/D, 400M/D and 401M had their PAGE-X turned into numbers by a
    spreadsheet (`587070000000000` for `0587070224406019`). The printed
    fields make the right words, with the printed CRC.
  - 387D's PAGE-Y is a digit short.
  - 401M's CRC cell reads `280000`.
  - 388D's comm bits are swapped against its printed row and its main tag.
  - LC tags 347M/D print applicable direction 0; their pages hold 1.
- **LPI 2.0.7:** 6D's pages say main tag.
- **GGD, NPL, SKP:** their LC pages print the bit positions `Y131` and
  `Y-14-Y12`.

From the Tables of Control (151 rows; 139 routes):

- **TLPR:** #13 gives 654 m from R-171 to R-173, where the tags lie 644 m
  apart (its total of 1024 m takes 644); #12's links add up to 2600 m
  against the 2361 m given.
- **NPL–TLPR IB:** S14 ID–S14's 780 / 240 m against R-177's location
  (770 / 250 m).
- **SKP:** routes over R-294, 296, 302, 451, 453, 455 and 473, which no tag
  sheet has; its border RFIDs are its own last stop signals' foot tags.
- **The "S1 → -" rows:** nine of them, Red / Nil at 15 km/h, carry a
  sequence of one or two links (GGD, LPI, NPL, TLPR) or none (SKP). LPI #31 S1637 →
  - gives 590 m of its 790 m.
- **MA sections:** TLPR #7 gives 3 MA sections at Green.

From the TSR route table: rows 11 and 12 are not there; TSR route ids 22 and
12 are each given to two routes; #10 lists S1637 → S1647, where the ToC has no
route (its row ends nowhere), so TSR route 18 covers S43-S1629 and
S1629-S1637 only; #13 starts at Hafizpet's S25.

From the track profiles: TLPR S18's 3361 m against its 2600 m of links;
S15/14D's 1024 m against the 1034 m the ToC's 654 makes; S1D → S1's 813 m
of SSP for 1080 m. Fouling marks carry no length on any sheet.

They are said as what each sheet shows; which one is right is for the
signatories.

## Tests

- `schema/validate_kavach_sheets.py` (a gate validator) reads the committed
  extracts in `schema/fixtures/kavach_sheets/`:
  - all of NPL-TLPR IB's tag data, Table of Control and track profile;
  - LPI's tag data pages 1-3, ToC pages 1 and 3, track profile pages 1-2
    (`LPI_routes`), with the TSR route table's .docx cut to rows 1 and 9;
  - SKP pages 16-23;
  - LPI pages 1 and 12.

  Each must give its committed `station_<id>.json` byte for byte, with the
  findings above.
- The whole pack is read too when `tests/fixtures/kavach_sheets/` is on the
  machine. That folder is git-ignored: the PDFs stay local.
- `session454` checks what DLConsole does with the files' tag notes.
- `session455` checks the IB's routes, the checks they raise, the junction
  locations, and the station answering a loco and handing it over to TLPR.
- `session456` checks LPI's profiles against station 527's own SLRPs.
- `session457` checks the TSR routes, a TSR given by poles, the editor and
  the answer that carries it.

## Not yet

A TSR route table given only as a PDF is not read (its .docx is). Signals' info
(CUR_SIG_INFO) and the STN version are not on the sheets: 0. A signal
starts at Red.
