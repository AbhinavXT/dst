"""
engine.py -- schema-driven Kavach packet decoder (reference implementation).

Reads kavach.xml and decodes a frame into (name, value) rows. The C++ engine
(schemadecoder.cpp) mirrors this logic exactly; validate every schema edit here
first (no Qt compiler in the sandbox).

Capabilities (full SLRP parity with the hand-written kschema tables):
  - msb-first / lsb-first bit cursors, signed fields, units
  - enums: exact values, ranges with formula (v*K / v+K / v-K), %v/%f label
    placeholders, and a default fallback
  - id/name capture; every field is addressable by name in when/count
  - when conditions: id OP literal, and "id in LO..HI" (inclusive range)
  - repeat count="name"; per-field entry templates (tmpl) and hidden fields
  - abs-location roles (seglen / start / len / taghop) -> "[a -> b m]" / "abs="
    suffixes, composed from a reference RFID abs_loc + DIST_PKT_START + PKT_DIR
  - byte-aligned sub-packet dispatch + MAC_CODE / unparsed tail
  - a tiny named-formatter registry (format="...") for genuinely composite
    fields that resist plain enums (currently: sigInfo)
"""
import re
import sys
import struct as _struct
import xml.etree.ElementTree as ET

# ============================ bit cursors ==================================
class MsbCursor:
    def __init__(s, b, start=0, limit=None):
        s.b = b; s.pos = start; s.limit = limit if limit is not None else len(b) * 8
    def take(s, n):
        v = 0
        for _ in range(n):
            if s.pos > s.limit - 1: s.pos += 1; continue
            v = (v << 1) | ((s.b[s.pos >> 3] >> (7 - (s.pos & 7))) & 1); s.pos += 1
        return v

class LsbCursor:                       # Annexure-D RFID (not used by SLRP)
    def __init__(s, b, start=0, limit=None):
        s.b = b; s.pos = start; s.limit = limit if limit is not None else len(b) * 8
    def take(s, n):
        v = 0
        for i in range(n):
            if s.pos > s.limit - 1: s.pos += 1; continue
            v |= ((s.b[s.pos >> 3] >> (s.pos & 7)) & 1) << i; s.pos += 1
        return v

def to_signed(v, n):
    return v - (1 << n) if (v & (1 << (n - 1))) else v

def hexRange(b, frm, ln):
    return ' '.join('%02X' % x for x in b[frm:frm+ln])

# ============================ named formatters =============================
# The single escape hatch: composite fields whose decode is a bit-decomposition
# into sparse sub-tables, not expressible as a flat enum. Referenced from the
# schema via format="name". The C++ engine keeps a matching pointer registry.
def _sigDir(v):
    d = {0:"UP",1:"DN",2:"UP FAST",3:"DN FAST",8:"UP SLOW",9:"DN SLOW",10:"UP MAIN",
         11:"DN MAIN",12:"UP SUB",13:"DN SUB",14:"UP BI-DIR",15:"DN BI-DIR"}
    return d.get(v, "undefined")
def _sigKind(v):
    d = {0x10:"Dist",0x11:"Inr-Dist",0x12:"Gate-Dist",0x13:"Gate-Inr-Dist",0x14:"IB-Dist",
         0x15:"IB-Inr-Dist",0x16:"Auto",0x17:"Semi-Auto Lit",0x18:"Home",0x19:"Home L-X",
         0x1A:"R-Home",0x1B:"R-Home L-X",0x1C:"M/L-Str L-X",0x1D:"L/L-Str L-X",0x1E:"Int-Str",
         0x01:"Adv-Str",0x02:"IB-Stop",0x03:"Gate-Stop",0x04:"Calling-On",0x05:"Adv-Str-cum-Gate",
         0x06:"Gate-cum-Dist",0x07:"Adv-Str-cum-Dist",0x23:"Auto-Gate",0x24:"Semi-Auto",
         0x25:"Adv-Str-cum-G-ID",0x26:"Gate-cum-ID",0x27:"Gate-ID-cum-Dist",0x28:"IB-cum-Gate-Dist",
         0x29:"IB-cum-Gate-ID",0x2A:"IB-cum-Dist",0x2B:"Adv-Str-cum-IB-D",0x2C:"Str-cum-IB-Dist",
         0x2D:"Stop Board",0x2E:"Gate-cum-IB-Dist",0x2F:"Gate-cum-IB-ID",0x30:"Adv-Str-cum-G-D"}
    return d.get(v, "undefined")
