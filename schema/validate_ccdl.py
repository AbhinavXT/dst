"""Validate XML engine ccsys/dlsys vs the hand-decoder oracle (incl. embedded CRCs)."""
import glob, sys
import engine as E
import ccdl_oracle as O

def parse(ln): return bytes(int(x, 16) for x in ln.split()[3:])
def captype(ln): return ln.split()[0].lstrip('@').split('_')[0]
ORACLE = {"ccsys": O.decodeCcSys, "dlsys": O.decodeDlSys}

if __name__ == "__main__":
    sch = E.Schema("kavach.xml")
    per = {k: [0, 0] for k in ORACLE}; fails = []
    for cap in sorted(glob.glob("../replay/*.cap")):
        for ln in open(cap):
            ct = captype(ln) if ln.startswith("@") else ""
            if ct not in ORACLE: continue
            b = parse(ln); per[ct][1] += 1
            o = ORACLE[ct](b)
            x = [(k.strip(), v) for k, v in E.decode(b, sch, None, ct)]
            if o == x: per[ct][0] += 1
            else: fails.append((ct, b, o, x))
    for ct in ("ccsys", "dlsys"): print(f"  {ct}: {per[ct][0]}/{per[ct][1]}")
    tot_ok = sum(v[0] for v in per.values()); tot = sum(v[1] for v in per.values())
    print(f"ccsys/dlsys total: {tot_ok}/{tot}")
    for ct, b, o, x in fails[:2]:
        print(f"  MISMATCH ({ct}) len {len(b)}:")
        od, xd = dict(o), dict(x)
        for k in list(dict.fromkeys([k for k,_ in o]+[k for k,_ in x])):
            if od.get(k,'<a>') != xd.get(k,'<a>'):
                print(f"    {k!r}: oracle={od.get(k,'<a>')!r} engine={xd.get(k,'<a>')!r}")
    sys.exit(0 if tot_ok == tot else 1)
