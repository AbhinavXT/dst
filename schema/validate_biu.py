#!/usr/bin/env python3
"""
validate_biu.py -- gate for the @biu_* schema packets (session 420).

The oracle is biu_oracle.py: Annexure-1 2.2's frames read with struct and bit
masks, as the firmware's biu_encode_command / take_biu_frame read them. Every
frame is decoded by the XML engine and compared row by row, names and text.

Usage:  python3 validate_biu.py [capture.log ...]
Default: every replay/**/*.cap|*.log carrying @biu_*; the synthetic fixture
(schema/fixtures/biu_synthetic.log, tests/biugen: the firmware module's own
copies) if none.
"""
import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import engine                      # noqa: E402
import biu_oracle as O             # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
REPLAY = os.path.join(HERE, "..", "replay")
SYNTHETIC = [os.path.join(HERE, "fixtures", "biu_synthetic.log")]


def token_of(line):
    """'biu_cmd' for '@biu_cmd_1_1 ...', None for any other line."""
    if not line.startswith("@biu_"):
        return None
    tag = line.split(" ", 1)[0][1:].split("_")
    tok = "_".join(tag[:-2])
    return tok if tok in O.SIZES else None


def default_logs():
    real = []
    for pat in ("**/*.cap", "**/*.log"):
        for p in sorted(glob.glob(os.path.join(REPLAY, pat), recursive=True)):
            with open(p, errors="replace") as fh:
                if any(token_of(ln) for ln in fh):
                    real.append(p)
    return (real, False) if real else (SYNTHETIC, True)


def check(token, frame, sch):
    try:
        want = O.decode(token, frame)
    except ValueError as e:
        return [str(e)]
    got = {k.strip(): v for k, v in engine.decode(frame, sch, captype=token)}
    bad = []
    if set(got) != set(want):
        bad.append(f"rows: engine {sorted(set(got) - set(want))} extra, {sorted(set(want) - set(got))} missing")
    for k, v in want.items():
        if k in got and got[k] != v:
            bad.append(f"{k}: engine {got[k]!r}, oracle {v!r}")
    return bad


def main(argv):
    logs, synthetic = (argv, False) if argv else default_logs()
    sch = engine.Schema(os.path.join(HERE, "kavach.xml"))
    total = ok = 0
    seen = {}
    for path in logs:
        for ln in open(path, errors="replace"):
            tok = token_of(ln)
            if not tok:
                continue
            total += 1
            b = bytes(int(x, 16) for x in ln.split()[3:])
            bad = check(tok, b, sch)
            if bad:
                print(f"  FAIL {os.path.basename(path)}: {'; '.join(bad)}  [{ln.strip()[:70]}]")
            else:
                ok += 1
                seen[tok] = seen.get(tok, 0) + 1
    src = "SYNTHETIC fixture (tests/biugen: the firmware module's copies, the BIU's frames spec-derived)" \
        if synthetic else f"{len(logs)} capture(s)"
    print(f"@biu_* from {src}: " + ", ".join(f"{k} {v}" for k, v in sorted(seen.items())))
    missing = sorted(set(O.SIZES) - set(seen))
    if missing:
        print(f"  FAIL no {', '.join(missing)} frame checked")
    print(f"frames matching the oracle: {ok}/{total}")
    return 0 if total and ok == total and not missing else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
