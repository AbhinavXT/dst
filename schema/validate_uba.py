#!/usr/bin/env python3
"""
validate_uba.py -- gate for the UBA (@uba) packet schema.

There is no hand-written decoder to diff against for this type (it is
schema-only from day one), so the oracle is the STRUCT ITSELF: this script
re-reads every frame with struct.unpack against the declared C layout and
checks the XML engine reproduces it, then checks the physical invariants the
firmware's curve builder has to satisfy.

  layout    Target{double,double,u8} + CURVE_SEGMENT[9]{6 doubles} + 3 B tail
  invariant end_loc == A*lower_speed^2 + C          (the curve's own equation)
  invariant seg[k].end_loc == seg[k-1].start_loc    (curve is continuous)
  invariant higher_speed >= lower_speed

Usage:  python3 validate_uba.py [capture.log ...]
Default: every replay/*.cap|*.log carrying @uba; the synthetic fixture if none.
"""
import struct
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import engine

HERE = os.path.dirname(os.path.abspath(__file__))
XML = os.path.join(HERE, "kavach.xml")

NSEG = 9
SEG = 48
HDR = 17
FRAME = HDR + NSEG * SEG + 3          # 452

# Real captures first: any .cap/.log in replay/ that carries @uba lines. The
# synthetic fixture is used only when there is none, and the run says so --
# it proves the layout, not the firmware (see make_uba_fixture.py). Until
# session 65 the default was an absolute path to a log that was never
# committed, so this gate found no frames and failed everywhere else.
REPLAY = os.path.join(HERE, "..", "replay")
SYNTHETIC = os.path.join(HERE, "fixtures", "uba_synthetic.log")


def default_logs():
    import glob
    real = []
    for pat in ("*.cap", "*.log"):
        for p in sorted(glob.glob(os.path.join(REPLAY, pat))):
            with open(p, errors="replace") as fh:
                if any("@uba" in ln for ln in fh):
                    real.append(p)
    return (real, False) if real else ([SYNTHETIC], True)


def frames(path):
    """Yield the payload bytes of every @uba line in a capture log."""
    with open(path, errors="replace") as fh:
        for ln in fh:
            i = ln.find("@uba")
            if i < 0:
                continue
            tok = ln[i:].split()
            if len(tok) < 3:
                continue
            hexb = [t for t in tok[3:] if len(t) == 2]
            try:
                yield bytes(int(x, 16) for x in hexb)
            except ValueError:
                continue


def oracle(b):
    """Decode a frame straight from the C struct layout."""
    loc, spd = struct.unpack_from("<dd", b, 0)
    ttype = b[16]
    segs = [struct.unpack_from("<6d", b, HDR + SEG * k) for k in range(NSEG)]
    return loc, spd, ttype, segs


def check_invariants(loc, spd, ttype, segs, out):
    prev_start = None
    for k, (A, C, s, e, hi, lo) in enumerate(segs):
        if (A, C, s, e, hi, lo) == (0.0,) * 6:
            prev_start = None                      # empty slot ends the chain
            continue
        if abs(A * lo * lo + C - e) > 1e-6:
            out.append("seg%d: end_loc %.6f != A*lo^2+C %.6f" % (k, e, A * lo * lo + C))
        if prev_start is not None and abs(e - prev_start) > 1e-9:
            out.append("seg%d: end_loc %.6f != previous start_loc %.6f" % (k, e, prev_start))
        if hi < lo:
            out.append("seg%d: higher_speed %.4f < lower_speed %.4f" % (k, hi, lo))
        prev_start = s


def main():
    if sys.argv[1:]:
        logs, synthetic = sys.argv[1:], False
    else:
        logs, synthetic = default_logs()
    if synthetic:
        print("NOTE: no real @uba capture in replay/ -- running on the SYNTHETIC")
        print("      fixture. Layout parity is checked; curve invariants hold by")
        print("      construction and say nothing about the firmware.\n")
    sch = engine.Schema(XML)

    total = bad_len = bad_rows = bad_inv = 0
    problems = []
    first_rows = None

    for path in logs:
        if not os.path.exists(path):
            print("skip (not found): %s" % path)
            continue
        for b in frames(path):
            total += 1
            if len(b) != FRAME:
                bad_len += 1
                if len(problems) < 10:
                    problems.append("frame %d: %d bytes, expected %d" % (total, len(b), FRAME))
                continue

            loc, spd, ttype, segs = oracle(b)
            rows = engine.decode(b, sch, captype="uba")
            if first_rows is None:
                first_rows = rows

            # --- the engine's numbers must be the struct's numbers ---
            got = {k.strip(): v for k, v in rows}
            errs = []
            if got.get("target_location") != "%.3f m" % loc:
                errs.append("target_location %r vs %.3f" % (got.get("target_location"), loc))
            if got.get("target_speed") != engine.FLOAT_FORMATTERS["mps"](spd):
                errs.append("target_speed %r vs %.4f" % (got.get("target_speed"), spd))
            for k in range(NSEG):
                A, C, s, e, hi, lo = segs[k]
                want = "   ".join([
                    "A=" + engine.FLOAT_FORMATTERS["curveA"](A),
                    "C=%.3f m" % C,
                    "start=%.3f m" % s,
                    "end=%.3f m" % e,
                    "hi=" + engine.FLOAT_FORMATTERS["mps"](hi),
                    "lo=" + engine.FLOAT_FORMATTERS["mps"](lo),
                ])
                if got.get("seg[%d]" % k) != want:
                    errs.append("seg[%d]\n      got  %r\n      want %r"
                                % (k, got.get("seg[%d]" % k), want))
            if errs:
                bad_rows += 1
                if len(problems) < 10:
                    problems.append("frame %d: " % total + "; ".join(errs))

            inv = []
            check_invariants(loc, spd, ttype, segs, inv)
            if inv:
                bad_inv += 1
                if len(problems) < 10:
                    problems.append("frame %d: " % total + "; ".join(inv))

    print("frames            : %d" % total)
    print("wrong length      : %d" % bad_len)
    print("row mismatches    : %d" % bad_rows)
    print("invariant failures: %d" % bad_inv)
    for p in problems:
        print("  ! " + p)

    if first_rows:
        print("\nfirst frame decoded:")
        for k, v in first_rows:
            print("  %-18s %s" % (k.strip(), v))

    ok = total > 0 and not (bad_len or bad_rows or bad_inv)
    tag = " (synthetic fixture only)" if synthetic else ""
    print("\n%s" % ("PASS — %d/%d frames match%s" % (total, total, tag) if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
