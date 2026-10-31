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
     git-ignored: the PDFs stay local), needs pdftotext, read together as one
     stretch: 712 tags in eight sections, their routes, the hand-overs, and the
     notes each gives. Skipped, and said, when absent.

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

# Section folder -> (station id, its own tags, tags in the file (with those
# copied from the next sections for the routes ending on them), routes, sheet
# notes) for the whole local pack, read together.
PACK = {
    "GGD-21-07-2023": (521, 108, 110, 21, 6),
    "SKP-GGD IB": (522, 40, 44, 6, 1),
    "SKP-21-07-2023": (523, 136, 140, 24, 21),
    "SKP-NPL IB-21-07-2023": (524, 28, 32, 6, 2),
    "NPL": (525, 160, 164, 30, 9),
    "NPL-TLPR IB": (526, 22, 26, 5, 1),
    "TLPR": (501, 74, 78, 17, 11),
    "LPI": (527, 144, 146, 30, 14),
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
        # session 455: its Table of Control
        k = lay.get("kavach", {})
        r = {x["name"]: x for x in k.get("routes", [])}
        check(len(r) == 5, "NPL-TLPR IB ToC: 5 routes (rows 1-4 and 6)")
        a = r.get("S9D-S9 ID Down Main", {})
        check([(x["dist"], x["tag"]) for x in a.get("tags", [])] == [(0, 134), (746, 136), (250, 138)]
              and a.get("pkt_dir") == 1 and a.get("section_type") == 1 and a.get("tin") == 86,
              "S9D-S9 ID: (R-134,746), (R-136,250) to S9 ID's foot R-138, Nominal, absolute block, TIN 86")
        a = r.get("S14 ID-S14 UP Main", {})
        check([(x["dist"], x["tag"], x.get("dup_dir", 0)) for x in a.get("tags", [])] == [(0, 175, 1), (780, 177, 1), (240, 179, 1)]
              and a.get("pkt_dir") == 2,
              "S14 ID-S14: 780 m and 240 m as printed (the tags lie 770 and 250), Reverse, each duplicate 4 m back")
        a = r.get("NPL S9/S2D TLPR-R-148 (501) Down Main", {})
        check(a.get("next_stn") == 501 and a.get("tags", [{}])[-1] == {"dist": 230, "tag": 148} and a.get("os_speed") == 3,
              "S9/S2D into the block: the last 230 m to TLPR's border tag R-148, handed over to 501, OS 15 km/h (speed6 3)")
        sig = {g["name"]: g["foot_tag"] for g in k.get("signals", [])}
        check(sig.get("S9D") == 134 and sig.get("S14") == 179 and sig.get("R-183 (525)") == 183,
              "the signals at their foot tags, NPL's border signal by its tag")
        lines = {x["name"]: x["tags"] for x in lay["lines"]}
        check(lines.get("Down Main", [])[:3] == ["134", "134D", "136"] and lines.get("UP Main", [])[:2] == ["181", "181D"],
              "the lines from the routes, in location order")
        check(sorted(kavach_sheets.read_station_id(kavach_sheets.read_pages(os.path.join(FIXTURES, "NPL-TLPR_IB", f)))
                     for f in os.listdir(os.path.join(FIXTURES, "NPL-TLPR_IB")) if "CONTROL" in f) == [526],
              "the ToC's own Station ID: 526")
    # session 456: the track profile
    lay, notes = got.get("NPL-TLPR_IB", (None, []))
    if lay:
        r = {x["name"]: x for x in lay["kavach"]["routes"]}
        a = r.get("S14 ID-S14 UP Main", {})
        check([(x["d"], x["u"]) for x in a.get("ssp", [])] == [(126, 20), (894, 22)],
              "IB S14 ID-S14: (U,100,126), (U,110,894) -> u 20, 22")
        check([(x["d"], x["dir"], x["value"]) for x in a.get("gradient", [])]
              == [(236, 0, 7), (200, 0, 0), (101, 0, 6), (301, 0, 0), (182, 1, 5)],
              "IB S14 ID-S14's average gradients, 'F in 130' read as 1 in 130")
        check(all(sum(x["d"] for x in rt["ssp"]) == sum(t["dist"] for t in rt["tags"]) for rt in r.values()),
              "IB: every SSP covers its route")
    lay, notes = got.get("LPI_routes", (None, []))
    if lay:
        r = {x["name"]: x for x in lay["kavach"]["routes"]}
        a = r.get("S50-S48 Down Main", {})
        check([(x["d"], x["u"]) for x in a.get("ssp", [])] == [(1670, 22)]
              and [(x["d"], x["dir"], x["value"]) for x in a.get("gradient", [])] == [(1245, 0, 4), (320, 0, 0), (105, 1, 3)],
              "LPI S50-S48: 1670 m at 110; F 1 in 209 -> 4, Level -> 0, R 1 in 260 -> 3 (as station 527 sent)")
        check([(x["type"], x["sd"], x["ln"]) for x in a.get("track_conditions", [])] == [(8, 780, 0), (8, 905, 0)],
              "LPI S50-S48: fouling marks 780 and 905 m from S50, no length on the sheet")
        b = r.get("S48-S43 Down Main", {})
        check([(x["d"], x["u"]) for x in b.get("ssp", [])] == [(534, 16)]
              and [(x["d"], x["dir"], x["value"]) for x in b.get("gradient", [])] == [(534, 1, 4)],
              "LPI S48-S43: 534 m at 80; R 1 in 221 -> 4")
        c = r.get("S50-S47 Down Common Loop 1", {})
        check(c.get("turnouts") == [{"speed": 3, "start": 808, "release": 1065}], "LPI S50-S47: turnout 15 km/h from 808 m for 1065 m")
        d = r.get("S50A-S48 Down Main", {})
        check(d.get("ssp") == a.get("ssp") and d.get("gradient") == a.get("gradient"),
              "LPI S50A-S48 (calling-on over the same tags) takes S50-S48's profile")
        check(has(notes, "Fouling Mark given without a length: sent as 0 m"), "LPI: lengthless fouling marks said once")
        # session 457: the TSR route table (its .docx, rows 1 and 9)
        trs = {t["id"]: t for t in lay["kavach"].get("tsr_routes", [])}
        t = trs.get(24, {})
        check(sorted(trs) == [12, 24] and t.get("routes") == ["S50-S48 Down Main"] and len(t.get("poles", [])) == 36
              and t["poles"][0] == {"pole": "159/24", "dist": 42.0} and t["poles"][3] == {"pole": "159/30", "dist": 186.0}
              and t["poles"][-1] == {"pole": "161/14", "dist": 1726.74},
              "TSR route 24: S50-S48, 159/24 at 42 m, 159/30 at 186 m, 161/14 at 1726.74 m")
        check(trs.get(12, {}).get("routes") == ["S48-S43 Down Main"] and trs[12]["poles"][0] == {"pole": "161/12", "dist": 33.0},
              "TSR route 12: S48-S43 from 161/12 at 33 m")
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
    elif not all(os.path.isdir(os.path.join(LOCAL, d)) for d in PACK):
        check(False, "local pack: a section folder is missing")
    else:
        folders = [os.path.join(LOCAL, d) for d in PACK]
        pack = kavach_sheets.read_pack(folders)
        got = {s.folder: (s, kavach_sheets.build_layout(s, pack)) for s in pack.sections}
        own = 0
        allnotes = []
        for d, (sid, nown, ntags, nroutes, nnotes) in PACK.items():
            sec, (lay, notes) = got[os.path.join(LOCAL, d)]
            allnotes += notes
            check(sec.station_id == sid, f"{d}: station {sid}")
            check(len(sec.tags) == nown, f"{d}: {nown} tags on its sheet")
            check(lay is not None and len(lay["tags"]) == ntags, f"{d}: {ntags} tags in the file")
            check(lay is not None and len(lay.get("kavach", {}).get("routes", [])) == nroutes, f"{d}: {nroutes} routes")
            check(len(notes) == nnotes, f"{d}: {nnotes} sheet notes (got {len(notes)})")
            own += len(sec.tags)
        check(own == 712, "the whole pack: 712 tags")
        routes = {}
        for d in PACK:
            lay = got[os.path.join(LOCAL, d)][1][0]
            for r in lay.get("kavach", {}).get("routes", []):
                routes[(d, r["name"])] = r
        r = routes.get(("GGD-21-07-2023", "S6-S7D (522) Down Main"), {})
        check(r.get("next_stn") == 522 and r.get("loc_reset") == {"start": 1611, "correction": 107, "adj_dir": 1},
              "GGD S6 into the IB: handed over to 522 at SKP-GGD IB's S7D, location reset 107 m at 1611 m, Nominal")
        r = routes.get(("NPL-TLPR IB", "NPL S9/S2D TLPR-S2 ID (501) Down Main"), {})
        check(r.get("next_stn") == 501, "the IB's S9/S2D route ends at TLPR's S2 ID, named from TLPR's ToC")
        check(has(allnotes, "TLPR", "#13", "add up to 1034 m", "says 1024 m"), "TLPR #13: 1034 m of links, 1024 m given")
        check(has(allnotes, "TLPR", "#12", "add up to 2600 m", "says 2361 m"), "TLPR #12: 2600 m of links, 2361 m given")
        check(has(allnotes, "GGD", "#7 S1 → -", "stops at R-516, 199 m of the 1125 m given: not a route"),
              "GGD #7: a Red / Nil row with a sequence of one link is not made a route")
        check(has(allnotes, "LPI", "#31 S1637 → -", "590 m of the 790 m given"), "LPI #31: 590 m of 790 m")
        check(has(allnotes, "SKP", "Border RFID given for S6 (station 524) is R-374, the foot tag of this station's S6"),
              "SKP: its border RFIDs are its own last stop signals' foot tags")
        check(has(allnotes, "TLPR", "#7", "3 MA sections", "at Green"), "TLPR #7: 3 MA sections at Green")
        r = routes.get(("TLPR", "S15 TLPR/14D NPL-TLPR IB-S14 ID (526) Up Main Line"), {})
        check([x["dist"] for x in r.get("tags", [])] == [0, 40, 110, 654, 230], "TLPR #13's 654 m kept as printed")
        # session 456
        n = len(routes)
        check(sum(1 for x in routes.values() if x["ssp"]) == n - 3 and sum(1 for x in routes.values() if x["gradient"]) == n - 2,
              f"the profiles: all but 3 of the {n} routes have their SSP, all but 2 their gradient")
        check(sum(len(x["lc"]) for x in routes.values()) == 23 and sum(len(x["turnouts"]) for x in routes.values()) == 59
              and sum(len(x["track_conditions"]) for x in routes.values()) == 42,
              "23 LC gate rows, 59 turnouts, 42 track conditions")
        check(has(allnotes, "TLPR", "S18", "speed profile covers 3361 m of the route's 2600 m"),
              "TLPR S18: 3361 m of profile for 2600 m of links, not taken")
        check(has(allnotes, "TLPR", "S1D → S1", "813 m of the route's 1080 m"), "TLPR S1D-S1: 813 m of SSP for 1080 m")
        # session 457
        lpi = got[os.path.join(LOCAL, "LPI")][1][0]
        trs = {t["id"]: t for t in lpi["kavach"].get("tsr_routes", [])}
        check(len(lpi["kavach"].get("tsr_routes", [])) == 19, "LPI: 19 TSR routes of the table's 20 rows")
        check(trs.get(18, {}).get("routes") == ["S43-S1629 Down Main", "S1629-S1637 Down Main"],
              "TSR route 18 runs over S43-S1629 and S1629-S1637 (its row goes on, where no route does)")
        check(trs.get(17, {}).get("routes") == ["S6 LPI/20D TLPR-S20 (501) Up Main"], "TSR route 17 into TLPR")
        check(has(allnotes, "TSR_ROUTE", "TSR route id 22 is given to 2 rows") and has(allnotes, "TSR_ROUTE", "TSR route id 12 is given to 2 rows"),
              "TSR route ids 22 and 12 each given twice: said")
        check(has(allnotes, "TSR_ROUTE", "no rows 11, 12"), "the table's rows 11 and 12 absent: said")
        check(has(allnotes, "TSR_ROUTE", "#13 S25(HFZ)", "not taken"), "#13 starts at Hafizpet's S25: not LPI's to take")
    print(f"kavach sheets: {ok}/{total} {'PASS' if ok == total else 'FAIL'}")
    return 0 if ok == total else 1


if __name__ == "__main__":
    sys.exit(main())
