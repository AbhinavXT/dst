#!/usr/bin/env python3
"""Turn RDSO SIF 0533 (Functional Test Format at System Level) into
DLConsole's test case catalog (session 231).

    python3 scripts/sif_catalog.py SIF.pdf OUT.json [--cache PAGES.json]

--cache keeps what was read from the PDF (about 6 minutes for its 893 pages)
for the next run. Needs pdfplumber (pip install pdfplumber): this runs once per SIF version,
on a developer machine, not in the build or the gate. The catalog it writes
is built into DLConsole (test_catalog.qrc) and edited there.

HOW THE PDF IS READ
  Word by word, not cell by cell. The SIF's tables nest smaller tables in
  their cells (mode matrices, comparator tables), and a cell extractor
  returns their text twice, once per overlapping cell. Every word on a page
  is used exactly once:
    - the page's column edges come from its header row ("Test ID | Test
      Scenario | Input Specification | ..." or "Operation ID | Transmission
      of packets ... | Reception ... | Expected result | Pass/Fail"); a page
      with no header row reuses the last page's;
    - each word goes to the column it starts in;
    - a clause starts at an id ("4.93.", "31.6.1.") in the id column, and
      runs until the next one, across pages.
  Sections 31-32 (radio packets) have a case row ("31.6. Testing of ..."),
  Purpose / Methodology / Setup rows, then operations ("31.6.1."). The
  case keeps its title, purpose and methodology; each operation is a
  clause of its own under it.

  Lines inside a cell are joined into paragraphs: a new line starts only at
  a list marker ("1.", "a)", "ii.", "#"). A word broken by a hyphen across
  lines ("KA-" "VACH") is joined.
"""
import json
import re
import sys
import unicodedata

ID_RE = re.compile(r'^(\d{1,2})\.(\d{1,3})\.(?:(\d{1,3})\.)?$')
SECTION_RE = re.compile(r'^(\d{1,2})\.$')
LIST_RE = re.compile(r'^(\d{1,2}[.)]|\(?[a-z][.)]|\(?(i|ii|iii|iv|v|vi|vii|viii|ix|x|xi|xii)[.)]|[•●▪\-–]\s|#\s|Step\s*\d)', re.I)
LABEL_TAILS = {'e', 'ogy', 'gy', 'ology', 'ion', 'configurat', 'configuration', 'configura', 'configu', 'ration',
               'tion', 'on', 'id', 'on id'}
FIRST_PAGE = 20          # page 20 (printed 19) holds clause 1.1
BANNER = ('system', 'level', 'test', 'cases', '(rdso)')


def load_pages(pdf_path, cache):
    if cache:
        try:
            return json.load(open(cache))
        except (OSError, ValueError):
            pass
    import pdfplumber
    out = []
    with pdfplumber.open(pdf_path) as pdf:
        for i, p in enumerate(pdf.pages):
            tabs = []
            for t in p.find_tables():
                tabs.append({"bbox": list(t.bbox), "rows": t.extract(),
                             "cellboxes": [[None if c is None else list(c) for c in r.cells] for r in t.rows]})
            words = p.extract_words()
            out.append({"page": i + 1, "w": p.width, "h": p.height, "tables": tabs,
                        "images": [[im["x0"], im["top"], im["x1"], im["bottom"]] for im in p.images],
                        "words": [[w["text"], round(w["x0"], 1), round(w["top"], 1), round(w["x1"], 1),
                                   round(w["bottom"], 1)] for w in words]})
    if cache:
        json.dump(out, open(cache, "w"))
    return out


# Symbol / Wingdings code points the PDF's fonts use, as what they draw.
SYMBOLS = {'\uf0b7': '\u2022', '\uf0a7': '\u25aa', '\uf0d8': '\u27a2', '\uf0fc': '\u2713', '\uf0fd': '\u2612',
           '\uf0fe': '\u2611', '\uf0af': '\u2193', '\uf0ad': '\u2191', '\uf0ae': '\u2192', '\uf0ac': '\u2190',
           '\uf044': '\u0394', '\uf020': ' ', '\uf0b3': '\u2265', '\uf0a3': '\u2264', '\uf0b1': '\u00b1'}


