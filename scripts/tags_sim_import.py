#!/usr/bin/env python3
"""Convert tags_sim route spreadsheets (.xlsx) into DLConsole tag routes.

    python3 scripts/tags_sim_import.py ROUTE.xlsx [MORE.xlsx ...]

Writes ROUTE.tagroute.xml next to each input. Only the standard library is
used (an .xlsx is a zip of XML), so it runs wherever tags_sim ran.

tags_sim's sheets: `tags` (Tag, `page_x="..." page_y="..."`) and `signals`
(sig_foot_tag, signal, sig_id). Older tags_sim saves have one sheet of tags
and no signals; those are read too. The direction is left unset (dir="0"):
tags_sim guessed it from the tags' locations, and DLConsole asks for it.
"""
import re
import sys
import zipfile
import xml.etree.ElementTree as ET
from xml.sax.saxutils import quoteattr

NS = {"m": "http://schemas.openxmlformats.org/spreadsheetml/2006/main",
      "r": "http://schemas.openxmlformats.org/officeDocument/2006/relationships"}
PAGES = re.compile(r'page_x="([0-9a-fA-F]{1,16})"\s*page_y="([0-9a-fA-F]{1,16})"')


def col_index(ref):
    n = 0
    for ch in re.match(r"[A-Z]+", ref).group(0):
        n = n * 26 + ord(ch) - 64
    return n - 1


def sheets(path):
    """{sheet name: [[cell text, ...], ...]} for every sheet."""
    z = zipfile.ZipFile(path)
    shared = []
    if "xl/sharedStrings.xml" in z.namelist():
        for si in ET.fromstring(z.read("xl/sharedStrings.xml")).findall("m:si", NS):
            shared.append("".join(t.text or "" for t in si.iter("{%s}t" % NS["m"])))
    rels = {r.get("Id"): r.get("Target") for r in ET.fromstring(z.read("xl/_rels/workbook.xml.rels"))}
    out = {}
    for s in ET.fromstring(z.read("xl/workbook.xml")).find("m:sheets", NS):
        target = rels[s.get("{%s}id" % NS["r"])].lstrip("/")
        target = target if target.startswith("xl/") else "xl/" + target
        rows = []
        for row in ET.fromstring(z.read(target)).iter("{%s}row" % NS["m"]):
            cells = []
            for c in row.findall("m:c", NS):
                v = c.find("m:v", NS)
                if c.get("t") == "s" and v is not None:
                    text = shared[int(v.text)]
                elif c.get("t") == "inlineStr":
                    text = "".join(t.text or "" for t in c.iter("{%s}t" % NS["m"]))
                else:
                    text = v.text if v is not None else ""
                i = col_index(c.get("r"))
                cells += [""] * (i + 1 - len(cells))
                cells[i] = text
            rows.append(cells)
        out[s.get("name")] = rows
    return out


def number(text):
    """'148.0' -> '148' (numbers come back from Calc/pandas as floats)."""
    return text[:-2] if text.endswith(".0") else text


def convert(path):
    book = sheets(path)
    if "tags" not in book:
        # Older tags_sim saves: one sheet (Sheet1) of tags, no signals, the
        # header spelt more than one way: take the first sheet that holds tags.
        first = next((rows for rows in book.values()
                      if any(len(r) > 1 and PAGES.search(r[1]) for r in rows)), None)
        if first is None:
            raise ValueError("no sheet of page_x / page_y tags")
        book = {"tags": first}
    name = re.sub(r"\.xlsx$", "", path.replace("\\", "/").split("/")[-1], flags=re.I)
    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             "<tag_route name=%s dir=\"0\">" % quoteattr(name)]
    tags = 0
    for row in book["tags"][1:]:
        m = PAGES.search(row[1]) if len(row) > 1 else None
        if not m:
            continue
        lines.append("    <tag name=%s page_x=\"%s\" page_y=\"%s\"/>"
                     % (quoteattr(number(row[0])), m.group(1).zfill(16).lower(), m.group(2).zfill(16).lower()))
        tags += 1
    for row in book.get("signals", [])[1:]:
        row = row + [""] * (3 - len(row))
        if not any(row[:3]):
            continue
        lines.append("    <signal foot_tag=%s name=%s sig_id=%s/>"
                     % (quoteattr(number(row[0])), quoteattr(row[1]), quoteattr(number(row[2]))))
    lines.append("</tag_route>")
    out = re.sub(r"\.xlsx$", "", path, flags=re.I) + ".tagroute.xml"
    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return out, tags


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    failed = 0
    for p in sys.argv[1:]:
        try:
            out, n = convert(p)
            print("%s: %d tags -> %s" % (p, n, out))
        except Exception as e:  # keep going through a folder of files
            failed += 1
            print("%s: not converted (%s)" % (p, e), file=sys.stderr)
    sys.exit(1 if failed else 0)
