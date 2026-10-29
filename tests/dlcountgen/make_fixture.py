#!/usr/bin/env python3
"""
make_fixture.py -- writes schema/fixtures/dlcount_synthetic.log, a SYNTHETIC
@dlcount capture (LKAVACH -> LDLU counter message, session 403).

One line per counter the firmware sends (isolation, SOS, OV trip), then a few
more in a plausible order, a second or more apart, each packed with
dlcount_oracle.encode from Send_Counter_Messege_To_Data_Logger. The firmware
never sets seq_num (msg_header is a stack variable): 0 here.

It proves the layout as the pasted code reads, and the window. Not the
firmware: no real @dlcount line has been captured yet, the capture token
"dlcount" is assumed, and so is CRC being a u32.

Deterministic: same bytes every run.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
sys.path.insert(0, os.path.join(ROOT, "schema"))
import dlcount_oracle as O         # noqa: E402

OUT = os.path.join(ROOT, "schema", "fixtures", "dlcount_synthetic.log")
SEQUENCE = [(0, "10:00:00"), (1, "10:00:05"), (2, "10:01:12"), (0, "10:03:40"), (1, "10:03:41"), (1, "10:07:02")]


def main():
    lines = []
    for i, (counter, hms) in enumerate(SEQUENCE):
        frame = O.encode(counter)
        lines.append(f"@dlcount_1_1 2026-10-29T{hms} {9100 + i} " + " ".join(f"{b:02X}" for b in frame))
    with open(OUT, "w", newline="\n") as fh:
        fh.write("\n".join(lines) + "\n")
    print(f"wrote {len(lines)} lines to {os.path.relpath(OUT, ROOT)}")


if __name__ == "__main__":
    main()