def clean(t):
    t = ''.join(SYMBOLS.get(ch, ch) for ch in t)
    # mathematical italic letters (an equation's) as plain letters
    t = ''.join(unicodedata.normalize('NFKC', ch) if 0x1D400 <= ord(ch) <= 0x1D7FF else ch for ch in t)
    return t


VOCAB = set()          # every word the document prints whole, lower case
HYPHENATED = set()     # and the ones it prints with a hyphen inside ("s-kavach")


def para(s):
    return reflow([(0, 0, l) for l in (s or '').split('\n')])


def norm(s):
    return re.sub(r'\s+', ' ', (s or '').replace('\n', ' ')).strip()


def header_role(text, seen):
    t = norm(text).lower().replace('-', '').replace(' ', '')
    if not t:
        return None
    if t.startswith('transmission'):
        return 'expected' if 'input' in seen else 'input'
    if t.startswith('reception'):
        seen.add('reception')
        return 'expected'
    if 'inputspec' in t:
        return 'input'
    if 'testscenario' in t or t == 'scenario':
        return 'scenario'
    if t.startswith('expected'):
        # a third text column after "Reception of packets ..." is the result
        return 'result' if ('reception' in seen or 'expected' in seen) else 'expected'
    if t.startswith('ok/') or t == 'result':
        return 'observed' if t.startswith('ok/') else 'result'
    if 'observed' in t or 'pass/fail' in t or 'passfa' in t or t in ('output', '(pass/fail)'):
        return 'observed'
    if t in ('test', 'id', 'testid', 'operation', 'operationid', 'operati', 'operat', 'opera', 'ot', 'o', 't',
             'ionid', 'on', 'ration', 'id.'):
        return 'id'
    return None


def is_header_row(cells):
    s = ' '.join(norm(c) for c in cells if c)
    if re.search(r'Input Specification|Transmission of packets|Reception of packets', s) and len(s) < 260:
        return True
    return False


def page_columns(page, prev):
    """(edges, roles, header_bands) for the page. edges: sorted x; roles parallel."""
    tabs = [t for t in page["tables"] if 95 < t["bbox"][1] < 500]
    bands = []
    hdr_cells = []
    for t in tabs:
        rows = list(zip(t["rows"], t["cellboxes"]))
        first = not hdr_cells
        for i, (r, b) in enumerate(rows):
            if is_header_row(r):
                # The header can wrap onto the next row ("ID", "(Pass/Fail)", "VACH side").
                group = [(r, b)]
                if i + 1 < len(rows):
                    r2, b2 = rows[i + 1]
                    s2 = ' '.join(norm(c) for c in r2 if c)
                    if s2 and len(s2) < 80 and not ID_RE.match(s2.split()[0]):
                        group.append((r2, b2))
                for gr, gb in group:
                    ys = [bb for bb in gb if bb]
                    # min bottom: a cell spanning down the table (Pass/Fail) is not the row
                    bands.append((min(bb[1] for bb in ys) - 1, min(bb[3] for bb in ys) + 1))
                    # the first table's header gives the columns; a second
                    # table on the page (a new section) repeats it
                    if first:
                        for c, bb in zip(gr, gb):
                            if c and bb:
                                hdr_cells.append((bb[0], c))
                break
    if not hdr_cells:
        return prev[0], prev[1], bands, False
    hdr_cells.sort(key=lambda h: h[0])     # stable: a wrapped header's first line first
    merged = []          # [x, text]
    for x, c in hdr_cells:
        if merged and x - merged[-1][0] < 9:
            merged[-1][1] += ' ' + c
        else:
            merged.append([x, c])
    edges, roles, seen = [], [], set()
    for x, c in merged:
        role = header_role(c, seen)
        if role is None:
            # "O t" / "pera-" fragments of a broken "Operation ID" header
            role = 'id' if not seen else None
        if role is None:
            continue
        if role in seen and role != 'id':
            # a wrapped header line repeated ("Test Scenario" twice)
            continue
        if role == 'id' and 'id' in seen:
            continue
        seen.add(role)
        edges.append(x)
        roles.append(role)
    # labels for the two text columns, as the page prints them
    labels = {}
    for (x, c) in merged:
        t = norm(c)
        if t.startswith('Transmission') and 'input' not in labels:
            labels['input'] = t
        elif t.startswith('Transmission') or t.startswith('Reception'):
            labels['expected'] = t
    if roles[:4] == ['input', 'expected', 'result', 'observed'] and norm(merged[0][1]).startswith('Input'):
        # Section 34 prints "Input Specification | Expected Output/Values |
        # Result | OK/Not OK" over test name | procedure | expected | remark.
        roles = ['scenario', 'input', 'expected', 'observed'] + roles[4:]
    if 'id' not in roles:
        edges.insert(0, 0)
        roles.insert(0, 'id')
    edges[0] = 0
    return (edges, roles), labels, bands, True


