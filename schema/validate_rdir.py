#!/usr/bin/env python3
"""
validate_rdir.py -- gate for the READER_INFO (@rdir) schema (session 194).

The oracle is the layout itself: reader_id u8, tag_id u16 LE, reader_dir u8,
movement_dir u8 (5 bytes), read with struct.unpack. Every frame is decoded by
the XML engine and compared field by field; every frame must be 5 bytes.

Usage:  python3 validate_rdir.py [capture.log ...]
Default: every replay/**/*.cap|*.log carrying @rdir; the synthetic fixture
(schema/fixtures/rdir_synthetic.log, tests/rdirgen) if none.
"""
import glob
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import engine

HERE = os.path.dirname(os.path.abspath(__file__))
REPLAY = os.path.join(HERE, "..", "replay")
SYNTHETIC = [os.path.join(HERE, "fixtures", "rdir_synthetic.log")]


def default_logs():
    real = []
    for pat in ("**/*.cap", "**/*.log"):
        for p in sorted(glob.glob(os.path.join(REPLAY, pat), recursive=True)):
            with open(p, errors="replace") as fh:
                if any(ln.startswith("@rdir_") for ln in fh):
                    real.append(p)
    return (real, False) if real else (SYNTHETIC, True)


def main(argv):
    logs, synthetic = (argv, False) if argv else default_logs()
    sch = engine.Schema(os.path.join(HERE, "kavach.xml"))
    total = ok = 0
    for path in logs:
        for ln in open(path, errors="replace"):
            if not ln.startswith("@rdir_"):
                continue
            total += 1
            b = bytes(int(x, 16) for x in ln.split()[3:])
            if len(b) != 5:
                print(f"  FAIL {os.path.basename(path)}: {len(b)} bytes, expected 5: {ln.strip()[:60]}")
                continue
            reader, tag, rdir, mdir = struct.unpack("<BHBB", b)
            got = {k.strip(): v for k, v in engine.decode(b, sch, captype="rdir")}
            want = {"reader_id": reader, "tag_id": tag, "reader_dir": rdir, "movement_dir": mdir}
            bad = [k for k, v in want.items() if got.get(k, "").split(" ")[0] != str(v)]
            if bad:
                print(f"  FAIL {os.path.basename(path)}: {bad} engine {got}, oracle {want}")
            else:
                ok += 1
    src = "SYNTHETIC fixture (tests/rdirgen; layout only, not firmware)" if synthetic else f"{len(logs)} capture(s)"
    print(f"@rdir from {src}")
    print(f"frames matching the oracle: {ok}/{total}")
    return 0 if total and ok == total else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
