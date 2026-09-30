"""Validate XML engine nmsflt BODY vs a port of the describe() NmsFault block
(reporting_subsystem + fault_count + decodeFault entries)."""
import glob, sys
import engine as E
from nms_meaning import nmsModuleName, nmsFaultInputName

def oracle(b):
    n = len(b)
    if n < 29: return []
    out = [("reporting_subsystem", f"{b[27]} ({nmsModuleName(b[27])})"),
           ("fault_count", str(b[28]))]
    if n >= 33:
        fc = b[28]; p = 29; idx = 1
        for _ in range(fc):
            if p + 4 > n - 4: break
            mod = b[p]; code = b[p+1]; fid = (b[p+2] << 8) | b[p+3]; p += 4
            out.append((f"fault[{idx}] module",    f"{mod} ({nmsModuleName(mod)})"))
            out.append((f"fault[{idx}] code_type", ("0x%02x" % code).upper()))
            out.append((f"fault[{idx}] fault_id",  f"{fid} ({nmsFaultInputName(fid)})"))
            idx += 1
    return out

def parse(ln): return bytes(int(x, 16) for x in ln.split()[3:])
def captype(ln): return ln.split()[0].lstrip('@').split('_')[0]

if __name__ == "__main__":
    sch = E.Schema("kavach.xml")
    ok = tot = 0; fails = []
    for cap in sorted(glob.glob("../replay/*.cap")):
        for ln in open(cap):
            if not ln.startswith("@") or captype(ln) != "nmsflt": continue
            b = parse(ln); tot += 1
            o = oracle(b)
            x = [(k.strip(), v) for k, v in E.decode(b, sch, None, "nmsflt")]
            if o == x: ok += 1
            else: fails.append((b, o, x))
    print(f"nmsflt body: {ok}/{tot} frames match")
    for b, o, x in fails[:2]:
        print(f"  len {len(b)}: oracle {len(o)} rows vs engine {len(x)} rows")
        for a, c in zip(o, x):
            if a != c: print(f"    {a!r} != {c!r}"); break
    sys.exit(0 if ok == tot else 1)
