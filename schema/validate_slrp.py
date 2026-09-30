"""
validate_slrp.py -- prove the XML engine reproduces the validated kschema
decoder row-for-row on real captures.

For every @slrp frame (pkt_type 9) in ../replay/*.cap, decode with BOTH:
  - kschema_oracle.decodeSlrp  (faithful port of the validated C++ tables)
  - engine.decode               (the generic XML-driven decoder)
and diff the rows. Two modes:
  A) no tagLoc      -> structural / field / enum / condition parity
  B) full tagLoc    -> also exercises abs-location roles (seglen/start/len/tag)

Any mismatch is printed with the offending rows. Exit non-zero on failure.
"""
import glob, sys
import engine as E
import kschema_oracle as O

def parse(ln):
    return bytes(int(x, 16) for x in ln.split()[3:])

def collect_slrp():
    frames = []
    for cap in sorted(glob.glob("../replay/*.cap")):
        for ln in open(cap):
            if not ln.startswith("@slrp"): continue
            b = parse(ln)
            if len(b) >= 12 and ((b[0] >> 4) & 0xF) == 9:
                frames.append((cap, b))
    return frames

def header_ref(b):
    # LAST_REF_RFID is bits [60..69] msb-first (after 4+10+17+16+3+20+4 = 74? recompute)
    c = O.BitCursor(b, 0, len(b) * 8)
    for f in O.HDR_F:
        v = c.takeS(f[1]) if f[2] in (O.K_S, O.K_SMet) else c.take(f[1])
        if f[0] == "LAST_REF_RFID": return v
    return 0

def diff(cap, b, tagLoc, label, failures):
    sch = SCH
    o = O.decodeSlrp(b, tagLoc)
    x = E.decode(b, sch, tagLoc, "slrp")
    if o == x:
        return True
    failures.append((cap, label, b, o, x))
    return False

if __name__ == "__main__":
    SCH = E.Schema("kavach.xml")
    frames = collect_slrp()
    print(f"collected {len(frames)} SLRP frames from ../replay/*.cap")

    # build a deterministic tagLoc covering every ref so abs-loc roles fire
    refs = sorted({header_ref(b) for _, b in frames})
    tagLoc = {r: 100000 + r * 100 for r in refs}
    tagLoc[989] = 162660            # documented golden ref

    total = 0; ok = 0
    for mode, tl in (("A no-tagLoc", None), ("B abs-loc", tagLoc)):
        failures = []
        for cap, b in frames:
            total += 1
            if diff(cap, b, tl, mode, failures): ok += 1
        n = len(frames)
        print(f"[{mode}] {n - len(failures)}/{n} frames match")
        for cap, lbl, b, o, x in failures[:3]:
            print(f"\n  MISMATCH ({lbl}) in {cap}, frame len {len(b)}:")
            od = dict(o); xd = dict(x)
            keys = list(dict.fromkeys([k for k, _ in o] + [k for k, _ in x]))
            for k in keys:
                ov = od.get(k, "<absent>"); xv = xd.get(k, "<absent>")
                if ov != xv:
                    print(f"    {k!r}\n        oracle: {ov!r}\n        engine: {xv!r}")

    print(f"\nTOTAL {ok}/{total} decodes identical across both modes")
    sys.exit(0 if ok == total else 1)
