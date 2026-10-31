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
                           against its duplicate.                (454)
  the TABLE OF CONTROL     every route row as a Stationary KAVACH route: its
                           signals, RFID sequence and linking distances,
                           points, track circuits, TINs, section type, OS
                           speed; the next station and location reset from
                           the "Last Stop Signal" table; the layout's lines
                           and signals from the routes.          (455)
  the TRACK PROFILE TABLE  each row onto its route: SSP, gradient (the
                           average rows), LC gates, turnouts, track
                           conditions, each list checked against the route's
                           length.                               (456)
  the TSR ROUTE TABLE      (its .docx: real table cells) each TSR route over
                           the routes it runs, its OHE poles at distances
                           from its start tag: a TSR is then given from pole
                           to pole (Station Layout, KAVACH tab, TSR). (457)

Folders given together are read as one stretch: a route that runs into the
next section ends on that section's tag and signal, which are copied in.

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


# ---- the TABLE OF CONTROL ---------------------------------------------------
def lines_of(ws, tol=3.5):
    """Words -> visual lines [(y centre, [words])], top to bottom."""
    out = []
    for w in sorted(ws, key=lambda w: (w.yc, w.x0)):
        if out and abs(out[-1][0] - w.yc) < tol:
            out[-1][1].append(w)
        else:
            out.append((w.yc, [w]))
    return out


def cell(ws):
    t = " ".join(" ".join(w.t for w in sorted(ln, key=lambda w: w.x0)) for _, ln in lines_of(ws)).strip()
    return re.sub(r"\bCom mon\b", "Common", t)     # a word the PDF breaks


# A header cell's text (its words top to bottom) -> the column it is. Two
# cells hold two columns: "Requires Points in Route / Normal | Reverse" and,
# on some sheets, "Check RFID Sequence / Entry Signal Foot Tag | (Linking ...)".
HEADERS = [("sno", lambda t: t.startswith("S.")),
           ("foot", lambda t: t.startswith("Entry Signal Foot")),
           ("entry", lambda t: t.startswith("Entry Signal")),
           ("exit", lambda t: t.startswith("Exit Signal")),
           ("section", lambda t: t.startswith("Section")),
           ("line", lambda t: t == "Line"),
           ("tsrms", lambda t: "Route Id" in t),
           ("sigtype", lambda t: t.startswith("Signal")),
           ("asp_entry", lambda t: t.startswith("Aspects of Entry")),
           ("asp_exit", lambda t: t.startswith("Requires Aspects")),
           ("next_sections", lambda t: t.startswith("Requires Next")),
           ("points", lambda t: t.startswith("Requires Points")),
           ("tcs", lambda t: t.startswith("Requires Track")),
           ("tins", lambda t: "Identification" in t),
           ("tins_overlap", lambda t: "Overlap" in t),
           ("seq", lambda t: t.startswith("Check RFID")),
           ("distance", lambda t: t.startswith("Distance")),
           ("ma", lambda t: t.startswith("Movement")),
           ("os_speed", lambda t: t.startswith("Authorized"))]


def toc_columns(ws):
    """[(column, x from, x to)] from the header words, or None on a page without the table."""
    s = next((w for w in ws if w.t in ("S.", "S.No", "S.No.")), None)
    if not s:
        return None, None, None
    hdr = [w for w in ws if s.y0 - 40 < w.yc < s.y1 + 34]
    spans = []
    for w in sorted(hdr, key=lambda w: w.x0):
        if spans and w.x0 <= spans[-1][1] + 1:
            spans[-1][1] = max(spans[-1][1], w.x1)
            spans[-1][2].append(w)
        else:
            spans.append([w.x0, w.x1, [w]])
    named = []
    for x0, x1, words in spans:
        t = " ".join(w.t for w in sorted(words, key=lambda w: (round(w.y0), w.x0)))
        key = next((k for k, f in HEADERS if f(t)), None)
        if t.startswith("No") and named and named[-1][0] == "sno":
            named[-1] = ("sno", named[-1][1], x1)
        elif key == "sigtype" and named and named[-1][0] == "sigtype":
            named[-1] = ("sigtype", named[-1][1], x1)
        elif key == "points":
            nw = next((w for w in words if w.t == "Normal"), None)
            rw = next((w for w in words if w.t == "Reverse"), None)
            mid = (nw.x1 + rw.x0) / 2 if nw and rw else (x0 + x1) / 2
            named += [("points_n", x0, mid), ("points_r", mid, x1)]
        elif key == "seq":
            foot = next((w for w in words if w.t == "Foot"), None)
            paren = [w.x0 for w in words if w.t.startswith("(")]
            if foot and paren and min(paren) > foot.x1:
                split = (foot.x1 + min(paren)) / 2
                named += [("foot", x0, split), ("seq", split, x1)]
            else:
                named.append(("seq", x0, x1))
        elif key is None and named and named[-1][0] == "seq":
            named[-1] = ("seq", named[-1][1], x1)        # "," "En-" of a wrapped heading
        else:
            named.append((key or "?", x0, x1))
    cols = []
    for i, (key, x0, x1) in enumerate(named):
        lo = (named[i - 1][2] + x0) / 2 if i else 0
        hi = (x1 + named[i + 1][1]) / 2 if i + 1 < len(named) else 1e9
        cols.append((key, lo, hi))
    return cols, s, max(w.y1 for w in hdr)


def row_bounds(body, anchors, foot_y):
    """[(top, bottom)] of each table row, from its S.No word.

    Every cell is centred on its row, as the S.No is, so the rows tile: each
    boundary is the one before reflected in the next S.No's centre (cells can
    touch, so the gaps alone do not split rows). Each boundary then moves to
    the nearest height no word straddles, within 8 pt, so a wrapped line stays
    in its cell's row."""
    c = [a.yc for a in anchors]
    prev = min([w.y0 for w in body if w.yc < c[0]] + [anchors[0].y0]) - 1.5
    cuts = []
    for i in range(len(c)):
        nxt = 2 * c[i] - prev
        if i + 1 < len(c) and not (c[i] < nxt < c[i + 1]):
            nxt = (c[i] + c[i + 1]) / 2
        if i + 1 == len(c):
            nxt = foot_y
        else:
            clear = [y / 2 for y in range(int(2 * (nxt - 8)), int(2 * (nxt + 8)) + 1)
                     if c[i] < y / 2 < c[i + 1] and not any(w.y0 < y / 2 < w.y1 for w in body)]
            if clear:
                nxt = min(clear, key=lambda y: abs(y - nxt))
        cuts.append((prev, nxt))
        prev = nxt
    return cuts


