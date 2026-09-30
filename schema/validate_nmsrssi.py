"""Validate XML engine nmsrssi BODY (byte 27+) vs the describe() hand code.
The NMS common header (appendNmsHeader) stays in C++ and is not part of this."""
import glob, sys
import engine as E

def parse(ln): return bytes(int(x, 16) for x in ln.split()[3:])
def captype(ln): return ln.split()[0].lstrip('@').split('_')[0]

def beU16(b, i): return (b[i] << 8) | b[i+1]
def be24(b, i):  return (b[i] << 16) | (b[i+1] << 8) | b[i+2]

def oracle_body(b):                    # mirrors the describe() NmsRssi body block
    o = 27
    return [
        ("stationary_kavach_id", str(beU16(b, o))),
        ("radio1_rssi_count",    str(b[o+2])),
        ("radio1_ref_rfid",      str(beU16(b, o+3))),
        ("radio1_abs_rfid",      str(be24(b, o+5))),
        ("radio1_rssi",          str(beU16(b, o+8))),
        ("radio2_rssi_count",    str(b[o+10])),
        ("radio2_ref_rfid",      str(beU16(b, o+11))),
        ("radio2_abs_rfid",      str(be24(b, o+13))),
        ("radio2_rssi",          str(beU16(b, o+16))),
    ]

if __name__ == "__main__":
    sch = E.Schema("kavach.xml")
    ok = tot = 0; fails = []
    for cap in sorted(glob.glob("../replay/*.cap")):
        for ln in open(cap):
            if captype(ln) != "nmsrssi" if ln.startswith("@") else True: continue
            b = parse(ln)
            if len(b) < 45: continue
            tot += 1
            o = oracle_body(b)
            x = [(k.strip(), v) for k, v in E.decode(b, sch, None, "nmsrssi")]
            if o == x: ok += 1
            else: fails.append((cap, b, o, x))
    print(f"nmsrssi body: {ok}/{tot} frames match")
    for cap, b, o, x in fails[:2]:
        od, xd = dict(o), dict(x)
        for k in list(dict.fromkeys([k for k,_ in o]+[k for k,_ in x])):
            if od.get(k,'<a>') != xd.get(k,'<a>'):
                print(f"    {k!r}: oracle={od.get(k,'<a>')!r} engine={xd.get(k,'<a>')!r}")
    sys.exit(0 if ok == tot else 1)
