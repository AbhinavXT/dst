#!/usr/bin/env python3
"""Read a firm's KAVACH data sheets into a DLConsole station layout (session 454).

    python3 scripts/kavach_sheets.py FOLDER [FOLDER ...] [-o OUTDIR]

One FOLDER per station or IB section, as the firm hands them over (HBL's
2023 sheets: KAVACH_RFID_TAG_DATA_*.pdf, KAVACH_TABLE_OF_CONTROL_*.pdf, ...).
Writes station_<id>.json (Station Layout opens it) into OUTDIR, default the
folder itself, and prints what the sheets do not agree on.

Read so far:
  the RFID TAG DATA sheet  every tag column: its PAGE-X / PAGE-Y words go in
                           as printed (they are what is programmed); each
                           printed field row is checked against the bits its
                           bit positions name, the CRC-30, and each main tag
                           against its duplicate.

A sheet is read through `pdftotext -bbox` (poppler): words and where they
are on the page, so a table's columns line up by position. The .html that
command writes is read as well, which is how the committed extracts in
schema/fixtures/kavach_sheets/ are kept (`pdftotext -bbox -f 16 -l 23`,
named ..._pages16-23.html so the notes give the sheet's own page numbers).

Nothing is corrected. What the sheets do not agree on goes into the file's
"sheet_notes" (Station Layout lists them under Checks as "Sheet: ...") and
is said as what each sheet shows; which one is right is for the signatories.
Standard library only, plus the repo's schema/rfid_oracle.py for the CRC-30.
"""
import html
import json
import os
import re
import subprocess
import sys
import tempfile
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "schema"))
import rfid_oracle  # noqa: E402  (the CRC-30 and the decoder the gate validates)

FORMAT = "dlconsole-station-layout"
WORD = re.compile(r'<word xMin="([\d.]+)" yMin="([\d.]+)" xMax="([\d.]+)" yMax="([\d.]+)">(.*?)</word>')
HEX = re.compile(r"^[0-9a-fA-F]{1,16}$")
M64 = (1 << 64) - 1
# The normal tag's rows by bit position (Annexure-D), for the notes; another
# type's rows are named by the sheet's own description.
NORMAL_FIELDS = {"X3-X0": "type", "X5-X4": "version", "X15-X6": "unique id", "X38-X16": "abs_loc",
                 "X46-X39": "TIN nominal", "X54-X47": "TIN reverse", "Y6-Y0 & X63-X55": "station nominal",
                 "Y22-Y7": "station reverse", "Y24-Y23": "section nominal", "Y26-Y25": "section reverse",
                 "Y30-Y27": "placement", "Y31": "duplication", "Y32": "comm nominal", "Y33": "comm reverse"}
BITS = re.compile(r"^[XYxy]\d+(?:-[XYxy]?\d+)?$")


# ---- reading a sheet --------------------------------------------------------
class Word:
    __slots__ = ("x0", "y0", "x1", "y1", "t")

    def __init__(self, x0, y0, x1, y1, t):
        self.x0, self.y0, self.x1, self.y1, self.t = x0, y0, x1, y1, t

    @property
    def yc(self):
        return (self.y0 + self.y1) / 2


def pages_of_html(text):
    out = []
    for pg in text.split("<page ")[1:]:
        out.append([Word(float(a), float(b), float(c), float(d), html.unescape(t))
                    for a, b, c, d, t in WORD.findall(pg)])
    return out