def role_of(x, edges, roles):
    r = roles[0]
    for e, ro in zip(edges, roles):
        if x + 3 >= e:
            r = ro
    return r


def lines_of(words):
    """words: [text, x0, top, ...] -> list of (top, x0, text) lines, reading order."""
    ws = sorted(words, key=lambda w: (w[2], w[1]))
    lines = []
    for w in ws:
        if lines and abs(w[2] - lines[-1][0]) <= 3.5:
            lines[-1][2].append(w)
        else:
            lines.append([w[2], w[1], [w]])
    out = []
    for top, _, ws in lines:
        ws.sort(key=lambda w: w[1])
        txt = ''
        prev_x1 = None
        for w in ws:
            if prev_x1 is not None and w[1] - prev_x1 > 0.8:
                txt += ' '
            txt += w[0]
            prev_x1 = w[3]
        out.append((top, ws[0][1], txt))
    return out


def reflow(lines):
    """Join PDF-wrapped lines into paragraphs; keep list items on their own lines."""
    paras = []
    for _, _, t in lines:
        t = t.strip()
        if not t:
            continue
        if not paras or LIST_RE.match(t) or t.startswith('| ') or paras[-1].startswith('| '):
            paras.append(t)
            continue
        p = paras[-1]
        if re.search(r'[A-Za-z]-$', p) and re.match(r'[A-Za-z]', t):
            # "KA-" / "VACH" is one word broken at the line end; "non-" /
            # "communication" keeps its hyphen. The document's own words decide.
            a = re.split(r'[\s(\[“‘"/]', p[:-1])[-1]
            b = re.split(r'[\s,.;:)\]”’"/]', t)[0]
            whole = re.sub(r'[^0-9a-z_]', '', (a + b).lower())
            hyph = re.sub(r'[^0-9a-z_-]', '', (a + '-' + b).lower())
            paras[-1] = (p[:-1] if whole in VOCAB and hyph not in HYPHENATED else p) + t
        else:
            paras[-1] = p + ' ' + t
    return '\n'.join(paras)


def place_x(x0, x1, edges, roles):
    """Where a table's rows go: a table across a column line is the SIF's
    "see table for expected output", so it goes to the expected column."""
    if any(x0 + 8 < e < x1 - 8 for e in edges if e > 0):
        return next((e + 2 for e, ro in zip(edges, roles) if ro == 'expected'), x0 + 2)
    return x0 + 2


