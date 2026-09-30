"""Validate XML engine @rfid output vs the hand-decoder oracle over real caps.
Field name + value must match exactly; key indent is ignored (the schema emits a
uniform 2-space indent, like every other migrated type).

Coverage note: types 9 / 11 / 12 and the null/unknown tag have real frames and
are golden-validated here. Type 10 (LC gate) has NO frames in any capture, so it
is encoded in kavach.xml from the hand decoder but cannot be validated.
"""
import glob, sys, collections
import engine as E
import rfid_oracle as O

def parse(ln): return bytes(int(x, 16) for x in ln.split()[3:])
def captype(ln): return ln.split()[0].lstrip('@').split('_')[0]

if __name__ == "__main__":
    sch = E.Schema("kavach.xml")
    ok = total = 0
    by_type = collections.Counter()         # tag type -> count seen
    type_ok = collections.Counter()
    fails = []
    for cap in sorted(glob.glob("../replay/*.cap")):
        for ln in open(cap, errors="ignore"):
            if not ln.startswith("@") or captype(ln) != "rfid":
                continue
            b = parse(ln)
            total += 1
            ty = (b[1] & 0x0F) if len(b) >= 2 else -1
            by_type[ty] += 1
            o = O.decodeRfid(b)
            x = [(k.strip(), v) for k, v in E.decode(b, sch, None, "rfid")]
            if o == x:
                ok += 1; type_ok[ty] += 1
            else:
                fails.append((cap, b, ty, o, x))

    print(f"@rfid total: {ok}/{total}")
    for ty in sorted(by_type):
        name = O.rfidTagType(ty)
        tag = "" if type_ok[ty] == by_type[ty] else "  <-- MISMATCH"
        print(f"   type {ty:>2} ({name}): {type_ok[ty]}/{by_type[ty]}{tag}")

    for cap, b, ty, o, x in fails[:3]:
        print(f"\n  MISMATCH type {ty} in {cap}  ({len(b)} B)")
        print("   ", " ".join("%02X" % c for c in b))
        od, xd = dict(o), dict(x)
        keys = list(dict.fromkeys([k for k, _ in o] + [k for k, _ in x]))
        for k in keys:
            ov, xv = od.get(k, "<absent>"), xd.get(k, "<absent>")
            if ov != xv:
                print(f"    {k!r}: oracle={ov!r}  engine={xv!r}")
        if [k for k, _ in o] != [k for k, _ in x]:
            print(f"    ORDER oracle={[k for k,_ in o]}")
            print(f"          engine={[k for k,_ in x]}")
    sys.exit(0 if ok == total else 1)