def _sigInfo(v):
    line = v & 0x1F; dr = (v >> 5) & 0xF; kind = (v >> 9) & 0x3F
    ovr = (v >> 15) & 1; stop = (v >> 16) & 1
    return (f"0x{v:x}  [type={_sigKind(kind)}  dir={_sigDir(dr)}  "
            f"line={'n/a' if line == 0 else line}  "
            f"override={'running' if ovr else 'standstill'}  stop_sig={'Y' if stop else 'N'}]")

def _sigAspect(v):
    t = {0:"Unidentified",1:"Red",2:"Yellow, no route indication",
         3:"Yellow + Pos1 junction route, left",4:"Yellow + Pos2 junction route, left",
         5:"Yellow + Pos3 junction route, left",6:"Yellow + Pos4 junction route, right",
         7:"Yellow + Pos5 junction route, right",8:"Yellow + Pos6 junction route, right",
         10:"Double Yellow",11:"Green",12:"Double Yellow + Pos1 junction route, left",
         13:"Double Yellow + Pos4 junction route, right",14:"AG Marker OFF",
         15:"Red with Calling-on at OFF",24:"Stop Board / Buffer Stop"}
    if v in t: return f"{v} ({t[v]})"
    if 32 <= v <= 63: return f"{v} (Yellow + Stencil route {v-31})"
    return f"{v} (Spare)"
def _absKm(v):  return f"{v} m ({v/1000.0:.3f} km)"      # DMI abs_loco_loc
def _decel(v):  return f"DC {v//100}.{v%100:02d}"        # DMI deceleration_constant

FORMATTERS = { "sigInfo": _sigInfo, "sigAspect": _sigAspect,
               "absKm": _absKm, "decel": _decel }

# Float/double counterparts, referenced the same way (format="name") from a
# field whose type= is float or double. Separate registry because the integer
# formatters above index/divide their argument and would silently misbehave on
# a real. Consulted by _fmt_float.
def _mps(v):                                             # m/s, with the km/h an
    return f"{v:.4f} m/s  ({v * 3.6:.1f} km/h)"          # operator actually reads
def _curveA(v):
    # UBA braking curves are stored as  x = A*v^2 + C, so A is not itself a
    # deceleration: a = -1/(2A). Show both — A is what is on the wire, a is
    # what the curve means. A == 0 is an empty array slot, not a zero decel.
    if v == 0.0: return f"{v:.6f}"
    return f"{v:.6f}  (decel {-1.0 / (2.0 * v):.3f} m/s\u00B2)"

FLOAT_FORMATTERS = { "mps": _mps, "curveA": _curveA }

# value-meaning hooks for event streams: (eid, value) -> str  ("" = numeric only)
try:
    from nms_meaning import nmsHealthMeaning as _nmsHealthMeaning
    MEANINGS = { "nmsHealth": _nmsHealthMeaning }
except ImportError:
    MEANINGS = {}

# CRC algorithms for <crc> elements: (data, from, len) -> int
try:
    from crc_algos import CRC_ALGOS
except ImportError:
    CRC_ALGOS = {}