def read_toc(pages, page1=1):
    """Every route row: its cells by column, the page, and its aspect sub-rows."""
    rows = []
    for pi, ws in enumerate(pages, start=page1 - 1):
        cols, s, hdr_bottom = toc_columns(ws)
        if not cols:
            continue
        colx = {k: (a, b) for k, a, b in cols}
        if not {"sno", "entry", "exit", "seq"} <= set(colx):
            continue

        def incol(w, key):
            a, b = colx.get(key, (0, -1))
            return a <= (w.x0 + w.x1) / 2 < b

        # The table ends at the title block.
        foot_y = min([w.y0 for w in ws if w.t in ("Last", "REF:", "PREPARED", "SIGNAL", "TELECOMMUNICATIONS")
                      and w.y0 > s.y1 + 40] + [1e9])
        body = [w for w in ws if hdr_bottom < w.yc and w.y0 < foot_y]
        anchors = sorted([w for w in body if incol(w, "sno") and re.fullmatch(r"\d{1,3}", w.t)], key=lambda w: w.yc)
        if not anchors:
            continue
        for a, (top, bot) in zip(anchors, row_bounds(body, anchors, foot_y)):
            rw = [w for w in body if top <= w.yc < bot]
            r = {"sno": int(a.t), "page": pi + 1}
            for key, lo, hi in cols:
                if key not in ("sno", "?"):
                    r[key] = cell([w for w in rw if lo <= (w.x0 + w.x1) / 2 < hi])
            for key in ("entry", "exit"):     # a name wrapped in its cell: "S15 TLPR/ 14D NPL- TLPR IB"
                r[key] = re.sub(r"([/-])\s+", r"\1", r.get(key, "")).strip()
            # The aspect sub-rows: the exit signal's aspect and the MA sections beside it.
            r["subs"] = []
            for y, ln in lines_of([w for w in rw if incol(w, "ma")]):
                ex = cell([w for w in rw if incol(w, "asp_exit") and abs(w.yc - y) < 6])
                r["subs"].append((ex, " ".join(w.t for w in ln)))
            rows.append(r)
    return rows


def read_station_id(pages):
    for ws in pages[:1]:
        for i, w in enumerate(ws):
            if w.t == "ID:" and i > 0 and ws[i - 1].t == "Station":
                nxt = [v for v in ws if abs(v.yc - w.yc) < 4 and v.x0 > w.x1 and re.fullmatch(r"\d{1,4}", v.t)]
                if nxt:
                    return int(min(nxt, key=lambda v: v.x0).t)
    return None


def read_borders(pages, page1=1):
    """The "Last Stop Signal for entering into Auto/Block Section" table: per
    row the signal, the next station, its border tag and that tag's location,
    and the location reset when one is given (distance from start, correction,
    loco direction after it)."""
    out = []
    for pi, ws in enumerate(pages, start=page1 - 1):
        last = next((w for w in ws if w.t == "Last"), None)
        if not last:
            continue
        hdr = [w for w in ws if last.y0 - 5 <= w.y0 <= last.y0 + 60 and w.x0 >= last.x0 - 5]
        lss = next((w for w in hdr if w.t == "LSS"), None)
        idw = next((w for w in hdr if w.t == "Id"), None)
        if not lss or not idw:
            continue
        hdr_bottom = max(w.y1 for w in hdr)
        endw = next((w for w in ws if w.t == "Signals" and w.y0 > hdr_bottom), None)
        end_y = endw.y0 if endw else hdr_bottom + 200
        right = max(w.x1 for w in hdr)
        body = [w for w in ws if hdr_bottom < w.yc < end_y and last.x0 - 5 <= w.x0 <= right + 20]
        rfids = sorted([w for w in body if re.match(r"R-\d+", w.t)], key=lambda w: w.yc)
        cy = [w.yc for w in rfids]
        for i, rw in enumerate(rfids):
            top = (cy[i - 1] + cy[i]) / 2 if i else hdr_bottom
            bot = (cy[i] + cy[i + 1]) / 2 if i + 1 < len(cy) else end_y
            row = [w for w in body if top <= w.yc < bot]
            name = cell([w for w in row if w.x1 < lss.x0 - 2])
            sid = [w for w in row if re.fullmatch(r"\d{1,3}", w.t)
                   and abs((w.x0 + w.x1) / 2 - (idw.x0 + idw.x1) / 2) < 25]
            m = re.match(r"R-(\d+),?(\d*)", rw.t)
            loc = m.group(2)
            if not loc:
                nxt = [w for w in row if w.x0 > rw.x1 - 1 and re.fullmatch(r"\d{5,7}", w.t) and abs(w.yc - rw.yc) < 15]
                loc = min(nxt, key=lambda w: w.x0).t if nxt else ""
            after = sorted([w for w in row if w.x0 > rw.x1 + 30 and abs(w.yc - rw.yc) < 15
                            and not re.fullmatch(r"\d{5,7}", w.t)], key=lambda w: w.x0)
            rest = [w.t for w in after][:3]
            reset = None
            if len(rest) == 3 and rest[0].isdigit() and rest[1].isdigit() and rest[2] in ("Nominal", "Reverse"):
                reset = {"start": int(rest[0]), "correction": int(rest[1]),
                         "adj_dir": 1 if rest[2] == "Nominal" else 2}
            out.append({"page": pi + 1, "lss": name, "next_stn": int(sid[0].t) if sid else None,
                        "border_tag": int(m.group(1)), "border_loc": int(loc) if loc else None, "reset": reset})
    return out


