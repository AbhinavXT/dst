#!/usr/bin/env python3
"""
make_fixture.py -- writes schema/fixtures/dllog_synthetic.log, a SYNTHETIC
@dllog capture (LKAVACH -> LDLU log message, session 267).

WHAT IS IN IT
  One @dllog line per sub-log type PrepareDataLoggerPkt can send (every
  Handle_* of the dangerous and abnormal tables, the two abnormal types with
  no handler, and the periodic one), between periodic packets, a second
  apart. Each is packed with dllog_oracle.encode from the firmware's structs
  (irs.h widths, PACK, little-endian). The RFID-tag packets carry REAL tags:
  @rfid frames of replay/loco_1_1_29062026_134509.cap without their reader
  id byte, which is what dl_tag_data holds.

WHAT IT PROVES, AND WHAT IT DOES NOT
  The layout as the pasted firmware code reads, and the window. Not the
  firmware: no real @dllog line has been captured yet, and the capture
  token "dllog" is assumed.

Deterministic: same bytes every run.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
sys.path.insert(0, os.path.join(ROOT, "schema"))
import dllog_oracle as O           # noqa: E402

OUT = os.path.join(ROOT, "schema", "fixtures", "dllog_synthetic.log")
RFID_SRC = os.path.join(ROOT, "replay", "loco_1_1_29062026_134509.cap")


def real_tags(n):
    """The first n distinct @rfid tags of RFID_SRC, without the reader id byte."""
    out, seen = [], set()
    for ln in open(RFID_SRC, errors="replace"):
        if not ln.startswith("@rfid_"):
            continue
        b = bytes(int(x, 16) for x in ln.split()[3:])
        if len(b) == 17 and b[1:] not in seen:
            seen.add(b[1:])
            out.append(b[1:])
            if len(out) == n:
                break
    return out


def main():
    tags = real_tags(2)
    assert len(tags) == 2, "replay capture lost its @rfid frames"
    abs_loc = 100000
    # (log_type, sub_log_type, field_applicable, body)
    sos = {"kavach_id": 1, "ref_rfid": 560, "dist_from_ref_rfid": 120}
    trip = {"loco_id": 1, "ref_rfid": 560, "dist_from_ref_rfid": 120}
    events = [
        (1, 9, 17, {"kavach_restart": 1}),
        (1, 12, 19, {"previous_mode": 1, "current_mode": 2}),
        (1, 11, 18, {"train_length": 650}),
        (1, 13, 20, {"mbt": 1}),
        (1, 14, 7, {"tag_data": tags[0]}),
        (1, 14, 7, {"tag_data": tags[1]}),
        (1, 7, 15, {"biu_isolated": 1}),
        (1, 5, 14, {"input_failure": 0x05}),
        (1, 6, 14, {"input_failure": 1}),
        (1, 10, 22, {"non_leading_mode": 1}),
        (1, 8, 4, {"brake_applied": 3,
                   "sd": 13, "sm": 10, "sy": 2026, "sh": 10, "sn": 0, "ss2": 20,
                   "ed": 13, "em": 10, "ey": 2026, "eh": 10, "en": 0, "es": 27,
                   "speed": 54, "distance": 105}),
        (1, 19, 27, {"gate_id": 61, "gate_id_alpha": 1, "gate_type": 0}),
        (1, 1, 23, {}),
        (1, 15, 23, {}),
        (4, 1, 1, {"station_id": 3001, "signal_aspect": 1, "signal_info": 0x00012345,
                   "ref_rfid": 560, "dist_from_ref_rfid": 210}),
        (4, 2, 1, {"station_id": 3001, "collision_loco_id": 2, "self_loco_rfid": 560,
                   "self_loco_dist_from_rfid": 300}),
        (4, 3, 1, {"collision_loco_id": 2, "ref_rfid": 598, "dist_from_ref_rfid": 45}),
        (4, 4, 1, dict(sos, sos_reason=1)),
        (4, 5, 1, dict(trip, loco_reason=1)),
        (4, 6, 1, dict(trip, loco_reason=2)),
        # alarm bit 17 (head-on collision with loco); context bit 10 (FSB in XX s)
        (4, 7, 24, {"alarm_code": (1 << 17).to_bytes(10, "little"),
                    "context_values": 1 << 10, "popup_time": 8, "popup_distance": 1500,
                    "popup_speed": 30, "popup_loco_id": 2, "popup_train_type": 3,
                    "popup_pressure": 350, "popup_station_id": 3001}),
        (4, 8, 1, {"dangerous_event": 8}),
        (4, 9, 1, dict(sos, sos_reason=1)),
        (4, 10, 1, dict(sos, sos_reason=2)),
    ]
    lines = []
    sec = 0
    seq = 9000

    def emit(lt, st, fa, body):
        nonlocal sec, seq
        hh, mi, ss = 10, sec // 60, sec % 60
        hdr = {"msg.src_id": 2, "msg.dest_id": 6, "msg.message_id": 102, "msg.seq_num": 0,
               "dd": 13, "mo": 10, "yy": 26, "hh": hh, "mi": mi, "ss": ss,
               "abs_location": abs_loc + sec * 10,
               "log_type": lt, "sub_log_type": st, "field_applicable": fa}
        f = O.encode(hdr, body)
        lines.append(f"@dllog_1_1 2026-10-13T{hh:02d}:{mi:02d}:{ss:02d} {seq} "
                     + " ".join(f"{x:02X}" for x in f))
        seq += 1

    periodic = lambda: emit(3, 1, 2, {"loco_id": 1, "speed": min(sec, 60), "train_movement_direction": 1,
                                       "tin": 21, "brake_type": 0, "system_faults": 0})
    for ev in events:
        periodic()
        emit(*ev)
        sec += 1
    periodic()
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", newline="\n") as fh:
        fh.write("\n".join(lines) + "\n")
    print(f"wrote {len(lines)} lines to {os.path.relpath(OUT, ROOT)}")


if __name__ == "__main__":
    main()