# ============================ schema model =================================
class Schema:
    def __init__(s, path):
        s.root = ET.parse(path).getroot()
        en = s.root.find('enums')
        s.enums = {e.get('name'): e for e in (en.findall('enum') if en is not None else [])}
        s.structs = {st.get('name'): st for st in s.root.findall('struct')}
        s.packets = s.root.findall('packet')
        # flag tables: name -> [flag name in declaration order] (single-bit vectors)
        s.flagtables = {}
        fts = s.root.find('flagtables')
        for ft in (fts.findall('flagtable') if fts is not None else []):
            s.flagtables[ft.get('name')] = [f.get('name') for f in ft.findall('flag')]
        # event tables: name -> {id: (bytes, signed, name)}
        s.eventtables = {}
        ets = s.root.find('eventtables')
        for et in (ets.findall('eventtable') if ets is not None else []):
            tbl = {}
            for e in et.findall('event'):
                tbl[int(e.get('id'))] = (int(e.get('bytes', '1')),
                                         e.get('signed') == 'true',
                                         e.get('name'))
            s.eventtables[et.get('name')] = tbl
        s._validate_conditions()

    def _validate_conditions(s):
        """Refuse, don't guess -- mirrors Decoder::validateConditions()."""
        owners = [e for e in s.root if e.tag in ('packet', 'struct')]
        declared = set()
        for o in owners:
            for e in o.iter():
                if e.get('name') is not None: declared.add(e.get('name'))
                if e.get('id')   is not None: declared.add(e.get('id'))
        for o in owners:
            for e in o.iter():
                for attr in ('when', 'test'):
                    cond = e.get(attr)
                    if cond is None: continue
                    why, n = check_condition(cond)
                    nm = e.get('name')
                    label = ' name="%s"' % nm if nm else ''
                    where = f'<{e.tag}{label}> in {o.tag} {o.get("name")}'
                    if why:
                        raise ValueError(f'bad condition {attr}="{cond}" on {where}: {why}')
                    if n is not None and n not in declared:
                        raise ValueError(f'condition {attr}="{cond}" on {where} names '
                                         f"'{n}', which no packet or struct declares")

    def enum_label(s, name, v):
        e = s.enums.get(name)
        if e is None: return str(v)
        default = None
        for m in e.findall('map'):
            if m.get('default') == 'true':
                default = m; continue
            if m.get('v') is not None and int(m.get('v')) == v:
                return s._fmt_map(m, v, v)
            if m.get('from') is not None and int(m.get('from')) <= v <= int(m.get('to')):
                f = s._formula(m.get('formula'), v)
                return s._fmt_map(m, v, f)
        if default is not None:
            return s._fmt_map(default, v, v)
        return str(v)

    @staticmethod
    def _formula(expr, v):
        if not expr: return v
        if expr.startswith('v*'): return v * int(expr[2:])
        if expr.startswith('v+'): return v + int(expr[2:])
        if expr.startswith('v-'): return v - int(expr[2:])
        return v

    @staticmethod
    def _fmt_map(m, v, f):
        lbl = m.get('label')
        if lbl is None:                       # no label: build from formula + unit
            unit = m.get('unit', '')
            return f"{f} {unit}".strip()
        lbl = lbl.replace('%v', str(v)).replace('%f', str(f))
        unit = m.get('unit')
        return f"{lbl} {unit}".strip() if unit else lbl

# ---------------------------- conditions ----------------------------------
# Condition grammar -- identical to Schema::Decoder::checkCondition() in C++:
#     NAME OP LIT               OP in == != >= <= > <
#     NAME & LIT (==|!=) LIT    mask test (LSRP health rotates on FRAME_NUM & 7)
#     NAME in LIT..LIT          inclusive range
#     NAME not in LIT..LIT      negated inclusive range
# LIT is decimal or 0x-hex, optionally negative; a leading zero is decimal,
# never octal. At run time an absent field reads as 0 (sub-packet fields are
# absent from most frames); a name NO packet declares is refused at load.
_COND_NAME = re.compile(r'[A-Za-z_][A-Za-z0-9_.]*\Z')

def cond_literal(text):
    t = text.strip()
    neg = t.startswith('-')
    if neg: t = t[1:].strip()
    if t[:2].lower() == '0x':
        if len(t) == 2: raise ValueError(text)
        v = int(t[2:], 16)
    else:
        if not t.isdigit(): raise ValueError(text)
        v = int(t, 10)
    return -v if neg else v

def check_condition(when):
    """('' , name) if well-formed, else (why, None)."""
    w = (when or '').strip()
    if not w: return '', None
    def name_of(raw):
        n = raw.strip()
        return n if _COND_NAME.match(n) else None
    def rng_ok(rng):
        if '..' not in rng: return 'range needs LO..HI'
        lo, hi = rng.split('..', 1)
        try: lo, hi = cond_literal(lo), cond_literal(hi)
        except ValueError: return 'range bounds must be numbers'
        return 'range LO is above HI' if lo > hi else ''
    for kw in (' not in ', ' in '):
        if kw in w:
            a, rng = w.split(kw, 1); n = name_of(a)
            if n is None: return f"'{a.strip()}' is not a field name", None
            why = rng_ok(rng.strip())
            return (why, None) if why else ('', n)
    if '&' in w:
        a, rest = w.split('&', 1); n = name_of(a)
        if n is None: return f"'{a.strip()}' is not a field name", None
        if '&' in rest: return "only one '&' mask is supported (no &&)", None
        op = '==' if '==' in rest else ('!=' if '!=' in rest else None)
        if op is None: return 'mask test needs == or !=', None
        mk, want = rest.split(op, 1)
        try: cond_literal(mk)
        except ValueError: return 'mask must be a number', None
        try: cond_literal(want)
        except ValueError: return 'mask comparand must be a number', None
        return '', n
    for op in ('==', '!=', '>=', '<=', '>', '<'):
        if op in w:
            a, b = w.split(op, 1); n = name_of(a)
            if n is None: return f"'{a.strip()}' is not a field name", None
            try: cond_literal(b)
            except ValueError: return 'right-hand side must be a number', None
            return '', n
    return 'no operator (expected ==, !=, >=, <=, >, <, &, in, not in)', None

