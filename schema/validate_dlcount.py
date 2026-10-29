#!/usr/bin/env python3
"""
validate_dlcount.py -- gate for the DLCOUNT (@dlcount) schema (session 403).

The oracle is dlcount_oracle.py: Send_Counter_Messege_To_Data_Logger's
buffer read with struct.unpack. Every frame is decoded by the XML engine and
compared field by field; the engine's CRC must say PASS where the oracle's
crcFast matches, and every frame must be as long as the struct and its
message_length say.

Usage:  python3 validate_dlcount.py [capture.log ...]
Default: every replay/**/*.cap|*.log carrying @dlcount; the synthetic fixture
(schema/fixtures/dlcount_synthetic.log, tests/dlcountgen) if none.
"""
import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import engine                      # noqa: E402
import dlcount_oracle as O         # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
REPLAY = os.path.join(HERE, "..", "replay")
SYNTHETIC = [os.path.join(HERE, "fixtures", "dlcount_synthetic.log")]
PREFIX = "@dlcount_"


def default_logs():
    real = []
    for pat in ("**/*.cap", "**/*.log"):
        for p in sorted(glob.glob(os.path.join(REPLAY, pat), recursive=True)):
            with open(p, errors="replace") as fh:
                if any(ln.startswith(PREFIX) for ln in fh):
                    real.append(p)
    return (real, False) if real else (SYNTHETIC, True)


def check(frame, sch):
    """A list of what disagrees; empty when the engine matches the oracle."""
    try:
        o = O.decode(frame)
    except ValueError as e:
        return [str(e)]
    bad = []
    if o["msg.message_length"] != len(frame):
        bad.append(f"message_length {o['msg.message_length']} != frame {len(frame)}")
    if not o["crc_ok"]:
        bad.append("crcFast over bytes 0-8 does not match the stored CRC")
    got = {k.strip(): v for k, v in engine.decode(frame, sch, captype="dlcount")}
    for k in ("msg.src_id", "msg.dest_id", "msg.message_id", "msg.message_length", "msg.seq_num", "counter_value"):
        if got.get(k, "<absent>").split(" ")[0] != str(o[k]):
            bad.append(f"{k}: engine {got.get(k)!r}, oracle {o[k]}")
    want = ("0x%08X" % o["crc"]) + ("  PASS" if o["crc_ok"] else "  FAIL")
    if got.get("crc", "").upper() != want.upper():
        bad.append(f"crc: engine {got.get('crc')!r}, oracle {want!r}")
    if o["counter_value"] in O.COUNTERS and O.COUNTERS[o["counter_value"]] not in got.get("counter_value", ""):
        bad.append(f"counter_value {o['counter_value']}: engine label {got.get('counter_value')!r}")
    return bad


def main(argv):
    logs, synthetic = (argv, False) if argv else default_logs()
    sch = engine.Schema(os.path.join(HERE, "kavach.xml"))
    total = ok = 0
    counters = set()
    for path in logs:
        for ln in open(path, errors="replace"):
            if not ln.startswith(PREFIX):
                continue
            total += 1
            b = bytes(int(x, 16) for x in ln.split()[3:])
            bad = check(b, sch)
            if bad:
                print(f"  FAIL {os.path.basename(path)}: {'; '.join(bad)}  [{ln.strip()[:70]}]")
            else:
                ok += 1
                counters.add(b[8])
    src = "SYNTHETIC fixture (tests/dlcountgen; layout only, not firmware)" if synthetic else f"{len(logs)} capture(s)"
    print(f"@dlcount from {src}: {len(counters)} counter(s)")
    print(f"frames matching the oracle: {ok}/{total}")
    return 0 if total and ok == total else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