def grid_rows(main, words, edges, roles):
    H = main["bbox"][3] - main["bbox"][1]
    cov = {}
    for row in main["cellboxes"]:
        for bb in row:
            if bb:
                cov[round(bb[0])] = cov.get(round(bb[0]), 0) + bb[3] - bb[1]
    lines = [e for e in edges if e > 0] + [x for x, h in cov.items() if h >= 0.6 * H]
    rows = []
    for r, b in zip(main["rows"], main["cellboxes"]):
        tc = [(c, bb) for c, bb in zip(r, b) if bb and c and c.strip()]
        has_id = any(ID_RE.match(norm(c).split(' ')[0]) or SECTION_RE.match(norm(c).split(' ')[0]) for c, bb in tc)
        off = [bb for c, bb in tc if all(abs(bb[0] - e) > 8 for e in lines)]
        rows.append((tc, has_id, len(off), len(tc)))
    grid = [not has_id and n_off >= 2 for tc, has_id, n_off, n in rows]
    # a caption row over a grid ("Tin Validation: ...") has one cell
    for i, (tc, has_id, n_off, n) in enumerate(rows):
        if not grid[i] and not has_id and n_off == n >= 1 and ((i + 1 < len(rows) and grid[i + 1]) or (i > 0 and grid[i - 1])):
            grid[i] = True
    for i, (tc, has_id, n_off, n) in enumerate(rows):
        if not grid[i]:
            continue
        hmin = min(bb[3] - bb[1] for c, bb in tc)
        cells = sorted([bb for c, bb in tc if bb[3] - bb[1] <= hmin * 1.6 + 2], key=lambda bb: bb[0])
        texts = []
        for bb in cells:
            inside = [w for w in words if bb[0] - 1 <= (w[1] + w[3]) / 2 <= bb[2] + 1
                      and bb[1] - 1 <= (w[2] + w[4]) / 2 <= bb[3] + 1]
            texts.append(' '.join(w[0] for w in sorted(inside, key=lambda w: (round(w[2]), w[1]))))
            words = [w for w in words if w not in inside]
        if not any(texts):
            continue
        top = min(bb[1] for bb in cells) + 1
        words.append(['| ' + ' | '.join(texts) + ' |', place_x(cells[0][0], cells[-1][2], edges, roles),
                      top, cells[-1][2], top + 8])
    return words


def main_table(page):
    tabs = [t for t in page["tables"] if 95 < t["bbox"][1] < 500]
    return max(tabs, key=lambda t: (t["bbox"][2] - t["bbox"][0]) * (t["bbox"][3] - t["bbox"][1])) if tabs else None


def main_table_x(page):
    t = main_table(page)
    return t["bbox"][0] if t else None


def derive_columns(page):
    """Columns for a page with no header row, from the cell lines that run
    most of the main table's height. A narrow second column holding
    "Purpose" / "Methodology" is a radio case's label column."""
    t = main_table(page)
    H = t["bbox"][3] - t["bbox"][1]
    cov = {}
    for row in t["cellboxes"]:
        for bb in row:
            if bb:
                cov[round(bb[0])] = cov.get(round(bb[0]), 0) + bb[3] - bb[1]
    edges = []
    for x in sorted(x for x, h in cov.items() if h >= 0.6 * H):
        if not edges or x - edges[-1] > 8:
            edges.append(x)
    if len(edges) < 2:
        return None
    labels = any(label_name(w[0]) in ('purpose', 'methodology', 'setup') and edges[1] - 2 <= w[1] < edges[1] + 30
                 for w in page["words"])
    if labels:
        roles = ['id', 'input', 'expected', 'result', 'observed']
        edges = [0] + edges[2:]
    else:
        roles = ['id', 'scenario', 'input', 'expected', 'observed']
        edges = [0] + edges[1:]
    n = min(len(edges), len(roles))
    return (edges[:n], roles[:n])


