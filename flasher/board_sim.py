#!/usr/bin/env python3
"""
board_sim.py - stand-in for the AM2634 updater, for testing the PC side.

Speaks the same UDP protocol (META / DATA / POLL / STATUS). Models the things
that make a real board drop packets:
  * a small receive ring  (socket SO_RCVBUF)  -> overruns when the PC is too fast
  * per-packet processing time (--proc-us)    -> MCU copy + flash write cost
  * random loss (--loss)                      -> link noise
  * first META lost (ARP resolution on a real link)

Usage: python3 board_sim.py [--port 50001] [--proc-us 600] [--loss 0.003] [--rcvbuf 24000]
                            [--v26] [--reject REASON]

--v26 answers HOLD (0x07) and BOOT (0x08) with an INFO reply (0x05, layout
v3, 136 bytes), as updater 2.2+ does; without it HOLD and BOOT are ignored,
like an older updater. --reject N (with --v26) answers a verified image with
STATUS REJECTED (0x04) and reason byte N instead of COMPLETE.
"""
import argparse
import hashlib
import random
import socket
import struct
import time
import zlib

BLOCK = 1450
META, DATA, POLL, STATUS = 0, 1, 2, 3
INFO, HOLD, BOOT = 5, 7, 8
COMPLETE, INCOMPLETE, NEED_META, IMAGE_FAIL, REJECTED = 0, 1, 2, 3, 4


def crc_fw(data: bytes) -> int:
    """Reflected CRC-32, init 0, xorout 0 (what the firmware / PC use)."""
    return zlib.crc32(data, 0xFFFFFFFF) ^ 0xFFFFFFFF


def spin(us: float):
    end = time.perf_counter() + us / 1e6
    while time.perf_counter() < end:
        pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=50001)
    ap.add_argument("--proc-us", type=float, default=600.0)
    ap.add_argument("--loss", type=float, default=0.003)
    ap.add_argument("--rcvbuf", type=int, default=24000)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--v26", action="store_true")
    ap.add_argument("--reject", type=int, default=0)
    a = ap.parse_args()
    random.seed(a.seed)

    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, a.rcvbuf)
    s.bind(("127.0.0.1", a.port))
    print(f"sim: port {a.port} proc {a.proc_us}us loss {a.loss} "
          f"rcvbuf {s.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF)}", flush=True)

    session = None          # dict once META accepted
    first_meta_dropped = False

    while True:
        pkt, addr = s.recvfrom(4096)
        if random.random() < a.loss:
            continue
        kind = pkt[0]

        if a.v26 and kind in (HOLD, BOOT):
            info = bytearray(136)
            info[0], info[1] = INFO, 3
            s.sendto(bytes(info), addr)
            if kind == BOOT:
                print("sim: BOOT - leaving the updater", flush=True)
                session = None
            continue

        if kind == META:
            if not first_meta_dropped:
                first_meta_dropped = True
                continue
            _, card, size, crc = struct.unpack_from("<BBII", pkt)
            sha = pkt[10:42]
            if session is None or session["crc"] != crc:
                nblk = (size + BLOCK - 1) // BLOCK
                session = dict(card=card, size=size, crc=crc, sha=sha, nblk=nblk,
                               buf=bytearray(size), have=bytearray((nblk + 7) // 8),
                               got=0, state=INCOMPLETE)
                spin(2000)  # erase / setup cost

        elif kind == DATA and session is not None:
            spin(a.proc_us)
            _, off, size, ccrc = struct.unpack_from("<BIII", pkt)
            payload = pkt[13:13 + size]
            if len(payload) != size or crc_fw(payload) != ccrc:
                continue
            blk = off // BLOCK
            if blk >= session["nblk"]:
                continue
            if not (session["have"][blk >> 3] >> (blk & 7)) & 1:
                session["have"][blk >> 3] |= 1 << (blk & 7)
                session["got"] += 1
                session["buf"][off:off + size] = payload

        elif kind == POLL:
            if session is None:
                s.sendto(struct.pack("<BBIIIH", STATUS, NEED_META, 0, 0, 0, 0), addr)
                continue
            if session["got"] == session["nblk"] and session["state"] == INCOMPLETE:
                ok = (crc_fw(bytes(session["buf"])) == session["crc"] and
                      hashlib.sha256(session["buf"]).digest() == session["sha"])
                session["state"] = COMPLETE if ok else IMAGE_FAIL
                if ok and a.v26 and a.reject:
                    session["state"] = REJECTED
            if session["state"] == REJECTED:
                s.sendto(struct.pack("<BBIIIHB", STATUS, REJECTED, session["crc"],
                                     session["nblk"], session["got"], 0, a.reject), addr)
                continue
            hdr = struct.pack("<BBIIIH", STATUS, session["state"], session["crc"],
                              session["nblk"], session["got"], len(session["have"]))
            s.sendto(hdr + bytes(session["have"]), addr)


if __name__ == "__main__":
    main()
