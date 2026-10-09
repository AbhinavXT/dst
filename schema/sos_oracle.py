#!/usr/bin/env python3
"""
sos_oracle.py -- @sos / @sossrc / @sosev straight from their byte layouts.

The layouts are SOS_handoff/01_SOS_LOGGING_PACKETS.md, version 1: every field
written byte by byte, little-endian, by SOS_LogPut* in the firmware. This file
reads them with struct.unpack at the README's offsets, independently of
kavach.xml, so validate_sos.py can check the XML engine against it.
"""
import struct

THREAT = {0: "none", 1: "head-on", 2: "rear-end", 3: "unusual stop", 4: "manual SoS",
          5: "train parted", 6: "station general SoS", 7: "DEST_LOCO_SOS general",
          8: "station head-on", 9: "station rear-end", 10: "SPAD", 11: "shunting limit"}


def snapshot(b):
    """@sos: 65 B + 20 B per station slot."""
    f = {}
    (f["version"], f["why"], f["snap_id"], f["tick_ms"], f["own_abs_loc_dm"], f["own_dir"],
     f["own_mode"], f["own_tin"], f["own_train_length_m"], f["own_speed_x100"], f["own_ma_m"],
     f["own_station_id"], f["own_rfid_tag_id"], f["rear_tag_found"], f["rear_sect_type"],
     f["own_emergency_status"], f["access_req_recvd_stn_id"], f["n_src_slots"],
     f["src_in_use_mask"], f["src_active_mask"], f["agg_threat"], f["agg_station_won"],
     f["collision_loco_id"], f["agg_station_id"], f["collision_distance_dm"],
     f["sos_distance_dm"], f["sos_station_id"], f["lp_flags"], f["dmi_bits"],
     f["last_dest_loco_sos"], f["self_flags"], f["n_stn_slots"]) = struct.unpack_from(
        "<BBIIiBBHHHiHHBBBHBHHBBIHiiHHHBBB", b, 0)
    stations = []
    for i in range(f["n_stn_slots"]):
        o = 65 + 20 * i
        s = dict(zip(("in_use", "flags", "station_id", "age_ms", "station_abs_loc_m",
                      "distance_dm", "sos_distance_dm"),
                     struct.unpack_from("<BBHIIii", b, o)))
        stations.append(s)
    f["stations"] = stations
    return f


def snapshot_v2(b):
    """@sos version 2 (README 03, the minimal layout): 37 B + 24 per loco + 13 per station."""
    f = {}
    (f["version"], f["tick_ms"], f["own_abs_loc_dm"], f["own_dir"], f["own_mode"], f["own_tin"],
     f["own_speed_x100"], f["own_emergency_status"], f["collision_loco_id"], f["collision_distance_dm"],
     f["sos_distance_dm"], f["sos_station_id"], f["lp_flags"], f["dmi_bits"], f["self_flags"],
     f["n_src"], f["n_stn"]) = struct.unpack_from("<BIiBBHHBIiiHHHBBB", b, 0)
    o = 37
    locos = []
    for _ in range(f["n_src"]):
        locos.append(dict(zip(("loco_id", "age_ds", "abs_loc_m", "dir", "tin", "train_length_m",
                               "sos_distance_dm", "collision_distance_dm", "threats"),
                              struct.unpack_from("<IHiBHHiiB", b, o))))
        o += 24
    stations = []
    for _ in range(f["n_stn"]):
        stations.append(dict(zip(("station_id", "add_em", "age_ds", "abs_loc_m", "sos_distance_dm"),
                                 struct.unpack_from("<HBHIi", b, o))))
        o += 13
    f["locos"], f["stations"], f["size"] = locos, stations, o
    return f


def source(b):
    """@sossrc: 56 B."""
    keys = ("version", "snap_id", "slot_index", "source_loco_id", "age_ms", "last_frame_no",
            "last_emergency_sts", "other_mode", "other_speed", "raw_abs_loc_m", "raw_dir",
            "last_rfid_id", "approaching_station_id", "abs_loc_m", "movement_dir", "tin",
            "train_length_m", "distance_dm", "sos_distance_dm", "collision_distance_dm",
            "threat_flags", "closest_flags", "eval_flags")
    return dict(zip(keys, struct.unpack_from("<BIBIIIBBHiBHHiBHHiiiBBH", b, 0)))


def event(b):
    """@sosev: 27 B."""
    keys = ("version", "tick_ms", "snap_id", "event", "aux1", "aux2", "id", "distance_dm",
            "own_abs_loc_dm", "own_speed_x100", "own_mode")
    return dict(zip(keys, struct.unpack_from("<BIIBBBIiiHB", b, 0)))


SIZES = {"sos": None, "sossrc": 56, "sosev": 27}


def frames(path):
    """(token, bytes) for every @sos/@sossrc/@sosev line of a capture."""
    with open(path, errors="replace") as fh:
        for ln in fh:
            if not ln.startswith("@sos"):
                continue
            tok = ln.split()
            tag = tok[0][1:].split("_")
            token = "_".join(tag[:-2])
            if token not in SIZES:
                continue
            yield token, bytes(int(x, 16) for x in tok[3:])