def parse(pages):
    clauses = []            # in document order; sections are clauses of kind "section"
    cur = None              # the clause words are going to
    cols = None
    cols_x = None           # the left edge of the table the columns came from
    labels = {}
    seen_ids = {}           # id -> the page it started on
    last_section = 0

    for page in pages:
        pn = page["page"]
        if pn < FIRST_PAGE:
            continue
        tabs = page["tables"]
        iso = [t for t in tabs if t["bbox"][1] < 95]
        top_lim = max((t["bbox"][3] for t in iso), default=95)
        foot = [w[2] for w in page["words"] if w[0] == 'Representative' and w[2] > 450]
        bot_lim = min(foot) - 2 if foot else page["h"] - 40
        res = page_columns(page, (cols, labels))
        main_x = main_table_x(page)
        if res[3]:
            cols, labels = res[0], res[1]
            cols_x = main_x
        elif main_x is not None and cols_x is not None and abs(main_x - cols_x) > 4:
            # No header here, and the table is not the last header's table
            # (31.1 follows section 30 on a new layout): its own column lines.
            derived = derive_columns(page)
            if derived:
                cols, labels, cols_x = derived, {}, main_x
        bands = res[2]
        if cols is None:
            continue
        edges, roles = cols
        words = []
        for w in page["words"]:
            if w[2] <= top_lim or w[2] >= bot_lim:
                continue
            if any(a <= w[2] <= b for a, b in bands):
                continue
            words.append([clean(w[0])] + w[1:])
        # Small tables inside a cell (comparator speeds, ID lists) keep their
        # rows, as "| a | b |" lines, rather than running into the text.
        main = [t for t in tabs if 95 < t["bbox"][1] < 500]
        main = max(main, key=lambda t: (t["bbox"][2] - t["bbox"][0]) * (t["bbox"][3] - t["bbox"][1])) if main else None
        for t in tabs:
            if t is main or not (95 < t["bbox"][1] < 500):
                continue
            mx0, my0, mx1, my1 = main["bbox"]
            if not (t["bbox"][0] >= mx0 - 1 and t["bbox"][2] <= mx1 + 1 and t["bbox"][1] >= my0 - 1 and t["bbox"][3] <= my1 + 1):
                continue            # a table of its own (page 617: section 29 starts)
            if max((sum(1 for c in r if c and c.strip()) for r in t["rows"]), default=0) < 2:
                continue
            x0, y0, x1, y1 = t["bbox"]
            words = [w for w in words if not (x0 - 1 <= w[1] and w[3] <= x1 + 1 and y0 - 1 <= w[2] and w[4] <= y1 + 1)]
            for r, b in zip(t["rows"], t["cellboxes"]):
                cells = [clean(norm(c)) for c in r if c is not None]
                if not any(cells):
                    continue
                top = min(bb[1] for bb in b if bb) + 1
                words.append(['| ' + ' | '.join(cells) + ' |', place_x(x0, x1, edges, roles), top, x1, top + 8])
        # A grid drawn inside the main table (the SoS mode matrices of
        # sections 22-23: "L2 / L1 | SR | LS | OS ..."): rows whose cells do
        # not sit on the main table's column lines keep their cells too.
        if main is not None:
            words = grid_rows(main, words, edges, roles)
        # the banner over some tables
        ban = [w for w in words if w[0].lower() in BANNER]
        if len(ban) >= 5 and any(w[0].lower() == 'system' for w in ban):
            bt = min(w[2] for w in ban if w[0].lower() == 'system')
            words = [w for w in words if not (abs(w[2] - bt) < 3 and w[0].lower() in BANNER)]
        # starts: ids in the id column (or left of it)
        id_edge_end = None
        for e, r in zip(edges, roles):
            if r != 'id':
                id_edge_end = e
                break
        starts = []
        for w in words:
            if id_edge_end is not None and w[1] >= id_edge_end - 2:
                continue
            m = ID_RE.match(w[0])
            s = SECTION_RE.match(w[0])
            if m or s:
                # the first word on its line in the id column
                left = [v for v in words if abs(v[2] - w[2]) < 3 and v[1] < w[1] - 1 and v[1] < (id_edge_end or 999)]
                if left and not (m and SECTION_RE.match(left[0][0])):
                    continue
                starts.append((w[2], w[1], w[0], bool(m)))
        # one start per line ("32.7.1." twice in a row; "1. 1. KAVACH_FRS_01")
        starts.sort()
        dedup = []
        for st in starts:
            if dedup and abs(st[0] - dedup[-1][0]) < 3:
                continue
            dedup.append(st)
        cells = [b for t in tabs if 95 < t["bbox"][1] < 500 for row in t["cellboxes"] for b in row if b]

        def row_top(top, x):
            # An id is centred in its row on some pages (31.13.21): the row
            # starts at its cell's top, not at the id.
            inside = [b for b in cells if b[0] - 1 <= x + 1 <= b[2] + 1 and b[1] - 1 <= top + 1 <= b[3] + 1]
            if not inside:
                return top - 2
            b = min(inside, key=lambda b: (b[2] - b[0]) * (b[3] - b[1]))
            return min(b[1] - 1, top - 2)

        bounds = [(a, ('band', b)) for a, b in bands]
        for top, x, txt, is_clause in dedup:
            line = ' '.join(v[0] for v in sorted(words, key=lambda v: v[1]) if abs(v[2] - top) < 3)
            if not is_clause:
                no = int(txt[:-1])
                # a section heading: "4." with KAVACH_FRS_04, or the next number on its own
                if re.search(r'KAVACH\s*_?(FRS|SYS)_?\s*\d+', line) or no == last_section + 1:
                    last_section = no
                    bounds.append((row_top(top, x), ('section', no)))
                continue
            cid = txt.rstrip('.')
            if cid.count('.') == 1 and seen_ids.get(cid, pn) < pn:
                # A radio case's row repeated at the top of its next page
                # ("32.1. Testing of ..."): dropped; what follows the page's
                # header continues the clause before it.
                after = [a for a, b in bands if a > top]
                bounds.append((row_top(top, x), ('drop', None)))
                bounds.append(((min(after) if after else top + 3), ('resume', None)))
                continue
            seen_ids.setdefault(cid, pn)
            bounds.append((row_top(top, x), ('clause', cid)))
        bounds.sort(key=lambda b: b[0])

        def put(w, target):
            if target is None:
                return
            role = role_of(w[1], edges, roles)
            target["words"].setdefault(role, []).append((pn, w))
            if pn not in target["pages"]:
                target["pages"].append(pn)

        bi = 0
        target = cur
        for w in sorted(words, key=lambda v: (v[2], v[1])) + [None]:
            while bi < len(bounds) and (w is None or w[2] >= bounds[bi][0]):
                kind, val = bounds[bi][1]
                bi += 1
                if kind == 'band':
                    # a header row inside this clause: what follows is in columns
                    if target is not None:
                        target.setdefault("bands", []).append((pn, bounds[bi - 1][0], dict(labels)))
                    continue
                if kind == 'drop':
                    target = None
                    continue
                if kind == 'resume':
                    target = cur
                    continue
                if kind == 'section':
                    cur = {"id": "S%d" % val, "no": val, "page": pn, "kind": "section", "words": {}, "pages": [pn]}
                else:
                    cur = {"id": val, "page": pn, "kind": "clause", "words": {}, "pages": [pn],
                           "labels": dict(labels), "roles": list(roles)}
                clauses.append(cur)
                target = cur
            if w is not None:
                put(w, target)
    return clauses


