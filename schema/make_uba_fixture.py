#!/usr/bin/env python3
"""
make_uba_fixture.py -- write fixtures/uba_synthetic.log, a SYNTHETIC @uba capture.

WHY THIS EXISTS
  validate_uba.py was written against 81_2_15092026_121737.log, which was never
  committed; from session 64 on it found no frames and failed. Until that log
  (or any real @uba capture) is placed in replay/, this fixture keeps the
  LAYOUT check honest: the engine must read every double, the target type and
  the tail exactly where the C struct puts them.

WHAT IT DOES NOT PROVE
  The curve invariants (end_loc == A*lo^2 + C, continuity, hi >= lo) hold here
  because this script constructs them to hold. On synthetic data they test the
  checker, not the firmware. Only a real capture tests the firmware.

Deterministic: same bytes every run, so the fixture can be diffed.
"""
import os, struct

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "fixtures", "uba_synthetic.log")
NSEG = 9

def frame(target_loc, target_speed, ttype, decels, step):
    """Braking curve to target_loc, one segment per deceleration, each spanning
    `step` m/s of speed. x(v) = C - v^2 / (2a)  ->  A = -1 / (2a)."""
    segs = []
    lo = target_speed
    prev_start = target_loc
    for a in decels:
        A = -1.0 / (2.0 * a)
        hi = lo + step
        C = prev_start - A * lo * lo          # end_loc (at lo) == previous start
        start = A * hi * hi + C
        segs.append((A, C, start, prev_start, hi, lo))
        prev_start, lo = start, hi
    segs += [(0.0,) * 6] * (NSEG - len(segs))  # unused slots are all-zero
    b = struct.pack("<ddB", target_loc, target_speed, ttype)
    for s in segs:
        b += struct.pack("<6d", *s)
    return b + b"\x00\x00\x00"

def main():
    cases = [
        (6248.0, 0.0,  1, [1.0, 0.9376, 0.95], 5.0),   # EOA-like, 3 steps
        (5200.0, 8.33, 2, [0.9, 0.6],          4.0),   # PSR-like, non-zero target speed
        (6900.0, 0.0,  3, [1.0] * NSEG,        3.0),   # every slot used
        (100.5,  2.5,  0, [0.75],              2.5),   # single segment
    ]
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as fh:
        fh.write("# SYNTHETIC @uba frames from make_uba_fixture.py -- layout check only,\n")
        fh.write("# not firmware evidence. Put a real capture in replay/ for that.\n")
        for i, c in enumerate(cases * 5):
            b = frame(*c)
            assert len(b) == 452, len(b)
            fh.write("@uba_1_1 2026-09-23T12:00:%02d %d %s\n"
                     % (i, 1000 + i, " ".join("%02X" % x for x in b)))
    print("wrote", OUT)

if __name__ == "__main__":
    main()