def cond_ok(when, ctx):
    if not when: return True
    if ' not in ' in when:                    # "name not in LO..HI" (negated range)
        a, rng = when.split(' not in ', 1)
        lo, hi = rng.split('..', 1)
        x = ctx.get(a.strip(), 0)
        return not (cond_literal(lo) <= x <= cond_literal(hi))
    if ' in ' in when:                        # "name in LO..HI" inclusive
        a, rng = when.split(' in ', 1)
        lo, hi = rng.split('..', 1)
        x = ctx.get(a.strip(), 0)
        return cond_literal(lo) <= x <= cond_literal(hi)
    if '&' in when:                           # "name & MASK ==|!= VALUE"
        a, rest = when.split('&', 1)
        op = '==' if '==' in rest else '!='
        mk, want = rest.split(op, 1)
        got = ctx.get(a.strip(), 0) & cond_literal(mk)
        return (got == cond_literal(want)) == (op == '==')
    for op in ('==', '!=', '>=', '<=', '>', '<'):
        if op in when:
            a, b = when.split(op, 1); x = ctx.get(a.strip(), 0); b = cond_literal(b)
            return {'==':x==b,'!=':x!=b,'>':x>b,'<':x<b,'>=':x>=b,'<=':x<=b}[op]
    return True       # unreachable for a schema that loaded (check_condition)

def captype_tokens(match):
    """The captype tokens a <packet match="captype==a,b"> answers to."""
    if not match.startswith('captype') or '==' not in match: return []
    return [t.strip() for t in match.split('==', 1)[1].split(',') if t.strip()]

# ---------------------------- value rendering -----------------------------
def _bare(fld, sch, val):
    """value as used inside an entry template (enum/format applied, no unit)."""
    if isinstance(val, float): return _fmt_float(fld, val)
    if isinstance(val, str):   return val if val != "" else "(empty)"
    if fld.get('format'): return FORMATTERS[fld.get('format')](val)
    if fld.get('enum'):   return sch.enum_label(fld.get('enum'), val)
    return str(val)

def _fmt_float(fld, val):
    fmt = fld.get('format')
    if fmt:                                   # formatter owns the whole token,
        return FLOAT_FORMATTERS[fmt](val)     # units included -> no prec/unit here
    prec = fld.get('prec')
    s = (("%%.%df" % int(prec)) % val) if prec else ("%g" % val)
    u = fld.get('unit')
    return f"{s} {u}" if u else s

def _full(fld, sch, val):
    """value for a flat row (meaning/enum/format/hex, else 'val unit', else val)."""
    if isinstance(val, float): return _fmt_float(fld, val)        # IEEE-754 le float
    if isinstance(val, str):   return val if val != "" else "(empty)"   # char[] ascii
    if fld.get('meaning'):                          # per-eid meaning, shown alone
        return MEANINGS[fld.get('meaning')](int(fld.get('eid')), val)
    if fld.get('format'): return FORMATTERS[fld.get('format')](val)
    if fld.get('enum'):   return sch.enum_label(fld.get('enum'), val)
    if fld.get('hex'):    return "0x%0*x" % (int(fld.get('hex')), val)
    if fld.get('hexup'):  return ("0x%0*x" % (int(fld.get('hexup')), val)).upper()
    if fld.get('unit'):   return f"{val} {fld.get('unit')}"
    return str(val)