def text_of(words, role=None):
    """words: [(page, w)] or a words-by-role dict and a role."""
    ws = words.get(role, []) if isinstance(words, dict) else words
    out_lines = []
    for pn in sorted(set(p for p, _ in ws)):
        out_lines += lines_of([w for p, w in ws if p == pn])
    return reflow(out_lines)


def label_name(frag):
    t = frag.lower().replace(' ', '').replace('-', '')
    for pre, name in (('purpos', 'purpose'), ('method', 'methodology'), ('setup', 'setup'),
                      ('operat', 'operation')):
        if t.startswith(pre):
            return name
    return None


def case_heading(c):
    """A radio case row ("31.6. Testing of ...") with Purpose / Methodology /
    Setup rows, and for the RF tests (31.1-31.5) an operation table with no
    operation ids, read in columns after its header."""
    wb = c["words"]
    bands = c.get("bands", [])
    allw = [(p, role, w) for role in wb for p, w in wb[role]]

    def in_columns(p, w):
        return any(bp < p or (bp == p and a < w[2]) for bp, a, _ in bands)

    pre = [x for x in allw if not in_columns(x[0], x[2])]
    post = [x for x in allw if in_columns(x[0], x[2])]
    # label fragments in the id column: "Purpos" / "e", "Methodol" / "ogy",
    # "Setup" / "configu" / "ration". Anything else there is the title's.
    idw = sorted([(p, w) for p, role, w in pre if role == 'id' and w[0] != c["id"] + '.'],
                 key=lambda pw: (pw[0], pw[1][2], pw[1][1]))
    starts = []             # [page, top, text, last top]
    label_words = set()
    for p, w in idw:
        t = w[0].lower().rstrip(':')
        if label_name(t) in ('purpose', 'methodology', 'setup', 'operation') and len(t) >= 5:
            starts.append([p, w[2], w[0], w[2]])
            label_words.add(id(w))
        elif starts and starts[-1][0] == p and 0 <= w[2] - starts[-1][3] < 16 and t.strip('-') in LABEL_TAILS:
            starts[-1][2] += w[0]
            starts[-1][3] = w[2]
            label_words.add(id(w))
    labelled = [(p, top, label_name(t)) for p, top, t, _ in starts]
    values = [(p, w) for p, role, w in pre if id(w) not in label_words and w[0] != c["id"] + '.']
    # labels printed in a text column (section 32: "Purpose" at x 121)
    lines = []
    for pn in sorted(set(p for p, _ in values)):
        for top, x, t in lines_of([w for p, w in values if p == pn]):
            first = t.split(' ', 1)[0]
            name = label_name(first) if first.rstrip(':') in ('Purpose', 'Methodology', 'Methodol', 'Setup', 'Purpos') else None
            if name:
                labelled.append((pn, top, name))
                t = t.split(' ', 1)[1] if ' ' in t else ''
            lines.append((pn, top, x, t))
    labelled.sort()
    fields = {}
    for pn, top, x, t in lines:
        name = 'title'
        for lp, lt, ln in labelled:
            if (lp, lt) <= (pn, top + 2):
                name = ln
        fields.setdefault(name, []).append((top, x, t))
    out = {"title": reflow(fields.pop('title', []))}
    if not out["title"] and fields.get('purpose'):
        # 31.2-31.4 print the title in the Purpose cell, before "This test ..."
        pur = reflow(fields['purpose'])
        m = re.match(r'(Testing\b.*?)\s+(This test\b.*)$', pur, re.S)
        if m:
            out["title"] = m.group(1)
            fields['purpose'] = [(0, 0, m.group(2))]
    for name in ('purpose', 'methodology', 'setup'):
        if name in fields:
            out[name] = reflow(fields.pop(name))
    if name == 'setup' and out.get('setup'):
        out['setup'] = re.sub(r'^(configu\w*|ration|ion)\s*', '', out['setup'])
    for name, ls in fields.items():
        if name != 'operation':
            out.setdefault('other', [])
            out['other'].append(name + ': ' + reflow(ls))
    if 'other' in out:
        out['other'] = '\n'.join(out['other'])
    if post:
        by = {}
        for p, role, w in post:
            by.setdefault(role, []).append((p, w))
        for role in ('input', 'expected', 'result', 'observed'):
            t = text_of(by.get(role, []))
            if t:
                out[role] = t
        if by.get('id'):
            out['input'] = (text_of(by['id']) + '\n' + out.get('input', '')).strip()
        labs = bands[-1][2]
        for k in ('input', 'expected'):
            if labs.get(k):
                out[k + '_label'] = labs[k]
    return out


