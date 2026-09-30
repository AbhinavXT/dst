"""Validate XML engine @dmi output vs the hand-decoder oracle over real caps.

Field name + value must match exactly; the schema's uniform 2-space key indent
is stripped before comparison (same convention as every other migrated type).
Exercises the new grammar: <align/>, <flags>, <row> (zero-padded rtc + lc.id),
and the absKm / decel / sigAspect formatters.
"""
import glob, sys
import engine as E
import dmi_oracle as O

def parse(ln): return bytes(int(x, 16) for x in ln.split()[3:])
def captype(ln): return ln.split()[0].lstrip('@').split('_')[0]

if __name__ == "__main__":
    sch = E.Schema("kavach.xml")
    ok = total = 0
    fails = []
    for cap in sorted(glob.glob("../replay/*.cap")):
        for ln in open(cap, errors="ignore"):
            if not ln.startswith("@") or captype(ln) != "dmi":
                continue
            b = parse(ln)
            total += 1
            o = O.decode_dmi(b)
            x = [(k.strip(), v) for k, v in E.decode(b, sch, None, "dmi")]
            if o == x:
                ok += 1
            else:
                fails.append((cap, b, o, x))

    print(f"@dmi total: {ok}/{total}")
    for cap, b, o, x in fails[:3]:
        print(f"\n  MISMATCH in {cap}  ({len(b)} B)")
        od, xd = dict(o), dict(x)
        keys = list(dict.fromkeys([k for k, _ in o] + [k for k, _ in x]))
        for k in keys:
            ov, xv = od.get(k, "<absent>"), xd.get(k, "<absent>")
            if ov != xv:
                print(f"    {k!r}: oracle={ov!r}  engine={xv!r}")
        ko, kx = [k for k, _ in o], [k for k, _ in x]
        if ko != kx:
            print(f"    ORDER oracle={ko}")
            print(f"          engine={kx}")
    sys.exit(0 if ok == total else 1)