def links(seq):
    """('dist-first' | 'tag-first' | None, [(tag, distance)]) from an RFID sequence cell.

    dist-first, "(0, R-148), (774, R-150)": each distance is from the tag before.
    tag-first, "(R-134, 746), (R-136, 250)": each distance is to the tag after;
    the last one's runs to the exit signal's foot tag, which the cell does not name.
    A tag written without its "R-" ("(200,989)") is still the tag.
    """
    s = re.sub(r"\s+", "", seq)
    out, fmt = [], None
    for m in re.finditer(r"\((R-)?(\d+),(R-)?(\d+)\)?", s):
        if m.group(1):
            fmt = fmt or "tag-first"
            out.append((int(m.group(2)), int(m.group(4))))
        else:
            fmt = fmt or "dist-first"
            out.append((int(m.group(4)), int(m.group(2))))
    return fmt, out


SECTION_TYPES = {"station section": 0, "absolute block": 1, "auto section": 2, "auto": 2,
                 "automatic section": 2, "autoblock": 2}
ASPECT_SECTIONS = {"Green": 4, "Double Yellow": 3, "Yellow": 2, "Red": 1}   # Annexure-I, as Station KAVACH has them


def names_list(text):
    """'P-11, P-13' -> ['P-11', 'P-13']; '-' -> []. A note in brackets ('(W12R)') is dropped."""
    text = re.sub(r"\([^)]*\)", "", text or "")
    return [x for x in re.split(r"[,\s]+", text) if x.strip("-")]


# ---- the TRACK PROFILE TABLE -------------------------------------------------
def profile_columns(ws):
    """[(column, x from, x to)] of a track profile page, or None."""
    s = next((w for w in ws if w.t in ("S.", "S.No", "S.No.")), None)
    if not s:
        return None, None, None
    hdr = [w for w in ws if s.y0 - 60 < w.yc < s.y1 + 40]
    spans = []
    for w in sorted(hdr, key=lambda w: w.x0):
        if spans and w.x0 <= spans[-1][1] + 1:
            spans[-1][1] = max(spans[-1][1], w.x1)
            spans[-1][2].append(w)
        else:
            spans.append([w.x0, w.x1, [w]])
    named = []
    for x0, x1, words in spans:
        t = " ".join(w.t for w in sorted(words, key=lambda w: (round(w.y0), w.x0)))
        last = named[-1][0] if named else None
        if t.startswith("S."):
            named.append(("sno", x0, x1))
        elif t.startswith("No") and last == "sno":
            named[-1] = ("sno", named[-1][1], x1)
        elif t.startswith("Entry"):
            named.append(("entry", x0, x1))
        elif t.startswith("Exit"):
            named.append(("exit", x0, x1))
        elif t == "Line":
            named.append(("line", x0, x1))
        elif t.startswith("Speed Value"):
            named.append(("to_speed", x0, x1))
        elif "Turnout" in t:
            named.append(("to_start", x0, x1))
        elif t.startswith("Length") and last == "to_start":
            named.append(("to_len", x0, x1))
        elif t.startswith("Permanent"):
            named.append(("psr", x0, x1))
        elif t.startswith("Engineering"):
            named.append(("eng", x0, x1))
        elif "Average" in t:
            named.append(("avg", x0, x1))
        elif t.startswith("Length") and last == "avg":
            named.append(("grad_len", x0, x1))
        elif t.startswith("LC Distance"):
            named.append(("lc_d", x0, x1))
        elif t.startswith("LC ID"):
            named.append(("lc_id", x0, x1))
        elif "Manning" in t:
            named.append(("lc_man", x0, x1))
        elif t == "LC":
            named.append(("lc_class", x0, x1))
        elif t == "Class" and last == "lc_class":
            named[-1] = ("lc_class", named[-1][1], x1)
        elif "Whistling" in t:
            named.append(("lc_aw", x0, x1))
        elif t.startswith("Track Condition"):
            ty = next((w for w in words if w.t == "Type"), None)
            st = next((w for w in words if w.t == "Start"), None)
            if ty and st:
                mid = ((ty.x1 + st.x0) if st.x0 > ty.x1 else (st.x1 + ty.x0)) / 2
                named += [("tc_type", x0, mid), ("tc_start", mid, x1)] if st.x0 > ty.x1 \
                    else [("tc_start", x0, mid), ("tc_type", mid, x1)]
            else:
                named.append(("tc_type", x0, x1))
        elif t.startswith("Length") and last in ("tc_type", "tc_start"):
            named.append(("tc_len", x0, x1))
        else:
            named.append(("?", x0, x1))
    cols = []
    for i, (key, x0, x1) in enumerate(named):
        lo = (named[i - 1][2] + x0) / 2 if i else 0
        hi = (x1 + named[i + 1][1]) / 2 if i + 1 < len(named) else 1e9
        cols.append((key, lo, hi))
    return cols, s, max(w.y1 for w in hdr)


def read_profile(pages, page1=1):
    """Every row of the track profile table: its cells by column, and its
    average-gradient sub-rows (value text, length)."""
    rows = []
    for pi, ws in enumerate(pages, start=page1 - 1):
        cols, s, hdr_bottom = profile_columns(ws)
        if not cols:
            continue
        colx = {c: (a, b) for c, a, b in cols}
        if not {"sno", "entry", "exit", "psr", "avg"} <= set(colx):
            continue

        def incol(w, key):
            a, b = colx.get(key, (0, -1))
            return a <= (w.x0 + w.x1) / 2 < b

        foot_y = min([w.y0 for w in ws if w.t in ("PSR", "Rev", "REF:", "PREPARED", "SIGNAL", "TELECOMMUNICATIONS")
                      and w.y0 > s.y1 + 40] + [1e9])
        body = [w for w in ws if hdr_bottom < w.yc and w.y0 < foot_y]
        anchors = sorted([w for w in body if incol(w, "sno") and re.fullmatch(r"\d{1,3}", w.t)], key=lambda w: w.yc)
        if not anchors:
            continue
        for a, (top, bot) in zip(anchors, row_bounds(body, anchors, foot_y)):
            rw = [w for w in body if top <= w.yc < bot]
            r = {"sno": int(a.t), "page": pi + 1}
            for key, lo, hi in cols:
                if key not in ("sno", "?"):
                    r[key] = cell([w for w in rw if lo <= (w.x0 + w.x1) / 2 < hi])
            for key in ("entry", "exit"):
                r[key] = re.sub(r"([/-])\s+", r"\1", r.get(key, "")).strip()
            r["avg_rows"] = []
            for y, ln in lines_of([w for w in rw if incol(w, "avg")]):
                length = cell([w for w in rw if incol(w, "grad_len") and abs(w.yc - y) < 6])
                r["avg_rows"].append((" ".join(w.t for w in sorted(ln, key=lambda w: w.x0)).strip(" )("), length))
            rows.append(r)
    return rows