def clean_label(t):
    t = re.sub(r'([A-Za-z])- ([A-Za-z])', r'\1\2', t)
    half = len(t) // 2
    if len(t) % 2 == 1 and t[:half] == t[half + 1:]:
        t = t[:half]               # "Reception ... side Reception ... side"
    return t


def build(clauses, images):
    sections = []
    cases = []
    parents = set()
    for c in clauses:
        if c["kind"] == "clause" and c["id"].count('.') == 2:
            parents.add(c["id"].rsplit('.', 1)[0])
    for c in clauses:
        wb = c["words"]
        if c["kind"] == "section":
            sections.append({"no": c["no"], "page": c["page"] - 1,
                             "heading": text_of([pw for r in wb for pw in wb[r] if r != 'id' or not SECTION_RE.match(pw[1][0])])})
            continue
        roles = c.get("roles", [])
        radio = 'scenario' not in roles
        case = {"id": c["id"], "page": c["page"] - 1}   # the SIF's own page number
        if radio and c["id"].count('.') == 1 and (c["id"] in parents or c.get("bands")
                                                  or any(label_name(w[0]) in ('purpose', 'methodology')
                                                         for p, w in wb.get('id', []))):
            case.update(case_heading(c))
        else:
            if radio:
                case["input"] = text_of(wb, 'input')
                case["expected"] = text_of(wb, 'expected')
                r = text_of(wb, 'result')
                if r:
                    case["result"] = r
                labs = c.get("labels", {})
                for k in ('input', 'expected'):
                    if labs.get(k):
                        case[k + "_label"] = labs[k]
            else:
                case["title"] = text_of(wb, 'scenario')
                case["input"] = text_of(wb, 'input')
                case["expected"] = text_of(wb, 'expected')
            o = text_of(wb, 'observed')
            o = re.sub(r'^\(?Pass/Fail\)?\s*', '', o)
            if o:
                case["observed"] = o
            stray = text_of([(p, w) for p, w in wb.get('id', []) if w[0] != c["id"] + '.'])
            if stray:
                case["title"] = (stray + ('\n' + case["title"] if case.get("title") else '')).strip()
        for k in ('input_label', 'expected_label'):
            if k in case:
                case[k] = clean_label(case[k])
        pages = sorted(set(c["pages"]))
        if len(pages) > 1:
            case["pages"] = [p - 1 for p in pages]
        figs = sorted(set(p - 1 for p in pages if images.get(p)))
        if figs:
            case["figure_pages"] = figs
        cases.append(case)
    return sections, cases