def _read(cur, fld):
    t = fld.get('type')
    if t == 'float':                         # 4-byte IEEE-754, le (lsb-first regime)
        raw = cur.take(32) & 0xFFFFFFFF
        return _struct.unpack('<f', _struct.pack('<I', raw))[0]
    if t == 'double':                        # 8-byte IEEE-754, le (lsb-first regime)
        # take() returns at most 32 bits, so read the two halves in wire order:
        # lsb-first means the LOW word comes first.
        lo = cur.take(32) & 0xFFFFFFFF
        hi = cur.take(32) & 0xFFFFFFFF
        return _struct.unpack('<d', _struct.pack('<II', lo, hi))[0]
    if t == 'char':                          # fixed ASCII array, NUL-trimmed
        n = int(fld.get('count'))
        bs = bytes(cur.take(8) & 0xFF for _ in range(n))
        return bs.split(b'\x00', 1)[0].decode('ascii', 'replace')
    n = int(fld.get('bits'))
    raw = cur.take(n)
    return to_signed(raw, n) if fld.get('signed') == 'true' else raw

# computed-row template: "{id}" -> str(value), "{id:0Nd}" -> zero-padded int.
# Substitutes already-stored field ids; consumes no bits.
import re as _re
_ROW_TOK = _re.compile(r'\{([A-Za-z0-9_]+)(?::([^}]+))?\}')
def _row_tmpl(tmpl, ctx):
    def sub(m):
        name, spec = m.group(1), m.group(2)
        v = ctx.get(name, 0)
        return format(int(v), spec) if spec else str(v)
    return _ROW_TOK.sub(sub, tmpl)

# ---------------------------- geo (abs-location) --------------------------
class Geo:
    def __init__(s): s.known=False; s.refAbs=0; s.blockAbs=0; s.travel=1

