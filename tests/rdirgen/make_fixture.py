#!/usr/bin/env python3
"""
make_fixture.py -- writes schema/fixtures/rdir_synthetic.log, a SYNTHETIC
@rdir capture (READER_INFO, session 194) for the RDSO direction test cases
FRS 18.1 - 18.7, one after another.

WHAT IS IN IT
  @rdir   one per tag read: reader_id, tag_id (LE), reader_dir, movement_dir,
          from a simulation of LKAVACH v1.2.9's DetermineTrainDirectionReader1/2
          (rfid_reader1.c): a reader decides N / R from its second distinct
          tag on (this tag's location against its last one); the loco's
          movement_dir takes the first reader's decision while undefined.
  @arp    after each step of a test's input table: a REAL ARP of loco 1 in
          SR mode (replay/2026-10-08, 10:16:06) with only LAST_RFID_TAG set to
          the tag the RDSO table says is reported to SVK (or kept), and its
          JAMCRC recomputed, so it decodes and passes like a real one.

WHAT IT PROVES, AND WHAT IT DOES NOT
  The layout and the window. Not the firmware: the reader logic is this
  script's reading of rfid_reader1.c, and the tag reported to SVK is the
  RDSO table's, scripted. (Simulated as written, the seven tables come out
  row for row; tests/test_session194.cpp checks the window says so.)

Tags: R1..R4 = tag ids 101..104 at 100000 / 100100 / 100200 / 100300 m.
Deterministic: same bytes every run.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
sys.path.insert(0, os.path.join(ROOT, "schema"))
import engine                      # noqa: E402
from crc_algos import jamcrc       # noqa: E402

OUT = os.path.join(ROOT, "schema", "fixtures", "rdir_synthetic.log")
SRC = os.path.join(ROOT, "replay", "2026-10-08", "loco_1_1_08102026_101300.cap")
SCH = engine.Schema(os.path.join(ROOT, "schema", "kavach.xml"))

TAG = {1: 101, 2: 102, 3: 103, 4: 104}
LOC = {101: 100000, 102: 100100, 103: 100200, 104: 100300}
U, N, R = 0, 1, 2

# Each case: its input table, row by row: (reader-1 reads, reader-2 reads),
# a tag number or None; and the tag the RDSO table says is reported to SVK
# after that row (None: blank).
CASES = [
    ("18.1", [((1, 1), 1), ((2, 2), 2), ((3, 3), 3)]),
    ("18.2", [((1, None), 1), ((2, 2), 2), ((3, 3), 3)]),
    ("18.3", [((None, 1), 1), ((2, 2), 2), ((3, 3), 3)]),
    ("18.4", [((2, None), 2), ((None, 1), 1), ((None, 2), 2), ((3, 3), 3)]),
    ("18.5", [((1, None), 1), ((None, 2), 2), ((3, None), 3), ((None, 3), None)]),
    ("18.6", [((1, None), 1), ((2, 1), 2), ((3, 2), 3), ((None, 3), None)]),
    ("18.7", [((1, None), 1), ((2, None), 2), ((3, 1), 3), ((None, 2), None), ((None, 3), None),
              ((None, 4), 4), ((4, None), None)]),
]


def real_arp():
    """A real SR-mode ARP of loco 1 (LOCO_MODE 2), as bytes."""
    for ln in open(SRC, errors="replace"):
        tok = ln.split()
        if not tok or not tok[0].startswith("@arp_1_1"):
            continue
        b = bytes(int(x, 16) for x in tok[3:])
        rows = {n.strip(): v for n, v in engine.decode(b, SCH, captype="arp")}
        if rows.get("LOCO_MODE", "").startswith("2 ") and "PASS" in rows.get("CRC", "PASS"):
            return bytearray(b)
    raise SystemExit("no SR-mode ARP in " + SRC)


def get_bits(b, off, n):
    v = 0
    for i in range(n):
        v = (v << 1) | ((b[(off + i) // 8] >> (7 - (off + i) % 8)) & 1)
    return v


def set_bits(b, off, n, v):
    for i in range(n):
        bit = (v >> (n - 1 - i)) & 1
        byte, sh = (off + i) // 8, 7 - (off + i) % 8
        b[byte] = (b[byte] & ~(1 << sh)) | (bit << sh)


def field_offset(arp):
    """Where LAST_RFID_TAG is (10 bits, msb-first): the one place where writing
    a value makes the engine read it back and changes no other field."""
    base = {n.strip(): v for n, v in engine.decode(bytes(arp), SCH, captype="arp")}
    for off in range(80, len(arp) * 8 - 10):
        t = bytearray(arp)
        set_bits(t, off, 10, 0x2A5)
        rows = {n.strip(): v for n, v in engine.decode(bytes(t), SCH, captype="arp")}
        if rows.get("LAST_RFID_TAG") != str(0x2A5):
            continue
        if all(rows.get(k) == v for k, v in base.items() if k not in ("LAST_RFID_TAG",) and "CRC" not in k.upper()):
            return off
    raise SystemExit("LAST_RFID_TAG not found")


def with_tag(arp, off, tag):
    t = bytearray(arp)
    set_bits(t, off, 10, tag)
    hdr, pkt_len = 10, get_bits(t, 80 + 4, 7)        # PKT_TYPE 4 bits, then PKT_LENGTH 7
    crc = jamcrc(bytes(t), hdr, pkt_len - 4)
    t[hdr + pkt_len - 4: hdr + pkt_len] = crc.to_bytes(4, "big")
    return bytes(t)


def main():
    arp = real_arp()
    off = field_offset(arp)
    lines, seq, t = [], 7000, 10 * 3600

    def stamp(sec):
        return "2026-10-09T%02d:%02d:%02d" % (sec // 3600, sec // 60 % 60, sec % 60)

    def emit(token, sec, payload):
        nonlocal seq
        lines.append("@%s_1_1 %s %d %s" % (token, stamp(sec), seq, " ".join("%02X" % x for x in payload)))
        seq += 1

    for name, steps in CASES:
        readers = {1: {"count": 0, "last": None, "dir": U}, 2: {"count": 0, "last": None, "dir": U}}
        movement = U
        reported = 0
        for (r1, r2), rep in steps:
            for reader, tagno in ((1, r1), (2, r2)):
                if tagno is None:
                    continue
                tag = TAG[tagno]
                st = readers[reader]
                if st["last"] != tag:                     # a duplicate is not a new read
                    st["count"] += 1
                    if st["count"] >= 2:
                        st["dir"] = N if LOC[tag] >= LOC[st["last"]] else R
                        if movement == U:
                            movement = st["dir"]
                    st["last"] = tag
                emit("rdir", t, bytes([reader, tag & 0xFF, tag >> 8, st["dir"], movement]))
                t += 1
            if rep is not None:
                reported = TAG[rep]
            emit("arp", t, with_tag(arp, off, reported))
            t += 3
        t += 60                                            # the next test case, a minute later
    with open(OUT, "w") as fh:
        fh.write("\n".join(lines) + "\n")
    print("%s: %d lines, LAST_RFID_TAG at bit %d" % (OUT, len(lines), off))


if __name__ == "__main__":
    main()