def definitions(pages):
    """The SIF's own list of its sections (pages 10-17: # | Test Case ID |
    Test Case | Description)."""
    out = {}
    last = None
    for p in pages[9:18]:
        for t in p["tables"]:
            for r in t["rows"]:
                cells = [clean(para(c)) for c in r if c and norm(c)]
                if not cells:
                    continue
                m = re.match(r'^(\d{1,2})\.$', cells[0])
                if m and len(cells) >= 3 and re.match(r'KAVACH', cells[1]):
                    no = int(m.group(1))
                    out[no] = {"no": no, "code": re.sub(r'\s+', '', cells[1]),
                               "title": norm(cells[2]) if len(cells) > 3 else '',
                               "description": cells[3] if len(cells) > 3 else cells[2]}
                    last = no
                elif last is not None and not re.match(r'^(#|Sys|Test|Case ID)', cells[0]):
                    # a description running onto the next page
                    sep = '\n' if LIST_RE.match(cells[-1]) else ' '
                    out[last]["description"] += sep + cells[-1]
    for d in out.values():
        # a description running over a page break carries the page's footer
        d["description"] = re.sub(r'\s*RDSO Inspecting Official.*?Part-I\s*', '\n', d["description"], flags=re.S).strip()
    return out


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cache = argv[argv.index('--cache') + 1] if '--cache' in argv else None
    pages = load_pages(argv[1], cache)
    for p in pages:
        for w in p["words"]:
            t = re.sub(r'^[^0-9A-Za-z_]+|[^0-9A-Za-z_]+$', '', w[0])
            if t and '-' not in t:      # "S-KAVACH" does not make "SKAVACH" a word
                VOCAB.add(re.sub(r'[^0-9a-z_]', '', t.lower()))
            elif t:
                HYPHENATED.add(re.sub(r'[^0-9a-z_-]', '', t.lower()))
    images = {p["page"]: p.get("images", []) for p in pages}
    clauses = parse(pages)
    headings, cases = build(clauses, images)
    defs = definitions(pages)
    secs = []
    for h in headings:
        d = defs.get(h["no"], {"no": h["no"], "code": "", "title": h["heading"], "description": ""})
        sec = dict(d, page=h["page"])
        # what the section prints under its heading row, beyond the code and
        # title the Test Definitions table already gives (section 33's PG rules)
        lines = h["heading"].split('\n')
        first = lines[0]
        m = re.search(r'KAVACH\s*_?\s*(?:FRS|SYS)_?\s*\d+', first, re.I)
        rest = '\n'.join(lines[1:]).strip()
        if not m and h["heading"].strip():
            rest = h["heading"].strip()     # "34. RFID Reader and Tag Reliability Test Format"
        sec["heading"] = first.strip() if m else ''
        if rest:
            sec["notes"] = rest
        secs.append(sec)
    for no in sorted(set(defs) - set(h["no"] for h in headings)):
        print("warning: section %d is listed in Test Definitions but has no heading" % no)
        secs.append(defs[no])
    secs.sort(key=lambda d: d["no"])
    doc = {"format": "dlconsole-test-catalog", "version": 1,
           "source": "RDSO SIF 0533 Version 4.22, effective 04-08-2026: Functional Test Cases at System "
                     "Level for KAVACH, RDSO/SPN/196/2020 version 4.0 (FRS)",
           "sections": secs, "cases": cases}
    with open(argv[2], 'w', encoding='utf-8') as f:
        json.dump(doc, f, ensure_ascii=False, indent=1)
        f.write('\n')
    print("%d sections, %d cases" % (len(secs), len(cases)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