# ============================ walkers =====================================
def walk_flat(cur, fld, sch, ctx, rows):
    tag = fld.tag
    if tag == 'when':                             # conditional block of flat children
        if cond_ok(fld.get('test'), ctx):
            for sub in fld:
                walk_flat(cur, sub, sch, ctx, rows)
        return
    if tag == 'pad':
        cur.take(int(fld.get('bits'))); return
    if tag == 'align':                            # round cursor up to next byte
        if cur.pos & 7: cur.pos += 8 - (cur.pos & 7)
        return
    if tag == 'flags':                            # N single-bit flags -> joined names
        # Mirrors the C++ <flags> walker. order="lsb": the table is indexed
        # by BIT POSITION (bit 0 = LSB), not read order -- in an msb-first
        # packet the first bit read is the highest, so without this every
        # fault would be reported as its mirror-image neighbour.
        # expand="all": one row per bit as well as the summary row.
        names = sch.flagtables.get(fld.get('table'), [])
        nbits = int(fld.get('bits'))
        lsb = fld.get('order') == 'lsb'
        bitset = [False] * nbits
        for i in range(nbits):
            idx = (nbits - 1 - i) if lsb else i
            bitset[idx] = bool(cur.take(1))
        on = [names[b] for b in range(nbits) if bitset[b] and b < len(names)]
        cur.take(int(fld.get('pad', '0')))
        rows.append(("  " + fld.get('name'), "; ".join(on) if on else "(none)"))
        if fld.get('expand') == 'all':
            for b in range(min(nbits, len(names))):
                rows.append(("    " + names[b], "1 (FAULT)" if bitset[b] else "0 (ok)"))
        return
    if tag == 'row':                              # computed row from stored ids
        rows.append(("  " + fld.get('name'), _row_tmpl(fld.get('tmpl', ''), ctx)))
        return
    if tag == 'note':                             # literal annotation row
        if not cond_ok(fld.get('when'), ctx): return
        rows.append(("  " + fld.get('name'), fld.get('text', '')))
        return
    if tag == 'crc':                              # stored value + computed check
        at = fld.get('at')                       # optional absolute bit offset to seek
        if at is not None: cur.pos = int(at)
        nbits = int(fld.get('bits', '32'))       # stored width (LE/lsb-first), default 32
        stored = cur.take(nbits)
        algo = CRC_ALGOS.get(fld.get('algo'))
        frm = int(fld.get('from')); ln = int(fld.get('len'))
        calc = algo(cur.b, frm, ln) if algo else None
        ok = (calc == stored)
        rows.append(("  " + fld.get('name'),
                     ("0x%08x" % stored).upper() + ("  PASS" if ok else "  FAIL")))
        return
    if tag == 'group':                            # several sub-fields -> one row
        sep = fld.get('sep', '  ')
        ev = dict(ctx)
        line = walk_entry(cur, fld, sch, ev, {'seg': 0, 'tag': 0}, sep)
        for k, v in ev.items():
            if k != '__geo__': ctx[k] = v
        rows.append(("  " + fld.get('name'), line))
        return
    if tag == 'eventstream':                      # count + (id -> size/name) records
        table = sch.eventtables.get(fld.get('table'), {})
        meaning = MEANINGS.get(fld.get('meaning'))
        cbits = int(fld.get('count_bits', '8'))
        idbits = int(fld.get('id_bits', '16'))
        count = cur.take(cbits)
        for _ in range(count):
            if cur.pos + idbits > cur.limit: break
            eid = cur.take(idbits)
            ed = table.get(eid)
            sz = ed[0] if ed else 1
            if cur.pos + sz * 8 > cur.limit: break
            raw = cur.take(sz * 8)
            v = to_signed(raw, sz * 8) if (ed and ed[1]) else raw
            name = ed[2] if ed else f"EVENT_{eid}"
            if meaning:
                m = meaning(eid, v)
                val = f"{v}  ({m})" if m else str(v)
            else:
                val = str(v)
            rows.append(("  " + name, val))
        return
    if tag == 'field':
        if not cond_ok(fld.get('when'), ctx): return
        v = _read(cur, fld)
        ctx[fld.get('name')] = v
        if fld.get('id'): ctx[fld.get('id')] = v
        if fld.get('hide') == 'true': return
        rows.append(("  " + fld.get('name'), _full(fld, sch, v)))
        return
    if tag == 'repeat':
        cattr = fld.get('count')
        if cattr is not None and cattr.lstrip('-').isdigit():
            cnt = int(cattr)                       # fixed literal count
        else:
            cnt = ctx.get(cattr, 0)                # count from a named field
        cmin = int(fld.get('cmin', '0')); cmax = int(fld.get('cmax', '63'))
        if cnt < cmin or cnt > cmax: cnt = 0
        nm = fld.get('name')
        base = 0 if fld.get('index0') == 'true' else 1
        sep = fld.get('sep', '  ')
        if fld.get('expand') == 'true':            # each child -> its own row
            kids = [k for k in fld if k.tag == 'field']
            ebits = sum(int(k.get('bits')) for k in kids)
            for i in range(cnt):
                if cur.pos + ebits > cur.limit: break
                for k in kids:
                    v = _read(cur, k)
                    ctx[k.get('name')] = v
                    rows.append((f"  {nm}[{i+base}] {k.get('name')}", _full(k, sch, v)))
            return
        grp = {'seg': 0, 'tag': 0}
        for i in range(cnt):
            if cur.pos >= cur.limit: break
            ev = dict(ctx)
            rows.append((f"  {nm}[{i+base}]", walk_entry(cur, fld, sch, ev, grp, sep)))
        return

def walk_entry(cur, rep, sch, ctx, grp, sep="  "):
    toks = []
    seg = start = ln = tag = None
    geo = ctx.get('__geo__')
    for sub in rep:
        if sub.tag == 'pad': cur.take(int(sub.get('bits'))); continue
        if sub.tag != 'field': continue
        if not cond_ok(sub.get('when'), ctx): continue
        v = _read(cur, sub)
        ctx[sub.get('name')] = v
        if sub.get('id'): ctx[sub.get('id')] = v
        role = sub.get('role')
        if   role == 'seglen': seg = v
        elif role == 'start':  start = v
        elif role == 'len':    ln = v
        elif role == 'taghop': tag = v
        if sub.get('hide') == 'true': continue
        bare = _bare(sub, sch, v)
        tmpl = sub.get('tmpl')
        toks.append((tmpl % bare) if tmpl else f"{sub.get('name')}={bare}")
    suf = ""
    if geo and geo.known:
        if seg is not None:
            a = grp['seg']; b = a + seg; grp['seg'] = b
            suf = f"   [{geo.blockAbs + geo.travel*a} \u2192 {geo.blockAbs + geo.travel*b} m]"
        elif start is not None and ln is not None:
            suf = f"   [{geo.blockAbs + geo.travel*start} \u2192 {geo.blockAbs + geo.travel*(start+ln)} m]"
        elif start is not None:
            suf = f"   abs={geo.blockAbs + geo.travel*start} m"
        elif tag is not None:
            grp['tag'] += tag
            suf = f"   abs={geo.blockAbs + geo.travel*grp['tag']} m"
    return sep.join(toks) + suf

