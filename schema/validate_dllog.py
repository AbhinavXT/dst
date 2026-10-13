#!/usr/bin/env python3
"""
validate_dllog.py -- gate for the DLLOG (@dllog) schema (session 267).

The oracle is dllog_oracle.py: the firmware's structs read with
struct.unpack. Every frame is decoded by the XML engine and compared field by
field: plain fields by value, the composed times by their parts, the LP-OCIP
alarm / context words by the flag names their set bits give, and the RFID
tag (abnormal 14) by decoding it as an @rfid frame and checking its CRC-30.
Every frame must be as long as its struct and its message_length say.

Usage:  python3 validate_dllog.py [capture.log ...]
Default: every replay/**/*.cap|*.log carrying @dllog; the synthetic fixture
(schema/fixtures/dllog_synthetic.log, tests/dlloggen) if none.
"""
import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import engine                      # noqa: E402
import dllog_oracle as O           # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
REPLAY = os.path.join(HERE, "..", "replay")
SYNTHETIC = [os.path.join(HERE, "fixtures", "dllog_synthetic.log")]
PREFIXES = ("@dllog_", "@dl_log_")


def default_logs():
    real = []
    for pat in ("**/*.cap", "**/*.log"):
        for p in sorted(glob.glob(os.path.join(REPLAY, pat), recursive=True)):
            with open(p, errors="replace") as fh:
                if any(ln.startswith(PREFIXES) for ln in fh):
                    real.append(p)
    return (real, False) if real else (SYNTHETIC, True)


def flag_names(sch, table, value, nbits):
    names = sch.flagtables.get(table, [])
    on = [names[i] for i in range(nbits) if (value >> i) & 1 and i < len(names)]
    return "; ".join(on) if on else "(none)"


def check(frame, sch):
    """A list of what disagrees; empty when the engine matches the oracle."""
    try:
        h, body = O.decode(frame)
    except ValueError as e:
        return [str(e)]
    bad = []
    if h["msg.message_length"] != len(frame):
        bad.append(f"message_length {h['msg.message_length']} != frame {len(frame)}")
    got = {k.strip(): v for k, v in engine.decode(frame, sch, captype="dllog")}
    first = lambda k: got.get(k, "<absent>").split(" ")[0]
    for k, v in h.items():
        if k in ("dd", "mo", "yy", "hh", "mi", "ss"):
            continue
        if first(k) != str(v):
            bad.append(f"{k}: engine {got.get(k)!r}, oracle {v}")
    want_time = "%02d-%02d-%02d %02d:%02d:%02d" % (h["dd"], h["mo"], h["yy"], h["hh"], h["mi"], h["ss"])
    if got.get("log_time") != want_time:
        bad.append(f"log_time: engine {got.get('log_time')!r}, oracle {want_time}")
    for k, v in body.items():
        if k == "alarm_code":
            want = flag_names(sch, "dmiAlarm", int.from_bytes(v, "little"), 71)
            if got.get(k) != want:
                bad.append(f"alarm_code: engine {got.get(k)!r}, oracle {want!r}")
        elif k == "context_values":
            want = flag_names(sch, "dmiCtx", v, 14)
            if got.get(k) != want:
                bad.append(f"context_values: engine {got.get(k)!r}, oracle {want!r}")
        elif k == "tag_data":
            rf = {a.strip(): b for a, b in engine.decode(b"\x00" + v, sch, captype="rfid")}
            if not rf.get("CRC-30", "").endswith("PASS"):
                bad.append(f"tag_data: CRC-30 {rf.get('CRC-30')!r} as an @rfid frame")
        elif k in ("sd", "sm", "sy", "sh", "sn", "ss2", "ed", "em", "ey", "eh", "en", "es"):
            continue
        elif first(k) != str(v):
            bad.append(f"{k}: engine {got.get(k)!r}, oracle {v}")
    if "sd" in body:
        for row, p in (("start_time", ("sd", "sm", "sy", "sh", "sn", "ss2")),
                       ("end_time", ("ed", "em", "ey", "eh", "en", "es"))):
            d, m, y, hh, mi, ss = (body[x] for x in p)
            want = "%02d-%02d-%d %02d:%02d:%02d" % (d, m, y, hh, mi, ss)
            if got.get(row) != want:
                bad.append(f"{row}: engine {got.get(row)!r}, oracle {want}")
    return bad


def main(argv):
    logs, synthetic = (argv, False) if argv else default_logs()
    sch = engine.Schema(os.path.join(HERE, "kavach.xml"))
    total = ok = 0
    kinds = set()
    for path in logs:
        for ln in open(path, errors="replace"):
            if not ln.startswith(PREFIXES):
                continue
            total += 1
            b = bytes(int(x, 16) for x in ln.split()[3:])
            bad = check(b, sch)
            if bad:
                print(f"  FAIL {os.path.basename(path)}: {'; '.join(bad)}  [{ln.strip()[:70]}]")
            else:
                ok += 1
                kinds.add((b[18], b[19]))
    src = "SYNTHETIC fixture (tests/dlloggen; layout only, not firmware)" if synthetic else f"{len(logs)} capture(s)"
    print(f"@dllog from {src}: {len(kinds)} log/sub-log types")
    print(f"frames matching the oracle: {ok}/{total}")
    return 0 if total and ok == total else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
