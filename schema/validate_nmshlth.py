"""Validate XML engine nmshlth event-stream vs a port of decodeHealth().
The event-size table is parsed from capturedecoder.cpp (independent of the XML),
so this checks both the XML <eventtable> values AND the stream walk. The per-eid
meaning is the shared nms_meaning port (same on both sides, as in the app)."""
import glob, re, sys
import engine as E
from nms_meaning import nmsHealthMeaning

# event table from the C++ source: id -> (size, signed, name)
src = open('../capturedecoder.cpp').read()
body = re.search(r'kNmsEventTable\[\]\s*=\s*\{(.*?)\};', src, re.S).group(1)
ETBL = {int(i): (int(sz), s == 'true', nm)
        for i, sz, s, nm in re.findall(r'\{\s*(\d+),\s*(\d+),\s*(true|false),\s*"([^"]+)"\s*\}', body)}

def beUInt(b, p, sz, signed):
    v = 0
    for k in range(sz): v = (v << 8) | b[p+k]
    if signed and (v & (1 << (sz*8-1))): v -= (1 << (sz*8))
    return v

def oracle(b):                                     # decodeHealth walk + describe render
    n = len(b)
    if n < 32: return []
    o = 27; count = b[o]; p = o + 1; out = []
    for _ in range(count):
        if p + 2 > n - 4: break
        eid = (b[p] << 8) | b[p+1]; p += 2
        ed = ETBL.get(eid); sz = ed[0] if ed else 1
        if p + sz > n - 4: break
        v = beUInt(b, p, sz, ed[1] if ed else False)
        name = ed[2] if ed else f"EVENT_{eid}"
        p += sz
        m = nmsHealthMeaning(eid, v)
        out.append((name, str(v) if not m else f"{v}  ({m})"))
    return out

def parse(ln): return bytes(int(x, 16) for x in ln.split()[3:])
def captype(ln): return ln.split()[0].lstrip('@').split('_')[0]

if __name__ == "__main__":
    sch = E.Schema("kavach.xml")
    ok = tot = 0; fails = []
    for cap in sorted(glob.glob("../replay/*.cap")):
        for ln in open(cap):
            if not ln.startswith("@") or captype(ln) != "nmshlth": continue
            b = parse(ln)
            tot += 1
            o = oracle(b)
            x = [(k.strip(), v) for k, v in E.decode(b, sch, None, "nmshlth")]
            if o == x: ok += 1
            else: fails.append((cap, b, o, x))
    print(f"nmshlth event-stream: {ok}/{tot} frames match")
    for cap, b, o, x in fails[:2]:
        print(f"  len {len(b)}: oracle {len(o)} rows, engine {len(x)} rows")
        for (ok_, ov), (xk, xv) in zip(o, x):
            if (ok_, ov) != (xk, xv):
                print(f"    oracle ({ok_!r},{ov!r}) != engine ({xk!r},{xv!r})"); break
    sys.exit(0 if ok == tot else 1)