def walk_struct(cur, st, sch, ctx, rows):
    for fld in st:
        walk_flat(cur, fld, sch, ctx, rows)

# ============================ top-level decode ============================
def decode(frame, sch, tagLoc=None, captype=None):
    rows = []
    pkt = None
    if captype:                                   # primary: select by @token
        for p in sch.packets:
            if captype in captype_tokens(p.get('match', '')):
                pkt = p; break
    if pkt is None:                               # fallback: top-nibble pkt_type
        top = (frame[0] >> 4) & 0xF
        for p in sch.packets:
            m = p.get('match', '')
            if m.startswith('pkt_type==') and int(m.split('==')[1]) == top:
                pkt = p; break
    if pkt is None:
        return [("(no schema)", f"captype={captype}")]

    n = len(frame)
    msb = pkt.get('wire', 'msb-first') != 'lsb-first'
    Cur = MsbCursor if msb else LsbCursor
    trailer = int(pkt.get('trailer_bytes', '4'))   # 4-byte CRC by default; 0 = none
    boff = int(pkt.get('body_offset', '0'))        # bits to skip (e.g. 80 = 10-B LE header)
    cur = Cur(frame, boff, (n - trailer) * 8)
    ctx = {}

    for fld in pkt:
        if fld.tag == 'subpackets':
            # geo from header fields, composed per Annexure-C
            geo = Geo()
            ref  = ctx.get('LAST_REF_RFID', 0)
            dps  = ctx.get('DIST_PKT_START', 0)
            pdir = ctx.get('PKT_DIR', 0)
            if tagLoc and ref in tagLoc:
                geo.known  = True
                geo.refAbs = tagLoc[ref]
                geo.travel = -1 if pdir == 2 else 1
                # Single spatial origin for the whole look-ahead profile: the
                # START SIGNAL the MA/profile is issued from. DIST_PKT_START is
                # the travel-signed gap from LAST_REF_RFID to that signal, so
                # blockAbs = refAbs + travel*dps. Every overlay AND the tag list
                # are look-ahead distances measured from this origin.
                geo.blockAbs = geo.refAbs + geo.travel * dps
            ctx['__geo__'] = geo

            tb = int(fld.get('type_bits')); lb = int(fld.get('len_bits'))
            reserve = int(fld.get('reserve_tail', '8'))
            cases = {c.get('type'): c.get('struct') for c in fld.findall('case')}
            idx = 1
            while cur.pos // 8 < n - reserve:
                sb = cur.pos // 8
                t = cur.take(tb); L = cur.take(lb)
                if L == 0 or sb + L > n - 4: break
                stname = cases.get(str(t))
                rows.append((f"subpkt[{idx}] {stname or 'reserved(%d)' % t} @byte {sb}", f"{L} B"))
                if stname and stname in sch.structs:
                    sc = Cur(frame, sb*8 + tb + lb, sb*8 + L*8)
                    ss = dict(ctx)
                    walk_struct(sc, sch.structs[stname], sch, ss, rows)
                cur.pos = sb*8 + L*8
                idx += 1
            endByte = cur.pos // 8
            rem = (n - 4) - endByte
            if rem == 4:   rows.append(("  MAC_CODE", hexRange(frame, endByte, 4)))
            elif rem > 0:  rows.append(("  unparsed (raw)", hexRange(frame, endByte, rem)))
        else:
            walk_flat(cur, fld, sch, ctx, rows)
    return rows

# ============================ CLI / smoke test ============================
if __name__ == "__main__":
    sch = Schema("kavach.xml")
    def parse(ln): return bytes(int(x, 16) for x in ln.split()[3:])
    cap = sys.argv[1] if len(sys.argv) > 1 else "../replay/loco_1_1_18062026_140921.cap"
    tagLoc = {989: 162660}                     # known golden ref for abs demo
    shown = 0
    for ln in open(cap):
        if not ln.startswith("@slrp"): continue
        b = parse(ln)
        if len(b) < 12: continue
        if ((b[0] >> 4) & 0xF) == 9:
            for k, v in decode(b, sch, tagLoc, "slrp"):
                print(f"{k:<34} {v}")
            shown += 1
            if shown >= 1: break