# The track condition types Annexure-C numbers (tcType).
TRACK_CONDITIONS = {"dead stop": 1, "radio hole": 2, "non-stopping": 3, "non stopping": 3, "tunnel stopping": 4,
                    "powerless": 5, "neutral": 5, "neutral section": 5, "sound horn": 6, "reversing": 7,
                    "fouling mark": 8, "kavach exit": 9}
LC_CLASSES = {"SPL": 0, "SPECIAL": 0, "A": 1, "B1": 2, "B2": 3, "B": 4, "C": 5, "D": 6}


def profile_rows(r, length, where, notes):
    """The SLRP profile rows of one track profile row (StationKavach's field
    names), each list checked against the route's `length`: one that does not
    cover it is left out and said (the station would refuse the whole answer)."""
    out = {"ssp": [], "gradient": [], "lc": [], "turnouts": [], "track_conditions": []}
    # SSP: (U, speed, length), speeds in steps of 5 km/h.
    ssp, ok = [], True
    for kind, v, d in re.findall(r"\(\s*([A-Z])\s*,\s*(\d+)\s*,\s*(\d+)\s*\)", r.get("psr", "")):
        if kind != "U" or int(v) % 5:
            notes.append(f"{where}: speed ({kind},{v},{d}) is not a universal speed in 5 km/h steps: SSP not taken")
            ok = False
            break
        ssp.append({"d": int(d), "class": 0, "u": int(v) // 5})
    if ok and ssp:
        if sum(x["d"] for x in ssp) == length:
            out["ssp"] = ssp
        else:
            notes.append(f"{where}: its speed profile covers {sum(x['d'] for x in ssp)} m of the route's {length} m: "
                         f"SSP not taken")
    # Gradient: the average rows. Rise (R) uphill, fall (F) and level downhill; value
    # 1000/N truncated (station 527 sent F 1 in 209 as 4, R 1 in 260 as 3, Level as 0).
    grad = []
    for text, d in r.get("avg_rows", []):
        m = re.match(r"(?i)\s*([RF])\s*(?:1\s*)?in\s*(\d+)", text)
        if not d.isdigit():
            continue
        if m:
            grad.append({"d": int(d), "dir": 1 if m.group(1).upper() == "R" else 0, "value": 1000 // int(m.group(2))})
        elif text.strip().upper().startswith("LEVEL"):
            grad.append({"d": int(d), "dir": 0, "value": 0})
        else:
            notes.append(f"{where}: average gradient {text!r} not read: gradient not taken")
            grad = []
            break
    if grad:
        if sum(x["d"] for x in grad) == length:
            out["gradient"] = grad
        else:
            notes.append(f"{where}: its average gradients cover {sum(x['d'] for x in grad)} m of the route's {length} m: "
                         f"gradient not taken")
    # Turnout speed: speed, where it starts ("-": at the entry signal), its length.
    sp, st, ln = r.get("to_speed", ""), r.get("to_start", ""), r.get("to_len", "")
    if sp.isdigit() and ln.isdigit():
        if int(sp) % 5:
            notes.append(f"{where}: turnout speed {sp} km/h is not in 5 km/h steps: not taken")
        else:
            out["turnouts"].append({"speed": int(sp) // 5, "start": int(st) if st.isdigit() else 0, "release": int(ln)})
    # LC gate.
    if r.get("lc_d", "").isdigit():
        idm = re.match(r"(\d+)\s*/?\s*([A-Ea-e])?", r.get("lc_id", ""))
        cls = LC_CLASSES.get(r.get("lc_class", "").replace(" ", "").upper())
        if not idm or cls is None:
            notes.append(f"{where}: LC gate {r.get('lc_id')!r} class {r.get('lc_class')!r} not read: not taken")
        else:
            aw = r.get("lc_aw", "").lower()
            out["lc"].append({"d": int(r["lc_d"]), "id": int(idm.group(1)),
                              "suffix": "abcde".index(idm.group(2).lower()) + 1 if idm.group(2) else 0,
                              "manned": 1 if "unmanned" in r.get("lc_man", "").lower() else 0, "class": cls,
                              "awEn": 1 if "yes" in aw else 0, "awType": 1 if "time" in aw else 0})
    # Track conditions: a type and each "<distance> From <signal>" it gives.
    tc = r.get("tc_type", "").strip().lower()
    if tc and tc != "-":
        code = TRACK_CONDITIONS.get(tc)
        starts = [int(x) for x in re.findall(r"(\d+)\s*From", r.get("tc_start", ""))]
        lens = [int(x) for x in re.findall(r"\d+", r.get("tc_len", ""))]
        if code is None or not starts:
            notes.append(f"{where}: track condition {r.get('tc_type')!r} at {r.get('tc_start')!r} not read: not taken")
        else:
            for i, sd in enumerate(starts):
                out["track_conditions"].append({"type": code, "sd": sd, "ln": lens[i] if i < len(lens) else 0})
    return out


def signal_key(name):
    """A signal as both tables can be matched on: its numbers and ID / D, without
    the stations it is written with or its LC:
    'S6 TLPR/50D LPI' and 'S6/50D' -> 'S6/50D'; 'S30 D' -> 'S30D';
    'S7/104D OF LC.22' -> 'S7/104D'; 'S2 ID (501)' and 'S2 ID TLPR' -> 'S2ID'."""
    name = re.sub(r"\([^)]*\)", " ", (name or "").upper())
    name = re.sub(r"\bOF\s+LC\.?\s*\w+", " ", name)
    parts = []
    for part in name.split("/"):
        toks = [t for t in re.split(r"[\s.\-]+", part) if t and (re.search(r"\d", t) or t in ("ID", "D"))]
        parts.append("".join(toks))
    return "/".join(p for p in parts if p)


def apply_profile(sec, kav, notes):
    """Each track profile row onto its route (same entry, same exit signal);
    a route with the same tags (a calling-on route over the main one) takes it too."""
    if not kav or not sec.profile:
        return
    src = sec.profile_src
    done = set()
    for r in sec.profile:
        where = f"{r.get('src', src)} p{r['page']} #{r['sno']} {r['entry']} → {r['exit']}"
        cands = [x for x in kav["routes"] if signal_key(x["entry"]) == signal_key(r["entry"])
                 and (signal_key(x["exit"]) == signal_key(r["exit"])
                      or (r["exit"].strip() in ("-", "") and "(" in x["exit"]))]
        if not cands:
            # Into the next station: the ToC names its exit by the border tag
            # ("R-673 (520)"), the profile by the signal ("S30 D (CTF)").
            into = [x for x in kav["routes"] if signal_key(x["entry"]) == signal_key(r["entry"])
                    and x.get("next_stn") is not None and "(" in x["exit"]]
            cands = into if len(into) == 1 else []
        if len(cands) > 1:
            line = re.sub(r"\s+", " ", r.get("line", "")).strip().lower()
            cands = [x for x in cands if x["name"].lower().endswith(line)] or cands
        if not cands:
            notes.append(f"{where}: no route of the Table of Control goes from {r['entry']} to {r['exit']}: not used")
            continue
        for route in cands:
            length = sum(t["dist"] for t in route["tags"])
            rows = profile_rows(r, length, where, notes)
            route.update(rows)
            done.add(route["name"])
            for other in kav["routes"]:
                if other["name"] not in done and [t["tag"] for t in other["tags"]] == [t["tag"] for t in route["tags"]]:
                    other.update({k: [dict(x) for x in v] for k, v in rows.items()})
                    done.add(other["name"])
    nolen = sorted({r["tc_type"] for r in sec.profile if r.get("tc_type", "-").strip() not in ("", "-")
                    and not re.search(r"\d", r.get("tc_len", ""))})
    if nolen:
        notes.append(f"{src}: {', '.join(nolen)} given without a length: sent as 0 m")
    missing = [x["name"] for x in kav["routes"] if x["name"] not in done]
    if missing:
        notes.append(f"{src}: no track profile row for {len(missing)} route(s): {', '.join(missing)}")


# ---- the TSR ROUTE TABLE ------------------------------------------------------
W_NS = "{http://schemas.openxmlformats.org/wordprocessingml/2006/main}"


def docx_rows(path):
    """Every table row of a .docx, as its cells' text (a .docx is a zip of XML)."""
    import xml.etree.ElementTree as ET
    import zipfile
    root = ET.fromstring(zipfile.ZipFile(path).read("word/document.xml"))
    out = []
    for tbl in root.iter(W_NS + "tbl"):
        for tr in tbl.findall(W_NS + "tr"):
            out.append([re.sub(r"\s+", " ", " ".join("".join(t.text or "" for t in p.iter(W_NS + "t"))
                                                     for p in tc.findall(W_NS + "p"))).strip()
                        for tc in tr.findall(W_NS + "tc")])
    return out


def read_tsr_table(path):
    """The TSR route table's rows: S.No, entry, exits, the first OHE pole, the
    start tag, the TSR route id, the first pole's distance from the start tag,
    and the poles after it as (span from the pole before, pole)."""
    rows = []
    for cells in docx_rows(path):
        if len(cells) < 8 or not cells[0].isdigit():
            continue
        m = re.search(r"R-?\s*0*(\d+)", cells[4])
        try:
            start = float(cells[6])
        except ValueError:
            start = None
        rows.append({"sno": int(cells[0]), "entry": cells[1], "exits": [x.strip() for x in cells[2].split(",") if x.strip()],
                     "first_pole": cells[3].strip(), "start_tag": int(m.group(1)) if m else None,
                     "id": int(cells[5]) if cells[5].isdigit() else None, "first_dist": start,
                     "spans": [(float(d), p) for d, p in re.findall(r"\(\s*([\d.]+)\s*-\s*(\d+/\d+)\s*\)", cells[7])]})
    return rows


def tsr_routes_of(sec, kav, notes):
    """The "tsr_routes" of the kavach block: each TSR route over the routes it
    runs (entry, then each exit the table lists, route by route), its poles at
    their distances from the start tag (the spans added up, 0.01 m)."""
    if not kav or not sec.tsr:
        return
    src = sec.tsr_src
    out, ids = [], Counter(r["id"] for r in sec.tsr)
    snos = [r["sno"] for r in sec.tsr]
    gaps = [n for n in range(min(snos), max(snos) + 1) if n not in snos] if snos else []
    if gaps:
        notes.append(f"{src}: no rows {', '.join(map(str, gaps))} (S.No goes from {gaps[0] - 1} to {gaps[-1] + 1})")
    for r in sec.tsr:
        where = f"{src} #{r['sno']} {r['entry']} → {', '.join(r['exits'])}"
        if r["id"] is None or r["first_dist"] is None or not r["first_pole"]:
            notes.append(f"{where}: its TSR route id, first pole or start distance not read: not taken")
            continue
        if ids[r["id"]] > 1:
            notes.append(f"{where}: TSR route id {r['id']} is given to {ids[r['id']]} rows")
        chain, at = [], r["entry"]
        for ex in r["exits"]:
            nxt = [x for x in kav["routes"] if signal_key(x["entry"]) == signal_key(at)
                   and signal_key(x["exit"]) == signal_key(ex)]
            if not nxt:   # into the next station, its exit known by the border tag
                into = [x for x in kav["routes"] if signal_key(x["entry"]) == signal_key(at)
                        and x.get("next_stn") is not None and "(" in x["exit"]]
                nxt = into if len(into) == 1 and ex == r["exits"][-1] else []
            if not nxt:
                break
            chain.append(nxt[0]["name"])
            at = ex
        if len(chain) < len(r["exits"]):
            notes.append(f"{where}: no route from {at} to {r['exits'][len(chain)]}"
                         + (f": the TSR route covers {', '.join(chain)} only" if chain else ": not taken"))
            if not chain:
                continue
        first = next(x for x in kav["routes"] if x["name"] == chain[0])
        if r["start_tag"] is not None and first["tags"][0]["tag"] != r["start_tag"]:
            notes.append(f"{where}: its start tag R-{r['start_tag']} is not {chain[0]}'s first tag R-{first['tags'][0]['tag']}")
        poles, d = [{"pole": r["first_pole"], "dist": r["first_dist"]}], r["first_dist"]
        for span, pole in r["spans"]:
            d = round(d + span, 2)
            poles.append({"pole": pole, "dist": d})
        out.append({"id": r["id"], "entry": r["entry"], "routes": chain, "poles": poles})
    if out:
        kav["tsr_routes"] = out


# ---- a station folder, and several together ----------------------------------
def sheets_in(folder):
    out = {}
    for name in sorted(os.listdir(folder)):
        k = kind_of(name)
        if k and name.lower().endswith((".pdf", ".html")):
            out.setdefault(k, []).append(os.path.join(folder, name))
    return out


class Section:
    """What one station / IB folder's sheets hold."""

    def __init__(self, folder):
        self.folder = folder
        self.sources, self.notes = [], []
        self.tags, self.rows, self.borders = [], [], []
        self.toc_src, self.toc_station = "", None
        sheets = sheets_in(folder)
        for path in sheets.get("tags", []):
            src = os.path.basename(path)
            self.sources.append(src)
            t = read_tag_sheet(read_pages(path), first_page(path))
            self.notes += check_tags(t, src)
            self.tags += t
        self.tsr, self.tsr_src = [], ""
        for path in sorted(os.path.join(folder, n) for n in os.listdir(folder)
                           if kind_of(n) == "tsr" and n.lower().endswith(".docx")):
            self.tsr_src = os.path.basename(path)
            self.sources.append(self.tsr_src)
            self.tsr += read_tsr_table(path)
        self.profile, self.profile_src = [], ""
        for path in sheets.get("profile", []):
            self.profile_src = os.path.basename(path)
            self.sources.append(self.profile_src)
            for r in read_profile(read_pages(path), first_page(path)):
                r["src"] = self.profile_src
                self.profile.append(r)
        for path in sheets.get("toc", []):
            self.toc_src = os.path.basename(path)
            self.sources.append(self.toc_src)
            pages = read_pages(path)
            for r in read_toc(pages, first_page(path)):
                r["src"] = self.toc_src
                self.rows.append(r)
            for b in read_borders(pages, first_page(path)):
                b["src"] = self.toc_src
                self.borders.append(b)
            self.toc_station = self.toc_station or read_station_id(pages)
        ids = Counter()
        self.locs = []
        for t in self.tags:
            d = decode(t.px, t.py)
            if d["crcOk"] and d["type"] == 9:
                ids[d["stnNom"]] += 1
                self.locs.append(d["absLoc"])
        self.station_id = ids.most_common(1)[0][0] if ids else (self.toc_station or 0)
        if self.toc_station is not None and ids and self.toc_station != self.station_id:
            self.notes.append(f"{self.toc_src}: Station ID {self.toc_station}; most of the tags say {self.station_id}")

    def tag_loc(self, number):
        """number -> abs_loc of its main tag here (CRC-30 holding), or None."""
        for t in self.tags:
            if t.number == number and not t.is_dup:
                d = decode(t.px, t.py)
                return d["absLoc"] if d["crcOk"] else None
        return None


class Pack:
    """Every section read together: a route that runs into the next section
    finds its tags and signals there."""

    def __init__(self, sections):
        self.sections = sections

    def tag_loc(self, number, near=None, prefer=None):
        """abs_loc of main tag `number`: in `prefer` first, else the section's copy nearest `near`."""
        if prefer is not None:
            v = prefer.tag_loc(number)
            if v is not None:
                return v
        found = [v for s in self.sections for v in [s.tag_loc(number)] if v is not None]
        if not found:
            return None
        return min(found, key=lambda v: abs(v - near)) if near is not None else found[0]

    def dup_behind(self, number, prefer):
        """True when tag `number`'s duplicate lies 4 m back from its main tag (dup_dir 1,
        "Reverse(-)", as station 527 sends it); False when ahead or unknown."""
        for s in [prefer] + [x for x in self.sections if x is not prefer]:
            m = d = None
            for t in s.tags:
                if t.number == number:
                    v = decode(t.px, t.py)
                    if v["crcOk"]:
                        if t.is_dup:
                            d = v["absLoc"]
                        else:
                            m = v["absLoc"]
            if m is not None and d is not None:
                return d < m
        return False

    def section_of(self, station_id):
        return next((s for s in self.sections if s.station_id == station_id), None)

    def tags_at(self, loc):
        return [(s, t.number) for s in self.sections for t in s.tags
                if not t.is_dup and s.tag_loc(t.number) == loc]


def kavach_of(sec, pack):
    """The "kavach" block from the Table of Control: (block or None, notes, tag numbers it needs)."""
    if not sec.rows:
        return None, [], set()
    notes, src = [], sec.toc_src
    foot = {}                      # this station's signals: name -> foot tag (its rows' entry foot tag)
    for r in sec.rows:
        m = re.search(r"R-?\s*0*(\d+)", r.get("foot", ""))
        if r["entry"] and m:
            f = int(m.group(1))
            if foot.setdefault(r["entry"], f) != f:
                notes.append(f"{r.get('src', src)} p{r['page']} #{r['sno']}: {r['entry']}'s foot tag is R-{f} here, "
                             f"R-{foot[r['entry']]} in an earlier row")
    borders = {b["border_tag"]: b for b in sec.borders}
    for b in sec.borders:
        own = [n for n, f in foot.items() if f == b["border_tag"]]
        if own:
            notes.append(f"{b.get('src', src)} p{b['page']}: the Next Station Border RFID given for {b['lss'] or 'its last stop signal'} "
                         f"(station {b['next_stn']}) is R-{b['border_tag']}, the foot tag of this station's {own[0]}")
    signals, routes, needed, names = {}, [], set(), Counter()
    for r in sec.rows:
        where = f"{r.get('src', src)} p{r['page']} #{r['sno']} {r['entry']} → {r['exit']}"
        fmt, ls = links(r.get("seq", ""))
        if not ls:
            notes.append(f"{where}: no RFID sequence: not a route")
            continue
        if fmt == "dist-first":
            seq = [(t, d) for t, d in ls]
            if seq[0][1] != 0:
                notes.append(f"{where}: the RFID sequence starts {seq[0][1]} m from R-{seq[0][0]}, not at 0")
        else:
            seq = [(ls[0][0], 0)] + [(ls[i][0], ls[i - 1][1]) for i in range(1, len(ls))]
        last_tag, last_d = (seq[-1][0], 0) if fmt == "dist-first" else (ls[-1][0], ls[-1][1])
        stated = int(r["distance"]) if re.fullmatch(r"\d+", r.get("distance", "")) else None
        total = sum(d for _, d in seq) + (last_d if fmt == "tag-first" else 0)
        # Which way: the tags' locations (rising: Nominal; the first and the
        # farthest tag the sheets place), else the line.
        placed = []
        for t, _ in seq:
            v = pack.tag_loc(t, near=placed[0] if placed else None, prefer=sec)
            if v is not None:
                placed.append(v)
        here = pack.tag_loc(seq[0][0], prefer=sec)
        if len(placed) > 1 and placed[-1] != placed[0]:
            pkt_dir = 1 if placed[-1] > placed[0] else 2
        else:
            pkt_dir = 2 if re.match(r"(?i)up", r.get("line", "")) else 1
            notes.append(f"{where}: the tags' locations do not give the direction: {'Reverse' if pkt_dir == 2 else 'Nominal'} "
                         f"from the line ({r.get('line', '')})")
        exit_name, next_stn = r["exit"], 0
        border = None
        if fmt == "tag-first":
            # The last distance runs to the exit signal's foot tag: this station's
            # signal, else the next station's border tag, else whatever tag lies there.
            want = None if here is None else pack.tag_loc(last_tag, near=here, prefer=sec)
            want = None if want is None else want + (last_d if pkt_dir == 1 else -last_d)
            end = foot.get(exit_name) if exit_name not in ("", "-") else None
            if end is None:
                b = next((b for b in sec.borders if b["border_loc"] == want), None)
                end = b["border_tag"] if b else None
            if end is None and want is not None:
                at = pack.tags_at(want)
                end = at[0][1] if at else None
            if end is None:
                notes.append(f"{where}: the RFID sequence's last {last_d} m from R-{last_tag} ends on no tag "
                             f"this station or its neighbours' sheets name: not a route")
                continue
            seq.append((end, last_d))
        if exit_name in ("", "-"):
            border = borders.get(seq[-1][0])
            if not border:
                stop = f"R-{seq[-1][0]}"
                sofar = sum(d for _, d in seq)
                notes.append(f"{where} ({r.get('asp_entry', '')} / {r.get('asp_exit', '')}, {r.get('os_speed', '')} km/h): "
                             f"no exit signal and its RFID sequence stops at {stop}, {sofar} m"
                             + (f" of the {stated} m given" if stated is not None else "") + ": not a route")
                continue
            nsec = pack.section_of(border["next_stn"]) if border["next_stn"] else None
            other = None
            if nsec:
                other = next((x["entry"] for x in nsec.rows
                              if re.sub(r"\D", "", x.get("foot", "")).lstrip("0") == str(seq[-1][0])), None)
            exit_name = f"{other or 'R-' + str(seq[-1][0])} ({border['next_stn'] or 'next station'})"
        # A block or auto route that ends on the "Last Stop Signal" table's
        # border tag: the loco is handed over to the next station there.
        if border is None and SECTION_TYPES.get(r.get("section", "").strip().lower()) in (1, 2):
            border = borders.get(seq[-1][0])
        if border:
            next_stn = border["next_stn"] or 0
        if stated is not None and stated != sum(d for _, d in seq):
            notes.append(f"{where}: the linking distances add up to {sum(d for _, d in seq)} m; the Distance column says {stated} m")
        # The MA sections against the aspect of the exit signal.
        said = set()
        for ex, n in r.get("subs", []):
            ex = ex.strip()
            if ex in ASPECT_SECTIONS and n.isdigit() and int(n) != ASPECT_SECTIONS[ex] and (ex, n) not in said:
                said.add((ex, n))
                notes.append(f"{where}: {n} MA sections with {exit_name} at {ex}; Station KAVACH gives "
                             f"{ASPECT_SECTIONS[ex]} unless the file says otherwise")
        sect = SECTION_TYPES.get(r.get("section", "").strip().lower())
        if sect is None:
            notes.append(f"{where}: section type {r.get('section')!r} is not one Annexure-I names: Station Section (0) taken")
            sect = 0
        # OS speed: speed6, 5 km/h steps (session 456: 455 wrote km/h). "MPS 15": 15;
        # "10 in Day/ 08 in Night": the day's 10 (the night's 8 is no 5 km/h step).
        speeds = [int(v) for v in re.findall(r"\d+", r.get("os_speed", "")) if int(v) % 5 == 0 and 0 < int(v) <= 250]
        calling_on = "calling" in r.get("sigtype", "").lower()
        tcs = [x for x in names_list(r.get("tcs", "")) if x.lower() != "occupied"]
        if "occupied" in r.get("tcs", "").lower():
            tcs = []            # a calling-on route wants its track circuit occupied: not a "must be clear"
        route = {"name": "", "entry": r["entry"], "exit": exit_name, "pkt_dir": pkt_dir, "section_type": sect,
                 "authority": 1 if calling_on else 2, "os_speed": speeds[-1] // 5 if speeds else 0,
                 "points_normal": names_list(r.get("points_n", "")),
                 "points_reverse": names_list(r.get("points_r", "")),
                 "track_circuits": tcs,
                 "tins": names_list(r.get("tins", "")) + names_list(r.get("tins_overlap", "")),
                 "dist_dup_tag": 4, "tin": 0,
                 "tags": [dict({"dist": d, "tag": t}, **({"dup_dir": 1} if pack.dup_behind(t, sec) else {}))
                          for t, d in seq],
                 "adj_tins": [], "ssp": [], "gradient": [], "lc": [], "turnouts": [], "track_conditions": [], "tsr": []}
        if next_stn:
            route["next_stn"] = next_stn
        if border and border.get("reset"):
            route["loc_reset"] = border["reset"]
        # The route's TIN: its entry foot tag's, the way it runs.
        for t in sec.tags:
            if t.number == seq[0][0] and not t.is_dup:
                d = decode(t.px, t.py)
                if d["crcOk"] and d["type"] == 9:
                    route["tin"] = d["tinNom"] if pkt_dir == 1 else d["tinRev"]
        line = re.sub(r"\s+", " ", r.get("line", "")).strip(" -")
        base = f"{r['entry']}-{exit_name}" + (f" {line}" if line else "")
        names[base] += 1
        route["name"] = base
        route["_line"] = line
        route["_sno"] = r["sno"]
        routes.append(route)
        signals.setdefault(r["entry"], seq[0][0])
        signals.setdefault(exit_name, seq[-1][0])
        needed.update(t for t, _ in seq)
    for rt in routes:                       # the same entry, exit and line twice: the S.No tells them apart
        if names[rt["name"]] > 1:
            rt["name"] += f" #{rt['_sno']}"
    for name, f in signals.items():
        if name in foot and foot[name] != f:
            notes.append(f"{src}: signal {name}'s foot tag is R-{foot[name]} as an entry, R-{f} as an exit")
    block = {"station_id": sec.station_id, "stn_version": 0,
             "signals": [{"name": n, "foot_tag": foot.get(n, f), "sig_info": 0, "aspect": 1} for n, f in signals.items()],
             "routes": routes}
    return block, notes, needed


def build_layout(sec, pack):
    """(layout dict, notes) for one section of the pack."""
    notes = list(sec.notes)
    if not sec.tags:
        return None, [f"{sec.folder}: no RFID tag data sheet"]
    kav, knotes, needed = kavach_of(sec, pack)
    apply_profile(sec, kav, knotes)
    tsr_routes_of(sec, kav, knotes)
    notes += knotes
    tags = list(sec.tags)
    names = Counter(t.name for t in tags)
    for n, c in names.items():
        if c > 1:
            notes.append(f"tag {n} is printed {c} times")
    # A route here that runs onto another section's tags: those tags, from that section's sheet.
    own = {t.number for t in sec.tags}
    copied = []
    for number in sorted(needed - own):
        near = sec.locs[len(sec.locs) // 2] if sec.locs else None
        best = None
        for other in pack.sections:
            if other is sec:
                continue
            v = other.tag_loc(number)
            if v is not None and (best is None or (near is not None and abs(v - near) < abs(best[1] - near))):
                best = (other, v)
        if best:
            tags += [t for t in best[0].tags if t.number == number]
            copied.append(f"R-{number} ({best[0].station_id})")
    if copied:
        notes.append(f"tags copied from the next sections' tag data, for the routes that end on them: {', '.join(copied)}")
    layout = {
        "format": FORMAT,
        "version": 2 if kav else 1,
        "comment": (f"Read from the KAVACH sheets by scripts/kavach_sheets.py (sessions 454-455): {', '.join(sec.sources)}. "
                    f"Station {sec.station_id}: the station ID most of its normal tags carry. Its location "
                    f"{(min(sec.locs) + max(sec.locs)) // 2 if sec.locs else 0} is not on the sheets: the middle of its "
                    f"tags, for the drawing."),
        "tags": [{"name": t.name, "page_x": t.px, "page_y": t.py} for t in tags],
        "signals": [], "points": [], "lines": [],
        "stations": [{"station_id": sec.station_id,
                      "location": (min(sec.locs) + max(sec.locs)) // 2 if sec.locs else 0}],
        "texts": [], "other_sheets": [],
    }
    if kav:
        # The lines: each tag on the line most of the routes through it name, in location order.
        votes = {}
        for rt in kav["routes"]:
            for x in rt["tags"]:
                votes.setdefault(x["tag"], Counter())[rt["_line"] or "(line not given)"] += 1
        loc_of = {}
        for t in tags:
            if not t.is_dup:
                d = decode(t.px, t.py)
                if d["crcOk"]:
                    loc_of[t.number] = d["absLoc"]
        lines = {}
        for number, v in votes.items():
            lines.setdefault(v.most_common(1)[0][0], []).append(number)
        tagnames = {t.name for t in tags}
        for name, numbers in lines.items():
            order = sorted(numbers, key=lambda n: loc_of.get(n, 0))
            names_in = []
            for n in order:
                names_in += [x for x in (str(n), f"{n}D") if x in tagnames]
            layout["lines"].append({"name": name, "line": name, "tags": names_in})
        for g in kav["signals"]:
            if str(g["foot_tag"]) in tagnames:
                m = re.search(r"\((\d+)\)$", g["name"])
                layout["signals"].append({"foot_tag": str(g["foot_tag"]), "signal": g["name"], "sig_id": 0,
                                          "station_id": int(m.group(1)) if m else sec.station_id})
        for rt in kav["routes"]:
            del rt["_line"], rt["_sno"]
        layout["kavach"] = kav
    layout["sheet_notes"] = notes
    return layout, notes


def read_pack(folders):
    return Pack([Section(f) for f in folders])


def import_pack(folders):
    """{folder: (layout, notes)} for folders read together."""
    pack = read_pack(folders)
    return {s.folder: build_layout(s, pack) for s in pack.sections}


def station_layout(folder):
    """(layout dict, notes) for one station / IB folder read on its own."""
    return import_pack([folder])[folder]


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
    done = import_pack(args)
    for folder in args:
        layout, notes = done[folder]
        if layout is None:
            print("\n".join(notes))
            rc = 1
            continue
        sid = layout["stations"][0]["station_id"]
        out = os.path.join(outdir or folder, f"station_{sid}.json")
        with open(out, "w", encoding="utf-8") as fh:
            fh.write(layout_text(layout))
        routes = len(layout.get("kavach", {}).get("routes", []))
        print(f"{out}: {len(layout['tags'])} tags, {routes} routes, {len(notes)} sheet notes")
        for n in notes:
            print("   ", n)
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
