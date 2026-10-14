#!/usr/bin/env python3
"""
dllog_oracle.py -- the @dllog layout as the firmware's structs give it
(session 267), independent of kavach.xml.

PrepareDataLoggerPkt memcpy's PACK structs, little-endian, no CRC:
  STRUCT_MESSAGE_HEADER_NEW  <BBHHH   src, dest, message_id, message_length, seq_num
  STRUCT_DATE_TIME_NEW       <BBBBBB  day, month, year - BASE_YEAR, hour, minute, second
  abs location               <I
  LOG_TYPE, SUB_LOG_TYPE, FIELD_APPLICABLE_FLAG  <BBB
then the sub-log type's struct (Handle_*). Widths from irs.h.

BODIES maps (log_type, sub_log_type) to (struct format, field names). Fields
the schema shows as one composed row (times) or as flag names (the LP-OCIP
alarm / context bit words) are named here by their raw parts.
"""
import struct

HEADER_FMT = "<BBHHHBBBBBBIBBB"
HEADER_NAMES = ["msg.src_id", "msg.dest_id", "msg.message_id", "msg.message_length", "msg.seq_num",
                "dd", "mo", "yy", "hh", "mi", "ss", "abs_location",
                "log_type", "sub_log_type", "field_applicable"]
HEADER_LEN = struct.calcsize(HEADER_FMT)   # 21

LOCO_SOS = ("<BIHI", ["sos_reason", "kavach_id", "ref_rfid", "dist_from_ref_rfid"])
TRIP_STOP = ("<BIHI", ["loco_reason", "loco_id", "ref_rfid", "dist_from_ref_rfid"])
BODIES = {
    # DANGEROUS
    (4, 1): ("<HBIHI", ["station_id", "signal_aspect", "signal_info", "ref_rfid", "dist_from_ref_rfid"]),
    (4, 2): ("<HIHI", ["station_id", "collision_loco_id", "self_loco_rfid", "self_loco_dist_from_rfid"]),
    (4, 3): ("<IHI", ["collision_loco_id", "ref_rfid", "dist_from_ref_rfid"]),
    (4, 4): LOCO_SOS,
    (4, 5): TRIP_STOP,
    (4, 6): TRIP_STOP,
    # alarm codes: 80 bits (71 flags + 9 pad) as 10 bytes; context values 16 bits
    (4, 7): ("<10sHBIHIBHH", ["alarm_code", "context_values", "popup_time", "popup_distance", "popup_speed",
                              "popup_loco_id", "popup_train_type", "popup_pressure", "popup_station_id"]),
    (4, 8): ("<B", ["dangerous_event"]),
    (4, 9): LOCO_SOS,
    (4, 10): LOCO_SOS,
    # ABNORMAL (1 and 15 have no handler: no body)
    (1, 1): ("<", []),
    (1, 5): ("<B", ["input_failure"]),
    (1, 6): ("<B", ["input_failure"]),
    (1, 7): ("<B", ["biu_isolated"]),
    (1, 8): ("<BBBHBBBBBHBBBHI", ["brake_applied",
                                  "sd", "sm", "sy", "sh", "sn", "ss2",
                                  "ed", "em", "ey", "eh", "en", "es",
                                  "speed", "distance"]),
    (1, 9): ("<B", ["kavach_restart"]),
    (1, 10): ("<B", ["non_leading_mode"]),
    (1, 11): ("<H", ["train_length"]),
    (1, 12): ("<BB", ["previous_mode", "current_mode"]),
    (1, 13): ("<B", ["mbt"]),
    (1, 14): ("<16s", ["tag_data"]),
    (1, 15): ("<", []),
    (1, 19): ("<HHB", ["gate_id", "gate_id_alpha", "gate_type"]),
    # PERIODIC
    (3, 1): ("<IHBHBB", ["loco_id", "speed", "train_movement_direction", "tin", "brake_type", "system_faults"]),
}


def encode(header, body):
    """header: dict of HEADER_NAMES (message_length is computed); body: dict of the body's names."""
    fmt, names = BODIES[(header["log_type"], header["sub_log_type"])]
    b = struct.pack(fmt, *[body[n] for n in names]) if names else b""
    h = dict(header, **{"msg.message_length": HEADER_LEN + len(b)})
    return struct.pack(HEADER_FMT, *[h[n] for n in HEADER_NAMES]) + b


def decode(frame):
    """(header dict, body dict) or raises ValueError."""
    if len(frame) < HEADER_LEN:
        raise ValueError(f"{len(frame)} bytes, the header is {HEADER_LEN}")
    h = dict(zip(HEADER_NAMES, struct.unpack_from(HEADER_FMT, frame)))
    key = (h["log_type"], h["sub_log_type"])
    if key not in BODIES:
        raise ValueError(f"no struct for log_type {key[0]} sub_log_type {key[1]}")
    fmt, names = BODIES[key]
    want = HEADER_LEN + struct.calcsize(fmt)
    if len(frame) != want:
        raise ValueError(f"{len(frame)} bytes, log_type {key[0]} sub {key[1]} is {want}")
    body = dict(zip(names, struct.unpack_from(fmt, frame, HEADER_LEN))) if names else {}
    return h, body
