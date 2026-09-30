"""Validate XML engine aap/aep output vs the hand-decoder oracle over real caps.
Field name + value must match exactly; leading indent on keys is ignored
(schema uses a uniform 2-space prefix, hand decoders use bare names)."""
import glob, sys
import engine as E
import aap_aep_oracle as O

def parse(ln): return bytes(int(x, 16) for x in ln.split()[3:])
def captype(ln): return ln.split()[0].lstrip('@').split('_')[0]

ORACLE = {"aap": O.decodeAAP, "aep": O.decodeAep}

if __name__ == "__main__":
    sch = E.Schema("kavach.xml")
    total = ok = 0; fails = []
    for cap in sorted(glob.glob("../replay/*.cap")):
        for ln in open(cap):
            ct = captype(ln) if ln.startswith("@") else ""
            if ct not in ORACLE: continue
            b = parse(ln)
            if len(b) < 8: continue
            total += 1
            o = ORACLE[ct](b)
            x = [(k.strip(), v) for k, v in E.decode(b, sch, None, ct)]
            if o == x: ok += 1
            else: fails.append((cap, ct, b, o, x))
    print(f"aap/aep: {ok}/{total} frames match the hand-decoder oracle")
    for cap, ct, b, o, x in fails[:3]:
        print(f"\n  MISMATCH ({ct}) {cap} len {len(b)}:")
        od, xd = dict(o), dict(x)
        for k in list(dict.fromkeys([k for k,_ in o] + [k for k,_ in x])):
            if od.get(k,"<absent>") != xd.get(k,"<absent>"):
                print(f"    {k!r}\n      oracle: {od.get(k,'<absent>')!r}\n      engine: {xd.get(k,'<absent>')!r}")
    sys.exit(0 if ok == total else 1)
