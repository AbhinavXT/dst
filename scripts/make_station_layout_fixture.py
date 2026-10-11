#!/usr/bin/env python3
"""Write tests/fixtures/station_layout_synthetic.xlsx (session 208).

A made-up station file in the layout of the old Python tool's
config/station/*.xlsx: the same seven sheets, deflated entries and a
shared-strings table, as Excel/openpyxl write them -- so the C++ reader's
inflate and shared strings are tested in CI, where the real station files
(tests/fixtures/station_layout/, git-ignored) are not. The three tags are
real tags already in the repo (tag_scenarios/); everything else is invented.
Standard library only.
"""
import sys, zipfile
from xml.sax.saxutils import escape

SHEETS = [
    ("tags", [["Tag", "Page X Page Y"],
              ["804", 'page_x="07b737026f0cc919" page_y="8ebf4ec402810781"'],
              ["2",   'page_x="07b737026fac0099" page_y="379015c80a00fa81"'],
              ["4",   'page_x="07b71902719b0119" page_y="8246aaa400010781"']]),
    ("signals", [["sig_foot_tag", "signal", "sig_id", "StationId"],
                 ["2", "S10", 4, 900], ["4", "S12", 3, 900]]),
    ("points", [["point", "location1", "line1", "location2", "line2", "tags", "StationId",
                 "conn_x_list", "conn_line_list", "conn_tin_list"],
                ["P1", 159700, "DM", 159800, "DL1", 2, 900, None, None, None],
                ["P2", 160000, "DL1", 160100, "DM", "2,4", 900, "160050", "DL2", "62"]]),
    ("lines", [["name", "line", "tags"], ["DN", "DM", "804,2,4"], ["LOOP1", "DL1", "2"]]),
    ("relaymap", [["EqipType", "EqipName", "EqipId", "EqipAttr", "BitPos", "StationId"],
                  [1, "S10", 11, 1, 1, 900, "Red"], [1, "S12", 13, 1, 7, 900]]),
    ("station", [["stationid", "location"], [900, 159900]]),
    ("texts", [["text", "location", "posy"], ["<< A & B >>", 159600, -250]]),
]

def col(i):
    s = ""
    i += 1
    while i:
        i, r = divmod(i - 1, 26)
        s = chr(65 + r) + s
    return s

def main(out):
    shared, index = [], {}
    def sid(s):
        if s not in index:
            index[s] = len(shared); shared.append(s)
        return index[s]
    sheet_xml = []
    for name, rows in SHEETS:
        x = ['<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
             '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData>']
        for r, row in enumerate(rows, 1):
            x.append('<row r="%d">' % r)
            for c, v in enumerate(row):
                if v is None: continue
                ref = "%s%d" % (col(c), r)
                if isinstance(v, str): x.append('<c r="%s" t="s"><v>%d</v></c>' % (ref, sid(v)))
                else: x.append('<c r="%s"><v>%s</v></c>' % (ref, v))
            x.append('</row>')
        x.append('</sheetData></worksheet>')
        sheet_xml.append("".join(x))
    ns = 'xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"'
    rel = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        ov = "".join('<Override PartName="/xl/worksheets/sheet%d.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>' % (i + 1) for i in range(len(SHEETS)))
        z.writestr("[Content_Types].xml", '<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/sharedStrings.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml"/>' + ov + '</Types>')
        z.writestr("_rels/.rels", '<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="%s/officeDocument" Target="xl/workbook.xml"/></Relationships>' % rel)
        z.writestr("xl/workbook.xml", '<?xml version="1.0" encoding="UTF-8"?><workbook %s xmlns:r="%s"><sheets>%s</sheets></workbook>' % (ns, rel, "".join('<sheet name="%s" sheetId="%d" r:id="rId%d"/>' % (n, i + 1, i + 1) for i, (n, _) in enumerate(SHEETS))))
        z.writestr("xl/_rels/workbook.xml.rels", '<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">%s<Relationship Id="rId99" Type="%s/sharedStrings" Target="sharedStrings.xml"/></Relationships>' % ("".join('<Relationship Id="rId%d" Type="%s/worksheet" Target="worksheets/sheet%d.xml"/>' % (i + 1, rel, i + 1) for i in range(len(SHEETS))), rel))
        z.writestr("xl/sharedStrings.xml", '<?xml version="1.0" encoding="UTF-8"?><sst %s count="%d" uniqueCount="%d">%s</sst>' % (ns, len(shared), len(shared), "".join("<si><t>%s</t></si>" % escape(s) for s in shared)))
        for i, x in enumerate(sheet_xml):
            z.writestr("xl/worksheets/sheet%d.xml" % (i + 1), x)

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "tests/fixtures/station_layout_synthetic.xlsx")
