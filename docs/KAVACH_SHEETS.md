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

They are said as what each sheet shows; which one is right is for the
signatories.

## Tests

- `schema/validate_kavach_sheets.py` (a gate validator) reads the committed
  extracts in `schema/fixtures/kavach_sheets/`:
  - all of NPL-TLPR IB's tag data;
  - SKP pages 16-23;
  - LPI pages 1 and 12.

  Each must give its committed `station_<id>.json` byte for byte, with the
  findings above.
- The whole pack is read too when `tests/fixtures/kavach_sheets/` is on the
  machine. That folder is git-ignored: the PDFs stay local.
- `session454` checks what DLConsole does with the files.

## Not yet

The Table of Control, the track profile and the TSR route table are not
read yet.
