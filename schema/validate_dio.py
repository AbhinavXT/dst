"""Validate XML engine dip/dop output vs the hand-decoder oracle over real caps.
Field name + value must match exactly; key indent is ignored."""
import glob, sys
import engine as E
import dio_oracle as O

def parse(ln): return bytes(int(x, 16) for x in ln.split()[3:])
def captype(ln): return ln.split()[0].lstrip('@').split('_')[0]

ORACLE = {"dip1": O.decodeDigitalIn1, "dip2": O.decodeDigitalIn2,
          "dop1": O.decodeDigitalOut, "dop2": O.decodeDigitalOut}

if __name__ == "__main__":
    sch = E.Schema("kavach.xml")
    per = {k: [0, 0] for k in ORACLE}              # captype -> [ok, total]
    fails = []
    for cap in sorted(glob.glob("../replay/*.cap")):
        for ln in open(cap):
            ct = captype(ln) if ln.startswith("@") else ""
            if ct not in ORACLE: continue
            b = parse(ln)
            per[ct][1] += 1
            o = ORACLE[ct](b)
            x = [(k.strip(), v) for k, v in E.decode(b, sch, None, ct)]
            if o == x: per[ct][0] += 1
            else: fails.append((cap, ct, b, o, x))
    tot_ok = sum(v[0] for v in per.values()); tot = sum(v[1] for v in per.values())
    for ct in ("dip1","dip2","dop1","dop2"):
        print(f"  {ct}: {per[ct][0]}/{per[ct][1]}")
    print(f"dip/dop total: {tot_ok}/{tot}")
    for cap, ct, b, o, x in fails[:2]:
        print(f"\n  MISMATCH ({ct}) {cap} len {len(b)}:")
        od, xd = dict(o), dict(x)
        for k in list(dict.fromkeys([k for k,_ in o] + [k for k,_ in x])):
            if od.get(k,"<absent>") != xd.get(k,"<absent>"):
                print(f"    {k!r}: oracle={od.get(k,'<absent>')!r} engine={xd.get(k,'<absent>')!r}")
    sys.exit(0 if tot_ok == tot else 1)