def read_pages(path):
    """[[Word, ...] per page] from a .pdf (through pdftotext -bbox) or the .html it writes."""
    if path.lower().endswith((".html", ".htm")):
        with open(path, encoding="utf-8") as fh:
            return pages_of_html(fh.read())
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "bbox.html")
        try:
            subprocess.run(["pdftotext", "-bbox", path, out], check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        except FileNotFoundError:
            sys.exit("pdftotext (poppler) is needed to read a PDF: brew install poppler, "
                     "apt install poppler-utils, or give the .html `pdftotext -bbox` writes")
        with open(out, encoding="utf-8") as fh:
            return pages_of_html(fh.read())


def kind_of(name):
    n = name.upper().replace(" ", "_")
    if "TAG_DATA" in n:
        return "tags"
    if "TABLE_OF_CONTROL" in n:
        return "toc"
    if "TRACK_PROFILE" in n:
        return "profile"
    if "TSR_ROUTE" in n:
        return "tsr"
    return None


def version_of(name):
    m = re.search(r"[Vv_ ](\d+[.-]\d+[.-]\d+)", name)
    return m.group(1).replace("-", ".") if m else ""


def row_words(ws, y, tol=4.0):
    return [w for w in ws if abs(w.yc - y) <= tol]


# ---- bits -------------------------------------------------------------------
def tag_words(px, py):
    return int(px, 16), int(py, 16)


def bit_parts(spec, width=None):
    """'Y6-Y0 & X63-X55' -> [('y', 6, 0), ('x', 63, 55)], most significant part first.

    A range across the two words ('Y0-X63': the LC approach bits) is Y's low
    bits down to 0, then X's from 63 down. None for what names no bit of a
    tag (HBL's LC sheets print 'Y131' and 'Y-14-Y12'): not guessed at.
    """
    parts = []
    for p in re.split(r"\s*&\s*", spec.strip()):
        m = re.fullmatch(r"([XYxy])(\d+)(?:-([XYxy])?(\d+))?", p.replace(" ", ""))
        if not m:
            return None
        w1, a = m.group(1).lower(), int(m.group(2))
        w2 = (m.group(3) or m.group(1)).lower()
        b = int(m.group(4)) if m.group(4) is not None else a
        if a > 63 or b > 63:
            return None
        if w1 == w2:
            parts.append((w1, max(a, b), min(a, b)))
        else:                       # Y<a> down to Y0, then X63 down to X<b>
            parts += [(w1, a, 0), (w2, 63, b)]
    return parts


def bits_get(x, y, parts):
    v = 0
    for w, hi, lo in parts:
        n = hi - lo + 1
        v = (v << n) | (((x if w == "x" else y) >> lo) & ((1 << n) - 1))
    return v


def bits_set(x, y, parts, value):
    for w, hi, lo in reversed(parts):
        n = hi - lo + 1
        field = value & ((1 << n) - 1)
        value >>= n
        mask = ((1 << n) - 1) << lo
        if w == "x":
            x = (x & ~mask) | (field << lo)
        else:
            y = (y & ~mask) | (field << lo)
    return x & M64, y & M64


def crc30(x, y):
    tag = bytearray(x.to_bytes(8, "little") + y.to_bytes(8, "little"))
    d = tag[:13]
    d[12] &= 0x03
    return rfid_oracle.rfid_crc30_bytes(bytes(d))


def decode(px, py):
    """The oracle's reading of a tag (type, unique, absLoc, dup, crcOk, ...)."""
    x, y = tag_words(px, py)
    return rfid_oracle._decode(bytes([0]) + x.to_bytes(8, "little") + y.to_bytes(8, "little"))


# ---- the RFID TAG DATA sheet ------------------------------------------------
class SheetTag:
    def __init__(self, label, px, py, page):
        self.label, self.px, self.py, self.page = label, px, py, page
        self.rows = []          # (description, bit spec, printed text, width)

    @property
    def number(self):
        m = re.match(r"\d+", self.label)
        return int(m.group()) if m else -1

    @property
    def is_dup(self):
        return self.label.rstrip().upper().endswith("D")

    @property
    def name(self):
        return f"{self.number}{'D' if self.is_dup else ''}"


def _first_word_at(ws, text):
    return next((w for w in ws if w.t == text), None)


def first_page(path):
    """An extract of some pages (`pdftotext -bbox -f 16 -l 23`, saved as
    ..._pages16-23.html) numbers its pages as the sheet does."""
    m = re.search(r"pages(\d+)-\d+", os.path.basename(path))
    return int(m.group(1)) if m else 1


def read_tag_sheet(pages, page1=1):
    """Every tag column of every page, with the field rows printed for it."""
    out = []
    for pi, ws in enumerate(pages, start=page1 - 1):
        wx = _first_word_at(ws, "PAGE-X")
        wy = _first_word_at(ws, "PAGE-Y")
        if not wx or not wy:
            continue
        hx = sorted([w for w in row_words(ws, wx.yc) if w.x0 > wx.x1 + 60 and HEX.match(w.t) and len(w.t) >= 8],
                    key=lambda w: w.x0)
        hy = sorted([w for w in row_words(ws, wy.yc) if w.x0 > wy.x1 + 60 and HEX.match(w.t) and len(w.t) >= 8],
                    key=lambda w: w.x0)
        if not hx:
            continue
        cols = [w.x0 for w in hx]
        span = (cols[-1] - cols[0]) / max(1, len(cols) - 1) if len(cols) > 1 else 100.0

        def col_of(w):
            i = min(range(len(cols)), key=lambda i: abs(cols[i] - w.x0))
            return i if abs(cols[i] - w.x0) < span * 0.45 else None

        # The labels: the row above the first field row holding a word per column.
        first_field = min((w.yc for w in ws if (BITS.match(w.t) or w.t.lower() == "x3") and w.x1 < cols[0] - 40),
                          default=wx.yc)
        labels = [None] * len(cols)
        for w in sorted(ws, key=lambda w: -w.yc):
            if w.yc >= first_field - 2 or not re.search(r"\d", w.t):
                continue
            c = col_of(w)
            if c is not None and labels[c] is None and re.fullmatch(r"\d+\s*-?\s*[MDmd]?", w.t):
                labels[c] = w.t
        tags = []
        for c, h in enumerate(hx):
            yw = next((w for w in hy if col_of(w) == c), None)
            tags.append(SheetTag(labels[c] or f"?{pi + 1}.{c + 1}", h.t.lower(), yw.t.lower() if yw else "", pi + 1))
        # The field rows: the "Bit positions" column, values under the tags. A
        # position can run onto a second line ("Y6-Y0 & X63-" / "X55").
        hb = next((w for w in ws if w.t == "Bit"), None)
        hn = next((w for w in ws if w.t == "No."), None)
        bx0 = hb.x0 - 40 if hb else 0
        bx1 = hn.x0 - 2 if hn and hn.x0 < cols[0] else cols[0] - 40
        toks = sorted([w for w in ws if bx0 <= w.x0 and w.x1 <= bx1 and w.y0 > (hb.y1 if hb else 0)
                       and re.fullmatch(r"[XYxy]-?\d*-?(?:[XYxy]?-?\d+)*-?|&|-", w.t)],
                      key=lambda w: (round(w.yc), w.x0))
        i = 0
        while i < len(toks):
            first = last = toks[i]
            spec = first.t
            i += 1
            while i < len(toks) and (spec.endswith(("&", "-")) or toks[i].t in ("&", "-")) \
                    and toks[i].yc - first.yc < 20:
                spec += (" &" if toks[i].t == "&" else (" " if spec.endswith("&") else "") + toks[i].t)
                last = toks[i]
                i += 1
            spec = re.sub(r"\s*&\s*", " & ", spec.replace(" - ", "-")).strip().upper()
            if not re.match(r"[XY]", spec) or spec.startswith(("X63-X0", "Y63-Y0")):
                continue
            # The row: the cell's own height, a little either side (a value sits
            # between the two lines of a two-line position).
            top, bot = min(first.y0, last.y0) - 1, max(first.y1, last.y1) + 1
            line = [w for w in ws if top <= w.yc <= bot]
            width = next((int(w.t) for w in line if bx1 - 5 <= w.x0 < cols[0] - 20 and re.fullmatch(r"\d{1,2}", w.t)),
                         None)
            desc = " ".join(w.t for w in sorted(line, key=lambda w: w.x0) if w.x1 < bx0)
            # One value per column: the word nearest the cell's middle (a
            # neighbouring row's value can reach into a two-line cell).
            mid = (top + bot) / 2
            vals = [None] * len(cols)
            for w in line:
                c = col_of(w)
                if c is not None and w.x0 > cols[0] - 20 and (vals[c] is None or abs(w.yc - mid) < abs(vals[c].yc - mid)):
                    vals[c] = w
            for c, v in enumerate(vals):
                if v is not None:
                    tags[c].rows.append((desc, spec, v.t, width))
        out.extend(tags)
    return out


def check_tags(tags, src):
    """What the sheet's own columns do not agree on, one line each."""
    notes = []
    odd = {}
    for t in tags:
        for desc, spec, v, width in t.rows:
            if bit_parts(spec, width) is None:
                odd.setdefault((t.page, spec), (desc, []))[1].append(t.label)
    for (page, spec), (desc, labels) in sorted(odd.items()):
        notes.append(f"{src} p{page}: the bit position {spec!r} ({desc or 'no description'}) names no bit of a tag: "
                     f"that row is not checked for {', '.join(labels)}")
    for t in tags:
        try:
            x, y = tag_words(t.px, t.py)
            ok_pages = bool(HEX.match(t.px)) and bool(HEX.match(t.py))
        except ValueError:
            ok_pages = False
        if not ok_pages:
            notes.append(f"{src} p{t.page}, tag {t.label}: PAGE-X {t.px!r} / PAGE-Y {t.py!r} are not hex words")
            continue
        short = [n for n, v in (("PAGE-X", t.px), ("PAGE-Y", t.py)) if len(v) != 16]
        bad = []
        printed_x, printed_y, crc_printed, settable = x, y, None, True
        printed_type = next((int(v) for d_, sp, v, w_ in t.rows if sp == "X3-X0" and v.isdigit()), x & 0xF)
        for desc, spec, v, width in t.rows:
            parts = bit_parts(spec, width)
            if not parts:
                continue
            if spec.upper() == "Y63-Y34":
                if not HEX.match(v):
                    continue
                crc_printed = int(v, 16)
                if crc_printed != bits_get(x, y, parts):
                    bad.append(f"CRC {v} printed, {bits_get(x, y, parts):08x} in PAGE-Y")
                continue
            if not re.fullmatch(r"\d+", v):
                settable = False
                continue
            got = bits_get(x, y, parts)
            if got != int(v):
                what = NORMAL_FIELDS.get(spec) if printed_type == 9 else None
                bad.append(f"{what or desc or spec} ({spec}) printed {v}, the pages hold {got}")
            printed_x, printed_y = bits_set(printed_x, printed_y, parts, int(v))
        crc = crc30(x, y)
        stored = (y >> 34) & 0x3FFFFFFF
        if crc != stored:
            bad.append(f"CRC-30 {stored:08x} in PAGE-Y, {crc:08x} computed")
        d = decode(t.px, t.py)
        if d["unique"] != t.number and not any("X15-X6" in b for b in bad):
            bad.append(f"the pages are tag {d['unique']}")
        if (d.get("dup", 0) == 1) != t.is_dup and not any("(Y31)" in b for b in bad):
            bad.append(f"the pages say {'duplicate' if d.get('dup') else 'main'} tag")
        if not bad:
            continue
        line = f"{src} p{t.page}, tag {t.label}: " + "; ".join(bad)
        if short:
            line += f" ({' and '.join(short)} {'is' if len(short) == 1 else 'are'} not 16 digits)"
        if settable:
            c = crc30(printed_x, printed_y)
            gx, gy = printed_x, (printed_y & ((1 << 34) - 1)) | (c << 34)
            if (gx, gy) != (x, y):
                line += f". The printed fields make PAGE-X {gx:016x} PAGE-Y {gy:016x}, CRC-30 {c:08x}"
                if crc_printed is not None:
                    line += ", the CRC printed" if c == crc_printed else f", not the {crc_printed:08x} printed"
        notes.append(line)
    # Each main tag against its duplicate.
    by = {}
    for t in tags:
        by.setdefault(t.number, {}).setdefault("D" if t.is_dup else "M", t)
    for n, pair in sorted(by.items()):
        if set(pair) != {"M", "D"}:
            notes.append(f"{src}: tag {n} has no {'duplicate' if 'M' in pair else 'main'} column")
            continue
        m, dd = decode(pair["M"].px, pair["M"].py), decode(pair["D"].px, pair["D"].py)
        if not (m["crcOk"] and dd["crcOk"]) or m["unique"] != n or dd["unique"] != n:
            continue          # already said, column by column
        diff = [k for k in m if k not in ("readerId", "dup", "crcStored", "crcCalc", "crcOk", "absLoc", "absLoc2")
                and m[k] != dd.get(k)]
        if diff:
            notes.append(f"{src}: tag {n}M and {n}D differ in " + ", ".join(
                f"{k} ({m[k]} / {dd.get(k)})" for k in diff))
        for k in ("absLoc", "absLoc2"):
            if k in m and abs(m[k] - dd[k]) not in (0, 4):
                notes.append(f"{src}: tag {n}M at {m[k]} m, {n}D at {dd[k]} m ({dd[k] - m[k]:+d} m)")
    return notes


# ---- a station folder -------------------------------------------------------
def sheets_in(folder):
    out = {}
    for name in sorted(os.listdir(folder)):
        k = kind_of(name)
        if k and name.lower().endswith((".pdf", ".html")):
            out.setdefault(k, []).append(os.path.join(folder, name))
    return out


def station_layout(folder):
    """(layout dict, notes) for one station / IB folder."""
    sheets = sheets_in(folder)
    notes, sources = [], []
    tags = []
    for path in sheets.get("tags", []):
        src = os.path.basename(path)
        sources.append(src)
        t = read_tag_sheet(read_pages(path), first_page(path))
        notes += check_tags(t, src)
        tags += t
    if not tags:
        return None, [f"{folder}: no RFID tag data sheet"]
    # The station: the ID most of its own normal tags carry in the nominal direction.
    ids = Counter()
    locs = []
    for t in tags:
        d = decode(t.px, t.py)
        if d["crcOk"] and d["type"] == 9:
            ids[d["stnNom"]] += 1
            locs.append(d["absLoc"])
    station_id = ids.most_common(1)[0][0] if ids else 0
    centre = (min(locs) + max(locs)) // 2 if locs else 0
    names = Counter(t.name for t in tags)
    for n, c in names.items():
        if c > 1:
            notes.append(f"tag {n} is printed {c} times")
    layout = {
        "format": FORMAT,
        "version": 1,
        "comment": (f"Read from the KAVACH sheets by scripts/kavach_sheets.py (session 454): {', '.join(sources)}. "
                    f"Station {station_id}: the station ID most of its normal tags carry. Its location {centre} "
                    f"is not on the sheets: the middle of its tags, for the drawing."),
        "tags": [{"name": t.name, "page_x": t.px, "page_y": t.py} for t in tags],
        "signals": [], "points": [], "lines": [],
        "stations": [{"station_id": station_id, "location": centre}],
        "texts": [], "other_sheets": [],
        "sheet_notes": notes,
    }
    return layout, notes


def layout_text(layout):
    """The file as written: what a golden is compared with."""
    return json.dumps(layout, indent=1, ensure_ascii=False) + "\n"


def main(argv):
    args, outdir = [], None
    it = iter(argv)
    for a in it:
        if a == "-o":
            outdir = next(it, None)
        else:
            args.append(a)
    if not args:
        print(__doc__)
        return 64
    rc = 0
    for folder in args:
        layout, notes = station_layout(folder)
        if layout is None:
            print("\n".join(notes))
            rc = 1
            continue
        sid = layout["stations"][0]["station_id"]
        out = os.path.join(outdir or folder, f"station_{sid}.json")
        with open(out, "w", encoding="utf-8") as fh:
            fh.write(layout_text(layout))
        print(f"{out}: {len(layout['tags'])} tags, {len(notes)} sheet notes")
        for n in notes:
            print("   ", n)
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
