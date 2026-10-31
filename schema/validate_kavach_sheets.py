#!/usr/bin/env python3
"""
validate_kavach_sheets.py -- gate for scripts/kavach_sheets.py (session 454).

The firm's KAVACH sheets are read into a station layout; this checks the
reading on real sheets.

  1. Every folder of schema/fixtures/kavach_sheets/ (pages of HBL's 2023
     sheets as `pdftotext -bbox` writes them) is read again and must give
     its committed station_<id>.json byte for byte: the file Station Layout
     opens, and the dltests suites read.
  2. What those pages are known to hold (checked by hand against the PDFs):
     the tags, and each disagreement the sheets print.
  3. The whole pack, when it is on this machine (tests/fixtures/kavach_sheets/,
     git-ignored: the PDFs stay local), needs pdftotext: 712 tags in eight
     sections and the notes each gives. Skipped, and said, when absent.

Usage:  python3 validate_kavach_sheets.py
"""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "scripts"))
import kavach_sheets  # noqa: E402

FIXTURES = os.path.join(HERE, "fixtures", "kavach_sheets")
LOCAL = os.path.join(HERE, "..", "tests", "fixtures", "kavach_sheets")

# Section folder -> (station id, tags, sheet notes) for the whole local pack.
PACK = {
    "GGD-21-07-2023": (521, 108, 2),
    "SKP-GGD IB": (522, 40, 0),
    "SKP-21-07-2023": (523, 136, 15),
    "SKP-NPL IB-21-07-2023": (524, 28, 0),
    "NPL": (525, 160, 4),
    "NPL-TLPR IB": (526, 22, 0),
    "TLPR": (501, 74, 0),
    "LPI": (527, 144, 1),
}

total = ok = 0


def check(cond, what):
    global total, ok
    total += 1
    if cond:
        ok += 1
    else:
        print("  FAIL", what)


def has(notes, *words):
    return any(all(w in n for w in words) for n in notes)


def main():
    folders = sorted(d for d in os.listdir(FIXTURES) if os.path.isdir(os.path.join(FIXTURES, d)))
    got = {}
    for d in folders:
        src = os.path.join(FIXTURES, d)
        layout, notes = kavach_sheets.station_layout(src)
        check(layout is not None, f"{d}: read")
        if layout is None:
            continue
        sid = layout["stations"][0]["station_id"]
        golden = os.path.join(src, f"station_{sid}.json")
        with open(golden, encoding="utf-8") as fh:
            same = fh.read().replace("\r\n", "\n") == kavach_sheets.layout_text(layout)
        check(same, f"{d}: station_{sid}.json is what the pages give (re-run scripts/kavach_sheets.py {d})")
        got[d] = (layout, notes)

    # 2. What the pages hold.
    lay, notes = got.get("NPL-TLPR_IB", (None, []))
    if lay:
        check(lay["stations"][0]["station_id"] == 526, "NPL-TLPR IB: station 526")
        check(len(lay["tags"]) == 22, "NPL-TLPR IB: 22 tags")
        check(notes == [], "NPL-TLPR IB: the sheet agrees with itself")
        crc = all(kavach_sheets.decode(t["page_x"], t["page_y"])["crcOk"] for t in lay["tags"])
        check(crc, "NPL-TLPR IB: every CRC-30 holds")
        t = {x["name"]: x for x in lay["tags"]}
        check(kavach_sheets.decode(t["177"]["page_x"], t["177"]["page_y"])["absLoc"] == 155566,
              "NPL-TLPR IB: tag 177 at 155566 m (the layout and ToC say 155.556)")
        check(t["175D"]["page_x"] == "faaaaa8262ac2bd9", "NPL-TLPR IB: 175D's PAGE-X as printed")
    lay, notes = got.get("SKP_extract", (None, []))
    if lay:
        check(len(lay["tags"]) == 42, "SKP pages 16-23: 42 tags")
        check(has(notes, "p16, tag 384M", "0587070224406019", "the CRC printed"),
              "SKP 384M: the number the spreadsheet made of PAGE-X, and the page the printed fields make")
        check(has(notes, "p20, tag 401M", "CRC 280000 printed, 280334e0 in PAGE-Y"), "SKP 401M: the CRC cell")
        check(has(notes, "p17, tag 387D", "PAGE-Y is not 16 digits", "6012beac80010581"),
              "SKP 387D: a digit short, and the page it should be")
        check(has(notes, "p18, tag 388D", "comm nominal (Y32) printed 1, the pages hold 0"), "SKP 388D: comm bits")
        check(has(notes, "tag 388M and 388D differ in commNom (1 / 0), commRev (0 / 1)"), "SKP 388: M against D")
        check(has(notes, "p23, tag 347M", "Applicable Direction (Y1) printed 0, the pages hold 1"),
              "SKP 347M: LC tag applicable direction")
        check(has(notes, "p23: the bit position 'Y131'", "372M"), "SKP p23: 'Y131' named, not guessed")
        check(has(notes, "p23: the bit position 'Y-14-Y12'"), "SKP p23: 'Y-14-Y12' named")
        check(not has(notes, "tag 385"), "SKP 385: a clean tag says nothing")
        names = [x["name"] for x in lay["tags"]]
        check("384" in names and "384D" in names, "SKP: 384 and 384D kept as printed, under their own names")
    lay, notes = got.get("LPI_extract", (None, []))
    if lay:
        check(len(lay["tags"]) == 12, "LPI pages 1 and 12: 12 tags")
        check(has(notes, "p1, tag 6D: duplication (Y31) printed 1, the pages hold 0"), "LPI 6D: main tag in its pages")
        check(len(notes) == 1, "LPI p12: the two-line station rows read (no note)")

    # 3. The whole pack, when it is here.
    if not os.path.isdir(LOCAL):
        print("  NOTE: tests/fixtures/kavach_sheets/ (the PDFs) is not on this machine: the whole pack not read")
    elif not shutil.which("pdftotext"):
        print("  NOTE: pdftotext not found: the local PDFs not read")
    else:
        n = 0
        for d, (sid, ntags, nnotes) in PACK.items():
            src = os.path.join(LOCAL, d)
            if not os.path.isdir(src):
                check(False, f"local pack: {d} missing")
                continue
            lay, notes = kavach_sheets.station_layout(src)
            check(lay is not None and lay["stations"][0]["station_id"] == sid, f"{d}: station {sid}")
            check(lay is not None and len(lay["tags"]) == ntags, f"{d}: {ntags} tags")
            check(len(notes) == nnotes, f"{d}: {nnotes} sheet notes (got {len(notes)})")
            n += len(lay["tags"]) if lay else 0
        check(n == 712, "the whole pack: 712 tags")
    print(f"kavach sheets: {ok}/{total} {'PASS' if ok == total else 'FAIL'}")
    return 0 if ok == total else 1


if __name__ == "__main__":
    sys.exit(main())
